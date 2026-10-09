#include "md2gba_gba_pcm_stream.h"

#define REG8(a) (*(volatile uint8_t*)(a))
#define REG16(a) (*(volatile uint16_t*)(a))
#define REG32(a) (*(volatile uint32_t*)(a))
#define SOUNDCNT_H REG16(0x04000082u)
#define SOUNDCNT_X REG16(0x04000084u)
#define SOUNDBIAS_H REG8(0x04000089u)
#define FIFO_A REG32(0x040000A0u)
#define DMA1SAD REG32(0x040000BCu)
#define DMA1DAD REG32(0x040000C0u)
#define DMA1CNT_L REG16(0x040000C4u)
#define DMA1CNT_H REG16(0x040000C6u)
#define TM0D REG16(0x04000100u)
#define TM0CNT REG16(0x04000102u)
#define TM1D REG16(0x04000104u)
#define TM1CNT REG16(0x04000106u)
#define TM2D REG16(0x04000108u)
#define TM2CNT REG16(0x0400010Au)
#define REG_IE REG16(0x04000200u)
#define REG_IF REG16(0x04000202u)
#define REG_IME REG16(0x04000208u)
#define IRQ_VECTOR REG32(0x03007FFCu)
#define IRQ_TIMER1 0x0010u
#define IRQ_TIMER2 0x0020u

#if defined(MD2GBA_GBA_IWRAM)
#define MD2GBA_GBA_HOT __attribute__((section(".iwram")))
#else
#define MD2GBA_GBA_HOT
#endif

Md2GbaGbaPcmStream *md2gba_gba_pcm_active_stream;
#define active_stream md2gba_gba_pcm_active_stream

static void cpu_irq_enable(void) {
    __asm__ volatile(
        "mrs r0, cpsr\n"
        "bic r0, r0, #0x80\n"
        "msr cpsr_c, r0\n"
        : : : "r0", "memory", "cc");
}

static uint32_t udiv32_init(uint32_t n, uint32_t d) {
    uint32_t q=0u,r=0u; int bit;
    if(!d) return 0u;
    for(bit=31;bit>=0;bit--) {
        r=(r<<1)|((n>>(unsigned)bit)&1u);
        if(r>=d){r-=d;q|=(1u<<(unsigned)bit);}
    }
    return q;
}

static void copy_fifo_guard(Md2GbaGbaPcmStream *s) {
    unsigned i;
    if(!s || !s->seamless_mode || !s->wrap_guard || !s->buffer[0]) return;
    for(i=0u;i<s->block_samples;i++) s->wrap_guard[i]=s->buffer[0][i];
}

static void copy_conveyor_guard(Md2GbaGbaPcmStream *s) {
    unsigned i;
    if(!s || !s->conveyor_mode || !s->wrap_guard || !s->ring_base) return;
    for(i=0u;i<MD2GBA_GBA_PCM_FIFO_BURST_SAMPLES;i++) s->wrap_guard[i]=s->ring_base[i];
}

static void dma_program_pointer(int8_t *src, int reset_fifo) {
    DMA1CNT_H=0u;
#if defined(MD2GBA_PSC_FIFO_DMA)
    if(reset_fifo) DMA1CNT_H=0x0400u; /* PSC preloads 32-bit mode while disabled. */
#endif
    /* Hardware needs a short settle after disabling repeat-mode sound DMA
       before its source/control registers are re-latched. The FIFO itself is
       reset once at startup only; seamless handoffs do not touch SOUNDCNT_H. */
    __asm__ volatile("nop; nop; nop; nop; nop; nop; nop; nop" ::: "memory");
#if defined(MD2GBA_PSC_FIFO_DMA)
    if(reset_fifo) SOUNDCNT_H=0x0B0Eu;
#else
    if(reset_fifo) SOUNDCNT_H=0x0B04u;
#endif
    DMA1SAD=(uint32_t)src;
    DMA1DAD=(uint32_t)&FIFO_A;
    DMA1CNT_L=4u;
#if defined(MD2GBA_PSC_FIFO_DMA)
    DMA1CNT_H=0xB600u; /* PSC-observed FIFO A DMA policy. */
#else
    DMA1CNT_H=0xB640u; /* legacy MD2GBA FIFO policy. */
#endif
}

static void dma_program_source(Md2GbaGbaPcmStream *s, unsigned index, unsigned skip, int reset_fifo) {
    dma_program_pointer(s->buffer[index&1u] + skip,reset_fifo);
}

