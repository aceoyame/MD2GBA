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
 /* visible output only: each layer through its scroll registers (off-screen ring cells are history-dependent by design) */
 {volatile uint16_t*hofs=(volatile uint16_t*)0x04000010u;unsigned x,y;
  for(l=0;l<4;l++){unsigned sb=PROJ_SB_FIRST+PS->map_bank*4u+l;for(y=0;y<160;y++)for(x=0;x<240;x+=4){uint32_t w4=0;unsigned q;for(q=0;q<4;q++){unsigned px=(x+q+hofs[l*2])&255u,py=(y+hofs[l*2+1])&255u;uint16_t e=BG_VRAM16[sb*1024u+(py>>3)*32u+(px>>3)];unsigned slot=e&0x3ffu,tx=px&7u,ty=py&7u;if(e&0x400u)tx=7u-tx;if(e&0x800u)ty=7u-ty;w4=(w4<<8)|((volatile uint8_t*)0x06000000u)[slot*64u+ty*8u+tx];}h=fnvh(h,w4);}}
  (void)c;}
 h=fnvh(h,REG_BG0HOFS);h=fnvh(h,REG_BG0VOFS);h=fnvh(h,REG_BG1HOFS);h=fnvh(h,REG_BG1VOFS);h=fnvh(h,REG_BG2HOFS);h=fnvh(h,REG_BG2VOFS);h=fnvh(h,REG_BG3HOFS);h=fnvh(h,REG_BG3VOFS);
 for(k=0;k<64u;k++)h=fnvh(h,BG_PAL[k]);
 /* sprites by appearance, not by OBJ tile index (allocator history) */
 {volatile uint16_t*oam=(volatile uint16_t*)0x07000000u;const volatile uint8_t*ov=(const volatile uint8_t*)0x06010000u;unsigned sp;
  static const uint8_t SW[3][4]={{1,2,4,8},{2,4,4,8},{1,1,2,4}},SH[3][4]={{1,2,4,8},{1,1,2,4},{2,4,4,8}};
  for(sp=0;sp<128u;sp++){uint16_t a0=oam[sp*4],a1=oam[sp*4+1],a2=oam[sp*4+2];unsigned shape=(a0>>14)&3u,size=(a1>>14)&3u,bpp8=(a0>>13)&1u,w,hh,nb,t,b;
   if((a0&0x300u)==0x200u||shape==3u)continue;w=SW[shape][size];hh=SH[shape][size];
   h=fnvh(h,0x5E000000u|sp);h=fnvh(h,a0&~0u);h=fnvh(h,a1);h=fnvh(h,a2&0xFC00u);
   t=a2&0x3ffu;nb=w*hh*(bpp8?64u:32u);for(b=0;b<nb;b+=4)h=fnvh(h,*(const volatile uint32_t*)(ov+((t*32u+b)&0x7fffu)));}
  for(k=0;k<256u;k++)h=fnvh(h,((volatile uint16_t*)0x05000200u)[k]);}
 return h;}
