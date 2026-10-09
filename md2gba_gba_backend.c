#include "md2gba_gba_backend.h"
#include "md2gba_compiler.h"
#if defined(MD2GBA_COMPILED_VDP)
#include "md2gba_vdp_plan.h"
#endif

#define REG_DISPCNT (*(volatile uint16_t*)0x04000000u)
#define REG_BG0CNT  (*(volatile uint16_t*)0x04000008u)
#define REG_BG1CNT  (*(volatile uint16_t*)0x0400000Au)
#define REG_BG0HOFS (*(volatile uint16_t*)0x04000010u)
#define REG_BG0VOFS (*(volatile uint16_t*)0x04000012u)
#define REG_BG1HOFS (*(volatile uint16_t*)0x04000014u)
#define REG_BG1VOFS (*(volatile uint16_t*)0x04000016u)
#define REG_BG2CNT  (*(volatile uint16_t*)0x0400000Cu)
#define REG_BG3CNT  (*(volatile uint16_t*)0x0400000Eu)
#define REG_BG2HOFS (*(volatile uint16_t*)0x04000018u)
#define REG_BG2VOFS (*(volatile uint16_t*)0x0400001Au)
#define REG_BG3HOFS (*(volatile uint16_t*)0x0400001Cu)
#define REG_BG3VOFS (*(volatile uint16_t*)0x0400001Eu)
#define REG_DMA0SAD (*(volatile uint32_t*)0x040000B0u)
#define REG_DMA0DAD (*(volatile uint32_t*)0x040000B4u)
#define REG_DMA0CNT (*(volatile uint32_t*)0x040000B8u)
#define REG_KEYINPUT (*(volatile uint16_t*)0x04000130u)
#define REG_VCOUNT  (*(volatile uint16_t*)0x04000006u)
#define REG_DMA3SAD (*(volatile uint32_t*)0x040000D4u)
#define REG_DMA3DAD (*(volatile uint32_t*)0x040000D8u)
#define REG_DMA3CNT (*(volatile uint32_t*)0x040000DCu)
#define REG_IME     (*(volatile uint16_t*)0x04000208u)
#define GBA_BG_PALETTE ((volatile uint16_t*)0x05000000u)
#define GBA_OBJ_PALETTE ((volatile uint16_t*)0x05000200u)
#define GBA_OBJ_VRAM16  ((volatile uint16_t*)0x06010000u)
#define GBA_OAM32       ((volatile uint32_t*)0x07000000u)
#define GBA_VRAM16_BYTES ((volatile uint16_t*)0x06000000u)
#define GBA_VRAM16      ((volatile uint16_t*)0x06000000u)

extern void md2gba_gba_scroll_dma_set(const uint16_t stream[160][8], int enabled);
extern void md2gba_gba_scroll_latch_direct(const uint16_t scroll[8]);

static void gba_oam_write(unsigned obj,uint16_t a0,uint16_t a1,uint16_t a2) {
    GBA_OAM32[obj*2u]=(uint32_t)a0|((uint32_t)a1<<16);
    GBA_OAM32[obj*2u+1u]=(uint32_t)a2;
}

uint16_t md2gba_gba_color_from_genesis(uint16_t c) {
    /* P84: exact 3->5 expansion as a tiny title-agnostic LUT.  Palette commits
       are bursty and can touch both BG/OBJ copies, so avoid repeating the bit
       expansion arithmetic for every component. */
    static const uint8_t c3_to_c5[8]={0u,4u,8u,12u,17u,21u,25u,29u};
    unsigned r=c3_to_c5[(c>>1)&7u],g=c3_to_c5[(c>>5)&7u],b=c3_to_c5[(c>>9)&7u];
    return (uint16_t)(r|(g<<5)|(b<<10));
}

uint8_t md2gba_gba_tile_byte_from_genesis(uint8_t p) {
    /* Both machines use 4bpp 8x8/32-byte tiles, but the packed-pixel nibble
       order is opposite. Swapping nibbles converts two pixels at once. */
    return (uint8_t)((p<<4)|(p>>4));
}


/* P72 coherent BG pattern staging.  The active text-BG tile cache occupies
   32 KiB of VRAM.  Rewriting it while the previous map is being scanned out
   caused the cutscene strip/noise corruption seen in P70/P71.  Convert the
   next cache into EWRAM first, then DMA it into VRAM during physical VBlank
   immediately before the inactive map bank becomes visible. */
uint16_t md2gba_bg_tile_stage[1024u * 16u] __attribute__((aligned(4)));


#define MD2GBA_ROM_COLD __attribute__((section(".rom.cold"),noinline))
#if defined(MD2GBA_GBA_IWRAM)
#define MD2GBA_BACKEND_HOT __attribute__((section(".iwram.backend"),noinline))
#else
#define MD2GBA_BACKEND_HOT
#endif

/* P82 deferred small-hardware commit queue.  P72's coherent hardware renderer
   waited synchronously for physical GBA VBlank before every visible update.
   At sub-60-fps emulation that wait can add almost another complete 16.7 ms
   frame.  Small map/tile/palette/full-screen-scroll updates are instead copied
   into EWRAM here and consumed by the physical VBlank IRQ.  Large/full-map and
   sprite rebuilds retain the conservative synchronous path. */
#define MD2GBA_HWQ_MAX_TILES 256u
#define MD2GBA_HWQ_MAX_MAP_WRITES 512u
typedef struct Md2GbaHwCommitQueue {
    volatile uint8_t pending;
    uint8_t palette_valid,scroll_valid,sprite_valid,sprite_bank;
    /* P120: full-map rebuilds are prepared into the inactive screenblock bank
       and EWRAM tile staging buffer, then committed by the physical VBlank IRQ.
       This removes the old foreground gba_wait_vblank_start() stall. */
    uint8_t full_tiles_valid,display_valid;
    uint16_t tile_count,map_write_count;
    uint16_t full_tile_halfwords;
    uint16_t dispcnt,bgcnt[4];
    uint16_t palette[64];
    uint32_t palette_mask[2];
    uint16_t scroll[8];
    uint16_t oam[128][4];
    /* Tile pixels already live in md2gba_bg_tile_stage.  While a queue is
       pending the foreground renderer refuses to prepare another hardware
       frame, so retaining only slot IDs avoids duplicating ~2 KiB of EWRAM. */
    uint16_t tile_slot[MD2GBA_HWQ_MAX_TILES];
    uint16_t map_index[MD2GBA_HWQ_MAX_MAP_WRITES];
    uint16_t map_value[MD2GBA_HWQ_MAX_MAP_WRITES];
} Md2GbaHwCommitQueue;
static Md2GbaHwCommitQueue md2gba_hwq __attribute__((aligned(4)));
static uint8_t md2gba_obj_active_bank;
static uint8_t md2gba_obj_doublebuffer_safe;
static MD2GBA_ROM_COLD int md2gba_hwq_stage_sprites(const Md2GbaVdp*v,unsigned ox,unsigned oy);
static void gba_dma3_copy16(volatile uint16_t *dst,const uint16_t *src,unsigned halfwords);

MD2GBA_ROM_COLD int md2gba_gba_hw_commit_pending(void){return md2gba_hwq.pending!=0u;}
MD2GBA_ROM_COLD void md2gba_gba_hw_cancel_pending(void){uint16_t old=REG_IME;REG_IME=0u;md2gba_hwq.pending=0u;REG_IME=old;}

MD2GBA_BACKEND_HOT void md2gba_gba_hw_vblank_commit_pending(void){
    unsigned i,j;
    if(!md2gba_hwq.pending)return;
    /* pending is the acquire point for the foreground-produced payload. */
    MD2GBA_COMPILER_BARRIER();
    if(md2gba_hwq.palette_valid){
        if(md2gba_hwq.palette_mask[0]==0xFFFFFFFFu&&md2gba_hwq.palette_mask[1]==0xFFFFFFFFu){
            /* P105 full-CRAM fade: two short DMA bursts are substantially
               cheaper than 128 individual palette halfword stores. */
            gba_dma3_copy16(GBA_BG_PALETTE,md2gba_hwq.palette,64u);
            gba_dma3_copy16(GBA_OBJ_PALETTE,md2gba_hwq.palette,64u);
        }else for(i=0u;i<64u;i++){if(!(md2gba_hwq.palette_mask[i>>5]&(1u<<(i&31u))))continue;GBA_BG_PALETTE[i]=md2gba_hwq.palette[i];GBA_OBJ_PALETTE[i]=md2gba_hwq.palette[i];}
    }
    /* P120 ASYNCFULL: full pattern caches are already coherent in EWRAM.
       DMA them once during physical VBlank rather than making foreground 68K
       execution wait for the next physical VBlank before it can continue. */
    if(md2gba_hwq.full_tiles_valid && md2gba_hwq.full_tile_halfwords)
        gba_dma3_copy16(GBA_VRAM16_BYTES,md2gba_bg_tile_stage,md2gba_hwq.full_tile_halfwords);
    /* A24/P128: adjacent staged pattern slots are one contiguous VRAM run.
       Coalesce them into one DMA instead of paying setup/loop overhead per tile. */
    for(i=0u;i<md2gba_hwq.tile_count;){
        unsigned slot=md2gba_hwq.tile_slot[i],run=1u;
        while(i+run<md2gba_hwq.tile_count && md2gba_hwq.tile_slot[i+run]==slot+run)run++;
        if(run>1u)gba_dma3_copy16(GBA_VRAM16_BYTES+slot*16u,md2gba_bg_tile_stage+slot*16u,run*16u);
        else {volatile uint16_t*dst=GBA_VRAM16_BYTES+slot*16u;const uint16_t*src=md2gba_bg_tile_stage+slot*16u;for(j=0u;j<16u;j++)dst[j]=src[j];}
        i+=run;
    }
    for(i=0u;i<md2gba_hwq.map_write_count;i++)GBA_VRAM16[md2gba_hwq.map_index[i]]=md2gba_hwq.map_value[i];
    if(md2gba_hwq.sprite_valid){
        /* A30/P128: the queue already stores the exact 1 KiB GBA OAM image.
           DMA it as one VBlank burst rather than issuing 256 CPU stores. */
        gba_dma3_copy16((volatile uint16_t*)(uintptr_t)GBA_OAM32,&md2gba_hwq.oam[0][0],128u*4u);
        md2gba_obj_active_bank=md2gba_hwq.sprite_bank;
        md2gba_obj_doublebuffer_safe=1u;
        REG_DISPCNT|=(uint16_t)((1u<<12)|(1u<<6));
    }
    if(md2gba_hwq.scroll_valid){
        REG_DMA0CNT=0u;
        REG_BG0HOFS=md2gba_hwq.scroll[0];REG_BG0VOFS=md2gba_hwq.scroll[1];
        REG_BG1HOFS=md2gba_hwq.scroll[2];REG_BG1VOFS=md2gba_hwq.scroll[3];
        REG_BG2HOFS=md2gba_hwq.scroll[4];REG_BG2VOFS=md2gba_hwq.scroll[5];
        REG_BG3HOFS=md2gba_hwq.scroll[6];REG_BG3VOFS=md2gba_hwq.scroll[7];
        md2gba_gba_scroll_dma_set((const uint16_t (*)[8])0,0);
    }
    /* Switch to the newly prepared inactive map bank only after all pattern,
       palette, OAM and scroll state for that frame is resident. */
    if(md2gba_hwq.display_valid){
        REG_BG0CNT=md2gba_hwq.bgcnt[0];REG_BG1CNT=md2gba_hwq.bgcnt[1];
        REG_BG2CNT=md2gba_hwq.bgcnt[2];REG_BG3CNT=md2gba_hwq.bgcnt[3];
        REG_DISPCNT=md2gba_hwq.dispcnt;
    }
    /* Do not let payload reads move below the release of queue ownership. */
    MD2GBA_COMPILER_BARRIER();
    md2gba_hwq.pending=0u;
}

static void gba_wait_vblank_start(void) {
    while(REG_VCOUNT >= 160u) {}
    while(REG_VCOUNT < 160u) {}
}

static void gba_dma3_copy16(volatile uint16_t *dst,const uint16_t *src,unsigned halfwords) {
    if(!halfwords) return;
    REG_DMA3CNT=0u;
    REG_DMA3SAD=(uint32_t)(uintptr_t)src;
    REG_DMA3DAD=(uint32_t)(uintptr_t)dst;
    /* Staging writes must remain above the volatile DMA-enable store. */
    MD2GBA_COMPILER_BARRIER();
    REG_DMA3CNT=(uint32_t)(halfwords & 0xFFFFu) | (1u<<31);
    while(REG_DMA3CNT & (1u<<31)) {}
}

static void gba_convert_tile32_to_ram(uint16_t *dst,const uint8_t *src) {
    typedef uint32_t AliasWord __attribute__((may_alias));
    const AliasWord *in=(const AliasWord*)(const void*)src;
    AliasWord *out=(AliasWord*)(void*)dst;
    for(unsigned i=0u;i<8u;i++){uint32_t q=in[i];out[i]=((q&0x0f0f0f0fu)<<4)|((q&0xf0f0f0f0u)>>4);}

}

/* GBA VRAM is a 16-bit port.  Byte stores do not behave like ordinary RAM
   stores, so all tile uploads are packed into aligned halfword writes. */
static void gba_copy_tile32_to_vram(volatile uint16_t *dst,const uint8_t *src) {
    /* B36/P128: source tiles and OBJ destinations are 32-byte aligned.  Convert
       four Genesis bytes at a time and use eight aligned 32-bit VRAM stores. */
    typedef uint32_t AliasWord __attribute__((may_alias));
    const AliasWord *in=(const AliasWord*)(const void*)src;
    volatile uint32_t *out=(volatile uint32_t*)(void*)dst;
    for(unsigned i=0u;i<8u;i++){uint32_t q=in[i];out[i]=((q&0x0f0f0f0fu)<<4)|((q&0xf0f0f0f0u)>>4);}
}

uint16_t md2gba_gba_name_word_from_genesis(uint16_t m) {
    /* Mega Drive: P CC V H TTTTTTTTTTT
       GBA text BG: CCCC V H TTTTTTTTTT
       Priority has no direct per-cell equivalent on GBA text BGs, so it is
       intentionally dropped here; plane/sprite priority is handled elsewhere. */
    uint16_t tile=(uint16_t)(m&0x03FFu);       /* GBA text BG has 10 tile bits. */
    uint16_t h=(uint16_t)((m&0x0800u)>>1);    /* MD bit11 -> GBA bit10 */
    uint16_t v=(uint16_t)((m&0x1000u)>>1);    /* MD bit12 -> GBA bit11 */
    uint16_t pal=(uint16_t)((m&0x6000u)>>1);  /* MD bits13-14 -> GBA bits12-13 */
    return (uint16_t)(tile|h|v|pal);
}

void md2gba_gba_commit_cram(const Md2GbaVdp *vdp) {
    /* PSC converts Genesis CRAM at source-write time.  Keep the same split:
       the source words remain in cram[], while presentation only copies the
       already-normalized GBA shadow. */
    for(unsigned i=0;i<64;i++){uint16_t c=vdp->cram_gba[i];GBA_BG_PALETTE[i]=c;GBA_OBJ_PALETTE[i]=c;}
}

void md2gba_gba_commit_vram_tiles(const Md2GbaVdp *vdp, uint16_t md_vram_addr,
                                  unsigned first_gba_tile, unsigned tile_count) {
    unsigned t;
    for(t=0;t<tile_count;t++) {
        unsigned src=((unsigned)md_vram_addr+t*32u)&0xFFFFu;
        gba_copy_tile32_to_vram(GBA_VRAM16_BYTES+(first_gba_tile+t)*16u,&vdp->vram[src]);
    }
}

void md2gba_gba_commit_name_table(const Md2GbaVdp *vdp, uint16_t md_vram_addr,
                                  unsigned gba_screenblock, unsigned width, unsigned height) {
    if(gba_screenblock>31u) return;
    if(width>32u) width=32u;
    if(height>32u) height=32u;
    volatile uint16_t *dst=GBA_VRAM16 + gba_screenblock*1024u;
    for(unsigned y=0;y<height;y++) {
        for(unsigned x=0;x<width;x++) {
            unsigned a=(unsigned)md_vram_addr + 2u*(y*width+x);
            uint16_t m=(uint16_t)(((uint16_t)vdp->vram[(uint16_t)a]<<8)|vdp->vram[(uint16_t)(a+1u)]);
            dst[y*32u+x]=md2gba_gba_name_word_from_genesis(m);
        }
    }
}

void md2gba_gba_init_display(void) {
    /* Mode 0, BG0 text background. Charblock 0, screenblock 31, 32x32. */
    REG_DISPCNT=(uint16_t)((0u<<0) | (1u<<8));
    REG_BG0CNT=(uint16_t)(31u<<8);
    REG_BG0HOFS=0; REG_BG0VOFS=0;
}

void md2gba_gba_set_bg0_scroll(uint16_t x, uint16_t y) {
    REG_BG0HOFS=x; REG_BG0VOFS=y;
}


