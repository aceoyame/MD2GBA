#include "md2gba_timing.h"
#if defined(MD2GBA_GBA_IWRAM)
#define MD2GBA_TIMING_COLD __attribute__((section(".rom.cold"),noinline))
#define MD2GBA_TIMING_HOT __attribute__((section(".iwram.text"),noinline))
#else
#define MD2GBA_TIMING_COLD
#define MD2GBA_TIMING_HOT
#endif
#include "md2gba_cycles.h"
static void z(void*p,unsigned n){uint8_t*q=(uint8_t*)p;while(n--)*q++=0;}
/* P129 recovery: exact unsigned division without the fixed 32-bit restoring
   loop.  Align the divisor only as far as the quotient requires, then descend.
   The d<=n>>1 guard prevents overflow while aligning.  Semantics intentionally
   retain P128's divide-by-zero result (0). */
static uint32_t udiv32(uint32_t n,uint32_t d){
    uint32_t q=0u,bit=1u;
    if(!d)return 0u;
    if(n<d)return 0u;
    while(d<=(n>>1)){d<<=1;bit<<=1;}
    do{if(n>=d){n-=d;q|=bit;}d>>=1;bit>>=1;}while(bit);
    return q;
}
MD2GBA_TIMING_COLD void md2gba_timing_init(Md2GbaTiming*t,uint32_t frame_interval,int yield_each_frame){z(t,sizeof(*t));t->frame_interval=frame_interval?frame_interval:50000u;t->yield_each_frame=yield_each_frame?1u:0u;}
void md2gba_timing_enable_scanlines(Md2GbaTiming*t,uint16_t total,uint16_t active){if(!t)return;t->total_lines=total?total:262u;t->active_lines=(active&&active<t->total_lines)?active:224u;t->scanline_interval=udiv32(t->frame_interval,t->total_lines);if(!t->scanline_interval)t->scanline_interval=1u;t->scanline=t->scanline_ticks=0;t->hint_counter=0;t->scanline_mode=1u;t->cycle_mode=0;t->scheduled_mode=0;t->scheduled_countdown=0;t->scheduled_overshoot=0;}
MD2GBA_TIMING_COLD void md2gba_timing_enable_cycles(Md2GbaTiming*t,uint16_t total,uint16_t active,uint16_t cpl){if(!t)return;t->total_lines=total?total:262u;t->active_lines=(active&&active<t->total_lines)?active:224u;t->cycles_per_line=cpl?cpl:488u;t->scanline=0;t->cycle_accum=0;t->total_cycles=0;t->hint_counter=0;t->scanline_mode=1u;t->cycle_mode=1u;t->scheduled_mode=0;t->scheduled_countdown=0;t->scheduled_overshoot=0;}
MD2GBA_TIMING_HOT static int advance_line(Md2GbaTiming*t,Md2Gba68k*c,Md2GbaBus*b){unsigned line=t->scanline;if(b->hblank_active)b->hblank_active=0;b->hblank_active=1;t->hblanks++;if(line==0u)t->hint_counter=b->vdp.regs[10];if(line<=t->active_lines){if(t->hint_counter==0u){if((b->vdp.regs[0]&0x10u)&&md68k_raise_autovector(c,b,4u))t->irq4_count++;t->hint_counter=b->vdp.regs[10];}else t->hint_counter--;}else t->hint_counter=b->vdp.regs[10];if(line==t->active_lines){t->vblanks++;md2gba_bus_set_vblank(b,1);if((b->vdp.regs[1]&0x20u)&&md68k_raise_autovector(c,b,6u))t->irq6_count++;t->scanline++;if(t->scanline>=t->total_lines)t->scanline=0;return t->yield_each_frame?1:0;}t->scanline++;if(t->scanline>=t->total_lines){t->scanline=0;md2gba_bus_set_vblank(b,0);}return 0;}
static int scanline_event(Md2GbaTiming*t,Md2Gba68k*c,Md2GbaBus*b){if(++t->scanline_ticks<t->scanline_interval)return 0;t->scanline_ticks=0;return advance_line(t,c,b);}
int md2gba_timing_event(Md2Gba68k*c,Md2GbaBus*b,uint32_t steps,void*user){Md2GbaTiming*t=(Md2GbaTiming*)user;(void)steps;if(!t)return 0;if(t->cycle_mode)return 0;if(t->scanline_mode)return scanline_event(t,c,b);if(++t->instruction_ticks<t->frame_interval)return 0;t->instruction_ticks=0;t->vblanks++;md2gba_bus_set_vblank(b,1);if((b->vdp.regs[1]&0x20u)&&md68k_raise_autovector(c,b,6u))t->irq6_count++;md2gba_bus_set_vblank(b,0);return t->yield_each_frame?1:0;}

