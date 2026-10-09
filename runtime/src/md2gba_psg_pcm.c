#include "md2gba_psg_pcm.h"

/* 2 dB voltage ladder from the standard 16-bit table, scaled so four channels
   can sum into signed 8-bit Direct Sound without a run-time divide. */
static const uint8_t volume8[16]={31,25,20,16,13,10,8,6,5,4,3,2,2,1,1,0};

static uint16_t tone_reload(const Md2GbaPsgPcm *p, unsigned ch) {
    uint16_t n=(uint16_t)(p->psg.tone_period[ch]&0x03FFu);
    return n<2u ? 1u : n;
}
static uint16_t noise_reload(const Md2GbaPsgPcm *p) {
    switch(p->psg.noise_control&3u) {
        case 0u:return 0x10u;
        case 1u:return 0x20u;
        case 2u:return 0x40u;
        default:return tone_reload(p,2u);
    }
}
static void noise_reset(Md2GbaPsgPcm *p) {
    /* Sega integrated PSG: 16-bit register, highest bit seeded on noise write. */
    p->noise_lfsr=0x8000u;
    p->noise_counter=noise_reload(p);
    p->noise_flip=0u;
}
static uint32_t udivmod32(uint32_t n, uint32_t d, uint32_t *rem) {
    uint32_t q=0u,r=0u;
    int bit;
    if(!d) { if(rem) *rem=0u; return 0u; }
    for(bit=31;bit>=0;bit--) {
        r=(r<<1)|((n>>(unsigned)bit)&1u);
        if(r>=d) { r-=d; q|=(1u<<(unsigned)bit); }
    }
    if(rem) *rem=r;
    return q;
}

#if defined(MD2GBA_GBA_IWRAM)
#define MD2GBA_PCM_HOT __attribute__((section(".iwram")))
#else
#define MD2GBA_PCM_HOT
#endif
#if defined(MD2GBA_GBA_AUDIO_HOT)
#if defined(MD2GBA_GBA_LEGACY_PSG)
/* P83 keeps only the selected residual renderer in zero-wait RAM. */
#define MD2GBA_PCM_SELECTED
#if defined(MD2GBA_GBA_LEGACY_PSG_TONE3)
#define MD2GBA_PCM_RESIDUAL2
#define MD2GBA_PCM_RESIDUAL1 __attribute__((section(".iwram.psg_noise")))
#else
#define MD2GBA_PCM_RESIDUAL2 __attribute__((section(".iwram.psg_tone2noise")))
#define MD2GBA_PCM_RESIDUAL1
#endif
#else
#define MD2GBA_PCM_SELECTED __attribute__((section(".iwram")))
#define MD2GBA_PCM_RESIDUAL2 __attribute__((section(".iwram")))
#define MD2GBA_PCM_RESIDUAL1 __attribute__((section(".iwram")))
#endif
#else
#define MD2GBA_PCM_SELECTED
#define MD2GBA_PCM_RESIDUAL2
#define MD2GBA_PCM_RESIDUAL1
#endif

static __attribute__((always_inline)) inline uint16_t noise_lfsr_step(uint16_t lfsr, uint8_t control) {
    uint16_t fb;
    if(control&4u) fb=(uint16_t)(((lfsr>>0)^(lfsr>>3))&1u);
    else fb=(uint16_t)(lfsr&1u);
    return (uint16_t)((lfsr>>1)|(fb<<15));
}

void md2gba_psg_pcm_init(Md2GbaPsgPcm *p, uint32_t input_clock_hz, uint32_t sample_rate_hz) {
    unsigned i;
    md2gba_psg_init(&p->psg);
    p->input_clock_hz=input_clock_hz?input_clock_hz:MD2GBA_PSG_NTSC_CLOCK;
    p->sample_rate_hz=sample_rate_hz?sample_rate_hz:MD2GBA_PSG_PCM_DEFAULT_RATE;
    p->tick_threshold=p->sample_rate_hz<<4;
    if(!p->tick_threshold) p->tick_threshold=MD2GBA_PSG_PCM_DEFAULT_RATE<<4;
    p->tick_step_base=udivmod32(p->input_clock_hz,p->tick_threshold,&p->tick_step_remainder);
    p->clock_accum=0u; p->rendered_samples=0u; p->internal_ticks=0u;
    for(i=0;i<3u;i++){p->tone_counter[i]=1u;p->tone_output[i]=1u;}
    noise_reset(p);
}
void md2gba_psg_pcm_write(void *user, uint8_t value) {
    Md2GbaPsgPcm *p=(Md2GbaPsgPcm*)user;
    unsigned noise_write=0u;
    if(value&0x80u) noise_write=((((value>>5)&3u)==3u)&&(((value>>4)&1u)==0u));
    else noise_write=(p->psg.latched_channel==3u && p->psg.latched_volume==0u);
    md2gba_psg_write(&p->psg,value);
    if(noise_write) noise_reset(p); /* even rewriting the same value resets LFSR */
}