void md2gba_gba_init_display_64x32(unsigned first_screenblock) {
    /* GBA text-BG size 1 = 512x256 (64x32 cells), arranged as two
       consecutive 32x32 screenblocks horizontally. */
    if(first_screenblock > 30u) first_screenblock=30u;
    REG_DISPCNT=(uint16_t)((0u<<0) | (1u<<8));
    REG_BG0CNT=(uint16_t)((first_screenblock<<8) | (1u<<14));
    REG_BG0HOFS=0; REG_BG0VOFS=0;
}

void md2gba_gba_commit_name_table_64x32(const Md2GbaVdp *vdp, uint16_t md_vram_addr,
                                        unsigned first_screenblock) {
    if(first_screenblock>30u) return;
    volatile uint16_t *left=GBA_VRAM16 + first_screenblock*1024u;
    volatile uint16_t *right=left+1024u;
    for(unsigned y=0;y<32u;y++) {
        for(unsigned x=0;x<64u;x++) {
            unsigned a=(unsigned)md_vram_addr + 2u*(y*64u+x);
            uint16_t m=(uint16_t)(((uint16_t)vdp->vram[(uint16_t)a]<<8)|vdp->vram[(uint16_t)(a+1u)]);
            volatile uint16_t *dst=(x<32u)?left:right;
            dst[y*32u+(x&31u)]=md2gba_gba_name_word_from_genesis(m);
        }
    }
}


uint16_t md2gba_vdp_plane_a_base(const Md2GbaVdp *vdp) {
    return (uint16_t)((vdp->regs[2] & 0x38u) << 10);
}

uint16_t md2gba_vdp_plane_b_base(const Md2GbaVdp *vdp) {
    return (uint16_t)((vdp->regs[4] & 0x07u) << 13);
}

unsigned md2gba_vdp_max_tile_64x32(const Md2GbaVdp *vdp, uint16_t base) {
    unsigned max_tile=0;
    for(unsigned y=0;y<32u;y++) {
        for(unsigned x=0;x<64u;x++) {
            unsigned a=(unsigned)base + 2u*(y*64u+x);
            uint16_t m=(uint16_t)(((uint16_t)vdp->vram[(uint16_t)a]<<8)|vdp->vram[(uint16_t)(a+1u)]);
            unsigned t=m&0x07FFu;
            if(t>max_tile) max_tile=t;
        }
    }
    return max_tile;
}

void md2gba_gba_init_display_dual_64x32(unsigned a_sb,unsigned b_sb) {
    if(a_sb>30u) a_sb=28u;
    if(b_sb>30u) b_sb=30u;
    /* Mode 0, BG0 (Plane A) above BG1 (Plane B), both 512x256 text BGs. */
    REG_DISPCNT=(uint16_t)((1u<<8)|(1u<<9));
    REG_BG0CNT=(uint16_t)((0u<<0)|(0u<<2)|(a_sb<<8)|(1u<<14));
    REG_BG1CNT=(uint16_t)((1u<<0)|(0u<<2)|(b_sb<<8)|(1u<<14));
    REG_BG0HOFS=REG_BG0VOFS=REG_BG1HOFS=REG_BG1VOFS=0;
}

void md2gba_gba_commit_vdp_planes_64x32(const Md2GbaVdp *vdp,unsigned a_sb,unsigned b_sb) {
    uint16_t a=md2gba_vdp_plane_a_base(vdp), b=md2gba_vdp_plane_b_base(vdp);
    unsigned ma=md2gba_vdp_max_tile_64x32(vdp,a), mb=md2gba_vdp_max_tile_64x32(vdp,b);
    unsigned max_tile=ma>mb?ma:mb;
    /* Current text-BG backend can address tiles 0..1023. Games exceeding this
       are handled later by the generic tile-cache/remapper, not a title hack. */
    if(max_tile>1023u) max_tile=1023u;
    md2gba_gba_commit_vram_tiles(vdp,0u,0u,max_tile+1u);
    md2gba_gba_commit_name_table_64x32(vdp,a,a_sb);
    md2gba_gba_commit_name_table_64x32(vdp,b,b_sb);
}

void md2gba_gba_set_bg_scroll(unsigned bg,uint16_t x,uint16_t y) {
    if(bg==0u){REG_BG0HOFS=x;REG_BG0VOFS=y;}
    else if(bg==1u){REG_BG1HOFS=x;REG_BG1VOFS=y;}
}


static unsigned md2gba_vdp_size_cells(unsigned code) {
    switch(code & 3u) {
        case 0u: return 32u;
        case 1u: return 64u;
        case 3u: return 128u;
        default: return 32u; /* VDP code 2 is prohibited/reserved. */
    }
}

unsigned md2gba_vdp_plane_width(const Md2GbaVdp *vdp) {
    return md2gba_vdp_size_cells(vdp->regs[16] & 3u);
}

unsigned md2gba_vdp_plane_height(const Md2GbaVdp *vdp) {
    return md2gba_vdp_size_cells((vdp->regs[16] >> 4) & 3u);
}

unsigned md2gba_vdp_max_tile(const Md2GbaVdp *vdp,uint16_t base,unsigned width,unsigned height) {
    unsigned max_tile=0;
    for(unsigned y=0;y<height;y++) for(unsigned x=0;x<width;x++) {
        unsigned a=(unsigned)base+2u*(y*width+x);
        uint16_t m=(uint16_t)(((uint16_t)vdp->vram[(uint16_t)a]<<8)|vdp->vram[(uint16_t)(a+1u)]);
        unsigned t=m&0x07FFu;if(t>max_tile)max_tile=t;
    }
    return max_tile;
}

static unsigned gba_bg_size_code(unsigned width,unsigned height) {
    if(width==32u&&height==32u)return 0u;
    if(width==64u&&height==32u)return 1u;
    if(width==32u&&height==64u)return 2u;
    if(width==64u&&height==64u)return 3u;
    return 0xFFFFFFFFu;
}

static unsigned gba_screenblocks_needed(unsigned width,unsigned height) {
    return (width/32u)*(height/32u);
}

int md2gba_gba_init_display_dual_vdp(const Md2GbaVdp *vdp,unsigned a_sb,unsigned b_sb) {
    unsigned w=md2gba_vdp_plane_width(vdp),h=md2gba_vdp_plane_height(vdp);
    unsigned size=gba_bg_size_code(w,h),need;
    if(size==0xFFFFFFFFu)return -1;
    need=gba_screenblocks_needed(w,h);
    if(a_sb+need>32u||b_sb+need>32u)return -2;
    REG_DISPCNT=(uint16_t)((1u<<8)|(1u<<9));
    REG_BG0CNT=(uint16_t)((0u<<0)|(0u<<2)|(a_sb<<8)|(size<<14));
    REG_BG1CNT=(uint16_t)((1u<<0)|(0u<<2)|(b_sb<<8)|(size<<14));
    REG_BG0HOFS=REG_BG0VOFS=REG_BG1HOFS=REG_BG1VOFS=0;
    return 0;
}

int md2gba_gba_commit_name_table_geometry(const Md2GbaVdp *vdp,uint16_t base,unsigned first_sb,unsigned width,unsigned height) {
    unsigned bx,by,blocks_x,blocks_y;
    if((width!=32u&&width!=64u)||(height!=32u&&height!=64u))return -1;
    blocks_x=width/32u;blocks_y=height/32u;
    if(first_sb+blocks_x*blocks_y>32u)return -2;
    for(unsigned y=0;y<height;y++) for(unsigned x=0;x<width;x++) {
        unsigned a=(unsigned)base+2u*(y*width+x);
        uint16_t m=(uint16_t)(((uint16_t)vdp->vram[(uint16_t)a]<<8)|vdp->vram[(uint16_t)(a+1u)]);
        bx=x>>5;by=y>>5;
        unsigned sb=first_sb+by*blocks_x+bx;
        volatile uint16_t *dst=GBA_VRAM16+sb*1024u;
        dst[(y&31u)*32u+(x&31u)]=md2gba_gba_name_word_from_genesis(m);
    }
    return 0;
}

int md2gba_gba_commit_vdp_planes(const Md2GbaVdp *vdp,unsigned a_sb,unsigned b_sb) {
    unsigned w=md2gba_vdp_plane_width(vdp),h=md2gba_vdp_plane_height(vdp);
    uint16_t a=md2gba_vdp_plane_a_base(vdp),b=md2gba_vdp_plane_b_base(vdp);
    unsigned ma,mb,max_tile;
    if((w!=32u&&w!=64u)||(h!=32u&&h!=64u))return -1;
    ma=md2gba_vdp_max_tile(vdp,a,w,h);mb=md2gba_vdp_max_tile(vdp,b,w,h);max_tile=ma>mb?ma:mb;
    if(max_tile>1023u)return -3; /* generic tile-cache/remap path is next. */
    md2gba_gba_commit_vram_tiles(vdp,0u,0u,max_tile+1u);
    if(md2gba_gba_commit_name_table_geometry(vdp,a,a_sb,w,h))return -4;
    if(md2gba_gba_commit_name_table_geometry(vdp,b,b_sb,w,h))return -5;
    return 0;
}


uint16_t md2gba_gba_pad_from_keyinput(uint16_t k){
    uint16_t h=0; k=(uint16_t)~k;
    if(k&(1u<<6)) h|=MD2GBA_PAD_UP;
    if(k&(1u<<7)) h|=MD2GBA_PAD_DOWN;
    if(k&(1u<<5)) h|=MD2GBA_PAD_LEFT;
    if(k&(1u<<4)) h|=MD2GBA_PAD_RIGHT;
    if(k&(1u<<1)) h|=MD2GBA_PAD_B;
    if(k&(1u<<8)) h|=MD2GBA_PAD_C;
    if(k&(1u<<0)) h|=MD2GBA_PAD_A;
    if(k&(1u<<3)) h|=MD2GBA_PAD_START;
    return h;
}
void md2gba_gba_sample_pad(Md2GbaBus*b,unsigned port,uint16_t keyinput){md2gba_bus_set_pad(b,port,md2gba_gba_pad_from_keyinput(keyinput));}

static uint16_t vram_word(const Md2GbaVdp*v,unsigned a){return (uint16_t)(((uint16_t)v->vram[(uint16_t)a]<<8)|v->vram[(uint16_t)(a+1u)]);}
static void cache_init(Md2GbaBgTileCache*c){unsigned i;for(i=0;i<2048u;i++)c->source_to_slot[i]=0xFFFFu;for(i=0;i<1024u;i++)c->slot_to_source[i]=0xFFFFu;for(i=0;i<64u;i++)c->source_mask[i]=0u;c->source_mask_valid=1u;c->used_slots=1u;c->slot_to_source[0]=0xFFFFu;}
static int cache_add(Md2GbaBgTileCache*c,unsigned src){if(src>=2048u)return -1;if(c->source_to_slot[src]!=0xFFFFu)return 0;if(c->used_slots>=1024u)return -2;c->source_to_slot[src]=(uint16_t)c->used_slots;c->slot_to_source[c->used_slots]=(uint16_t)src;c->source_mask[src>>5]|=1u<<(src&31u);c->used_slots++;return 0;}
int md2gba_gba_build_bg_tile_cache(const Md2GbaVdp*v,uint16_t a,uint16_t b,unsigned w,unsigned h,Md2GbaBgTileCache*c){
    unsigned x,y;cache_init(c);if((w!=32u&&w!=64u)||(h!=32u&&h!=64u))return -1;
    for(y=0;y<h;y++) {
        for(x=0;x<w;x++) {
            unsigned o=2u*(y*w+x);
            if(cache_add(c,vram_word(v,(unsigned)a+o)&0x7FFu)) return -2;
            if(cache_add(c,vram_word(v,(unsigned)b+o)&0x7FFu)) return -2;
        }
    }
    return 0;
}
int md2gba_gba_commit_cached_tiles(const Md2GbaVdp*v,const Md2GbaBgTileCache*c){
    unsigned slot,i;for(i=0;i<16u;i++)GBA_VRAM16_BYTES[i]=0; /* reserved blank */
    for(slot=1u;slot<c->used_slots;slot++){unsigned src=c->slot_to_source[slot];if(src>=2048u)return -1;gba_copy_tile32_to_vram(GBA_VRAM16_BYTES+slot*16u,&v->vram[(uint16_t)(src*32u)]);}return 0;
}
static int md2gba_gba_stage_cached_tiles(const Md2GbaVdp*v,const Md2GbaBgTileCache*c){
    unsigned slot,i;
    for(i=0;i<16u;i++)md2gba_bg_tile_stage[i]=0;
    for(slot=1u;slot<c->used_slots;slot++){unsigned src=c->slot_to_source[slot];if(src>=2048u||slot>=1024u)return -1;gba_convert_tile32_to_ram(md2gba_bg_tile_stage+slot*16u,&v->vram[(uint16_t)(src*32u)]);}
    return 0;
}
static int md2gba_vram_block32_dirty(const Md2GbaVdp*v,unsigned a){
    unsigned blk=((uint16_t)a)>>5;
    return !!(v->vram_dirty_tiles[blk>>5]&(1u<<(blk&31u)));
}
static int md2gba_source_tile_dirty(const Md2GbaVdp*v,unsigned src){
    return src<2048u && !!(v->vram_dirty_tiles[src>>5]&(1u<<(src&31u)));
}
static int md2gba_cache_has_source_mask(const Md2GbaBgTileCache*c){return c->source_mask_valid!=0u;}
/* OPT-B: enumerate set bits only; ARM7 lacks a native CTZ instruction. */
static unsigned md2gba_lowbit_index(uint32_t bits){
    static const uint8_t bit_index[32]={0,1,28,2,29,14,24,3,30,22,20,15,25,17,4,8,31,27,13,23,21,19,16,7,26,12,18,6,11,5,10,9};
    return bit_index[((bits&(0u-bits))*0x077CB531u)>>27];
}
static unsigned md2gba_gba_stage_dirty_cached_tiles(const Md2GbaVdp*v,const Md2GbaBgTileCache*c){
    unsigned n=0;
    if(md2gba_cache_has_source_mask(c)){
        unsigned wi;for(wi=0u;wi<64u;wi++){uint32_t bits=v->vram_dirty_tiles[wi]&c->source_mask[wi];while(bits){unsigned src=wi*32u+md2gba_lowbit_index(bits),slot=c->source_to_slot[src];bits&=bits-1u;if(slot!=0xFFFFu&&slot<c->used_slots){gba_convert_tile32_to_ram(md2gba_bg_tile_stage+slot*16u,&v->vram[(uint16_t)(src*32u)]);n++;}}}
    }else{
        unsigned slot;for(slot=1u;slot<c->used_slots;slot++){unsigned src=c->slot_to_source[slot];if(!md2gba_source_tile_dirty(v,src))continue;gba_convert_tile32_to_ram(md2gba_bg_tile_stage+slot*16u,&v->vram[(uint16_t)(src*32u)]);n++;}
    }
    return n;
}
static void md2gba_gba_commit_staged_dirty_tiles(const Md2GbaVdp*v,const Md2GbaBgTileCache*c){
    unsigned slot;
    /* Synchronous fallback is rare, but coalesce consecutive dirty cache slots
       into one DMA3 burst instead of programming DMA once per 32-byte tile. */
    unsigned run=0u,run_first=0u;
    for(slot=1u;slot<c->used_slots;slot++){
        unsigned src=c->slot_to_source[slot];int dirty=md2gba_source_tile_dirty(v,src);
        if(dirty){if(!run)run_first=slot;run++;continue;}
        if(run){gba_dma3_copy16(GBA_VRAM16_BYTES+run_first*16u,md2gba_bg_tile_stage+run_first*16u,run*16u);run=0u;}
    }
    if(run)gba_dma3_copy16(GBA_VRAM16_BYTES+run_first*16u,md2gba_bg_tile_stage+run_first*16u,run*16u);
}
static uint16_t cached_name(uint16_t m,const Md2GbaBgTileCache*c){unsigned src=m&0x7FFu,slot=c->source_to_slot[src];uint16_t h=(uint16_t)((m&0x0800u)>>1),v=(uint16_t)((m&0x1000u)>>1),pal=(uint16_t)((m&0x6000u)>>1);return (uint16_t)((slot&0x3FFu)|h|v|pal);}

/* Hardware-BG width fit for a source-framed UI panel. Keep the border tiles
   at native 8 px and resample only the interior from N source tiles to 28 GBA
   tiles. The source VDP state is never modified. P87 allocates presentation
   scratch from the *top* of the 1024-tile BG character space according to the
   actual panel height. This removes the old fixed 856-slot cutoff: a 3-row
   panel needs only 28 scratch tiles, while the worst supported 8-row panel
   still naturally starts at slot 856. The allocation remains title-agnostic
   and never overlaps the source cache. */
#define MD2GBA_UI_OUT_TILES 30u
#define MD2GBA_UI_OUT_INNER 28u
#define MD2GBA_UI_SLOT_CAP 168u
static unsigned md2gba_ui_scratch_count(const Md2GbaGbaUiPanel*p){return (p->h-2u)*MD2GBA_UI_OUT_INNER;}
static unsigned md2gba_ui_slot_base(const Md2GbaGbaUiPanel*p){return 1024u-md2gba_ui_scratch_count(p);}

