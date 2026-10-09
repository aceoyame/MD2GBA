#include "md2gba_ym2612_pcm.h"

/* 256-entry signed sine and 0.75 dB attenuation tables are generated at
   source-package creation time.  No libm or floating point exists in target
   code. */
#if defined(MD2GBA_GBA_IWRAM)
#define MD2GBA_YM_HOT_DATA __attribute__((section(".iwram.data"),aligned(4)))
#else
#define MD2GBA_YM_HOT_DATA
#endif

static const int8_t ym_sine[256] MD2GBA_YM_HOT_DATA={
    0,3,6,9,12,16,19,22,25,28,31,34,37,40,43,46,
    49,51,54,57,60,63,65,68,71,73,76,78,81,83,85,88,
    90,92,94,96,98,100,102,104,106,107,109,111,112,113,115,116,
    117,118,120,121,122,122,123,124,125,125,126,126,126,127,127,127,
    127,127,127,127,126,126,126,125,125,124,123,122,122,121,120,118,
    117,116,115,113,112,111,109,107,106,104,102,100,98,96,94,92,
    90,88,85,83,81,78,76,73,71,68,65,63,60,57,54,51,
    49,46,43,40,37,34,31,28,25,22,19,16,12,9,6,3,
    0,-3,-6,-9,-12,-16,-19,-22,-25,-28,-31,-34,-37,-40,-43,-46,
    -49,-51,-54,-57,-60,-63,-65,-68,-71,-73,-76,-78,-81,-83,-85,-88,
    -90,-92,-94,-96,-98,-100,-102,-104,-106,-107,-109,-111,-112,-113,-115,-116,
    -117,-118,-120,-121,-122,-122,-123,-124,-125,-125,-126,-126,-126,-127,-127,-127,
    -127,-127,-127,-127,-126,-126,-126,-125,-125,-124,-123,-122,-122,-121,-120,-118,
    -117,-116,-115,-113,-112,-111,-109,-107,-106,-104,-102,-100,-98,-96,-94,-92,
    -90,-88,-85,-83,-81,-78,-76,-73,-71,-68,-65,-63,-60,-57,-54,-51,
    -49,-46,-43,-40,-37,-34,-31,-28,-25,-22,-19,-16,-12,-9,-6,-3,
};
static const uint8_t ym_gain075[128] MD2GBA_YM_HOT_DATA={
    255,234,215,197,181,166,152,139,128,117,108,99,90,83,76,70,
    64,59,54,49,45,42,38,35,32,29,27,25,23,21,19,18,
    16,15,14,12,11,10,10,9,8,7,7,6,6,5,5,4,
    4,4,3,3,3,3,2,2,2,2,2,2,1,1,1,1,
    1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
};
static const uint8_t ym_rate_step[32] MD2GBA_YM_HOT_DATA={
    0,0,0,0,0,0,0,1,1,1,1,1,2,2,2,3,
    3,4,5,6,8,10,12,16,20,24,32,40,48,64,96,128,
};

#if defined(MD2GBA_GBA_IWRAM)
#define MD2GBA_YM_HOT __attribute__((section(".iwram")))
#else
#define MD2GBA_YM_HOT
#endif
#if defined(MD2GBA_GBA_AUDIO_HOT)
#define MD2GBA_YM_SELECTED __attribute__((section(".iwram")))
#else
#define MD2GBA_YM_SELECTED
#endif

static uint32_t udiv32(uint32_t n,uint32_t d){
    uint32_t q=0u,r=0u; int b; if(!d)return 0u;
    for(b=31;b>=0;b--){r=(r<<1)|((n>>(unsigned)b)&1u);if(r>=d){r-=d;q|=1u<<(unsigned)b;}}
    return q;
}
static void zero_bytes(void *p,size_t n){uint8_t *q=(uint8_t*)p;while(n--)*q++=0;}

static unsigned key_channel(uint8_t v){
    unsigned c=(unsigned)(v&3u); if(c==3u)return 0xFFu; if(v&4u)c+=3u; return c;
}
static unsigned op_index(uint8_t reg){return (unsigned)((reg>>2)&3u);}
static unsigned reg_channel(uint8_t port,uint8_t reg){
    unsigned c=(unsigned)(reg&3u); if(c>=3u)return 0xFFu; return c+(port?3u:0u);
}

static uint32_t base_phase_inc(const Md2GbaYm2612Pcm *y,const Md2GbaYmChannel *c){
    /* At 32768 Hz, the YM phase formula collapses to
       inc32 = FNUM * (clock/144) * 2^(block-4).  This derives directly from
       FNUM = 144*f*2^20/clock/2^(block-1). */
    uint32_t v=(uint32_t)c->fnum*y->clock_div_144;
    if(c->block>=4u) v<<=(unsigned)(c->block-4u);
    else v>>=(unsigned)(4u-c->block);
    if(y->sample_rate_hz!=MD2GBA_YM2612_PCM_RATE && y->sample_rate_hz){
        /* Generic-rate fallback. Keep arithmetic 32-bit/helper-free; this is
           primarily for host probes. Production GBA transport is 32768 Hz. */
        uint32_t ratio=udiv32(MD2GBA_YM2612_PCM_RATE<<8,y->sample_rate_hz);
        v=(v>>8)*ratio + (((v&255u)*ratio)>>8);
    }
    return v;
}
static void update_incs(Md2GbaYm2612Pcm *y,unsigned ci){
    Md2GbaYmChannel *c=&y->ch[ci]; uint32_t base=base_phase_inc(y,c); unsigned oi;
    for(oi=0;oi<4u;oi++){
        Md2GbaYmOperator *o=&c->op[oi]; unsigned mul=o->dt_mul&15u; unsigned dt=(o->dt_mul>>4)&7u; uint32_t inc;
        inc=(mul==0u)?(base>>1):(base*mul);
        /* DT1 is intentionally coarse in Phase 25. Preserve sign/magnitude
           semantics now so later exact detune-table work is ABI-neutral. */
        if((dt&3u)!=0u){uint32_t d=(base>>11)*(dt&3u);if(dt&4u)inc-=d;else inc+=d;}
        o->phase_inc=inc;
    }
}

static __attribute__((always_inline)) inline uint8_t operator_gain(const Md2GbaYmOperator *o){
    unsigned att=(unsigned)o->tl+((unsigned)o->env>>3);
    if(att>127u)att=127u;
    return ym_gain075[att];
}

static void key_set(Md2GbaYmOperator *o,unsigned on){
    if(on){
        if(!o->key_on){o->phase=0u;o->env=1023u;o->env_state=MD2GBA_YM_ENV_ATTACK;}
        o->key_on=1u;
    }else{
        if(o->key_on && o->env_state!=MD2GBA_YM_ENV_OFF)o->env_state=MD2GBA_YM_ENV_RELEASE;
        o->key_on=0u;
    }
}
static __attribute__((always_inline)) inline void env_step(Md2GbaYmOperator *o){
    unsigned r,step,target;
    switch(o->env_state){
    case MD2GBA_YM_ENV_ATTACK:
        r=o->rs_ar&31u; if(!r)return;
        if(r>=31u){o->env=0u;o->env_state=MD2GBA_YM_ENV_DECAY;return;}
        step=ym_rate_step[r]; if(!step)step=1u;
        {unsigned d=1u+(((unsigned)o->env*step)>>7);o->env=(uint16_t)(d>=o->env?0u:(unsigned)o->env-d);}
        if(o->env==0u)o->env_state=MD2GBA_YM_ENV_DECAY;
        break;
    case MD2GBA_YM_ENV_DECAY:
        r=o->am_d1r&31u; target=((unsigned)(o->sl_rr>>4)&15u)*64u; if(target>960u)target=960u;
        if(!r){if(o->env>=target)o->env_state=MD2GBA_YM_ENV_SUSTAIN;return;}
        step=ym_rate_step[r]; if(!step)step=1u; o->env=(uint16_t)(((unsigned)o->env+step>1023u)?1023u:(unsigned)o->env+step);
        if(o->env>=target) o->env_state=MD2GBA_YM_ENV_SUSTAIN;
        break;
    case MD2GBA_YM_ENV_SUSTAIN:
        r=o->d2r&31u; if(!r)return; step=ym_rate_step[r]; if(!step)step=1u;
        o->env=(uint16_t)(((unsigned)o->env+step>1023u)?1023u:(unsigned)o->env+step); if(o->env>=1023u)o->env_state=MD2GBA_YM_ENV_OFF; break;
    case MD2GBA_YM_ENV_RELEASE:
        r=((unsigned)(o->sl_rr&15u)<<1)|1u; step=ym_rate_step[r]; if(!step)step=1u;
        o->env=(uint16_t)(((unsigned)o->env+step>1023u)?1023u:(unsigned)o->env+step); if(o->env>=1023u)o->env_state=MD2GBA_YM_ENV_OFF; break;
    default: break;
    }
}

