#ifndef MD2GBA_GBA_LEGACY_PSG_H
#define MD2GBA_GBA_LEGACY_PSG_H
#include <stdint.h>
#include "md2gba_psg.h"

/* GBA-mode access to the legacy DMG/CGB sound block.  P83 always uses the
   two square channels for SN76489 tone 0/1.  With
   MD2GBA_GBA_LEGACY_PSG_TONE3 it also uses GBA's banked wave channel for
   tone 2.  By default Sega noise remains software because GB channel 4's
   7/15-bit polynomial cannot reproduce the Genesis integrated PSG's 16-bit
   LFSR.  MD2GBA_GBA_LEGACY_PSG_NOISE4 enables an explicitly approximate,
   precomputed channel-4 translation for performance A/B testing. */
typedef struct Md2GbaGbaLegacyPsg {
    uint16_t period[3];
    uint8_t attenuation[3];
    uint8_t active[3];
    uint8_t started;
    uint8_t output_ratio; /* SOUNDCNT_H old-sound ratio: 0=25,1=50,2=100%. */
    uint8_t wave_bank;
    uint8_t noise_control;
    uint8_t noise_attenuation;
    uint8_t noise_rate;
} Md2GbaGbaLegacyPsg;

uint16_t md2gba_gba_legacy_psg_freq_reg(uint32_t psg_clock_hz, uint16_t sn_period);
uint8_t md2gba_gba_legacy_psg_volume(uint8_t sn_attenuation);

void md2gba_gba_legacy_psg_init(Md2GbaGbaLegacyPsg *g);
void md2gba_gba_legacy_psg_start(Md2GbaGbaLegacyPsg *g, const Md2GbaPsg *psg, uint32_t psg_clock_hz);
void md2gba_gba_legacy_psg_sync(Md2GbaGbaLegacyPsg *g, const Md2GbaPsg *psg, uint32_t psg_clock_hz);
void md2gba_gba_legacy_psg_sync_write(Md2GbaGbaLegacyPsg *g, const Md2GbaPsg *psg, uint32_t psg_clock_hz, uint8_t raw_write);
void md2gba_gba_legacy_psg_stop(Md2GbaGbaLegacyPsg *g);

#endif
