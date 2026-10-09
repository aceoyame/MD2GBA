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
static Md2GbaBus bus,abus,ibus; static Md2Gba68k cpu,acpu,icpu; static Md2GbaTiming timing;
static int check_boundary(uint32_t asset){
 uint32_t initial_left=timing.cycles_per_line-timing.cycle_accum;acpu=icpu=cpu;abus=ibus=bus;md2gba_profile_raw_nem_reset();
 ps4_whole_region_pschybrid_state st={0};st.left=initial_left;Md2GbaAotHooks hooks={0};
 int rc=ps4_whole_region_page_0105_pschybrid(&acpu,&abus,&hooks,&st);if(!st.steps)return 10;
 uint32_t left=initial_left,over=0;for(uint32_t k=0;k<st.steps;k++){uint32_t before=icpu.pc;if(md68k_interp_step(&icpu,&ibus)!=1)return 11;uint32_t cy=md68k_estimate_cycles(&ibus,before,icpu.pc);if(left<=cy){over+=cy-left;left=0;}else left-=cy;}
 if(memcmp(&acpu,&icpu,sizeof(acpu))||st.left!=left||st.over!=over){fprintf(stderr,"asset %06x CPU/timing mismatch steps=%u rc=%d Apc=%06x Ipc=%06x L=%u/%u O=%u/%u\n",asset,st.steps,rc,acpu.pc,icpu.pc,st.left,left,st.over,over);return 12;}
 if(memcmp(abus.wram,ibus.wram,sizeof(abus.wram))||memcmp(abus.z80_ram,ibus.z80_ram,sizeof(abus.z80_ram))||memcmp(abus.sram,ibus.sram,sizeof(abus.sram))||memcmp(&abus.vdp,&ibus.vdp,sizeof(abus.vdp))||memcmp(&abus.audio,&ibus.audio,sizeof(abus.audio))){fprintf(stderr,"asset %06x mutable bus mismatch\n",asset);return 13;}
 printf("asset %06x: %u canonical instructions -> seam PC %06X left %u over %u\n",asset,st.steps,acpu.pc,st.left,st.over);return 0;
}
int main(int argc,char**argv){if(argc<2)return 2;FILE*f=fopen(argv[1],"rb");if(!f)return 2;uint8_t*rom=malloc(0x400000);size_t n=fread(rom,1,0x400000,f);fclose(f);md2gba_bus_init(&bus,rom,n);md68k_reset_context(&cpu);cpu.pc=0x220;cpu.a[7]=md2gba_read32(&bus,0);md2gba_timing_init(&timing,0,1);md2gba_timing_enable_cycles(&timing,262,224,488);
 uint32_t asset=0,last_asset=0;unsigned last=0,checked=0;unsigned long long guard=0;
 while(timing.vblanks<241u&&guard++<40000000ull){
  if(cpu.pc==0x041750u)asset=cpu.a[0]&0xffffffu;
  if(cpu.pc==0x04178Au&&asset>=0x100000u&&asset!=last_asset){int r=check_boundary(asset);if(r)return r;last_asset=asset;checked++;}
  uint32_t before=cpu.pc;if(md68k_interp_step(&cpu,&bus)!=1)return 3;md2gba_timing_after_step(&cpu,&bus,before,(uint32_t)guard,&timing);
  if(timing.vblanks!=last){last=timing.vblanks;bus.pad_held[0]=(last>180&&last<185)?MD2GBA_PAD_START:0;}
 }
 if(checked!=5u){fprintf(stderr,"expected 5 opening assets, checked %u\n",checked);return 4;}
 printf("PASS P125 integrated page: raw-command + generic native token paths match canonical at all opening asset boundaries\n");return 0;}