static void stream_common_init(Md2GbaGbaPcmStream *s,
                               int8_t *buffer0,
                               int8_t *buffer1,
                               int8_t *guard,
                               uint16_t block_samples,
                               uint32_t sample_rate_hz,
                               unsigned seamless) {
    uint32_t clocks_per_sample;
    if(!block_samples) block_samples=2048u;
    if(!sample_rate_hz) sample_rate_hz=32768u;
#if defined(MD2GBA_PSC_FRAME_TIMING)
    /* PSC selects audio quality by samples-per-video-frame and derives the
       sample timer from the 280,896 GBA clocks in one 59.7275-Hz frame. */
    clocks_per_sample=udiv32_init(280896u,(uint32_t)block_samples);
#else
    clocks_per_sample=udiv32_init(16777216u,sample_rate_hz);
#endif
    if(!clocks_per_sample) clocks_per_sample=1u;
    if(clocks_per_sample>65535u) clocks_per_sample=65535u;
    s->buffer[0]=buffer0; s->buffer[1]=buffer1; s->wrap_guard=guard;
    s->block_samples=block_samples;
    s->timer_reload=(uint16_t)(0x10000u-clocks_per_sample);
    s->playing=0u; s->ready_mask=0u; s->refill_mask=0u; s->started=0u; s->polling_mode=0u;
    s->seamless_mode=(uint8_t)(seamless?1u:0u);
    s->conveyor_mode=0u; s->pump_mode=0u; s->irq_pump_mode=0u; s->ring_blocks=0u; s->ring_base=0; s->physical_block=0u; s->refill_cursor=0u;
    s->ring_ready_mask=0u; s->ring_refill_mask=0u;
    s->irq_blocks=0u; s->refills=0u; s->underruns=0u; s->seamless_handoffs=0u; s->wrap_retargets=0u;
    s->pump_bursts=0u; s->pump_late_quanta=0u; s->pump_refill_slices=0u;
    s->pump_burst_seen=0u; s->pump_offset=0u; s->pump_refill_offset=0u;
    s->pump_refill_block=0u; s->pump_refill_active=0u;
    s->last_dma_skip=0u; s->last_dma_phase=0u;
}

void md2gba_gba_pcm_stream_init(Md2GbaGbaPcmStream *s,
                                int8_t *buffer0,
                                int8_t *buffer1,
                                uint16_t block_samples,
                                uint32_t sample_rate_hz) {
    stream_common_init(s,buffer0,buffer1,0,block_samples,sample_rate_hz,0u);
}

void md2gba_gba_pcm_stream_init_seamless(Md2GbaGbaPcmStream *s,
                                         int8_t *ring_storage,
                                         uint16_t block_samples,
                                         uint32_t sample_rate_hz) {
    if(!block_samples) block_samples=2048u;
    stream_common_init(s,
                       ring_storage,
                       ring_storage ? ring_storage+block_samples : 0,
                       ring_storage ? ring_storage+(uint32_t)block_samples*2u : 0,
                       block_samples,
                       sample_rate_hz,
                       1u);
}

void md2gba_gba_pcm_stream_init_conveyor(Md2GbaGbaPcmStream *s,
                                         int8_t *ring_storage,
                                         uint16_t block_samples,
                                         uint8_t ring_blocks,
                                         uint32_t sample_rate_hz) {
    if(!block_samples) block_samples=2048u;
    if(ring_blocks<3u) ring_blocks=3u;
    if(ring_blocks>31u) ring_blocks=31u;
    stream_common_init(s,
                       ring_storage,
                       ring_storage ? ring_storage+block_samples : 0,
                       ring_storage ? ring_storage+(uint32_t)block_samples*(uint32_t)ring_blocks : 0,
                       block_samples,
                       sample_rate_hz,
                       1u);
    s->conveyor_mode=1u;
    s->ring_blocks=ring_blocks;
    s->ring_base=ring_storage;
}

