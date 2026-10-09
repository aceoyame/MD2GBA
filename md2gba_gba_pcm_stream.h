#ifndef MD2GBA_GBA_PCM_STREAM_H
#define MD2GBA_GBA_PCM_STREAM_H
#include <stddef.h>
#include <stdint.h>

/* Generic Direct Sound A PCM streamer.

   Phase 17 established a working foreground-polled block scheduler. Phase 18
   removed FIFO resets at handoff and Phase 19 phase-locked source retargeting.
   Phase 20 added a conveyor mode that greatly reduced DMA retargets. Phase 21
   adds a foreground FIFO-pump mode that removes sound DMA source retargeting
   entirely: Timer0 clocks Direct Sound A, Timer1 divides the sample clock by
   16, Timer2 counts 16-sample FIFO service quanta, and foreground code writes
   four 32-bit words to FIFO A for each elapsed quantum. Phase 22 kept that DMA-free path but showed that even 16-sample cooperative
   foreground slices cannot meet a 0.488 ms FIFO deadline while translated 68K
   work is running. Phase 23 therefore moves only the four-word FIFO feed into a
   tiny Timer1 IRQ; future PCM generation remains cooperative in foreground.
   Phase 24 accelerates the sample-exact PSG producer and moves the hot IRQ and
   renderer paths into IWRAM in the hardware probe. */

#define MD2GBA_GBA_PCM_FIFO_BURST_SAMPLES 16u
#define MD2GBA_GBA_PCM_REFILL_SLICE_SAMPLES 64u

typedef void (*Md2GbaGbaPcmFillFn)(void *user, int8_t *dst, size_t samples);

/* Portable conveyor geometry helpers shared by the target runtime and host
   validation. A physical block advances monotonically until the final block,
   then wraps to zero through the mirrored guard. */
static inline uint8_t md2gba_gba_pcm_conveyor_next(uint8_t current, uint8_t ring_blocks) {
    unsigned next=(unsigned)current+1u;
    if(ring_blocks<3u) ring_blocks=3u;
    if(next>=(unsigned)ring_blocks) next=0u;
    return (uint8_t)next;
}

static inline unsigned md2gba_gba_pcm_conveyor_is_wrap(uint8_t current, uint8_t ring_blocks) {
    return md2gba_gba_pcm_conveyor_next(current,ring_blocks)==0u;
}

/* Phase 21 software-FIFO helpers. Timer2 is a free-running 16-bit count of
   completed 16-sample quanta, so unsigned subtraction naturally handles wrap. */
static inline uint16_t md2gba_gba_pcm_pump_burst_delta(uint16_t now, uint16_t seen) {
    return (uint16_t)(now-seen);
}

static inline unsigned md2gba_gba_pcm_pump_crosses_block(uint16_t offset,
                                                          uint16_t block_samples) {
    if(block_samples < MD2GBA_GBA_PCM_FIFO_BURST_SAMPLES) return 1u;
    return (unsigned)offset + MD2GBA_GBA_PCM_FIFO_BURST_SAMPLES >= (unsigned)block_samples;
}

static inline uint16_t md2gba_gba_pcm_pump_next_offset(uint16_t offset,
                                                        uint16_t block_samples) {
    unsigned next=(unsigned)offset + MD2GBA_GBA_PCM_FIFO_BURST_SAMPLES;
    if(!block_samples || next >= (unsigned)block_samples) return 0u;
    return (uint16_t)next;
}


/* Pure helper used by the target runtime and host tests. timer_count is the
   live Timer1 counter after it reloaded at -block_samples on the most recent
   block edge. Sound FIFO DMA advances its source in 16-sample bursts. */
static inline uint16_t md2gba_gba_pcm_catchup_skip(uint16_t block_samples,
                                                   uint16_t timer_count) {
    uint16_t reload=(uint16_t)(0u-block_samples);
    unsigned elapsed=(unsigned)(uint16_t)(timer_count-reload);
    unsigned bursts=(elapsed + 15u) >> 4;
    unsigned skip=16u*(1u+bursts);
    if(skip>(unsigned)block_samples) skip=(unsigned)block_samples;
    return (uint16_t)skip;
}


/* Number of samples until the preferred post-refill phase.  In steady-state
   FIFO DMA refills in 16-sample bursts.  Phase 2 is deliberately a little
   after the request point, leaving about 30 queued samples of runway. */
static inline uint16_t md2gba_gba_pcm_retarget_wait(uint16_t block_samples,
                                                    uint16_t timer_count,
                                                    uint16_t target_phase) {
    uint16_t reload=(uint16_t)(0u-block_samples);
    unsigned elapsed=(unsigned)(uint16_t)(timer_count-reload);
    unsigned wait=(target_phase-(elapsed & 15u)) & 15u;
    /* Do not wait across the Timer1 reload boundary: the counter reloads to
       -block_samples rather than wrapping through zero.  A very late service
       falls back to an immediate retarget instead of risking a dead wait. */
    if(elapsed >= (unsigned)block_samples || wait >= (unsigned)block_samples-elapsed) return 0u;
    return (uint16_t)wait;
}

