#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "md2gba_gba_backend.h"
void md2gba_gba_scroll_dma_set(const uint16_t (*s)[8],int e){(void)s;(void)e;}
void md2gba_gba_scroll_latch_direct(const uint16_t s[8]){(void)s;}
#include "../md2gba_gba_backend.c"

int main(void){
    Md2GbaVdp v; Md2GbaGbaFrameState s; uint32_t p;
    memset(&v,0,sizeof v); memset(&s,0,sizeof s);

    /* A pure CRAM ramp must bypass geometry/map/sprite dependency work. */
    s.initialized=1u; s.last_vram_epoch=5u; s.last_vsram_epoch=7u;
    s.last_cram_epoch=10u; s.last_regs_epoch=9u;
    v.vram_epoch=5u; v.vsram_epoch=7u; v.cram_epoch=11u; v.regs_epoch=9u;
    p=md2gba_gba_plan_frame(&v,&s);
    if(p!=MD2GBA_FRAME_PALETTE){printf("FAIL palette plan %08x\n",(unsigned)p);return 1;}

    /* Positive and negative UI grammar results are stable across scroll-only
       presentation changes while the name table itself is unchanged. */
    v.regs[1]=0x08u; /* 240-line source raster: center=40, bottom=80. */
    s.ui_active=1u;
    if(md2gba_gba_suggest_view_y_cached(&v,&s,MD2GBA_FRAME_SCROLL)!=80u){puts("FAIL cached positive UI");return 2;}
    s.ui_active=0u;
    if(md2gba_gba_suggest_view_y_cached(&v,&s,MD2GBA_FRAME_SCROLL)!=40u){puts("FAIL cached negative UI");return 3;}

    /* Dirty source tiles that the current BG cache does not reference must not
       produce a tile upload.  The dependency shadow is a source-tile bitset. */
    memset(&v,0,sizeof v); memset(&s,0,sizeof s);
    s.initialized=1u; s.last_vram_epoch=20u; s.last_cram_epoch=1u;
    s.last_vsram_epoch=1u; s.last_regs_epoch=1u;
    v.vram_epoch=21u; v.cram_epoch=1u; v.vsram_epoch=1u; v.regs_epoch=1u;
    v.regs[16]=0u; /* 32x32 */
    v.regs[2]=0x30u; v.regs[4]=0x07u; v.regs[5]=0x7Eu; v.regs[13]=0x3Fu;
    s.bg_cache.used_slots=2u; s.bg_cache.slot_to_source[1]=1u;
    s.bg_cache.source_to_slot[1]=1u; s.bg_cache.source_mask[0]|=1u<<1;
    v.vram_dirty_tiles[0]|=1u<<10;
    p=md2gba_gba_plan_frame(&v,&s);
    if(p&MD2GBA_FRAME_TILES){printf("FAIL unrelated tile dirtied plan %08x\n",(unsigned)p);return 4;}
    v.vram_dirty_tiles[0]=1u<<1;
    p=md2gba_gba_plan_frame(&v,&s);
    if(!(p&MD2GBA_FRAME_TILES)){printf("FAIL referenced tile omitted %08x\n",(unsigned)p);return 5;}

    puts("PASS P105 presentation cache + dependency shadow");
    return 0;
}