void md2gba_gba_pcm_stream_prefill_silence(Md2GbaGbaPcmStream *s) {
    unsigned i,n;
    if(!s) return;
    if(s->conveyor_mode && s->ring_base && s->ring_blocks) {
        uint32_t ready=0u;
        n=(unsigned)s->block_samples*(unsigned)s->ring_blocks;
        for(i=0u;i<n;i++) s->ring_base[i]=0;
        for(i=0u;i<s->ring_blocks;i++) ready|=(1u<<i);
        if(s->wrap_guard) {
            for(i=0u;i<MD2GBA_GBA_PCM_FIFO_BURST_SAMPLES;i++) s->wrap_guard[i]=0;
        }
        s->ring_ready_mask=ready;
        s->ring_refill_mask=0u;
        s->ready_mask=3u;
    } else if(s->buffer[0] && s->buffer[1]) {
        for(i=0u;i<s->block_samples;i++) { s->buffer[0][i]=0; s->buffer[1][i]=0; }
        if(s->wrap_guard) for(i=0u;i<s->block_samples;i++) s->wrap_guard[i]=0;
        s->ready_mask=3u;
    }
}

void md2gba_gba_pcm_stream_prefill(Md2GbaGbaPcmStream *s,
                                   Md2GbaGbaPcmFillFn fill,
                                   void *user) {
    if(!s || !fill) return;
    if(s->conveyor_mode && s->ring_base && s->ring_blocks) {
        unsigned i;
        uint32_t ready=0u;
        for(i=0u;i<s->ring_blocks;i++) {
            fill(user,s->ring_base+(uint32_t)i*s->block_samples,s->block_samples);
            ready|=(1u<<i);
        }
        copy_conveyor_guard(s);
        s->ring_ready_mask=ready;
        s->ring_refill_mask=0u;
        s->ready_mask=3u;
    } else if(s->buffer[0] && s->buffer[1]) {
        fill(user,s->buffer[0],s->block_samples);
        fill(user,s->buffer[1],s->block_samples);
        copy_fifo_guard(s);
        s->ready_mask=3u;
    }
}

static unsigned seamless_dma_skip_phase_locked(Md2GbaGbaPcmStream *s) {
    uint16_t reload=(uint16_t)(0u-s->block_samples);
    uint16_t now=TM1D;
    uint16_t wait=md2gba_gba_pcm_retarget_wait(s->block_samples,now,2u);
    uint16_t target;
    /* Wait at most 15 sample clocks until just after a known FIFO DMA burst.
       The old DMA remains enabled during this wait, so audio continues. */
    if(wait) {
        target=(uint16_t)(now+wait);
        while((uint16_t)(TM1D-target) & 0x8000u) { }
    }
    now=TM1D;
    s->last_dma_phase=(uint16_t)(((uint16_t)(now-reload)) & 15u);
    return (unsigned)md2gba_gba_pcm_catchup_skip(s->block_samples,now);
}

static void stream_advance_block(Md2GbaGbaPcmStream *s) {
    if(s->conveyor_mode && s->ring_base && s->ring_blocks) {
        unsigned old=s->physical_block;
        unsigned next=md2gba_gba_pcm_conveyor_next((uint8_t)old,s->ring_blocks);
        uint32_t nextbit;
        nextbit=(1u<<next);
        if(!(s->ring_ready_mask&nextbit)) s->underruns++;
        else s->ring_ready_mask&=~nextbit;
        s->physical_block=(uint8_t)next;
        s->playing=(uint8_t)(next&1u);
        s->ring_refill_mask|=(1u<<old);
        s->seamless_handoffs++;
        if(next==0u) {
            unsigned skip=seamless_dma_skip_phase_locked(s);
            /* The old source is already inside a byte-identical mirror of
               block zero. Retarget only at this rare wrap, from guard+skip
               to ring0+skip; ordinary block boundaries never touch DMA. */
            dma_program_pointer(s->ring_base+skip,0);
            s->last_dma_skip=(uint16_t)skip;
            s->wrap_retargets++;
        }
        s->irq_blocks++;
        return;
    }
    {
        unsigned old=s->playing&1u;
        unsigned next=old^1u;
        uint8_t nextbit=(uint8_t)(1u<<next);
        if(!(s->ready_mask&nextbit)) s->underruns++;
        else s->ready_mask=(uint8_t)(s->ready_mask&~nextbit);
        s->playing=(uint8_t)next;
        if(s->seamless_mode) {
            unsigned skip=seamless_dma_skip_phase_locked(s);
            dma_program_source(s,next,skip,0);
            s->last_dma_skip=(uint16_t)skip;
            s->seamless_handoffs++;
        } else {
            dma_program_source(s,next,0u,1);
        }
        s->refill_mask=(uint8_t)(s->refill_mask|(1u<<old));
        s->irq_blocks++;
    }
}

