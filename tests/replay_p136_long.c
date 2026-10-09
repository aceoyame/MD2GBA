/* P136 replay diagnostic: PSIV on the canonical interpreter, presented at every
   guest VBlank through main_p85's exact renderer selection and the real
   md2gba_gba_projected.c (host VRAM/IO mapped with mmap).  Reports why and how
   often the fitted projector is abandoned/re-entered and how much it builds.
   usage: replay ROM FRAMES [trace] */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>
#define MD2GBA_PROJECTED_HOST_TEST 1
#define MD2GBA_PROFILE_PROJECTED_UI 1
extern uint16_t md2gba_bg_tile_stage[1024u*16u];
#include "../md2gba_gba_projected.c"
#include "md2gba_interp68k.h"
#include "md2gba_timing.h"
#include "md2gba_cycles.h"
#include "md2gba_vdp_soft.h"
#include "../md2gba_projected_retry.h"
void md2gba_gba_scroll_latch_clear(void){}
void md2gba_gba_scroll_dma_set(const void*a,unsigned b){(void)a;(void)b;}
void md2gba_gba_scroll_latch_direct(const void*a){(void)a;}

static uint64_t fnvh(uint64_t h,uint32_t x){h^=x;h*=1099511628211ull;return h;}
static uint64_t display_hash(void){uint64_t h=1469598103934665603ull;unsigned l,c,k;
 for(l=0;l<4;l++){unsigned sb=PROJ_SB_FIRST+PS->map_bank*4u+l;for(c=0;c<1024u;c++){uint16_t e=BG_VRAM16[sb*1024u+c];unsigned slot=e&0x3ffu;h=fnvh(h,(c<<16)|(e>>10));for(k=0;k<16u;k++)h=fnvh(h,BG_VRAM32[slot*16u+k]);}}
 h=fnvh(h,REG_BG0HOFS);h=fnvh(h,REG_BG0VOFS);h=fnvh(h,REG_BG1HOFS);h=fnvh(h,REG_BG1VOFS);h=fnvh(h,REG_BG2HOFS);h=fnvh(h,REG_BG2VOFS);h=fnvh(h,REG_BG3HOFS);h=fnvh(h,REG_BG3VOFS);
 for(k=0;k<64u;k++)h=fnvh(h,BG_PAL[k]);for(k=0;k<512u;k++)h=fnvh(h,((volatile uint16_t*)0x07000000u)[k]);return h;}