/* Phase 13 scheduled-event bridge.  The countdown expresses how many complete
   instructions may execute before the next event must run at a pre-instruction
   boundary.  Therefore an N-step legacy interval maps to N-1 executable steps
   when the corresponding tick counter is zero. */
static uint32_t scheduled_limit(const Md2GbaTiming*t){uint32_t n;if(!t)return 0;if(t->cycle_mode)return t->cycles_per_line?t->cycles_per_line:488u;n=t->scanline_mode?t->scanline_interval:t->frame_interval;if(!n)return 0;return n-1u;}
MD2GBA_TIMING_COLD void md2gba_timing_enable_scheduled(Md2GbaTiming*t){uint32_t lim,progress;if(!t)return;lim=scheduled_limit(t);if(t->cycle_mode){t->scheduled_countdown=lim;t->scheduled_overshoot=0u;t->scheduled_mode=1u;return;}progress=t->scanline_mode?t->scanline_ticks:t->instruction_ticks;if(progress>lim)progress=lim;t->scheduled_countdown=lim-progress;t->scheduled_mode=1u;}
void md2gba_timing_sync_scheduled(Md2GbaTiming*t){uint32_t lim,left;if(!t||!t->scheduled_mode)return;lim=scheduled_limit(t);left=t->scheduled_countdown;if(left>lim)left=lim;if(t->cycle_mode){t->cycle_accum=lim-left;return;}if(t->scanline_mode)t->scanline_ticks=lim-left;else t->instruction_ticks=lim-left;}
/* P108: advance a structurally proven two-instruction fixed-WRAM poll to the
   next VBlank without changing the interrupt return PC.  Unlike P87, this does
   not force an early seam: it analytically executes the poll/branch stream to
   the exact instruction that crosses the real VBlank boundary, preserves the
   final overshoot, then raises the interrupt from that exact post-instruction
   PC.  H-int must be disabled because otherwise intermediate scanlines are
   observable CPU events. */
MD2GBA_TIMING_HOT static int timing_idle_poll_to_vblank(Md2GbaTiming*t,Md2Gba68k*c,Md2GbaBus*b,uint32_t lim,uint32_t initial_over){
    uint32_t pc=t->idle_poll_pc,br=t->idle_branch_pc,pcy=t->idle_poll_cycles,bcy=t->idle_branch_cycles;
    uint32_t lines,target,rem,n,steps=0u,final_over=0u;int r=0;
    if(!t->idle_poll_hint || !pc || !br || !pcy || !bcy)return 0;
    if(b->vdp.regs[0]&0x10u)return 0; /* Genesis H-int enabled. */
    if(c->pc!=pc && c->pc!=br)return 0;
    if(!t->total_lines || t->active_lines>=t->total_lines)return 0;
    if(t->scanline<=t->active_lines)lines=(uint32_t)t->active_lines-(uint32_t)t->scanline+1u;
    else lines=(uint32_t)t->total_lines-(uint32_t)t->scanline+(uint32_t)t->active_lines+1u;
    if(!lines)return 0;
    target=lines*lim;
    if(target<=initial_over)return 0;
    rem=target-initial_over;

    /* We enter only after generated code has observed the backward branch as
       taken.  The polled fixed-WRAM value cannot change until a machine event,
       so every intervening poll recreates the same CCR; SR therefore remains
       exactly the already-latched poll result while we collapse iterations. */
    if(c->pc==br){
        if(rem<=bcy){final_over=bcy-rem;c->pc=pc;steps=1u;goto reached;}
        rem-=bcy;steps++;c->pc=pc;
    }
    /* Consume only complete pairs strictly before the final VBlank boundary. */
    n=udiv32(rem-1u,pcy+bcy);rem-=n*(pcy+bcy);steps+=n*2u;
    if(rem<=pcy){final_over=pcy-rem;c->pc=br;steps++;goto reached;}
    rem-=pcy;steps++;c->pc=br;
    /* After the maximal strict pairs, the taken branch must cross/reach it. */
    final_over=bcy-rem;c->pc=pc;steps++;
reached:
    /* P109: H-int is disabled and no 68K instruction executes between these
       boundaries, so the intermediate scanlines are not CPU-observable.  Fold
       their timing state arithmetically and raise only the real final VBlank
       event.  This preserves P108's exact post-instruction PC at IRQ6 while
       removing up to ~224 advance_line() calls per wait. */
    {
        uint32_t reload=(uint32_t)b->vdp.regs[10],k;
        uint32_t h=(uint32_t)t->hint_counter;
        unsigned start=(unsigned)t->scanline;
        if(start==0u || start>t->active_lines){h=reload;k=(uint32_t)t->active_lines+1u;}
        else k=(uint32_t)t->active_lines-(uint32_t)start+1u;
        if(reload==0u)h=0u;
        else{uint32_t period=reload+1u,remk=k-udiv32(k,period)*period,hv=h+period-remk;if(hv>=period)hv-=period;h=hv;}
        t->hint_counter=(uint8_t)h;
        t->hblanks+=lines;
        b->hblank_active=1u;
        t->scanline=(uint16_t)(t->active_lines+1u);
        t->vblanks++;
        md2gba_bus_set_vblank(b,1);
        if((b->vdp.regs[1]&0x20u)&&md68k_raise_autovector(c,b,6u))t->irq6_count++;
        r=t->yield_each_frame?1:0;
    }
    t->total_cycles+=lines*lim-initial_over+final_over;
    t->cycle_accum=final_over;
    t->scheduled_overshoot=0u;
    t->scheduled_countdown=lim-final_over;
    t->idle_fast_forwards++;
    t->idle_lines_advanced+=lines;
    t->idle_steps_advanced+=steps;
    return r?2:1; /* 2 = batched and frontend-yielding, 1 = batched/no yield. */
}