static MD2GBA_YM_HOT __attribute__((noinline)) int op_sample(Md2GbaYmOperator *o,int mod){
    uint32_t pm; int s; unsigned g;
    if(o->env_state==MD2GBA_YM_ENV_OFF)return 0;
    /* D2R=0 sustain is a common steady-state case.  Phase 25 recalculated an
       unchanged envelope and attenuation table index for every operator on
       every 32.768-kHz output sample.  Cache that gain and only revisit the
       envelope when the state can actually move. */
    if(!(o->env_state==MD2GBA_YM_ENV_SUSTAIN && (o->d2r&31u)==0u)){
        env_step(o);
        o->gain=operator_gain(o);
    }
    o->phase+=o->phase_inc;
    pm=o->phase+(uint32_t)((int32_t)mod*1048576);
    g=o->gain; s=(int)ym_sine[pm>>24]; return (s*(int)g)>>8;
}

static __attribute__((always_inline)) inline unsigned alg4_stable(const Md2GbaYmChannel *c){
    unsigned i;
    if((c->algorithm&7u)!=4u)return 0u;
    for(i=0u;i<4u;i++)
        if(c->op[i].env_state!=MD2GBA_YM_ENV_SUSTAIN || (c->op[i].d2r&31u)!=0u)return 0u;
    return 1u;
}

/* Phase 28: the Phase-27 real-mGBA result got within roughly ten percent of
   realtime (Q/R stayed close and the screen was briefly green), which makes
   the four signed multiplies per output sample the next dominant cost in the
   stable algorithm-4 kernel.  Stable sustain has a constant gain per operator,
   so cache sine*gain/256 exactly once.  The cache is rebuilt only when the
   operator gain changes, preserving the Phase-25/26/27 PCM bit-for-bit. */
static MD2GBA_YM_SELECTED __attribute__((noinline)) void prepare_wave_cache(
    Md2GbaYm2612Pcm *y,const Md2GbaYmChannel *c,const int8_t **waves){
    uint32_t pinned=0u; unsigned o,i,slot;
    for(o=0u;o<4u;o++){
        uint8_t g=c->op[o].gain; int found=-1;
        /* Reuse an identical gain table first. */
        for(slot=0u;slot<17u;slot++)if(y->wave_gain[slot]==g){found=(int)slot;break;}
        if(found<0){
            /* Prefer an unused slot, otherwise round-robin among slots not
               already handed out to this four-operator kernel invocation. */
            for(slot=0u;slot<17u;slot++)if(y->wave_gain[slot]==0xFFu && !(pinned&(1u<<slot))){found=(int)slot;break;}
            if(found<0){
                for(i=0u;i<17u;i++){
                    slot=(unsigned)(y->wave_next+i); if(slot>=17u)slot-=17u;
                    if(!(pinned&(1u<<slot))){found=(int)slot;break;}
                }
            }
            slot=(unsigned)found;
            for(i=0u;i<256u;i++)y->wave_cache[slot][i]=(int8_t)(((int)ym_sine[i]*(int)g)>>8);
            y->wave_gain[slot]=g;
            y->wave_next=(uint8_t)(slot+1u); if(y->wave_next>=17u)y->wave_next=0u;
        }
        slot=(unsigned)found; pinned|=1u<<slot; waves[o]=y->wave_cache[slot];
    }
}

/* Phase 27: the real-GBA Phase-26 trace showed FM production still about
   1.8x behind consumption even with the whole generic renderer in IWRAM.
   The hot synthetic/game-like case is a sustained channel: envelopes are not
   moving, but Phase 26 still paid four op_sample calls plus render_channel for
   every output sample.  Keep all phases, gains, feedback history and the two
   algorithm-4 carrier chains in locals across the whole slice.  This is
   sample-identical to the generic path; it only removes call/branch/state
   traffic. */
static MD2GBA_YM_HOT __attribute__((noinline)) void render_alg4_stable(Md2GbaYm2612Pcm *y,Md2GbaYmChannel *c,int8_t *dst,size_t samples,unsigned add){
    uint32_t p0=c->op[0].phase,p1=c->op[1].phase,p2=c->op[2].phase,p3=c->op[3].phase;
    const uint32_t i0=c->op[0].phase_inc,i1=c->op[1].phase_inc,i2=c->op[2].phase_inc,i3=c->op[3].phase_inc;
    const int8_t *w0,*w1,*w2,*w3,*wv[4];
    int fb0=(int)c->fb_prev[0],fb1=(int)c->fb_prev[1];
    const unsigned feedback=c->feedback;
    size_t n;
    prepare_wave_cache(y,c,wv);
    w0=wv[0];w1=wv[1];w2=wv[2];w3=wv[3];
    for(n=0;n<samples;n++){
        int fb=0,a,b,d,e,v;
        uint32_t pm;
        if(feedback)fb=(fb0+fb1)>>(8u-feedback);
        p0+=i0;pm=p0+(uint32_t)((int32_t)fb*1048576);a=(int)w0[pm>>24];
        fb1=fb0;fb0=a;
        p1+=i1;pm=p1+(uint32_t)((int32_t)a*1048576);b=(int)w1[pm>>24];
        p2+=i2;d=(int)w2[p2>>24];
        p3+=i3;pm=p3+(uint32_t)((int32_t)d*1048576);e=(int)w3[pm>>24];
        v=(b+e)>>1;v>>=1;
        if(v>127)v=127;else if(v<-128)v=-128;
        if(add){v+=(int)dst[n];if(v>127)v=127;else if(v<-128)v=-128;}
        dst[n]=(int8_t)v;
    }
    c->op[0].phase=p0;c->op[1].phase=p1;c->op[2].phase=p2;c->op[3].phase=p3;
    c->fb_prev[0]=(int16_t)fb0;c->fb_prev[1]=(int16_t)fb1;
}


/* Phase 31: general stable-envelope block kernel for real multi-channel FM.
   Phase 30 proved the transport and translated-PC resume path.  The next real
   Genesis workload is several simultaneously audible YM channels, where the
   old sample-major loop reloaded channel/operator state and called op_sample
   for every operator of every channel.  Once all operators on an audible
   channel are in D2R=0 sustain, the waveform/gain is immutable over the block.
   Keep phases/feedback in ARM registers and accumulate each channel into a
   small signed-16 block, preserving the exact Phase-25 algorithm topology and
   final mixer clamp. */
static __attribute__((always_inline)) inline unsigned channel_stable(const Md2GbaYmChannel *c){
    unsigned i;
    for(i=0u;i<4u;i++)
        if(c->op[i].env_state!=MD2GBA_YM_ENV_SUSTAIN || (c->op[i].d2r&31u)!=0u)return 0u;
    return 1u;
}

static __attribute__((always_inline)) inline int wave_mod(const int8_t *w,uint32_t *p,uint32_t inc,int mod){
    uint32_t pm; *p+=inc; pm=*p+(uint32_t)((int32_t)mod*1048576); return (int)w[pm>>24];
}
static __attribute__((always_inline)) inline int wave_plain(const int8_t *w,uint32_t *p,uint32_t inc){
    *p+=inc; return (int)w[*p>>24];
}

