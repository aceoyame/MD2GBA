#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "md2gba_interp68k.h"
#include "md2gba_cycles.h"
#include "md2gba_profile_nem_native.h"
static uint32_t rng=0x1255A11u;
static uint32_t R(void){rng=rng*1664525u+1013904223u;return rng;}
static int cpu_eq(const Md2Gba68k*a,const Md2Gba68k*b){return memcmp(a,b,sizeof(*a))==0;}
int main(int argc,char**argv){
 if(argc<2)return 2; FILE*f=fopen(argv[1],"rb"); if(!f)return 2;
 uint8_t*rom=malloc(0x400000u); size_t n=fread(rom,1,0x400000u,f); fclose(f); if(n<0x300000u)return 2;
 Md2GbaBus base,ib,hb; Md2Gba68k pre,ic,hc; md2gba_bus_init(&base,rom,n);
 unsigned accepted=0,refills=0,specials=0,tries=0;
 while(accepted<100000u && tries++<300000u){
   memset(&pre,0,sizeof(pre));
   for(unsigned i=0;i<8;i++){pre.d[i]=R();pre.a[i]=R();}
   uint16_t d6=(uint16_t)(9u+(R()%8u)); uint16_t d5=(uint16_t)R();
   uint16_t d7=(uint16_t)(d6-8u); unsigned sh=d7&63u; uint16_t probe=sh>=16u?0u:(uint16_t)(d5>>sh); uint8_t sym=(uint8_t)probe;
   pre.d[5]=(pre.d[5]&0xFFFF0000u)|d5; pre.d[6]=(pre.d[6]&0xFFFF0000u)|d6;
   pre.a[0]=0x100000u+(R()%0x100000u); pre.a[1]=0xFFE200u; pre.pc=0x04178Au; pre.sr=(uint16_t)(0x2100u|(R()&31u));
   base.wram[(0xE200u + ((unsigned)sym<<1))&0xFFFFu]=(uint8_t)R();
   base.wram[(0xE201u + ((unsigned)sym<<1))&0xFFFFu]=(uint8_t)R();
   /* Use bus accessors to make the test independent of WRAM representation. */
   uint8_t len=(uint8_t)R(), code=(uint8_t)R();
   md2gba_write8(&base,0xFFE200u+((uint32_t)sym<<1),len);
   md2gba_write8(&base,0xFFE201u+((uint32_t)sym<<1),code);
   rom[pre.a[0]&0x3FFFFFu]=(uint8_t)R();
   ib=hb=base; ic=hc=pre;
   uint32_t cyc=0,steps=0;
   while(ic.pc!=0x0417C2u && steps<40u){uint32_t before=ic.pc;if(md68k_interp_step(&ic,&ib)!=1){fprintf(stderr,"interp fail %06x\n",before);return 3;}cyc+=md68k_estimate_cycles(&ib,before,ic.pc);steps++;}
   if(ic.pc!=0x0417C2u){continue;} /* escape token or otherwise not common path */
   uint32_t hcy=0,hst=0; int ok=md2gba_profile_nem_token_native(&hc,&hb,1000u,&hcy,&hst);
   if(!ok){fprintf(stderr,"helper refused normal sym %02x d6=%u\n",sym,d6);return 4;}
   hc.pc=ic.pc;
   if(hcy!=cyc||hst!=steps||!cpu_eq(&hc,&ic)){
     fprintf(stderr,"mismatch #%u sym=%02x d5=%04x d6=%u cyc %u/%u steps %u/%u pc %06x/%06x\n",accepted,sym,d5,d6,hcy,cyc,hst,steps,hc.pc,ic.pc);
     for(int i=0;i<8;i++)fprintf(stderr,"D%d %08x/%08x A%d %08x/%08x\n",i,hc.d[i],ic.d[i],i,hc.a[i],ic.a[i]);
     fprintf(stderr,"SR %04x/%04x\n",hc.sr,ic.sr); return 5;
   }
   if(memcmp(&hb.wram,&ib.wram,sizeof(hb.wram))||memcmp(&hb.vdp,&ib.vdp,sizeof(hb.vdp))){fprintf(stderr,"bus changed\n");return 6;}
   if(hcy==130u)refills++; if(sym>=0xFCu)specials++; accepted++;
 }
 if(accepted!=100000u)return 7;
 /* Deadline equality must stay canonical and not mutate CPU. */
 hb=base; hc=pre; Md2Gba68k before=hc; uint32_t c=0,s=0; int ok=md2gba_profile_nem_token_native(&hc,&hb,112u,&c,&s);
 if(ok||memcmp(&hc,&before,sizeof(hc))){fprintf(stderr,"deadline refusal failed\n");return 8;}
 printf("PASS P125 native Nemesis token: %u randomized canonical differentials (%u refill-cost, %u escape)\n",accepted,refills,specials);
 return 0;
}
