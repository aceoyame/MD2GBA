#include <stdint.h>
#include "md2gba_compiler.h"

#define REG16(a) (*(volatile uint16_t*)(a))
#define REG32(a) (*(volatile uint32_t*)(a))
#define REG_DISPSTAT REG16(0x04000004u)
#define REG_VCOUNT   REG16(0x04000006u)
#define REG_DISPCNT  REG16(0x04000000u)
#define REG_KEYINPUT REG16(0x04000130u)
#define REG_KEYCNT   REG16(0x04000132u)
#define REG_BG0HOFS REG16(0x04000010u)
#define REG_BG0VOFS REG16(0x04000012u)
#define REG_BG1HOFS REG16(0x04000014u)
#define REG_BG1VOFS REG16(0x04000016u)
#define REG_BG2HOFS REG16(0x04000018u)
#define REG_BG2VOFS REG16(0x0400001Au)
#define REG_BG3HOFS REG16(0x0400001Cu)
#define REG_BG3VOFS REG16(0x0400001Eu)
#define REG_DMA0SAD REG32(0x040000B0u)
#define REG_DMA0DAD REG32(0x040000B4u)
#define REG_DMA0CNT REG32(0x040000B8u)
#define REG_IE      REG16(0x04000200u)
#define REG_IF      REG16(0x04000202u)
#define REG_IME     REG16(0x04000208u)
#define IRQ_VECTOR  REG32(0x03007FFCu)
#define IRQ_VBLANK  0x0001u
#define IRQ_TIMER1  0x0010u
#define IRQ_KEYPAD  0x1000u

extern void md2gba_gba_pcm_irq_handler(void);
#if !defined(MD2GBA_PSC_M4A_AUDIO)
extern void md2gba_gba_pcm_audio_irq_service(uint16_t pending);
#endif
extern void md2gba_gba_hw_vblank_commit_pending(void);
extern void md2gba_gba_projected_vblank_commit_pending(void);
#if defined(MD2GBA_PSC_M4A_AUDIO)
extern void md2gba_psc_m4a_vsync(void);
#endif

/* Written by the renderer.  The physical VBlank IRQ rewinds DMA0 every GBA
   frame; HBlank-repeat DMA does not reload its source address by itself. */
const uint16_t * volatile md2gba_hblank_scroll_source;
volatile uint8_t md2gba_hblank_scroll_enabled;
/* Latest whole-plane camera state is independent of the deferred VRAM queue. */
static volatile uint16_t md2gba_direct_scroll_latch[8];
static volatile uint8_t md2gba_direct_scroll_valid;
/* P105b: direct whole-plane registers persist in hardware. Reapply at the
   next VBlank only when a newly published latch must win over older queued work. */
static volatile uint8_t md2gba_direct_scroll_pending;
/* P75: translated PSIV can take far longer than one physical frame.  Capture
   physical key presses asynchronously so a normal tap is not lost before the
   next emulated controller poll. Bits are active-high GBA key bits. */
static volatile uint16_t md2gba_key_press_latch;

/* P80: software-rendered Mode-4 frames are presented by physical VBlank IRQ
   instead of blocking the translated CPU in a foreground wait loop. */
static volatile uint8_t md2gba_soft_flip_pending;
static volatile uint8_t md2gba_soft_flip_page;

static void soft_apply_page(unsigned page) {
    uint16_t d=REG_DISPCNT;
    d=(uint16_t)((d&~0x0017u)|4u|((page&1u)<<4)|(1u<<10));
    REG_DISPCNT=d;
}

void md2gba_gba_soft_request_page_flip(unsigned page) {
    uint16_t old=REG_IME;
    REG_IME=0u;
    if(REG_VCOUNT>=160u){soft_apply_page(page);md2gba_soft_flip_pending=0u;}
    else {md2gba_soft_flip_page=(uint8_t)(page&1u);md2gba_soft_flip_pending=1u;}
    REG_IME=old;
}
int md2gba_gba_soft_page_flip_pending(void) { return md2gba_soft_flip_pending!=0u; }
void md2gba_gba_soft_cancel_page_flip(void) { uint16_t old=REG_IME;REG_IME=0u;md2gba_soft_flip_pending=0u;REG_IME=old; }
unsigned md2gba_gba_soft_visible_page(void) { return (unsigned)((REG_DISPCNT>>4)&1u); }

