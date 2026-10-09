#include "md2gba_psc_m4a.h"

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
#define MD2GBA_PSC_M4A_HOT __attribute__((section(".iwram"),noinline))
#else
#define MD2GBA_PSC_M4A_HOT
#endif

static Md2GbaPscM4a *g_psc_m4a;

static void zero_buffer(int8_t *p) {
    unsigned i;
    if(!p) return;
    for(i=0u;i<MD2GBA_PSC_M4A_PCM_BUFFER_SIZE;i++) p[i]=0;
}

/* PSC/MusicPlayer2000 SoundMain does not keep a separate producer cursor.
   The current DMA phase chooses the only safe 224-sample region to mix:
     counter 7 -> slot 1, 6 -> 2, ... 2 -> 6, 1/0 -> slot 0.
   Slot 0 is therefore refreshed immediately before the seven-VBlank rewind,
   never while DMA has just restarted from the start of the buffer. */
unsigned md2gba_psc_m4a_mix_slot(uint8_t counter) {
    unsigned period=MD2GBA_PSC_M4A_DMA_PERIOD;
    if(counter<=1u) return 0u;
    if(counter>period) counter=(uint8_t)period;
    return period-(unsigned)counter+1u;
}

static void dma_rewind(Md2GbaPscM4a *s) {
    if(!s || !s->pcm_buffer) return;
    /* MusicPlayer2000 m4aSoundVSync pattern used by PSC: retire an active
       repeating sound DMA with a four-word immediate destination-fixed transfer,
       leave DMA1 in 32-bit mode while disabled, then restart FIFO-A repeat/special timing. */
    if(DMA1CNT_H & 0x0200u) {
        DMA1CNT_L=4u;
        DMA1CNT_H=0x8440u;
    }
    DMA1CNT_H=0x0400u;
    DMA1SAD=(uint32_t)(uintptr_t)s->pcm_buffer;
    DMA1DAD=(uint32_t)(uintptr_t)&FIFO_A;
    DMA1CNT_L=4u;
    DMA1CNT_H=0xB600u;
    s->dma_rewinds++;
}

/* Clean-room equivalent of PSC's m4aSoundVSyncOff behavior. */
static void sound_vsync_off(Md2GbaPscM4a *s) {
    uint32_t delta;
    if(!s || !s->pcm_buffer) return;
    delta=s->ident-MD2GBA_PSC_M4A_ID_NUMBER;
    if(delta>1u) return;
    s->ident+=10u;
    if(DMA1CNT_H & 0x0200u) {
        DMA1CNT_L=4u;
        DMA1CNT_H=0x8440u;
    }
    DMA1CNT_H=0x0400u;
    zero_buffer(s->pcm_buffer);
}

/* Clean-room equivalent of PSC's m4aSoundVSyncOn behavior. */
static void sound_vsync_on(Md2GbaPscM4a *s) {
    if(!s || !s->pcm_buffer) return;
    if(s->ident==MD2GBA_PSC_M4A_ID_NUMBER) return;
    DMA1SAD=(uint32_t)(uintptr_t)s->pcm_buffer;
    DMA1DAD=(uint32_t)(uintptr_t)&FIFO_A;
    DMA1CNT_L=4u;
    DMA1CNT_H=0xB600u;
    s->pcm_dma_counter=0u;
    s->ident-=10u;
}

void md2gba_psc_m4a_init(Md2GbaPscM4a *s, int8_t *pcm_buffer) {
    if(!s) return;
    s->ident=MD2GBA_PSC_M4A_ID_NUMBER;
    s->pcm_dma_counter=0u;
    s->reverb=0u;
    s->max_chans=8u;        /* PSC SoundInit writes 8. */
    s->master_volume=15u;   /* PSC SoundInit writes 15. */
    s->freq=MD2GBA_PSC_M4A_FREQ_MODE;
    s->mode=0u;
    s->c15=0u;
    s->pcm_dma_period=MD2GBA_PSC_M4A_DMA_PERIOD;
    s->max_lines=0u;
    s->gap[0]=s->gap[1]=s->gap[2]=0u;
    s->pcm_samples_per_vblank=(int32_t)MD2GBA_PSC_M4A_SAMPLES_PER_VBLANK;
    s->pcm_freq=(int32_t)MD2GBA_PSC_M4A_PCM_FREQ;
    s->div_freq=(int32_t)MD2GBA_PSC_M4A_DIV_FREQ;
    s->pcm_buffer=pcm_buffer;
    s->started=0u;
    s->pending_frame=0u;
    s->last_mix_slot=0u;
    s->reserved=0u;
    s->timer_reload=(uint16_t)(0u-(280896u/MD2GBA_PSC_M4A_SAMPLES_PER_VBLANK));
    s->vsyncs=0u; s->mixes=0u; s->dropped_frames=0u; s->dma_rewinds=0u;
    zero_buffer(pcm_buffer);
}