static int md2gba_ui_fit_eligible(const Md2GbaGbaUiPanel*p,const Md2GbaBgTileCache*c,
                                  unsigned pw,unsigned ox,unsigned oy){
    unsigned dx=ox>>3,dy=oy>>3;
    if(!p||!p->active||p->w<=MD2GBA_UI_OUT_TILES||p->w>40u||p->h<3u||p->h>8u)return 0;
    if((ox&7u)||(oy&7u)||dx+MD2GBA_UI_OUT_TILES>pw)return 0;
    if(p->y<dy || (unsigned)p->y+p->h>dy+20u)return 0;
    if(md2gba_ui_scratch_count(p)>MD2GBA_UI_SLOT_CAP)return 0;
    if(c->used_slots>md2gba_ui_slot_base(p))return 0;
    return 1;
}
static uint8_t md2gba_ui_source_pixel(const Md2GbaVdp*v,uint16_t m,unsigned x,unsigned y){
    unsigned ti=m&0x07ffu;
    if(m&0x0800u)x=7u-x;if(m&0x1000u)y=7u-y;
    {uint8_t b=v->vram[(uint16_t)(ti*32u+y*4u+(x>>1))];return (uint8_t)((x&1u)?(b&15u):(b>>4));}
}
static void md2gba_ui_stage_tiles(const Md2GbaVdp*v,const Md2GbaGbaUiPanel*p,unsigned pw){
    uint16_t base=md2gba_vdp_plane_a_base(v);unsigned ry,py,op,slot_base=md2gba_ui_slot_base(p);
    unsigned src_px=(p->w-2u)*8u,out_px=MD2GBA_UI_OUT_INNER*8u;
    uint16_t source_x[224];
    unsigned sp=0u,err=src_px>>1;
    /* OPTSAFE: geometry is identical on every pixel row. Compute DDA once. */
    for(op=0u;op<out_px;op++){
        source_x[op]=(uint16_t)sp;
        err+=src_px;
        if(err>=224u){err-=224u;sp++;if(err>=224u){err-=224u;sp++;}}
        if(sp>=src_px)sp=src_px-1u;
    }
    for(ry=1u;ry+1u<p->h;ry++){
        uint16_t names[38];unsigned x;
        /* Name words do not change between the eight scanlines of a tile row. */
        for(x=0u;x<p->w-2u;x++)
            names[x]=vram_word(v,(unsigned)base+2u*((p->y+ry)*pw+p->x+1u+x));
        for(py=0u;py<8u;py++)for(op=0u;op<out_px;op+=2u){
            unsigned sx0=source_x[op],sx1=source_x[op+1u];
            unsigned slot=slot_base+(ry-1u)*MD2GBA_UI_OUT_INNER+(op>>3);
            uint8_t*d=(uint8_t*)(void*)(md2gba_bg_tile_stage+slot*16u);
            uint8_t lo=md2gba_ui_source_pixel(v,names[sx0>>3],sx0&7u,py);
            uint8_t hi=md2gba_ui_source_pixel(v,names[sx1>>3],sx1&7u,py);
            d[py*4u+((op&7u)>>1)]=(uint8_t)(lo|(hi<<4));
        }
    }
}
static unsigned md2gba_ui_map_index(unsigned first_sb,unsigned pw,unsigned x,unsigned y){
    unsigned bxs=pw>>5,bx=x>>5,by=y>>5,cell=(y&31u)*32u+(x&31u);
    return (first_sb+by*bxs+bx)*1024u+cell;
}
static uint16_t md2gba_ui_out_name(const Md2GbaVdp*v,const Md2GbaGbaUiPanel*p,
                                   const Md2GbaBgTileCache*c,unsigned pw,unsigned ry,unsigned ox){
    uint16_t base=md2gba_vdp_plane_a_base(v);
    if(ox==0u)return cached_name(vram_word(v,(unsigned)base+2u*((p->y+ry)*pw+p->x)),c);
    if(ox==MD2GBA_UI_OUT_TILES-1u)return cached_name(vram_word(v,(unsigned)base+2u*((p->y+ry)*pw+p->x+p->w-1u)),c);
    if(ry==0u||ry==p->h-1u)
        return cached_name(vram_word(v,(unsigned)base+2u*((p->y+ry)*pw+p->x+1u)),c);
    {unsigned slot=md2gba_ui_slot_base(p)+(ry-1u)*MD2GBA_UI_OUT_INNER+(ox-1u);
     return (uint16_t)((slot&0x03ffu)|((uint16_t)p->palette<<12));}
}
static int md2gba_ui_queue(const Md2GbaVdp*v,const Md2GbaGbaUiPanel*p,const Md2GbaBgTileCache*c,
                           unsigned pw,unsigned ah,unsigned ox,unsigned oy,int upload_tiles,int write_map,int tiles_staged){
    unsigned ry,x,dx=ox>>3;
    if(upload_tiles){
        unsigned n=md2gba_ui_scratch_count(p),base=md2gba_ui_slot_base(p),i;
        if((unsigned)md2gba_hwq.tile_count+n>MD2GBA_HWQ_MAX_TILES)return -1;
        /* Full-map preparation already generated these identical pixels. */
        if(!tiles_staged)md2gba_ui_stage_tiles(v,p,pw);
        for(i=0u;i<n;i++)md2gba_hwq.tile_slot[md2gba_hwq.tile_count++]=(uint16_t)(base+i);
    }
    if(write_map){
        unsigned n=p->h*MD2GBA_UI_OUT_TILES;
        if((unsigned)md2gba_hwq.map_write_count+n>MD2GBA_HWQ_MAX_MAP_WRITES)return -2;
        for(ry=0u;ry<p->h;ry++)for(x=0u;x<MD2GBA_UI_OUT_TILES;x++){
            unsigned q=md2gba_hwq.map_write_count++;
            md2gba_hwq.map_index[q]=(uint16_t)md2gba_ui_map_index(ah,pw,dx+x,p->y+ry);
            md2gba_hwq.map_value[q]=md2gba_ui_out_name(v,p,c,pw,ry,x);
        }
    }
    (void)oy;return 0;
}
static void md2gba_ui_direct_map(const Md2GbaVdp*v,const Md2GbaGbaUiPanel*p,const Md2GbaBgTileCache*c,
                                 unsigned pw,unsigned ah,unsigned ox){
    unsigned ry,x,dx=ox>>3;
    for(ry=0u;ry<p->h;ry++)for(x=0u;x<MD2GBA_UI_OUT_TILES;x++)
        GBA_VRAM16[md2gba_ui_map_index(ah,pw,dx+x,p->y+ry)]=md2gba_ui_out_name(v,p,c,pw,ry,x);
}
static void md2gba_ui_direct_tiles(const Md2GbaVdp*v,const Md2GbaGbaUiPanel*p,unsigned pw){
    unsigned n=md2gba_ui_scratch_count(p),base=md2gba_ui_slot_base(p);
    md2gba_ui_stage_tiles(v,p,pw);
    gba_dma3_copy16(GBA_VRAM16_BYTES+base*16u,
                    md2gba_bg_tile_stage+base*16u,n*16u);
}

/* Phase 75 sparse-map path.  The Genesis name tables are naturally aligned to
   32-byte VRAM dirty blocks (16 name words).  Small edits can therefore keep
   the existing tile-slot mapping, append newly referenced source tiles, and
   touch only the corresponding cells in the active GBA maps. */
#define MD2GBA_MAP_PATCH_BLOCK_LIMIT 32u

static unsigned md2gba_popcount32(uint32_t x){unsigned n=0u;while(x){x&=x-1u;n++;}return n;}
static unsigned md2gba_map_dirty_blocks(const Md2GbaVdp*v,uint16_t base,unsigned bytes){
    /* Name tables are 32-byte-block aligned in the renderer. Count dirtiness
       directly from the 2048-bit VRAM block bitmap, including 64-KiB wrap,
       instead of probing every block individually. */
    unsigned blk=((unsigned)base)>>5,remain=(bytes+31u)>>5,n=0u;
    while(remain){unsigned wi=(blk>>5)&63u,bo=blk&31u,take=32u-bo;if(take>remain)take=remain;uint32_t mask=(take==32u)?0xFFFFFFFFu:((1u<<take)-1u)<<bo;n+=md2gba_popcount32(v->vram_dirty_tiles[wi]&mask);blk=(blk+take)&2047u;remain-=take;}
    return n;
}

static void md2gba_map_collect_missing(const Md2GbaVdp*v,uint16_t base,unsigned bytes,
                                        const Md2GbaBgTileCache*c,uint32_t missing[64],unsigned*count){
    unsigned off,i;
    for(off=0;off<bytes;off+=32u){
        if(!md2gba_vram_block32_dirty(v,(unsigned)base+off))continue;
        for(i=0;i<16u && off+i*2u<bytes;i++){
            unsigned src=vram_word(v,(unsigned)base+off+i*2u)&0x7FFu;
            if(c->source_to_slot[src]!=0xFFFFu)continue;
            if(!(missing[src>>5]&(1u<<(src&31u)))){missing[src>>5]|=1u<<(src&31u);(*count)++;}
        }
    }
}

static int md2gba_map_patch_possible(const Md2GbaVdp*v,const Md2GbaBgTileCache*c,
                                     uint16_t a,uint16_t b,unsigned bytes,unsigned*dirty_blocks){
    uint32_t missing[64];unsigned i,blocks,miss=0;
    blocks=md2gba_map_dirty_blocks(v,a,bytes)+md2gba_map_dirty_blocks(v,b,bytes);
    if(dirty_blocks)*dirty_blocks=blocks;
    if(!blocks)return 0;
    if(blocks>MD2GBA_MAP_PATCH_BLOCK_LIMIT)return 0;
    /* OPTSAFE: avoid clearing 256 scratch bytes on no-map-change/full-rebuild paths. */
    for(i=0;i<64u;i++)missing[i]=0u;
    md2gba_map_collect_missing(v,a,bytes,c,missing,&miss);
    md2gba_map_collect_missing(v,b,bytes,c,missing,&miss);
    return c->used_slots+miss<=1024u;
}

static int md2gba_extend_cache_for_dirty_map(const Md2GbaVdp*v,uint16_t base,unsigned bytes,Md2GbaBgTileCache*c){
    unsigned off,i;
    for(off=0;off<bytes;off+=32u){
        if(!md2gba_vram_block32_dirty(v,(unsigned)base+off))continue;
        for(i=0;i<16u && off+i*2u<bytes;i++){
            unsigned src=vram_word(v,(unsigned)base+off+i*2u)&0x7FFu;
            if(cache_add(c,src))return -1;
        }
    }
    return 0;
}

static unsigned md2gba_gba_stage_patch_tiles(const Md2GbaVdp*v,const Md2GbaBgTileCache*c,unsigned old_used){
    unsigned slot,n=0;
    for(slot=1u;slot<c->used_slots;slot++){
        unsigned src=c->slot_to_source[slot];
        if(src>=2048u)continue;
        if(slot<old_used&&!md2gba_source_tile_dirty(v,src))continue;
        gba_convert_tile32_to_ram(md2gba_bg_tile_stage+slot*16u,&v->vram[(uint16_t)(src*32u)]);n++;
    }
    return n;
}

static void md2gba_gba_commit_staged_patch_tiles(const Md2GbaVdp*v,const Md2GbaBgTileCache*c,unsigned old_used){
    unsigned slot;
    for(slot=1u;slot<c->used_slots;slot++){
        unsigned src=c->slot_to_source[slot];
        if(src>=2048u)continue;
        if(slot<old_used&&!md2gba_source_tile_dirty(v,src))continue;
        gba_dma3_copy16(GBA_VRAM16_BYTES+slot*16u,md2gba_bg_tile_stage+slot*16u,16u);
    }
}

static MD2GBA_ROM_COLD int md2gba_hwq_add_tiles(const Md2GbaVdp*v,const Md2GbaBgTileCache*c,unsigned old_used,int patch_mode){
    unsigned slot;
    /* Use dependency bitmaps for larger real caches. Small and legacy host
       caches keep the canonical slot walk. Queue order is irrelevant for
       disjoint pattern slots; new slots are appended exactly once. */
    if(c->used_slots>128u && md2gba_cache_has_source_mask(c)){
        unsigned wi,limit=patch_mode?old_used:c->used_slots;
        for(wi=0u;wi<64u;wi++){
            uint32_t bits=v->vram_dirty_tiles[wi]&c->source_mask[wi];
            while(bits){
                unsigned src=wi*32u+md2gba_lowbit_index(bits);bits&=bits-1u;
                slot=c->source_to_slot[src];
                if(slot==0u||slot>=limit||slot>=c->used_slots)continue;
                if(md2gba_hwq.tile_count>=MD2GBA_HWQ_MAX_TILES)return -1;
                md2gba_hwq.tile_slot[md2gba_hwq.tile_count++]=(uint16_t)slot;
            }
        }
        if(patch_mode)for(slot=old_used?old_used:1u;slot<c->used_slots;slot++){
            if(c->slot_to_source[slot]>=2048u)continue;
            if(md2gba_hwq.tile_count>=MD2GBA_HWQ_MAX_TILES)return -1;
            md2gba_hwq.tile_slot[md2gba_hwq.tile_count++]=(uint16_t)slot;
        }
        return 0;
    }
    for(slot=1u;slot<c->used_slots;slot++){
        unsigned src=c->slot_to_source[slot];
        int need;
        if(src>=2048u)continue;
        need=patch_mode ? (slot>=old_used || md2gba_source_tile_dirty(v,src)) : md2gba_source_tile_dirty(v,src);
        if(!need)continue;
        if(md2gba_hwq.tile_count>=MD2GBA_HWQ_MAX_TILES)return -1;
        md2gba_hwq.tile_slot[md2gba_hwq.tile_count++]=(uint16_t)slot;
    }
    return 0;
}

static MD2GBA_ROM_COLD int md2gba_hwq_add_map_patch(const Md2GbaVdp*v,uint16_t base,
                                     unsigned hi_sb,unsigned lo_sb,unsigned w,unsigned h,
                                     const Md2GbaBgTileCache*c){
    unsigned bytes=w*h*2u,off,i,bxs,shift;
    if((w!=32u&&w!=64u)||(h!=32u&&h!=64u))return -1;
    bxs=w>>5;shift=(w==64u)?6u:5u;
    for(off=0;off<bytes;off+=32u){
        if(!md2gba_vram_block32_dirty(v,(unsigned)base+off))continue;
        for(i=0;i<16u && off+i*2u<bytes;i++){
            unsigned idx=(off>>1)+i,y=idx>>shift,x=idx&(w-1u),bx=x>>5,by=y>>5;
            unsigned cell=(y&31u)*32u+(x&31u),hisb=hi_sb+by*bxs+bx,losb=lo_sb+by*bxs+bx;
            uint16_t m=vram_word(v,(unsigned)base+off+i*2u),out=cached_name(m,c);
            unsigned q=md2gba_hwq.map_write_count;
            if(q+2u>MD2GBA_HWQ_MAX_MAP_WRITES)return -1;
            md2gba_hwq.map_index[q]=(uint16_t)(hisb*1024u+cell);
            md2gba_hwq.map_value[q]=(m&0x8000u)?out:0u;
            md2gba_hwq.map_index[q+1u]=(uint16_t)(losb*1024u+cell);
            md2gba_hwq.map_value[q+1u]=(m&0x8000u)?0u:out;
            md2gba_hwq.map_write_count=(uint16_t)(q+2u);
        }
    }
    return 0;
}

static MD2GBA_ROM_COLD void md2gba_hwq_stage_palette(const Md2GbaVdp*v){
    unsigned i;
    md2gba_hwq.palette_mask[0]=v->cram_dirty_mask[0];
    md2gba_hwq.palette_mask[1]=v->cram_dirty_mask[1];
    for(i=0u;i<64u;i++)if(md2gba_hwq.palette_mask[i>>5]&(1u<<(i&31u)))md2gba_hwq.palette[i]=v->cram_gba[i];
    md2gba_hwq.palette_valid=(uint8_t)!!(md2gba_hwq.palette_mask[0]|md2gba_hwq.palette_mask[1]);
}

static MD2GBA_ROM_COLD void md2gba_hwq_reset_stage(void){
    md2gba_hwq.palette_valid=0u;md2gba_hwq.scroll_valid=0u;md2gba_hwq.sprite_valid=0u;
    md2gba_hwq.full_tiles_valid=0u;md2gba_hwq.display_valid=0u;
    md2gba_hwq.tile_count=0u;md2gba_hwq.map_write_count=0u;md2gba_hwq.full_tile_halfwords=0u;
}