/* Phase 24: render the exact same digital PSG state without stepping the
   /16 chip clock one tick at a time.  At 32.768 kHz a Genesis PSG advances
   about 6.8 internal ticks for every output sample; the old implementation
   revisited all four generators on every one of those ticks.  Here we split
   the input/sample-clock ratio into a base tick count plus an accumulator
   carry, then advance each tone counter arithmetically over that tick span.
   Noise still clocks its LFSR at each required generator edge, preserving the
   bit-exact Phase 16 PCM signature while removing the dominant ARM7 cost. */
MD2GBA_PCM_SELECTED void md2gba_psg_pcm_render(Md2GbaPsgPcm *p, int8_t *dst, size_t samples) {
    uint32_t threshold=p->tick_threshold;
    uint32_t step_rem=p->tick_step_remainder,step_base=p->tick_step_base,accum,internal;
    uint16_t n0,n1,n2,c0,c1,c2,nc,nn,lfsr;
    uint8_t o0,o1,o2,nflip,nctl;
    int a0,a1,a2,an;
    size_t i;
    if(!threshold) {
        threshold=p->sample_rate_hz<<4;
        if(!threshold) threshold=MD2GBA_PSG_PCM_DEFAULT_RATE<<4;
        step_base=udivmod32(p->input_clock_hz,threshold,&step_rem);
    }
    accum=p->clock_accum;
    internal=p->internal_ticks;

    n0=(uint16_t)(p->psg.tone_period[0]&0x03FFu);
    n1=(uint16_t)(p->psg.tone_period[1]&0x03FFu);
    n2=(uint16_t)(p->psg.tone_period[2]&0x03FFu);
    c0=p->tone_counter[0]; c1=p->tone_counter[1]; c2=p->tone_counter[2];
    o0=p->tone_output[0]; o1=p->tone_output[1]; o2=p->tone_output[2];
    nctl=p->psg.noise_control;
    nc=p->noise_counter; lfsr=p->noise_lfsr; nflip=p->noise_flip;
    switch(nctl&3u) {
        case 0u: nn=0x10u; break;
        case 1u: nn=0x20u; break;
        case 2u: nn=0x40u; break;
        default: nn=n2<2u?1u:n2; break;
    }
    a0=(int)volume8[p->psg.attenuation[0]&15u];
    a1=(int)volume8[p->psg.attenuation[1]&15u];
    a2=(int)volume8[p->psg.attenuation[2]&15u];
    an=(int)volume8[p->psg.attenuation[3]&15u];

#define ADV_TONE(period,counter,output,ticks_) do { \
        unsigned _t=(ticks_); \
        if(_t) { \
            uint16_t _n=(period); \
            if(_n<=1u) { (output)=1u; (counter)=1u; } \
            else { \
                uint16_t _c=(counter); unsigned _tog=0u; \
                if(!_c) _c=1u; \
                if(_t<_c) _c=(uint16_t)(_c-_t); \
                else { \
                    _t-=_c; _tog=1u; \
                    while(_t>=(unsigned)_n) { _t-=(unsigned)_n; _tog++; } \
                    _c=(uint16_t)((unsigned)_n-_t); \
                    if(_tog&1u) (output)^=1u; \
                } \
                (counter)=_c; \
            } \
        } \
    } while(0)

    for(i=0;i<samples;i++) {
        unsigned ticks=step_base;
        unsigned nt;
        int mix;
        accum+=step_rem;
        if(accum>=threshold) { accum-=threshold; ticks++; }
        if(ticks) {
            /* Phase 37: common exact no-edge span.  At 32.768 kHz the NTSC
               PSG advances only 6-7 internal ticks per output sample.  For
               ordinary musical periods most samples do not reach any tone
               or noise edge.  In that case all four counters simply subtract
               the same tick span; skip the three generic ADV_TONE decision
               trees and noise edge loop.  Equality is deliberately excluded:
               the original path toggles/clocks when ticks == counter. */
            if(ticks<(unsigned)c0 && ticks<(unsigned)c1 && ticks<(unsigned)c2 &&
               ticks<(unsigned)nc) {
                c0=(uint16_t)((unsigned)c0-ticks);
                c1=(uint16_t)((unsigned)c1-ticks);
                c2=(uint16_t)((unsigned)c2-ticks);
                nc=(uint16_t)((unsigned)nc-ticks);
            } else {
                ADV_TONE(n0,c0,o0,ticks);
                ADV_TONE(n1,c1,o1,ticks);
                ADV_TONE(n2,c2,o2,ticks);
                nt=ticks;
                if(!nc) nc=1u;
                while(nt>=(unsigned)nc) {
                    nt-=(unsigned)nc;
                    nc=nn;
                    nflip^=1u;
                    if(nflip) lfsr=noise_lfsr_step(lfsr,nctl);
                }
                nc=(uint16_t)((unsigned)nc-nt);
            }
            internal+=ticks;
        }
        /* volume8 tops out at 31, so four channels cannot exceed +/-124;
           signed 8-bit saturation is therefore unnecessary. */
        mix=(o0?a0:-a0)+(o1?a1:-a1)+(o2?a2:-a2)+((lfsr&1u)?an:-an);
        dst[i]=(int8_t)mix;
    }