static MD2GBA_YM_HOT __attribute__((noinline)) void render_stable_channel_accum(Md2GbaYm2612Pcm *y,Md2GbaYmChannel *c,int16_t *acc,size_t samples){
    uint32_t p0=c->op[0].phase,p1=c->op[1].phase,p2=c->op[2].phase,p3=c->op[3].phase;
    const uint32_t i0=c->op[0].phase_inc,i1=c->op[1].phase_inc,i2=c->op[2].phase_inc,i3=c->op[3].phase_inc;
    const int8_t *w0,*w1,*w2,*w3,*wv[4]; int fb0=(int)c->fb_prev[0],fb1=(int)c->fb_prev[1];
    const unsigned feedback=c->feedback,alg=c->algorithm&7u; size_t n;
    prepare_wave_cache(y,c,wv); w0=wv[0];w1=wv[1];w2=wv[2];w3=wv[3];
    switch(alg){
    case 0:
        for(n=0;n<samples;n++){int fb=feedback?((fb0+fb1)>>(8u-feedback)):0;int a=wave_mod(w0,&p0,i0,fb);int d,b,e;fb1=fb0;fb0=a;d=wave_mod(w2,&p2,i2,a);b=wave_mod(w1,&p1,i1,d);e=wave_mod(w3,&p3,i3,b);acc[n]=(int16_t)(acc[n]+e);}break;
    case 1:
        for(n=0;n<samples;n++){int fb=feedback?((fb0+fb1)>>(8u-feedback)):0;int a=wave_mod(w0,&p0,i0,fb);int d,b,e;fb1=fb0;fb0=a;d=wave_plain(w2,&p2,i2);b=wave_mod(w1,&p1,i1,a+d);e=wave_mod(w3,&p3,i3,b);acc[n]=(int16_t)(acc[n]+e);}break;
    case 2:
        for(n=0;n<samples;n++){int fb=feedback?((fb0+fb1)>>(8u-feedback)):0;int a=wave_mod(w0,&p0,i0,fb);int d,b,e;fb1=fb0;fb0=a;d=wave_plain(w2,&p2,i2);b=wave_mod(w1,&p1,i1,d);e=wave_mod(w3,&p3,i3,a+b);acc[n]=(int16_t)(acc[n]+e);}break;
    case 3:
        for(n=0;n<samples;n++){int fb=feedback?((fb0+fb1)>>(8u-feedback)):0;int a=wave_mod(w0,&p0,i0,fb);int d,b,e;fb1=fb0;fb0=a;d=wave_mod(w2,&p2,i2,a);b=wave_plain(w1,&p1,i1);e=wave_mod(w3,&p3,i3,d+b);acc[n]=(int16_t)(acc[n]+e);}break;
    case 4:
        for(n=0;n<samples;n++){int fb=feedback?((fb0+fb1)>>(8u-feedback)):0;int a=wave_mod(w0,&p0,i0,fb);int b,d,e;fb1=fb0;fb0=a;b=wave_mod(w1,&p1,i1,a);d=wave_plain(w2,&p2,i2);e=wave_mod(w3,&p3,i3,d);acc[n]=(int16_t)(acc[n]+((b+e)>>1));}break;
    case 5:
        for(n=0;n<samples;n++){int fb=feedback?((fb0+fb1)>>(8u-feedback)):0;int a=wave_mod(w0,&p0,i0,fb);int b,d,e;fb1=fb0;fb0=a;b=wave_mod(w1,&p1,i1,a);d=wave_mod(w2,&p2,i2,a);e=wave_mod(w3,&p3,i3,a);acc[n]=(int16_t)(acc[n]+((b+d+e)>>2));}break;
    case 6:
        for(n=0;n<samples;n++){int fb=feedback?((fb0+fb1)>>(8u-feedback)):0;int a=wave_mod(w0,&p0,i0,fb);int b,d,e;fb1=fb0;fb0=a;b=wave_mod(w1,&p1,i1,a);d=wave_plain(w2,&p2,i2);e=wave_plain(w3,&p3,i3);acc[n]=(int16_t)(acc[n]+((b+d+e)>>2));}break;
    default:
        for(n=0;n<samples;n++){int fb=feedback?((fb0+fb1)>>(8u-feedback)):0;int a=wave_mod(w0,&p0,i0,fb);int b,d,e;fb1=fb0;fb0=a;b=wave_plain(w1,&p1,i1);d=wave_plain(w2,&p2,i2);e=wave_plain(w3,&p3,i3);acc[n]=(int16_t)(acc[n]+((a+b+d+e)>>2));}break;
    }
    c->op[0].phase=p0;c->op[1].phase=p1;c->op[2].phase=p2;c->op[3].phase=p3;
    c->fb_prev[0]=(int16_t)fb0;c->fb_prev[1]=(int16_t)fb1;
}


/* Phase 32: feedback-zero steady-state kernel.
   The Phase-31 hardware result was ~13-16% behind realtime with three FM
   channels, while all three probe voices use FB=0.  In that case operator 1
   has no sample-to-sample feedback dependency.  Keep one waveform base plus
   all four phase increments in ARM registers and rebuild the two feedback
   history values from the final two operator-1 phases after the slice.  This
   removes four hot stack loads and the feedback branch/shift from each sample
   while preserving the exact PCM and feedback state should a later register
   write enable feedback. */
static MD2GBA_YM_HOT __attribute__((noinline)) void render_stable_channel_accum_nofb(Md2GbaYm2612Pcm *y,Md2GbaYmChannel *c,int16_t *acc,size_t samples){
    uint32_t p0=c->op[0].phase,p1=c->op[1].phase,p2=c->op[2].phase,p3=c->op[3].phase;
    const uint32_t i0=c->op[0].phase_inc,i1=c->op[1].phase_inc,i2=c->op[2].phase_inc,i3=c->op[3].phase_inc;
    const int8_t *wv[4]; const unsigned alg=c->algorithm&7u; size_t n;
    prepare_wave_cache(y,c,wv);
#define W0(pm) ((int)wv[0][((pm)>>24)])
#define W1(pm) ((int)wv[1][((pm)>>24)])
#define W2(pm) ((int)wv[2][((pm)>>24)])
#define W3(pm) ((int)wv[3][((pm)>>24)])
    switch(alg){
    case 0: for(n=0;n<samples;n++){uint32_t pm;int a,d,b,e;p0+=i0;a=W0(p0);p2+=i2;pm=p2+(uint32_t)((int32_t)a*1048576);d=W2(pm);p1+=i1;pm=p1+(uint32_t)((int32_t)d*1048576);b=W1(pm);p3+=i3;pm=p3+(uint32_t)((int32_t)b*1048576);e=W3(pm);acc[n]=(int16_t)(acc[n]+e);}break;
    case 1: for(n=0;n<samples;n++){uint32_t pm;int a,d,b,e;p0+=i0;a=W0(p0);p2+=i2;d=W2(p2);p1+=i1;pm=p1+(uint32_t)((int32_t)(a+d)*1048576);b=W1(pm);p3+=i3;pm=p3+(uint32_t)((int32_t)b*1048576);e=W3(pm);acc[n]=(int16_t)(acc[n]+e);}break;
    case 2: for(n=0;n<samples;n++){uint32_t pm;int a,d,b,e;p0+=i0;a=W0(p0);p2+=i2;d=W2(p2);p1+=i1;pm=p1+(uint32_t)((int32_t)d*1048576);b=W1(pm);p3+=i3;pm=p3+(uint32_t)((int32_t)(a+b)*1048576);e=W3(pm);acc[n]=(int16_t)(acc[n]+e);}break;
    case 3: for(n=0;n<samples;n++){uint32_t pm;int a,d,b,e;p0+=i0;a=W0(p0);p2+=i2;pm=p2+(uint32_t)((int32_t)a*1048576);d=W2(pm);p1+=i1;b=W1(p1);p3+=i3;pm=p3+(uint32_t)((int32_t)(d+b)*1048576);e=W3(pm);acc[n]=(int16_t)(acc[n]+e);}break;
    case 4: for(n=0;n<samples;n++){uint32_t pm;int a,b,d,e;p0+=i0;a=W0(p0);p1+=i1;pm=p1+(uint32_t)((int32_t)a*1048576);b=W1(pm);p2+=i2;d=W2(p2);p3+=i3;pm=p3+(uint32_t)((int32_t)d*1048576);e=W3(pm);acc[n]=(int16_t)(acc[n]+((b+e)>>1));}break;
    case 5: for(n=0;n<samples;n++){uint32_t pm;int a,b,d,e;p0+=i0;a=W0(p0);p1+=i1;pm=p1+(uint32_t)((int32_t)a*1048576);b=W1(pm);p2+=i2;pm=p2+(uint32_t)((int32_t)a*1048576);d=W2(pm);p3+=i3;pm=p3+(uint32_t)((int32_t)a*1048576);e=W3(pm);acc[n]=(int16_t)(acc[n]+((b+d+e)>>2));}break;
    case 6: for(n=0;n<samples;n++){uint32_t pm;int a,b,d,e;p0+=i0;a=W0(p0);p1+=i1;pm=p1+(uint32_t)((int32_t)a*1048576);b=W1(pm);p2+=i2;d=W2(p2);p3+=i3;e=W3(p3);acc[n]=(int16_t)(acc[n]+((b+d+e)>>2));}break;
    default: for(n=0;n<samples;n++){int a,b,d,e;p0+=i0;a=W0(p0);p1+=i1;b=W1(p1);p2+=i2;d=W2(p2);p3+=i3;e=W3(p3);acc[n]=(int16_t)(acc[n]+((a+b+d+e)>>2));}break;
    }
    c->op[0].phase=p0;c->op[1].phase=p1;c->op[2].phase=p2;c->op[3].phase=p3;
    if(samples){uint32_t prevp=p0-i0;c->fb_prev[0]=(int16_t)W0(p0);c->fb_prev[1]=(int16_t)W0(prevp);}
#undef W0
#undef W1
#undef W2
#undef W3
}


