#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "md2gba_gba_backend.h"
void md2gba_gba_scroll_dma_set(const uint16_t s[160][8],int e){(void)s;(void)e;}
void md2gba_gba_scroll_latch_direct(const uint16_t scroll[8]){(void)scroll;}
static void put(Md2GbaVdp*v,unsigned a,unsigned x){v->vram[(uint16_t)a]=x>>8;v->vram[(uint16_t)(a+1)]=x;}
int main(void){Md2GbaVdp v;Md2GbaGbaFrameState state;uint16_t stream[160][8];memset(&v,0,sizeof(v));v.regs[13]=0x3f;v.vsram[0]=17;v.vsram[1]=23;
 for(unsigned y=0;y<240;y++){put(&v,0xfc00+y*4,3*y+7);put(&v,0xfc02+y*4,5*y+11);}
 for(unsigned mode=0;mode<4;mode++)for(unsigned oy=0;oy<=80;oy+=8){v.regs[11]=mode;if(md2gba_gba_build_scroll_stream_viewport(&v,stream,40,oy))return 1;for(unsigned y=0;y<160;y++){unsigned sy=y+oy,idx=(mode==0||mode==1)?0:mode==2?(sy&~7):sy;if(stream[y][0]!=(uint16_t)(40-(3*idx+7))||stream[y][2]!=(uint16_t)(40-(5*idx+11))||stream[y][1]!=17+oy||stream[y][3]!=23+oy||memcmp(stream[y],stream[y]+4,8))return 2;}}
 {uint16_t d[8];v.regs[11]=0;v.vsram[0]=(uint16_t)(int16_t)-7;v.vsram[1]=19;put(&v,0xfc00,12);put(&v,0xfc02,(uint16_t)(int16_t)-5);if(md2gba_gba_build_direct_scroll_viewport(&v,d,40,32))return 5;if(d[0]!=(uint16_t)28||d[1]!=(uint16_t)25||d[2]!=(uint16_t)45||d[3]!=(uint16_t)51||memcmp(d,d+4,8))return 6;v.regs[11]=2;if(md2gba_gba_build_direct_scroll_viewport(&v,d,40,32)!=-2)return 7;}
 memset(&state,0,sizeof(state));state.initialized=1;state.bg_cache.used_slots=1;for(unsigned i=0;i<2048;i++)state.bg_cache.source_to_slot[i]=0xffff;for(unsigned i=0;i<1024;i++)state.bg_cache.slot_to_source[i]=0xffff;
 v.regs[2]=0x30;v.regs[4]=7;v.regs[5]=0x60;v.regs[16]=0;v.regs[11]=3;v.regs[1]=8;v.vram_epoch=1;v.vram_dirty_pages[7]=1u<<31;v.vram_dirty_tiles[63]=1u<<29;
 if(!(md2gba_gba_plan_frame(&v,&state)&MD2GBA_FRAME_SCROLL))return 3;state.last_vram_epoch=1;if(md2gba_gba_plan_frame(&v,&state))return 4;
 puts("PASS scroll modes 0/1/2/3, crop offsets, lower source rows and unchanged planner");return 0;}
