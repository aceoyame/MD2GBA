#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "md2gba_bus.h"
static uint32_t s=0x12345678u;static uint32_t rr(void){s=s*1664525u+1013904223u;return s;}
int main(void){static Md2GbaBus a,b;static uint8_t rom[64];uint32_t vals[64];unsigned ok=0,refuse=0;
 for(unsigned z=0;z<20000u;z++){
  md2gba_bus_init(&a,rom,sizeof rom);b=a;
  for(unsigned i=0;i<0x10000u;i++)a.vdp.vram[i]=b.vdp.vram[i]=(uint8_t)rr();
  a.vdp.target=b.vdp.target=MD2GBA_VDP_VRAM;a.vdp.regs[15]=b.vdp.regs[15]=2u;
  a.vdp.address=b.vdp.address=(uint16_t)((rr()%0xF000u)&~1u);
  a.vdp.regs[11]=b.vdp.regs[11]=(uint8_t)rr();a.vdp.regs[13]=b.vdp.regs[13]=(uint8_t)rr();
  a.vdp.present_scroll_valid=b.vdp.present_scroll_valid=(uint8_t)rr();a.vdp.present_scroll_mode=b.vdp.present_scroll_mode=(uint8_t)rr();
  a.vdp.present_hscroll[0]=b.vdp.present_hscroll[0]=(int16_t)rr();a.vdp.present_hscroll[1]=b.vdp.present_hscroll[1]=(int16_t)rr();
  a.vdp.present_vscroll[0]=b.vdp.present_vscroll[0]=(int16_t)rr();a.vdp.present_vscroll[1]=b.vdp.present_vscroll[1]=(int16_t)rr();
  a.vdp.present_scroll_epoch=b.vdp.present_scroll_epoch=rr();
  a.vdp.trace_count=b.vdp.trace_count=(uint8_t)(rr()%33u);a.vdp.code=b.vdp.code=(uint8_t)rr();a.vdp.pending_control=b.vdp.pending_control=(uint8_t)(rr()&1u);
  unsigned n=1u+(rr()%64u);if((unsigned)a.vdp.address+n*4u>0x10000u){z--;continue;}for(unsigned i=0;i<n;i++)vals[i]=rr();
  if(!md2gba_vdp_data32_stream_fast_ready(&a,n)){refuse++;continue;}
  for(unsigned i=0;i<n;i++)md2gba_write32(&b,0xC00000u,vals[i]);
  if(!md2gba_vdp_data32_stream_fast(&a,vals,n)){puts("FAIL accepted then refused");return 1;}
  if(memcmp(&a.vdp,&b.vdp,sizeof a.vdp)!=0){
   printf("FAIL z=%u n=%u addr=%04x r11=%02x r13=%02x epochs %u/%u pse %u/%u trace %u/%u\n",z,n,b.vdp.address,a.vdp.regs[11],a.vdp.regs[13],a.vdp.vram_epoch,b.vdp.vram_epoch,a.vdp.present_scroll_epoch,b.vdp.present_scroll_epoch,a.vdp.trace_count,b.vdp.trace_count);return 1;
  }ok++;
 }
 {Md2GbaBus q;md2gba_bus_init(&q,rom,sizeof rom);uint32_t v=0x12345678u;q.vdp.target=MD2GBA_VDP_CRAM;if(md2gba_vdp_data32_stream_fast_ready(&q,1))return 3;q.vdp.target=MD2GBA_VDP_VRAM;q.vdp.dma_fill_pending=1;if(md2gba_vdp_data32_stream_fast_ready(&q,1))return 4;q.vdp.dma_fill_pending=0;q.vdp.regs[15]=4;if(md2gba_vdp_data32_stream_fast_ready(&q,1))return 5;q.vdp.regs[15]=2;q.vdp.address=1;if(md2gba_vdp_data32_stream_fast_ready(&q,1))return 6;q.vdp.address=0xfffe;if(md2gba_vdp_data32_stream_fast_ready(&q,1))return 7;q.vdp.address=0;q.vdp.regs[11]=0;q.vdp.regs[13]=0;if(md2gba_vdp_data32_stream_fast_ready(&q,1))return 8;(void)v;}
 printf("PASS P122 VDP stream differential: %u accepted + refusal side-effect gates\n",ok);return 0;}
