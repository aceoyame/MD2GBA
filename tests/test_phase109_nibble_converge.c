#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "md2gba_interp68k.h"
#include "md2gba_cycles.h"
static uint32_t rng=0x109c0a7eu;static uint32_t rnd(void){rng=rng*1664525u+1013904223u;return rng;}
static void cand(Md2Gba68k*c,Md2GbaBus*b,uint32_t*steps,uint32_t*cycles){
 uint16_t w=md2gba_read16(b,c->a[0]&0xffffffu),t=md2gba_read16(b,c->a[1]&0xffffffu),x=w,y=t,tc=0,tt=0;unsigned m=0;
 for(unsigned i=0;i<4;i++){tc=(uint16_t)(x&15u);tt=(uint16_t)(y&15u);if(tc!=tt){m++;tc=(uint16_t)(tc+1u);x=(uint16_t)((x&0xfff0u)|tc);}x=(uint16_t)((x>>4)|(x<<12));y=(uint16_t)((y>>4)|(y<<12));}
 uint16_t oc=(uint16_t)((c->d[7]&0xffffu)-1u),sr=c->sr;c->a[1]+=2u;c->d[0]=(c->d[0]&0xffff0000u)|x;c->d[1]=(c->d[1]&0xffff0000u)|y;
 c->d[2]=(c->d[2]&0xffff0000u)|tc;c->d[3]=(c->d[3]&0xffff0000u)|tt;c->d[6]=0x0000ffffu;if(m)c->d[5]=(c->d[5]&0xffffff00u)|0xffu;
 md2gba_write16(b,c->a[0]&0xffffffu,x);c->a[0]+=2u;c->d[7]=(c->d[7]&0xffff0000u)|oc;c->sr=(uint16_t)((sr&~31u)|((m?0u:(sr&16u)))|(x==0u?4u:0u)|(x&0x8000u?8u:0u));
 c->pc=(oc!=0xffffu)?0x00421f6u:0x0042224u;*steps=41u+m*4u;*cycles=282u+m*20u;
}
static int cpu_same(const Md2Gba68k*a,const Md2Gba68k*b){return !memcmp(a,b,sizeof(*a));}
int main(int argc,char**argv){if(argc<2)return 2;FILE*f=fopen(argv[1],"rb");if(!f)return 2;uint8_t*rom=malloc(0x400000),*rom2=malloc(0x400000);size_t n=fread(rom,1,0x400000,f);fclose(f);memcpy(rom2,rom,n);
 for(unsigned it=0;it<30000u;it++){Md2GbaBus ba,bb;Md2Gba68k a,b;md2gba_bus_init(&ba,rom,n);md2gba_bus_init(&bb,rom2,n);md68k_reset_context(&a);
  for(unsigned i=0;i<8;i++){a.d[i]=rnd();a.a[i]=rnd();}a.pc=0x00421f6u;a.sr=(uint16_t)((rnd()&0x1fu)|0x2000u);a.a[0]=0x00ff0000u+((rnd()&0x7ffeu));a.a[1]=0x00ff8000u+((rnd()&0x7ffeu));a.d[7]=(a.d[7]&0xffff0000u)|(rnd()&63u);b=a;
  uint16_t w=(uint16_t)rnd(),t=(uint16_t)rnd();md2gba_write16(&ba,a.a[0],w);md2gba_write16(&bb,b.a[0],w);md2gba_write16(&ba,a.a[1],t);md2gba_write16(&bb,b.a[1],t);
  uint32_t rs=0,rc=0;while(a.pc!=0x00421f6u && a.pc!=0x0042224u){puts("internal start");return 3;}do{uint32_t pc=a.pc;if(md68k_interp_step(&a,&ba)!=1){printf("interp fault %x\n",pc);return 4;}rs++;rc+=md68k_estimate_cycles(&ba,pc,a.pc);}while(a.pc!=0x00421f6u && a.pc!=0x0042224u);
  uint32_t cs=0,cc=0;cand(&b,&bb,&cs,&cc);
  if(rs!=cs||rc!=cc||!cpu_same(&a,&b)){printf("FAIL it=%u w=%04x t=%04x steps=%u/%u cycles=%u/%u pc=%x/%x sr=%04x/%04x d0=%08x/%08x d2=%08x/%08x d5=%08x/%08x d6=%08x/%08x\n",it,w,t,rs,cs,rc,cc,a.pc,b.pc,a.sr,b.sr,a.d[0],b.d[0],a.d[2],b.d[2],a.d[5],b.d[5],a.d[6],b.d[6]);return 1;}
  if(md2gba_read16(&ba,b.a[0]-2u)!=md2gba_read16(&bb,b.a[0]-2u)){printf("MEM FAIL %u\n",it);return 1;}
 }
 puts("PASS P109 nibble-converge unit: 30,000 interpreter differential states");return 0;}
