#ifndef MD2GBA_Z80_H
#define MD2GBA_Z80_H
#include <stdint.h>
#include "md2gba_bus.h"

typedef struct Md2GbaZ80 {
    uint16_t pc,sp,ix,iy;
    uint16_t bank;
    uint8_t a,f,b,c,d,e,h,l;
    uint8_t a2,f2,b2,c2,d2,e2,h2,l2;
    uint8_t i,r,iff1,iff2,im,halted,ei_delay,irq_pending;
    uint8_t reset_active;
    uint16_t idle_wait_pc,idle_wait_addr;
    uint8_t idle_wait_sense,idle_wait_valid;
    uint32_t executed,unsupported,last_pc;
    uint8_t last_opcode,last_prefix;
    uint32_t ym_writes,psg_writes,bank_writes;
} Md2GbaZ80;

void md2gba_z80_init(Md2GbaZ80 *z);
void md2gba_z80_reset(Md2GbaZ80 *z);
void md2gba_z80_raise_irq(Md2GbaZ80 *z);
unsigned md2gba_z80_run(Md2GbaZ80 *z,Md2GbaBus *bus,unsigned tstates);
#if defined(MD2GBA_Z80_PROFILE)
extern uint32_t md2gba_z80_profile[0x2000];
#endif
#endif
