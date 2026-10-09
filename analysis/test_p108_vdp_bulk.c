#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "md2gba_bus.h"
static unsigned seed=0x1086aa98u;
static unsigned rnd(void){seed=seed*1664525u+1013904223u;return seed;}
static int fail(unsigned t,const char*why){printf("FAIL t=%u %s\n",t,why);return 1;}
int main(void){
  static uint8_t rom[0x1000];
  for(unsigned t=0;t<30000u;t++){
    Md2GbaBus a,b;md2gba_bus_init(&a,rom,sizeof(rom));md2gba_bus_init(&b,rom,sizeof(rom));
    unsigned count=1u+(rnd()%34u);uint16_t value=(uint16_t)rnd();uint16_t addr=(uint16_t)((rnd()%(0x10000u-count*2u))&0xFFFEu);
    a.vdp.target=b.vdp.target=MD2GBA_VDP_VRAM;a.vdp.address=b.vdp.address=addr;
    a.vdp.regs[15]=b.vdp.regs[15]=(rnd()&1u)?2u:0u;a.vdp.code=b.vdp.code=(uint8_t)rnd();
    a.vdp.pending_control=b.vdp.pending_control=(uint8_t)(rnd()&1u);
    a.vdp.regs[11]=b.vdp.regs[11]=(uint8_t)rnd();a.vdp.regs[13]=b.vdp.regs[13]=(uint8_t)rnd();
    a.vdp.present_scroll_valid=b.vdp.present_scroll_valid=(uint8_t)(rnd()&1u);
    a.vdp.present_scroll_mode=b.vdp.present_scroll_mode=(uint8_t)rnd();
    a.vdp.present_hscroll[0]=b.vdp.present_hscroll[0]=(int16_t)rnd();a.vdp.present_hscroll[1]=b.vdp.present_hscroll[1]=(int16_t)rnd();
    a.vdp.present_vscroll[0]=b.vdp.present_vscroll[0]=(int16_t)rnd();a.vdp.present_vscroll[1]=b.vdp.present_vscroll[1]=(int16_t)rnd();
    a.vdp.present_scroll_epoch=b.vdp.present_scroll_epoch=rnd();
    a.vdp.vsram[0]=b.vdp.vsram[0]=(uint16_t)rnd();a.vdp.vsram[1]=b.vdp.vsram[1]=(uint16_t)rnd();
    a.vdp.vram_epoch=b.vdp.vram_epoch=rnd();a.vdp.data_writes=b.vdp.data_writes=rnd();
    a.vdp.trace_count=b.vdp.trace_count=(uint8_t)(rnd()%33u);
    for(unsigned i=0;i<0x10000u;i++){uint8_t q=(uint8_t)rnd();a.vdp.vram[i]=b.vdp.vram[i]=q;}
    /* Ensure a mix of changed/unchanged words. */
    for(unsigned i=0;i<count;i++)if((i&3u)==0u){a.vdp.vram[addr+i*2u]=(uint8_t)(value>>8);b.vdp.vram[addr+i*2u]=(uint8_t)(value>>8);a.vdp.vram[addr+i*2u+1u]=(uint8_t)value;b.vdp.vram[addr+i*2u+1u]=(uint8_t)value;}
    if(!md2gba_vdp_data16_repeat_fast(&a,count,value))return fail(t,"helper refused accepted state");
    for(unsigned i=0;i<count;i++)md2gba_vdp_data16_direct_vdp(&b.vdp,value);
    if(memcmp(&a.vdp,&b.vdp,sizeof(a.vdp)))return fail(t,"VDP state mismatch");
  }
  /* Refusal must be side-effect free for unsupported VDP modes. */
  for(unsigned k=0;k<6u;k++){
    Md2GbaBus a;md2gba_bus_init(&a,rom,sizeof(rom));a.vdp.target=MD2GBA_VDP_VRAM;a.vdp.address=0x100u;a.vdp.regs[15]=2u;
    if(k==0)a.vdp.target=MD2GBA_VDP_CRAM;
    if(k==1)a.vdp.target=MD2GBA_VDP_VSRAM;
    if(k==2)a.vdp.dma_fill_pending=1u;
    if(k==3)a.vdp.address=0x101u;
    if(k==4)a.vdp.regs[15]=4u;
    if(k==5)a.vdp.address=0xFFF0u;
    Md2GbaVdp before=a.vdp;int ok=md2gba_vdp_data16_repeat_fast(&a,k==5?16u:4u,0x1234u);
    if(ok||memcmp(&before,&a.vdp,sizeof(before))){printf("FAIL refusal %u\n",k);return 2;}
  }
  puts("PASS 30,000 P108 VDP bulk states + refusal side-effect gates");return 0;
}