#undef ADV_TONE

    p->clock_accum=accum;
    p->internal_ticks=internal;
    p->tone_counter[0]=c0; p->tone_counter[1]=c1; p->tone_counter[2]=c2;
    p->tone_output[0]=o0; p->tone_output[1]=o1; p->tone_output[2]=o2;
    p->noise_counter=nc; p->noise_lfsr=lfsr; p->noise_flip=nflip;
    p->rendered_samples+=(uint32_t)samples;
}

/* P83 hardware-assist residual.  Legacy GBA square channels synthesize SN
   tones 0 and 1, so the Direct Sound stream needs only tone 2 and the exact
   Sega 16-bit noise generator.  This deliberately keeps tone2/noise coupled:
   SN noise mode 3 derives its rate from tone2. */
MD2GBA_PCM_RESIDUAL2 void md2gba_psg_pcm_render_tone2_noise(Md2GbaPsgPcm *p, int8_t *dst, size_t samples) {
    uint32_t threshold=p->tick_threshold;
    uint32_t step_rem=p->tick_step_remainder,step_base=p->tick_step_base,accum,internal;
    uint16_t n2,c2,nc,nn,lfsr;
    uint8_t o2,nflip,nctl;
    int a2,an;
    size_t i;
    if(!threshold) {
        threshold=p->sample_rate_hz<<4;
        if(!threshold) threshold=MD2GBA_PSG_PCM_DEFAULT_RATE<<4;
        step_base=udivmod32(p->input_clock_hz,threshold,&step_rem);
    }
    accum=p->clock_accum; internal=p->internal_ticks;
    n2=(uint16_t)(p->psg.tone_period[2]&0x03FFu);
    c2=p->tone_counter[2]; o2=p->tone_output[2];
    nctl=p->psg.noise_control;
    nc=p->noise_counter; lfsr=p->noise_lfsr; nflip=p->noise_flip;
    switch(nctl&3u) {
        case 0u: nn=0x10u; break;
        case 1u: nn=0x20u; break;
        case 2u: nn=0x40u; break;
        default: nn=n2<2u?1u:n2; break;
    }
    a2=(int)volume8[p->psg.attenuation[2]&15u];
    an=(int)volume8[p->psg.attenuation[3]&15u];
    for(i=0;i<samples;i++) {
        unsigned ticks=step_base,nt;
        int mix;
        accum+=step_rem;
        if(accum>=threshold) { accum-=threshold; ticks++; }
        if(ticks) {
            if(ticks<(unsigned)c2 && ticks<(unsigned)nc) {
                c2=(uint16_t)((unsigned)c2-ticks);
                nc=(uint16_t)((unsigned)nc-ticks);
            } else {
                unsigned t=ticks;
                uint16_t n=n2;
                if(n<=1u) { o2=1u; c2=1u; }
                else {
                    uint16_t c=c2; unsigned tog=0u;
                    if(!c) c=1u;
                    if(t<c) c=(uint16_t)(c-t);
                    else {
                        t-=c; tog=1u;
                        while(t>=(unsigned)n) { t-=(unsigned)n; tog++; }
                        c=(uint16_t)((unsigned)n-t);
                        if(tog&1u) o2^=1u;
                    }
                    c2=c;
                }
                nt=ticks;
                if(!nc) nc=1u;
                while(nt>=(unsigned)nc) {
                    nt-=(unsigned)nc; nc=nn; nflip^=1u;
                    if(nflip) lfsr=noise_lfsr_step(lfsr,nctl);
                }
                nc=(uint16_t)((unsigned)nc-nt);
            }
            internal+=ticks;
        }
        mix=(o2?a2:-a2)+((lfsr&1u)?an:-an);
        dst[i]=(int8_t)mix;
    }
    p->clock_accum=accum; p->internal_ticks=internal;
    p->tone_counter[2]=c2; p->tone_output[2]=o2;
    p->noise_counter=nc; p->noise_lfsr=lfsr; p->noise_flip=nflip;
    p->rendered_samples+=(uint32_t)samples;
}


