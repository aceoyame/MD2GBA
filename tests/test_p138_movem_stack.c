#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "md2gba_aot68k.h"

static uint32_t rng=0x13579bdfu;
static uint32_t rnd(void){rng=rng*1664525u+1013904223u;return rng;}
static void seed_cpu(Md2Gba68k*c){unsigned i;md68k_reset_context(c);for(i=0;i<8;i++){c->d[i]=rnd();c->a[i]=rnd();}c->sr=(uint16_t)rnd();c->pc=rnd();}
static void seed_mem(Md2GbaBus*b){unsigned i;for(i=0;i<0x10000;i++)b->wram[i]=(uint8_t)rnd();}
static int same_cpu(const Md2Gba68k*a,const Md2Gba68k*b){return memcmp(a,b,sizeof(*a))==0;}
int main(void){
 static Md2GbaBus b1,b2; static Md2Gba68k c1,c2; unsigned it;
 md2gba_bus_init(&b1,0,0);md2gba_bus_init(&b2,0,0);
 for(it=0;it<20000u;it++){
   uint16_t mask=(uint16_t)rnd();seed_cpu(&c1);c1.a[7]=0xFFFF00u+(rnd()&0x7cu);c2=c1;seed_mem(&b1);memcpy(b2.wram,b1.wram,sizeof b1.wram);
   md68k_movem_store(&c1,&b1,mask,MD68K_SZ_L,MD68K_EA_AN_PREDEC,7,0u,0,0u,0x123456u);
   md68k_movem_store_a7_l(&c2,&b2,mask);
   if(!same_cpu(&c1,&c2)||memcmp(b1.wram,b2.wram,sizeof b1.wram)){printf("FAIL store it=%u mask=%04x a7=%08x/%08x\n",it,mask,c1.a[7],c2.a[7]);return 1;}
   seed_cpu(&c1);c1.a[7]=0xFF0100u+(rnd()&0x7cu);c2=c1;seed_mem(&b1);memcpy(b2.wram,b1.wram,sizeof b1.wram);
   md68k_movem_load(&c1,&b1,mask,MD68K_SZ_L,MD68K_EA_AN_POSTINC,7,0u,0,0u,0x654321u);
   md68k_movem_load_a7_l(&c2,&b2,mask);
   if(!same_cpu(&c1,&c2)||memcmp(b1.wram,b2.wram,sizeof b1.wram)){printf("FAIL load it=%u mask=%04x a7=%08x/%08x\n",it,mask,c1.a[7],c2.a[7]);return 2;}
 }
 puts("PASS P138 A7 long MOVEM specialization: 20,000 randomized store + 20,000 load states");return 0;
}