static FILE*hashout;static FILE*csv;static unsigned refbad_frames;static uint8_t prevA[0x2000],prevAv;static uint8_t prevPat[0x10000];
typedef struct{unsigned a,b,m;}Hold;static Hold holds[4096];static unsigned nholds;
static unsigned btn(const char*t){unsigned m=0;for(;*t;t++){switch(*t){case'U':m|=MD2GBA_PAD_UP;break;case'D':m|=MD2GBA_PAD_DOWN;break;case'L':m|=MD2GBA_PAD_LEFT;break;case'R':m|=MD2GBA_PAD_RIGHT;break;case'A':m|=MD2GBA_PAD_A;break;case'B':m|=MD2GBA_PAD_B;break;case'C':m|=MD2GBA_PAD_C;break;case'S':m|=MD2GBA_PAD_START;break;}}return m;}
static unsigned shots[256],nshots;
static void shot(unsigned vbl){char fn[64];unsigned x,y,l;static const unsigned order[4]={3,2,1,0};FILE*o;{const char*sd=getenv("P137_SHOTDIR");sprintf(fn,"%s/shot_%05u.ppm",sd?sd:"/tmp/pb",vbl);}o=fopen(fn,"wb");if(!o)return;fprintf(o,"P6 240 160 255\n");
 volatile uint16_t*hofs=(volatile uint16_t*)0x04000010u;
 for(y=0;y<160;y++)for(x=0;x<240;x++){uint16_t c=BG_PAL[0];
  for(l=0;l<4;l++){unsigned L=order[l],sb=PROJ_SB_FIRST+PS->map_bank*4u+L;unsigned px=(x+hofs[L*2])&255u,py=(y+hofs[L*2+1])&255u;uint16_t e=BG_VRAM16[sb*1024u+(py>>3)*32u+(px>>3)];unsigned slot=e&0x3ffu,tx=px&7u,ty=py&7u;if(e&0x400u)tx=7u-tx;if(e&0x800u)ty=7u-ty;
   uint8_t ci=((volatile uint8_t*)0x06000000u)[slot*64u+ty*8u+tx];if(ci)c=BG_PAL[ci];}
  fputc((c&31)<<3,o);fputc(((c>>5)&31)<<3,o);fputc(((c>>10)&31)<<3,o);}
 fclose(o);}