typedef struct Md2GbaGbaPcmStream {
    int8_t *buffer[2];
    int8_t *wrap_guard;
    uint16_t block_samples;
    uint16_t timer_reload;
    volatile uint8_t playing;
    volatile uint8_t ready_mask;
    volatile uint8_t refill_mask;
    volatile uint8_t started;
    volatile uint8_t polling_mode;
    volatile uint8_t seamless_mode;
    volatile uint8_t conveyor_mode;
    volatile uint8_t pump_mode;
    volatile uint8_t irq_pump_mode;
    uint8_t ring_blocks;
    int8_t *ring_base;
    volatile uint8_t physical_block;
    volatile uint8_t refill_cursor;
    volatile uint32_t ring_ready_mask;
    volatile uint32_t ring_refill_mask;
    volatile uint32_t irq_blocks;
    volatile uint32_t refills;
    volatile uint32_t underruns;
    volatile uint32_t seamless_handoffs;
    volatile uint32_t wrap_retargets;
    volatile uint32_t pump_bursts;
    volatile uint32_t pump_late_quanta;
    volatile uint32_t pump_refill_slices;
    volatile uint16_t pump_burst_seen;
    volatile uint16_t pump_offset;
    volatile uint16_t pump_refill_offset;
    volatile uint8_t pump_refill_block;
    volatile uint8_t pump_refill_active;
    volatile uint16_t last_dma_skip;
    volatile uint16_t last_dma_phase;
} Md2GbaGbaPcmStream;

void md2gba_gba_pcm_stream_init(Md2GbaGbaPcmStream *s,
                                int8_t *buffer0,
                                int8_t *buffer1,
                                uint16_t block_samples,
                                uint32_t sample_rate_hz);

/* ring_storage must provide (3 * block_samples) writable bytes: A, B, mirror-A. */
void md2gba_gba_pcm_stream_init_seamless(Md2GbaGbaPcmStream *s,
                                         int8_t *ring_storage,
                                         uint16_t block_samples,
                                         uint32_t sample_rate_hz);

/* Phase 20 conveyor mode. ring_storage must provide
   (ring_blocks + 1) * block_samples writable bytes. The final block is a
   mirror guard for block zero. ring_blocks is clamped to 3..31. */
void md2gba_gba_pcm_stream_init_conveyor(Md2GbaGbaPcmStream *s,
                                         int8_t *ring_storage,
                                         uint16_t block_samples,
                                         uint8_t ring_blocks,
                                         uint32_t sample_rate_hz);

void md2gba_gba_pcm_stream_prefill(Md2GbaGbaPcmStream *s,
                                   Md2GbaGbaPcmFillFn fill,
                                   void *user);
/* Fill the transport ring with digital silence without advancing the
   attached synth state.  Full-system frontends use this at boot so the FIFO
   can start immediately while translated code is still configuring YM/PSG. */
void md2gba_gba_pcm_stream_prefill_silence(Md2GbaGbaPcmStream *s);
void md2gba_gba_pcm_stream_start(Md2GbaGbaPcmStream *s);
void md2gba_gba_pcm_stream_start_polling(Md2GbaGbaPcmStream *s);
/* Phase 21: conveyor ring + CPU FIFO writes; no sound DMA retargets. */
void md2gba_gba_pcm_stream_start_pumped(Md2GbaGbaPcmStream *s);
/* Phase 23: Timer1 IRQ feeds one 16-sample FIFO quantum; producer stays foreground. */
void md2gba_gba_pcm_stream_start_irq_pumped(Md2GbaGbaPcmStream *s);
unsigned md2gba_gba_pcm_stream_pump(Md2GbaGbaPcmStream *s);
unsigned md2gba_gba_pcm_stream_poll(Md2GbaGbaPcmStream *s);
void md2gba_gba_pcm_stream_stop(Md2GbaGbaPcmStream *s);
unsigned md2gba_gba_pcm_stream_service(Md2GbaGbaPcmStream *s,
                                       Md2GbaGbaPcmFillFn fill,
                                       void *user);

/* IRQ path remains available for frontends that use BIOS/CPU IRQ delivery. */
void md2gba_gba_pcm_irq_handler(void);
void md2gba_gba_pcm_irq_body(void);
void md2gba_gba_pcm_audio_irq_service(uint16_t pending);

/* Diagnostics: irq_state packs IME:IE(low8):IF(low8):CPSR(low8).
   timer_state packs TM1CNT:TM1D. */
uint32_t md2gba_gba_pcm_irq_state(void);
uint32_t md2gba_gba_pcm_timer1_state(void);

#endif
