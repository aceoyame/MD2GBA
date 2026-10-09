#ifndef MD2GBA_AOT_REGION_H
#define MD2GBA_AOT_REGION_H
#include <stdint.h>
#include "md2gba_aot68k.h"

enum {
    MD2GBA_AOT_OK = 0,
    MD2GBA_AOT_MISSING_PC = 1,
    MD2GBA_AOT_STEP_LIMIT = 2,
    MD2GBA_AOT_HOOK_ERROR = 3,
    MD2GBA_AOT_FALLBACK_ERROR = 4,
    MD2GBA_AOT_YIELD = 5,
    MD2GBA_AOT_EVENT_ERROR = 6,
    /* Internal sharded-runner continuation: execution moved to another AOT page. */
    MD2GBA_AOT_PAGE_EXIT = 7
};

/* Pass as start to continue at cpu->pc rather than overwriting it. */
#define MD2GBA_AOT_RESUME_PC 0xFFFFFFFFu

typedef int (*Md2GbaAotPcHook)(Md2Gba68k *cpu, Md2GbaBus *bus, uint32_t pc, void *user);
typedef int (*Md2GbaAotEventHook)(Md2Gba68k *cpu, Md2GbaBus *bus, uint32_t steps, void *user);
typedef int (*Md2GbaAotAfterStepHook)(Md2Gba68k *cpu, Md2GbaBus *bus, uint32_t executed_pc, uint32_t steps, void *user);

typedef struct Md2GbaAotHooks {
    /* Optional title/hardware service hook (VBlank, sound driver seams, etc.). */
    Md2GbaAotPcHook handle_pc;
    void *user;
    /* Optional title-agnostic interpreter/dynamic-code fallback seam. */
    Md2GbaAotPcHook fallback_pc;
    void *fallback_user;
    /* Optional generic machine/event scheduler. Return >0 to yield to the
       frontend at a frame boundary, 0 to continue, <0 on an event error. */
    Md2GbaAotEventHook event_step;
    void *event_user;
    /* Optional post-instruction timing hook. It receives the PC that actually
       executed, allowing generic 68K cycle estimation without changing AOT semantics. */
    Md2GbaAotAfterStepHook after_step;
    void *after_user;

    /* Phase 13 scheduled-event fast path.  ``scheduled_countdown`` is the
       number of complete 68K instructions that may execute before
       ``scheduled_event`` must run at the next pre-instruction boundary.
       The runner caches the countdown while executing native chains and
       synchronizes it whenever control returns to the caller or the callback.
       The callback must reload *scheduled_countdown with the next interval
       before returning 0.  When all three scheduled fields are populated the
       chain runner uses this path instead of event_step; event_step remains the
       exact every-instruction compatibility path for non-periodic schedulers. */
    Md2GbaAotEventHook scheduled_event;
    void *scheduled_user;
    uint32_t *scheduled_countdown;
    /* Cycle-mode carry when an instruction crosses a scheduled scanline boundary.
       Generated AOT records the excess cycles here before invoking the callback;
       the timing core folds them into the next deadline exactly like the step path. */
    uint32_t *scheduled_overshoot;
    /* Optional structural idle-loop hint. Generated code sets this before a
       scheduled callback only when it has proven that a backward polling loop
       reads fixed WRAM and can make progress solely through an external machine
       event. Timing backends may then advance directly to their next observable
       event instead of waking the guest once per otherwise-empty scanline. */
    uint32_t *scheduled_idle_hint;
    /* P108: optional exact idle-poll descriptor.  Appended so previously built
       page objects keep the same hook-field offsets.  The descriptor is
       poll_pc, branch_pc, poll_cycles, taken_branch_cycles.  Timing may report
       the number of logically skipped poll instructions through the final ptr. */
    uint32_t *scheduled_idle_descriptor;
    uint32_t *scheduled_idle_steps;
} Md2GbaAotHooks;

#endif
