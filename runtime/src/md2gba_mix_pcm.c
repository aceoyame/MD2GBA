#include "md2gba_mix_pcm.h"

#if defined(MD2GBA_GBA_IWRAM)
#define MD2GBA_MIX_HOT __attribute__((section(".iwram")))
#else
#define MD2GBA_MIX_HOT
#endif
#if defined(MD2GBA_GBA_AUDIO_HOT)
#define MD2GBA_MIX_SELECTED __attribute__((section(".iwram")))
#else
#define MD2GBA_MIX_SELECTED
#endif

static void ym_write_cb(void *u,uint8_t p,uint8_t r,uint8_t v){md2gba_ym2612_pcm_write(&((Md2GbaMixPcm*)u)->ym,p,r,v);}
static uint8_t ym_status_cb(void *u){return md2gba_ym2612_pcm_status(&((Md2GbaMixPcm*)u)->ym);}
static void psg_write_cb(void *u,uint8_t v){
    Md2GbaMixPcm *m=(Md2GbaMixPcm*)u;
    md2gba_psg_pcm_write(&m->psg,v);
#if defined(MD2GBA_GBA_LEGACY_PSG)
    md2gba_gba_legacy_psg_sync_write(&m->legacy_psg,&m->psg.psg,m->psg.input_clock_hz,v);
#endif
}
void md2gba_mix_pcm_init(Md2GbaMixPcm *m,uint32_t psg_clock_hz,uint32_t ym_clock_hz,uint32_t sample_rate_hz){
    md2gba_psg_pcm_init(&m->psg,psg_clock_hz,sample_rate_hz);
    md2gba_ym2612_pcm_init(&m->ym,ym_clock_hz,sample_rate_hz);
#if defined(MD2GBA_GBA_LEGACY_PSG)
    md2gba_gba_legacy_psg_init(&m->legacy_psg);
#endif
}
MD2GBA_MIX_SELECTED void md2gba_mix_pcm_render(Md2GbaMixPcm *m,int8_t *dst,size_t samples){
#if defined(MD2GBA_GBA_LEGACY_PSG)
#if defined(MD2GBA_GBA_LEGACY_PSG_NOISE4)
    /* P83 experimental maximum assist: the entire SN76489 is on legacy APU
       hardware.  Noise is the documented approximate path, so Direct Sound
       can render YM directly without even clearing/mixing a PSG buffer. */
    md2gba_ym2612_pcm_render(&m->ym,dst,samples);
#if defined(MD2GBA_YM_OUTPUT_GAIN_Q8) && MD2GBA_YM_OUTPUT_GAIN_Q8 != 256
    /* P89 diagnostic/correctness gain: the current live YM model's real-game
       output peaks far below signed-8-bit headroom.  Keep the gain generic and
       compile-time selectable while the PSC-style wavetable lowering replaces
       this approximate live path. */
    for(size_t i=0;i<samples;i++){
        int v=((int)dst[i]*(int)MD2GBA_YM_OUTPUT_GAIN_Q8)>>8;
        if(v>127)v=127;else if(v<-128)v=-128;dst[i]=(int8_t)v;
    }
#endif
    return;
#elif defined(MD2GBA_GBA_LEGACY_PSG_TONE3)
    /* P83 maximum exact assist: all SN tones are physical; PCM retains Sega noise. */
    md2gba_psg_pcm_render_noise_only(&m->psg,dst,samples);
#else
    /* P83 conservative assist: tone 0/1 physical, tone 2 + Sega noise PCM. */
    md2gba_psg_pcm_render_tone2_noise(&m->psg,dst,samples);
#endif
#else
    md2gba_psg_pcm_render(&m->psg,dst,samples);
#endif
    md2gba_ym2612_pcm_mix_into(&m->ym,dst,samples);
}
Md2GbaAudioSink md2gba_mix_pcm_sink(Md2GbaMixPcm *m){
    Md2GbaAudioSink s;s.user=m;s.ym2612_write=ym_write_cb;s.ym2612_status=ym_status_cb;s.psg_write=psg_write_cb;s.z80_ram_write=0;return s;
}
#if defined(MD2GBA_GBA_LEGACY_PSG)
void md2gba_mix_pcm_gba_legacy_start(Md2GbaMixPcm *m){
    if(m) md2gba_gba_legacy_psg_start(&m->legacy_psg,&m->psg.psg,m->psg.input_clock_hz);
}
void md2gba_mix_pcm_gba_legacy_stop(Md2GbaMixPcm *m){
    if(m) md2gba_gba_legacy_psg_stop(&m->legacy_psg);
}
#endif

#undef MD2GBA_MIX_HOT
#undef MD2GBA_MIX_SELECTED