void md2gba_psc_m4a_start(Md2GbaPscM4a *s) {
    if(!s || !s->pcm_buffer) return;
    REG_IME=0u;
    g_psc_m4a=s;
    /* Start from the same locked/cleared state as SoundVSyncOff, then use the
       matching VSyncOn transition to start the repeating FIFO DMA. */
    s->ident=MD2GBA_PSC_M4A_ID_NUMBER;
    sound_vsync_off(s);
    SOUNDCNT_X=0x008Fu;
    SOUNDCNT_H=0x0B0Eu;
    SOUNDBIAS_H=(uint8_t)((SOUNDBIAS_H&0x3Fu)|0x40u);
    TM0CNT=0u;
    TM0D=s->timer_reload;
    sound_vsync_on(s);
    s->pending_frame=0u;
    s->last_mix_slot=0u;
    /* Match SampleFreqSet's scanline-159 phase lock without a second refill
       timer/IRQ. */
    REG_IME=1u;
    while((uint8_t)REG_VCOUNT==159u) { }
    while((uint8_t)REG_VCOUNT!=159u) { }
    TM0CNT=0x0080u;
    s->started=1u;
}

void md2gba_psc_m4a_stop(Md2GbaPscM4a *s) {
    uint16_t old=REG_IME;
    REG_IME=0u;
    TM0CNT=0u;
    if(s) {
        sound_vsync_off(s);
        s->started=0u;
        s->pending_frame=0u;
    } else DMA1CNT_H=0x0400u;
    if(g_psc_m4a==s) g_psc_m4a=0;
    REG_IME=old;
}

MD2GBA_PSC_M4A_HOT void md2gba_psc_m4a_vsync(void) {
    Md2GbaPscM4a *s=g_psc_m4a;
    uint8_t c;
    uint32_t delta;
    if(!s || !s->started) return;
    delta=s->ident-MD2GBA_PSC_M4A_ID_NUMBER;
    if(delta>1u) return;
    s->vsyncs++;

    /* MusicPlayer2000 does not build an audio-debt queue. SoundMain gets one
       opportunity per physical frame. If the prior frame is still pending,
       discard that stale opportunity rather than mixing multiple sequence
       frames at once and racing the DMA cursor. */
    if(s->pending_frame) s->dropped_frames++;
    s->pending_frame=1u;

    c=s->pcm_dma_counter;
    c=(uint8_t)(c-1u);
    s->pcm_dma_counter=c;
    if((int8_t)c<=0) {
        s->pcm_dma_counter=s->pcm_dma_period;
        dma_rewind(s);
    }
}

unsigned md2gba_psc_m4a_service(Md2GbaPscM4a *s, Md2GbaPscM4aFillFn fill, void *user) {
    uint16_t old;
    unsigned slot;
    if(!s || !s->started || !fill || !s->pcm_buffer) return 0u;
    old=REG_IME; REG_IME=0u;
    if(!s->pending_frame || s->ident!=MD2GBA_PSC_M4A_ID_NUMBER) {
        REG_IME=old; return 0u;
    }
    s->pending_frame=0u;
    slot=md2gba_psc_m4a_mix_slot(s->pcm_dma_counter);
    s->last_mix_slot=(uint8_t)slot;
    s->ident=MD2GBA_PSC_M4A_ID_NUMBER+1u;
    REG_IME=old;

    fill(user,s->pcm_buffer+slot*MD2GBA_PSC_M4A_SAMPLES_PER_VBLANK,
         MD2GBA_PSC_M4A_SAMPLES_PER_VBLANK);

    old=REG_IME; REG_IME=0u;
    s->ident=MD2GBA_PSC_M4A_ID_NUMBER;
    s->mixes++;
    REG_IME=old;
    return 1u;
}

Md2GbaPscM4a *md2gba_psc_m4a_active(void) { return g_psc_m4a; }

#undef MD2GBA_PSC_M4A_HOT
