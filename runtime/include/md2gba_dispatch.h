#ifndef MD2GBA_DISPATCH_H
#define MD2GBA_DISPATCH_H
#include <stdint.h>
#include "md2gba_bus.h"
#include "md2gba_cpu.h"
/* Temporary ABI seam for direct/unresolved 68K calls during bring-up. */
void md2gba_dispatch(Md2GbaM68kContext *ctx, Md2GbaBus *bus, uint32_t genesis_pc);
#endif
