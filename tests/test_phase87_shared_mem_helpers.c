#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define MD2GBA_AOT_GENERATED_FAST 1
#define MD2GBA_AOT_SHARED_MEM_HELPERS 1
#define MD2GBA_PSC_REVERSED_WRAM 1
#define MD2GBA_PSC_REVERSED_ROM 1
#include "md2gba_aot68k.h"

static uint32_t rng=0x87c0ffeeu;
static uint32_t rnd(void){rng=rng*1664525u+1013904223u;return rng;}
static void reverse_rom(uint8_t *dst,const uint8_t *src,size_t n){for(size_t i=0;i<n;i++)dst[n-1-i]=src[i];}

static void seed_cpu(Md2Gba68k*c){
 memset(c,0,sizeof *c); for(int i=0;i<8;i++){c->d[i]=rnd();c->a[i]=0xff0000u|(rnd()&0xfff0u);} c->pc=(rnd()&0x3fffeu);c->sr=(uint16_t)rnd();
}
static int same_cpu(const Md2Gba68k*a,const Md2Gba68k*b){return memcmp(a,b,sizeof *a)==0;}

int main(void){
 enum{RN=0x40000}; uint8_t *rom=malloc(RN),*rev=malloc(RN); if(!rom||!rev)return 2;
 for(unsigned i=0;i<RN;i++)rom[i]=(uint8_t)rnd(); reverse_rom(rev,rom,RN);
 for(unsigned it=0;it<100000u;it++){
  Md2GbaBus bi,bs; Md2Gba68k ci,cs; md2gba_bus_init(&bi,rom,RN);md2gba_bus_init(&bs,rom,RN);md2gba_bus_set_reversed_rom(&bi,rev);md2gba_bus_set_reversed_rom(&bs,rev);seed_cpu(&ci);cs=ci;
  for(unsigned q=0;q<65536u;q++){uint8_t v=(uint8_t)rnd();md2gba_wram_write8_offset(&bi,q,v);md2gba_wram_write8_offset(&bs,q,v);}
  unsigned sz=(unsigned[]){1,2,4}[rnd()%3u]; int k; unsigned sel=rnd()%9u;
  switch(sel){case 0:k=MD68K_EA_DN;break;case 1:k=MD68K_EA_AN;break;case 2:k=MD68K_EA_AN_IND;break;case 3:k=MD68K_EA_AN_POSTINC;break;case 4:k=MD68K_EA_AN_PREDEC;break;case 5:k=MD68K_EA_AN_DISP16;break;case 6:k=MD68K_EA_ABS_W;break;case 7:k=MD68K_EA_ABS_L;break;default:k=MD68K_EA_IMM;break;}
  int r=(int)(rnd()&7u);uint32_t value=rnd(),pc=ci.pc;int32_t disp=(int16_t)rnd();uint16_t ext=(uint16_t)rnd();
  if(k==MD68K_EA_ABS_W)value=0x8000u|(rnd()&0x7ffeu); /* sign-extends into FFxxxx */
  if(k==MD68K_EA_ABS_L)value=0xff0000u|(rnd()&0xfffcu);
  uint32_t vi=md68k_rd_gen(&ci,&bi,k,r,value,disp,ext,sz,pc);
  uint32_t vs=md68k_rd(&cs,&bs,k,r,value,disp,ext,sz,pc);
  if(vi!=vs||!same_cpu(&ci,&cs)||memcmp(bi.wram,bs.wram,sizeof bi.wram)){printf("FAIL read it=%u k=%d sz=%u %08x/%08x\n",it,k,sz,vi,vs);return 1;}
  /* Fresh identical state for write comparison. */
  seed_cpu(&ci);cs=ci;memcpy(bs.wram,bi.wram,sizeof bi.wram);
  r=(int)(rnd()&7u);value=rnd();pc=ci.pc;disp=(int16_t)rnd();ext=(uint16_t)rnd();
  if(k==MD68K_EA_ABS_W)value=0x8000u|(rnd()&0x7ffeu);
  if(k==MD68K_EA_ABS_L)value=0xff0000u|(rnd()&0xfffcu);
  uint32_t out=rnd();
  md68k_wr_gen(&ci,&bi,k,r,value,disp,ext,sz,out,pc);
  md68k_wr(&cs,&bs,k,r,value,disp,ext,sz,out,pc);
  if(!same_cpu(&ci,&cs)||memcmp(bi.wram,bs.wram,sizeof bi.wram)){printf("FAIL write it=%u k=%d sz=%u\n",it,k,sz);return 1;}
 }
 puts("PASS 100000 inline-vs-shared EA/read/write states with PSC ROM/WRAM layout");return 0;
}
