#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "md2gba_gba_backend.h"
void md2gba_gba_scroll_dma_set(const uint16_t (*s)[8],int e){(void)s;(void)e;}
void md2gba_gba_scroll_latch_direct(const uint16_t s[8]){(void)s;}
#include "../md2gba_gba_backend.c"
static void pv(Md2GbaVdp*v,unsigned a,uint16_t q){v->vram[(uint16_t)a]=(uint8_t)(q>>8);v->vram[(uint16_t)(a+1)]=(uint8_t)q;}
int main(void){Md2GbaVdp v;Md2GbaGbaUiPanel p;unsigned base=0xc000,pw=64,x0=3,y0=21,w=34,h=6;memset(&v,0,sizeof v);v.regs[1]=0x40;v.regs[2]=0x30;v.regs[12]=0x81;v.regs[16]=1; /* 64x32, H40 */
 /* Top: corner tile 10, edge 11, mirrored corner. */
 pv(&v,base+2*(y0*pw+x0),0x800a);for(unsigned x=1;x<w-1;x++)pv(&v,base+2*(y0*pw+x0+x),0x800b);pv(&v,base+2*(y0*pw+x0+w-1),0x880a);
 for(unsigned y=1;y<h-1;y++){pv(&v,base+2*((y0+y)*pw+x0),0x800c);for(unsigned x=1;x<w-1;x++)pv(&v,base+2*((y0+y)*pw+x0+x),0x800d);pv(&v,base+2*((y0+y)*pw+x0+w-1),0x880c);}
 pv(&v,base+2*((y0+h-1)*pw+x0),0x900a);for(unsigned x=1;x<w-1;x++)pv(&v,base+2*((y0+h-1)*pw+x0+x),0x900b);pv(&v,base+2*((y0+h-1)*pw+x0+w-1),0x980a);
 if(!md2gba_gba_detect_bottom_ui_panel(&v,&p)||!p.active||p.w!=34||p.h!=6){printf("FAIL detect %u %u %u\n",p.active,p.w,p.h);return 1;}
 if(md2gba_gba_suggest_view_y(&v)!=64u){printf("FAIL anchor %u\n",md2gba_gba_suggest_view_y(&v));return 2;}
 /* High-priority scenery without frame grammar must not move viewport. */
 memset(&v.vram[base],0,0x1000);for(unsigned y=20;y<28;y++)for(unsigned x=0;x<20;x++)pv(&v,base+2*(y*pw+x),0x8001);
 if(md2gba_gba_detect_bottom_ui_panel(&v,&p)){puts("FAIL scenery panel");return 3;}if(md2gba_gba_suggest_view_y(&v)!=32u){printf("FAIL scenery anchor %u\n",md2gba_gba_suggest_view_y(&v));return 4;}
 puts("PASS P89 framed wide UI detection + scenery-safe viewport");return 0;}
