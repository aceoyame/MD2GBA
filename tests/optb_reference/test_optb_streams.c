#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "md2gba_bus.h"
static Md2GbaBus got,ref,before;
static uint8_t rom[65536] __attribute__((aligned(4))),rev[65536] __attribute__((aligned(4)));
static uint32_t seed=0xB122;
static unsigned rnd(void){seed=seed*1664525u+1013904223u;return seed>>4;}
static uint32_t readv(Md2GbaBus*b,uint32_t a,unsigned w){return w==1?md2gba_read8_fast(b,a):w==2?md2gba_read16(b,a):md2gba_read32(b,a);}
static void writev(Md2GbaBus*b,uint32_t a,unsigned w,uint32_t v){if(w==1)md2gba_write8_fast(b,a,v);else if(w==2)md2gba_write16(b,a,v);else md2gba_write32(b,a,v);}
int main(void){
 unsigned accepted[3]={0},rejected[3]={0};
 for(unsigned i=0;i<65536;i++)rom[i]=(uint8_t)rnd();
 for(unsigned i=0;i<65536;i++)rev[65535-i]=rom[i];
 for(unsigned test=0;test<15000;test++){
  unsigned width=1u<<(test%3),n=1+rnd()%40,si=rnd()%10,di=rnd()%10;
  uint32_t src=(test&1)?0xFF0000u:0u,dst=(test&2)?0xFF0000u:0u;
  src+=rnd()%65000;dst+=rnd()%65000;
  if(test%4==0){src&=~(width-1u);dst&=~(width-1u);si=(si/width)*width;di=(di/width)*width;}
  if(test%5==0){src=0xFF1000;dst=src+(test%13);si=width;di=width;}
  if(test%7==0){src=0xFFFFE0;dst=0xFF2000;}
  if(test%11==0){src=0xFF1000;dst=src;di=si;}
  md2gba_bus_init(&got,rom,sizeof(rom));got.rom_reversed=rev;
  if(test%13==0){got.sram_present=1;got.sram_control=1;got.sram_start=0;got.sram_end=65535;got.sram_size=1024;}
  for(unsigned i=0;i<65536;i++)got.wram[i]=(uint8_t)rnd();
  before=got;ref=got;
  uint32_t last=0x12345678,expected=0;unsigned ok=md2gba_bus_copy_stream_fast(&got,src,dst,n,width,si,di,&last);
  if(ok){accepted[0]++;for(unsigned i=0;i<n;i++){expected=readv(&ref,src+i*si,width);writev(&ref,dst+i*di,width,expected);}if(last!=expected||memcmp(got.wram,ref.wram,sizeof(got.wram)))goto fail;}
  else{rejected[0]++;if(last!=0x12345678||memcmp(&got,&before,sizeof got))goto fail;}
  got=before;ref=before;uint32_t sv=0x89,dv=0xAB;unsigned done=0xCD;
  ok=md2gba_bus_compare_stream_fast(&got,src,dst,n,width,si,di,&sv,&dv,&done);
  if(ok){accepted[1]++;unsigned k=0;uint32_t es,ed;do{es=readv(&ref,src+k*si,width);ed=readv(&ref,dst+k*di,width);k++;}while(k<n&&es==ed);if(sv!=es||dv!=ed||done!=k||memcmp(&got,&ref,sizeof got))goto fail;}
  else{rejected[1]++;if(sv!=0x89||dv!=0xAB||done!=0xCD||memcmp(&got,&before,sizeof got))goto fail;}
  got=before;ref=before;uint32_t value=rnd();
  ok=md2gba_bus_fill_stream_fast(&got,dst,n,width,di,value);
  if(ok){accepted[2]++;for(unsigned i=0;i<n;i++)writev(&ref,dst+i*di,width,value);if(memcmp(&got,&ref,sizeof got))goto fail;}
  else{rejected[2]++;if(memcmp(&got,&before,sizeof got))goto fail;}
  continue;
 fail:printf("FAIL streams case %u w=%u n=%u si=%u di=%u src=%x dst=%x\n",test,width,n,si,di,src,dst);return 1;
 }
 /* Pathological multiplication must refuse before touching memory. */
 before=got;uint32_t last=99;
 if(md2gba_bus_copy_stream_fast(&got,0xFF0000,0xFF1000,0x80000001u,4,2,2,&last)||last!=99||memcmp(&got,&before,sizeof got)){puts("FAIL overflow guard");return 2;}
 for(unsigned i=0;i<3;i++)if(!accepted[i]||!rejected[i])return 3;
 printf("PASS OPT-B streams 15,000 cases; copy/compare/fill accepted %u/%u/%u; overlap, fixed/odd strides, SRAM, wrap and overflow refusal\n",accepted[0],accepted[1],accepted[2]);
 return 0;
}