MD2GBA_TIMING_HOT int md2gba_timing_scheduled_event(Md2Gba68k*c,Md2GbaBus*b,uint32_t steps,void*user){
    Md2GbaTiming*t=(Md2GbaTiming*)user;uint32_t lim;(void)steps;
    if(!t||!t->scheduled_mode)return 0;lim=scheduled_limit(t);
    if(t->cycle_mode){
        int r,batched;uint32_t over=t->scheduled_overshoot,prior=t->cycle_accum;
        t->scheduled_overshoot=0u;
        t->cycle_accum=over;t->total_cycles+=(lim-prior)+over;
        r=advance_line(t,c,b);
        /* If this exact event was already VBlank, the interrupt/yield is the
           observable event and no future scanline may be collapsed. */
        if(!r){
            batched=timing_idle_poll_to_vblank(t,c,b,lim,over);
            if(batched){t->idle_poll_hint=0u;return batched==2?1:0;}
        }
        t->idle_poll_hint=0u;
        if(over>=lim)over-=udiv32(over,lim)*lim;
        t->scheduled_countdown=lim-over;
        return r;
    }
    if(t->scanline_mode){int r;t->idle_poll_hint=0u;t->scanline_ticks=0;r=advance_line(t,c,b);t->scheduled_countdown=lim;return r;}
    t->idle_poll_hint=0u;t->instruction_ticks=0;t->vblanks++;md2gba_bus_set_vblank(b,1);if((b->vdp.regs[1]&0x20u)&&md68k_raise_autovector(c,b,6u))t->irq6_count++;md2gba_bus_set_vblank(b,0);t->scheduled_countdown=lim;return t->yield_each_frame?1:0;
}

int md2gba_timing_after_step(Md2Gba68k*c,Md2GbaBus*b,uint32_t executed_pc,uint32_t steps,void*user){Md2GbaTiming*t=(Md2GbaTiming*)user;uint32_t cyc;(void)steps;if(!t||!t->cycle_mode)return 0;cyc=md68k_estimate_cycles(b,executed_pc,c->pc);t->cycle_accum+=cyc;t->total_cycles+=cyc;while(t->cycle_accum>=t->cycles_per_line){int r;t->cycle_accum-=t->cycles_per_line;r=advance_line(t,c,b);if(r)return r;}return 0;}


/* P121 scheduler-safe semantic-span support.  This deliberately refuses any
   span that reaches VBlank or any span while H-int is enabled.  The caller is
   responsible for proving that its collapsed guest block cannot observe the
   intermediate HBlank/status transitions. */