static Md2GbaBus bus; static Md2Gba68k cpu; static Md2GbaTiming timing;
static unsigned dark,chk;static void ck(void*u){(void)u;chk++;if(!(REG_DISPCNT&0x1f00u))dark++;}
int main(int argc,char**argv){
    if(argc<3)return 2;
    void*m=mmap((void*)0x04000000,0x04000000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0);if(m==MAP_FAILED)return 99;
    FILE*f=fopen(argv[1],"rb");if(!f)return 3;uint8_t*rom=malloc(0x400000);size_t n=fread(rom,1,0x400000,f);fclose(f);
    unsigned frames=(unsigned)atoi(argv[2]);
    {const char*e=getenv("P137_INPUT");if(e){FILE*fi=fopen(e,"r");char t[32];unsigned a,b;while(fi&&fscanf(fi,"%u %u %31s",&a,&b,t)==3&&nholds<4096){holds[nholds].a=a;holds[nholds].b=b;holds[nholds].m=btn(t);nholds++;}if(fi)fclose(fi);}
     e=getenv("P137_SHOTS");if(e){const char*q=e;while(*q&&nshots<256){shots[nshots++]=(unsigned)strtoul(q,(char**)&q,10);while(*q==',')q++;}}
     e=getenv("P137_CSV");if(e)csv=fopen(e,"w");}int trace=argc>3&&argv[3][0]=='t';if(argc>3&&argv[3][0]=='h')hashout=fopen(argv[4],"w");
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
        if(nholds){unsigned m2=0,h2;for(h2=0;h2<nholds;h2++)if(last>=holds[h2].a&&last<holds[h2].b)m2|=holds[h2].m;bus.pad_held[0]=(uint16_t)m2;}
        else {unsigned k=last%240u;bus.pad_held[0]=(last>170&&(k==10||k==11||k==130||k==131))?MD2GBA_PAD_START:0;}
        st_frames++;
        int fade=bus.vdp.cram_epoch!=last_cram;last_cram=bus.vdp.cram_epoch;if(fade)st_fadeframes++;
        if(!(bus.vdp.regs[1]&0x40u)){st_off++;continue;}
        uint32_t complex=md2gba_vdp_complex_features(&bus.vdp);
        if(getenv("P137_DIAG")&&last>=350&&last<=372){uint16_t ab=md2gba_vdp_plane_a_base(&bus.vdp);unsigned i3,ch=0,x0=99,x1=0,y0=99,y1=0,pch=0,dblk=0,ndirtyA=0;
            for(i3=0;i3<0x2000;i3+=2){unsigned a=(ab+i3)&0xffff;if(prevAv&&(bus.vdp.vram[a]!=prevA[i3]||bus.vdp.vram[a+1]!=prevA[i3+1])){unsigned n=i3/2,x=n%64,y=n/64;ch++;if(x<x0)x0=x;if(x>x1)x1=x;if(y<y0)y0=y;if(y>y1)y1=y;}prevA[i3]=bus.vdp.vram[a];prevA[i3+1]=bus.vdp.vram[a+1];}
            for(i3=0;i3<0x10000;i3+=32){unsigned b=i3>>5;int d=!!(bus.vdp.vram_dirty_tiles[b>>5]&(1u<<(b&31)));int isA=((i3-ab)&0xffff)<0x2000;if(d&&isA)ndirtyA++;if(d&&!isA){dblk++;if(memcmp(prevPat+i3,(const void*)&bus.vdp.vram[i3],32))pch++;}memcpy(prevPat+i3,(const void*)&bus.vdp.vram[i3],32);}
            prevAv=1;printf("DIAG vbl %u planeA names changed=%u box x%u-%u y%u-%u | dirty A-name blocks=%u | dirty pattern blocks=%u of which bytes actually changed=%u\n",last,ch,x0,x1,y0,y1,ndirtyA,dblk,pch);}
        unsigned scan0=md2gba_projected_test_scan_cells,sc0=md2gba_projected_test_bg_source_checks,us0=md2gba_projected_test_ui_scans,rz0=md2gba_projected_test_ui_resizes,b0=md2gba_projected_test_bg_builds,d0=dark,why=0,ru0=md2gba_projected_test_bg_reuses,ip0=md2gba_projected_test_inplace_rebuilds,bank0=PS->map_bank;int pr=99;md2gba_projected_test_reason=0;
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
        if(renderer==3u&&!PS->pending&&PS->initialized){static uint16_t cnt[PROJ_TILE_SLOTS];unsigned l2,c2,s2,bad=0;memset(cnt,0,sizeof cnt);
            for(l2=0;l2<PROJ_LAYERS;l2++)for(c2=0;c2<PROJ_MAP_CELLS;c2++){unsigned q2=PS->active_slot[l2][c2];if(q2&&q2<PROJ_TILE_SLOTS)cnt[q2]++;}
            for(s2=1;s2<PROJ_TILE_SLOTS;s2++)if(cnt[s2]!=PS->slot_ref[s2])bad++;
            if(bad){refbad_frames++;if(refbad_frames<4)printf("REFCOUNT MISMATCH vbl %u slots=%u\n",last,bad);}}
        if(csv)fprintf(csv,"%u,%u,%u,%u,%u,%u,%u,%u,%d,%d,%u,%u,%u,%u,%u\n",last,md2gba_projected_test_bg_builds-b0,md2gba_projected_test_bg_reuses-ru0,md2gba_projected_test_inplace_rebuilds-ip0,PS->map_bank!=bank0,PS->map_write_count,PS->ui_count,md2gba_projected_test_reason,(int)PS->origin_x[0],(int)PS->origin_y[0],dark-d0,md2gba_projected_test_bg_source_checks-sc0,md2gba_projected_test_ui_scans-us0,md2gba_projected_test_ui_resizes-rz0,md2gba_projected_test_scan_cells-scan0);
        {unsigned q;for(q=0;q<nshots;q++)if(shots[q]==last)shot(last);}
        prevcomplex=complex;prevrend=renderer;
    }
    printf("REFCOUNT_MISMATCH_FRAMES=%u prebuilt=%u taken=%u\n",refbad_frames,md2gba_projected_test_pre_built,md2gba_projected_test_pre_taken);
    printf("SUMMARY frames=%u display_off=%u projected=%u legacy_complex=%u legacy_refused=%u projector_reentries(full reset+rebuild)=%u frames_with_dark_rebuild=%u transactional_bank_switches=%u pair_builds=%llu\n",
           st_frames,st_off,st_proj,st_legacy_complex,st_legacy_refuse,st_reentry,st_darkframes,st_trans,st_builds);
    printf("FADES cram_changing_frames=%u of_which_on_1:1=%u of_which_reentered_projector=%u\n",st_fadeframes,st_fade_legacy,st_fade_reentry);
    printf("UI_NAME_READS=%u\n",md2gba_projected_test_ui_name_reads);
    return 0;
}