static MD2GBA_ROM_COLD int md2gba_hwq_stage_display(const Md2GbaVdp*v,unsigned ah,unsigned bh,unsigned al,unsigned bl){
    unsigned w=md2gba_vdp_plane_width(v),h=md2gba_vdp_plane_height(v),size=gba_bg_size_code(w,h),need;
    if(size==0xFFFFFFFFu)return -1;
    need=gba_screenblocks_needed(w,h);
    if(ah+need>32u||bh+need>32u||al+need>32u||bl+need>32u)return -2;
    md2gba_hwq.dispcnt=(uint16_t)((1u<<8)|(1u<<9)|(1u<<10)|(1u<<11)|(1u<<12)|(1u<<6));
    md2gba_hwq.bgcnt[0]=(uint16_t)(1u|(ah<<8)|(size<<14));
    md2gba_hwq.bgcnt[1]=(uint16_t)(1u|(bh<<8)|(size<<14));
    md2gba_hwq.bgcnt[2]=(uint16_t)(3u|(al<<8)|(size<<14));
    md2gba_hwq.bgcnt[3]=(uint16_t)(3u|(bl<<8)|(size<<14));
    md2gba_hwq.display_valid=1u;
    return 0;
}

/* P120 large/full-map queue. Name tables have already been written to the
   inactive screenblock bank and pattern pixels are coherent in EWRAM. Keep
   foreground execution running; the physical VBlank IRQ performs the shared
   charblock DMA and atomically switches BG controls to the prepared bank. */
static MD2GBA_ROM_COLD int md2gba_hwq_prepare_full(const Md2GbaVdp*v,const Md2GbaBgTileCache*c,uint32_t plan,
        const uint16_t stream[160][8],unsigned ox,unsigned oy,unsigned ah,unsigned bh,unsigned al,unsigned bl){
    unsigned i;
    if(!(plan&MD2GBA_FRAME_MAPS))return 0;
    /* Cell/line horizontal scroll needs the full 160-row DMA stream to remain
       live after the swap. Keep that less-common mode on the canonical path
       until the deferred queue owns a private stream snapshot too. */
    if((plan&MD2GBA_FRAME_SCROLL)&&(((v->regs[11]&3u)&2u)!=0u || !stream))return 0;
    md2gba_hwq_reset_stage();
    if(plan&MD2GBA_FRAME_PALETTE)md2gba_hwq_stage_palette(v);
    md2gba_hwq.full_tile_halfwords=(uint16_t)(c->used_slots*16u);
    md2gba_hwq.full_tiles_valid=1u;
    if(md2gba_hwq_stage_display(v,ah,bh,al,bl))return 0;
    if(plan&MD2GBA_FRAME_SPRITES){if(md2gba_hwq_stage_sprites(v,ox,oy))return 0;}
    if((plan&MD2GBA_FRAME_SCROLL)&&stream){for(i=0u;i<8u;i++)md2gba_hwq.scroll[i]=stream[0][i];md2gba_hwq.scroll_valid=1u;}
    return 1;
}

static MD2GBA_ROM_COLD int md2gba_hwq_prepare_small(const Md2GbaVdp*v,const Md2GbaBgTileCache*c,uint32_t plan,
                                    unsigned patch_old_used,uint16_t a,uint16_t b,unsigned w,unsigned h,
                                    unsigned ah,unsigned bh,unsigned al,unsigned bl,
                                    const uint16_t stream[160][8],unsigned ox,unsigned oy){
    unsigned i;
    if(plan&(MD2GBA_FRAME_FULL|MD2GBA_FRAME_MAPS))return 0;
    /* PSC treats reg11 bit1 clear as one fixed plane offset.  Both legal
       mode 00 and prohibited mode 01 therefore fit the cheap VBlank scroll
       queue; only cell/line modes need HBlank DMA. */
    if((plan&MD2GBA_FRAME_SCROLL)&&(((v->regs[11]&3u)&2u)!=0u || !stream))return 0;
    md2gba_hwq_reset_stage();
    if(plan&MD2GBA_FRAME_PALETTE)md2gba_hwq_stage_palette(v);
    if(plan&MD2GBA_FRAME_MAP_PATCH){
        /* P134/PF01: one dirty 32-byte name block contains 16 cells and each
           cell emits one high + one low map write. Preflight the exact queue
           ownership before staging pattern work so a >512-write patch cannot
           partially prepare and then fall through to duplicate synchronous work. */
        unsigned map_blocks=md2gba_map_dirty_blocks(v,a,w*h*2u)+md2gba_map_dirty_blocks(v,b,w*h*2u);
        if(map_blocks > (MD2GBA_HWQ_MAX_MAP_WRITES/32u))return 0;
        if(md2gba_hwq_add_tiles(v,c,patch_old_used,1))return 0;
        if(md2gba_hwq_add_map_patch(v,a,ah,al,w,h,c))return 0;
        if(md2gba_hwq_add_map_patch(v,b,bh,bl,w,h,c))return 0;
    }else if(plan&MD2GBA_FRAME_TILES){
        if(md2gba_hwq_add_tiles(v,c,0u,0))return 0;
    }
    if(plan&MD2GBA_FRAME_SPRITES){if(md2gba_hwq_stage_sprites(v,ox,oy))return 0;}
    if(plan&MD2GBA_FRAME_SCROLL){for(i=0u;i<8u;i++)md2gba_hwq.scroll[i]=stream[0][i];md2gba_hwq.scroll_valid=1u;}
    return 1;
}

static MD2GBA_ROM_COLD void md2gba_hwq_sort_tile_slots(void){
    /* PF05: target slots live in 0..1023. A 128-byte stack bitmap is both
       cheaper and more predictable than insertion-sorting up to 256 entries,
       and it naturally removes any accidental duplicate slot request. */
    uint32_t seen[32];unsigned i,w,n=0u;for(i=0u;i<32u;i++)seen[i]=0u;
    for(i=0u;i<md2gba_hwq.tile_count;i++){unsigned s=md2gba_hwq.tile_slot[i];if(s<1024u)seen[s>>5]|=1u<<(s&31u);}
    for(w=0u;w<32u;w++){uint32_t b=seen[w];while(b){unsigned bit=md2gba_lowbit_index(b);md2gba_hwq.tile_slot[n++]=(uint16_t)(w*32u+bit);b&=b-1u;}}
    md2gba_hwq.tile_count=(uint16_t)n;
}

static MD2GBA_ROM_COLD void md2gba_hwq_publish(void){
    uint16_t old=REG_IME;
    /* P134/PF05: collection order follows dirty-source traversal, not target
       slot order. Sorting this bounded list turns adjacent slots into the DMA
       runs the existing VBlank coalescer already knows how to emit. */
    if(md2gba_hwq.tile_count>1u)md2gba_hwq_sort_tile_slots();
    REG_IME=0u;
    /* Publish only after every queue payload and staging write is complete. */
    MD2GBA_COMPILER_BARRIER();
    md2gba_hwq.pending=1u;
    /* If foreground preparation happened to finish during physical VBlank,
       apply immediately while writes are safe instead of adding a frame of
       avoidable latency. */
    if(REG_VCOUNT>=160u)md2gba_gba_hw_vblank_commit_pending();
    REG_IME=old;
}

static MD2GBA_ROM_COLD unsigned md2gba_gba_patch_name_table_split(const Md2GbaVdp*v,uint16_t base,
                                                   unsigned hi_sb,unsigned lo_sb,unsigned w,unsigned h,
                                                   const Md2GbaBgTileCache*c){
    unsigned bytes=w*h*2u,off,i,n=0,bxs,shift;
    if((w!=32u&&w!=64u)||(h!=32u&&h!=64u))return 0u;
    bxs=w>>5;shift=(w==64u)?6u:5u;
    for(off=0;off<bytes;off+=32u){
        if(!md2gba_vram_block32_dirty(v,(unsigned)base+off))continue;
        for(i=0;i<16u && off+i*2u<bytes;i++){
            unsigned idx=(off>>1)+i,y=idx>>shift,x=idx&(w-1u),bx=x>>5,by=y>>5;
            unsigned cell=(y&31u)*32u+(x&31u),hisb=hi_sb+by*bxs+bx,losb=lo_sb+by*bxs+bx;
            uint16_t m=vram_word(v,(unsigned)base+off+i*2u),out=cached_name(m,c);
            volatile uint16_t*hd=GBA_VRAM16+hisb*1024u;volatile uint16_t*ld=GBA_VRAM16+losb*1024u;
            if(m&0x8000u){hd[cell]=out;ld[cell]=0u;}else{hd[cell]=0u;ld[cell]=out;}
            n++;
        }
    }
    return n;
}

static MD2GBA_ROM_COLD int md2gba_gba_commit_name_table_split_full(const Md2GbaVdp*v,uint16_t base,unsigned hi_sb,unsigned lo_sb,unsigned w,unsigned h,const Md2GbaBgTileCache*c){
    unsigned x,y,bx,by,bxs;if((w!=32u&&w!=64u)||(h!=32u&&h!=64u))return -1;bxs=w/32u;
    if(hi_sb+(w/32u)*(h/32u)>32u||lo_sb+(w/32u)*(h/32u)>32u)return -2;
    for(y=0;y<h;y++)for(x=0;x<w;x++){
        uint16_t m=vram_word(v,(unsigned)base+2u*(y*w+x)),out=cached_name(m,c);
        unsigned cell=(y&31u)*32u+(x&31u);bx=x>>5;by=y>>5;
        {unsigned hs=hi_sb+by*bxs+bx,ls=lo_sb+by*bxs+bx;volatile uint16_t*hd=GBA_VRAM16+hs*1024u,*ld=GBA_VRAM16+ls*1024u;
         if(m&0x8000u){hd[cell]=out;ld[cell]=0u;}else{hd[cell]=0u;ld[cell]=out;}}
    }
    return 0;
}

int md2gba_gba_commit_name_table_cached(const Md2GbaVdp*v,uint16_t base,unsigned first_sb,unsigned w,unsigned h,const Md2GbaBgTileCache*c,int pf){
    unsigned x,y,bx,by,bxs;if((w!=32u&&w!=64u)||(h!=32u&&h!=64u))return -1;bxs=w/32u;if(first_sb+(w/32u)*(h/32u)>32u)return -2;
    for(y=0;y<h;y++) {
        for(x=0;x<w;x++) {
            uint16_t m=vram_word(v,(unsigned)base+2u*(y*w+x));
            int hi=!!(m&0x8000u);
            uint16_t out=((pf==0&&!hi)||(pf==1&&hi)||pf<0)?cached_name(m,c):0;
            bx=x>>5; by=y>>5;
            {
                unsigned sb=first_sb+by*bxs+bx;
                volatile uint16_t*d=GBA_VRAM16+sb*1024u;
                d[(y&31u)*32u+(x&31u)]=out;
            }
        }
    }
    return 0;
}
int md2gba_gba_commit_vdp_planes_cached(const Md2GbaVdp*v,unsigned asb,unsigned bsb,Md2GbaBgTileCache*c){unsigned w=md2gba_vdp_plane_width(v),h=md2gba_vdp_plane_height(v);uint16_t a=md2gba_vdp_plane_a_base(v),b=md2gba_vdp_plane_b_base(v);int r;if((r=md2gba_gba_build_bg_tile_cache(v,a,b,w,h,c)))return r;if((r=md2gba_gba_commit_cached_tiles(v,c)))return -3;if((r=md2gba_gba_commit_name_table_cached(v,a,asb,w,h,c,-1)))return -4;if((r=md2gba_gba_commit_name_table_cached(v,b,bsb,w,h,c,-1)))return -5;return 0;}

unsigned md2gba_vdp_sat_base(const Md2GbaVdp*v){unsigned mask=((v->regs[12]&0x81u)==0x81u)?0x7Eu:0x7Fu;return (unsigned)(v->regs[5]&mask)<<9;}
static unsigned sprite_cache_tile(const Md2GbaVdp*v,unsigned src,uint16_t srcs[128],unsigned*used){unsigned i;for(i=0;i<*used;i++)if(srcs[i]==src)return i;if(*used>=128u)return 0xFFFFFFFFu;i=(*used)++;srcs[i]=(uint16_t)src;gba_copy_tile32_to_vram(GBA_OBJ_VRAM16+i*16u,&v->vram[(uint16_t)(src*32u)]);return i;}
unsigned md2gba_gba_commit_sprites(const Md2GbaVdp*v,unsigned max_cells,unsigned*dropped){
    uint16_t srcs[128];unsigned used=0,cells=0,drop=0,entry=0,guard=0,base=md2gba_vdp_sat_base(v),i;for(i=0;i<128u;i++)srcs[i]=0xFFFFu;if(max_cells>128u)max_cells=128u;
    while(guard++<80u){unsigned a=(base+entry*8u)&0xFFFFu;uint16_t yw=vram_word(v,a),attr=vram_word(v,a+4u),xw=vram_word(v,a+6u);unsigned sz=v->vram[(uint16_t)(a+2u)],link=v->vram[(uint16_t)(a+3u)]&0x7Fu,w=((sz>>2)&3u)+1u,h=(sz&3u)+1u,base_tile=attr&0x7FFu,hf=!!(attr&0x0800u),vf=!!(attr&0x1000u),pal=(attr>>13)&3u,hi=!!(attr&0x8000u),sx,sy;int x=(int)(xw&0x1FFu)-128,y=(int)(yw&0x3FFu)-128;
        for(sy=0;sy<h;sy++)for(sx=0;sx<w;sx++){unsigned rx=hf?(w-1u-sx):sx,ry=vf?(h-1u-sy):sy,src=base_tile+rx*h+ry,slot=sprite_cache_tile(v,src,srcs,&used);if(cells>=max_cells||slot==0xFFFFFFFFu){drop++;continue;}gba_oam_write(cells,(uint16_t)((y+(int)sy*8)&0xFF),(uint16_t)(((x+(int)sx*8)&0x1FF)|(hf?0x1000u:0u)|(vf?0x2000u:0u)),(uint16_t)((slot&0x3FFu)|((hi?0u:2u)<<10)|(pal<<12)));cells++;}
        if(!link) break;
        entry=link;
    }
    for(i=cells;i<128u;i++){gba_oam_write(i,0x0200u,0u,0u);}
    REG_DISPCNT|=(uint16_t)((1u<<12)|(1u<<6));if(dropped)*dropped=drop;return cells;
}

uint16_t md2gba_vdp_hscroll_base(const Md2GbaVdp*v){return (uint16_t)((v->regs[13]&0x3Fu)<<10);}
int md2gba_vdp_hscroll_line(const Md2GbaVdp*v,unsigned plane,unsigned line,int16_t*out){unsigned mode=v->regs[11]&3u,idx;if(plane>1u||!out)return -1;/* Genesis mode 01 is prohibited. PSC's shared PSII/PSIII presenter treats reg11 bit1 clear as full-plane scroll, so use the conservative mode-0 behavior rather than inventing an 8-row periodic mapping. */if(mode==0u||mode==1u)idx=0u;else if(mode==2u)idx=line&~7u;else if(mode==3u)idx=line;else return -2;*out=(int16_t)vram_word(v,(unsigned)md2gba_vdp_hscroll_base(v)+idx*4u+plane*2u);return 0;}
int md2gba_vdp_vscroll_plane(const Md2GbaVdp*v,unsigned plane,int16_t*out){if(plane>1u||!out)return -1;if(v->regs[11]&4u)return -2;*out=(int16_t)v->vsram[plane];return 0;}
int md2gba_vdp_vscroll_column(const Md2GbaVdp*v,unsigned plane,unsigned col,int16_t*out){unsigned idx,maxcol=((v->regs[12]&0x81u)==0x81u)?20u:16u;if(plane>1u||!out||col>=maxcol)return -1;if(!(v->regs[11]&4u))return md2gba_vdp_vscroll_plane(v,plane,out);idx=col*2u+plane;if(idx>=40u)return -1;*out=(int16_t)v->vsram[idx];return 0;}

/* P83 source-geometry panel detector.  Ordinary PSIV dialogue is not a VDP
   Window plane: it is a framed, high-priority Plane-A rectangle.  On H40 the
   source frame is 34 tiles wide (272 px), so a 30-tile GBA crop cannot contain
   it at 1:1 no matter where the crop is placed.  Detect the *VDP framing
   grammar* rather than any PSIV tile IDs/coordinates: same corner tile with
   mirrored right edge, repeated horizontal edge tile, repeated vertical side
   tile, common palette, and a solid priority rectangle near the bottom. */