/* Phase 34: paired-increment feedback-zero kernel.
   A large class of Genesis patches (including the six-channel stress probe)
   uses the common 1,2,1,2 operator-multiplier pattern, which makes operators
   0/2 share one phase increment and 1/3 share another.  The Phase-32 generic
   no-feedback kernel still had to keep four independent increments live, which
   forced ARM7 register pressure/spills in several algorithms.  Detect equal
   increment pairs at runtime and use only two increment registers.  This is an
   exact fast path: phases, waveform lookup order, carrier scaling, accumulator
   math and reconstructed feedback history are unchanged. */
static MD2GBA_YM_HOT __attribute__((noinline)) void render_stable_channel_accum_nofb_pairinc(Md2GbaYm2612Pcm *y,Md2GbaYmChannel *c,int16_t *acc,size_t samples){
    uint32_t p0=c->op[0].phase,p1=c->op[1].phase,p2=c->op[2].phase,p3=c->op[3].phase;
    const uint32_t ia=c->op[0].phase_inc,ib=c->op[1].phase_inc;
    const int8_t *wv[4]; const unsigned alg=c->algorithm&7u; size_t n;
    prepare_wave_cache(y,c,wv);
#define P34W0(pm) ((int)wv[0][((pm)>>24)])
#define P34W1(pm) ((int)wv[1][((pm)>>24)])
#define P34W2(pm) ((int)wv[2][((pm)>>24)])
#define P34W3(pm) ((int)wv[3][((pm)>>24)])
    switch(alg){
    case 0: for(n=0;n<samples;n++){uint32_t pm;int a,d,b,e;p0+=ia;a=P34W0(p0);p2+=ia;pm=p2+(uint32_t)((int32_t)a*1048576);d=P34W2(pm);p1+=ib;pm=p1+(uint32_t)((int32_t)d*1048576);b=P34W1(pm);p3+=ib;pm=p3+(uint32_t)((int32_t)b*1048576);e=P34W3(pm);acc[n]=(int16_t)(acc[n]+e);}break;
    case 1: for(n=0;n<samples;n++){uint32_t pm;int a,d,b,e;p0+=ia;a=P34W0(p0);p2+=ia;d=P34W2(p2);p1+=ib;pm=p1+(uint32_t)((int32_t)(a+d)*1048576);b=P34W1(pm);p3+=ib;pm=p3+(uint32_t)((int32_t)b*1048576);e=P34W3(pm);acc[n]=(int16_t)(acc[n]+e);}break;
    case 2: for(n=0;n<samples;n++){uint32_t pm;int a,d,b,e;p0+=ia;a=P34W0(p0);p2+=ia;d=P34W2(p2);p1+=ib;pm=p1+(uint32_t)((int32_t)d*1048576);b=P34W1(pm);p3+=ib;pm=p3+(uint32_t)((int32_t)(a+b)*1048576);e=P34W3(pm);acc[n]=(int16_t)(acc[n]+e);}break;
    case 3: for(n=0;n<samples;n++){uint32_t pm;int a,d,b,e;p0+=ia;a=P34W0(p0);p2+=ia;pm=p2+(uint32_t)((int32_t)a*1048576);d=P34W2(pm);p1+=ib;b=P34W1(p1);p3+=ib;pm=p3+(uint32_t)((int32_t)(d+b)*1048576);e=P34W3(pm);acc[n]=(int16_t)(acc[n]+e);}break;
    case 4: for(n=0;n<samples;n++){uint32_t pm;int a,b,d,e;p0+=ia;a=P34W0(p0);p1+=ib;pm=p1+(uint32_t)((int32_t)a*1048576);b=P34W1(pm);p2+=ia;d=P34W2(p2);p3+=ib;pm=p3+(uint32_t)((int32_t)d*1048576);e=P34W3(pm);acc[n]=(int16_t)(acc[n]+((b+e)>>1));}break;
    case 5: for(n=0;n<samples;n++){uint32_t pm;int a,b,d,e;p0+=ia;a=P34W0(p0);p1+=ib;pm=p1+(uint32_t)((int32_t)a*1048576);b=P34W1(pm);p2+=ia;pm=p2+(uint32_t)((int32_t)a*1048576);d=P34W2(pm);p3+=ib;pm=p3+(uint32_t)((int32_t)a*1048576);e=P34W3(pm);acc[n]=(int16_t)(acc[n]+((b+d+e)>>2));}break;
    case 6: for(n=0;n<samples;n++){uint32_t pm;int a,b,d,e;p0+=ia;a=P34W0(p0);p1+=ib;pm=p1+(uint32_t)((int32_t)a*1048576);b=P34W1(pm);p2+=ia;d=P34W2(p2);p3+=ib;e=P34W3(p3);acc[n]=(int16_t)(acc[n]+((b+d+e)>>2));}break;
    default: for(n=0;n<samples;n++){int a,b,d,e;p0+=ia;a=P34W0(p0);p1+=ib;b=P34W1(p1);p2+=ia;d=P34W2(p2);p3+=ib;e=P34W3(p3);acc[n]=(int16_t)(acc[n]+((a+b+d+e)>>2));}break;
    }
    c->op[0].phase=p0;c->op[1].phase=p1;c->op[2].phase=p2;c->op[3].phase=p3;
    if(samples){uint32_t prevp=p0-ia;c->fb_prev[0]=(int16_t)P34W0(p0);c->fb_prev[1]=(int16_t)P34W0(prevp);}
#undef P34W0
#undef P34W1
#undef P34W2
#undef P34W3
}

/* Phase 34: first-channel store variant.  Stable multi-channel rendering used
   to clear the whole 16-bit accumulator and then load/add/store it again for
   the first FM channel.  When any active channel qualifies for the exact
   paired-increment/no-feedback path, render that channel first and overwrite
   the accumulator.  Addition is exact/commutative here (six signed 8-bit-ish
   channel results cannot overflow int16_t), so this removes one accumulator
   read plus the entire zero-fill pass without changing PCM or channel state. */