__attribute__((section(".iwram"),noinline)) void md2gba_gba_scroll_latch_direct(const uint16_t scroll[8]) {
    uint16_t old; unsigned i;
    if(!scroll)return;
    old=REG_IME;REG_IME=0u;
    /* P134S: restore P130-style redundant whole-plane publication suppression.
       A valid direct latch is authoritative until a raster request or explicit
       clear invalidates it, so identical camera state needs no DMA teardown,
       eight MMIO writes, or another pending VBlank publication. */
    if(md2gba_direct_scroll_valid){
        for(i=0u;i<8u;i++)if(md2gba_direct_scroll_latch[i]!=scroll[i])break;
        if(i==8u){REG_IME=old;return;}
    }
    /* Whole-plane scroll supersedes any earlier row/cell HBlank stream
       immediately.  Otherwise a pending VRAM transaction could leave DMA0
       replaying stale offsets and make the fresh camera latch invisible. */
    REG_DMA0CNT=0u;
    md2gba_hblank_scroll_source=(const uint16_t*)0;
    md2gba_hblank_scroll_enabled=0u;
    for(i=0u;i<8u;i++)md2gba_direct_scroll_latch[i]=scroll[i];
    md2gba_direct_scroll_valid=1u;
    md2gba_direct_scroll_pending=1u;
    /* P89: whole-plane camera state is already translated into GBA units.
       Publish it immediately instead of waiting for physical VBlank.  BG
       scroll registers are double-buffer-free hardware state and are safe to
       update during active display; VBlank reapplies the same latch for a
       coherent frame boundary.  This removes the last deferred seam that let
       a slow guest frame display a stationary camera for multiple host frames. */
    REG_BG0HOFS=scroll[0];REG_BG0VOFS=scroll[1];
    REG_BG1HOFS=scroll[2];REG_BG1VOFS=scroll[3];
    REG_BG2HOFS=scroll[4];REG_BG2VOFS=scroll[5];
    REG_BG3HOFS=scroll[6];REG_BG3VOFS=scroll[7];
    REG_IME=old;
}

void md2gba_gba_scroll_latch_clear(void) {
    uint16_t old=REG_IME;REG_IME=0u;
    md2gba_direct_scroll_valid=0u;md2gba_direct_scroll_pending=0u;
    REG_IME=old;
}

static void apply_direct_scroll_latch(void) {
    if(!md2gba_direct_scroll_valid||!md2gba_direct_scroll_pending)return;
    /* The latch is the newest whole-plane presentation request. If an older
       raster/HBlank stream is still armed, retire it atomically at VBlank so
       stale DMA cannot overwrite the freshly published camera offsets. A newer
       raster request clears md2gba_direct_scroll_valid before this point. */
    REG_DMA0CNT=0u;
    md2gba_hblank_scroll_enabled=0u;
    md2gba_hblank_scroll_source=(const uint16_t*)0;
    REG_BG0HOFS=md2gba_direct_scroll_latch[0];REG_BG0VOFS=md2gba_direct_scroll_latch[1];
    REG_BG1HOFS=md2gba_direct_scroll_latch[2];REG_BG1VOFS=md2gba_direct_scroll_latch[3];
    REG_BG2HOFS=md2gba_direct_scroll_latch[4];REG_BG2VOFS=md2gba_direct_scroll_latch[5];
    REG_BG3HOFS=md2gba_direct_scroll_latch[6];REG_BG3VOFS=md2gba_direct_scroll_latch[7];
    md2gba_direct_scroll_pending=0u;
}

