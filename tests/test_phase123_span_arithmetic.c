#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "md2gba_timing.h"
static uint32_t rng=0x1235a11du;static uint32_t rnd(void){rng=rng*1664525u+1013904223u;return rng;}
static void setup(Md2GbaTiming*t,Md2GbaBus*b,unsigned total,unsigned active,unsigned cpl,unsigned line,unsigned left,unsigned reload,unsigned hint,unsigned vb,unsigned hb){
 memset(b,0,sizeof(*b));md2gba_timing_init(t,0u,1);md2gba_timing_enable_cycles(t,(uint16_t)total,(uint16_t)active,(uint16_t)cpl);md2gba_timing_enable_scheduled(t);
 t->scanline=(uint16_t)line;t->scheduled_countdown=left;t->hint_counter=(uint8_t)hint;t->cycle_accum=0u;t->total_cycles=777u;
 b->vdp.regs[0]&=(uint8_t)~0x10u;b->vdp.regs[10]=(uint8_t)reload;b->vblank_active=(uint8_t)vb;b->hblank_active=(uint8_t)hb;
}
static void expected(Md2GbaTiming*t,Md2GbaBus*b,uint32_t*left,uint32_t cyc){
 uint32_t cpl=t->cycles_per_line?t->cycles_per_line:488u,rem,crossed,phase,i;
 if(cyc<*left){*left-=cyc;t->scheduled_countdown=*left;t->total_cycles+=cyc;return;}
 rem=cyc-*left;crossed=1u+rem/cpl;phase=rem%cpl;
 for(i=0;i<crossed;i++){
  unsigned line=t->scanline;
  if(b->hblank_active)b->hblank_active=0u;b->hblank_active=1u;t->hblanks++;
  if(line==0u)t->hint_counter=b->vdp.regs[10];
  if(line<=t->active_lines){if(t->hint_counter==0u)t->hint_counter=b->vdp.regs[10];else t->hint_counter--;}
  else t->hint_counter=b->vdp.regs[10];
  t->scanline++;if(t->scanline>=t->total_lines){t->scanline=0u;b->vblank_active=0u;}
 }
 t->cycle_accum=phase;*left=phase?(cpl-phase):cpl;t->scheduled_countdown=*left;t->scheduled_overshoot=0u;t->total_cycles+=cyc;
}
int main(void){unsigned accepted=0;
 for(unsigned it=0;it<160000u;it++){
  unsigned total=240u+(rnd()%80u),active=180u+(rnd()%50u);if(active>=total)active=total-1u;
  unsigned cpl=300u+(rnd()%400u),line=rnd()%total,left=1u+(rnd()%cpl),reload=rnd()&255u,hint=rnd()&255u;
  unsigned vb=(line>active),hb=rnd()&1u;Md2GbaTiming a,e;Md2GbaBus ba,be;setup(&a,&ba,total,active,cpl,line,left,reload,hint,vb,hb);e=a;be=ba;
  uint32_t budget=md2gba_timing_scheduled_cycles_to_vblank(&a,left);if(budget<=1u)continue;
  uint32_t cyc=1u+(rnd()%(budget-1u));uint32_t la=left,le=left;
  if(!md2gba_timing_consume_unobservable_span_budgeted(&a,&ba,&la,cyc,budget)){printf("FAIL refused it=%u\n",it);return 1;}
  expected(&e,&be,&le,cyc);
  if(la!=le||a.scanline!=e.scanline||a.hint_counter!=e.hint_counter||a.hblanks!=e.hblanks||a.cycle_accum!=e.cycle_accum||a.scheduled_countdown!=e.scheduled_countdown||a.scheduled_overshoot!=e.scheduled_overshoot||a.total_cycles!=e.total_cycles||ba.vblank_active!=be.vblank_active||ba.hblank_active!=be.hblank_active){
   printf("FAIL state it=%u total=%u active=%u cpl=%u line=%u left=%u cyc=%u scan=%u/%u hint=%u/%u vb=%u/%u hb=%u/%u\n",it,total,active,cpl,line,left,cyc,a.scanline,e.scanline,a.hint_counter,e.hint_counter,ba.vblank_active,be.vblank_active,ba.hblank_active,be.hblank_active);return 1;
  }accepted++;
 }
 printf("PASS P123 closed-form scanline span: %u randomized accepted states\n",accepted);return 0;
}
