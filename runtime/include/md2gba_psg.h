#ifndef MD2GBA_PSG_H
#define MD2GBA_PSG_H
#include <stdint.h>

/* Title-agnostic SN76489-compatible PSG write decoder.
   Genesis software exposes the PSG as a serial byte stream at $C00011.  This
   helper turns that stream into stable channel state without assuming any
   game-specific driver, song format, or GBA backend. */
typedef struct Md2GbaPsg {
    uint16_t tone_period[3];      /* 10-bit divider, 1..1023 in normal use */
    uint8_t attenuation[4];       /* 0 loudest .. 15 mute */
    uint8_t noise_control;        /* bits 2: white/periodic, bits 1:0 rate */
    uint8_t latched_channel;
    uint8_t latched_volume;
    uint32_t writes;
} Md2GbaPsg;

#if defined(__GNUC__) || defined(__clang__)
#define MD2GBA_PSG_INLINE static __attribute__((always_inline)) inline
#else
#define MD2GBA_PSG_INLINE static inline
#endif

MD2GBA_PSG_INLINE void md2gba_psg_init(Md2GbaPsg *p) {
    unsigned i;
    p->tone_period[0]=p->tone_period[1]=p->tone_period[2]=0u;
    for(i=0;i<4u;i++) p->attenuation[i]=15u;
    p->noise_control=0u; p->latched_channel=0u; p->latched_volume=0u; p->writes=0u;
}

MD2GBA_PSG_INLINE void md2gba_psg_write(Md2GbaPsg *p, uint8_t value) {
    unsigned ch, vol;
    p->writes++;
    if(value & 0x80u) {
        ch=(value>>5)&3u; vol=(value>>4)&1u;
        p->latched_channel=(uint8_t)ch; p->latched_volume=(uint8_t)vol;
        if(vol) p->attenuation[ch]=(uint8_t)(value&0x0Fu);
        else if(ch<3u) {
            uint16_t n=(uint16_t)((p->tone_period[ch]&0x03F0u)|(value&0x0Fu));
            p->tone_period[ch]=n;
        } else p->noise_control=(uint8_t)(value&7u);
        return;
    }
    ch=p->latched_channel; vol=p->latched_volume;
    if(vol) p->attenuation[ch]=(uint8_t)(value&0x0Fu);
    else if(ch<3u) {
        uint16_t n=(uint16_t)(((uint16_t)(value&0x3Fu)<<4)|(p->tone_period[ch]&0x000Fu));
        p->tone_period[ch]=n;
    } else p->noise_control=(uint8_t)(value&7u);
}

#undef MD2GBA_PSG_INLINE
#endif
