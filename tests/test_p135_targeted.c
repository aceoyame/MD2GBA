/* P135: pair-synthesis cost of the specific user-reported slow events. */
#define P135_NO_MAIN 1
#include "test_p135_rebuild_reuse.c"
static Md2GbaVdp V;
static void base_scene(void){unsigned i;rs=77;md2gba_gba_projected_reset();memset(&V,0,sizeof V);
 V.regs[2]=0x10;V.regs[4]=3;V.regs[5]=0x38;V.regs[16]=1;V.regs[12]=0x81;V.regs[1]=0x40;V.present_scroll_valid=1;
 tile(&V,0x6e9,2);tile(&V,0x6ea,3);tile(&V,0x6f3,4);tile(&V,7,5);for(i=0;i<24;i++)pattern(&V,i);tile(&V,0,0);
 for(i=0;i<2048;i++){namew(&V,0x4000,i,mapname(i%64u,i/64u,1u));namew(&V,0x6000,i,mapname(i%64u,i/64u,3u));}
 for(i=0;i<64;i++)V.cram_gba[i]=(uint16_t)(i*977u);memset(V.vram_dirty_tiles,0,sizeof V.vram_dirty_tiles);settle(&V);}
static unsigned b0,r0;static void mark(void){b0=md2gba_projected_test_bg_builds;r0=md2gba_projected_test_bg_reuses;}
static void report(const char*what){printf("%-46s pair builds %5u  reuses %5u\n",what,md2gba_projected_test_bg_builds-b0,md2gba_projected_test_bg_reuses-r0);}
static void openpanel(unsigned sx,unsigned sy,unsigned w,unsigned h){psx=sx;psy=sy;pw=w;ph=h;unsigned x,y;for(y=0;y<ph;y++)for(x=0;x<pw;x++){unsigned idx=(psy+y)*64+psx+x;uint16_t n=7;
 if(y==0||y==ph-1)n=(x==0?0x6e9:x==pw-1?0xee9:0x6ea)|(y?0x1000:0);else if(x==0||x==pw-1)n=x?0xef3:0x6f3;
 saved[idx]=(uint16_t)((V.vram[(uint16_t)(0x4000+2*idx)]<<8)|V.vram[(uint16_t)(0x4000+2*idx+1)]);set_name(&V,0x4000,idx,n);}panel_open=1;V.vram_epoch++;}
int main(void){
 void*m=mmap((void*)0x04000000,0x04000000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0);if(m==MAP_FAILED)return 99;
 unsigned x,y;
 base_scene();mark();openpanel(4,18,30,8);settle(&V);report("dialogue window appears (30x8)");
 mark();panel(&V,0);V.vram_epoch++;settle(&V);report("dialogue window disappears");
 mark();openpanel(4,18,30,8);settle(&V);openpanel(20,4,12,10);settle(&V);report("second window opens while first is up");
 mark();V.present_vscroll[0]+=24;V.present_scroll_epoch++;settle(&V);report("camera moves 3 cells with windows up");
 base_scene();mark();for(y=4;y<18;y++)for(x=2;x<40;x++)set_name(&V,0x4000,y*64+x,0);V.vram_epoch++;settle(&V);report("title logo vanishes (38x14 names cleared)");
 base_scene();mark();V.present_hscroll[1]-=100;V.present_vscroll[1]+=60;V.present_scroll_epoch++;settle(&V);report("background jumps 100x60 px");
 base_scene();mark();for(y=0;y<28;y++)for(x=0;x<40;x++)set_name(&V,0x6000,y*64+x,mapname(x,y,5));V.vram_epoch++;settle(&V);report("full-screen picture swap (worst case)");
 return 0;}
