#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "md2gba_interp68k.h"
#include "md2gba_timing.h"
#include "md2gba_cycles.h"
#include "md2gba_aot_region.h"
#include "ps4_whole_region_pschybrid_internal.h"
#include "md2gba_profile_raw_nem.h"
int ps4_whole_region_page_0105_pschybrid(Md2Gba68k*,Md2GbaBus*,const Md2GbaAotHooks*,ps4_whole_region_pschybrid_state*);
int main(int argc,char**argv){
 if(argc<2)return 2;FILE*f=fopen(argv[1],"rb");if(!f)return 2;uint8_t*rom=malloc(0x400000);size_t n=fread(rom,1,0x400000,f);fclose(f);
 Md2GbaBus bus;Md2Gba68k cpu;Md2GbaTiming timing;md2gba_bus_init(&bus,rom,n);md68k_reset_context(&cpu);cpu.pc=0x220;cpu.a[7]=md2gba_read32(&bus,0);md2gba_timing_init(&timing,0,1);md2gba_timing_enable_cycles(&timing,262,224,488);md2gba_timing_enable_scheduled(&timing);
 uint32_t asset=0,last_asset=0;unsigned last=0,checked=0,batch_hits=0;unsigned long long guard=0;
 while(timing.vblanks<241u&&guard++<40000000ull){
  if(cpu.pc==0x041750u)asset=cpu.a[0]&0xffffffu;
  if(cpu.pc==0x04178Au&&asset>=0x100000u&&asset!=last_asset){
   Md2Gba68k acpu=cpu;Md2GbaBus abus=bus;Md2GbaTiming at=timing;uint32_t initial=at.cycles_per_line-at.cycle_accum;
   ps4_whole_region_pschybrid_state st={0};st.left=initial;Md2GbaAotHooks hooks={0};hooks.scheduled_user=&at;md2gba_profile_raw_nem_reset();
   int rc=ps4_whole_region_page_0105_pschybrid(&acpu,&abus,&hooks,&st);
   printf("asset %06x combined steps=%u pc=%06x left=%u over=%u scan=%u rc=%d\n",asset,st.steps,acpu.pc,st.left,st.over,at.scanline,rc);
   if(st.steps>1000u)batch_hits++; checked++;last_asset=asset;
  }
  uint32_t before=cpu.pc;if(md68k_interp_step(&cpu,&bus)!=1)return 3;md2gba_timing_after_step(&cpu,&bus,before,(uint32_t)guard,&timing);
  if(timing.vblanks!=last){last=timing.vblanks;bus.pad_held[0]=(last>180&&last<185)?MD2GBA_PAD_START:0;}
 }
 if(checked!=5u||batch_hits!=5u){fprintf(stderr,"expected 5 batch-selected opening assets, checked=%u hits=%u\n",checked,batch_hits);return 4;}
 puts("PASS P126 combined page selects VBlank superblock for all five opening assets before fallback layers");return 0;
}