static MD2GBA_YM_HOT __attribute__((noinline)) void render_stable_channel_store_nofb_pairinc(Md2GbaYm2612Pcm *y,Md2GbaYmChannel *c,int16_t *acc,size_t samples){
    uint32_t p0=c->op[0].phase,p1=c->op[1].phase,p2=c->op[2].phase,p3=c->op[3].phase;
    const uint32_t ia=c->op[0].phase_inc,ib=c->op[1].phase_inc;
    const int8_t *wv[4]; const unsigned alg=c->algorithm&7u; size_t n;
    prepare_wave_cache(y,c,wv);
#define P34W0(pm) ((int)wv[0][((pm)>>24)])
#define P34W1(pm) ((int)wv[1][((pm)>>24)])
#define P34W2(pm) ((int)wv[2][((pm)>>24)])
#define P34W3(pm) ((int)wv[3][((pm)>>24)])
    switch(alg){
    case 0: for(n=0;n<samples;n++){uint32_t pm;int a,d,b,e;p0+=ia;a=P34W0(p0);p2+=ia;pm=p2+(uint32_t)((int32_t)a*1048576);d=P34W2(pm);p1+=ib;pm=p1+(uint32_t)((int32_t)d*1048576);b=P34W1(pm);p3+=ib;pm=p3+(uint32_t)((int32_t)b*1048576);e=P34W3(pm);acc[n]=(int16_t)e;}break;
    case 1: for(n=0;n<samples;n++){uint32_t pm;int a,d,b,e;p0+=ia;a=P34W0(p0);p2+=ia;d=P34W2(p2);p1+=ib;pm=p1+(uint32_t)((int32_t)(a+d)*1048576);b=P34W1(pm);p3+=ib;pm=p3+(uint32_t)((int32_t)b*1048576);e=P34W3(pm);acc[n]=(int16_t)e;}break;
    case 2: for(n=0;n<samples;n++){uint32_t pm;int a,d,b,e;p0+=ia;a=P34W0(p0);p2+=ia;d=P34W2(p2);p1+=ib;pm=p1+(uint32_t)((int32_t)d*1048576);b=P34W1(pm);p3+=ib;pm=p3+(uint32_t)((int32_t)(a+b)*1048576);e=P34W3(pm);acc[n]=(int16_t)e;}break;
    case 3: for(n=0;n<samples;n++){uint32_t pm;int a,d,b,e;p0+=ia;a=P34W0(p0);p2+=ia;pm=p2+(uint32_t)((int32_t)a*1048576);d=P34W2(pm);p1+=ib;b=P34W1(p1);p3+=ib;pm=p3+(uint32_t)((int32_t)(d+b)*1048576);e=P34W3(pm);acc[n]=(int16_t)e;}break;
    case 4: for(n=0;n<samples;n++){uint32_t pm;int a,b,d,e;p0+=ia;a=P34W0(p0);p1+=ib;pm=p1+(uint32_t)((int32_t)a*1048576);b=P34W1(pm);p2+=ia;d=P34W2(p2);p3+=ib;pm=p3+(uint32_t)((int32_t)d*1048576);e=P34W3(pm);acc[n]=(int16_t)((b+e)>>1);}break;
    case 5: for(n=0;n<samples;n++){uint32_t pm;int a,b,d,e;p0+=ia;a=P34W0(p0);p1+=ib;pm=p1+(uint32_t)((int32_t)a*1048576);b=P34W1(pm);p2+=ia;pm=p2+(uint32_t)((int32_t)a*1048576);d=P34W2(pm);p3+=ib;pm=p3+(uint32_t)((int32_t)a*1048576);e=P34W3(pm);acc[n]=(int16_t)((b+d+e)>>2);}break;
    case 6: for(n=0;n<samples;n++){uint32_t pm;int a,b,d,e;p0+=ia;a=P34W0(p0);p1+=ib;pm=p1+(uint32_t)((int32_t)a*1048576);b=P34W1(pm);p2+=ia;d=P34W2(p2);p3+=ib;e=P34W3(p3);acc[n]=(int16_t)((b+d+e)>>2);}break;
    default: for(n=0;n<samples;n++){int a,b,d,e;p0+=ia;a=P34W0(p0);p1+=ib;b=P34W1(p1);p2+=ia;d=P34W2(p2);p3+=ib;e=P34W3(p3);acc[n]=(int16_t)((a+b+d+e)>>2);}break;
    }
    c->op[0].phase=p0;c->op[1].phase=p1;c->op[2].phase=p2;c->op[3].phase=p3;
    if(samples){uint32_t prevp=p0-ia;c->fb_prev[0]=(int16_t)P34W0(p0);c->fb_prev[1]=(int16_t)P34W0(prevp);}
#undef P34W0
#undef P34W1
#undef P34W2
#undef P34W3
}



/* Phase 35: phase-paired exact kernels.
   When operators 0/2 have the same increment AND phase, and operators 1/3 do
   too, those equalities remain invariant for the whole stable slice.  Common
   1,2,1,2 multiplier patches keyed simultaneously satisfy this naturally.
   Track only p0/p1 and reuse them for operators 2/3.  Compared with Phase 34
   this removes two live phase registers and two phase-update instructions per
   output sample, which also lets ARM7 keep the two increments out of the
   stack in the hot loops. */
static MD2GBA_YM_SELECTED __attribute__((noinline)) void render_stable_channel_accum_nofb_pairphase(Md2GbaYm2612Pcm *y,Md2GbaYmChannel *c,int16_t *acc,size_t samples){
    uint32_t p0=c->op[0].phase,p1=c->op[1].phase;
    const uint32_t ia=c->op[0].phase_inc,ib=c->op[1].phase_inc;
    const int8_t *wv[4]; const unsigned alg=c->algorithm&7u; size_t n;
    prepare_wave_cache(y,c,wv);
#define P35P0(pm) ((int)wv[0][((pm)>>24)])
#define P35P1(pm) ((int)wv[1][((pm)>>24)])
#define P35P2(pm) ((int)wv[2][((pm)>>24)])
#define P35P3(pm) ((int)wv[3][((pm)>>24)])
    switch(alg){
    case 0:for(n=0;n<samples;n++){uint32_t pm;int a,d,b,e;p0+=ia;a=P35P0(p0);pm=p0+(uint32_t)((int32_t)a<<20);d=P35P2(pm);p1+=ib;pm=p1+(uint32_t)((int32_t)d<<20);b=P35P1(pm);pm=p1+(uint32_t)((int32_t)b<<20);e=P35P3(pm);acc[n]=(int16_t)(acc[n]+e);}break;
    case 1:for(n=0;n<samples;n++){uint32_t pm;int a,d,b,e;p0+=ia;a=P35P0(p0);d=P35P2(p0);p1+=ib;pm=p1+(uint32_t)((int32_t)(a+d)<<20);b=P35P1(pm);pm=p1+(uint32_t)((int32_t)b<<20);e=P35P3(pm);acc[n]=(int16_t)(acc[n]+e);}break;
    case 2:for(n=0;n<samples;n++){uint32_t pm;int a,d,b,e;p0+=ia;a=P35P0(p0);d=P35P2(p0);p1+=ib;pm=p1+(uint32_t)((int32_t)d<<20);b=P35P1(pm);pm=p1+(uint32_t)((int32_t)(a+b)<<20);e=P35P3(pm);acc[n]=(int16_t)(acc[n]+e);}break;
    case 3:for(n=0;n<samples;n++){uint32_t pm;int a,d,b,e;p0+=ia;a=P35P0(p0);pm=p0+(uint32_t)((int32_t)a<<20);d=P35P2(pm);p1+=ib;b=P35P1(p1);pm=p1+(uint32_t)((int32_t)(d+b)<<20);e=P35P3(pm);acc[n]=(int16_t)(acc[n]+e);}break;
    case 4:for(n=0;n<samples;n++){uint32_t pm;int a,b,d,e;p0+=ia;a=P35P0(p0);d=P35P2(p0);p1+=ib;pm=p1+(uint32_t)((int32_t)a<<20);b=P35P1(pm);pm=p1+(uint32_t)((int32_t)d<<20);e=P35P3(pm);acc[n]=(int16_t)(acc[n]+((b+e)>>1));}break;
    case 5:for(n=0;n<samples;n++){uint32_t pm;int a,b,d,e;p0+=ia;a=P35P0(p0);p1+=ib;pm=p1+(uint32_t)((int32_t)a<<20);b=P35P1(pm);pm=p0+(uint32_t)((int32_t)a<<20);d=P35P2(pm);pm=p1+(uint32_t)((int32_t)a<<20);e=P35P3(pm);acc[n]=(int16_t)(acc[n]+((b+d+e)>>2));}break;
    case 6:for(n=0;n<samples;n++){uint32_t pm;int a,b,d,e;p0+=ia;a=P35P0(p0);d=P35P2(p0);p1+=ib;pm=p1+(uint32_t)((int32_t)a<<20);b=P35P1(pm);e=P35P3(p1);acc[n]=(int16_t)(acc[n]+((b+d+e)>>2));}break;
    default:for(n=0;n<samples;n++){int a,b,d,e;p0+=ia;a=P35P0(p0);d=P35P2(p0);p1+=ib;b=P35P1(p1);e=P35P3(p1);acc[n]=(int16_t)(acc[n]+((a+b+d+e)>>2));}break;
    }
    c->op[0].phase=p0;c->op[2].phase=p0;c->op[1].phase=p1;c->op[3].phase=p1;
    if(samples){uint32_t prevp=p0-ia;c->fb_prev[0]=(int16_t)P35P0(p0);c->fb_prev[1]=(int16_t)P35P0(prevp);}
#undef P35P0
#undef P35P1
#undef P35P2
#undef P35P3
}

