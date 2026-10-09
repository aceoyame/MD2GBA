#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "md2gba_bus.h"

static uint32_t rng=0x1245A11u;
static uint32_t rnd(void){rng=rng*1664525u+1013904223u;return rng;}

static int check(const Md2GbaVdp *v,unsigned t){
    unsigned mode=v->regs[11]&3u, base=((unsigned)v->regs[13]&0x3fu)<<10;
    unsigned valid=((mode&2u)==0u)&&((v->regs[11]&4u)==0u);
    int16_t ha=0,hb=0,va=0,vb=0;
    if(valid){
        ha=(int16_t)(((uint16_t)v->vram[(uint16_t)base]<<8)|v->vram[(uint16_t)(base+1u)]);
        hb=(int16_t)(((uint16_t)v->vram[(uint16_t)(base+2u)]<<8)|v->vram[(uint16_t)(base+3u)]);
        va=(int16_t)v->vsram[0]; vb=(int16_t)v->vsram[1];
    }
    if(v->present_scroll_valid!=(uint8_t)valid || v->present_scroll_mode!=(uint8_t)mode ||
       v->present_hscroll[0]!=ha || v->present_hscroll[1]!=hb ||
       v->present_vscroll[0]!=va || v->present_vscroll[1]!=vb){
        fprintf(stderr,"FAIL t=%u mode=%u valid=%u got %d,%d,%d,%d exp %d,%d,%d,%d\n",t,mode,valid,
          v->present_hscroll[0],v->present_hscroll[1],v->present_vscroll[0],v->present_vscroll[1],ha,hb,va,vb);
        return 0;
    }
    return 1;
}

int main(void){
    Md2GbaBus b; unsigned t;
    static uint8_t dummyrom[4]={0};
    md2gba_bus_init(&b,dummyrom,sizeof(dummyrom));
    for(t=0;t<50000u;t++){
        unsigned mode=rnd()&7u, reg13=rnd()&0x3fu;
        /* Use canonical register writes so the presentation shadow begins synchronized. */
        md2gba_vdp_control16_direct(&b,(uint16_t)(0x8000u|(11u<<8)|mode));
        md2gba_vdp_control16_direct(&b,(uint16_t)(0x8000u|(13u<<8)|reg13));
        if(!check(&b.vdp,t))return 1;
        b.vdp.target=MD2GBA_VDP_VRAM; b.vdp.regs[15]=2u;
        /* Bias half the cases toward the live four-byte H-scroll window. */
        if(t&1u){unsigned base=(reg13&0x3fu)<<10;b.vdp.address=(uint16_t)(base+(rnd()%4u));}
        else b.vdp.address=(uint16_t)rnd();
        md2gba_vdp_data16_direct_vdp(&b.vdp,(uint16_t)rnd());
        if(!check(&b.vdp,t))return 2;
    }
    puts("PASS P124 scroll-shadow gate: 50000 randomized direct VRAM writes");
    return 0;
}
