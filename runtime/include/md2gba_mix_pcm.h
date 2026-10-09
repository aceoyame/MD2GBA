#ifndef MD2GBA_MIX_PCM_H
#define MD2GBA_MIX_PCM_H
#include <stddef.h>
#include <stdint.h>
#include "md2gba_audio.h"
#include "md2gba_psg_pcm.h"
#include "md2gba_ym2612_pcm.h"
#if defined(MD2GBA_GBA_LEGACY_PSG)
#include "md2gba_gba_legacy_psg.h"
#endif

typedef struct Md2GbaMixPcm {
    Md2GbaPsgPcm psg;
    Md2GbaYm2612Pcm ym;
#if defined(MD2GBA_GBA_LEGACY_PSG)
    Md2GbaGbaLegacyPsg legacy_psg;
#endif
} Md2GbaMixPcm;

void md2gba_mix_pcm_init(Md2GbaMixPcm *m,uint32_t psg_clock_hz,uint32_t ym_clock_hz,uint32_t sample_rate_hz);
void md2gba_mix_pcm_render(Md2GbaMixPcm *m,int8_t *dst,size_t samples);
Md2GbaAudioSink md2gba_mix_pcm_sink(Md2GbaMixPcm *m);
#if defined(MD2GBA_GBA_LEGACY_PSG)
void md2gba_mix_pcm_gba_legacy_start(Md2GbaMixPcm *m);
void md2gba_mix_pcm_gba_legacy_stop(Md2GbaMixPcm *m);
#endif
#endif
