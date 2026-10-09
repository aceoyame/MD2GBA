#ifndef MD2GBA_YM2612_PCM_H
#define MD2GBA_YM2612_PCM_H
#include <stddef.h>
#include <stdint.h>
#include "md2gba_audio.h"

/* Phase 25-31: title-agnostic YM2612/OPN2 PCM synthesis foundation.

   This is deliberately a register-driven native GBA core rather than a VGM
   player or title-specific music converter.  It implements the normal six FM
   channels, four operators per channel, all eight algorithms, operator
   feedback, FNUM/block pitch, MUL + coarse DT1, TL, ADSR-style OPN envelope
   stages, key-on/off, stereo-enable collapse to mono, and the channel-6 DAC
   replacement path.

   The register model follows the YM2612 application manual.  Envelope timing,
   detune magnitude, LFO, SSG-EG and channel-3 special-frequency mode are not
   yet transistor/cycle exact; these remain explicit next-step fidelity work.
   The renderer is intended for the MD2GBA 32768 Hz PCM transport established
   in Phases 16-24. */

#define MD2GBA_YM2612_NTSC_CLOCK 7670454u
#define MD2GBA_YM2612_PAL_CLOCK  7600489u
#define MD2GBA_YM2612_PCM_RATE    32768u

typedef enum Md2GbaYmEnvState {
    MD2GBA_YM_ENV_OFF=0,
    MD2GBA_YM_ENV_ATTACK=1,
    MD2GBA_YM_ENV_DECAY=2,
    MD2GBA_YM_ENV_SUSTAIN=3,
    MD2GBA_YM_ENV_RELEASE=4
} Md2GbaYmEnvState;

typedef struct Md2GbaYmOperator {
    uint32_t phase;
    uint32_t phase_inc;
    uint16_t env;          /* 0 loud .. 1023 silent */
    uint8_t dt_mul;
    uint8_t tl;
    uint8_t rs_ar;
    uint8_t am_d1r;
    uint8_t d2r;
    uint8_t sl_rr;
    uint8_t ssg_eg;
    uint8_t key_on;
    uint8_t env_state;
    uint8_t gain;          /* cached TL+envelope attenuation, Phase 26 hot path */
    uint8_t reserved0;
} Md2GbaYmOperator;

typedef struct Md2GbaYmChannel {
    Md2GbaYmOperator op[4];
    uint16_t fnum;
    uint8_t block;
    uint8_t algorithm;
    uint8_t feedback;
    uint8_t pan_l;
    uint8_t pan_r;
    int16_t fb_prev[2];
} Md2GbaYmChannel;

typedef struct Md2GbaYm2612Pcm {
    Md2GbaYmChannel ch[6];
    uint32_t input_clock_hz;
    uint32_t sample_rate_hz;
    uint32_t clock_div_144;
    uint8_t lfo;
    uint8_t timer_control;
    uint8_t dac_data;
    uint8_t dac_enable;
    uint8_t status;
    uint8_t pan_mask;      /* bit per channel: audible L/R output enabled */
    uint8_t reserved0[2];
    /* P84: title-agnostic shared stable-wave cache.  Phase 28 kept four
       pre-scaled sine tables per FM channel (6 KiB total).  Stable operator
       gain is only an 8-bit key, so channels can safely reuse identical
       sine*gain tables.  Seventeen 256-byte slots retain the multiply-free
       hot kernels while reclaiming 1,820 bytes of EWRAM. */
    int8_t wave_cache[17][256];
    uint8_t wave_gain[17];       /* 0xFF = unused slot */
    uint8_t wave_next;
    uint8_t reserved_wave[2];
    uint32_t register_writes;
    uint32_t rendered_samples;
} Md2GbaYm2612Pcm;

void md2gba_ym2612_pcm_init(Md2GbaYm2612Pcm *y, uint32_t input_clock_hz, uint32_t sample_rate_hz);
void md2gba_ym2612_pcm_write(void *user, uint8_t port, uint8_t reg, uint8_t value);
uint8_t md2gba_ym2612_pcm_status(void *user);
void md2gba_ym2612_pcm_render(Md2GbaYm2612Pcm *y, int8_t *dst, size_t samples);
/* Phase 26+ fast mixer entry: add the same clamped YM sample stream into an
   existing signed-8 PCM buffer, preserving Phase-25 mix semantics without a
   second temporary array/copy pass. */
void md2gba_ym2612_pcm_mix_into(Md2GbaYm2612Pcm *y, int8_t *dst, size_t samples);
Md2GbaAudioSink md2gba_ym2612_pcm_sink(Md2GbaYm2612Pcm *y);

#endif