MD2GBA_TIMING_HOT uint32_t md2gba_timing_scheduled_cycles_to_vblank(const Md2GbaTiming*t,uint32_t countdown){
    uint32_t lines,cpl;
    if(!t||!t->scheduled_mode||!t->cycle_mode||!countdown)return 0u;
    cpl=t->cycles_per_line?t->cycles_per_line:488u;
    if(countdown>cpl)countdown=cpl;
    if(!t->total_lines||t->active_lines>=t->total_lines)return 0u;
    if(t->scanline<=t->active_lines)lines=(uint32_t)t->active_lines-(uint32_t)t->scanline+1u;
    else lines=(uint32_t)t->total_lines-(uint32_t)t->scanline+(uint32_t)t->active_lines+1u;
    if(!lines)return 0u;
    return countdown+(lines-1u)*cpl;
}

/* P123: exact closed-form update for the H-int-disabled scanlines skipped by a
   semantic block.  The old implementation iterated advance-line bookkeeping
   once per crossed scanline (often ~200 times for a loader batch). */
static uint8_t timing_hint_after_active(uint8_t h,uint8_t reload,uint32_t n,int reset0){
    uint32_t period,q,m,hv;
    if(!n)return h;
    if(reset0)h=reload;
    if(!reload){return h>n?(uint8_t)(h-n):0u;}
    if(h>reload){uint32_t d=(uint32_t)h-(uint32_t)reload;if(n<=d)return (uint8_t)((uint32_t)h-n);n-=d;h=reload;}
    period=(uint32_t)reload+1u;q=udiv32(n,period);m=n-q*period;
    hv=(uint32_t)h+period-m;if(hv>=period)hv-=period;return (uint8_t)hv;
}

MD2GBA_TIMING_HOT int md2gba_timing_consume_unobservable_span_budgeted(Md2GbaTiming*t,Md2GbaBus*b,uint32_t*countdown,uint32_t cycles,uint32_t budget){
    uint32_t left,cpl,rem,q,crossed,phase,start,total,active,to_wrap,after_wrap;
    uint8_t reload,h;
    if(!t||!b||!countdown||!cycles||!budget||!t->scheduled_mode||!t->cycle_mode)return 0;
    if(b->vdp.regs[0]&0x10u)return 0;
    left=*countdown;cpl=t->cycles_per_line?t->cycles_per_line:488u;
    if(!left||left>cpl||cycles>=budget)return 0;
    if(cycles<left){*countdown=left-cycles;t->scheduled_countdown=*countdown;t->total_cycles+=cycles;return 1;}
    rem=cycles-left;q=udiv32(rem,cpl);crossed=1u+q;phase=rem-q*cpl;
    start=(uint32_t)t->scanline;total=(uint32_t)t->total_lines;active=(uint32_t)t->active_lines;
    if(!total||active>=total||start>=total)return 0;
    /* A strict pre-VBlank budget can never consume the active-line event. */
    if(start<=active){if(crossed>active-start)return 0;}
    else if(crossed>total-start+active)return 0;
    reload=b->vdp.regs[10];h=t->hint_counter;
    if(start<=active){
        h=timing_hint_after_active(h,reload,crossed,start==0u);
        t->scanline=(uint16_t)(start+crossed);
    }else{
        to_wrap=total-start;
        if(crossed<to_wrap){h=reload;t->scanline=(uint16_t)(start+crossed);}
        else{
            h=reload;after_wrap=crossed-to_wrap;t->scanline=(uint16_t)after_wrap;
            md2gba_bus_set_vblank(b,0);
            if(after_wrap)h=timing_hint_after_active(h,reload,after_wrap,1);
        }
    }
    t->hint_counter=h;b->hblank_active=1u;t->hblanks+=crossed;
    t->cycle_accum=phase;*countdown=phase?(cpl-phase):cpl;t->scheduled_countdown=*countdown;t->scheduled_overshoot=0u;t->total_cycles+=cycles;
    return 1;
}

MD2GBA_TIMING_HOT int md2gba_timing_consume_unobservable_span(Md2GbaTiming*t,Md2GbaBus*b,uint32_t*countdown,uint32_t cycles){
    uint32_t budget;if(!t||!b||!countdown||!cycles)return 0;
    budget=md2gba_timing_scheduled_cycles_to_vblank(t,*countdown);
    return md2gba_timing_consume_unobservable_span_budgeted(t,b,countdown,cycles,budget);
}
