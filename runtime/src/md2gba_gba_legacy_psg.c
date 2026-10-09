#include "md2gba_gba_legacy_psg.h"
#include "md2gba_gba_legacy_psg_lut.h"

#define REG16(a) (*(volatile uint16_t*)(a))
#define REG32(a) (*(volatile uint32_t*)(a))
#define SOUND1CNT_L REG16(0x04000060u)
#define SOUND1CNT_H REG16(0x04000062u)
#define SOUND1CNT_X REG16(0x04000064u)
#define SOUND2CNT_L REG16(0x04000068u)
#define SOUND2CNT_H REG16(0x0400006Cu)
#define SOUND3CNT_L REG16(0x04000070u)
#define SOUND3CNT_H REG16(0x04000072u)
#define SOUND3CNT_X REG16(0x04000074u)
#define SOUND4CNT_L REG16(0x04000078u)
#define SOUND4CNT_H REG16(0x0400007Cu)
#define SOUNDCNT_L  REG16(0x04000080u)
#define SOUNDCNT_H  REG16(0x04000082u)
#define SOUNDCNT_X  REG16(0x04000084u)
#define WAVE_RAM32(i) REG32(0x04000090u + ((uint32_t)(i)<<2))

static uint32_t div_round_u32(uint32_t n, uint32_t d) {
    uint32_t q=0u,r=0u; int bit;
    if(!d) return 0u;
    for(bit=31;bit>=0;--bit) {
        r=(r<<1)|((n>>(unsigned)bit)&1u);
        if(r>=d) { r-=d; q|=(1u<<(unsigned)bit); }
    }
    if(r >= (d-r)) ++q;
    return q;
}

uint16_t md2gba_gba_legacy_psg_freq_reg(uint32_t psg_clock_hz, uint16_t sn_period) {
    uint32_t n=(uint32_t)(sn_period&0x03FFu);
    uint32_t delta,reg;
    /* P83 translation table: PSIV uses the NTSC PSG clock, so normal play is
       now one ROM lookup instead of a 32-step software divide on every pitch
       write.  Keep the formula fallback so the runtime remains title/region
       agnostic. */
    if(!psg_clock_hz || psg_clock_hz==3579545u)
        return md2gba_legacy_freq_ntsc[n];
    if(n<2u) n=1u;
    delta=div_round_u32(4194304u*n,psg_clock_hz);
    if(delta<1u) delta=1u;
    if(delta>2047u) delta=2047u;
    reg=2048u-delta;
    return (uint16_t)(reg&0x07FFu);
}

uint8_t md2gba_gba_legacy_psg_volume(uint8_t sn_attenuation) {
    return md2gba_legacy_volume_lut[sn_attenuation&15u];
}

void md2gba_gba_legacy_psg_init(Md2GbaGbaLegacyPsg *g) {
    unsigned i;
    if(!g) return;
    for(i=0;i<3u;i++) { g->period[i]=0xFFFFu; g->attenuation[i]=0xFFu; g->active[i]=0u; }
    g->started=0u; g->output_ratio=2u; g->wave_bank=0u; /* PSC uses SOUNDCNT_H old-sound ratio 2 = 100%. */
    g->noise_control=0xFFu; g->noise_attenuation=0xFFu; g->noise_rate=0xFFu;
}

static void write_pulse(unsigned ch,uint16_t period,uint8_t attenuation,uint32_t clock_hz,int update_volume,int trigger) {
    uint16_t freq=md2gba_gba_legacy_psg_freq_reg(clock_hz,period);
    uint16_t raw=(uint16_t)(period&0x03FFu);
    uint16_t vol=(raw<2u)?0u:(uint16_t)md2gba_gba_legacy_psg_volume(attenuation);
    if(ch==0u) {
        if(update_volume) SOUND1CNT_H=(uint16_t)((2u<<6)|(vol<<12));
        SOUND1CNT_X=(uint16_t)(freq | (trigger?0x8000u:0u));
    } else {
        if(update_volume) SOUND2CNT_L=(uint16_t)((2u<<6)|(vol<<12));
        SOUND2CNT_H=(uint16_t)(freq | (trigger?0x8000u:0u));
    }
}

