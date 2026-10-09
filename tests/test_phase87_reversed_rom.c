#include <stdint.h>
#include <stdio.h>
#include "md2gba_bus.h"
static uint32_t r=0x87654321u;static uint32_t rnd(void){r=r*1664525u+1013904223u;return r;}
static uint16_t be16(const uint8_t*p,unsigned a){return (uint16_t)(((uint16_t)p[a]<<8)|p[a+1]);}
static uint32_t be32(const uint8_t*p,unsigned a){return ((uint32_t)p[a]<<24)|((uint32_t)p[a+1]<<16)|((uint32_t)p[a+2]<<8)|p[a+3];}
int main(void){enum{N=0x20000};static uint8_t rom[N],rev[N];Md2GbaBus b;for(unsigned i=0;i<N;i++)rom[i]=(uint8_t)(i*37u+(i>>8));for(unsigned i=0;i<N;i++)rev[i]=rom[N-1u-i];md2gba_bus_init(&b,rom,N);md2gba_bus_set_reversed_rom(&b,rev);for(unsigned i=0;i<50000u;i++){unsigned a=(rnd()%(N-4u))&~1u;if(md2gba_read16(&b,a)!=be16(rom,a)){fprintf(stderr,"r16 %x\n",a);return 1;}if(md2gba_read32(&b,a)!=be32(rom,a)){fprintf(stderr,"r32 %x\n",a);return 2;}}puts("PASS P87 PSC-style reversed ROM wide reads: 100000 checks");return 0;}
