#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "md2gba_aot_region.h"
#include "md2gba_bus.h"
#include "md2gba_profile_fieldfast.h"
#include "ps4_whole_region_pschybrid_internal.h"

int ps4_whole_region_page_0108_pschybrid(Md2Gba68k*,Md2GbaBus*,const Md2GbaAotHooks*,ps4_whole_region_pschybrid_state*);

static uint32_t xs=0x13579BDFu;
static uint32_t rnd(void){xs^=xs<<13;xs^=xs>>17;xs^=xs<<5;return xs;}
static void put32(Md2GbaBus*b,uint32_t a,uint32_t v){md2gba_write32(b,a,v);}

static int cpu_eq(const Md2Gba68k*a,const Md2Gba68k*b){return memcmp(a,b,sizeof(*a))==0;}

int main(int argc,char**argv){
 if(argc!=2){fprintf(stderr,"usage: %s ps4built.bin\n",argv[0]);return 2;}
 FILE*f=fopen(argv[1],"rb"); if(!f){perror("fopen");return 2;}
 fseek(f,0,SEEK_END); long n=ftell(f); rewind(f); uint8_t*rom=malloc((size_t)n); if(!rom||fread(rom,1,(size_t)n,f)!=(size_t)n)return 2; fclose(f);
 for(unsigned it=0;it<5000;it++){
   Md2GbaBus a,b; Md2Gba68k ca={0},cb={0}; ps4_whole_region_pschybrid_state st={0};
   md2gba_bus_init(&a,rom,(size_t)n); md2gba_bus_init(&b,rom,(size_t)n);
   for(unsigned i=0;i<8;i++){ca.d[i]=rnd();ca.a[i]=rnd()&0x00FFFFFFu;}
   ca.pc=0x04209Cu; ca.sr=(uint16_t)rnd(); ca.a[5]=0x00C00000u; ca.a[6]=0x00C00004u; ca.a[7]=0x00FF8000u;
   /* Routine source lookup uses 2*D0. Keep it in immutable cartridge ROM. */
   ca.d[0]=rnd()%((uint32_t)n/2u-2u);
   md2gba_wram_write16_offset(&a,0xEF0Au,(uint16_t)rnd());
   md2gba_wram_write8_offset(&a,0xEF0Bu,(uint8_t)rnd());
   /* MOVEM restore image + valid 24-bit RTS target outside page 0108. */
   for(unsigned k=0;k<5;k++)put32(&a,ca.a[7]+4u*k,rnd());
   put32(&a,ca.a[7]+20u,0x00430000u+(rnd()&0xFFFEu));
   cb=ca; b=a;
   st.left=1000u;
   int rc=ps4_whole_region_page_0108_pschybrid(&ca,&a,0,&st);
   uint32_t cost=0,steps=0;
   int ok=md2gba_profile_fieldfast_4209c(&cb,&b,1000u,&cost,&steps);
   if(!ok||cost!=286u||steps!=41u){fprintf(stderr,"helper admission fail %u\n",it);return 3;}
   if(st.steps!=41u||st.left!=714u){fprintf(stderr,"canonical charge fail it=%u rc=%d steps=%u left=%u\n",it,rc,st.steps,st.left);return 4;}
   if(!cpu_eq(&ca,&cb)){
     fprintf(stderr,"CPU mismatch it=%u\n",it);
     fprintf(stderr,"pc %08x/%08x sr %04x/%04x a7 %08x/%08x\n",ca.pc,cb.pc,ca.sr,cb.sr,ca.a[7],cb.a[7]);
     for(int k=0;k<4;k++)fprintf(stderr,"d%d %08x/%08x\n",k,ca.d[k],cb.d[k]);
     return 5;
   }
   if(memcmp(&a,&b,sizeof(a))!=0){
     fprintf(stderr,"BUS mismatch it=%u\n",it);return 6;
   }
   /* Strict boundary: helper must refuse so P96 exact seam remains canonical. */
   cost=steps=0;
   if(md2gba_profile_fieldfast_4209c(&cb,&b,286u,&cost,&steps)){fprintf(stderr,"deadline admission bug\n");return 7;}
 }
 puts("P115 fieldfast differential: 5000/5000 exact; strict deadline refusal exact");
 return 0;
}
