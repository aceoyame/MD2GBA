#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "md2gba_timing.h"

static void put32be(uint8_t *p,unsigned off,uint32_t v){p[off]=(uint8_t)(v>>24);p[off+1]=(uint8_t)(v>>16);p[off+2]=(uint8_t)(v>>8);p[off+3]=(uint8_t)v;}

static void setup(Md2GbaTiming *t,Md2Gba68k *c,Md2GbaBus *b,uint8_t *rom,unsigned start,int vint){
    memset(rom,0,0x200); put32be(rom,0,0x00FFFFF0u); put32be(rom,0x78,0x00000100u);
    md2gba_bus_init(b,rom,0x200); md68k_reset_context(c); c->a[7]=0x00FFFFF0u; c->pc=0x000080u; c->sr=0x2000u;
    md2gba_timing_init(t,0,1); md2gba_timing_enable_cycles(t,262,224,488); md2gba_timing_enable_scheduled(t);
    t->scanline=(uint16_t)start; if(vint)b->vdp.regs[1]|=0x20u; else b->vdp.regs[1]&=(uint8_t)~0x20u;
    b->vdp.regs[0]&=(uint8_t)~0x10u;
}

static int same_guest(const Md2GbaTiming *a,const Md2GbaTiming *z,const Md2Gba68k *ca,const Md2Gba68k *cz,const Md2GbaBus *ba,const Md2GbaBus *bz){
    Md2GbaTiming x=*a,y=*z;
    x.idle_poll_hint=y.idle_poll_hint=0; x.idle_fast_forwards=y.idle_fast_forwards=0; x.idle_lines_advanced=y.idle_lines_advanced=0;
    if(memcmp(&x,&y,sizeof(x))||memcmp(ca,cz,sizeof(*ca))||memcmp(&ba->vdp,&bz->vdp,sizeof(ba->vdp)))return 0;
    for(unsigned q=0;q<65536u;q++) if(md2gba_read8((Md2GbaBus*)ba,0xff0000u+q)!=md2gba_read8((Md2GbaBus*)bz,0xff0000u+q))return 0;
    return 1;
}

int main(void){
    static const unsigned starts[]={0,1,37,159,223,224,225,250,261};
    for(unsigned vi=0;vi<2;vi++)for(unsigned si=0;si<sizeof(starts)/sizeof(starts[0]);si++){
        uint8_t ra[0x200],rb[0x200]; Md2GbaTiming ta,tb; Md2Gba68k ca,cb; Md2GbaBus ba,bb; int r1=0,r2;
        setup(&ta,&ca,&ba,ra,starts[si],vi); setup(&tb,&cb,&bb,rb,starts[si],vi);
        while(!r1){r1=md2gba_timing_scheduled_event(&ca,&ba,0,&ta); if(ta.hblanks>300u && !r1){printf("FAIL baseline failed to reach VBlank start=%u\n",starts[si]);return 1;}}
        tb.idle_poll_hint=1u; r2=md2gba_timing_scheduled_event(&cb,&bb,0,&tb);
        if(r1!=r2||!same_guest(&ta,&tb,&ca,&cb,&ba,&bb)){
            printf("FAIL batch equivalence start=%u vint=%u r=%d/%d line=%u/%u pc=%06x/%06x\n",starts[si],vi,r1,r2,ta.scanline,tb.scanline,ca.pc,cb.pc);return 1;
        }
        unsigned expected=(starts[si]<=224u)?(225u-starts[si]):((262u-starts[si])+225u);
        if(tb.idle_fast_forwards!=1u || tb.idle_lines_advanced!=expected){
            printf("FAIL diagnostics start=%u ff=%u lines=%u\n",starts[si],tb.idle_fast_forwards,tb.idle_lines_advanced);return 1;
        }
    }
    {
        uint8_t r[0x200];Md2GbaTiming t;Md2Gba68k c;Md2GbaBus b;setup(&t,&c,&b,r,10,0);b.vdp.regs[0]|=0x10u;t.idle_poll_hint=1u;
        if(md2gba_timing_scheduled_event(&c,&b,0,&t)!=0 || t.scanline!=11u || t.idle_fast_forwards!=0u || t.idle_poll_hint!=0u){puts("FAIL H-int guard");return 1;}
    }
    puts("PASS idle scheduler batching == repeated scanline scheduling; H-int guard exact");return 0;
}