static __attribute__((always_inline)) inline void restart_scroll_dma(void) {
    const uint16_t *p=(const uint16_t*)md2gba_hblank_scroll_source;
    REG_DMA0CNT=0u;
    if(!md2gba_hblank_scroll_enabled || !p) return;
    REG_BG0HOFS=p[0]; REG_BG0VOFS=p[1];
    REG_BG1HOFS=p[2]; REG_BG1VOFS=p[3];
    REG_BG2HOFS=p[4]; REG_BG2VOFS=p[5];
    REG_BG3HOFS=p[6]; REG_BG3VOFS=p[7];
    REG_DMA0SAD=(uint32_t)(uintptr_t)(p+8);
    REG_DMA0DAD=0x04000010u;
    /* 8 halfwords, dest increment/reload, repeat, HBlank start, enabled. */
    REG_DMA0CNT=(8u)|(3u<<21)|(1u<<25)|(2u<<28)|(1u<<31);
}

void md2gba_gba_vblank_irq_install(void) {
    REG_IME=0u;
    IRQ_VECTOR=(uint32_t)(uintptr_t)md2gba_gba_pcm_irq_handler;
    REG_DISPSTAT=(uint16_t)(REG_DISPSTAT|0x0008u); /* VBlank IRQ enable. */
    /* Any selected key, OR mode, keypad IRQ enabled. */
    REG_KEYCNT=(uint16_t)(0x03FFu|(1u<<14));
    REG_IF=(uint16_t)(IRQ_VBLANK|IRQ_KEYPAD);
    REG_IE=(uint16_t)(REG_IE|IRQ_VBLANK|IRQ_KEYPAD);
    REG_IME=1u;
    { uint32_t cpsr; __asm__ volatile("mrs %0, cpsr\n\tbic %0, %0, #0x80\n\tmsr cpsr_c, %0" : "=&r"(cpsr) :: "memory"); }
}

/* Shared physical IRQ body: service the proven Timer1 FIFO pump, then the
   P68/P75 VBlank scroll rewind and asynchronous keypad latch. */
__attribute__((section(".iwram"),target("arm"),minsize))
void md2gba_gba_pcm_irq_body(void) {
    uint16_t pending=(uint16_t)(REG_IF&REG_IE);
#if !defined(MD2GBA_PSC_M4A_AUDIO)
    if(pending&IRQ_TIMER1)md2gba_gba_pcm_audio_irq_service(pending);
#endif
    if(pending&IRQ_VBLANK){
#if defined(MD2GBA_PSC_M4A_AUDIO)
        md2gba_psc_m4a_vsync();
#endif
        uint16_t pressed=(uint16_t)((~REG_KEYINPUT)&0x03FFu);
        if(md2gba_soft_flip_pending){soft_apply_page(md2gba_soft_flip_page);md2gba_soft_flip_pending=0u;}
        md2gba_gba_hw_vblank_commit_pending();
        md2gba_gba_projected_vblank_commit_pending();
        apply_direct_scroll_latch();
        md2gba_key_press_latch|=pressed;
        /* A keypad request is level-like while its selected condition remains
           true on real hardware/emulators. Re-arm it only after all keys have
           been released so one held key cannot turn into an IRQ storm. */
        if(!pressed)REG_KEYCNT=(uint16_t)(0x03FFu|(1u<<14));
        restart_scroll_dma();
    }
    if(pending&IRQ_KEYPAD){
        md2gba_key_press_latch|=(uint16_t)((~REG_KEYINPUT)&0x03FFu);
        REG_KEYCNT=0x03FFu; /* inhibit until a later physical VBlank sees release */
    }
    REG_IF=pending;
}

uint16_t md2gba_gba_take_key_press_latch(void){
    uint16_t old_ime=REG_IME,v;
    REG_IME=0u;v=md2gba_key_press_latch;md2gba_key_press_latch=0u;REG_IME=old_ime;
    return v;
}

void md2gba_gba_scroll_dma_set(const uint16_t stream[160][8], int enabled) {
    md2gba_hblank_scroll_source=enabled ? &stream[0][0] : (const uint16_t*)0;
    /* Pointer payload first, publication flag second. */
    MD2GBA_COMPILER_BARRIER();
    md2gba_hblank_scroll_enabled=enabled ? 1u : 0u;
    if(enabled){md2gba_direct_scroll_valid=0u;md2gba_direct_scroll_pending=0u;}
    restart_scroll_dma();
}
