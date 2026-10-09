#ifndef MD2GBA_TIMING_H
#define MD2GBA_TIMING_H
#include <stdint.h>
#include "md2gba_aot68k.h"
#include "md2gba_bus.h"
typedef struct Md2GbaTiming {
    uint32_t instruction_ticks,frame_interval,vblanks,irq6_count,hblanks,irq4_count;
    uint32_t scanline_ticks,scanline_interval;
    uint32_t cycle_accum,cycles_per_line,total_cycles;
    /* Phase 13: exact pre-instruction event deadline for the AOT scheduled hook. */
    uint32_t scheduled_countdown,scheduled_overshoot;
    /* P87: generic scheduler idle-loop batching. The AOT only raises the hint
       for a structurally proven fixed-WRAM polling loop; the timing layer owns
       the decision about how far it is safe to advance. Counters are diagnostic. */
    uint32_t idle_poll_hint,idle_fast_forwards,idle_lines_advanced;
    /* P108 exact cross-scanline poll batching descriptor/counters. */
    uint32_t idle_poll_pc,idle_branch_pc,idle_poll_cycles,idle_branch_cycles;
    uint32_t idle_steps_advanced;
    uint16_t scanline,total_lines,active_lines;
    uint8_t hint_counter,scanline_mode,cycle_mode,yield_each_frame,scheduled_mode;
} Md2GbaTiming;
void md2gba_timing_init(Md2GbaTiming *t,uint32_t frame_interval,int yield_each_frame);
void md2gba_timing_enable_scanlines(Md2GbaTiming *t,uint16_t total_lines,uint16_t active_lines);
void md2gba_timing_enable_cycles(Md2GbaTiming *t,uint16_t total_lines,uint16_t active_lines,uint16_t cycles_per_line);
int md2gba_timing_event(Md2Gba68k *cpu,Md2GbaBus *bus,uint32_t steps,void *user);
void md2gba_timing_enable_scheduled(Md2GbaTiming *t);
void md2gba_timing_sync_scheduled(Md2GbaTiming *t);
int md2gba_timing_scheduled_event(Md2Gba68k *cpu, Md2GbaBus *bus, uint32_t steps, void *user);
int md2gba_timing_after_step(Md2Gba68k *cpu,Md2GbaBus *bus,uint32_t executed_pc,uint32_t steps,void *user);

/* P121: generated semantic blocks may prove that intermediate scanlines are
   unobservable (for example, H-int disabled and no status reads) while still
   needing the real VBlank boundary.  These helpers expose only the safe budget
   and advance non-VBlank scanline bookkeeping without raising/deferring IRQs. */
uint32_t md2gba_timing_scheduled_cycles_to_vblank(const Md2GbaTiming *t,uint32_t countdown);
int md2gba_timing_consume_unobservable_span(Md2GbaTiming *t,Md2GbaBus *bus,uint32_t *countdown,uint32_t cycles);
/* P123: same transition when the caller already computed/proved the strict
   cycles-to-VBlank budget. Avoids recomputing that budget inside the helper. */
int md2gba_timing_consume_unobservable_span_budgeted(Md2GbaTiming *t,Md2GbaBus *bus,uint32_t *countdown,uint32_t cycles,uint32_t budget);
#endif
