#include "md2gba_psc_m4a.h"
#include "md2gba_compiler.h"

#define REG8(a) (*(volatile uint8_t*)(a))
#define REG16(a) (*(volatile uint16_t*)(a))
#define REG32(a) (*(volatile uint32_t*)(a))
#define REG_VCOUNT REG16(0x04000006u)
#define SOUNDCNT_H REG16(0x04000082u)
#define SOUNDCNT_X REG16(0x04000084u)
#define SOUNDBIAS_H REG8(0x04000089u)
#define FIFO_A REG32(0x040000A0u)
#define DMA1SAD REG32(0x040000BCu)
#define DMA1DAD REG32(0x040000C0u)
#define DMA1CNT_L REG16(0x040000C4u)
#define DMA1CNT_H REG16(0x040000C6u)
#define TM0D REG16(0x04000100u)
#define TM0CNT REG16(0x04000102u)
#define REG_IME REG16(0x04000208u)

#if defined(MD2GBA_GBA_IWRAM)
#define MAXMOD_HOT __attribute__((section(".iwram"),noinline))
#else
#define MAXMOD_HOT
#endif

static Md2GbaPscM4a *g_maxmod;
/* P134S: missed foreground mixes must never replay a loud 16.7 ms slice.
   A dedicated silent frame makes starvation audible as a quiet gap rather than
   the painful buzz/hang produced by repeatedly rewinding the old segment. */
static int8_t g_audio_silence[MD2GBA_PSC_M4A_SAMPLES_PER_VBLANK] __attribute__((aligned(4)));

static void zero_buffer(int8_t *p) {
    unsigned i;
    if(!p) return;
    for(i=0;i<MD2GBA_PSC_M4A_PCM_BUFFER_SIZE;i++)p[i]=0;
}

/* Maxmod GBA style: sound DMA is restarted at every VBlank.  With Timer0
   clocked at exactly 224 samples per physical frame this keeps the source
   pointer bounded to one frame segment instead of letting a repeating FIFO
   DMA walk through a seven-frame MP2K ring. */
static MAXMOD_HOT void restart_dma_from(Md2GbaPscM4a *s,const int8_t *src) {
    if(!s || !src)return;
    DMA1CNT_H=0u;
    DMA1SAD=(uint32_t)(uintptr_t)src;
    DMA1DAD=(uint32_t)(uintptr_t)&FIFO_A;
    DMA1CNT_L=4u;
    DMA1CNT_H=0xB600u;
    s->dma_rewinds++;
}
static MAXMOD_HOT void restart_dma(Md2GbaPscM4a *s) {
    unsigned seg;if(!s || !s->pcm_buffer)return;seg=(unsigned)(s->pcm_dma_counter&1u);
    restart_dma_from(s,s->pcm_buffer+seg*MD2GBA_PSC_M4A_SAMPLES_PER_VBLANK);
}

unsigned md2gba_psc_m4a_mix_slot(uint8_t counter) {
    return ((unsigned)counter^1u)&1u;
}

void md2gba_psc_m4a_init(Md2GbaPscM4a *s, int8_t *pcm_buffer) {
    if(!s)return;
    s->ident=MD2GBA_PSC_M4A_ID_NUMBER;
    s->pcm_dma_counter=0u; /* current playback segment */
    s->reverb=0u;
    s->max_chans=8u;
    s->master_volume=15u;
    s->freq=MD2GBA_PSC_M4A_FREQ_MODE;
    s->mode=0u;s->c15=0u;s->pcm_dma_period=1u;s->max_lines=0u;
    s->gap[0]=s->gap[1]=s->gap[2]=0u;
    s->pcm_samples_per_vblank=(int32_t)MD2GBA_PSC_M4A_SAMPLES_PER_VBLANK;
    s->pcm_freq=(int32_t)MD2GBA_PSC_M4A_PCM_FREQ;
    s->div_freq=(int32_t)MD2GBA_PSC_M4A_DIV_FREQ;
    s->pcm_buffer=pcm_buffer;
    s->started=0u;
    s->pending_frame=0u;
    s->last_mix_slot=1u; /* segment most recently rendered */
    s->reserved=0u;      /* completed segment waiting for VBlank handoff */
    s->timer_reload=(uint16_t)(0u-(280896u/MD2GBA_PSC_M4A_SAMPLES_PER_VBLANK));
    s->vsyncs=s->mixes=s->dropped_frames=s->dma_rewinds=0u;
    zero_buffer(pcm_buffer);
}