static uint32_t pcm_pack4(const int8_t *p) {
    return (uint32_t)(uint8_t)p[0] |
           ((uint32_t)(uint8_t)p[1] << 8) |
           ((uint32_t)(uint8_t)p[2] << 16) |
           ((uint32_t)(uint8_t)p[3] << 24);
}

static __attribute__((always_inline)) inline void conveyor_advance_no_dma(Md2GbaGbaPcmStream *s) {
    unsigned old=s->physical_block;
    unsigned next=md2gba_gba_pcm_conveyor_next((uint8_t)old,s->ring_blocks);
    uint32_t nextbit=(1u<<next);
    if(!(s->ring_ready_mask&nextbit)) s->underruns++;
    else s->ring_ready_mask&=~nextbit;
    s->physical_block=(uint8_t)next;
    s->playing=(uint8_t)(next&1u);
    s->ring_refill_mask|=(1u<<old);
    s->seamless_handoffs++;
    s->irq_blocks++;
}

static __attribute__((always_inline)) inline void pump_write_quantum(Md2GbaGbaPcmStream *s, int write_fifo) {
    const int8_t *p=s->ring_base + (uint32_t)s->physical_block*s->block_samples + s->pump_offset;
    unsigned i;
    if(write_fifo) {
        /* Ring storage and every quantum offset are 4-byte aligned. A native
           little-endian word load has exactly the byte order FIFO A expects. */
        for(i=0u;i<MD2GBA_GBA_PCM_FIFO_BURST_SAMPLES;i+=4u)
            FIFO_A=*(const uint32_t*)(const void*)(p+i);
    }
    s->pump_offset=md2gba_gba_pcm_pump_next_offset(s->pump_offset,s->block_samples);
    if(s->pump_offset==0u) conveyor_advance_no_dma(s);
    s->pump_bursts++;
}

static void psc_sound_hw_prepare(void) {
#if defined(MD2GBA_PSC_FIFO_DMA)
    /* PSC startup at ROM $2F98 preserves the low six SOUNDBIAS-high bits and
       selects PWM mode bit 14 before enabling Direct Sound. */
    SOUNDBIAS_H=(uint8_t)((SOUNDBIAS_H&0x3Fu)|0x40u);
#endif
}

void md2gba_gba_pcm_stream_start(Md2GbaGbaPcmStream *s) {
    if(!s || !s->buffer[0] || !s->buffer[1] || !s->block_samples) return;
    REG_IME=0u;
    active_stream=s;
    IRQ_VECTOR=(uint32_t)md2gba_gba_pcm_irq_handler;
    REG_IF=IRQ_TIMER1;
    REG_IE=(uint16_t)(REG_IE|IRQ_TIMER1);
    psc_sound_hw_prepare();
#if defined(MD2GBA_PSC_FIFO_DMA)
    SOUNDCNT_X=0x008Fu;
#else
    SOUNDCNT_X=0x0080u;
#endif
    s->playing=0u; s->physical_block=0u; s->refill_cursor=0u;
    if(s->conveyor_mode) s->ring_ready_mask&=~1u;
    else s->ready_mask=(uint8_t)(s->ready_mask&~1u);
    dma_program_pointer(s->conveyor_mode?s->ring_base:s->buffer[0],1);
    TM1CNT=0u;
    TM1D=(uint16_t)(0u-s->block_samples);
    TM1CNT=0x00C4u;
    TM0CNT=0u;
    TM0D=s->timer_reload;
    TM0CNT=0x0080u;
    s->polling_mode=0u;
    s->pump_mode=0u;
    s->started=1u;
    REG_IME=1u;
    cpu_irq_enable();
}

void md2gba_gba_pcm_stream_start_polling(Md2GbaGbaPcmStream *s) {
    if(!s || !s->buffer[0] || !s->buffer[1] || !s->block_samples) return;
    REG_IME=0u;
    REG_IE=(uint16_t)(REG_IE&~IRQ_TIMER1);
    REG_IF=IRQ_TIMER1;
    active_stream=s;
    psc_sound_hw_prepare();
#if defined(MD2GBA_PSC_FIFO_DMA)
    SOUNDCNT_X=0x008Fu;
#else
    SOUNDCNT_X=0x0080u;
#endif
    s->playing=0u; s->physical_block=0u; s->refill_cursor=0u;
    if(s->conveyor_mode) s->ring_ready_mask&=~1u;
    else s->ready_mask=(uint8_t)(s->ready_mask&~1u);
    dma_program_pointer(s->conveyor_mode?s->ring_base:s->buffer[0],1);
    TM1CNT=0u;
    TM1D=(uint16_t)(0u-s->block_samples);
    TM1CNT=0x00C4u; /* latch IF, but IE/IME prevent CPU IRQ delivery */
    TM0CNT=0u;
    TM0D=s->timer_reload;
    TM0CNT=0x0080u;
    s->polling_mode=1u;
    s->pump_mode=0u;
    s->started=1u;
}