static MD2GBA_YM_SELECTED __attribute__((noinline)) void render_stable_channel_store_nofb_pairphase(Md2GbaYm2612Pcm *y,Md2GbaYmChannel *c,int16_t *acc,size_t samples){
    uint32_t p0=c->op[0].phase,p1=c->op[1].phase;
    const uint32_t ia=c->op[0].phase_inc,ib=c->op[1].phase_inc;
    const int8_t *wv[4]; const unsigned alg=c->algorithm&7u; size_t n;
    prepare_wave_cache(y,c,wv);
#define P35S0(pm) ((int)wv[0][((pm)>>24)])
#define P35S1(pm) ((int)wv[1][((pm)>>24)])
#define P35S2(pm) ((int)wv[2][((pm)>>24)])
#define P35S3(pm) ((int)wv[3][((pm)>>24)])
    switch(alg){
    case 0:for(n=0;n<samples;n++){uint32_t pm;int a,d,b,e;p0+=ia;a=P35S0(p0);pm=p0+(uint32_t)((int32_t)a<<20);d=P35S2(pm);p1+=ib;pm=p1+(uint32_t)((int32_t)d<<20);b=P35S1(pm);pm=p1+(uint32_t)((int32_t)b<<20);e=P35S3(pm);acc[n]=(int16_t)e;}break;
    case 1:for(n=0;n<samples;n++){uint32_t pm;int a,d,b,e;p0+=ia;a=P35S0(p0);d=P35S2(p0);p1+=ib;pm=p1+(uint32_t)((int32_t)(a+d)<<20);b=P35S1(pm);pm=p1+(uint32_t)((int32_t)b<<20);e=P35S3(pm);acc[n]=(int16_t)e;}break;
    case 2:for(n=0;n<samples;n++){uint32_t pm;int a,d,b,e;p0+=ia;a=P35S0(p0);d=P35S2(p0);p1+=ib;pm=p1+(uint32_t)((int32_t)d<<20);b=P35S1(pm);pm=p1+(uint32_t)((int32_t)(a+b)<<20);e=P35S3(pm);acc[n]=(int16_t)e;}break;
    case 3:for(n=0;n<samples;n++){uint32_t pm;int a,d,b,e;p0+=ia;a=P35S0(p0);pm=p0+(uint32_t)((int32_t)a<<20);d=P35S2(pm);p1+=ib;b=P35S1(p1);pm=p1+(uint32_t)((int32_t)(d+b)<<20);e=P35S3(pm);acc[n]=(int16_t)e;}break;
    case 4:for(n=0;n<samples;n++){uint32_t pm;int a,b,d,e;p0+=ia;a=P35S0(p0);d=P35S2(p0);p1+=ib;pm=p1+(uint32_t)((int32_t)a<<20);b=P35S1(pm);pm=p1+(uint32_t)((int32_t)d<<20);e=P35S3(pm);acc[n]=(int16_t)((b+e)>>1);}break;
    case 5:for(n=0;n<samples;n++){uint32_t pm;int a,b,d,e;p0+=ia;a=P35S0(p0);p1+=ib;pm=p1+(uint32_t)((int32_t)a<<20);b=P35S1(pm);pm=p0+(uint32_t)((int32_t)a<<20);d=P35S2(pm);pm=p1+(uint32_t)((int32_t)a<<20);e=P35S3(pm);acc[n]=(int16_t)((b+d+e)>>2);}break;
    case 6:for(n=0;n<samples;n++){uint32_t pm;int a,b,d,e;p0+=ia;a=P35S0(p0);d=P35S2(p0);p1+=ib;pm=p1+(uint32_t)((int32_t)a<<20);b=P35S1(pm);e=P35S3(p1);acc[n]=(int16_t)((b+d+e)>>2);}break;
    default:for(n=0;n<samples;n++){int a,b,d,e;p0+=ia;a=P35S0(p0);d=P35S2(p0);p1+=ib;b=P35S1(p1);e=P35S3(p1);acc[n]=(int16_t)((a+b+d+e)>>2);}break;
    }
    c->op[0].phase=p0;c->op[2].phase=p0;c->op[1].phase=p1;c->op[3].phase=p1;
    if(samples){uint32_t prevp=p0-ia;c->fb_prev[0]=(int16_t)P35S0(p0);c->fb_prev[1]=(int16_t)P35S0(prevp);}
#undef P35S0
#undef P35S1
#undef P35S2
#undef P35S3
}

/* Phase 35: fuse the last stable algorithm-7 channel with final mixdown.
   Phase 34 removed the accumulator clear and first-channel read/modify/write,
   but the six-FM hardware run still spent a complete extra pass writing the
   sixth channel back to the 16-bit accumulator and then rereading that buffer
   only to halve/clamp/add PSG into the 8-bit destination.  Algorithm 7 is the
   cheapest no-feedback carrier topology and is common in real patches.  When
   one is present, keep it until last and combine its exact channel result with
   the existing accumulator directly into dst.  This removes one IWRAM store,
   one IWRAM reload, and the separate final mixdown loop per sample without
   changing phases, feedback history, rounding, clipping, or PCM bytes. */
static MD2GBA_YM_SELECTED __attribute__((noinline)) void render_stable_channel_finish_alg7_nofb_pairinc(
    Md2GbaYm2612Pcm *y,Md2GbaYmChannel *c,const int16_t *acc,int8_t *dst,size_t samples,unsigned add){
    uint32_t p0=c->op[0].phase,p1=c->op[1].phase,p2=c->op[2].phase,p3=c->op[3].phase;
    const uint32_t ia=c->op[0].phase_inc,ib=c->op[1].phase_inc;
    const int8_t *wv[4]; size_t n;
    prepare_wave_cache(y,c,wv);
#define P35W0(pm) ((int)wv[0][((pm)>>24)])
#define P35W1(pm) ((int)wv[1][((pm)>>24)])
#define P35W2(pm) ((int)wv[2][((pm)>>24)])
#define P35W3(pm) ((int)wv[3][((pm)>>24)])
    for(n=0;n<samples;n++){
        int a,b,d,e,v;
        p0+=ia;a=P35W0(p0);p1+=ib;b=P35W1(p1);
        p2+=ia;d=P35W2(p2);p3+=ib;e=P35W3(p3);
        v=(int)acc[n]+((a+b+d+e)>>2);
        v>>=1;
        if(v>127)v=127;else if(v<-128)v=-128;
        if(add){v+=(int)dst[n];if(v>127)v=127;else if(v<-128)v=-128;}
        dst[n]=(int8_t)v;
    }
    c->op[0].phase=p0;c->op[1].phase=p1;c->op[2].phase=p2;c->op[3].phase=p3;
    if(samples){uint32_t prevp=p0-ia;c->fb_prev[0]=(int16_t)P35W0(p0);c->fb_prev[1]=(int16_t)P35W0(prevp);}
#undef P35W0
#undef P35W1
#undef P35W2
#undef P35W3
}


