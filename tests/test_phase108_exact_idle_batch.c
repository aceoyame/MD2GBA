#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "md2gba_timing.h"

static void put32be(uint8_t*p,unsigned off,uint32_t v){p[off]=(uint8_t)(v>>24);p[off+1]=(uint8_t)(v>>16);p[off+2]=(uint8_t)(v>>8);p[off+3]=(uint8_t)v;}
static void setup(Md2GbaTiming*t,Md2Gba68k*c,Md2GbaBus*b,uint8_t*rom,unsigned line,unsigned over,unsigned prior,unsigned phase){
 memset(rom,0,0x200);put32be(rom,0x78,0x00000120u);md2gba_bus_init(b,rom,0x200);md68k_reset_context(c);
 c->a[7]=0x00FFF000u;c->pc=phase?0x004207Au:0x0042076u;c->sr=0x2000u; /* Z clear => BNE taken. */
 b->vdp.regs[1]|=0x20u;b->vdp.regs[0]&=(uint8_t)~0x10u;
 md2gba_timing_init(t,0u,1);md2gba_timing_enable_cycles(t,262u,224u,488u);md2gba_timing_enable_scheduled(t);
 t->scanline=(uint16_t)line;t->scheduled_countdown=0u;t->scheduled_overshoot=over;t->cycle_accum=prior;
}
/* Continue the exact P96 poll stream after the first scheduled event. */
static unsigned exact_until_yield(Md2GbaTiming*t,Md2Gba68k*c,Md2GbaBus*b){unsigned steps=0u;for(;;){
 uint32_t cyc=10u,left=t->scheduled_countdown;
 if(c->pc==0x0042076u)c->pc=0x004207Au;else if(c->pc==0x004207Au)c->pc=0x0042076u;else{printf("bad exact pc %x\n",c->pc);return 0xffffffffu;}
 steps++;
 if(left<=cyc){t->scheduled_overshoot+=cyc-left;t->scheduled_countdown=0u;if(md2gba_timing_scheduled_event(c,b,steps,t))return steps;}
 else t->scheduled_countdown=left-cyc;
 }}
static int same(const Md2GbaTiming*a,const Md2GbaTiming*b,const Md2Gba68k*ca,const Md2Gba68k*cb,const Md2GbaBus*ba,const Md2GbaBus*bb){
 Md2GbaTiming x=*a,y=*b;
 x.idle_poll_hint=y.idle_poll_hint=0;x.idle_fast_forwards=y.idle_fast_forwards=0;x.idle_lines_advanced=y.idle_lines_advanced=0;
 x.idle_poll_pc=y.idle_poll_pc=0;x.idle_branch_pc=y.idle_branch_pc=0;x.idle_poll_cycles=y.idle_poll_cycles=0;x.idle_branch_cycles=y.idle_branch_cycles=0;
 x.idle_steps_advanced=y.idle_steps_advanced=0;
 if(memcmp(&x,&y,sizeof(x))||memcmp(ca,cb,sizeof(*ca))||memcmp(&ba->vdp,&bb->vdp,sizeof(ba->vdp)))return 0;
 for(unsigned i=0;i<0x10000u;i++)if(md2gba_read8((Md2GbaBus*)ba,0xff0000u+i)!=md2gba_read8((Md2GbaBus*)bb,0xff0000u+i))return 0;
 return 1;
}
int main(void){static const unsigned lines[]={0,1,37,159,200,223,225,250,261};
 for(unsigned li=0;li<sizeof(lines)/sizeof(lines[0]);li++)for(unsigned over=0;over<10u;over++)for(unsigned prior=0;prior<10u;prior++)for(unsigned phase=0;phase<2u;phase++){
  uint8_t ra[0x200],rb[0x200];Md2GbaTiming a,b;Md2Gba68k ca,cb;Md2GbaBus ba,bb;
  setup(&a,&ca,&ba,ra,lines[li],over,prior,phase);setup(&b,&cb,&bb,rb,lines[li],over,prior,phase);
  /* Baseline processes current real seam, then executes exact loop to VBlank. */
  int ar=md2gba_timing_scheduled_event(&ca,&ba,0,&a);unsigned exact_steps=0u;if(!ar)exact_steps=exact_until_yield(&a,&ca,&ba);int expected_yield=1;
  /* P108 gets the same current seam plus a descriptor proving the loop stable. */
  b.idle_poll_hint=1u;b.idle_poll_pc=0x0042076u;b.idle_branch_pc=0x004207Au;b.idle_poll_cycles=10u;b.idle_branch_cycles=10u;
  int br=md2gba_timing_scheduled_event(&cb,&bb,0,&b);
  if(expected_yield!=br || !same(&a,&b,&ca,&cb,&ba,&bb) || (!ar && b.idle_steps_advanced!=exact_steps)){
   printf("FAIL line=%u over=%u prior=%u phase=%u r=%d/%d steps=%u/%u pc=%x/%x scan=%u/%u cyc=%u/%u over2=%u/%u cd=%u/%u\n",lines[li],over,prior,phase,ar,br,exact_steps,b.idle_steps_advanced,ca.pc,cb.pc,a.scanline,b.scanline,a.total_cycles,b.total_cycles,a.cycle_accum,b.cycle_accum,a.scheduled_countdown,b.scheduled_countdown);return 1;
  }
 }
 /* H-int guard: descriptor must reduce to exactly one normal scheduled line. */
 {uint8_t r[0x200];Md2GbaTiming t;Md2Gba68k c;Md2GbaBus b;setup(&t,&c,&b,r,10,3,2,0);b.vdp.regs[0]|=0x10u;t.idle_poll_hint=1u;t.idle_poll_pc=0x42076u;t.idle_branch_pc=0x4207au;t.idle_poll_cycles=t.idle_branch_cycles=10u;
  if(md2gba_timing_scheduled_event(&c,&b,0,&t)!=0||t.scanline!=11u||t.idle_fast_forwards!=0u){puts("FAIL H-int guard");return 2;}}
 puts("PASS P108 exact idle batch: 1,800 seam/phase states + VBlank stacked-PC equivalence + H-int guard");return 0;}
