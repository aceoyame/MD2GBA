#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "md2gba_vdp_soft.h"
#include "md2gba_gba_backend.h"

void md2gba_gba_scroll_dma_set(const uint16_t stream[160][8],int enabled){(void)stream;(void)enabled;}
void md2gba_gba_scroll_latch_direct(const uint16_t scroll[8]){(void)scroll;}

static void name(Md2GbaVdp*v,unsigned a,unsigned t){v->vram[a]=(uint8_t)(t>>8);v->vram[a+1]=(uint8_t)t;}
static void fill_map(Md2GbaVdp*v,unsigned base,unsigned w,unsigned h,unsigned tile){for(unsigned i=0;i<w*h;i++)name(v,base+i*2u,tile);}
static void dirty_tile(Md2GbaVdp*v,unsigned tile){v->vram_dirty_tiles[tile>>5]|=1u<<(tile&31u);v->vram_dirty_pages[(tile*32u>>8)>>5]|=1u<<((tile*32u>>8)&31u);}
static void dirty_addr(Md2GbaVdp*v,unsigned addr){unsigned b=(addr&0xffffu)>>5;v->vram_dirty_tiles[b>>5]|=1u<<(b&31u);v->vram_dirty_pages[((addr&0xffffu)>>8)>>5]|=1u<<(((addr&0xffffu)>>8)&31u);}
static void clear_dirty(Md2GbaVdp*v){memset(v->vram_dirty_tiles,0,sizeof(v->vram_dirty_tiles));memset(v->vram_dirty_pages,0,sizeof(v->vram_dirty_pages));}
static int req(int ok,const char*m){if(!ok){fprintf(stderr,"FAIL %s\n",m);return 0;}return 1;}
int main(void){
    Md2GbaVdp v; static uint16_t before[19200],partial[19200],full[19200]; unsigned f,l; int ok=1;
    memset(&v,0,sizeof(v));
    v.regs[1]=0x40; v.regs[2]=0x30; v.regs[3]=0x28; v.regs[4]=7; v.regs[5]=0x40; v.regs[12]=0x81; v.regs[13]=0x20; v.regs[16]=0;
    v.regs[17]=0; v.regs[18]=(uint8_t)(0x80|20); /* full-width bottom 64 source px */
    fill_map(&v,0xC000,32,32,1); fill_map(&v,0xE000,32,32,2); fill_map(&v,0xA000,64,32,10);
    memset(&v.vram[1*32],0x11,32); memset(&v.vram[2*32],0x22,32); memset(&v.vram[10*32],0x33,32);
    /* SAT at $8000: first entry link=0 and offscreen. H-scroll also $8000; zero is fine. */
    ok&=req(md2gba_vdp_soft_fullwidth_window_rows(&v,&f,&l)&&f==96u&&l==160u,"bottom Window maps to output rows 96..159");
    clear_dirty(&v); dirty_tile(&v,10); ok&=req(md2gba_vdp_soft_window_only_dirty(&v),"Window-only glyph tile accepted");
    clear_dirty(&v); dirty_addr(&v,0xA000); ok&=req(md2gba_vdp_soft_window_only_dirty(&v),"Window name-table edit accepted");
    clear_dirty(&v); dirty_addr(&v,0xC000); ok&=req(!md2gba_vdp_soft_window_only_dirty(&v),"Plane A map edit rejected");
    clear_dirty(&v); name(&v,0xC000,10); dirty_tile(&v,10); ok&=req(!md2gba_vdp_soft_window_only_dirty(&v),"shared A/Window glyph rejected"); name(&v,0xC000,1);
    v.regs[17]=1; ok&=req(!md2gba_vdp_soft_fullwidth_window_rows(&v,&f,&l),"horizontal Window rejects partial-row path"); v.regs[17]=0;

    clear_dirty(&v); md2gba_vdp_soft_render_240x160(&v,before,0,0);
    memcpy(partial,before,sizeof(before));
    memset(&v.vram[10*32],0x77,32); dirty_tile(&v,10);
    ok&=req(md2gba_vdp_soft_window_only_dirty(&v),"changed glyph still proven Window-only");
    ok&=req(md2gba_vdp_soft_fullwidth_window_rows(&v,&f,&l),"row span available for partial render");
    md2gba_vdp_soft_render_rows_240x160_coop(&v,partial,f,l,0,0,0,0,160);
    md2gba_vdp_soft_render_240x160(&v,full,0,0);
    ok&=req(memcmp(partial,full,sizeof(full))==0,"partial Window rows equal canonical full render");
    if(ok)puts("PASS P81 conservative Window-only partial renderer");
    return ok?0:1;
}
