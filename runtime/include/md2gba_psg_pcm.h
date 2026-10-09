#ifndef MD2GBA_PSG_PCM_H
#define MD2GBA_PSG_PCM_H
#include <stddef.h>
#include <stdint.h>
#include "md2gba_audio.h"
#include "md2gba_psg.h"

/* Phase 16: cycle-faithful digital SN76489/Sega PSG PCM core.
   Phase 24 keeps the same sample-exact model but advances tone/noise counters
   arithmetically across each output-sample tick span for ARM7 throughput.
   The generator clocks the same /16 tone/noise counters used by the chip,
   uses the Genesis 16-bit noise LFSR (white taps 0+3), and applies the
   documented 2 dB attenuation ladder.  It intentionally models the digital
   generator, not console-specific analogue filtering/clipping. */
#define MD2GBA_PSG_NTSC_CLOCK 3579545u
#define MD2GBA_PSG_PAL_CLOCK  3546893u
#define MD2GBA_PSG_PCM_DEFAULT_RATE 32768u

typedef struct Md2GbaPsgPcm {
    Md2GbaPsg psg;
    uint32_t input_clock_hz;
    uint32_t sample_rate_hz;
    /* Precomputed /16 PSG ticks per output sample.  Phase 24 keeps the
       quotient/remainder here so cooperative refills do not repeat a 32-bit
       divide setup for every small render slice. */
    uint32_t tick_threshold;
    uint32_t tick_step_base;
    uint32_t tick_step_remainder;
    uint32_t clock_accum;
    uint16_t tone_counter[3];
    uint8_t tone_output[3];
    uint16_t noise_counter;
    uint16_t noise_lfsr;
    uint8_t noise_flip;
    uint32_t rendered_samples;
    uint32_t internal_ticks;
} Md2GbaPsgPcm;

void md2gba_psg_pcm_init(Md2GbaPsgPcm *p, uint32_t input_clock_hz, uint32_t sample_rate_hz);
void md2gba_psg_pcm_write(void *user, uint8_t value);
void md2gba_psg_pcm_render(Md2GbaPsgPcm *p, int8_t *dst, size_t samples);
/* P83 GBA legacy-APU assist: software residual for SN tone 2 + Sega noise. */
void md2gba_psg_pcm_render_tone2_noise(Md2GbaPsgPcm *p, int8_t *dst, size_t samples);
/* Optional P83 three-tone legacy assist: exact Sega noise only in PCM. */
void md2gba_psg_pcm_render_noise_only(Md2GbaPsgPcm *p, int8_t *dst, size_t samples);
Md2GbaAudioSink md2gba_psg_pcm_sink(Md2GbaPsgPcm *p);

#endif
