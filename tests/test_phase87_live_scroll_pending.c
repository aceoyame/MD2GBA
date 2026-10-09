#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include "md2gba_gba_backend.h"

static uint16_t got_scroll[8];
static unsigned got_count;

void md2gba_gba_scroll_dma_set(const uint16_t stream[160][8], int enabled){(void)stream;(void)enabled;}
void md2gba_gba_scroll_latch_direct(const uint16_t scroll[8]){
    if(scroll){memcpy(got_scroll,scroll,sizeof got_scroll);got_count++;}
}

static int map_io(void){
    void *p=mmap((void*)0x04000000u,0x10000u,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0);
    return p==MAP_FAILED ? -1 : 0;
}
static void put16be(Md2GbaVdp*v,unsigned a,uint16_t x){v->vram[(uint16_t)a]=(uint8_t)(x>>8);v->vram[(uint16_t)(a+1u)]=(uint8_t)x;}
static int eq8(const uint16_t*a,const uint16_t*b){return memcmp(a,b,8u*sizeof(uint16_t))==0;}

int main(void){
    Md2GbaVdp v; Md2GbaGbaFrameState s; uint16_t stream[160][8],expect[8];
    if(map_io()) { perror("mmap GBA IO"); return 2; }
    memset(&v,0,sizeof v); memset(&s,0,sizeof s); memset(stream,0,sizeof stream);
    s.initialized=1u; s.bg_cache.used_slots=1u;
    for(unsigned i=0;i<2048u;i++)s.bg_cache.source_to_slot[i]=0xffffu;
    for(unsigned i=0;i<1024u;i++)s.bg_cache.slot_to_source[i]=0xffffu;
    v.regs[2]=0x30u;v.regs[4]=7u;v.regs[5]=0x60u;v.regs[13]=0u;v.regs[16]=0u;v.regs[11]=0u;

    /* Publish a cheap palette transaction so the deferred VRAM queue is busy. */
    v.cram_dirty_mask[0]=1u; v.cram_gba[0]=0x1234u;
    if(md2gba_gba_commit_priority_incremental_viewport_planned(&v,&s,28u,29u,30u,31u,stream,0u,0u,MD2GBA_FRAME_PALETTE))return 3;
    if(!md2gba_gba_hw_commit_pending()){puts("FAIL did not create pending commit");return 4;}

    /* New camera state must bypass that pending visual transaction. */
    v.vsram[0]=(uint16_t)(int16_t)-7; v.vsram[1]=19u;
    put16be(&v,0u,12u); put16be(&v,2u,(uint16_t)(int16_t)-5);
    if(md2gba_gba_build_direct_scroll_viewport(&v,expect,40u,32u))return 5;
    got_count=0u;
    if(md2gba_gba_commit_priority_incremental_viewport_planned(&v,&s,28u,29u,30u,31u,stream,40u,32u,MD2GBA_FRAME_SCROLL))return 6;
    if(got_count!=1u || !eq8(got_scroll,expect)){puts("FAIL pending queue blocked live scroll publication");return 7;}
    if(!md2gba_gba_hw_commit_pending()){puts("FAIL camera publication consumed unrelated pending queue");return 8;}

    /* And another camera update must replace it immediately, still without
       requiring the old tile/palette transaction to complete first. */
    v.vsram[0]=41u;v.vsram[1]=(uint16_t)(int16_t)-9;put16be(&v,0u,(uint16_t)(int16_t)-17);put16be(&v,2u,23u);
    if(md2gba_gba_build_direct_scroll_viewport(&v,expect,8u,16u))return 9;
    if(md2gba_gba_commit_priority_incremental_viewport_planned(&v,&s,28u,29u,30u,31u,stream,8u,16u,MD2GBA_FRAME_SCROLL))return 10;
    if(got_count!=2u || !eq8(got_scroll,expect)){puts("FAIL newest live scroll did not supersede prior latch");return 11;}

    puts("PASS live whole-plane scroll bypasses pending VRAM queue and keeps newest camera state");
    return 0;
}
