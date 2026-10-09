#ifndef MD2GBA_AUDIO_H
#define MD2GBA_AUDIO_H
#include <stddef.h>
#include <stdint.h>

/* Phase 14: title-agnostic Genesis audio bus seam.
   YM2612, PSG and Z80-facing writes are converted into semantic events for a
   pluggable backend. This stays header-inline so the existing MD2GBA bus can
   retain its single-object link contract in small generated probes. */
typedef struct Md2GbaAudioSink {
    void *user;
    void (*ym2612_write)(void *user, uint8_t port, uint8_t reg, uint8_t value);
    uint8_t (*ym2612_status)(void *user);
    void (*psg_write)(void *user, uint8_t value);
    void (*z80_ram_write)(void *user, uint16_t offset, uint8_t value);
} Md2GbaAudioSink;

typedef struct Md2GbaAudio {
    Md2GbaAudioSink sink;
    uint8_t ym_address[2];
    uint8_t last_psg_write;
    uint32_t ym_address_writes;
    uint32_t ym_data_writes;
    uint32_t psg_writes;
    uint32_t z80_ram_writes;
} Md2GbaAudio;

#if defined(__GNUC__) || defined(__clang__)
#define MD2GBA_AUDIO_INLINE static __attribute__((always_inline)) inline
#else
#define MD2GBA_AUDIO_INLINE static inline
#endif

MD2GBA_AUDIO_INLINE void md2gba_audio_zero(void *ptr, size_t count) {
    uint8_t *p=(uint8_t*)ptr; while(count--) *p++=0;
}
MD2GBA_AUDIO_INLINE void md2gba_audio_init(Md2GbaAudio *audio) {
    md2gba_audio_zero(audio,sizeof(*audio));
}
MD2GBA_AUDIO_INLINE void md2gba_audio_set_sink(Md2GbaAudio *audio, const Md2GbaAudioSink *sink) {
    if(sink) audio->sink=*sink; else md2gba_audio_zero(&audio->sink,sizeof(audio->sink));
}
MD2GBA_AUDIO_INLINE void md2gba_audio_write_ym_port(Md2GbaAudio *audio, unsigned port_byte, uint8_t value) {
    unsigned bank=(port_byte>>1)&1u;
    if((port_byte&1u)==0u) { audio->ym_address[bank]=value; audio->ym_address_writes++; return; }
    audio->ym_data_writes++;
    if(audio->sink.ym2612_write) audio->sink.ym2612_write(audio->sink.user,(uint8_t)bank,audio->ym_address[bank],value);
}
MD2GBA_AUDIO_INLINE uint8_t md2gba_audio_ym_status(Md2GbaAudio *audio) {
    return audio->sink.ym2612_status ? audio->sink.ym2612_status(audio->sink.user) : 0u;
}
MD2GBA_AUDIO_INLINE void md2gba_audio_write_psg(Md2GbaAudio *audio, uint8_t value) {
    audio->last_psg_write=value; audio->psg_writes++;
    if(audio->sink.psg_write) audio->sink.psg_write(audio->sink.user,value);
}
MD2GBA_AUDIO_INLINE void md2gba_audio_write_z80_ram(Md2GbaAudio *audio, uint16_t offset, uint8_t value) {
    audio->z80_ram_writes++;
    if(audio->sink.z80_ram_write) audio->sink.z80_ram_write(audio->sink.user,offset,value);
}
#undef MD2GBA_AUDIO_INLINE
#endif