#if defined(MD2GBA_GBA_LEGACY_PSG_TONE3)
static void wave_fill_inactive(uint8_t attenuation) {
    /* Build-time translated 32-sample square bank.  This removes waveform
       construction from live PSG register traffic. */
    const uint32_t *w=md2gba_legacy_wave_lut[attenuation&15u];
    WAVE_RAM32(0)=w[0]; WAVE_RAM32(1)=w[1]; WAVE_RAM32(2)=w[2]; WAVE_RAM32(3)=w[3];
}
static void wave_set_level(Md2GbaGbaLegacyPsg *g,uint8_t attenuation) {
    wave_fill_inactive(attenuation);
    g->wave_bank^=1u;
    /* One 32-sample bank, select newly written bank, keep output enabled. */
    SOUND3CNT_L=(uint16_t)(0x0080u | (g->wave_bank?0x0040u:0u));
    SOUND3CNT_H=0x2000u; /* wave output 100%; amplitude lives in wave RAM. */
}
static void wave_set_freq(uint16_t period,uint32_t clock_hz,int trigger) {
    uint16_t freq=md2gba_gba_legacy_psg_freq_reg(clock_hz,period);
    SOUND3CNT_X=(uint16_t)(freq | (trigger?0x8000u:0u));
}
#endif

#if defined(MD2GBA_GBA_LEGACY_PSG_NOISE4)
static uint8_t noise_rate_code(const Md2GbaPsg *psg) {
    uint16_t n;
    switch(psg->noise_control&3u) {
        case 0u:n=0x10u;break; case 1u:n=0x20u;break; case 2u:n=0x40u;break;
        default:n=(uint16_t)(psg->tone_period[2]&0x03FFu);if(n<2u)n=1u;break;
    }
    return md2gba_legacy_noise_rate_lut[n&0x03FFu];
}
static void noise_write_hw(uint8_t attenuation,uint8_t control,uint8_t rate,int trigger) {
    uint16_t vol=(uint16_t)md2gba_gba_legacy_psg_volume(attenuation);
    uint16_t poly=(uint16_t)rate;
    /* GB 15-bit white noise is spectrally closest to Sega white noise.
       For Sega periodic noise, 7-bit mode is the nearest legacy-hardware
       surrogate, but it is intentionally marked approximate: the polynomial
       and sequence are not the Sega 16-bit periodic generator. */
    if(!(control&4u)) poly|=0x0008u;
    SOUND4CNT_L=(uint16_t)(vol<<12);
    SOUND4CNT_H=(uint16_t)(poly | (trigger?0x8000u:0u));
}
#endif

void md2gba_gba_legacy_psg_start(Md2GbaGbaLegacyPsg *g,const Md2GbaPsg *psg,uint32_t clock_hz) {
    uint16_t h;
    unsigned ch;
    if(!g || !psg) return;
    SOUNDCNT_X=0x0080u;
#if defined(MD2GBA_GBA_LEGACY_PSG_NOISE4)
    SOUNDCNT_L=0xFF77u; /* all four legacy channels -> left+right, master 7/7. */
#elif defined(MD2GBA_GBA_LEGACY_PSG_TONE3)
    SOUNDCNT_L=0x7777u; /* legacy channels 1,2,3 -> left+right, master 7/7. */
#else
    SOUNDCNT_L=0x3377u; /* legacy channels 1,2 -> left+right, master 7/7. */
#endif
    h=SOUNDCNT_H; h=(uint16_t)((h&~3u)|(g->output_ratio&3u)); SOUNDCNT_H=h;
    SOUND1CNT_L=0x0008u; /* sweep disabled; direction bit set. */
    for(ch=0;ch<2u;ch++) {
        g->period[ch]=psg->tone_period[ch]; g->attenuation[ch]=psg->attenuation[ch];
        g->active[ch]=(uint8_t)(((psg->tone_period[ch]&0x03FFu)>=2u) && md2gba_gba_legacy_psg_volume(psg->attenuation[ch])!=0u);
        write_pulse(ch,g->period[ch],g->attenuation[ch],clock_hz,1,g->active[ch]?1:0);
    }
#if defined(MD2GBA_GBA_LEGACY_PSG_TONE3)
    /* Selected bank 0 means MMIO writes target inactive bank 1. */
    g->wave_bank=0u; SOUND3CNT_L=0x0080u;
    g->period[2]=psg->tone_period[2]; g->attenuation[2]=psg->attenuation[2];
    g->active[2]=(uint8_t)(((psg->tone_period[2]&0x03FFu)>=2u) && md2gba_gba_legacy_psg_volume(psg->attenuation[2])!=0u);
    wave_set_level(g,g->attenuation[2]);
    wave_set_freq(g->period[2],clock_hz,g->active[2]?1:0);
#endif
#if defined(MD2GBA_GBA_LEGACY_PSG_NOISE4)
    g->noise_control=psg->noise_control; g->noise_attenuation=psg->attenuation[3];
    g->noise_rate=noise_rate_code(psg);
    noise_write_hw(g->noise_attenuation,g->noise_control,g->noise_rate,1);
#endif
    g->started=1u;
}

