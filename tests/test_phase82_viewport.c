#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "md2gba_gba_backend.h"

int md2gba_vdp_vram_range_dirty(const Md2GbaVdp *v,uint16_t start,unsigned length){(void)v;(void)start;(void)length;return 0;}
void md2gba_gba_scroll_dma_set(const uint16_t stream[160][8],int enabled){(void)stream;(void)enabled;}
void md2gba_gba_scroll_latch_direct(const uint16_t scroll[8]){(void)scroll;}

static void setw(Md2GbaVdp*v,unsigned a,uint16_t x){v->vram[(uint16_t)a]=(uint8_t)(x>>8);v->vram[(uint16_t)(a+1u)]=(uint8_t)x;}
static void fill_row_priority(Md2GbaVdp*v,unsigned row,unsigned first,unsigned count){
    uint16_t base=md2gba_vdp_plane_a_base(v);unsigned w=md2gba_vdp_plane_width(v),x;
    for(x=first;x<first+count && x<w;x++)setw(v,(unsigned)base+2u*(row*w+x),(uint16_t)(0x8000u|1u));
}
int main(void){
    Md2GbaVdp v;unsigned y;
    memset(&v,0,sizeof(v));v.regs[1]=0x40;v.regs[2]=0x30;v.regs[16]=0; /* 32x32, 224 lines */
    y=md2gba_gba_suggest_view_y(&v);if(y!=32u){fprintf(stderr,"blank center=%u\n",y);return 1;}
    /* Four dense rows at source 192..223 and four more at 160..191 model a
       fixed bottom dialogue/menu slab. */
    for(unsigned r=20;r<28;r++)fill_row_priority(&v,r,0,16);
    y=md2gba_gba_suggest_view_y(&v);if(y!=64u){fprintf(stderr,"bottom UI=%u\n",y);return 2;}
    memset(&v.vram[0xC000],0,0x800); /* clear plane A map */
    /* Priority scenery in both comparison bands should not look like a newly
       introduced fixed bottom overlay. */
    for(unsigned r=12;r<28;r++)fill_row_priority(&v,r,0,16);
    y=md2gba_gba_suggest_view_y(&v);if(y!=32u){fprintf(stderr,"scenery center=%u\n",y);return 3;}
    puts("Phase82 viewport tests: PASS");return 0;
}
