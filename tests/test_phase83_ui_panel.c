#include <stdio.h>
#include <string.h>
#include "md2gba_gba_backend.h"
void md2gba_gba_scroll_dma_set(const uint16_t (*stream)[8], int enabled){(void)stream;(void)enabled;}
void md2gba_gba_scroll_latch_direct(const uint16_t scroll[8]){(void)scroll;}
static void setvw(Md2GbaVdp*v,unsigned base,unsigned w,unsigned x,unsigned y,unsigned m){unsigned a=base+2u*(y*w+x);v->vram[a]=(unsigned char)(m>>8);v->vram[a+1]=(unsigned char)m;}
int main(void){Md2GbaVdp v;Md2GbaGbaUiPanel p;unsigned x,y;memset(&v,0,sizeof(v));v.regs[1]=0x40;v.regs[2]=0x30;v.regs[12]=0x81;/* 64x32 */v.regs[16]=1u;/* width 64 height32 */
/* Plane A base c000, exact framing grammar but arbitrary tile IDs. */
for(x=3;x<=36;x++)setvw(&v,0xc000,64,x,20,0xc000u|(x==3||x==36?0x6e9u:0x6eau)|(x==36?0x0800u:0));
for(y=21;y<=24;y++)for(x=3;x<=36;x++)setvw(&v,0xc000,64,x,y,0xc000u|(x==3||x==36?0x6f3u:(0x580u+(y-21u)*64u+(x-4u)*2u))|(x==36?0x0800u:0));
for(x=3;x<=36;x++)setvw(&v,0xc000,64,x,25,0xc000u|0x1000u|(x==3||x==36?0x6e9u:0x6eau)|(x==36?0x0800u:0));
if(!md2gba_gba_detect_bottom_ui_panel(&v,&p)){puts("FAIL detect");return 1;}if(p.x!=3||p.y!=20||p.w!=34||p.h!=6||p.palette!=2){printf("FAIL geom %u %u %u %u %u\n",p.x,p.y,p.w,p.h,p.palette);return 1;}
/* Break border grammar: should no longer be treated as a UI panel. */
setvw(&v,0xc000,64,36,20,0xc6e9u);if(md2gba_gba_detect_bottom_ui_panel(&v,&p)){puts("FAIL false positive");return 1;}puts("PASS phase83 ui panel detector");return 0;}
