#include <stdint.h>
#include <stdio.h>
#include "md2gba_gba_backend.h"
void md2gba_gba_scroll_dma_set(const uint16_t stream[160][8],int enabled){(void)stream;(void)enabled;}
void md2gba_gba_scroll_latch_direct(const uint16_t scroll[8]){(void)scroll;}
#include "../md2gba_gba_backend.c"

static int req(int ok,const char*m){if(!ok){fprintf(stderr,"FAIL %s\n",m);return 0;}return 1;}
int main(void){int ok=1;
    ok&=req(md2gba_sprite_piece_visible(0,0,1,1),"origin visible");
    ok&=req(md2gba_sprite_piece_visible(-7,10,1,1),"partial left edge retained");
    ok&=req(!md2gba_sprite_piece_visible(-8,10,1,1),"fully left culled");
    ok&=req(md2gba_sprite_piece_visible(239,10,1,1),"partial right edge retained");
    ok&=req(!md2gba_sprite_piece_visible(240,10,1,1),"fully right culled");
    ok&=req(md2gba_sprite_piece_visible(10,-7,1,1),"partial top edge retained");
    ok&=req(!md2gba_sprite_piece_visible(10,-8,1,1),"fully top culled before OBJ Y wrap");
    ok&=req(md2gba_sprite_piece_visible(10,159,1,1),"partial bottom edge retained");
    ok&=req(!md2gba_sprite_piece_visible(10,160,1,1),"fully bottom culled before OBJ Y wrap");
    ok&=req(md2gba_sprite_piece_visible(-31,-31,4,4),"large partial corner retained");
    ok&=req(!md2gba_sprite_piece_visible(-32,-32,4,4),"large fully hidden corner culled");
    if(ok)puts("PASS P87 generic cropped-viewport sprite culling");return ok?0:1;
}
