#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "md2gba_vdp_soft.h"
#include "md2gba_gba_backend.h"

int md2gba_vdp_vram_range_dirty(const Md2GbaVdp *v,uint16_t start,unsigned length){(void)v;(void)start;(void)length;return 0;}
void md2gba_gba_scroll_dma_set(const uint16_t stream[160][8],int enabled){(void)stream;(void)enabled;}
void md2gba_gba_scroll_latch_direct(const uint16_t scroll[8]){(void)scroll;}

static void setw(Md2GbaVdp*v,unsigned a,uint16_t x){v->vram[(uint16_t)a]=(uint8_t)(x>>8);v->vram[(uint16_t)(a+1u)]=(uint8_t)x;}
static void set_pattern(Md2GbaVdp*v,unsigned tile,uint8_t n){for(unsigned i=0;i<32u;i++)v->vram[tile*32u+i]=(uint8_t)(n?n:0x11u);}
static void dirty_tile(Md2GbaVdp*v,unsigned tile){v->vram_dirty_tiles[tile>>5]|=1u<<(tile&31u);v->vram_dirty_pages[(tile>>3)>>5]|=1u<<((tile>>3)&31u);}
static void clear_dirty(Md2GbaVdp*v){memset(v->vram_dirty_tiles,0,sizeof(v->vram_dirty_tiles));memset(v->vram_dirty_pages,0,sizeof(v->vram_dirty_pages));}
static int req(int ok,const char*m){if(!ok){fprintf(stderr,"FAIL %s\n",m);return 0;}return 1;}

int main(void){
    Md2GbaVdp v;static uint16_t before[19200],partial[19200],full[19200];int ok=1;unsigned first=0,last=0;
    const unsigned A=0xC000u,B=0xE000u,W=64u,H=32u,ui_tile=10u,bg_tile=1u;
    memset(&v,0,sizeof(v));
    v.regs[1]=0x40;v.regs[2]=0x30;v.regs[4]=7;v.regs[5]=0x78;v.regs[11]=0;v.regs[12]=0x81;v.regs[13]=0x3D;v.regs[16]=1; /* H40, 64x32 */
    for(unsigned i=0;i<W*H;i++){setw(&v,A+i*2u,bg_tile);setw(&v,B+i*2u,2u);}
    /* Dense high-priority final 64 source lines: generic fixed bottom UI. */
    for(unsigned y=20;y<28;y++)for(unsigned x=0;x<40;x++)setw(&v,A+2u*(y*W+x),(uint16_t)(0x8000u|3u));
    /* One source glyph occupies two pattern rows in the same bottom band. */
    setw(&v,A+2u*(21u*W+24u),(uint16_t)(0x8000u|ui_tile));
    set_pattern(&v,bg_tile,0x11u);set_pattern(&v,2u,0x22u);set_pattern(&v,3u,0x33u);set_pattern(&v,ui_tile,0x44u);

    ok&=req(md2gba_gba_suggest_view_y(&v)==64u,"hardware detector recognizes bottom UI");
    ok&=req(md2gba_vdp_soft_bottom_ui_rows(&v,&first,&last)&&first==96u&&last==160u,"bottom UI projects to output rows 96..159");
    md2gba_vdp_soft_render_240x160(&v,before,0,0);
    ok&=req((before[96u*120u]&0xffu)==md2gba_vdp_soft_pixel(&v,0u,160u),"bottom projection preserves source y=160 at output y=96");
    ok&=req((before[96u*120u+119u]>>8)==md2gba_vdp_soft_pixel(&v,319u,160u),"H40 projection includes source right edge x=319");

    clear_dirty(&v);set_pattern(&v,ui_tile,0x77u);dirty_tile(&v,ui_tile);
    ok&=req(md2gba_vdp_soft_bottom_ui_only_dirty(&v),"bottom-only glyph pattern accepted");
    memcpy(partial,before,sizeof(before));
    md2gba_vdp_soft_render_rows_240x160_coop(&v,partial,first,last,0,0,0,0,160u);
    md2gba_vdp_soft_render_240x160(&v,full,0,0);
    ok&=req(memcmp(partial,full,sizeof(full))==0,"bottom-only partial render equals canonical full frame");

    /* Sharing the changed glyph above the redraw band must reject partial. */
    setw(&v,A+2u*(5u*W+5u),(uint16_t)(0x8000u|ui_tile));
    ok&=req(!md2gba_vdp_soft_bottom_ui_only_dirty(&v),"glyph shared above UI band rejected");
    if(ok)puts("PASS P82 bottom Plane-A UI fit + conservative partial renderer");
    return ok?0:1;
}