int md2gba_gba_detect_bottom_ui_panel(const Md2GbaVdp*v,Md2GbaGbaUiPanel*out){
    unsigned pw=md2gba_vdp_plane_width(v),ph=md2gba_vdp_plane_height(v);
    unsigned cols=((v->regs[12]&0x81u)==0x81u)?40u:32u;
    unsigned rows=(v->regs[1]&0x08u)?30u:28u;
    uint16_t base=md2gba_vdp_plane_a_base(v);int16_t vs=0,hs=0;
    unsigned sy,x0;
    if(out){out->active=0u;out->x=0u;out->y=0u;out->w=0u;out->h=0u;out->palette=0u;}
    if((pw!=32u&&pw!=64u)||(ph!=32u&&ph!=64u)||cols<30u)return 0;
    /* The width-fit map transform is intentionally limited to fixed panels.
       Scroll-mode UI still uses the conservative crop/software path. */
    if((v->regs[11]&7u)!=0u)return 0;
    if(md2gba_vdp_vscroll_plane(v,0u,&vs)||vs!=0)return 0;
    if(md2gba_vdp_hscroll_line(v,0u,0u,&hs)||hs!=0)return 0;
    for(sy=(rows>10u?rows-10u:0u);sy+2u<rows;sy++){
        for(x0=0u;x0+30u<cols;x0++){
            unsigned x1=x0,hgt,y,x;uint16_t tl,tr,ti,li;unsigned pal;
            uint16_t m=vram_word(v,(unsigned)base+2u*(sy*pw+x0));
            if(!(m&0x8000u))continue;
            /* Maximal contiguous priority run on this candidate top row. */
            while(x1+1u<cols && (vram_word(v,(unsigned)base+2u*(sy*pw+x1+1u))&0x8000u))x1++;
            if(x1-x0+1u<=30u){x0=x1;continue;}
            if(x1-x0+1u>40u){x0=x1;continue;}
            tl=vram_word(v,(unsigned)base+2u*(sy*pw+x0));
            tr=vram_word(v,(unsigned)base+2u*(sy*pw+x1));
            pal=tl&0x6000u;
            /* Same top corner art; right corner mirrors horizontally. */
            if((tl&0x07ffu)!=(tr&0x07ffu) || !(tr&0x0800u) || (tr&0x6000u)!=pal){x0=x1;continue;}
            ti=vram_word(v,(unsigned)base+2u*(sy*pw+x0+1u));
            if(!(ti&0x8000u)||(ti&0x6000u)!=pal){x0=x1;continue;}
            for(x=x0+1u;x<x1;x++){
                uint16_t q=vram_word(v,(unsigned)base+2u*(sy*pw+x));
                if((q&0x87ffu)!=(ti&0x87ffu)||(q&0x6000u)!=pal)break;
            }
            if(x!=x1){x0=x1;continue;}
            /* Find the matching bottom border (same tile IDs, V-flipped). */
            for(hgt=3u;hgt<=8u && sy+hgt<=rows;hgt++){
                unsigned by=sy+hgt-1u;uint16_t bl,br,bi;
                bl=vram_word(v,(unsigned)base+2u*(by*pw+x0));
                br=vram_word(v,(unsigned)base+2u*(by*pw+x1));
                bi=vram_word(v,(unsigned)base+2u*(by*pw+x0+1u));
                if(!(bl&0x8000u)||!(br&0x8000u)||!(bi&0x8000u))continue;
                if((bl&0x07ffu)!=(tl&0x07ffu)||(bi&0x07ffu)!=(ti&0x07ffu))continue;
                if(!(bl&0x1000u)||!(bi&0x1000u)||!(br&0x1000u))continue;
                if((bl&0x6000u)!=pal||(br&0x6000u)!=pal||(bi&0x6000u)!=pal)continue;
                /* Every row must remain a full high-priority rectangle with
                   mirrored side-border art and one common palette. */
                for(y=sy;y<=by;y++){
                    uint16_t l=vram_word(v,(unsigned)base+2u*(y*pw+x0));
                    uint16_t r=vram_word(v,(unsigned)base+2u*(y*pw+x1));
                    if(!(l&0x8000u)||!(r&0x8000u)||(l&0x6000u)!=pal||(r&0x6000u)!=pal)break;
                    if(y>sy&&y<by){
                        if(y==sy+1u)li=l;
                        if((l&0x07ffu)!=(li&0x07ffu)||(r&0x07ffu)!=(li&0x07ffu)||!(r&0x0800u))break;
                    }
                    for(x=x0+1u;x<x1;x++){
                        uint16_t q=vram_word(v,(unsigned)base+2u*(y*pw+x));
                        if(!(q&0x8000u)||(q&0x6000u)!=pal)break;
                    }
                    if(x!=x1)break;
                }
                if(y==by+1u){
                    if(out){out->active=1u;out->x=(uint8_t)x0;out->y=(uint8_t)sy;out->w=(uint8_t)(x1-x0+1u);out->h=(uint8_t)hgt;out->palette=(uint8_t)(pal>>13);}
                    return 1;
                }
            }
            x0=x1;
        }
    }
    return 0;
}

/* P82 hardware-BG viewport selection.  The GBA hardware path cannot vertically
   scale four priority BG layers, so it normally center-crops the 224/240-line
   Genesis raster.  A fixed, dense high-priority Plane-A overlay in the bottom
   64 source lines is a strong generic signature of dialogue/menu UI.  In that
   case bottom-anchor the 160-line viewport so the complete overlay remains
   visible.  Scrolling field scenes retain the centered crop. */
MD2GBA_ROM_COLD unsigned md2gba_gba_suggest_view_y(const Md2GbaVdp*v){
    unsigned vh=(v->regs[1]&0x08u)?240u:224u;
    unsigned center=vh>160u?(vh-160u)/2u:0u,bottom=vh>160u?vh-160u:0u;
    Md2GbaGbaUiPanel panel;
    if(bottom==center)return center;
    /* P89: only an actually framed fixed UI window may move the viewport.
       The older priority-density heuristic mistook ordinary high-priority
       scenery for UI and could jump the crop while the camera was moving. */
    if(md2gba_gba_detect_bottom_ui_panel(v,&panel) && panel.active && panel.w>30u)
        return bottom;
    return center;
}
/* P105: panel geometry is presentation state. Palette/tile/sprite/scroll
   changes cannot create or destroy the framed name-table grammar, so reuse the
   last proven result until a map/full update actually invalidates it. */
unsigned md2gba_gba_suggest_view_y_cached(const Md2GbaVdp*v,const Md2GbaGbaFrameState*s,uint32_t plan){
    unsigned vh=(v->regs[1]&0x08u)?240u:224u;
    unsigned center=vh>160u?(vh-160u)/2u:0u,bottom=vh>160u?vh-160u:0u;
    if(bottom==center)return center;
    /* Both positive and negative detection results are cacheable while the
       name table/geometry is unchanged. */
    if(s&&s->initialized&&!(plan&(MD2GBA_FRAME_FULL|MD2GBA_FRAME_MAPS|MD2GBA_FRAME_MAP_PATCH)))
        return s->ui_active?bottom:center;
    return md2gba_gba_suggest_view_y(v);
}
MD2GBA_BACKEND_HOT int md2gba_gba_build_direct_scroll_viewport(const Md2GbaVdp*v,uint16_t out[8],unsigned ox,unsigned oy){
    int16_t va,vb,ha,hb;unsigned base,mode;
    if(!v||!out)return -1;
    mode=v->regs[11]&3u;
    if((mode&2u)!=0u)return -2; /* rows require the HBlank stream */
    if(md2gba_vdp_vscroll_plane(v,0u,&va)||md2gba_vdp_vscroll_plane(v,1u,&vb))return -3;
    if(v->present_scroll_valid && v->present_scroll_mode==mode){
        ha=v->present_hscroll[0];hb=v->present_hscroll[1];
        va=v->present_vscroll[0];vb=v->present_vscroll[1];
    }else{
        base=(unsigned)md2gba_vdp_hscroll_base(v);
        ha=(int16_t)vram_word(v,base);hb=(int16_t)vram_word(v,base+2u);
    }
    out[0]=(uint16_t)(ox-ha);out[1]=(uint16_t)(va+(int16_t)oy);
    out[2]=(uint16_t)(ox-hb);out[3]=(uint16_t)(vb+(int16_t)oy);
    out[4]=out[0];out[5]=out[1];out[6]=out[2];out[7]=out[3];
    return 0;
}
/* P87c / PSC-style live presentation seam. Whole-plane scroll is cheap
   hardware state and must remain publishable even while the deferred VRAM/map
   queue owns its staging buffers. This intentionally does not touch frame
   dirtiness/epochs; the normal renderer still accounts for source state. */
MD2GBA_BACKEND_HOT int md2gba_gba_publish_live_whole_plane_scroll(const Md2GbaVdp*v,unsigned ox,unsigned oy){
    uint16_t direct[8];
    if(!v)return -1;
    if((v->regs[11]&2u)!=0u)return 0; /* row/line modes require HBlank stream */
    if(md2gba_gba_build_direct_scroll_viewport(v,direct,ox,oy)!=0)return -2;
    md2gba_gba_scroll_latch_direct(direct);
    return 1;
}

int md2gba_gba_build_scroll_stream_viewport(const Md2GbaVdp*v,uint16_t stream[160][8],unsigned ox,unsigned oy){
    int16_t va,vb; unsigned y,mode,base;
    if(md2gba_vdp_vscroll_plane(v,0,&va)||md2gba_vdp_vscroll_plane(v,1,&vb))return -2;
    mode=v->regs[11]&3u;
    va=(int16_t)(va+oy);vb=(int16_t)(vb+oy);
    base=(unsigned)md2gba_vdp_hscroll_base(v);
    if(mode==0u || mode==1u){
        uint16_t direct[8];
        /* Mode 01 is prohibited by the Mega Drive VDP. PSC's shared
           presenter treats bit1-clear modes as full-plane scroll. */
        if(md2gba_gba_build_direct_scroll_viewport(v,direct,ox,oy))return -2;
        for(y=0;y<160u;y++){uint16_t *d=stream[y];unsigned q;for(q=0u;q<8u;q++)d[q]=direct[q];}
        return 0;
    }
    if(mode==2u){
        /* A31/P128: cell-scroll source values are shared by eight source
           scanlines. Re-read VRAM only when the source 8-line group changes. */
        unsigned last_idx=0xFFFFFFFFu;uint16_t row[8];
        for(y=0u;y<160u;y++){unsigned sy=y+oy,idx=sy&~7u;if(idx!=last_idx){unsigned a=base+idx*4u;int16_t ha=(int16_t)vram_word(v,a),hb=(int16_t)vram_word(v,a+2u);row[0]=(uint16_t)(ox-ha);row[1]=(uint16_t)va;row[2]=(uint16_t)(ox-hb);row[3]=(uint16_t)vb;row[4]=row[0];row[5]=row[1];row[6]=row[2];row[7]=row[3];last_idx=idx;}for(unsigned q=0u;q<8u;q++)stream[y][q]=row[q];}
        return 0;
    }
    for(y=0;y<160u;y++){
        unsigned sy=y+oy,a=base+sy*4u;
        int16_t ha=(int16_t)vram_word(v,a),hb=(int16_t)vram_word(v,a+2u);
        uint16_t *d=stream[y];
        d[0]=(uint16_t)(ox-ha);d[1]=(uint16_t)va;d[2]=(uint16_t)(ox-hb);d[3]=(uint16_t)vb;
        d[4]=d[0];d[5]=d[1];d[6]=d[2];d[7]=d[3];
    }
    return 0;
}
int md2gba_gba_build_scroll_stream(const Md2GbaVdp*v,uint16_t stream[160][8]){return md2gba_gba_build_scroll_stream_viewport(v,stream,0u,0u);}
void md2gba_gba_stop_hblank_scroll_dma(void){REG_DMA0CNT=0;md2gba_gba_scroll_dma_set((const uint16_t (*)[8])0,0);}
void md2gba_gba_start_hblank_scroll_dma(const uint16_t stream[160][8]){md2gba_gba_scroll_dma_set(stream,1);}

int md2gba_gba_init_display_priority_vdp(const Md2GbaVdp*v,unsigned ah,unsigned bh,unsigned al,unsigned bl){unsigned w=md2gba_vdp_plane_width(v),h=md2gba_vdp_plane_height(v),size=gba_bg_size_code(w,h),need;if(size==0xFFFFFFFFu)return -1;need=gba_screenblocks_needed(w,h);if(ah+need>32u||bh+need>32u||al+need>32u||bl+need>32u)return -2;REG_DISPCNT=(uint16_t)((1u<<8)|(1u<<9)|(1u<<10)|(1u<<11)|(1u<<12)|(1u<<6));REG_BG0CNT=(uint16_t)(1u|(ah<<8)|(size<<14));REG_BG1CNT=(uint16_t)(1u|(bh<<8)|(size<<14));REG_BG2CNT=(uint16_t)(3u|(al<<8)|(size<<14));REG_BG3CNT=(uint16_t)(3u|(bl<<8)|(size<<14));return 0;}
int md2gba_gba_commit_vdp_priority(const Md2GbaVdp*v,unsigned ah,unsigned bh,unsigned al,unsigned bl,Md2GbaBgTileCache*c){unsigned w=md2gba_vdp_plane_width(v),h=md2gba_vdp_plane_height(v);uint16_t a=md2gba_vdp_plane_a_base(v),b=md2gba_vdp_plane_b_base(v);int r;if((r=md2gba_gba_build_bg_tile_cache(v,a,b,w,h,c)))return r;if(md2gba_gba_commit_cached_tiles(v,c))return -3;if(md2gba_gba_commit_name_table_cached(v,a,ah,w,h,c,1))return -4;if(md2gba_gba_commit_name_table_cached(v,b,bh,w,h,c,1))return -5;if(md2gba_gba_commit_name_table_cached(v,a,al,w,h,c,0))return -6;if(md2gba_gba_commit_name_table_cached(v,b,bl,w,h,c,0))return -7;return 0;}


unsigned md2gba_vdp_collect_sprite_tiles(const Md2GbaVdp*v,uint16_t*out,unsigned max_tiles){
    unsigned n=0,entry=0,guard=0,base=md2gba_vdp_sat_base(v),k;
    uint32_t seen[64];for(k=0;k<64u;k++)seen[k]=0u;
    while(guard++<80u){
        unsigned a=(base+entry*8u)&0xFFFFu;
        uint16_t attr=vram_word(v,a+4u);
        unsigned sz=v->vram[(uint16_t)(a+2u)],link=v->vram[(uint16_t)(a+3u)]&0x7Fu;
        unsigned count=((((sz>>2)&3u)+1u)*((sz&3u)+1u)),t=attr&0x7FFu,i;
        /* Physical pattern addresses wrap in the same 64-KiB VRAM as the
           sprite converter. Deduplicate without reading past bounded output. */
        for(i=0;i<count;i++){
            unsigned src=(t+i)&0x7FFu;uint32_t bit=1u<<(src&31u);
            if(seen[src>>5]&bit)continue;
            seen[src>>5]|=bit;if(out&&n<max_tiles)out[n]=(uint16_t)src;n++;
        }
        if(!link)break;
        entry=link;
    }
    return n;
}
static int bg_sources_dirty(const Md2GbaVdp*v,const Md2GbaBgTileCache*c){unsigned i;if(c->source_mask_valid){for(i=0u;i<64u;i++)if(v->vram_dirty_tiles[i]&c->source_mask[i])return 1;return 0;}/* Keep host/oracle callers that construct the public cache mapping manually compatible. Real caches always set source_mask_valid in cache_init(). */if(c->used_slots>1u){unsigned slot;for(slot=1u;slot<c->used_slots;slot++){unsigned src=c->slot_to_source[slot];if(src<2048u&&(v->vram_dirty_tiles[src>>5]&(1u<<(src&31u))))return 1;}}return 0;}
static int sprite_sources_dirty(const Md2GbaVdp*v,const Md2GbaGbaFrameState*s){
    unsigned i;
    /* More than 128 distinct references cannot fit the bounded dependency
       list. Preserve the count and conservatively invalidate on any VRAM
       change instead of silently missing late sprite patterns. */
    if(s->sprite_source_count>128u){for(i=0;i<64u;i++)if(v->vram_dirty_tiles[i])return 1;return 0;}
    for(i=0;i<s->sprite_source_count;i++){unsigned src=s->sprite_source_tiles[i];if(src<2048u&&(v->vram_dirty_tiles[src>>5]&(1u<<(src&31u))))return 1;}
    return 0;
}
uint32_t md2gba_gba_plan_frame(const Md2GbaVdp*v,const Md2GbaGbaFrameState*s){
    if(s && s->initialized && v->vram_epoch==s->last_vram_epoch &&
       v->cram_epoch==s->last_cram_epoch && v->vsram_epoch==s->last_vsram_epoch &&
       v->regs_epoch==s->last_regs_epoch)return 0u;
    /* P105 palette-only fast plan. Logo fades and other CRAM ramps must not
       derive plane geometry, scan name tables or inspect tile dependencies. */
    if(s&&s->initialized&&v->vram_epoch==s->last_vram_epoch&&v->vsram_epoch==s->last_vsram_epoch){
        uint32_t rm=(v->regs_epoch!=s->last_regs_epoch)?v->regs_dirty_mask:0u;
        if((v->cram_epoch!=s->last_cram_epoch || (rm&(1u<<7))) && !(rm&~(1u<<7)))return MD2GBA_FRAME_PALETTE;
    }
    uint32_t p=0;unsigned w=md2gba_vdp_plane_width(v),h=md2gba_vdp_plane_height(v);unsigned map_bytes=w*h*2u;
    uint16_t a=md2gba_vdp_plane_a_base(v),b=md2gba_vdp_plane_b_base(v),sat=(uint16_t)md2gba_vdp_sat_base(v),hs=md2gba_vdp_hscroll_base(v);
    if(!s || !s->initialized)return MD2GBA_FRAME_FULL|MD2GBA_FRAME_PALETTE|MD2GBA_FRAME_MAPS|MD2GBA_FRAME_TILES|MD2GBA_FRAME_SPRITES|MD2GBA_FRAME_SCROLL;
    if(v->regs_epoch!=s->last_regs_epoch){
        uint32_t rm=v->regs_dirty_mask;
        /* P74: only geometry/addressing registers require a complete text-BG
           rebuild. Games commonly rewrite auto-increment/interrupt/scroll
           registers every frame; treating those as geometry changes was making
           the hardware renderer rebuild four complete maps unnecessarily. */
        const uint32_t geom=(1u<<2)|(1u<<4)|(1u<<12)|(1u<<16);
        const uint32_t scroll=(1u<<11)|(1u<<13);
        if(rm&geom)p|=MD2GBA_FRAME_FULL|MD2GBA_FRAME_MAPS|MD2GBA_FRAME_TILES|MD2GBA_FRAME_SPRITES|MD2GBA_FRAME_SCROLL;
        else {
            /* B27/P128: register 5 only relocates the SAT.  Background maps,
               pattern cache and scroll geometry do not depend on its base. */
            if(rm&(1u<<5))p|=MD2GBA_FRAME_SPRITES;
            if(rm&scroll)p|=MD2GBA_FRAME_SCROLL;
            if(rm&(1u<<7))p|=MD2GBA_FRAME_PALETTE;
        }
    }
    if(v->cram_epoch!=s->last_cram_epoch)p|=MD2GBA_FRAME_PALETTE;
    if(v->vsram_epoch!=s->last_vsram_epoch)p|=MD2GBA_FRAME_SCROLL;
    if(v->vram_epoch!=s->last_vram_epoch){
        if(!(p&MD2GBA_FRAME_FULL)&&(w==32u||w==64u)&&(h==32u||h==64u)){
            unsigned blocks=0;
            if(md2gba_map_patch_possible(v,&s->bg_cache,a,b,map_bytes,&blocks))p|=MD2GBA_FRAME_MAP_PATCH;
            else if(blocks)p|=MD2GBA_FRAME_MAPS|MD2GBA_FRAME_TILES;
        } else if(!(w==32u||w==64u)||!(h==32u||h==64u)) {
            p|=MD2GBA_FRAME_MAPS|MD2GBA_FRAME_TILES;
        }
        if(!(p&MD2GBA_FRAME_MAPS)&&bg_sources_dirty(v,&s->bg_cache))p|=MD2GBA_FRAME_TILES;
        if(md2gba_vdp_vram_range_dirty(v,sat,640u)||sprite_sources_dirty(v,s))p|=MD2GBA_FRAME_SPRITES;
        unsigned hm=v->regs[11]&3u;
        unsigned hs_bytes=(hm==0u||hm==1u)?4u:((v->regs[1]&8u)?240u:224u)*4u;
        if(md2gba_vdp_vram_range_dirty(v,hs,hs_bytes))p|=MD2GBA_FRAME_SCROLL;
    }
    /* If a width-fit panel vanishes or changes geometry, rebuild the maps once
       so no presentation-only compacted cells survive after the source UI has
       moved/closed.  Activation itself can use the sparse/deferred path. */
    if(s->ui_active){
        /* Panel grammar lives in Plane-A name words. Do not rescan it for
           palette ramps, pattern-only animation, sprites or ordinary scroll. */
        int ui_names_changed=(v->vram_epoch!=s->last_vram_epoch)&&md2gba_vdp_vram_range_dirty(v,a,map_bytes);
        int ui_geom_changed=(v->regs_epoch!=s->last_regs_epoch)&&!!(v->regs_dirty_mask&((1u<<2)|(1u<<11)|(1u<<12)|(1u<<13)|(1u<<16)));
        if(ui_names_changed||ui_geom_changed){Md2GbaGbaUiPanel up;int ua=md2gba_gba_detect_bottom_ui_panel(v,&up);
            if(!ua || up.x!=s->ui_x || up.y!=s->ui_y || up.w!=s->ui_w || up.h!=s->ui_h || up.palette!=s->ui_palette || s->bg_cache.used_slots>md2gba_ui_slot_base(&up))
                p|=MD2GBA_FRAME_MAPS|MD2GBA_FRAME_TILES;
        }
    }
    return p;
}

