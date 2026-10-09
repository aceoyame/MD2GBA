#include <stdio.h>
#include <string.h>
#include "../../md2gba_gba_backend.c"
void md2gba_gba_scroll_dma_set(const uint16_t (*p)[8],int e){(void)p;(void)e;}
void md2gba_gba_scroll_latch_direct(const uint16_t p[8]){(void)p;}
static Md2GbaVdp v;
static unsigned seed=0xB5;
static unsigned rnd(void){seed=seed*1664525u+1013904223u;return seed>>4;}
static void word(unsigned a,unsigned x){v.vram[(uint16_t)a]=(uint8_t)(x>>8);v.vram[(uint16_t)(a+1)]=(uint8_t)x;}
static unsigned oracle(uint16_t*out){
 unsigned used[2048]={0},n=0,entry=0,base=md2gba_vdp_sat_base(&v);
 for(unsigned g=0;g<80;g++){
  unsigned a=(base+8*entry)&65535,sz=v.vram[(uint16_t)(a+2)],t=vram_word(&v,a+4)&2047;
  for(unsigned x=0;x<((sz>>2)&3)+1;x++)for(unsigned y=0;y<(sz&3)+1;y++){
   unsigned physical=((t+x*((sz&3)+1)+y)*32)&65535,src=physical/32;
   if(!used[src]){used[src]=1;out[n++]=(uint16_t)src;}
  }
  entry=v.vram[(uint16_t)(a+3)]&127;if(!entry)break;
 }
 return n;
}
int main(void){
 uint16_t expected[1280],guarded[130];
 for(unsigned it=0;it<2500;it++){
  memset(&v,0,sizeof v);v.regs[5]=0x70;unsigned base=md2gba_vdp_sat_base(&v),sprites=1+rnd()%80;
  for(unsigned i=0;i<sprites;i++){
   unsigned a=base+i*8,t=(it%5==0)?2040+rnd()%8:rnd()%2048;
   v.vram[(uint16_t)(a+2)]=(uint8_t)rnd();v.vram[(uint16_t)(a+3)]=(uint8_t)((i+1<sprites)?i+1:0);
   word(a+4,t);
  }
  if(it%9==0&&sprites>2)v.vram[(uint16_t)(base+(sprites-1)*8+3)]=1;
  unsigned n=oracle(expected),cap=it%129;
  for(unsigned i=0;i<130;i++)guarded[i]=0xBEEF;
  unsigned got=md2gba_vdp_collect_sprite_tiles(&v,guarded+1,cap);
  if(got!=n||guarded[0]!=0xBEEF||guarded[cap+1]!=0xBEEF||memcmp(guarded+1,expected,(n<cap?n:cap)*2)){printf("FAIL sprite %u n=%u/%u cap=%u\n",it,got,n,cap);return 1;}
  if(md2gba_vdp_collect_sprite_tiles(&v,0,0)!=n)return 2;
 }
 Md2GbaGbaFrameState s={0};s.sprite_source_count=129;
 memset(v.vram_dirty_tiles,0,sizeof v.vram_dirty_tiles);
 if(sprite_sources_dirty(&v,&s))return 3;
 v.vram_dirty_tiles[63]=1u<<31;if(!sprite_sources_dirty(&v,&s))return 4;
 puts("PASS OPT-B sprite references: 2,500 SAT chains; duplicates, capacity 0..128, null output, tile wrap, cyclic links and overflow invalidation");
}