void md2gba_gba_pcm_stream_start_pumped(Md2GbaGbaPcmStream *s) {
    unsigned i;
    if(!s || !s->conveyor_mode || !s->ring_base || !s->ring_blocks ||
       s->block_samples<32u || (s->block_samples&15u)) return;
    REG_IME=0u;
    REG_IE=(uint16_t)(REG_IE&~(IRQ_TIMER1|IRQ_TIMER2));
    REG_IF=(uint16_t)(IRQ_TIMER1|IRQ_TIMER2);
    active_stream=s;
    SOUNDCNT_X=0x0080u;
    DMA1CNT_H=0u;
    SOUNDCNT_H=0x0B04u; /* Direct Sound A full volume L/R + FIFO A reset. */
    s->playing=0u; s->physical_block=0u; s->refill_cursor=0u;
    s->ring_ready_mask&=~1u;
    s->pump_mode=1u; s->polling_mode=0u; s->started=0u;
    s->pump_bursts=0u; s->pump_late_quanta=0u; s->pump_refill_slices=0u;
    s->pump_burst_seen=0u; s->pump_offset=0u; s->pump_refill_offset=0u;
    s->pump_refill_block=0u; s->pump_refill_active=0u;
    /* Prime the complete 32-byte FIFO before any sample timer can consume it. */
    for(i=0u;i<32u;i+=4u) FIFO_A=pcm_pack4(s->ring_base+i);
    s->pump_offset=32u;
    TM2CNT=0u; TM2D=0u;
    TM1CNT=0u; TM1D=(uint16_t)(0u-MD2GBA_GBA_PCM_FIFO_BURST_SAMPLES);
    TM2CNT=0x0084u; /* Count Timer1 overflows. */
    TM1CNT=0x0084u; /* Cascade from Timer0; overflow every 16 samples. */
    TM0CNT=0u; TM0D=s->timer_reload; TM0CNT=0x0080u;
    s->started=1u;
}

void md2gba_gba_pcm_stream_start_irq_pumped(Md2GbaGbaPcmStream *s) {
    unsigned i;
    if(!s || !s->conveyor_mode || !s->ring_base || !s->ring_blocks ||
       s->block_samples<32u || (s->block_samples&15u)) return;
    REG_IME=0u;
    REG_IE=(uint16_t)((REG_IE&~IRQ_TIMER2)|IRQ_TIMER1);
    REG_IF=(uint16_t)(IRQ_TIMER1|IRQ_TIMER2);
    active_stream=s;
    IRQ_VECTOR=(uint32_t)md2gba_gba_pcm_irq_handler;
    SOUNDCNT_X=0x0080u;
    DMA1CNT_H=0u;
    SOUNDCNT_H=0x0B04u;
    s->playing=0u; s->physical_block=0u; s->refill_cursor=0u;
    s->ring_ready_mask&=~1u;
    s->pump_mode=1u; s->irq_pump_mode=1u; s->polling_mode=0u; s->started=0u;
    s->pump_bursts=0u; s->pump_late_quanta=0u; s->pump_refill_slices=0u;
    s->pump_burst_seen=0u; s->pump_offset=0u; s->pump_refill_offset=0u;
    s->pump_refill_block=0u; s->pump_refill_active=0u;
    /* Prime all 32 FIFO bytes. Timer1 then fires every 16 consumed samples;
       the IRQ handler restores exactly those 16 samples. */
    for(i=0u;i<32u;i+=4u) FIFO_A=pcm_pack4(s->ring_base+i);
    s->pump_offset=32u;
    TM2CNT=0u; TM2D=0u;
    TM1CNT=0u; TM1D=(uint16_t)(0u-MD2GBA_GBA_PCM_FIFO_BURST_SAMPLES);
    TM2CNT=0x0084u; /* hardware truth counter: one tick per Timer1 overflow */
    TM1CNT=0x00C4u; /* cascade + IRQ + enable, every 16 samples */
    TM0CNT=0u; TM0D=s->timer_reload; TM0CNT=0x0080u;
    s->started=1u;
    REG_IME=1u;
    cpu_irq_enable();
}