unsigned md2gba_gba_commit_dirty_cached_tiles(const Md2GbaVdp*v,const Md2GbaBgTileCache*c){
    unsigned slot,n=0;for(slot=1u;slot<c->used_slots;slot++){unsigned src=c->slot_to_source[slot];if(!md2gba_source_tile_dirty(v,src))continue;gba_copy_tile32_to_vram(GBA_VRAM16_BYTES+slot*16u,&v->vram[(uint16_t)(src*32u)]);n++;}return n;
}

/* P127: this is the capacity/error fallback after both asynchronous queue
   strategies refuse a frame.  Keeping the synchronous VBlank path inside the
   IWRAM hot function consumed scarce zero-wait RAM even though execution here
   is already dominated by a VBlank wait and direct VRAM publication.  Move it
   to ROM without changing its work/order; the normal async path stays hot. */
static MD2GBA_ROM_COLD int md2gba_gba_commit_priority_sync_fallback(
        Md2GbaVdp*v,Md2GbaGbaFrameState*s,
        unsigned ah,unsigned bh,unsigned al,unsigned bl,uint16_t stream[160][8],
        unsigned ox,unsigned oy,uint32_t plan,unsigned patch_old_used,
        const Md2GbaGbaUiPanel*ui,int ui_fit,int direct_scroll_done){
    int r;unsigned dropped=0u;
    unsigned w=md2gba_vdp_plane_width(v),h=md2gba_vdp_plane_height(v);
    uint16_t a=md2gba_vdp_plane_a_base(v),b=md2gba_vdp_plane_b_base(v);
    if(plan&(MD2GBA_FRAME_PALETTE|MD2GBA_FRAME_MAPS|MD2GBA_FRAME_MAP_PATCH|MD2GBA_FRAME_TILES|MD2GBA_FRAME_SPRITES|MD2GBA_FRAME_SCROLL|MD2GBA_FRAME_FULL))
        gba_wait_vblank_start();

    if(plan&MD2GBA_FRAME_PALETTE){md2gba_gba_commit_cram(v);s->palette_updates++;}
    if(plan&MD2GBA_FRAME_MAPS){
        gba_dma3_copy16(GBA_VRAM16_BYTES,md2gba_bg_tile_stage,s->bg_cache.used_slots*16u);
    } else if(plan&MD2GBA_FRAME_MAP_PATCH){
        md2gba_gba_commit_staged_patch_tiles(v,&s->bg_cache,patch_old_used);
        md2gba_gba_patch_name_table_split(v,a,ah,al,w,h,&s->bg_cache);
        md2gba_gba_patch_name_table_split(v,b,bh,bl,w,h,&s->bg_cache);
        s->map_patches++;
    } else if(plan&MD2GBA_FRAME_TILES){
        md2gba_gba_commit_staged_dirty_tiles(v,&s->bg_cache);
    }
    if(ui_fit){
        if((plan&MD2GBA_FRAME_MAPS)||!s->ui_active||(plan&(MD2GBA_FRAME_TILES|MD2GBA_FRAME_MAP_PATCH)))
            md2gba_ui_direct_tiles(v,ui,w);
        if(!(plan&MD2GBA_FRAME_MAPS) && (!s->ui_active||(plan&MD2GBA_FRAME_MAP_PATCH)))
            md2gba_ui_direct_map(v,ui,&s->bg_cache,w,ah,ox);
    }
    if(plan&MD2GBA_FRAME_SPRITES){md2gba_gba_commit_sprites_packed_viewport(v,128u,&dropped,ox,oy);s->sprite_source_count=md2gba_vdp_collect_sprite_tiles(v,s->sprite_source_tiles,128u);s->sprite_rebuilds++;}
    if((plan&MD2GBA_FRAME_SCROLL)&&stream){
        if(((v->regs[11]&3u)&2u)==0u){md2gba_gba_stop_hblank_scroll_dma();REG_BG0HOFS=stream[0][0];REG_BG0VOFS=stream[0][1];REG_BG1HOFS=stream[0][2];REG_BG1VOFS=stream[0][3];REG_BG2HOFS=stream[0][4];REG_BG2VOFS=stream[0][5];REG_BG3HOFS=stream[0][6];REG_BG3VOFS=stream[0][7];}
        else {md2gba_gba_start_hblank_scroll_dma((const uint16_t (*)[8])stream);}
        s->scroll_updates++;
    } else if(direct_scroll_done) s->scroll_updates++;
    if(plan&(MD2GBA_FRAME_FULL|MD2GBA_FRAME_MAPS)){if((r=md2gba_gba_init_display_priority_vdp(v,ah,bh,al,bl)))return r;}
    s->last_vram_epoch=v->vram_epoch;s->last_cram_epoch=v->cram_epoch;s->last_vsram_epoch=v->vsram_epoch;s->last_regs_epoch=v->regs_epoch;
    s->last_view_x=(uint16_t)ox;s->last_view_y=(uint16_t)oy;s->initialized=1u;s->ui_active=(uint8_t)ui_fit;if(ui_fit){s->ui_x=ui->x;s->ui_y=ui->y;s->ui_w=ui->w;s->ui_h=ui->h;s->ui_palette=ui->palette;}md2gba_vdp_clear_dirty(v);(void)dropped;return 0;
}
#if defined(MD2GBA_GBA_IWRAM)
__attribute__((section(".iwram.backend"),noinline))
#endif
static int md2gba_gba_commit_priority_incremental_viewport_plan(Md2GbaVdp*v,Md2GbaGbaFrameState*s,unsigned ah,unsigned bh,unsigned al,unsigned bl,uint16_t stream[160][8],unsigned ox,unsigned oy,uint32_t plan){
    int r=0;unsigned dropped=0,updated=0,patch_old_used=0;
    unsigned w,h;uint16_t a,b;Md2GbaGbaUiPanel ui;int ui_fit=0;
    if(!s)return -20;
    /* P87b / PSC-style presentation: whole-plane scroll is live hardware
       state, not part of the heavyweight tile/map transaction. Publish the
       newest camera offsets even while an older deferred map update owns the
       commit queue. Physical VBlank applies this latch after any queued frame,
       so stale map work cannot pin the camera in place. */
    int direct_scroll_done=0;
    if((plan&MD2GBA_FRAME_SCROLL) && ((v->regs[11]&2u)==0u)){
        uint16_t direct[8];
        if(md2gba_gba_build_direct_scroll_viewport(v,direct,ox,oy)==0){
            md2gba_gba_scroll_latch_direct(direct);
            direct_scroll_done=1;
            /* Fixed-plane scroll is already live GBA hardware state. Avoid
               building/queueing the 160x8 HBlank stream for it. */
            plan&=~MD2GBA_FRAME_SCROLL;
        }
    }
    /* The queue owns md2gba_bg_tile_stage until the physical VBlank IRQ has
       consumed it.  Preserve any later VDP dirtiness and retry on a future
       scheduler yield instead of overwriting the staged frame. */
    if(md2gba_hwq.pending)return 0;
    /* P105: CRAM-only presentation never needs geometry/UI/tile/sprite work. */
    if(plan==MD2GBA_FRAME_PALETTE){
        s->frames++;
        md2gba_hwq_reset_stage();
        md2gba_hwq_stage_palette(v);
        s->palette_updates++;
        s->last_vram_epoch=v->vram_epoch;s->last_cram_epoch=v->cram_epoch;
        s->last_vsram_epoch=v->vsram_epoch;s->last_regs_epoch=v->regs_epoch;
        s->last_view_x=(uint16_t)ox;s->last_view_y=(uint16_t)oy;s->initialized=1u;
        md2gba_vdp_clear_dirty(v);
        if(md2gba_hwq.palette_valid)md2gba_hwq_publish();
        return 0;
    }
    w=md2gba_vdp_plane_width(v);h=md2gba_vdp_plane_height(v);
    a=md2gba_vdp_plane_a_base(v);b=md2gba_vdp_plane_b_base(v);
    if(s->initialized&&(s->last_view_x!=ox||s->last_view_y!=oy)){
        /* Map contents do not change when the crop moves, but sprite screen
           coordinates and BG offsets do. */
        plan|=MD2GBA_FRAME_SPRITES|MD2GBA_FRAME_SCROLL;
    }
    /* P84: if the already-computed plan proves that no visible state changed,
       do not traverse maps/sprites/UI a second time.  Consume nonvisual VDP
       dirtiness/epochs so register housekeeping does not retrigger planning. */
    if(plan==0u){
        s->frames++;
        if(direct_scroll_done)s->scroll_updates++;
        s->last_vram_epoch=v->vram_epoch;s->last_cram_epoch=v->cram_epoch;
        s->last_vsram_epoch=v->vsram_epoch;s->last_regs_epoch=v->regs_epoch;
        s->last_view_x=(uint16_t)ox;s->last_view_y=(uint16_t)oy;
        md2gba_vdp_clear_dirty(v);return 0;
    }
    s->frames++;
    if(plan&MD2GBA_FRAME_FULL)s->full_rebuilds++;

    /* Prepare everything that can safely be prepared off-screen.  Full maps
       still use the inactive bank.  P75 sparse patches preserve cache slots,
       append any newly referenced tiles, and stage only new/actually dirty
       pattern data before touching the active maps at VBlank. */
    if(plan&MD2GBA_FRAME_MAPS){
        if((r=md2gba_gba_build_bg_tile_cache(v,a,b,w,h,&s->bg_cache))) return r;
        if((r=md2gba_gba_stage_cached_tiles(v,&s->bg_cache))) return -3;
        if((r=md2gba_gba_commit_name_table_split_full(v,a,ah,al,w,h,&s->bg_cache))) return -4;
        if((r=md2gba_gba_commit_name_table_split_full(v,b,bh,bl,w,h,&s->bg_cache))) return -5;
        s->map_rebuilds++;
    } else if(plan&MD2GBA_FRAME_MAP_PATCH){
        unsigned bytes=w*h*2u;
        patch_old_used=s->bg_cache.used_slots;
        if(md2gba_extend_cache_for_dirty_map(v,a,bytes,&s->bg_cache)||md2gba_extend_cache_for_dirty_map(v,b,bytes,&s->bg_cache))return -8;
        updated=md2gba_gba_stage_patch_tiles(v,&s->bg_cache,patch_old_used);
        s->tile_updates+=updated;
    } else if(plan&MD2GBA_FRAME_TILES){
        updated=md2gba_gba_stage_dirty_cached_tiles(v,&s->bg_cache);
        s->tile_updates+=updated;
    }

    /* P105: the framed-UI grammar lives in the name table.  Palette, pattern
       pixels, sprites and ordinary scroll cannot create/destroy that grammar,
       so do not rescan the source map on those frames.  Reuse both positive
       and negative cached results until maps/name-table state is invalidated. */
    if(plan&(MD2GBA_FRAME_FULL|MD2GBA_FRAME_MAPS|MD2GBA_FRAME_MAP_PATCH)){
        ui_fit=md2gba_gba_detect_bottom_ui_panel(v,&ui) && md2gba_ui_fit_eligible(&ui,&s->bg_cache,w,ox,oy);
    }else if(s->ui_active){
        ui.active=1u;ui.x=s->ui_x;ui.y=s->ui_y;ui.w=s->ui_w;ui.h=s->ui_h;ui.palette=s->ui_palette;
        ui_fit=md2gba_ui_fit_eligible(&ui,&s->bg_cache,w,ox,oy);
    }else{
        ui.active=0u;ui.x=ui.y=ui.w=ui.h=ui.palette=0u;ui_fit=0;
    }
    if(ui_fit && (plan&MD2GBA_FRAME_MAPS)){
        /* Full maps are being built in the inactive screenblock bank, so the
           compacted UI names can be written there before the VBlank switch. */
        md2gba_ui_stage_tiles(v,&ui,w);
        md2gba_ui_direct_map(v,&ui,&s->bg_cache,w,ah,ox);
    }

    if((plan&MD2GBA_FRAME_SCROLL)&&stream){
        if(md2gba_gba_build_scroll_stream_viewport(v,stream,ox,oy)!=0)
            plan&=~MD2GBA_FRAME_SCROLL;
    }

    /* P120: full-map rebuilds used to block here waiting for physical VBlank.
       Queue the coherent EWRAM pattern cache + register bank switch instead.
       The full name tables (and fitted UI names) were written above to the
       inactive screenblock bank, so they are safe to prepare outside VBlank. */
    if(plan&MD2GBA_FRAME_MAPS){
        int qok=md2gba_hwq_prepare_full(v,&s->bg_cache,plan,(const uint16_t (*)[8])stream,ox,oy,ah,bh,al,bl);
        if(qok && ui_fit){
            /* Full-cache DMA covers only source-cache slots. UI scratch tiles
               live above used_slots and are appended as bounded slot uploads. */
            if(md2gba_ui_queue(v,&ui,&s->bg_cache,w,ah,ox,oy,1,0,1))qok=0;
        }
        if(qok){
            if(plan&MD2GBA_FRAME_PALETTE)s->palette_updates++;
            if(plan&MD2GBA_FRAME_SPRITES){s->sprite_source_count=md2gba_vdp_collect_sprite_tiles(v,s->sprite_source_tiles,128u);s->sprite_rebuilds++;}
            if((plan&MD2GBA_FRAME_SCROLL)||direct_scroll_done)s->scroll_updates++;
            s->last_vram_epoch=v->vram_epoch;s->last_cram_epoch=v->cram_epoch;s->last_vsram_epoch=v->vsram_epoch;s->last_regs_epoch=v->regs_epoch;
            s->last_view_x=(uint16_t)ox;s->last_view_y=(uint16_t)oy;s->initialized=1u;
            s->ui_active=(uint8_t)ui_fit;if(ui_fit){s->ui_x=ui.x;s->ui_y=ui.y;s->ui_w=ui.w;s->ui_h=ui.h;s->ui_palette=ui.palette;}
            md2gba_vdp_clear_dirty(v);
            md2gba_hwq_publish();
            (void)dropped;return 0;
        }
    }

    /* P82: the common dialogue/glyph case is a small map/tile update on the
       active hardware renderer.  Stage a bounded snapshot and let the already
       installed physical VBlank IRQ apply it, so translated 68K execution does
       not sit in gba_wait_vblank_start for up to 16.7 ms. */
    if(plan){
        int qok=0,skip_async=0;
        /* PF01: account for the UI name writes before building the small queue.
           A map patch can be individually legal (<=512 writes) yet become
           impossible once a fitted dialogue panel appends h*30 map entries.
           Select the synchronous ownership path up front instead of partially
           constructing a queue that is guaranteed to be rejected. */
        if(ui_fit && (plan&MD2GBA_FRAME_MAP_PATCH)){
            unsigned map_blocks=md2gba_map_dirty_blocks(v,a,w*h*2u)+md2gba_map_dirty_blocks(v,b,w*h*2u);
            unsigned map_writes=map_blocks*32u,ui_writes=(unsigned)ui.h*MD2GBA_UI_OUT_TILES;
            if(map_writes+ui_writes>MD2GBA_HWQ_MAX_MAP_WRITES)skip_async=1;
        }
        if(!skip_async)qok=md2gba_hwq_prepare_small(v,&s->bg_cache,plan,patch_old_used,a,b,w,h,ah,bh,al,bl,(const uint16_t (*)[8])stream,ox,oy);
        if(qok && ui_fit && !(plan&MD2GBA_FRAME_MAPS)){
            int upload=(!s->ui_active)||!!(plan&(MD2GBA_FRAME_TILES|MD2GBA_FRAME_MAP_PATCH));
            int names=(!s->ui_active)||!!(plan&MD2GBA_FRAME_MAP_PATCH);
            if((upload||names) && md2gba_ui_queue(v,&ui,&s->bg_cache,w,ah,ox,oy,upload,names,0))qok=0;
        }
        if(qok){
            if(plan&MD2GBA_FRAME_PALETTE)s->palette_updates++;
            if(plan&MD2GBA_FRAME_MAP_PATCH)s->map_patches++;
            if(plan&MD2GBA_FRAME_SPRITES){s->sprite_source_count=md2gba_vdp_collect_sprite_tiles(v,s->sprite_source_tiles,128u);s->sprite_rebuilds++;}
            if((plan&MD2GBA_FRAME_SCROLL)||direct_scroll_done)s->scroll_updates++;
            s->last_vram_epoch=v->vram_epoch;s->last_cram_epoch=v->cram_epoch;s->last_vsram_epoch=v->vsram_epoch;s->last_regs_epoch=v->regs_epoch;
            s->last_view_x=(uint16_t)ox;s->last_view_y=(uint16_t)oy;s->initialized=1u;
            s->ui_active=(uint8_t)ui_fit;if(ui_fit){s->ui_x=ui.x;s->ui_y=ui.y;s->ui_w=ui.w;s->ui_h=ui.h;s->ui_palette=ui.palette;}
            md2gba_vdp_clear_dirty(v);
            md2gba_hwq_publish();
            (void)dropped;return 0;
        }
    }

    return md2gba_gba_commit_priority_sync_fallback(v,s,ah,bh,al,bl,stream,ox,oy,plan,patch_old_used,&ui,ui_fit,direct_scroll_done);
}

