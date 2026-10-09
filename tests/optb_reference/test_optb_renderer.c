#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../../md2gba_gba_backend.c"
void md2gba_gba_scroll_dma_set(const uint16_t (*p)[8],int e){(void)p;(void)e;}
void md2gba_gba_scroll_latch_direct(const uint16_t p[8]){(void)p;}
#include "opta_sparse_reference.inc"
static Md2GbaVdp v;
static Md2GbaBgTileCache cache;
static uint16_t stage_ref[16384],queue_ref[256],queue_got[256];
static uint32_t state=0xB777;
static unsigned rnd(void){state=state*1664525u+1013904223u;return state>>6;}
static int cmp16(const void*a,const void*b){return (int)*(const uint16_t*)a-(int)*(const uint16_t*)b;}
int main(void){
 for(unsigned bit=0;bit<32;bit++)if(md2gba_lowbit_index(1u<<bit)!=bit)return 1;
 for(unsigned i=0;i<65536;i++)v.vram[i]=(uint8_t)rnd();
 for(unsigned test=0;test<2500;test++){
  cache_init(&cache);unsigned want=1+rnd()%1023;
  while(cache.used_slots<want)cache_add(&cache,rnd()%2048);
  memset(v.vram_dirty_tiles,0,sizeof v.vram_dirty_tiles);
  unsigned dirty=(test%4==0)?2048:(test%4==1)?128:1+rnd()%8;
  for(unsigned i=0;i<dirty;i++){unsigned t=rnd()%2048;v.vram_dirty_tiles[t>>5]|=1u<<(t&31);}
  if(test%13==0){memset(cache.source_mask,0,sizeof cache.source_mask);cache.source_mask_valid=0u;}
  memset(md2gba_bg_tile_stage,0x6c,sizeof md2gba_bg_tile_stage);
  unsigned nr=opta_md2gba_gba_stage_dirty_cached_tiles(&v,&cache);
  memcpy(stage_ref,md2gba_bg_tile_stage,sizeof stage_ref);
  memset(md2gba_bg_tile_stage,0x6c,sizeof md2gba_bg_tile_stage);
  unsigned ng=md2gba_gba_stage_dirty_cached_tiles(&v,&cache);
  if(nr!=ng||memcmp(stage_ref,md2gba_bg_tile_stage,sizeof stage_ref)){printf("FAIL staging %u\n",test);return 2;}
  unsigned old=rnd()%(cache.used_slots+1u),patch=test&1,initial=(test%5==0)?200:0;
  md2gba_hwq.tile_count=initial;int rr=opta_md2gba_hwq_add_tiles(&v,&cache,old,patch);unsigned rc=md2gba_hwq.tile_count-initial;
  memcpy(queue_ref,md2gba_hwq.tile_slot+initial,rc*2);
  md2gba_hwq.tile_count=initial;int gr=md2gba_hwq_add_tiles(&v,&cache,old,patch);unsigned gc=md2gba_hwq.tile_count-initial;
  memcpy(queue_got,md2gba_hwq.tile_slot+initial,gc*2);
  if(rr!=gr){printf("FAIL queue status %u\n",test);return 3;}
  if(!rr){qsort(queue_ref,rc,2,cmp16);qsort(queue_got,gc,2,cmp16);if(rc!=gc||memcmp(queue_ref,queue_got,rc*2)){printf("FAIL queue set %u old=%u patch=%u\n",test,old,patch);return 4;}}
 }
 puts("PASS OPT-B sparse renderer: 2,500 cache/dirty configurations; exact staging bytes, queued tile sets, capacity refusal and legacy fallback");
 return 0;
}
