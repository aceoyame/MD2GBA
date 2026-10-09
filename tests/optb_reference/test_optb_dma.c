#include <stdio.h>
#include <string.h>
#include "../../md2gba_bus.c"
static Md2GbaBus actual,expected,before;
static unsigned state=0xB16;
static unsigned rnd(void){state=1664525u*state+1013904223u;return state>>3;}
int main(void){
 unsigned accepted=0,refused=0;
 for(unsigned it=0;it<5000;it++){
  md2gba_bus_init(&actual,0,0);
  for(unsigned i=0;i<65536;i++){actual.wram[i]=(uint8_t)rnd();actual.vdp.vram[i]=(uint8_t)rnd();}
  actual.vdp.target=MD2GBA_VDP_VRAM;actual.vdp.address=(uint16_t)(rnd()&0xFFFEu);
  actual.vdp.regs[15]=(it%3)?2:0;actual.vdp.regs[11]=(uint8_t)(rnd()&7u);actual.vdp.regs[13]=(uint8_t)(rnd()&63u);
  actual.vdp.trace_count=(uint8_t)(rnd()%33);actual.vdp.code=(uint8_t)rnd();actual.vdp.pending_control=rnd()&1;
  actual.vdp.data_writes=rnd();actual.vdp.vram_epoch=rnd();
  vdp_refresh_present_scroll(&actual.vdp);
  unsigned n=1+rnd()%257;uint32_t src=0xFF0000u+(rnd()&0xFFFEu);
  if(it%7==0)actual.vdp.address=(uint16_t)((actual.vdp.regs[13]<<10)-2);
  if(it%11==0){actual.vdp.address|=1;}
  if(it%13==0)actual.vdp.dma_fill_pending=1;
  if(it%17==0)actual.vdp.regs[15]=4;
  if(it%19==0)actual.vdp.target=MD2GBA_VDP_CRAM;
  if(it%23==0){src=0xFF0000;n=32768;actual.vdp.address=0;}
  if(it%29==0){n=32769;}
  before=actual;expected=actual;
  if(vdp_dma_reversed_wram_vram(&actual,src,n)){
   accepted++;
   for(unsigned i=0;i<n;i++)md2gba_vdp_data16_direct_vdp(&expected.vdp,md2gba_read16(&expected,src+i*2u));
   if(memcmp(&actual,&expected,sizeof actual)){printf("FAIL DMA diff %u n=%u src=%x dst=%x\n",it,n,src,before.vdp.address);return 1;}
  }else{refused++;if(memcmp(&actual,&before,sizeof actual)){puts("FAIL side effects on refusal");return 2;}}
 }
 printf("PASS OPT-B reversed-WRAM DMA: 5,000 states (%u accepted/%u refused), full bus state, traces/epochs/scroll overlap and boundary sizes\n",accepted,refused);
}
