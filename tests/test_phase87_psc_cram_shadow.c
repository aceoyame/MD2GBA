#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "md2gba_bus.h"
static uint16_t ref(uint16_t c){unsigned r=(c>>1)&7u,g=(c>>5)&7u,b=(c>>9)&7u;r=(r<<2)|(r>>1);g=(g<<2)|(g>>1);b=(b<<2)|(b>>1);return (uint16_t)(r|(g<<5)|(b<<10));}
int main(void){static uint8_t rom[64];Md2GbaBus b;md2gba_bus_init(&b,rom,sizeof rom);b.vdp.target=MD2GBA_VDP_CRAM;b.vdp.address=0;b.vdp.regs[15]=2;for(unsigned c=0;c<4096;c++){unsigned i=c&63u;b.vdp.address=(uint16_t)(i*2u);md2gba_vdp_data16_direct_vdp(&b.vdp,(uint16_t)c);if(b.vdp.cram[i]!=(uint16_t)c||b.vdp.cram_gba[i]!=ref((uint16_t)c)){printf("mismatch %u\n",c);return 1;}}puts("PASS PSC-style CRAM write-time GBA shadow: 4096 vectors");return 0;}
