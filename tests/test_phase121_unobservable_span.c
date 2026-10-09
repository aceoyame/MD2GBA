#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "md2gba_timing.h"
static uint32_t rng=0x1215a17du;
static uint32_t rnd(void){rng=rng*1664525u+1013904223u;return rng;}
static void setup(Md2GbaTiming*t,Md2GbaBus*b,unsigned line,unsigned left,unsigned reload,unsigned hint,unsigned vb,unsigned hb){
 memset(b,0,sizeof(*b));md2gba_timing_init(t,0u,1);md2gba_timing_enable_cycles(t,262u,224u,488u);md2gba_timing_enable_scheduled(t);
 t->scanline=(uint16_t)line;t->scheduled_countdown=left;t->hint_counter=(uint8_t)hint;t->cycle_accum=0u;t->total_cycles=777u;
 b->vdp.regs[0]&=(uint8_t)~0x10u;b->vdp.regs[10]=(uint8_t)reload;b->vblank_active=(uint8_t)vb;b->hblank_active=(uint8_t)hb;
}
static void expected(Md2GbaTiming*t,Md2GbaBus*b,uint32_t*left,uint32_t cyc){
 uint32_t cpl=488u,rem,crossed,phase,i;
 if(cyc<*left){*left-=cyc;t->scheduled_countdown=*left;t->total_cycles+=cyc;return;}
 rem=cyc-*left;crossed=1u+rem/cpl;phase=rem%cpl;
 for(i=0;i<crossed;i++){
  unsigned line=t->scanline;
  if(b->hblank_active)b->hblank_active=0u;
  b->hblank_active=1u;t->hblanks++;
  if(line==0u)t->hint_counter=b->vdp.regs[10];
  if(line<=t->active_lines){if(t->hint_counter==0u)t->hint_counter=b->vdp.regs[10];else t->hint_counter--;}
  else t->hint_counter=b->vdp.regs[10];
  t->scanline++;
  if(t->scanline>=t->total_lines){t->scanline=0u;b->vblank_active=0u;}
 }
 t->cycle_accum=phase;*left=phase?(cpl-phase):cpl;t->scheduled_countdown=*left;t->scheduled_overshoot=0u;t->total_cycles+=cyc;
}
int main(void){
 for(unsigned it=0;it<20000u;it++){
  unsigned line=rnd()%262u,left=1u+(rnd()%488u),reload=rnd()&255u,hint=reload?(rnd()%(reload+1u)):0u;
  unsigned vb=(line>224u),hb=rnd()&1u;Md2GbaTiming a,e;Md2GbaBus ba,be;setup(&a,&ba,line,left,reload,hint,vb,hb);e=a;be=ba;
  uint32_t budget=md2gba_timing_scheduled_cycles_to_vblank(&a,left);if(!budget){printf("FAIL budget0\n");return 1;}
  uint32_t cyc=1u+(rnd()%(budget-1u));uint32_t la=left,le=left;
  if(!md2gba_timing_consume_unobservable_span(&a,&ba,&la,cyc)){printf("FAIL refused it=%u budget=%u cyc=%u\n",it,budget,cyc);return 1;}
  expected(&e,&be,&le,cyc);
  if(la!=le||a.scanline!=e.scanline||a.hint_counter!=e.hint_counter||a.hblanks!=e.hblanks||a.cycle_accum!=e.cycle_accum||a.scheduled_countdown!=e.scheduled_countdown||a.scheduled_overshoot!=e.scheduled_overshoot||a.total_cycles!=e.total_cycles||ba.vblank_active!=be.vblank_active||ba.hblank_active!=be.hblank_active){
   printf("FAIL state it=%u line=%u left=%u cyc=%u budget=%u -> scan %u/%u left %u/%u hint %u/%u hb %u/%u vb %u/%u\n",it,line,left,cyc,budget,a.scanline,e.scanline,la,le,a.hint_counter,e.hint_counter,ba.hblank_active,be.hblank_active,ba.vblank_active,be.vblank_active);return 1;
  }
  Md2GbaTiming r=a;Md2GbaBus br=ba;uint32_t lr=la;if(md2gba_timing_consume_unobservable_span(&r,&br,&lr,md2gba_timing_scheduled_cycles_to_vblank(&a,la))!=0||memcmp(&r,&a,sizeof(a))||memcmp(&br,&ba,sizeof(ba))||lr!=la){printf("FAIL vblank refusal\n");return 1;}
  r=a;br=ba;lr=la;br.vdp.regs[0]|=0x10u;if(md2gba_timing_consume_unobservable_span(&r,&br,&lr,1u)!=0){printf("FAIL hint refusal\n");return 1;}
 }
 puts("PASS P121 unobservable-span timing: 20,000 randomized states + VBlank/H-int refusal");return 0;
}