unsigned md2gba_gba_pcm_stream_pump(Md2GbaGbaPcmStream *s) {
    uint16_t now,delta,drop;
    unsigned done=0u;
    if(!s || !s->started || !s->pump_mode || s->irq_pump_mode) return 0u;
    now=TM2D;
    delta=md2gba_gba_pcm_pump_burst_delta(now,s->pump_burst_seen);
    if(!delta) return 0u;
    /* FIFO A holds 32 samples. If more than two 16-sample deadlines elapsed,
       an underrun already occurred. Advance over the irrecoverably late source
       quanta without trying to overfill the hardware FIFO, then enqueue the
       newest two quanta and resynchronize to Timer2. */
    if(delta>2u) {
        drop=(uint16_t)(delta-2u);
        s->underruns+=(uint32_t)drop;
        s->pump_late_quanta+=(uint32_t)drop;
        while(drop--) pump_write_quantum(s,0);
        delta=2u;
    }
    while(delta--) { pump_write_quantum(s,1); done++; }
    s->pump_burst_seen=now;
    return done;
}

unsigned md2gba_gba_pcm_stream_poll(Md2GbaGbaPcmStream *s) {
    uint16_t pending;
    if(!s || !s->started || !s->polling_mode) return 0u;
    pending=REG_IF;
    if(!(pending&IRQ_TIMER1)) return 0u;
    REG_IF=IRQ_TIMER1;
    stream_advance_block(s);
    return 1u;
}

void md2gba_gba_pcm_stream_stop(Md2GbaGbaPcmStream *s) {
    uint16_t old_ime=REG_IME;
    REG_IME=0u;
    TM0CNT=0u; TM1CNT=0u; TM2CNT=0u; DMA1CNT_H=0u;
    REG_IE=(uint16_t)(REG_IE&~(IRQ_TIMER1|IRQ_TIMER2));
    REG_IF=(uint16_t)(IRQ_TIMER1|IRQ_TIMER2);
    SOUNDCNT_H=0x0800u;
    if(s) { s->started=0u; s->polling_mode=0u; s->pump_mode=0u; s->irq_pump_mode=0u; }
    if(active_stream==s) active_stream=0;
    REG_IME=old_ime;
}

static uint16_t ime_lock(void) {
    uint16_t old=REG_IME;
    REG_IME=0u;
    return old;
}

static void ime_restore(uint16_t old) { REG_IME=old; }

/* Phase 23: refill at most one bounded PCM slice per foreground service call.
   The PSG/FM producer state is sequential, so once a consumed block is claimed
   we finish that block in-order before claiming the next one.  This avoids the
   multi-millisecond 2048-sample render burst that could starve the 32-byte
   Direct Sound FIFO even though the pump itself was scheduled frequently. */
static MD2GBA_GBA_HOT unsigned pump_refill_one_slice(Md2GbaGbaPcmStream *s,
                                      Md2GbaGbaPcmFillFn fill,
                                      void *user) {
    uint16_t old_ime;
    unsigned block,n,remaining;
    if(!s || !fill || !s->conveyor_mode ||
       !s->ring_base || !s->ring_blocks) return 0u;

    if(!s->pump_refill_active) {
        uint32_t mask;
        unsigned scan,i,found=s->ring_blocks;
        old_ime=ime_lock();
        mask=s->ring_refill_mask;
        for(scan=0u;scan<s->ring_blocks;scan++) {
            i=(unsigned)s->refill_cursor+scan;
            if(i>=s->ring_blocks) i-=s->ring_blocks;
            if(mask&(1u<<i)) { found=i; break; }
        }
        if(found<s->ring_blocks) {
            s->ring_refill_mask&=~(1u<<found);
            s->pump_refill_block=(uint8_t)found;
            s->pump_refill_offset=0u;
            s->pump_refill_active=1u;
        }
        ime_restore(old_ime);
        if(found>=s->ring_blocks) return 0u;
    }

    block=(unsigned)s->pump_refill_block;
    remaining=(unsigned)s->block_samples-(unsigned)s->pump_refill_offset;
    n=remaining<MD2GBA_GBA_PCM_REFILL_SLICE_SAMPLES ? remaining : MD2GBA_GBA_PCM_REFILL_SLICE_SAMPLES;
    fill(user,
         s->ring_base+(uint32_t)block*s->block_samples+s->pump_refill_offset,
         n);
    s->pump_refill_offset=(uint16_t)(s->pump_refill_offset+n);
    s->pump_refill_slices++;

    if(s->pump_refill_offset>=s->block_samples) {
        old_ime=ime_lock();
        s->ring_ready_mask|=(1u<<block);
        s->refill_cursor=(uint8_t)(block+1u);
        if(s->refill_cursor>=s->ring_blocks) s->refill_cursor=0u;
        s->refills++;
        if(block==0u && !s->pump_mode) copy_conveyor_guard(s);
        s->pump_refill_offset=0u;
        s->pump_refill_active=0u;
        ime_restore(old_ime);
        return 1u;
    }
    return 0u;
}

