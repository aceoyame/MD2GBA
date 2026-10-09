#ifndef MD2GBA_INTERP68K_H
#define MD2GBA_INTERP68K_H
#include <stdint.h>
#include "md2gba_aot_region.h"

enum {
    MD68K_INTERP_OK = 1,
    MD68K_INTERP_UNSUPPORTED = 0,
    MD68K_INTERP_FAULT = -1
};

typedef struct Md2GbaInterpStats {
    uint32_t steps;
    uint32_t unsupported;
    uint32_t last_pc;
    uint16_t last_opcode;
} Md2GbaInterpStats;

/* Execute exactly one raw-ROM 68000 instruction using the same CPU/bus helpers
   as AOT code. This is intentionally a cold path. */
int md68k_interp_step(Md2Gba68k *cpu, Md2GbaBus *bus);

/* Md2GbaAotHooks.fallback_pc-compatible adapter. */
int md68k_interp_fallback_hook(Md2Gba68k *cpu, Md2GbaBus *bus, uint32_t pc, void *user);
#endif
