#include <stdio.h>
#include <stdint.h>
#include "md2gba_interp68k.h"
int main(void){
  uint8_t rom[8]={0x4e,0x71,0x4e,0x71,0x4e,0x71,0x4e,0x71};
  Md2GbaBus b; Md2Gba68k c={0};
  md2gba_bus_init(&b,rom,sizeof rom);
  c.pc=1u;
  int r=md68k_interp_step(&c,&b);
  if(r!=MD68K_INTERP_FAULT){printf("FAIL odd PC result=%d pc=%06x\n",r,c.pc);return 1;}
  if(c.pc!=1u){printf("FAIL odd PC mutated pc=%06x\n",c.pc);return 2;}
  c.pc=0u;
  r=md68k_interp_step(&c,&b);
  if(r!=MD68K_INTERP_OK||c.pc!=2u){printf("FAIL even PC result=%d pc=%06x\n",r,c.pc);return 3;}
  puts("PASS 68000 instruction fetch alignment: odd PC faults, even PC executes");
  return 0;
}
