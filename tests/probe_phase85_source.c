#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "md2gba_interp68k.h"
#include "md2gba_timing.h"
#include "md2gba_vdp_soft.h"
#include "md2gba_gba_backend.h"
static Md2GbaBus bus;
static Md2Gba68k cpu;
static Md2GbaTiming timing;
static unsigned counts[0x100000];
void md2gba_gba_scroll_dma_set(const uint16_t s[160][8],int e){(void)s;(void)e;}
void md2gba_gba_scroll_latch_direct(const uint16_t scroll[8]){(void)scroll;}
int main(int argc,char **argv){
 if(argc<3)return 2;
 FILE *f=fopen(argv[1],"rb"); if(!f)return 2;
 unsigned char *rom=malloc(0x400000); size_t n=fread(rom,1,0x400000,f);fclose(f);
 md2gba_bus_init(&bus,rom,n);md68k_reset_context(&cpu);cpu.pc=0x220;cpu.a[7]=md2gba_read32(&bus,0);
 md2gba_timing_init(&timing,0,1);md2gba_timing_enable_cycles(&timing,262,224,488);
 unsigned limit=atoi(argv[2]),last=0;unsigned long long steps=0;
 while(timing.vblanks<limit){unsigned pc=cpu.pc;
  if((pc>>1)<0x100000)counts[pc>>1]++;
  if(md68k_interp_step(&cpu,&bus)!=1){printf("FAULT %06x %04x\n",pc,md2gba_read16(&bus,pc));return 1;}
  md2gba_timing_after_step(&cpu,&bus,pc,++steps,&timing);
  if(timing.vblanks!=last){last=timing.vblanks;
   bus.pad_held[0]=(last>180 && last<185)?MD2GBA_PAD_START:((last>240 && last<244)?MD2GBA_PAD_C:0);
   if(last%120==0 || last==limit){char path[100];Md2GbaGbaUiPanel panel;int yes=md2gba_gba_detect_bottom_ui_panel(&bus.vdp,&panel);
    printf("frame=%u pc=%06x steps=%llu hscroll=%04x vs=%04x,%04x mode=%02x ui=%d\n",last,cpu.pc,steps,md2gba_read16(&bus,0xffef94),bus.vdp.vsram[0],bus.vdp.vsram[1],bus.vdp.regs[11],yes);fflush(stdout);
    snprintf(path,sizeof(path),"analysis/source-%04u.vdp",last);f=fopen(path,"wb");if(f){fwrite(&bus.vdp,1,sizeof(bus.vdp),f);fclose(f);}
   }
  }
 }
 f=fopen("analysis/source-pc-counts.bin","wb");fwrite(counts,1,sizeof(counts),f);fclose(f);
 return 0;
}
