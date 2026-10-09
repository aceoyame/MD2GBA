#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "md2gba_timing.h"
static uint32_t rng=0x109b17d1u;static uint32_t rnd(void){rng=rng*1664525u+1013904223u;return rng;}
static void put32be(uint8_t*p,unsigned off,uint32_t v){p[off]=(uint8_t)(v>>24);p[off+1]=(uint8_t)(v>>16);p[off+2]=(uint8_t)(v>>8);p[off+3]=(uint8_t)v;}
static void setup(Md2GbaTiming*t,Md2Gba68k*c,Md2GbaBus*b,uint8_t*rom,unsigned line,unsigned over,unsigned prior,unsigned phase,unsigned reload,unsigned hint,unsigned vint,unsigned pcy,unsigned bcy){
 memset(rom,0,0x200);put32be(rom,0x78,0x00000120u);md2gba_bus_init(b,rom,0x200);md68k_reset_context(c);
 c->a[7]=0x00FFF000u;c->pc=phase?0x004207Au:0x0042076u;c->sr=0x2000u;
 b->vdp.regs[1]=(uint8_t)((b->vdp.regs[1]&~0x20u)|(vint?0x20u:0u));b->vdp.regs[0]&=(uint8_t)~0x10u;b->vdp.regs[10]=(uint8_t)reload;
 b->vblank_active=(line>224u)?1u:0u;b->hblank_active=(uint8_t)(rnd()&1u);
 md2gba_timing_init(t,0u,1);md2gba_timing_enable_cycles(t,262u,224u,488u);md2gba_timing_enable_scheduled(t);
 t->scanline=(uint16_t)line;t->scheduled_countdown=0u;t->scheduled_overshoot=over;t->cycle_accum=prior;t->hint_counter=(uint8_t)hint;
 t->idle_poll_pc=0x0042076u;t->idle_branch_pc=0x004207Au;t->idle_poll_cycles=(uint8_t)pcy;t->idle_branch_cycles=(uint8_t)bcy;
}
static unsigned exact_until_yield(Md2GbaTiming*t,Md2Gba68k*c,Md2GbaBus*b,unsigned pcy,unsigned bcy){unsigned steps=0u;for(;;){
 uint32_t cyc,left=t->scheduled_countdown;if(c->pc==0x0042076u){cyc=pcy;c->pc=0x004207Au;}else if(c->pc==0x004207Au){cyc=bcy;c->pc=0x0042076u;}else return 0xffffffffu;steps++;
 if(left<=cyc){t->scheduled_overshoot+=cyc-left;t->scheduled_countdown=0u;if(md2gba_timing_scheduled_event(c,b,steps,t))return steps;}else t->scheduled_countdown=left-cyc;
 }}
static int same(const Md2GbaTiming*a,const Md2GbaTiming*b,const Md2Gba68k*ca,const Md2Gba68k*cb,const Md2GbaBus*ba,const Md2GbaBus*bb){
 Md2GbaTiming x=*a,y=*b;x.idle_poll_hint=y.idle_poll_hint=0;x.idle_fast_forwards=y.idle_fast_forwards=0;x.idle_lines_advanced=y.idle_lines_advanced=0;x.idle_steps_advanced=y.idle_steps_advanced=0;
 x.idle_poll_pc=y.idle_poll_pc=0;x.idle_branch_pc=y.idle_branch_pc=0;x.idle_poll_cycles=y.idle_poll_cycles=0;x.idle_branch_cycles=y.idle_branch_cycles=0;
 if(memcmp(&x,&y,sizeof(x))||memcmp(ca,cb,sizeof(*ca))||memcmp(&ba->vdp,&bb->vdp,sizeof(ba->vdp)))return 0;
 if(ba->vblank_active!=bb->vblank_active||ba->hblank_active!=bb->hblank_active)return 0;
 for(unsigned i=0;i<0x10000u;i++)if(md2gba_read8((Md2GbaBus*)ba,0xff0000u+i)!=md2gba_read8((Md2GbaBus*)bb,0xff0000u+i))return 0;return 1;
}
int main(void){for(unsigned it=0;it<5000u;it++){
 unsigned line=rnd()%262u,over=rnd()%24u,prior=rnd()%24u,phase=rnd()&1u,reload=rnd()&255u,hint=reload?(rnd()%(reload+1u)):0u,vint=rnd()&1u;
 unsigned pcy=4u+2u*(rnd()%9u),bcy=4u+2u*(rnd()%9u);uint8_t ra[0x200],rb[0x200];Md2GbaTiming a,b;Md2Gba68k ca,cb;Md2GbaBus ba,bb;
 setup(&a,&ca,&ba,ra,line,over,prior,phase,reload,hint,vint,pcy,bcy);setup(&b,&cb,&bb,rb,line,over,prior,phase,reload,hint,vint,pcy,bcy);
 /* setup consumes RNG for hblank flag independently; make those equal. */ bb.hblank_active=ba.hblank_active;
 int ar=md2gba_timing_scheduled_event(&ca,&ba,0,&a);unsigned exact=0u;if(!ar)exact=exact_until_yield(&a,&ca,&ba,pcy,bcy);
 b.idle_poll_hint=1u;int br=md2gba_timing_scheduled_event(&cb,&bb,0,&b);
 if(br!=1||!same(&a,&b,&ca,&cb,&ba,&bb)||(!ar&&b.idle_steps_advanced!=exact)){
  printf("FAIL it=%u line=%u reload=%u hint=%u over=%u prior=%u phase=%u vint=%u cyc=%u/%u pc=%x/%x scan=%u/%u hc=%u/%u vb=%u/%u hb=%u/%u total=%u/%u\n",it,line,reload,hint,over,prior,phase,vint,pcy,bcy,ca.pc,cb.pc,a.scanline,b.scanline,a.hint_counter,b.hint_counter,ba.vblank_active,bb.vblank_active,ba.hblank_active,bb.hblank_active,a.total_cycles,b.total_cycles);return 1;
 }
 }
 puts("PASS P109 arithmetic idle batch: 5,000 randomized exact-to-VBlank states");return 0;}