void md2gba_psc_m4a_start(Md2GbaPscM4a *s) {
    uint16_t old;
    if(!s || !s->pcm_buffer)return;
    old=REG_IME;REG_IME=0u;
    g_maxmod=s;
    s->ident=MD2GBA_PSC_M4A_ID_NUMBER;
    s->pcm_dma_counter=0u;
    s->last_mix_slot=1u;
    s->reserved=0u;
    s->pending_frame=0u;
    SOUNDCNT_X=0x008Fu;
    /* P134T: restore the proven P104 mono DirectSound-A full-ratio route.
       Underruns still feed silence instead of repeating stale audio. */
    SOUNDCNT_H=0x0B0Eu;
    SOUNDBIAS_H=(uint8_t)((SOUNDBIAS_H&0x3Fu)|0x40u);
    TM0CNT=0u;
    TM0D=s->timer_reload;
    DMA1CNT_H=0u;
    REG_IME=old;

    /* Unlike the prior MP2K path, phase-lock to the physical VBlank boundary
       because Maxmod restarts sound DMA from VBlank every frame. */
    while((uint8_t)REG_VCOUNT>=160u) { }
    while((uint8_t)REG_VCOUNT<160u) { }

    old=REG_IME;REG_IME=0u;
    restart_dma(s);
    TM0CNT=0x0080u;
    s->started=1u;
    s->pending_frame=1u; /* prepare the opposite segment for next VBlank */
    REG_IME=old;
}

void md2gba_psc_m4a_stop(Md2GbaPscM4a *s) {
    uint16_t old=REG_IME;REG_IME=0u;
    TM0CNT=0u;DMA1CNT_H=0u;
    if(s){s->started=0u;s->pending_frame=0u;s->reserved=0u;s->ident=MD2GBA_PSC_M4A_ID_NUMBER;}
    if(g_maxmod==s)g_maxmod=0;
    REG_IME=old;
}

MAXMOD_HOT void md2gba_psc_m4a_vsync(void) {
    Md2GbaPscM4a *s=g_maxmod;
    if(!s || !s->started)return;
    s->vsyncs++;

    /* Publish a fully rendered frame only at VBlank. If foreground mixing did
       not finish, play silence for this physical frame. Repeating the previous
       frame creates a harsh tonal buzz exactly when video work overruns. */
    if(s->reserved){
        MD2GBA_COMPILER_BARRIER();
        s->pcm_dma_counter=(uint8_t)(s->last_mix_slot&1u);
        s->reserved=0u;
        restart_dma(s);
    }else{
        s->dropped_frames++;
        restart_dma_from(s,g_audio_silence);
    }
    if(s->pending_frame)s->dropped_frames++;
    s->pending_frame=1u;
}

unsigned md2gba_psc_m4a_service(Md2GbaPscM4a *s, Md2GbaPscM4aFillFn fill, void *user) {
    uint16_t old;
    unsigned slot;
    if(!s || !s->started || !fill || !s->pcm_buffer)return 0u;
    old=REG_IME;REG_IME=0u;
    /* A completed buffer waiting for VBlank must not be overwritten. */
    if(!s->pending_frame || s->reserved || s->ident!=MD2GBA_PSC_M4A_ID_NUMBER){REG_IME=old;return 0u;}
    s->pending_frame=0u;
    slot=((unsigned)s->pcm_dma_counter^1u)&1u;
    s->ident=MD2GBA_PSC_M4A_ID_NUMBER+1u;
    REG_IME=old;

    fill(user,s->pcm_buffer+slot*MD2GBA_PSC_M4A_SAMPLES_PER_VBLANK,
         MD2GBA_PSC_M4A_SAMPLES_PER_VBLANK);

    old=REG_IME;REG_IME=0u;
    s->last_mix_slot=(uint8_t)slot;
    /* Publish a completed slot only after fill() and metadata writes. */
    MD2GBA_COMPILER_BARRIER();
    s->reserved=1u;
    s->ident=MD2GBA_PSC_M4A_ID_NUMBER;
    s->mixes++;
    REG_IME=old;
    return 1u;
}

Md2GbaPscM4a *md2gba_psc_m4a_active(void){return g_maxmod;}
#undef MAXMOD_HOT