MD2GBA_GBA_HOT unsigned md2gba_gba_pcm_stream_service(Md2GbaGbaPcmStream *s,
                                       Md2GbaGbaPcmFillFn fill,
                                       void *user) {
    unsigned done=0u;
    uint16_t old_ime;
    if(!s || !fill) return 0u;
    if(s->pump_mode) {
        if(!s->irq_pump_mode) (void)md2gba_gba_pcm_stream_pump(s);
        done+=pump_refill_one_slice(s,fill,user);
        if(!s->irq_pump_mode) (void)md2gba_gba_pcm_stream_pump(s);
        return done;
    }
    else if(s->polling_mode) (void)md2gba_gba_pcm_stream_poll(s);
    if(s->conveyor_mode && s->ring_base && s->ring_blocks) {
        /* P88: DMA consumption is the producer clock.  Refill at most one
           bounded slice per service opportunity; do not burst-render a whole
           block after Timer1 marks it consumed. */
        done+=pump_refill_one_slice(s,fill,user);
        return done;
    }
    for(;;) {
        uint8_t mask;
        old_ime=ime_lock();
        mask=s->refill_mask;
        if(mask&1u) s->refill_mask=(uint8_t)(mask&~1u);
        else if(mask&2u) s->refill_mask=(uint8_t)(mask&~2u);
        ime_restore(old_ime);
        if(mask&1u) {
            fill(user,s->buffer[0],s->block_samples);
            copy_fifo_guard(s);
            old_ime=ime_lock();
            s->ready_mask=(uint8_t)(s->ready_mask|1u); s->refills++;
            ime_restore(old_ime);
            done++;
        } else if(mask&2u) {
            fill(user,s->buffer[1],s->block_samples);
            old_ime=ime_lock();
            s->ready_mask=(uint8_t)(s->ready_mask|2u); s->refills++;
            ime_restore(old_ime);
            done++;
        } else break;
    }
    return done;
}

__attribute__((section(".iwram"))) void md2gba_gba_pcm_audio_irq_service(uint16_t pending) {
    Md2GbaGbaPcmStream *s=active_stream;
    if((pending&IRQ_TIMER1) && s && s->started) {
        if(s->irq_pump_mode) {
            uint16_t now=TM2D;
            uint16_t delta=(uint16_t)(now-s->pump_burst_seen);
            if(delta>1u) {
                uint16_t drop=(uint16_t)(delta-1u);
                s->underruns+=(uint32_t)drop;
                s->pump_late_quanta+=(uint32_t)drop;
                while(drop--) pump_write_quantum(s,0);
            }
            pump_write_quantum(s,1);
            s->pump_burst_seen=now;
        } else if(!s->polling_mode) stream_advance_block(s);
    }
}

#undef MD2GBA_GBA_HOT

uint32_t md2gba_gba_pcm_irq_state(void) {
    uint32_t cpsr;
    __asm__ volatile("mrs %0, cpsr" : "=r"(cpsr));
    return ((uint32_t)(REG_IME & 0x00FFu) << 24) |
           ((uint32_t)(REG_IE  & 0x00FFu) << 16) |
           ((uint32_t)(REG_IF  & 0x00FFu) << 8)  |
           (cpsr & 0xFFu);
}

uint32_t md2gba_gba_pcm_timer1_state(void) {
    return ((uint32_t)TM1CNT << 16) | (uint32_t)TM1D;
}
