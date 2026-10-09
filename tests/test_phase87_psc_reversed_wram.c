#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "md2gba_bus.h"
static uint32_t rng=0x8128a55au;
static uint32_t rnd(void){rng=rng*1664525u+1013904223u;return rng;}
static uint16_t be16(const uint8_t*m,unsigned o){return (uint16_t)(((uint16_t)m[o]<<8)|m[(o+1u)&0xffffu]);}
static uint32_t be32(const uint8_t*m,unsigned o){return ((uint32_t)m[o]<<24)|((uint32_t)m[o+1]<<16)|((uint32_t)m[o+2]<<8)|m[o+3];}
int main(void){
 static uint8_t rom[0x1000],model[0x10000]; static Md2GbaBus b;
 md2gba_bus_init(&b,rom,sizeof rom);
 for(unsigned i=0;i<0x10000;i++){model[i]=(uint8_t)rnd();md2gba_write8(&b,0xff0000u+i,model[i]);}
 for(unsigned n=0;n<200000;n++){
   unsigned o=rnd()&0xffffu; unsigned op=rnd()%6u; uint32_t v=rnd();
   if(op==0){if(md2gba_read8_fast(&b,0xff0000u+o)!=model[o]){printf("r8 %x\n",o);return 1;}}
   else if(op==1 && o<=0xfffeu){if(md2gba_read16(&b,0xff0000u+o)!=be16(model,o)){printf("r16 %x %04x %04x\n",o,md2gba_read16(&b,0xff0000u+o),be16(model,o));return 2;}}
   else if(op==2 && o<=0xfffcu){if(md2gba_read32(&b,0xff0000u+o)!=be32(model,o)){printf("r32 %x %08x %08x\n",o,md2gba_read32(&b,0xff0000u+o),be32(model,o));return 3;}}
   else if(op==3){md2gba_write8_fast(&b,0xff0000u+o,(uint8_t)v);model[o]=(uint8_t)v;}
   else if(op==4 && o<=0xfffeu){md2gba_write16(&b,0xff0000u+o,(uint16_t)v);model[o]=(uint8_t)(v>>8);model[o+1]=(uint8_t)v;}
   else if(op==5 && o<=0xfffcu){md2gba_write32(&b,0xff0000u+o,v);model[o]=(uint8_t)(v>>24);model[o+1]=(uint8_t)(v>>16);model[o+2]=(uint8_t)(v>>8);model[o+3]=(uint8_t)v;}
 }
 for(unsigned i=0;i<0x10000;i++)if(md2gba_read8_fast(&b,0xff0000u+i)!=model[i]){printf("final %x\n",i);return 4;}
 puts("PASS phase87 PSC descending WRAM: 200000 mixed accesses + full byte image");return 0;
}