/* P83 aggressive legacy assist residual: all three SN square tones are handled
   by GBA legacy tone/wave hardware; retain the Sega 16-bit LFSR exactly in
   Direct Sound.  Noise mode 3 still reads the source tone-2 divider. */
MD2GBA_PCM_RESIDUAL1 void md2gba_psg_pcm_render_noise_only(Md2GbaPsgPcm *p, int8_t *dst, size_t samples) {
    uint32_t threshold=p->tick_threshold;
    uint32_t step_rem=p->tick_step_remainder,step_base=p->tick_step_base,accum,internal;
    uint16_t nc,nn,lfsr,n2;
    uint8_t nflip,nctl;
    int an;
    size_t i;
    if(!threshold) {
        threshold=p->sample_rate_hz<<4;
        if(!threshold) threshold=MD2GBA_PSG_PCM_DEFAULT_RATE<<4;
        step_base=udivmod32(p->input_clock_hz,threshold,&step_rem);
    }
    accum=p->clock_accum; internal=p->internal_ticks;
    n2=(uint16_t)(p->psg.tone_period[2]&0x03FFu);
    nctl=p->psg.noise_control; nc=p->noise_counter; lfsr=p->noise_lfsr; nflip=p->noise_flip;
    switch(nctl&3u) {
        case 0u: nn=0x10u; break;
        case 1u: nn=0x20u; break;
        case 2u: nn=0x40u; break;
        default: nn=n2<2u?1u:n2; break;
    }
    an=(int)volume8[p->psg.attenuation[3]&15u];
    for(i=0;i<samples;i++) {
        unsigned ticks=step_base;
        accum+=step_rem;
        if(accum>=threshold) { accum-=threshold; ticks++; }
        if(ticks) {
            if(ticks<(unsigned)nc) nc=(uint16_t)((unsigned)nc-ticks);
            else {
                unsigned nt=ticks;
                if(!nc) nc=1u;
                while(nt>=(unsigned)nc) {
                    nt-=(unsigned)nc; nc=nn; nflip^=1u;
                    if(nflip) lfsr=noise_lfsr_step(lfsr,nctl);
                }
                nc=(uint16_t)((unsigned)nc-nt);
            }
            internal+=ticks;
        }
        dst[i]=(int8_t)((lfsr&1u)?an:-an);
    }
    p->clock_accum=accum; p->internal_ticks=internal;
    p->noise_counter=nc; p->noise_lfsr=lfsr; p->noise_flip=nflip;
    p->rendered_samples+=(uint32_t)samples;
}

#undef MD2GBA_PCM_HOT
#undef MD2GBA_PCM_SELECTED
#undef MD2GBA_PCM_RESIDUAL2
#undef MD2GBA_PCM_RESIDUAL1

Md2GbaAudioSink md2gba_psg_pcm_sink(Md2GbaPsgPcm *p) {
    Md2GbaAudioSink s;
    s.user=p; s.ym2612_write=0; s.ym2612_status=0; s.psg_write=md2gba_psg_pcm_write; s.z80_ram_write=0;
    return s;
}