static FILE*hashout;
static Md2GbaBus bus; static Md2Gba68k cpu; static Md2GbaTiming timing;
static unsigned dark,chk;static void ck(void*u){(void)u;chk++;if(!(REG_DISPCNT&0x1f00u))dark++;}
int main(int argc,char**argv){
    if(argc<3)return 2;
    void*m=mmap((void*)0x04000000,0x04000000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0);if(m==MAP_FAILED)return 99;
    FILE*f=fopen(argv[1],"rb");if(!f)return 3;uint8_t*rom=malloc(0x400000);size_t n=fread(rom,1,0x400000,f);fclose(f);
    unsigned frames=(unsigned)atoi(argv[2]);int trace=argc>3&&argv[3][0]=='t';if(argc>3&&argv[3][0]=='h')hashout=fopen(argv[4],"w");
    md2gba_bus_init(&bus,rom,n);md68k_reset_context(&cpu);cpu.pc=0x220;cpu.a[7]=md2gba_read32(&bus,0);
    md2gba_timing_init(&timing,0,1);md2gba_timing_enable_cycles(&timing,262,224,488);
    md2gba_gba_projected_set_checkpoint(ck,0);
    Md2GbaProjectedRetry retry;memset(&retry,0,sizeof retry);
    unsigned renderer=0,last=0,prevcomplex=0,prevrend=0;unsigned long long guard=0;
    unsigned st_frames=0,st_off=0,st_proj=0,st_legacy_complex=0,st_legacy_refuse=0,st_reentry=0,st_darkframes=0,st_fadeframes=0,st_fade_legacy=0,st_fade_reentry=0,st_trans=0;
    unsigned long long st_builds=0;unsigned last_cram=bus.vdp.cram_epoch,lastbank=0;
    while(timing.vblanks<frames&&guard++<6000000000ull){
        uint32_t before=cpu.pc;if(md68k_interp_step(&cpu,&bus)!=1){fprintf(stderr,"interp stop %06X\n",before);break;}
        md2gba_timing_after_step(&cpu,&bus,before,(uint32_t)guard,&timing);
        if(timing.vblanks==last)continue;last=timing.vblanks;
        {unsigned keys=0u;if(last>=1800u&&(last%600u)<60u)keys=MD2GBA_PAD_START;else if(last>=660u&&(last%180u)<30u)keys=MD2GBA_PAD_A;else if(last>=4800u&&(last%600u)<300u)keys=MD2GBA_PAD_B;bus.pad_held[0]=keys;}
        st_frames++;
        int fade=bus.vdp.cram_epoch!=last_cram;last_cram=bus.vdp.cram_epoch;if(fade)st_fadeframes++;
        if(!(bus.vdp.regs[1]&0x40u)){st_off++;continue;}
        uint32_t complex=md2gba_vdp_complex_features(&bus.vdp);
        unsigned b0=md2gba_projected_test_bg_builds,d0=dark,why=0;int pr=99;
        unsigned dtiles=0,dpat=0,i2;for(i2=0;i2<64;i2++){dtiles+=__builtin_popcount(bus.vdp.vram_dirty_tiles[i2]);}
        {uint16_t ab=md2gba_vdp_plane_a_base(&bus.vdp),bb=md2gba_vdp_plane_b_base(&bus.vdp);for(i2=0;i2<2048;i2++){unsigned a=i2*32u;int isname=(a>=ab&&a<ab+0x2000u)||(a>=bb&&a<bb+0x2000u);if(!isname&&(bus.vdp.vram_dirty_tiles[i2>>5]&(1u<<(i2&31))))dpat++;}}
        if(complex==0u&&md2gba_projected_retry_ready(&retry,bus.vdp.regs,bus.vdp.vram_epoch,timing.vblanks)){
            if(renderer!=3u){md2gba_gba_projected_reset();renderer=3u;st_reentry++;if(fade)st_fade_reentry++;why=1;}
            pr=md2gba_gba_projected_present(&bus.vdp);
            md2gba_projected_retry_result(&retry,pr,bus.vdp.regs,bus.vdp.vram_epoch,timing.vblanks);
            if(pr==0){st_proj++;if(md2gba_gba_projected_pending())md2gba_gba_projected_vblank_commit_pending();
                if(PS->map_bank!=lastbank){st_trans++;lastbank=PS->map_bank;}}
            else{md2gba_gba_projected_reset();renderer=1u;st_legacy_refuse++;if(fade)st_fade_legacy++;why=2;}
        }else{renderer=1u;st_legacy_complex++;if(fade)st_fade_legacy++;why=3;}
        st_builds+=md2gba_projected_test_bg_builds-b0;if(dark!=d0)st_darkframes++;
        if(dark!=d0){unsigned used=0,k2;for(k2=1;k2<PROJ_TILE_SLOTS;k2++)if(PS->slot_ref[k2])used++;
            printf("DARK vbl %5u builds=%u dirty32B=%u dirty_patterns=%u slots_in_use_after=%u cram_changed=%d r7=%02x\n",last,md2gba_projected_test_bg_builds-b0,dtiles,dpat,used,fade,bus.vdp.regs[7]);}
        if(trace&&(why==1||why==2||complex!=prevcomplex||renderer!=prevrend||dark!=d0))
            printf("vbl %5u %s complex=%x pr=%d r1=%02x r11=%02x r12=%02x r17=%02x r18=%02x builds=%u dark=%u fade=%d\n",last,
                   why==1?"ENTER-PROJ(reset+full)":why==2?"PROJ-REFUSED->1:1":why==3?"COMPLEX->1:1":"proj",complex,pr,
                   bus.vdp.regs[1],bus.vdp.regs[11],bus.vdp.regs[12],bus.vdp.regs[17],bus.vdp.regs[18],md2gba_projected_test_bg_builds-b0,dark-d0,fade);
        if(hashout&&renderer==3u&&pr==0)fprintf(hashout,"%u %016llx\n",last,(unsigned long long)display_hash());
        prevcomplex=complex;prevrend=renderer;
    }
    printf("SUMMARY frames=%u display_off=%u projected=%u legacy_complex=%u legacy_refused=%u projector_reentries(full reset+rebuild)=%u frames_with_dark_rebuild=%u transactional_bank_switches=%u pair_builds=%llu\n",
           st_frames,st_off,st_proj,st_legacy_complex,st_legacy_refuse,st_reentry,st_darkframes,st_trans,st_builds);
    printf("FADES cram_changing_frames=%u of_which_on_1:1=%u of_which_reentered_projector=%u\n",st_fadeframes,st_fade_legacy,st_fade_reentry);
    return 0;
}
