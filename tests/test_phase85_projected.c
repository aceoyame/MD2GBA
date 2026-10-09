#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include "md2gba_vdp_soft.h"
void ref_md2gba_vdp_soft_render_240x160(const Md2GbaVdp*,volatile uint16_t*,unsigned,unsigned);
void md2gba_gba_scroll_dma_set(const uint16_t s[160][8],int e){(void)s;(void)e;}
void md2gba_gba_scroll_latch_direct(const uint16_t scroll[8]){(void)scroll;}
static Md2GbaVdp v;
static uint16_t ref[19200],got[19200];
static uint32_t rng=1234567;
static uint32_t random32(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
int main(int argc,char **argv){
 for(unsigned k=0;k<48;k++){
  memset(&v,0,sizeof(v));
  for(unsigned i=0;i<65536;i++)v.vram[i]=random32();
  for(unsigned i=0;i<40;i++)v.vsram[i]=random32();
  v.regs[1]=0x40|((k&1)?8:0);v.regs[2]=0x30;v.regs[3]=0x2c;v.regs[4]=7;v.regs[5]=0x78;
  v.regs[7]=k&63;v.regs[11]=(unsigned[]){0,2,3,4,6,7}[k%6];v.regs[12]=((k&2)?0x81:0)|((k%7==0)?8:0);
  v.regs[13]=0x3d;v.regs[16]=(k&4)?0x11:1;v.regs[17]=(k&8)?0x88:0;v.regs[18]=(k&16)?0x94:0;
  ref_md2gba_vdp_soft_render_240x160(&v,ref,0,0);md2gba_vdp_soft_render_240x160(&v,got,0,0);
  if(memcmp(ref,got,sizeof(got))){for(unsigned i=0;i<19200;i++)if(ref[i]!=got[i]){printf("FAIL vector=%u xy=%u,%u ref=%04x got=%04x\n",k,(i%120)*2,i/120,ref[i],got[i]);return 1;}}
 }
 for(int a=1;a<argc;a++){FILE*f=fopen(argv[a],"rb");if(!f||fread(&v,1,sizeof(v),f)!=sizeof(v))return 2;fclose(f);
  ref_md2gba_vdp_soft_render_240x160(&v,ref,0,0);md2gba_vdp_soft_render_240x160(&v,got,0,0);
  if(memcmp(ref,got,sizeof(got))){printf("FAIL snapshot %s\n",argv[a]);return 1;}
 }
 clock_t t=clock();for(unsigned i=0;i<30;i++)ref_md2gba_vdp_soft_render_240x160(&v,ref,0,0);double old=(double)(clock()-t)/CLOCKS_PER_SEC;
 t=clock();for(unsigned i=0;i<30;i++)md2gba_vdp_soft_render_240x160(&v,got,0,0);double now=(double)(clock()-t)/CLOCKS_PER_SEC;
 printf("PASS 48 mixed VDP vectors + %d source snapshots. Host render ratio %.3fx (%.4fs -> %.4fs).\n",argc-1,old/now,old,now);return 0;
}