/* Phase 37: pair-phase form of the fused algorithm-7 finisher.  This is the
   non-unrolled version: P36's manual two-sample unroll increased ARM register
   pressure/code size and regressed the real target. */
static MD2GBA_YM_SELECTED __attribute__((noinline)) void render_stable_channel_finish_alg7_nofb_pairphase(
    Md2GbaYm2612Pcm *y,Md2GbaYmChannel *c,const int16_t *acc,int8_t *dst,size_t samples,unsigned add){
    uint32_t p0=c->op[0].phase,p1=c->op[1].phase;
    const uint32_t ia=c->op[0].phase_inc,ib=c->op[1].phase_inc;
    const int8_t *wv[4]; size_t n;
    prepare_wave_cache(y,c,wv);
#define P37W0(pm) ((int)wv[0][((pm)>>24)])
#define P37W1(pm) ((int)wv[1][((pm)>>24)])
#define P37W2(pm) ((int)wv[2][((pm)>>24)])
#define P37W3(pm) ((int)wv[3][((pm)>>24)])
    if(add){
        for(n=0;n<samples;n++){
            int a,b,d,e,v;
            p0+=ia;a=P37W0(p0);d=P37W2(p0);
            p1+=ib;b=P37W1(p1);e=P37W3(p1);
            v=(int)acc[n]+((a+b+d+e)>>2);
            v>>=1;
            if(v>127)v=127;else if(v<-128)v=-128;
            v+=(int)dst[n];if(v>127)v=127;else if(v<-128)v=-128;
            dst[n]=(int8_t)v;
        }
    }else{
        for(n=0;n<samples;n++){
            int a,b,d,e,v;
            p0+=ia;a=P37W0(p0);d=P37W2(p0);
            p1+=ib;b=P37W1(p1);e=P37W3(p1);
            v=(int)acc[n]+((a+b+d+e)>>2);
            v>>=1;
            if(v>127)v=127;else if(v<-128)v=-128;
            dst[n]=(int8_t)v;
        }
    }
    c->op[0].phase=p0;c->op[2].phase=p0;c->op[1].phase=p1;c->op[3].phase=p1;
    if(samples){uint32_t prevp=p0-ia;c->fb_prev[0]=(int16_t)P37W0(p0);c->fb_prev[1]=(int16_t)P37W0(prevp);}
#undef P37W0
#undef P37W1
#undef P37W2
#undef P37W3
}

static __attribute__((always_inline)) inline unsigned nofb_pairinc_ready(const Md2GbaYmChannel *c){
    return c->feedback==0u && c->op[0].phase_inc==c->op[2].phase_inc && c->op[1].phase_inc==c->op[3].phase_inc;
}
static __attribute__((always_inline)) inline unsigned nofb_pairphase_ready(const Md2GbaYmChannel *c){
    return nofb_pairinc_ready(c) && c->op[0].phase==c->op[2].phase && c->op[1].phase==c->op[3].phase;
}

static __attribute__((always_inline)) inline unsigned stable_mask_ready(const Md2GbaYm2612Pcm *y,uint8_t mask){
    unsigned c; if(!mask || y->dac_enable)return 0u;
    for(c=0u;c<6u;c++)if((mask&(1u<<c)) && !channel_stable(&y->ch[c]))return 0u;
    return 1u;
}

static MD2GBA_YM_HOT __attribute__((noinline)) void render_stable_mask(Md2GbaYm2612Pcm *y,int8_t *dst,size_t samples,unsigned add,uint8_t mask){
    enum { CHUNK=256 }; int16_t acc[CHUNK]; size_t done=0u;
    while(done<samples){
        size_t n=(samples-done>CHUNK)?CHUNK:samples-done,i; unsigned c,first=6u,finish=6u;
        /* Prefer a no-feedback paired-increment algorithm-7 voice as the final
           channel so Phase 35 can fold the last accumulation into mixdown. */
        for(c=0u;c<6u;c++)if((mask&(uint8_t)(1u<<c)) && nofb_pairinc_ready(&y->ch[c]) &&
                              (y->ch[c].algorithm&7u)==7u){finish=c;break;}
        for(c=0u;c<6u;c++)if(c!=finish && (mask&(uint8_t)(1u<<c)) && nofb_pairinc_ready(&y->ch[c])){first=c;break;}
        if(first<6u){
            if(nofb_pairphase_ready(&y->ch[first]))render_stable_channel_store_nofb_pairphase(y,&y->ch[first],acc,n);
            else render_stable_channel_store_nofb_pairinc(y,&y->ch[first],acc,n);
        }else{
            for(i=0u;i<n;i++)acc[i]=0;
        }
        for(c=0u;c<6u;c++)if((mask&(uint8_t)(1u<<c)) && c!=first && c!=finish){
            Md2GbaYmChannel *ch=&y->ch[c];
            if(nofb_pairphase_ready(ch))render_stable_channel_accum_nofb_pairphase(y,ch,acc,n);
            else if(nofb_pairinc_ready(ch))render_stable_channel_accum_nofb_pairinc(y,ch,acc,n);
            else if(ch->feedback==0u)render_stable_channel_accum_nofb(y,ch,acc,n);
            else render_stable_channel_accum(y,ch,acc,n);
        }
        if(finish<6u && first<6u){
            { Md2GbaYmChannel *ch=&y->ch[finish];
            if(nofb_pairphase_ready(ch))render_stable_channel_finish_alg7_nofb_pairphase(y,ch,acc,dst+done,n,add);
            else render_stable_channel_finish_alg7_nofb_pairinc(y,ch,acc,dst+done,n,add); }
        }else{
            if(finish<6u){
                Md2GbaYmChannel *ch=&y->ch[finish];
                if(nofb_pairinc_ready(ch))render_stable_channel_accum_nofb_pairinc(y,ch,acc,n);
                else if(ch->feedback==0u)render_stable_channel_accum_nofb(y,ch,acc,n);
                else render_stable_channel_accum(y,ch,acc,n);
            }
            for(i=0u;i<n;i++){
                int v=(int)acc[i]; v>>=1; if(v>127)v=127;else if(v<-128)v=-128;
                if(add){v+=(int)dst[done+i];if(v>127)v=127;else if(v<-128)v=-128;}
                dst[done+i]=(int8_t)v;
            }
        }
        done+=n;
    }
}

