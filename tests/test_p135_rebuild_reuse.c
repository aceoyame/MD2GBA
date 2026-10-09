/* P135 exact rebuild carry-over oracle.
   Build twice: -DMD2GBA_PROJECTED_REBUILD_REUSE=1 and =0.  Both binaries run
   the identical randomized presentation script and print one hash of the
   *displayed* output (decoded pixels of all four hardware BG layers of the
   visible bank, scroll registers, DISPCNT, BG palette) per step.  The two
   streams must be identical; counters go to stderr. */
#define main inherited_oracle_main
#include "test_p134_projected_runtime.c"
#undef main

static uint64_t rs;
static uint32_t rnd(void){rs^=rs<<13;rs^=rs>>7;rs^=rs<<17;return (uint32_t)(rs>>11);}
static void markw(Md2GbaVdp*v,unsigned addr){unsigned b=(addr&0xffffu)>>5;v->vram_dirty_tiles[b>>5]|=1u<<(b&31u);}
static void set_name(Md2GbaVdp*v,unsigned base,unsigned idx,uint16_t n){namew(v,base,idx,n);markw(v,base+2u*idx);}
static void pattern(Md2GbaVdp*v,unsigned t){unsigned i,mode=rnd()%4u;for(i=0;i<32;i++){uint8_t b=mode==0?0:mode==1?(uint8_t)(rnd()&0x11u):(uint8_t)rnd();v->vram[t*32u+i]=b;}markw(v,t*32u);}
static uint64_t fnv(uint64_t h,uint32_t x){h^=x;h*=1099511628211ull;return h;}
static uint64_t display_hash(void){
    uint64_t h=1469598103934665603ull;unsigned l,c,k;
    for(l=0;l<4;l++){unsigned sb=PROJ_SB_FIRST+PS->map_bank*4u+l;
        for(c=0;c<1024u;c++){uint16_t e=BG_VRAM16[sb*1024u+c];unsigned slot=e&0x3ffu;h=fnv(h,(c<<16)|(e>>10));
            for(k=0;k<16u;k++)h=fnv(h,BG_VRAM32[slot*16u+k]);}}
    h=fnv(h,REG_BG0HOFS);h=fnv(h,REG_BG0VOFS);h=fnv(h,REG_BG1HOFS);h=fnv(h,REG_BG1VOFS);h=fnv(h,REG_BG2HOFS);h=fnv(h,REG_BG2VOFS);h=fnv(h,REG_BG3HOFS);h=fnv(h,REG_BG3VOFS);
    h=fnv(h,REG_DISPCNT);for(k=0;k<64u;k++)h=fnv(h,BG_PAL[k]);return h;
}
static void settle(Md2GbaVdp*v){unsigned i;for(i=0;i<3;i++){md2gba_gba_projected_present(v);if(md2gba_gba_projected_pending())md2gba_gba_projected_vblank_commit_pending();}}
static uint16_t saved[64*32];
static unsigned psx,psy,pw,ph,panel_open;
static void panel(Md2GbaVdp*v,int open){
    unsigned x,y;
    if(open){pw=6+rnd()%18;ph=4+rnd()%8;psx=rnd()%(64-pw);psy=rnd()%(32-ph);}
    for(y=0;y<ph;y++)for(x=0;x<pw;x++){
        unsigned idx=(psy+y)*64+psx+x;uint16_t n=7;
        if(y==0||y==ph-1)n=(x==0?0x6e9:x==pw-1?0xee9:0x6ea)|(y?0x1000:0);else if(x==0||x==pw-1)n=x?0xef3:0x6f3;
        if(open){saved[idx]=(uint16_t)((v->vram[(uint16_t)(0x4000+2*idx)]<<8)|v->vram[(uint16_t)(0x4000+2*idx+1)]);set_name(v,0x4000,idx,n);}
        else set_name(v,0x4000,idx,saved[idx]);
    }panel_open=open;
}
static uint16_t rname(void){unsigned t=rnd()%7u;uint16_t n=(uint16_t)t;if(rnd()%8==0)n|=0x8000u;if(rnd()%16==0)n|=0x0800u;if(rnd()%16==0)n|=0x1000u;n|=(uint16_t)((rnd()%2)<<13);return n;}
/* structured map: 4x4-cell metatiles from a small tile set, like real field maps */
static uint16_t mapname(unsigned x,unsigned y,unsigned salt){unsigned mt=((x>>2)*7u+(y>>2)*13u+salt)%5u;uint16_t n=(uint16_t)(1u+((mt+(x&1u)+(y&1u))%6u));if(mt==4u)n=0;if(mt==3u)n|=0x8000u;return n;}
#ifndef P135_NO_MAIN
int main(int argc,char**argv){
    static Md2GbaVdp v;unsigned scen,step,i,trans=0,steps=0,lastbank;
    unsigned nscen=argc>1?(unsigned)atoi(argv[1]):40u,nstep=argc>2?(unsigned)atoi(argv[2]):60u;
    void*m=mmap((void*)0x04000000,0x04000000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0);if(m==MAP_FAILED)return 99;
    for(scen=0;scen<nscen;scen++){
        rs=0x9E3779B97F4A7C15ull*(scen+1u);
        md2gba_gba_projected_reset();memset(&v,0,sizeof v);
        v.regs[2]=0x10;v.regs[4]=3;v.regs[5]=0x38;v.regs[16]=1;v.regs[12]=0x81;v.regs[1]=0x40;v.present_scroll_valid=1;
        tile(&v,0x6e9,2);tile(&v,0x6ea,3);tile(&v,0x6f3,4);tile(&v,7,5);
        for(i=0;i<24;i++)pattern(&v,i);tile(&v,0,0);
        for(i=0;i<2048;i++){namew(&v,0x4000,i,mapname(i%64u,i/64u,1u));namew(&v,0x6000,i,mapname(i%64u,i/64u,3u));}
        for(i=0;i<64;i++)v.cram_gba[i]=(uint16_t)(i*977u);
        v.present_hscroll[0]=(int16_t)(rnd()%512);v.present_vscroll[0]=(int16_t)(rnd()%256);v.present_hscroll[1]=(int16_t)(rnd()%512);v.present_vscroll[1]=(int16_t)(rnd()%256);
        memset(v.vram_dirty_tiles,0,sizeof v.vram_dirty_tiles);panel_open=0;
        settle(&v);lastbank=PS->map_bank;
        for(step=0;step<nstep;step++){
            unsigned op=rnd()%10u,x,y,p;
            if(op==0||op==1){pattern(&v,rnd()%24u);}
            else if(op==2){p=rnd()%2u;unsigned w=1+rnd()%4,h=1+rnd()%4,sx=rnd()%60,sy=rnd()%28;for(y=0;y<h;y++)for(x=0;x<w;x++)set_name(&v,p?0x6000:0x4000,(sy+y)*64+sx+x,rname());}
            else if(op==3){p=rnd()%2u;unsigned sy=rnd()%8,salt=rnd()%9u;for(y=0;y<24;y++)for(x=0;x<40;x++)set_name(&v,p?0x6000:0x4000,((sy+y)&31)*64+x,mapname(x,y,salt));}
            else if(op==4||op==5){if(panel_open)panel(&v,0);else panel(&v,1);}
            else if(op==6){p=rnd()%2u;v.present_hscroll[p]=(int16_t)(v.present_hscroll[p]+(int)(rnd()%200)-100);v.present_vscroll[p]=(int16_t)(v.present_vscroll[p]+(int)(rnd()%120)-60);v.present_scroll_epoch++;}
            else if(op==7){p=rnd()%2u;v.present_hscroll[p]=(int16_t)(v.present_hscroll[p]+(int)(rnd()%5)-2);v.present_vscroll[p]=(int16_t)(v.present_vscroll[p]+(int)(rnd()%5)-2);v.present_scroll_epoch++;}
            else if(op==8){v.cram_gba[rnd()%64]=(uint16_t)rnd();v.cram_epoch++;}
            else {unsigned t=rnd()%24u;for(i=0;i<32;i++)v.vram[t*32u+i]^=0;markw(&v,t*32u);} /* dirty but unchanged */
            v.vram_epoch++;settle(&v);
            if(PS->map_bank!=lastbank){trans++;lastbank=PS->map_bank;}
            printf("%u %u %016llx\n",scen,step,(unsigned long long)display_hash());steps++;
        }
    }
    fprintf(stderr,"REUSE=%d steps=%u bank_switch_rebuilds=%u pair_builds=%u pair_reuses=%u\n",MD2GBA_PROJECTED_REBUILD_REUSE,steps,trans,md2gba_projected_test_bg_builds,md2gba_projected_test_bg_reuses);
    return 0;
}
#endif