static void legacy_sync_core(Md2GbaGbaLegacyPsg *g,const Md2GbaPsg *psg,uint32_t clock_hz,int noise_trigger) {
    unsigned ch;
    if(!g || !psg || !g->started) return;
    for(ch=0;ch<2u;ch++) {
        uint16_t np=psg->tone_period[ch]; uint8_t na=psg->attenuation[ch];
        uint8_t now_active=(uint8_t)(((np&0x03FFu)>=2u) && md2gba_gba_legacy_psg_volume(na)!=0u);
        uint8_t oldv=md2gba_gba_legacy_psg_volume(g->attenuation[ch]);
        uint8_t newv=md2gba_gba_legacy_psg_volume(na);
        int pc=(np!=g->period[ch]);
        int vc=(newv!=oldv) || (now_active!=g->active[ch]);
        int trigger=0;
        if(!pc&&!vc) { g->attenuation[ch]=na; continue; }
        if(vc && now_active) trigger=1;
        write_pulse(ch,np,na,clock_hz,vc,trigger);
        g->period[ch]=np;g->attenuation[ch]=na;g->active[ch]=now_active;
    }
#if defined(MD2GBA_GBA_LEGACY_PSG_TONE3)
    {
        uint16_t np=psg->tone_period[2]; uint8_t na=psg->attenuation[2];
        uint8_t now_active=(uint8_t)(((np&0x03FFu)>=2u) && md2gba_gba_legacy_psg_volume(na)!=0u);
        int pc=(np!=g->period[2]),vc=(na!=g->attenuation[2]);
        int ac=(now_active!=g->active[2]);
        /* A single inactive-bank rewrite + bank flip changes amplitude without
           retriggering the wave oscillator.  Invalid/period<2 state uses a
           zero-amplitude bank rather than an ultrasonic approximation. */
        if(vc || ac) wave_set_level(g,now_active?na:15u);
        if(pc || (now_active&&!g->active[2]))
            wave_set_freq(np,clock_hz,(now_active&&!g->active[2])?1:0);
        g->period[2]=np;g->attenuation[2]=na;g->active[2]=now_active;
    }
#endif
#if defined(MD2GBA_GBA_LEGACY_PSG_NOISE4)
    {
        uint8_t nc=psg->noise_control,na=psg->attenuation[3],nr=noise_rate_code(psg);
        int rate_changed=(nr!=g->noise_rate) || (nc!=g->noise_control);
        int vol_changed=(md2gba_gba_legacy_psg_volume(na)!=md2gba_gba_legacy_psg_volume(g->noise_attenuation));
        if(rate_changed || vol_changed || noise_trigger)
            noise_write_hw(na,nc,nr,noise_trigger);
        g->noise_control=nc;g->noise_attenuation=na;g->noise_rate=nr;
    }
#endif
}

void md2gba_gba_legacy_psg_sync(Md2GbaGbaLegacyPsg *g,const Md2GbaPsg *psg,uint32_t clock_hz) {
    legacy_sync_core(g,psg,clock_hz,0);
}

void md2gba_gba_legacy_psg_sync_write(Md2GbaGbaLegacyPsg *g,const Md2GbaPsg *psg,uint32_t clock_hz,uint8_t raw_write) {
    int noise_write=0;
#if defined(MD2GBA_GBA_LEGACY_PSG_NOISE4)
    if(raw_write&0x80u) noise_write=((((raw_write>>5)&3u)==3u) && !((raw_write>>4)&1u));
    else noise_write=(psg->latched_channel==3u && !psg->latched_volume);
#else
    (void)raw_write;
#endif
    legacy_sync_core(g,psg,clock_hz,noise_write);
}

void md2gba_gba_legacy_psg_stop(Md2GbaGbaLegacyPsg *g) {
    if(!g) return;
    SOUND1CNT_H=0u; SOUND1CNT_X=0u; SOUND2CNT_L=0u; SOUND2CNT_H=0u;
#if defined(MD2GBA_GBA_LEGACY_PSG_TONE3)
    SOUND3CNT_H=0u; SOUND3CNT_X=0u; SOUND3CNT_L=0u;
#endif
#if defined(MD2GBA_GBA_LEGACY_PSG_NOISE4)
    SOUND4CNT_L=0u; SOUND4CNT_H=0u;
#endif
    SOUNDCNT_L=0u; g->started=0u;
}