static MD2GBA_YM_HOT __attribute__((noinline)) int render_channel(Md2GbaYmChannel *c){
    int a,b,d,e,out=0,fb=0; unsigned carriers=1u;
    if(!(c->op[0].key_on|c->op[1].key_on|c->op[2].key_on|c->op[3].key_on) &&
       c->op[0].env_state==MD2GBA_YM_ENV_OFF&&c->op[1].env_state==MD2GBA_YM_ENV_OFF&&
       c->op[2].env_state==MD2GBA_YM_ENV_OFF&&c->op[3].env_state==MD2GBA_YM_ENV_OFF)return 0;
    if(c->feedback)fb=((int)c->fb_prev[0]+(int)c->fb_prev[1])>>(8u-c->feedback);
    a=op_sample(&c->op[0],fb); c->fb_prev[1]=c->fb_prev[0]; c->fb_prev[0]=(int16_t)a;
    switch(c->algorithm&7u){
    case 0: d=op_sample(&c->op[2],a); b=op_sample(&c->op[1],d); e=op_sample(&c->op[3],b); out=e; break;
    case 1: d=op_sample(&c->op[2],0); b=op_sample(&c->op[1],a+d); e=op_sample(&c->op[3],b); out=e; break;
    case 2: d=op_sample(&c->op[2],0); b=op_sample(&c->op[1],d); e=op_sample(&c->op[3],a+b); out=e; break;
    case 3: d=op_sample(&c->op[2],a); b=op_sample(&c->op[1],0); e=op_sample(&c->op[3],d+b); out=e; break;
    case 4: b=op_sample(&c->op[1],a); d=op_sample(&c->op[2],0); e=op_sample(&c->op[3],d); out=b+e; carriers=2u; break;
    case 5: b=op_sample(&c->op[1],a); d=op_sample(&c->op[2],a); e=op_sample(&c->op[3],a); out=b+d+e; carriers=3u; break;
    case 6: b=op_sample(&c->op[1],a); d=op_sample(&c->op[2],0); e=op_sample(&c->op[3],0); out=b+d+e; carriers=3u; break;
    default: b=op_sample(&c->op[1],0); d=op_sample(&c->op[2],0); e=op_sample(&c->op[3],0); out=a+b+d+e; carriers=4u; break;
    }
    if(carriers==2u)out>>=1; else if(carriers>=3u)out>>=2;
    return out;
}

void md2gba_ym2612_pcm_init(Md2GbaYm2612Pcm *y,uint32_t input_clock_hz,uint32_t sample_rate_hz){
    unsigned c,o; zero_bytes(y,sizeof(*y)); y->input_clock_hz=input_clock_hz?input_clock_hz:MD2GBA_YM2612_NTSC_CLOCK;
    y->sample_rate_hz=sample_rate_hz?sample_rate_hz:MD2GBA_YM2612_PCM_RATE; y->clock_div_144=udiv32(y->input_clock_hz,144u); y->dac_data=0x80u;
    for(c=0;c<17u;c++)y->wave_gain[c]=0xFFu;
    for(c=0;c<6u;c++)for(o=0;o<4u;o++){y->ch[c].op[o].env=1023u;y->ch[c].op[o].env_state=MD2GBA_YM_ENV_OFF;y->ch[c].op[o].gain=0u;}
}

void md2gba_ym2612_pcm_write(void *user,uint8_t port,uint8_t reg,uint8_t value){
    Md2GbaYm2612Pcm *y=(Md2GbaYm2612Pcm*)user; unsigned c,o; Md2GbaYmOperator *op;
    y->register_writes++; port&=1u;
    if(port==0u){
        if(reg==0x22u){y->lfo=value;return;}
        if(reg==0x27u){y->timer_control=value;return;}
        if(reg==0x2Au){y->dac_data=value;return;}
        if(reg==0x2Bu){y->dac_enable=(uint8_t)((value&0x80u)?1u:0u);return;}
        if(reg==0x28u){c=key_channel(value);if(c<6u)for(o=0;o<4u;o++)key_set(&y->ch[c].op[o],(value>>(4u+o))&1u);return;}
    }
    if(reg>=0x30u && reg<=0x9Fu){
        c=reg_channel(port,reg); if(c>=6u)return; o=op_index(reg); op=&y->ch[c].op[o];
        switch(reg&0xF0u){
        case 0x30u:op->dt_mul=value;update_incs(y,c);break; case 0x40u:op->tl=value&0x7Fu;op->gain=operator_gain(op);break;
        case 0x50u:op->rs_ar=value;break; case 0x60u:op->am_d1r=value;break; case 0x70u:op->d2r=value&31u;break;
        case 0x80u:op->sl_rr=value;break; case 0x90u:op->ssg_eg=value&15u;break; default:break;
        } return;
    }
    c=reg_channel(port,reg);
    if(c<6u){
        if((reg&0xFCu)==0xA0u){y->ch[c].fnum=(uint16_t)((y->ch[c].fnum&0x0700u)|value);update_incs(y,c);return;}
        if((reg&0xFCu)==0xA4u){y->ch[c].fnum=(uint16_t)((y->ch[c].fnum&0x00FFu)|((uint16_t)(value&7u)<<8));y->ch[c].block=(uint8_t)((value>>3)&7u);update_incs(y,c);return;}
        if((reg&0xFCu)==0xB0u){y->ch[c].algorithm=value&7u;y->ch[c].feedback=(value>>3)&7u;return;}
        if((reg&0xFCu)==0xB4u){
            y->ch[c].pan_l=(value>>7)&1u;
            y->ch[c].pan_r=(value>>6)&1u;
            if(y->ch[c].pan_l||y->ch[c].pan_r)y->pan_mask=(uint8_t)(y->pan_mask|(uint8_t)(1u<<c));
            else y->pan_mask=(uint8_t)(y->pan_mask&~(uint8_t)(1u<<c));
            return;
        }
    }
}
uint8_t md2gba_ym2612_pcm_status(void *user){return ((Md2GbaYm2612Pcm*)user)->status;}

static MD2GBA_YM_SELECTED __attribute__((noinline)) void render_core(Md2GbaYm2612Pcm *y,int8_t *dst,size_t samples,unsigned add){
    size_t i; unsigned c; uint8_t mask=y->pan_mask;
    if(mask && (mask&(uint8_t)(mask-1u))==0u && !(y->dac_enable && (mask&0x20u))){
        Md2GbaYmChannel *ch=&y->ch[(mask&1u)?0u:(mask&2u)?1u:(mask&4u)?2u:(mask&8u)?3u:(mask&16u)?4u:5u];
        /* Key-on can enter this call in ATTACK/DECAY.  Advance the exact old
           path only until the channel reaches the steady sustain state, then
           use the block-local Phase-27 kernel for the remainder. */
        while(samples && !alg4_stable(ch)){
            int v=render_channel(ch)>>1;
            if(v>127)v=127;else if(v<-128)v=-128;
            if(add){v+=(int)*dst;if(v>127)v=127;else if(v<-128)v=-128;}
            *dst++=(int8_t)v;samples--;y->rendered_samples++;
        }
        if(samples && alg4_stable(ch)){
            render_alg4_stable(y,ch,dst,samples,add);
            y->rendered_samples+=(uint32_t)samples;
            return;
        }
    }else if(stable_mask_ready(y,mask)){
        render_stable_mask(y,dst,samples,add,mask);
    }else{
        for(i=0;i<samples;i++){
            int v=0; uint8_t m=mask;
            for(c=0;c<6u;c++,m>>=1){
                if(!(m&1u))continue;
                if(c==5u && y->dac_enable){v+=(int)y->dac_data-128;continue;}
                v+=render_channel(&y->ch[c]);
            }
            v>>=1;if(v>127)v=127;else if(v<-128)v=-128;
            if(add){v+=(int)dst[i];if(v>127)v=127;else if(v<-128)v=-128;}
            dst[i]=(int8_t)v;
        }
    }
    y->rendered_samples+=(uint32_t)samples;
}

MD2GBA_YM_SELECTED void md2gba_ym2612_pcm_render(Md2GbaYm2612Pcm *y,int8_t *dst,size_t samples){render_core(y,dst,samples,0u);}
MD2GBA_YM_SELECTED void md2gba_ym2612_pcm_mix_into(Md2GbaYm2612Pcm *y,int8_t *dst,size_t samples){render_core(y,dst,samples,1u);}
#undef MD2GBA_YM_HOT
#undef MD2GBA_YM_SELECTED
#undef MD2GBA_YM_HOT_DATA

Md2GbaAudioSink md2gba_ym2612_pcm_sink(Md2GbaYm2612Pcm *y){
    Md2GbaAudioSink s; s.user=y;s.ym2612_write=md2gba_ym2612_pcm_write;s.ym2612_status=md2gba_ym2612_pcm_status;s.psg_write=0;s.z80_ram_write=0;return s;
}
