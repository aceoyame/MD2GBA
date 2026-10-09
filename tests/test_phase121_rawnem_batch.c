#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "md2gba_profile_raw_nem.h"
typedef struct {const char*name;uint32_t d[8],a[7];uint16_t sr;} Init;
static const Init inits[]={
  {"1CD370",{0x000001FFu,0x00000000u,0x00000000u,0x00000008u,0x00000000u,0x0000FFA7u,0x00000010u,0x00000877u},{0x001CD3AAu,0x00FFE200u,0x00FFFB0Eu,0x00041802u,0x00C00000u,0x00000068u,0x00C00004u},0x2110u},
  {"1CD2A6",{0x000001FFu,0x00000002u,0x00000000u,0x00000008u,0x00000000u,0x0000FA4Au,0x00000010u,0x00000655u},{0x001CD2D5u,0x00FFE200u,0x00FFFB0Eu,0x00041802u,0x00C00000u,0x00000058u,0x00C00004u},0x2110u},
  {"1CD462",{0x000001FFu,0x00000001u,0x00000000u,0x00000008u,0x00000000u,0x00000C18u,0x00000010u,0x0000071Fu},{0x001CD4BAu,0x00FFE200u,0x00FFFB0Eu,0x0004180Cu,0x00C00000u,0x00000CF0u,0x00C00004u},0x2110u},
  {"2A2B7E",{0x000001FFu,0x00000000u,0x00000000u,0x00000008u,0x00000000u,0x0000FFF3u,0x00000010u,0x0000087Du},{0x002A2BADu,0x00FFE200u,0x00FFFB0Eu,0x0004180Cu,0x00C00000u,0x00000400u,0x00C00004u},0x2110u},
  {"2A303A",{0x000001FFu,0x00000002u,0x00000000u,0x00000008u,0x00000000u,0x0000579Bu,0x00000010u,0x0000065Fu},{0x002A3060u,0x00FFE200u,0x00FFFB0Eu,0x00041802u,0x00C00000u,0x000002B8u,0x00C00004u},0x2110u},
};
static void setupcpu(Md2Gba68k*c,const Init*i){memset(c,0,sizeof(*c));memcpy(c->d,i->d,sizeof(i->d));memcpy(c->a,i->a,sizeof(i->a));c->sr=i->sr;c->pc=0x04178Au;}
static int cpueq(const Md2Gba68k*a,const Md2Gba68k*b){return memcmp(a,b,sizeof(*a))==0;}
int main(void){
 for(unsigned z=0;z<sizeof(inits)/sizeof(inits[0]);z++){
  uint8_t rom[0x200]={0};Md2GbaBus bb,bo;Md2Gba68k cb,co;Md2GbaTiming t;uint32_t left=488u,cost=0,steps=0,recs=0;
  md2gba_bus_init(&bb,rom,sizeof(rom));bo=bb;setupcpu(&cb,&inits[z]);co=cb;
  /* Deterministic VDP write stream state; both implementations see identical state. */
  bb.vdp.address=0x1234u;bb.vdp.code=1u;bb.vdp.target=MD2GBA_VDP_VRAM;bo.vdp=bb.vdp;
  md2gba_timing_init(&t,0u,1);md2gba_timing_enable_cycles(&t,262u,224u,488u);md2gba_timing_enable_scheduled(&t);t.scanline=16u;t.scheduled_countdown=left;t.hint_counter=7u;bb.vdp.regs[0]&=(uint8_t)~0x10u;bb.vdp.regs[10]=7u;bo.vdp.regs[0]=bb.vdp.regs[0];bo.vdp.regs[10]=bb.vdp.regs[10];
  md2gba_profile_raw_nem_reset();
  if(!md2gba_profile_raw_nem_vblank_batch(&cb,&bb,&t,&left,&cost,&steps,&recs)||recs<2u){printf("FAIL batch %s recs=%u cost=%u\n",inits[z].name,recs,cost);return 1;}
  md2gba_profile_raw_nem_reset();uint32_t bc=0,bs=0;
  for(unsigned k=0;k<recs;k++){uint32_t c=0,s=0;if(!md2gba_profile_raw_nem(&co,&bo,0x7FFFFFFFu,&c,&s)){printf("FAIL seq %s k=%u/%u\n",inits[z].name,k,recs);return 1;}bc+=c;bs+=s;}
  if(cost!=bc||steps!=bs||!cpueq(&cb,&co)||memcmp(&bb.vdp,&bo.vdp,sizeof(bb.vdp))){printf("FAIL diff %s recs=%u cost=%u/%u steps=%u/%u pc=%x/%x\n",inits[z].name,recs,cost,bc,steps,bs,cb.pc,co.pc);return 1;}
  printf("PASS %s records=%u cycles=%u steps=%u left=%u scan=%u\n",inits[z].name,recs,cost,steps,left,t.scanline);
 }
 puts("PASS P121 raw Nemesis VBlank batch matches P119 command replay for all opening assets");return 0;
}