int md2gba_gba_commit_priority_incremental_viewport_planned(Md2GbaVdp*v,Md2GbaGbaFrameState*s,unsigned ah,unsigned bh,unsigned al,unsigned bl,uint16_t stream[160][8],unsigned ox,unsigned oy,uint32_t plan){
    return md2gba_gba_commit_priority_incremental_viewport_plan(v,s,ah,bh,al,bl,stream,ox,oy,plan);
}
int md2gba_gba_commit_priority_incremental_viewport(Md2GbaVdp*v,Md2GbaGbaFrameState*s,unsigned ah,unsigned bh,unsigned al,unsigned bl,uint16_t stream[160][8],unsigned ox,unsigned oy){
    return md2gba_gba_commit_priority_incremental_viewport_plan(v,s,ah,bh,al,bl,stream,ox,oy,md2gba_gba_plan_frame(v,s));
}
int md2gba_gba_commit_priority_incremental_coop(Md2GbaVdp*v,Md2GbaGbaFrameState*s,unsigned ah,unsigned bh,unsigned al,unsigned bl,uint16_t stream[160][8],Md2GbaGbaYieldFn yield_fn,void*yield_user){
    int r=md2gba_gba_commit_priority_incremental_viewport(v,s,ah,bh,al,bl,stream,0u,0u);if(yield_fn)yield_fn(yield_user);return r;
}
int md2gba_gba_commit_priority_incremental(Md2GbaVdp*v,Md2GbaGbaFrameState*s,unsigned ah,unsigned bh,unsigned al,unsigned bl,uint16_t stream[160][8]){return md2gba_gba_commit_priority_incremental_viewport(v,s,ah,bh,al,bl,stream,0u,0u);}



static unsigned dim_parts(unsigned d,unsigned out[2]){if(d==3u){out[0]=2u;out[1]=1u;return 2u;}out[0]=d;return 1u;}
/* Genesis sprites may legally sit well outside the cropped GBA viewport.
   GBA OBJ coordinates wrap (9-bit X / 8-bit Y), so blindly masking a fully
   off-screen negative coordinate can make a hidden source sprite reappear on
   the opposite side of the display. Cull only pieces with no intersection;
   partially visible negative-edge pieces are retained so hardware wrapping
   still clips them correctly. */
static int md2gba_sprite_piece_visible(int px,int py,unsigned pw,unsigned ph){
    return px < 240 && py < 160 && px+(int)(pw*8u) > 0 && py+(int)(ph*8u) > 0;
}

static int gba_obj_shape_size(unsigned w,unsigned h,unsigned*shape,unsigned*size){
    if(w==h){*shape=0u;if(w==1u)*size=0u;else if(w==2u)*size=1u;else if(w==4u)*size=2u;else return -1;return 0;}
    if(h==1u&&(w==2u||w==4u)){*shape=1u;*size=w==2u?0u:1u;return 0;}
    if(w==1u&&(h==2u||h==4u)){*shape=2u;*size=h==2u?0u:1u;return 0;}
    if(w==4u&&h==2u){*shape=1u;*size=2u;return 0;}
    if(w==2u&&h==4u){*shape=2u;*size=2u;return 0;}
    return -1;
}
unsigned md2gba_vdp_sprite_piece_count(const Md2GbaVdp*v){unsigned n=0,entry=0,guard=0,base=md2gba_vdp_sat_base(v);while(guard++<80u){unsigned a=(base+entry*8u)&0xFFFFu,sz=v->vram[(uint16_t)(a+2u)],link=v->vram[(uint16_t)(a+3u)]&0x7Fu,w=((sz>>2)&3u)+1u,h=(sz&3u)+1u,wp[2],hp[2];n+=dim_parts(w,wp)*dim_parts(h,hp);if(!link) break;
        entry=link;}return n;}
static void copy_sprite_piece(const Md2GbaVdp*v,unsigned dst,unsigned base_tile,unsigned full_h,unsigned x0,unsigned y0,unsigned w,unsigned h){unsigned sy,sx;for(sy=0;sy<h;sy++)for(sx=0;sx<w;sx++){unsigned src=base_tile+(x0+sx)*full_h+(y0+sy),dt=dst+sy*w+sx;gba_copy_tile32_to_vram(GBA_OBJ_VRAM16+dt*16u,&v->vram[(uint16_t)(src*32u)]);}}

static MD2GBA_ROM_COLD int md2gba_hwq_stage_sprites(const Md2GbaVdp*v,unsigned ox,unsigned oy){
    unsigned obj=0u,tiles=0u,entry=0u,guard=0u,base=md2gba_vdp_sat_base(v),bank,bank_base,i;
    if(!md2gba_obj_doublebuffer_safe)return -1;
    bank=md2gba_obj_active_bank^1u;bank_base=bank?512u:0u;
    while(guard++<80u){
        unsigned a=(base+entry*8u)&0xFFFFu;
        uint16_t yw=vram_word(v,a),attr=vram_word(v,a+4u),xw=vram_word(v,a+6u);
        unsigned sz=v->vram[(uint16_t)(a+2u)],link=v->vram[(uint16_t)(a+3u)]&0x7Fu;
        unsigned fw=((sz>>2)&3u)+1u,fh=(sz&3u)+1u,base_tile=attr&0x7FFu;
        unsigned hf=!!(attr&0x0800u),vf=!!(attr&0x1000u),pal=(attr>>13)&3u,hi=!!(attr&0x8000u);
        unsigned wp[2],hp[2],wn=dim_parts(fw,wp),hn=dim_parts(fh,hp),xi,yi,x0=0u;
        int x=(int)(xw&0x1FFu)-128-(int)ox,y=(int)(yw&0x3FFu)-128-(int)oy;
        for(xi=0u;xi<wn;xi++){
            unsigned y0=0u;
            for(yi=0u;yi<hn;yi++){
                unsigned pw=wp[xi],ph=hp[yi],shape,size,need=pw*ph;
                int px=x+(int)(hf?(fw-x0-pw):x0)*8,py=y+(int)(vf?(fh-y0-ph):y0)*8;
                if(!md2gba_sprite_piece_visible(px,py,pw,ph)){y0+=ph;continue;}
                if(obj>=128u||tiles+need>512u||gba_obj_shape_size(pw,ph,&shape,&size))return -1;
                copy_sprite_piece(v,bank_base+tiles,base_tile,fh,x0,y0,pw,ph);
                md2gba_hwq.oam[obj][0]=(uint16_t)((py&0xFF)|(shape<<14));
                md2gba_hwq.oam[obj][1]=(uint16_t)((px&0x1FF)|(hf?0x1000u:0u)|(vf?0x2000u:0u)|(size<<14));
                md2gba_hwq.oam[obj][2]=(uint16_t)(((bank_base+tiles)&0x3FFu)|((hi?0u:2u)<<10)|(pal<<12));
                md2gba_hwq.oam[obj][3]=0u;
                tiles+=need;obj++;y0+=ph;
            }
            x0+=wp[xi];
        }
        if(!link)break;
        entry=link;
    }
    for(i=obj;i<128u;i++){md2gba_hwq.oam[i][0]=0x0200u;md2gba_hwq.oam[i][1]=md2gba_hwq.oam[i][2]=md2gba_hwq.oam[i][3]=0u;}
    md2gba_hwq.sprite_bank=(uint8_t)bank;md2gba_hwq.sprite_valid=1u;
    return 0;
}
unsigned md2gba_gba_commit_sprites_packed_viewport(const Md2GbaVdp*v,unsigned max_objs,unsigned*dropped,unsigned ox,unsigned oy){
    unsigned obj=0,drop=0,tiles=0,entry=0,guard=0,base=md2gba_vdp_sat_base(v),i;if(max_objs>128u)max_objs=128u;
    while(guard++<80u){unsigned a=(base+entry*8u)&0xFFFFu;uint16_t yw=vram_word(v,a),attr=vram_word(v,a+4u),xw=vram_word(v,a+6u);unsigned sz=v->vram[(uint16_t)(a+2u)],link=v->vram[(uint16_t)(a+3u)]&0x7Fu,fw=((sz>>2)&3u)+1u,fh=(sz&3u)+1u,base_tile=attr&0x7FFu,hf=!!(attr&0x0800u),vf=!!(attr&0x1000u),pal=(attr>>13)&3u,hi=!!(attr&0x8000u),wp[2],hp[2],wn=dim_parts(fw,wp),hn=dim_parts(fh,hp),xi,yi,x0=0;int x=(int)(xw&0x1FFu)-128-(int)ox,y=(int)(yw&0x3FFu)-128-(int)oy;
        for(xi=0;xi<wn;xi++){unsigned y0=0;for(yi=0;yi<hn;yi++){unsigned pw=wp[xi],ph=hp[yi],shape,size,need=pw*ph;int px=x+(int)(hf?(fw-x0-pw):x0)*8,py=y+(int)(vf?(fh-y0-ph):y0)*8;if(!md2gba_sprite_piece_visible(px,py,pw,ph)){y0+=ph;continue;}if(obj>=max_objs||tiles+need>1024u||gba_obj_shape_size(pw,ph,&shape,&size)){drop++;y0+=ph;continue;}copy_sprite_piece(v,tiles,base_tile,fh,x0,y0,pw,ph);gba_oam_write(obj,(uint16_t)((py&0xFF)|(shape<<14)),(uint16_t)((px&0x1FF)|(hf?0x1000u:0u)|(vf?0x2000u:0u)|(size<<14)),(uint16_t)((tiles&0x3FFu)|((hi?0u:2u)<<10)|(pal<<12)));tiles+=need;obj++;y0+=ph;}x0+=wp[xi];}
        if(!link) break;
        entry=link;
    }
    for(i=obj;i<128u;i++){gba_oam_write(i,0x0200u,0u,0u);}
    md2gba_obj_active_bank=0u;md2gba_obj_doublebuffer_safe=(uint8_t)(tiles<=512u);
    REG_DISPCNT|=(uint16_t)((1u<<12)|(1u<<6));if(dropped)*dropped=drop;return obj;
}
unsigned md2gba_gba_commit_sprites_packed(const Md2GbaVdp*v,unsigned max_objs,unsigned*dropped){
    return md2gba_gba_commit_sprites_packed_viewport(v,max_objs,dropped,0u,0u);
}

/* -------------------------------------------------------------------------
 * P150 profile-generated direct VDP lowering
 * -------------------------------------------------------------------------
 *
 * The generic P134 projected renderer is deliberately conservative and can
 * spend a large amount of ARM7 time rebuilding source->slot ownership and
 * resampling a 320x224 Genesis display.  The desktop profiler can instead
 * prove a cheaper representation for ordinary text-plane scenes.  The host
 * compiler clusters the actually observed Genesis source-pattern sets and
 * emits one or more fixed source->GBA-slot maps.  Source IDs may therefore be
 * anywhere in the Genesis 0..2047 pattern space while each generated mapping
 * stays within the GBA text-BG limit (slots 1..1023).
 *
 * GBA slot zero is permanently blank, so high/low priority split maps can use
 * zero safely.  Full-map scans happen only when geometry/base state changes or
 * when a new source pattern requires another prebuilt mapping; ordinary frames
 * enumerate the VDP's existing dirty bitsets.  Any runtime state outside the
 * profiled contract returns a nonzero guard code without clearing dirtiness,
 * allowing main to fall back to the canonical renderer rather than guessing.
 */
#define MD2GBA_DIRECT_AH0 16u
#define MD2GBA_DIRECT_BH0 18u
#define MD2GBA_DIRECT_AL0 20u
#define MD2GBA_DIRECT_BL0 22u
#define MD2GBA_DIRECT_AH1 24u
#define MD2GBA_DIRECT_BH1 26u
#define MD2GBA_DIRECT_AL1 28u
#define MD2GBA_DIRECT_BL1 30u
#define MD2GBA_DIRECT_NO_MAP 0xFFFFu

static int md2gba_direct_window_active(const Md2GbaVdp*v){
    unsigned hc=((v->regs[12]&0x81u)==0x81u)?40u:32u;
    unsigned vr=(v->regs[1]&0x08u)?30u:28u;
    unsigned hp=(v->regs[17]&0x1fu)*2u,vp=v->regs[18]&0x1fu;
    int h,w;
    if(hp>hc)hp=hc;if(vp>vr)vp=vr;
    h=(v->regs[17]&0x80u)?(hp<hc):(hp>0u);
    w=(v->regs[18]&0x80u)?(vp<vr):(vp>0u);
    return h||w;
}

void md2gba_gba_compiled_vdp_reset(Md2GbaCompiledVdpState*s){
    if(!s)return;
    {uint8_t*p=(uint8_t*)s;unsigned i;for(i=0u;i<sizeof(*s);i++)p[i]=0u;}
    s->map_profile=MD2GBA_DIRECT_NO_MAP;
    md2gba_gba_hw_cancel_pending();
    md2gba_gba_stop_hblank_scroll_dma();
}

int md2gba_gba_compiled_vdp_eligible(const Md2GbaVdp*v){
    unsigned w,h;
    if(!v)return 0;
    w=md2gba_vdp_plane_width(v);h=md2gba_vdp_plane_height(v);
    if((w!=32u&&w!=64u)||h!=32u)return 0;
    if(v->regs[11]&4u)return 0;              /* column V-scroll */
    if(v->regs[12]&8u)return 0;              /* shadow/highlight */
    if(md2gba_direct_window_active(v))return 0;
    return 1;
}

