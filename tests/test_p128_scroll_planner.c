#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "md2gba_gba_backend.h"

static uint32_t st=0x6d2b79f5u;
static uint32_t rng(void){st=st*1664525u+1013904223u;return st;}
static uint16_t vw(const Md2GbaVdp*v,unsigned a){return (uint16_t)(((uint16_t)v->vram[(uint16_t)a]<<8)|v->vram[(uint16_t)(a+1u)]);}
static int ref_stream(const Md2GbaVdp*v,uint16_t out[160][8],unsigned ox,unsigned oy){
    int16_t va,vb;unsigned y,mode=v->regs[11]&3u,base;
    if(md2gba_vdp_vscroll_plane(v,0,&va)||md2gba_vdp_vscroll_plane(v,1,&vb))return -2;
    va=(int16_t)(va+oy);vb=(int16_t)(vb+oy);base=(unsigned)md2gba_vdp_hscroll_base(v);
    if(mode<2u)return md2gba_gba_build_scroll_stream_viewport(v,out,ox,oy); /* unchanged path */
    for(y=0;y<160u;y++){
        unsigned sy=y+oy,idx=(mode==2u)?(sy&~7u):sy,a=base+idx*4u;int16_t ha=(int16_t)vw(v,a),hb=(int16_t)vw(v,a+2u);uint16_t*d=out[y];
        d[0]=(uint16_t)(ox-ha);d[1]=(uint16_t)va;d[2]=(uint16_t)(ox-hb);d[3]=(uint16_t)vb;d[4]=d[0];d[5]=d[1];d[6]=d[2];d[7]=d[3];
    }
    return 0;
}
int main(void){
    for(unsigned n=0;n<20000;n++){
        Md2GbaVdp v;memset(&v,0,sizeof(v));v.regs[11]=(uint8_t)(2u+(rng()&1u));v.regs[13]=(uint8_t)(rng()&0x3fu);
        for(unsigned i=0;i<0x10000u;i++)v.vram[i]=(uint8_t)rng();for(unsigned i=0;i<0x40u;i++)v.vsram[i]=(uint16_t)rng();
        unsigned ox=rng()%161u,oy=rng()%81u;uint16_t a[160][8],b[160][8];int ra=md2gba_gba_build_scroll_stream_viewport(&v,a,ox,oy),rb=ref_stream(&v,b,ox,oy);
        if(ra!=rb||memcmp(a,b,sizeof(a))){printf("scroll mismatch n=%u mode=%u ox=%u oy=%u\n",n,v.regs[11]&3u,ox,oy);return 1;}
    }
    {Md2GbaVdp v;Md2GbaGbaFrameState s;memset(&v,0,sizeof(v));memset(&s,0,sizeof(s));s.initialized=1u;s.last_vram_epoch=v.vram_epoch;s.last_cram_epoch=v.cram_epoch;s.last_vsram_epoch=v.vsram_epoch;s.last_regs_epoch=0u;v.regs_epoch=1u;v.regs_dirty_mask=1u<<5;uint32_t p=md2gba_gba_plan_frame(&v,&s);if(p!=MD2GBA_FRAME_SPRITES){printf("SAT plan=%x expected sprites-only\n",p);return 2;}}
    puts("PASS P128 cell-scroll equivalence + SAT-only invalidation");return 0;
}
