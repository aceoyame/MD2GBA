#ifndef MD2GBA_CYCLES_H
#define MD2GBA_CYCLES_H
#include <stdint.h>
#include "md2gba_bus.h"
/* Approximate original 68000 core cycles for event scheduling. This is not a
   bus-cycle-perfect emulator; it exists to put H/V interrupts on the right
   scanline scale while AOT code runs much faster than a Genesis. */
uint16_t md68k_estimate_cycles(const Md2GbaBus *bus,uint32_t executed_pc,uint32_t next_pc);
#endif
