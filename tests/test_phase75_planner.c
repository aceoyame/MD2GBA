#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "md2gba_gba_backend.h"

int md2gba_vdp_vram_range_dirty(const Md2GbaVdp *v,uint16_t start,unsigned length){
    unsigned i,first=start>>8,last=((uint16_t)(start+length-1u))>>8;
    for(i=first;i<=last;i++)if(v->vram_dirty_pages[i>>5]&(1u<<(i&31u)))return 1;
    return 0;
}
void md2gba_gba_scroll_dma_set(const uint16_t stream[160][8],int enabled){(void)stream;(void)enabled;}
void md2gba_gba_scroll_latch_direct(const uint16_t scroll[8]){(void)scroll;}

static void cache_init_public(Md2GbaBgTileCache*c){
    unsigned i;for(i=0;i<2048;i++)c->source_to_slot[i]=0xFFFFu;
    for(i=0;i<1024;i++)c->slot_to_source[i]=0xFFFFu;
    c->used_slots=1;
}
static void set_name(Md2GbaVdp*v,unsigned a,unsigned tile){v->vram[a]=(uint8_t)(tile>>8);v->vram[a+1]=(uint8_t)tile;}
static void dirty32(Md2GbaVdp*v,unsigned a){unsigned b=((uint16_t)a)>>5;v->vram_dirty_tiles[b>>5]|=1u<<(b&31u);v->vram_dirty_pages[((uint16_t)a)>>13]|=1u<<((((uint16_t)a)>>8)&31u);}
static void base(Md2GbaVdp*v,Md2GbaGbaFrameState*s){
    memset(v,0,sizeof(*v));memset(s,0,sizeof(*s));
    v->regs[2]=0x30; /* A = C000 */ v->regs[4]=7; /* B = E000 */ v->regs[16]=0; /* 32x32 */
    v->vram_epoch=2;s->last_vram_epoch=1;s->initialized=1;cache_init_public(&s->bg_cache);
    s->bg_cache.source_to_slot[0]=1;s->bg_cache.slot_to_source[1]=0;s->bg_cache.used_slots=2;
}
static int expect(const char*n,uint32_t p,uint32_t set,uint32_t clear){
    if((p&set)!=set || (p&clear)){fprintf(stderr,"%s: plan=%02X wanted set=%02X clear=%02X\n",n,(unsigned)p,(unsigned)set,(unsigned)clear);return 1;}return 0;
}
int main(void){Md2GbaVdp v;Md2GbaGbaFrameState s;uint32_t p;unsigned i;int fail=0;
    base(&v,&s);set_name(&v,0xC000,5);dirty32(&v,0xC000);p=md2gba_gba_plan_frame(&v,&s);
    fail|=expect("one-new-glyph",p,MD2GBA_FRAME_MAP_PATCH,MD2GBA_FRAME_MAPS|MD2GBA_FRAME_FULL);

    base(&v,&s);s.bg_cache.source_to_slot[5]=2;s.bg_cache.slot_to_source[2]=5;s.bg_cache.used_slots=3;set_name(&v,0xC000,5);dirty32(&v,0xC000);dirty32(&v,5*32u);p=md2gba_gba_plan_frame(&v,&s);
    fail|=expect("map-plus-pattern",p,MD2GBA_FRAME_MAP_PATCH|MD2GBA_FRAME_TILES,MD2GBA_FRAME_MAPS|MD2GBA_FRAME_FULL);

    base(&v,&s);for(i=0;i<33;i++){set_name(&v,0xC000+i*32u,(i+1)&0x7FFu);dirty32(&v,0xC000+i*32u);}p=md2gba_gba_plan_frame(&v,&s);
    fail|=expect("large-map-change",p,MD2GBA_FRAME_MAPS|MD2GBA_FRAME_TILES,MD2GBA_FRAME_MAP_PATCH);

    base(&v,&s);s.bg_cache.used_slots=1024;for(i=1;i<1024;i++){s.bg_cache.slot_to_source[i]=(uint16_t)(i-1);s.bg_cache.source_to_slot[i-1]=(uint16_t)i;}set_name(&v,0xC000,1500);dirty32(&v,0xC000);p=md2gba_gba_plan_frame(&v,&s);
    fail|=expect("cache-full",p,MD2GBA_FRAME_MAPS|MD2GBA_FRAME_TILES,MD2GBA_FRAME_MAP_PATCH);

    base(&v,&s);s.bg_cache.source_to_slot[5]=2;s.bg_cache.slot_to_source[2]=5;s.bg_cache.used_slots=3;dirty32(&v,5*32u);p=md2gba_gba_plan_frame(&v,&s);
    fail|=expect("pattern-only",p,MD2GBA_FRAME_TILES,MD2GBA_FRAME_MAP_PATCH|MD2GBA_FRAME_MAPS);
    if(!fail)puts("Phase75 planner tests: PASS");return fail;
}
