#include <stdint.h>
#include <stdio.h>
#include <string.h>
uint16_t md2gba_bg_tile_stage[1024u*16u] __attribute__((aligned(4)));
#include "../md2gba_gba_projected.c"

/* Standalone host-link stubs: this oracle exercises only projection math and
   source-pixel semantics, not physical GBA publication. */
void md2gba_gba_stop_hblank_scroll_dma(void){}
void md2gba_gba_scroll_latch_clear(void){}
unsigned md2gba_vdp_plane_width(const Md2GbaVdp*v){return (v->regs[16]&3u)==1u?64u:32u;}
unsigned md2gba_vdp_plane_height(const Md2GbaVdp*v){return ((v->regs[16]>>4)&3u)==1u?64u:32u;}
uint16_t md2gba_vdp_plane_a_base(const Md2GbaVdp*v){return (uint16_t)((v->regs[2]&0x38u)<<10);}
uint16_t md2gba_vdp_plane_b_base(const Md2GbaVdp*v){return (uint16_t)((v->regs[4]&7u)<<13);}
unsigned md2gba_vdp_sat_base(const Md2GbaVdp*v){return (unsigned)((v->regs[5]&0x7fu)<<9);}
int md2gba_vdp_hscroll_line(const Md2GbaVdp*v,unsigned p,unsigned line,int16_t*out){(void)line;*out=v->present_hscroll[p&1u];return 0;}
int md2gba_vdp_vscroll_plane(const Md2GbaVdp*v,unsigned p,int16_t*out){*out=v->present_vscroll[p&1u];return 0;}
int md2gba_vdp_vram_range_dirty(const Md2GbaVdp*v,uint16_t start,unsigned len){(void)v;(void)start;(void)len;return 0;}
void md2gba_vdp_clear_dirty(Md2GbaVdp*v){(void)v;}

static void put_name(Md2GbaVdp*v,unsigned base,unsigned x,unsigned y,uint16_t n){unsigned a=base+2u*(y*32u+x);v->vram[(uint16_t)a]=(uint8_t)(n>>8);v->vram[(uint16_t)(a+1u)]=(uint8_t)n;}
static void make_x_tile(Md2GbaVdp*v,unsigned t){unsigned y,x;for(y=0;y<8;y++)for(x=0;x<8;x+=2){unsigned a=t*32u+y*4u+x/2u;v->vram[a]=(uint8_t)(((x+1u)<<4)|(x+2u));}}
int main(void){
    Md2GbaVdp v;uint8_t out[64];unsigned x,y;memset(&v,0,sizeof(v));memset(md2gba_bg_tile_stage,0,sizeof(md2gba_bg_tile_stage));
    PS->magic=PROJ_MAGIC;PS->plane_w=PS->plane_h=32u;PS->plane_base[0]=0x4000u;PS->plane_base[1]=0x5000u;
    make_x_tile(&v,1u);for(y=0;y<32;y++)for(x=0;x<32;x++)put_name(&v,0x4000u,x,y,1u);
    make_bg_tile(&v,2u,0,0,out);
    {const uint8_t want[8]={1,2,3,5,6,7,1,2};for(x=0;x<8;x++)if(out[x]!=want[x]){printf("FAIL x%u got%u want%u\n",x,out[x],want[x]);return 1;}}
    make_bg_tile(&v,0u,0,0,out);for(x=0;x<64;x++)if(out[x]){puts("FAIL low/high split");return 2;}
    for(y=0;y<32;y++)for(x=0;x<32;x++)put_name(&v,0x4000u,x,y,(uint16_t)(0x8000u|0x4000u|1u));
    make_bg_tile(&v,0u,0,0,out);if(out[0]!=33u||out[3]!=37u){printf("FAIL palette/high %u %u\n",out[0],out[3]);return 3;}
    make_bg_tile(&v,2u,0,0,out);for(x=0;x<64;x++)if(out[x]){puts("FAIL high/low split");return 4;}
    if(qx(-1)!=-1||qy(-1)!=-1||invx(-1)!=-2||invy(-1)!=-2){printf("FAIL negative floor %ld %ld %ld %ld\n",(long)qx(-1),(long)qy(-1),(long)invx(-1),(long)invy(-1));return 5;}
    if(qx(320)!=240||qy(224)!=160){printf("FAIL fit endpoints %ld %ld\n",(long)qx(320),(long)qy(224));return 6;}
    if(sizeof(Md2GbaProjectedState)>sizeof(md2gba_bg_tile_stage)){puts("FAIL state size");return 7;}
    printf("PASS P134 projection math: 320x224->%ldx%ld, state=%u bytes, priority/palette/index sampling exact\n",(long)qx(320),(long)qy(224),(unsigned)sizeof(Md2GbaProjectedState));return 0;
}
