#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "md2gba_gba_backend.h"

/* Pull the implementation into this host-only TU so the P83 presentation
   helpers themselves are exercised.  --gc-sections discards MMIO-only paths. */
void md2gba_gba_scroll_dma_set(const uint16_t (*stream)[8], int enabled){(void)stream;(void)enabled;}
void md2gba_gba_scroll_latch_direct(const uint16_t scroll[8]){(void)scroll;}
#include "../md2gba_gba_backend.c"

static void putvw(Md2GbaVdp *v,unsigned a,uint16_t m){v->vram[(uint16_t)a]=(uint8_t)(m>>8);v->vram[(uint16_t)(a+1u)]=(uint8_t)m;}
static void fill_src_tile(Md2GbaVdp *v,unsigned tile,unsigned global_base){
    for(unsigned y=0;y<8u;y++)for(unsigned x=0;x<8u;x++){
        uint8_t p=(uint8_t)(((global_base+x)%15u)+1u);
        unsigned a=tile*32u+y*4u+(x>>1);
        if(!(x&1u))v->vram[a]=(uint8_t)(p<<4); else v->vram[a]|=p;
    }
}
static unsigned gba_pix(const uint16_t *stage,unsigned slot,unsigned x,unsigned y){
    const uint8_t *d=(const uint8_t*)(const void*)(stage+slot*16u);
    uint8_t b=d[y*4u+(x>>1)]; return (x&1u)?(b>>4):(b&15u);
}
static int req(int ok,const char *m){if(!ok){fprintf(stderr,"FAIL %s\n",m);return 0;}return 1;}
int main(void){
    Md2GbaVdp v; Md2GbaGbaUiPanel p={1,3,20,34,6,2}; Md2GbaBgTileCache c; int ok=1;
    const unsigned pw=64u,base=0xc000u,left=100u,edge=101u,right=102u;
    memset(&v,0,sizeof(v)); memset(&c,0xff,sizeof(c)); memset(md2gba_bg_tile_stage,0,sizeof(md2gba_bg_tile_stage));
    v.regs[2]=0x30; v.regs[16]=1u; c.used_slots=200u;
    c.source_to_slot[left]=10u;c.slot_to_source[10u]=left;
    c.source_to_slot[edge]=11u;c.slot_to_source[11u]=edge;
    c.source_to_slot[right]=12u;c.slot_to_source[12u]=right;
    /* Framing map plus 32 unique interior source tiles on every content row. */
    for(unsigned ry=0;ry<p.h;ry++){
        putvw(&v,base+2u*((p.y+ry)*pw+p.x),(uint16_t)(0xc000u|left));
        putvw(&v,base+2u*((p.y+ry)*pw+p.x+p.w-1u),(uint16_t)(0xc800u|right));
        for(unsigned sx=1;sx+1<p.w;sx++){
            unsigned tile=(ry==0u||ry==p.h-1u)?edge:(200u+(ry-1u)*32u+(sx-1u));
            putvw(&v,base+2u*((p.y+ry)*pw+p.x+sx),(uint16_t)(0xc000u|tile));
            if(ry>0u&&ry+1u<p.h)fill_src_tile(&v,tile,(sx-1u)*8u);
        }
    }
    ok&=req(md2gba_ui_fit_eligible(&p,&c,pw,0u,64u),"34-tile panel eligible for 30-tile fit");
    c.used_slots=900u; ok&=req(md2gba_ui_fit_eligible(&p,&c,pw,0u,64u),"dynamic scratch permits cache above old slot 856 cutoff"); c.used_slots=913u; ok&=req(!md2gba_ui_fit_eligible(&p,&c,pw,0u,64u),"scratch/cache overlap rejected"); c.used_slots=200u;
    md2gba_ui_stage_tiles(&v,&p,pw);
    /* Independent nearest-neighbour equation for 256 -> 224 interior pixels. */
    for(unsigned ry=1u;ry+1u<p.h;ry++)for(unsigned op=0u;op<224u;op++){
        unsigned sp=(op*256u+128u)/224u;if(sp>255u)sp=255u;
        unsigned want=(sp%15u)+1u;
        unsigned slot=912u+(ry-1u)*28u+(op>>3),got=gba_pix(md2gba_bg_tile_stage,slot,op&7u,3u);
        if(got!=want){fprintf(stderr,"FAIL pixel row=%u x=%u sp=%u got=%u want=%u\n",ry,op,sp,got,want);return 2;}
    }
    ok&=req((md2gba_ui_out_name(&v,&p,&c,pw,0u,0u)&0x3ffu)==10u,"top-left border preserved");
    ok&=req((md2gba_ui_out_name(&v,&p,&c,pw,0u,29u)&0x3ffu)==12u,"top-right border preserved");
    ok&=req((md2gba_ui_out_name(&v,&p,&c,pw,0u,1u)&0x3ffu)==11u,"top edge preserved without resample");
    ok&=req((md2gba_ui_out_name(&v,&p,&c,pw,1u,1u)&0x3ffu)==912u,"first content output uses dynamic scratch slot");
    ok&=req((md2gba_ui_out_name(&v,&p,&c,pw,4u,28u)&0x3ffu)==1023u,"last content output uses expected scratch slot");
    if(ok)puts("PASS P83 34->30 tile UI fit pixels + border/map semantics");return ok?0:1;
}
