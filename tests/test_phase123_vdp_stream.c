#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "md2gba_bus.h"
static uint32_t s=0x9e3779b9u;static uint32_t rr(void){s=s*1664525u+1013904223u;return s;}
static uint32_t sw32(uint32_t x){return ((x&0xffu)<<24)|((x&0xff00u)<<8)|((x&0xff0000u)>>8)|((x>>24)&0xffu);}
int main(void){static Md2GbaBus a,b,before;static uint8_t rom[64];uint32_t vals[96],sw[96];unsigned ok=0;
 for(unsigned z=0;z<30000u;z++){
  md2gba_bus_init(&a,rom,sizeof rom);b=a;
  for(unsigned i=0;i<0x10000u;i++)a.vdp.vram[i]=b.vdp.vram[i]=(uint8_t)rr();
  a.vdp.target=b.vdp.target=MD2GBA_VDP_VRAM;a.vdp.regs[15]=b.vdp.regs[15]=2u;
  a.vdp.address=b.vdp.address=(uint16_t)((rr()%0xEE00u)&~3u);
  a.vdp.regs[11]=b.vdp.regs[11]=(uint8_t)rr();a.vdp.regs[13]=b.vdp.regs[13]=(uint8_t)rr();
  a.vdp.present_scroll_valid=b.vdp.present_scroll_valid=(uint8_t)rr();a.vdp.present_scroll_mode=b.vdp.present_scroll_mode=(uint8_t)rr();
  a.vdp.present_hscroll[0]=b.vdp.present_hscroll[0]=(int16_t)rr();a.vdp.present_hscroll[1]=b.vdp.present_hscroll[1]=(int16_t)rr();
  a.vdp.present_vscroll[0]=b.vdp.present_vscroll[0]=(int16_t)rr();a.vdp.present_vscroll[1]=b.vdp.present_vscroll[1]=(int16_t)rr();
  a.vdp.present_scroll_epoch=b.vdp.present_scroll_epoch=rr();a.vdp.vram_epoch=b.vdp.vram_epoch=rr();
  a.vdp.trace_count=b.vdp.trace_count=(uint8_t)(rr()%33u);a.vdp.code=b.vdp.code=(uint8_t)rr();a.vdp.pending_control=b.vdp.pending_control=(uint8_t)(rr()&1u);
  unsigned n=1u+(rr()%96u);if((unsigned)a.vdp.address+n*4u>0x10000u){z--;continue;}
  for(unsigned i=0;i<n;i++){vals[i]=rr();sw[i]=sw32(vals[i]);}
  if(!md2gba_vdp_data32_stream_aligned_ready(&a,n)){z--;continue;}
  for(unsigned i=0;i<n;i++)md2gba_write32(&b,0xC00000u,vals[i]);
  md2gba_vdp_data32_stream_preswapped_unchecked(&a,sw,n);
  if(memcmp(&a.vdp,&b.vdp,sizeof a.vdp)!=0){
   printf("FAIL z=%u n=%u addr=%04x epoch=%u/%u trace=%u/%u data=%04x/%04x\n",z,n,b.vdp.address,a.vdp.vram_epoch,b.vdp.vram_epoch,a.vdp.trace_count,b.vdp.trace_count,a.vdp.data_word,b.vdp.data_word);return 1;
  }ok++;
 }
 {Md2GbaBus q;md2gba_bus_init(&q,rom,sizeof rom);q.vdp.target=MD2GBA_VDP_CRAM;before=q;if(md2gba_vdp_data32_stream_aligned_ready(&q,1))return 2;if(memcmp(&q,&before,sizeof q))return 3;
  q=before;q.vdp.target=MD2GBA_VDP_VRAM;q.vdp.address=2;if(md2gba_vdp_data32_stream_aligned_ready(&q,1))return 4;
  q=before;q.vdp.target=MD2GBA_VDP_VRAM;q.vdp.address=0;q.vdp.regs[15]=4;if(md2gba_vdp_data32_stream_aligned_ready(&q,1))return 5;
 }
 printf("PASS P123 pre-swapped aligned VDP stream differential: %u states\n",ok);return 0;
}