static int md2gba_direct_collect_refs(const Md2GbaVdp*v,uint16_t base,unsigned w,unsigned h,uint32_t refs[64]){
    unsigned x,y;
    if((w!=32u&&w!=64u)||h!=32u)return -1;
    for(y=0u;y<h;y++)for(x=0u;x<w;x++){
        unsigned src=vram_word(v,(unsigned)base+2u*(y*w+x))&0x07ffu;
        refs[src>>5]|=1u<<(src&31u);
    }
    return 0;
}

static int md2gba_direct_map_covers(unsigned mapid,const uint32_t refs[64]){
    unsigned i;if(mapid>=MD2GBA_COMPILED_VDP_MAP_COUNT)return 0;
    for(i=0u;i<64u;i++)if(refs[i]&~md2gba_compiled_vdp_source_masks[mapid][i])return 0;
    return 1;
}

static int md2gba_direct_select_map(const uint32_t refs[64],unsigned preferred){
    unsigned i;
    if(preferred<MD2GBA_COMPILED_VDP_MAP_COUNT&&md2gba_direct_map_covers(preferred,refs))return (int)preferred;
    for(i=0u;i<MD2GBA_COMPILED_VDP_MAP_COUNT;i++)if(md2gba_direct_map_covers(i,refs))return (int)i;
    return -1;
}

static int md2gba_direct_name(uint16_t m,const uint16_t*map,uint16_t*out){
    unsigned src=m&0x07ffu,slot=map[src];
    uint16_t h,v,pal;
    if(!slot||slot>1023u)return -1;
    h=(uint16_t)((m&0x0800u)>>1);
    v=(uint16_t)((m&0x1000u)>>1);
    pal=(uint16_t)((m&0x6000u)>>1);
    *out=(uint16_t)((slot&0x03ffu)|h|v|pal);
    return 0;
}

static int md2gba_direct_write_full_map(const Md2GbaVdp*v,uint16_t base,
                                         unsigned hi_sb,unsigned lo_sb,
                                         unsigned w,unsigned h,const uint16_t*map){
    unsigned x,y,bx,by,bxs=w>>5;
    if((w!=32u&&w!=64u)||h!=32u)return -1;
    for(y=0u;y<h;y++)for(x=0u;x<w;x++){
        uint16_t m=vram_word(v,(unsigned)base+2u*(y*w+x)),out;
        unsigned cell=(y&31u)*32u+(x&31u);
        if(md2gba_direct_name(m,map,&out))return -2;
        bx=x>>5;by=y>>5;
        {unsigned hs=hi_sb+by*bxs+bx,ls=lo_sb+by*bxs+bx;
         volatile uint16_t*hd=GBA_VRAM16+hs*1024u,*ld=GBA_VRAM16+ls*1024u;
         if(m&0x8000u){hd[cell]=out;ld[cell]=0u;}else{hd[cell]=0u;ld[cell]=out;}}
    }
    return 0;
}

static void md2gba_direct_stage_tile(const Md2GbaVdp*v,unsigned src,unsigned slot){
    gba_convert_tile32_to_ram(md2gba_bg_tile_stage+slot*16u,&v->vram[(uint16_t)(src*32u)]);
}

static void md2gba_direct_stage_mapping(const Md2GbaVdp*v,unsigned mapid){
    const uint16_t*map=md2gba_compiled_vdp_source_to_slot[mapid];
    unsigned src;
    for(src=0u;src<16u;src++)md2gba_bg_tile_stage[src]=0u;
    for(src=0u;src<2048u;src++){unsigned slot=map[src];if(slot)md2gba_direct_stage_tile(v,src,slot);}
}

static unsigned md2gba_direct_dirty_tiles(const Md2GbaVdp*v,unsigned mapid){
    const uint16_t*map=md2gba_compiled_vdp_source_to_slot[mapid];
    unsigned wi,n=0u;
    for(wi=0u;wi<64u;wi++){
        uint32_t bits=v->vram_dirty_tiles[wi];
        while(bits){unsigned src=wi*32u+md2gba_lowbit_index(bits);bits&=bits-1u;if(map[src])n++;}
    }
    return n;
}

/* Return -3 when a newly referenced pattern is not represented by the current
   generated mapping. The caller can rescan the complete live maps and select a
   different prebuilt mapping without guessing code/data semantics. */
static int md2gba_direct_patch_map(const Md2GbaVdp*v,uint16_t base,
                                    unsigned hi_sb,unsigned lo_sb,unsigned w,unsigned h,
                                    unsigned mapid){
    const uint16_t*map=md2gba_compiled_vdp_source_to_slot[mapid];
    unsigned bytes=w*h*2u,off,i,bxs=w>>5,shift=(w==64u)?6u:5u;
    for(off=0u;off<bytes;off+=32u){
        if(!md2gba_vram_block32_dirty(v,(unsigned)base+off))continue;
        for(i=0u;i<16u&&off+i*2u<bytes;i++){
            unsigned idx=(off>>1)+i,y=idx>>shift,x=idx&(w-1u),bx=x>>5,by=y>>5;
            unsigned cell=(y&31u)*32u+(x&31u),hisb=hi_sb+by*bxs+bx,losb=lo_sb+by*bxs+bx;
            uint16_t m=vram_word(v,(unsigned)base+off+i*2u),out;unsigned q;
            if(md2gba_direct_name(m,map,&out))return -3;
            if((unsigned)md2gba_hwq.map_write_count+2u>MD2GBA_HWQ_MAX_MAP_WRITES)return -2;
            q=md2gba_hwq.map_write_count;
            md2gba_hwq.map_index[q]=(uint16_t)(hisb*1024u+cell);
            md2gba_hwq.map_value[q]=(m&0x8000u)?out:0u;
            md2gba_hwq.map_index[q+1u]=(uint16_t)(losb*1024u+cell);
            md2gba_hwq.map_value[q+1u]=(m&0x8000u)?0u:out;
            md2gba_hwq.map_write_count=(uint16_t)(q+2u);
        }
    }
    return 0;
}

static int md2gba_direct_sprite_sources_dirty(const Md2GbaVdp*v,const Md2GbaCompiledVdpState*s){
    unsigned i;
    if(s->sprite_source_count>128u){for(i=0u;i<64u;i++)if(v->vram_dirty_tiles[i])return 1;return 0;}
    for(i=0u;i<s->sprite_source_count;i++){unsigned src=s->sprite_source_tiles[i];if(src<2048u&&(v->vram_dirty_tiles[src>>5]&(1u<<(src&31u))))return 1;}
    return 0;
}

static void md2gba_direct_screenblocks(const Md2GbaCompiledVdpState*s,unsigned*ah,unsigned*bh,unsigned*al,unsigned*bl){
    if(s->map_bank){*ah=MD2GBA_DIRECT_AH1;*bh=MD2GBA_DIRECT_BH1;*al=MD2GBA_DIRECT_AL1;*bl=MD2GBA_DIRECT_BL1;}
    else {*ah=MD2GBA_DIRECT_AH0;*bh=MD2GBA_DIRECT_BH0;*al=MD2GBA_DIRECT_AL0;*bl=MD2GBA_DIRECT_BL0;}
}

static void md2gba_direct_finish_state(Md2GbaVdp*v,Md2GbaCompiledVdpState*s,unsigned ox,unsigned oy){
    s->last_vram_epoch=v->vram_epoch;s->last_cram_epoch=v->cram_epoch;
    s->last_vsram_epoch=v->vsram_epoch;s->last_regs_epoch=v->regs_epoch;
    s->last_view_x=(uint16_t)ox;s->last_view_y=(uint16_t)oy;s->initialized=1u;
    s->width=(uint8_t)md2gba_vdp_plane_width(v);s->height=(uint8_t)md2gba_vdp_plane_height(v);
    md2gba_vdp_clear_dirty(v);
}

int md2gba_gba_compiled_vdp_present(Md2GbaVdp*v,Md2GbaCompiledVdpState*s,
                                     uint16_t stream[160][8],unsigned ox,unsigned oy){
    uint32_t plan=0u;unsigned w,h,map_bytes,ah,bh,al,bl;
    uint16_t a,b,sat,hs;int line_scroll,direct_scroll_done=0;
    if(!v||!s||!stream)return -20;
    if(!md2gba_gba_compiled_vdp_eligible(v))return -21;
    w=md2gba_vdp_plane_width(v);h=md2gba_vdp_plane_height(v);map_bytes=w*h*2u;
    a=md2gba_vdp_plane_a_base(v);b=md2gba_vdp_plane_b_base(v);sat=(uint16_t)md2gba_vdp_sat_base(v);hs=md2gba_vdp_hscroll_base(v);
    line_scroll=!!(v->regs[11]&2u);

    /* A pending VBlank payload owns the shared pattern staging buffer. Keep
       guest execution running; accumulated VDP dirtiness is consumed after the
       physical VBlank publishes the prior payload. */
    if(md2gba_hwq.pending){
        if(!line_scroll)md2gba_gba_publish_live_whole_plane_scroll(v,ox,oy);
        return 0;
    }

    if(!s->initialized){
        plan=MD2GBA_FRAME_FULL|MD2GBA_FRAME_PALETTE|MD2GBA_FRAME_MAPS|MD2GBA_FRAME_TILES|MD2GBA_FRAME_SPRITES|MD2GBA_FRAME_SCROLL;
    }else{
        if(v->regs_epoch!=s->last_regs_epoch){
            uint32_t rm=v->regs_dirty_mask;
            const uint32_t geom=(1u<<2)|(1u<<4)|(1u<<12)|(1u<<16);
            if(rm&geom)plan|=MD2GBA_FRAME_FULL|MD2GBA_FRAME_MAPS|MD2GBA_FRAME_TILES|MD2GBA_FRAME_SPRITES|MD2GBA_FRAME_SCROLL;
            if(rm&(1u<<5))plan|=MD2GBA_FRAME_SPRITES;
            if(rm&((1u<<11)|(1u<<13)))plan|=MD2GBA_FRAME_SCROLL;
            if(rm&(1u<<7))plan|=MD2GBA_FRAME_PALETTE;
        }
        if(v->cram_epoch!=s->last_cram_epoch)plan|=MD2GBA_FRAME_PALETTE;
        if(v->vsram_epoch!=s->last_vsram_epoch)plan|=MD2GBA_FRAME_SCROLL;
        if(v->vram_epoch!=s->last_vram_epoch){
            unsigned blocks=md2gba_map_dirty_blocks(v,a,map_bytes)+md2gba_map_dirty_blocks(v,b,map_bytes);
            if(blocks){if(blocks>(MD2GBA_HWQ_MAX_MAP_WRITES/32u))plan|=MD2GBA_FRAME_MAPS;else plan|=MD2GBA_FRAME_MAP_PATCH;}
            if(s->map_profile<MD2GBA_COMPILED_VDP_MAP_COUNT && md2gba_direct_dirty_tiles(v,s->map_profile))plan|=MD2GBA_FRAME_TILES;
            if(md2gba_vdp_vram_range_dirty(v,sat,640u)||md2gba_direct_sprite_sources_dirty(v,s))plan|=MD2GBA_FRAME_SPRITES;
            {unsigned hm=v->regs[11]&3u,hs_bytes=(hm==0u||hm==1u)?4u:((v->regs[1]&8u)?240u:224u)*4u;
             if(md2gba_vdp_vram_range_dirty(v,hs,hs_bytes))plan|=MD2GBA_FRAME_SCROLL;}
        }
        if(s->last_view_x!=ox||s->last_view_y!=oy)plan|=MD2GBA_FRAME_SPRITES|MD2GBA_FRAME_SCROLL;
    }
    if(!plan){md2gba_direct_finish_state(v,s,ox,oy);return 0;}

    if((plan&MD2GBA_FRAME_SCROLL)&&!line_scroll){
        uint16_t direct[8];
        if(md2gba_gba_build_direct_scroll_viewport(v,direct,ox,oy)==0){md2gba_gba_scroll_latch_direct(direct);direct_scroll_done=1;plan&=~MD2GBA_FRAME_SCROLL;}
    }
    if((plan&MD2GBA_FRAME_SCROLL)&&line_scroll){
        if(md2gba_gba_build_scroll_stream_viewport(v,stream,ox,oy)!=0)return -22;
        md2gba_gba_start_hblank_scroll_dma((const uint16_t (*)[8])stream);
        plan&=~MD2GBA_FRAME_SCROLL;
    }

full_rebuild:
    if(plan&MD2GBA_FRAME_MAPS){
        uint32_t refs[64];int mapid;unsigned i;const uint16_t*map;
        for(i=0u;i<64u;i++)refs[i]=0u;
        if(md2gba_direct_collect_refs(v,a,w,h,refs)||md2gba_direct_collect_refs(v,b,w,h,refs))return -23;
        mapid=md2gba_direct_select_map(refs,s->initialized?s->map_profile:MD2GBA_DIRECT_NO_MAP);
        if(mapid<0)return -24;
        map=md2gba_compiled_vdp_source_to_slot[(unsigned)mapid];
        if(s->initialized)s->map_bank^=1u;
        md2gba_direct_screenblocks(s,&ah,&bh,&al,&bl);
        md2gba_direct_stage_mapping(v,(unsigned)mapid);
        if(md2gba_direct_write_full_map(v,a,ah,al,w,h,map)||md2gba_direct_write_full_map(v,b,bh,bl,w,h,map))return -25;
        md2gba_hwq_reset_stage();
        md2gba_hwq.full_tiles_valid=1u;
        md2gba_hwq.full_tile_halfwords=(uint16_t)((md2gba_compiled_vdp_slot_count[(unsigned)mapid]+1u)*16u);
        if(plan&MD2GBA_FRAME_PALETTE)md2gba_hwq_stage_palette(v);
        if(md2gba_hwq_stage_display(v,ah,bh,al,bl))return -26;
        if(plan&MD2GBA_FRAME_SPRITES){if(md2gba_hwq_stage_sprites(v,ox,oy))return -27;s->sprite_source_count=(uint16_t)md2gba_vdp_collect_sprite_tiles(v,s->sprite_source_tiles,128u);}
        s->map_profile=(uint16_t)mapid;
        md2gba_direct_finish_state(v,s,ox,oy);
        md2gba_hwq_publish();
        (void)direct_scroll_done;return 0;
    }

    if(s->map_profile>=MD2GBA_COMPILED_VDP_MAP_COUNT)return -28;
    md2gba_direct_screenblocks(s,&ah,&bh,&al,&bl);
    md2gba_hwq_reset_stage();
    if(plan&MD2GBA_FRAME_PALETTE)md2gba_hwq_stage_palette(v);
    if(plan&MD2GBA_FRAME_MAP_PATCH){
        int ra=md2gba_direct_patch_map(v,a,ah,al,w,h,s->map_profile),rb=0;
        if(ra==0)rb=md2gba_direct_patch_map(v,b,bh,bl,w,h,s->map_profile);
        if(ra==-3||rb==-3){
            /* A profiled scene family changed. Discard the unpublished small
               payload, select another desktop-generated map from the complete
               live plane, and rebuild atomically into the inactive bank. */
            md2gba_hwq_reset_stage();
            plan|=MD2GBA_FRAME_MAPS|MD2GBA_FRAME_TILES|MD2GBA_FRAME_SPRITES;
            goto full_rebuild;
        }
        if(ra||rb)return -29;
    }
    if(plan&MD2GBA_FRAME_TILES){
        const uint16_t*map=md2gba_compiled_vdp_source_to_slot[s->map_profile];
        unsigned dirty=md2gba_direct_dirty_tiles(v,s->map_profile);
        if(dirty>MD2GBA_HWQ_MAX_TILES-(unsigned)md2gba_hwq.tile_count){
            md2gba_direct_stage_mapping(v,s->map_profile);md2gba_hwq.tile_count=0u;
            md2gba_hwq.full_tiles_valid=1u;
            md2gba_hwq.full_tile_halfwords=(uint16_t)((md2gba_compiled_vdp_slot_count[s->map_profile]+1u)*16u);
        }else{
            unsigned wi;
            for(wi=0u;wi<64u;wi++){
                uint32_t bits=v->vram_dirty_tiles[wi];
                while(bits){unsigned src=wi*32u+md2gba_lowbit_index(bits),slot;bits&=bits-1u;slot=map[src];if(!slot)continue;md2gba_direct_stage_tile(v,src,slot);md2gba_hwq.tile_slot[md2gba_hwq.tile_count++]=(uint16_t)slot;}
            }
        }
    }
    if(plan&MD2GBA_FRAME_SPRITES){if(md2gba_hwq_stage_sprites(v,ox,oy))return -30;s->sprite_source_count=(uint16_t)md2gba_vdp_collect_sprite_tiles(v,s->sprite_source_tiles,128u);}
    md2gba_direct_finish_state(v,s,ox,oy);
    if(md2gba_hwq.palette_valid||md2gba_hwq.tile_count||md2gba_hwq.map_write_count||md2gba_hwq.sprite_valid||md2gba_hwq.full_tiles_valid)md2gba_hwq_publish();
    return 0;
}

