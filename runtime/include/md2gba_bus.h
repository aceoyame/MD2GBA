#ifndef MD2GBA_BUS_H
#define MD2GBA_BUS_H
#include <stddef.h>
#include <stdint.h>
#include "md2gba_audio.h"

enum {
    MD2GBA_PAD_UP    = 1u << 0,
    MD2GBA_PAD_DOWN  = 1u << 1,
    MD2GBA_PAD_LEFT  = 1u << 2,
    MD2GBA_PAD_RIGHT = 1u << 3,
    MD2GBA_PAD_B     = 1u << 4,
    MD2GBA_PAD_C     = 1u << 5,
    MD2GBA_PAD_A     = 1u << 6,
    MD2GBA_PAD_START = 1u << 7
};

typedef enum Md2GbaVdpTarget {
    MD2GBA_VDP_NONE = 0,
    MD2GBA_VDP_VRAM,
    MD2GBA_VDP_CRAM,
    MD2GBA_VDP_VSRAM
} Md2GbaVdpTarget;

typedef struct Md2GbaVdp {
    uint16_t data_word;
    uint16_t control_word;
    uint32_t control_long;
    uint32_t data_writes;
    uint32_t control_writes;
    uint8_t regs[0x20];
    uint8_t vram[0x10000];
    uint16_t cram[0x40];
    uint16_t vsram[0x40];
    uint16_t address;
    uint8_t code;
    uint8_t pending_control;
    uint16_t first_control_word;
    Md2GbaVdpTarget target;
    uint32_t register_writes;
    uint32_t dma_transfers;
    uint32_t dma_words;
    uint32_t unsupported_dma_modes;
    uint8_t dma_busy;
    /* Renderer-facing dirtiness is part of the generic virtual VDP, not a
       title profile. VRAM is tracked in 256-byte pages (256 bits total). */
    uint32_t vram_dirty_pages[8];
    uint32_t vram_dirty_tiles[64];
    uint32_t cram_dirty_mask[2];
    uint32_t vsram_dirty_mask[2];
    uint32_t regs_dirty_mask;
    uint32_t vram_epoch, cram_epoch, vsram_epoch, regs_epoch;
    /* Phase 50 diagnostic trace: first raw VDP port events. Appended so existing field offsets remain stable. */
    uint8_t trace_type[32]; /* 1=control, 2=data */
    uint16_t trace_value[32];
    uint16_t trace_addr[32];
    uint8_t trace_code[32], trace_target[32], trace_pending[32];
    uint32_t trace_pc[32];
    uint8_t trace_count;
    /* Phase 52: snapshot the first memory-DMA command before it mutates registers. */
    uint8_t dma_diag_valid, dma_diag_target, dma_diag_code;
    uint16_t dma_diag_words_reg, dma_diag_dst;
    uint32_t dma_diag_src_word, dma_diag_src_byte, dma_diag_cmd;
    uint16_t dma_diag_source_words[8];
    /* Phase 53: first non-memory DMA request (VRAM fill/copy) before execution. */
    uint8_t special_dma_diag_valid, special_dma_diag_mode, special_dma_diag_target, special_dma_diag_code;
    uint16_t special_dma_diag_words_reg, special_dma_diag_dst;
    uint32_t special_dma_diag_cmd, special_dma_diag_src_word;
    uint8_t special_dma_regs[5]; /* regs 19..23 */
    /* Phase 54: VDP DMA fill is armed by the command and consumes the next data-port word. */
    uint8_t dma_fill_pending;
    uint16_t dma_fill_words, dma_fill_dst;
    /* Phase 55 diagnostics for fill arm/trigger/completion. */
    uint32_t dma_fill_armed_count, dma_fill_trigger_count;
    uint16_t dma_fill_trigger_value, dma_fill_trigger_addr;
    uint8_t dma_fill_before[8], dma_fill_after[8];
    /* PSC-style presentation shadows. Appended so all pre-P87 virtual-VDP
       field offsets stay stable in already-generated AOT objects/probes. */
    uint16_t cram_gba[0x40];
    /* Whole-plane scroll is translated at VDP-write time, mirroring PSC's
       GBA-facing video shadow.  Source VDP state remains authoritative. */
    int16_t present_hscroll[2];
    int16_t present_vscroll[2];
    uint32_t present_scroll_epoch;
    uint8_t present_scroll_valid;
    uint8_t present_scroll_mode;
} Md2GbaVdp;

struct Md2GbaBus;
typedef uint16_t (*Md2GbaPadPollFn)(void *user, unsigned port);
typedef unsigned (*Md2GbaZ80SyncFn)(void *user, struct Md2GbaBus *bus, unsigned budget);

typedef struct Md2GbaBus {
    const uint8_t *rom;
    /* Optional PSC-style descending-address ROM view generated offline.
       Byte streams and provenance still use ``rom``; aligned wide reads may
       use this view to obtain big-endian Genesis values with one native
       little-endian ARM load and no byte-swap. */
    const uint8_t *rom_reversed;
    size_t rom_size;
    uint8_t wram[0x10000];
    uint8_t z80_ram[0x2000];
    /* Cartridge SRAM is discovered from the standard Genesis ROM header.
       The fixed backing store is a bring-up choice; a later GBA persistence
       backend can map this logical save memory to SRAM/Flash hardware. */
    uint8_t sram[0x10000];
    uint32_t sram_start, sram_end;
    uint32_t sram_size;
    uint8_t sram_present, sram_control, sram_lane_stride;
    uint8_t pad1_data, pad2_data;
    uint16_t pad_held[2];
    Md2GbaPadPollFn pad_poll;
    void *pad_poll_user;
    uint8_t pad1_control, pad2_control, expansion_control;
    uint8_t hw_version;
    uint8_t tmss_unlocked;
    uint8_t vblank_active, hblank_active;
    uint16_t z80_bus_request, z80_reset;
    /* Minimal generic YM2612 register/status model. Status reads currently
       report not-busy; register/data writes are latched for later audio backends. */
    uint8_t ym2612_addr[2], ym2612_data[2];
    uint32_t ym2612_writes;
    Md2GbaAudio audio;
    Md2GbaVdp vdp;
    uint32_t unsupported_reads, unsupported_writes;
    /* P90: 68K release of the shared Z80 bus is a real cross-CPU timing
       boundary.  Native/AOT 68K blocks may otherwise release and reacquire
       within one host block, starving the Z80 completely.  The frontend can
       install a tiny synchronization slice; null preserves standalone tests. */
    Md2GbaZ80SyncFn z80_release_sync;
    void *z80_release_user;
} Md2GbaBus;

void md2gba_bus_init(Md2GbaBus *bus, const uint8_t *rom, size_t rom_size);
void md2gba_bus_set_reversed_rom(Md2GbaBus *bus, const uint8_t *reversed_rom);
void md2gba_bus_set_vblank(Md2GbaBus *bus, int active);
void md2gba_bus_set_audio_sink(Md2GbaBus *bus, const Md2GbaAudioSink *sink);
void md2gba_bus_set_z80_release_sync(Md2GbaBus *bus, Md2GbaZ80SyncFn fn, void *user);
void md2gba_bus_set_pad(Md2GbaBus *bus, unsigned port, uint16_t held);
void md2gba_bus_set_pad_poll(Md2GbaBus *bus, Md2GbaPadPollFn fn, void *user);
uint16_t md2gba_bus_get_pad(const Md2GbaBus *bus, unsigned port);
void md2gba_vdp_clear_dirty(Md2GbaVdp *vdp);
int md2gba_vdp_vram_range_dirty(const Md2GbaVdp *vdp,uint16_t start,unsigned length);
uint16_t md2gba_vdp_status(const Md2GbaBus *bus);
uint8_t  md2gba_read8(Md2GbaBus *bus, uint32_t address);
void md2gba_write8(Md2GbaBus *bus, uint32_t address, uint8_t value);

/*
 * Phase 10b: keep the uncommon hardware/overlay cases out of the hot path.
 * The slow functions are the canonical bus implementation. The public wide
 * helpers are tiny always-inline front ends so translated code can read/write
 * ordinary 68K WRAM without paying another Thumb function call and full bus
 * decode on every word/long access.
 */
uint16_t md2gba_read16_slow(Md2GbaBus *bus, uint32_t address);
uint32_t md2gba_read32_slow(Md2GbaBus *bus, uint32_t address);
void md2gba_write16_slow(Md2GbaBus *bus, uint32_t address, uint16_t value);
void md2gba_write32_slow(Md2GbaBus *bus, uint32_t address, uint32_t value);
void md2gba_vdp_data16_direct(Md2GbaBus *bus,uint16_t value);
void md2gba_vdp_data16_direct_vdp(Md2GbaVdp *vdp,uint16_t value);
/* P108: scheduler-admitted repeated writes to the VDP data port.  Returns zero
   without side effects unless the current VDP state is a simple sequential
   VRAM stream that can be batched exactly. */
int md2gba_vdp_data16_repeat_fast(Md2GbaBus *bus,unsigned count,uint16_t value);
/* P122: scheduler-batched sequential 32-bit VDP data stream.  The caller
   may use the readiness probe before consuming guest timing; the write helper
   then publishes the exact ordered longword stream without per-word bus decode. */
int md2gba_vdp_data32_stream_fast_ready(const Md2GbaBus *bus,unsigned count);
int md2gba_vdp_data32_stream_fast(Md2GbaBus *bus,const uint32_t *values,unsigned count);
/* P123: stricter aligned readiness + no-recheck publisher for generated streams
   whose longwords are already byte-swapped for native little-endian VRAM. */
int md2gba_vdp_data32_stream_aligned_ready(const Md2GbaBus *bus,unsigned count);
void md2gba_vdp_data32_stream_preswapped_unchecked(Md2GbaBus *bus,const uint32_t *values,unsigned count);
void md2gba_vdp_control16_direct(Md2GbaBus *bus,uint16_t value);
/* P86: one-shot fast paths used by deadline-admitted AOT copy/fill loops.
   They return zero without side effects unless the complete batch can be
   served by ordinary ROM/WRAM storage; hardware/SRAM cases stay canonical. */
int md2gba_bus_copy_stream_fast(Md2GbaBus *bus,uint32_t src,uint32_t dst,unsigned count,unsigned width,unsigned src_inc,unsigned dst_inc,uint32_t *last_value);
int md2gba_bus_fill_stream_fast(Md2GbaBus *bus,uint32_t dst,unsigned count,unsigned width,unsigned dst_inc,uint32_t value);
int md2gba_bus_compare_stream_fast(Md2GbaBus *bus,uint32_t src,uint32_t dst,unsigned count,unsigned width,unsigned src_inc,unsigned dst_inc,uint32_t *src_value,uint32_t *dst_value,unsigned *done);

#if defined(__GNUC__) || defined(__clang__)
#define MD2GBA_INLINE static __attribute__((always_inline)) inline
#else
#define MD2GBA_INLINE static inline
#endif

MD2GBA_INLINE uint16_t md2gba_bswap16(uint16_t x) { return (uint16_t)((x>>8)|(x<<8)); }
MD2GBA_INLINE uint32_t md2gba_bswap32(uint32_t x) {
    return ((x&0x000000FFu)<<24)|((x&0x0000FF00u)<<8)|((x&0x00FF0000u)>>8)|((x&0xFF000000u)>>24);
}

MD2GBA_INLINE int md2gba_wram_unshadowed(const Md2GbaBus *bus, uint32_t a, uint32_t end) {
    (void)bus; (void)a; (void)end; return 1;
}

MD2GBA_INLINE int md2gba_cart_unshadowed(const Md2GbaBus *bus, uint32_t a, uint32_t end) {
    if(!bus->sram_present || !(bus->sram_control & 1u)) return 1;
    return end < bus->sram_start || a > bus->sram_end;
}

/* P87 PSC-derived storage option.  The native PSII/PSIII compatibility
   core stores $FFxxxx work RAM in descending guest-address order.  A guest
   byte at offset o lives at host byte 0xFFFF-o; aligned guest words/longs
   can therefore use one native little-endian halfword/word load with no
   byte swap.  The source-visible 68K address space is unchanged. */
MD2GBA_INLINE unsigned md2gba_wram_byte_index(unsigned o) {
#if defined(MD2GBA_PSC_REVERSED_WRAM)
    return 0xFFFFu-(o&0xFFFFu);
#else
    return o&0xFFFFu;
#endif
}
MD2GBA_INLINE uint8_t md2gba_wram_read8_offset(const Md2GbaBus *bus,unsigned o) {
    return bus->wram[md2gba_wram_byte_index(o)];
}
MD2GBA_INLINE void md2gba_wram_write8_offset(Md2GbaBus *bus,unsigned o,uint8_t v) {
    bus->wram[md2gba_wram_byte_index(o)]=v;
}
MD2GBA_INLINE uint16_t md2gba_wram_read16_offset(const Md2GbaBus *bus,unsigned o) {
#if defined(MD2GBA_PSC_REVERSED_WRAM)
    return *(const uint16_t*)(const void*)(bus->wram+(0xFFFEu-(o&0xFFFFu)));
#else
    return md2gba_bswap16(*(const uint16_t*)(const void*)(bus->wram+(o&0xFFFFu)));
#endif
}
MD2GBA_INLINE void md2gba_wram_write16_offset(Md2GbaBus *bus,unsigned o,uint16_t v) {
#if defined(MD2GBA_PSC_REVERSED_WRAM)
    *(uint16_t*)(void*)(bus->wram+(0xFFFEu-(o&0xFFFFu)))=v;
#else
    *(uint16_t*)(void*)(bus->wram+(o&0xFFFFu))=md2gba_bswap16(v);
#endif
}
MD2GBA_INLINE uint32_t md2gba_wram_read32_offset(const Md2GbaBus *bus,unsigned o) {
#if defined(MD2GBA_PSC_REVERSED_WRAM)
    if(((o&3u)==0u)) return *(const uint32_t*)(const void*)(bus->wram+(0xFFFCu-(o&0xFFFFu)));
    { uint16_t hi=*(const uint16_t*)(const void*)(bus->wram+(0xFFFEu-(o&0xFFFFu)));
      uint16_t lo=*(const uint16_t*)(const void*)(bus->wram+(0xFFFCu-(o&0xFFFFu)));
      return ((uint32_t)hi<<16)|lo; }
#else
    if(((o&3u)==0u)) return md2gba_bswap32(*(const uint32_t*)(const void*)(bus->wram+(o&0xFFFFu)));
    { uint16_t hi=md2gba_bswap16(*(const uint16_t*)(const void*)(bus->wram+(o&0xFFFFu)));
      uint16_t lo=md2gba_bswap16(*(const uint16_t*)(const void*)(bus->wram+(o&0xFFFFu)+2u));
      return ((uint32_t)hi<<16)|lo; }
#endif
}
MD2GBA_INLINE void md2gba_wram_write32_offset(Md2GbaBus *bus,unsigned o,uint32_t v) {
#if defined(MD2GBA_PSC_REVERSED_WRAM)
    if((o&3u)==0u){*(uint32_t*)(void*)(bus->wram+(0xFFFCu-(o&0xFFFFu)))=v;return;}
    *(uint16_t*)(void*)(bus->wram+(0xFFFEu-(o&0xFFFFu)))=(uint16_t)(v>>16);
    *(uint16_t*)(void*)(bus->wram+(0xFFFCu-(o&0xFFFFu)))=(uint16_t)v;
#else
    if((o&3u)==0u){*(uint32_t*)(void*)(bus->wram+(o&0xFFFFu))=md2gba_bswap32(v);return;}
    *(uint16_t*)(void*)(bus->wram+(o&0xFFFFu))=md2gba_bswap16((uint16_t)(v>>16));
    *(uint16_t*)(void*)(bus->wram+(o&0xFFFFu)+2u)=md2gba_bswap16((uint16_t)v);
#endif
}

/* P74: translated code performs a large number of dynamic ROM table reads.
   Keep ordinary cartridge and WRAM accesses entirely inside the generated
   translation unit instead of paying the generic bus decoder call. */
MD2GBA_INLINE uint8_t md2gba_read8_fast(Md2GbaBus *bus, uint32_t address) {
    uint32_t a=address & 0x00FFFFFFu;
    if(a>=0xFF0000u && md2gba_wram_unshadowed(bus,a,a)) return md2gba_wram_read8_offset(bus,a&0xFFFFu);
    if(a<bus->rom_size && md2gba_cart_unshadowed(bus,a,a)) return bus->rom[a];
    return md2gba_read8(bus,a);
}
MD2GBA_INLINE void md2gba_write8_fast(Md2GbaBus *bus, uint32_t address, uint8_t value) {
    uint32_t a=address & 0x00FFFFFFu;
    if(a>=0xFF0000u && md2gba_wram_unshadowed(bus,a,a)) { md2gba_wram_write8_offset(bus,a&0xFFFFu,value); return; }
    md2gba_write8(bus,a,value);
}

MD2GBA_INLINE uint16_t md2gba_read16(Md2GbaBus *bus, uint32_t address) {
    uint32_t a=address & 0x00FFFFFFu;
    if(a==0xC00000u)return md2gba_read16_slow(bus,a);
    if(a==0xC00004u)return (uint16_t)((bus->vblank_active?0x0008u:0u)|(bus->hblank_active?0x0004u:0u)|(bus->vdp.dma_busy?0x0002u:0u));
    if(!(a&1u) && a>=0xFF0000u && a<=0xFFFFFEu) {
        uint32_t o=a&0xFFFFu; return md2gba_wram_read16_offset(bus,o);
    }
    if(!(a&1u) && a+1u<bus->rom_size && md2gba_cart_unshadowed(bus,a,a+1u)) {
#if defined(MD2GBA_PSC_REVERSED_ROM)
        const uint8_t *p=bus->rom_reversed+(bus->rom_size-2u-a);
        return *(const uint16_t*)(const void*)p;
#else
        if(bus->rom_reversed) {
            const uint8_t *p=bus->rom_reversed+(bus->rom_size-2u-a);
            return *(const uint16_t*)(const void*)p;
        }
        return md2gba_bswap16(*(const uint16_t*)(const void*)(bus->rom+a));
#endif
    }
    return md2gba_read16_slow(bus,a);
}
MD2GBA_INLINE uint32_t md2gba_read32(Md2GbaBus *bus, uint32_t address) {
    uint32_t a=address & 0x00FFFFFFu;
    if((a&3u)==0u && a>=0xFF0000u && a<=0xFFFFFCu)
        return md2gba_wram_read32_offset(bus,a&0xFFFFu);
    if(!(a&1u) && a>=0xFF0000u && a<=0xFFFFFCu) {
        return md2gba_wram_read32_offset(bus,a&0xFFFFu);
    }
    if((a&3u)==0u && a+3u<bus->rom_size && md2gba_cart_unshadowed(bus,a,a+3u)) {
#if defined(MD2GBA_PSC_REVERSED_ROM)
        const uint8_t *p=bus->rom_reversed+(bus->rom_size-4u-a);
        return *(const uint32_t*)(const void*)p;
#else
        if(bus->rom_reversed) {
            const uint8_t *p=bus->rom_reversed+(bus->rom_size-4u-a);
            return *(const uint32_t*)(const void*)p;
        }
        return md2gba_bswap32(*(const uint32_t*)(const void*)(bus->rom+a));
#endif
    }
    if(!(a&1u) && a+3u<bus->rom_size && md2gba_cart_unshadowed(bus,a,a+3u)) {
#if defined(MD2GBA_PSC_REVERSED_ROM)
        const uint8_t *p=bus->rom_reversed+(bus->rom_size-2u-a);
        uint16_t hi=*(const uint16_t*)(const void*)p;
        uint16_t lo=*(const uint16_t*)(const void*)(p-2u);
        return ((uint32_t)hi<<16)|lo;
#else
        if(bus->rom_reversed) {
            const uint8_t *p=bus->rom_reversed+(bus->rom_size-2u-a);
            uint16_t hi=*(const uint16_t*)(const void*)p;
            uint16_t lo=*(const uint16_t*)(const void*)(p-2u);
            return ((uint32_t)hi<<16)|lo;
        }
        uint16_t hi=md2gba_bswap16(*(const uint16_t*)(const void*)(bus->rom+a));
        uint16_t lo=md2gba_bswap16(*(const uint16_t*)(const void*)(bus->rom+a+2u));
        return ((uint32_t)hi<<16)|lo;
#endif
    }
    return md2gba_read32_slow(bus,a);
}
MD2GBA_INLINE void md2gba_write16(Md2GbaBus *bus, uint32_t address, uint16_t value) {
    uint32_t a=address & 0x00FFFFFFu;
    if(a==0xC00000u){md2gba_vdp_data16_direct_vdp(&bus->vdp,value);return;}
    if(a==0xC00004u){md2gba_vdp_control16_direct(bus,value);return;}
    if(!(a&1u) && a>=0xFF0000u && a<=0xFFFFFEu) {
        md2gba_wram_write16_offset(bus,a&0xFFFFu,value); return;
    }
    md2gba_write16_slow(bus,a,value);
}
MD2GBA_INLINE void md2gba_write32(Md2GbaBus *bus, uint32_t address, uint32_t value) {
    uint32_t a=address & 0x00FFFFFFu;
    if(a==0xC00000u){md2gba_vdp_data16_direct_vdp(&bus->vdp,(uint16_t)(value>>16));md2gba_vdp_data16_direct_vdp(&bus->vdp,(uint16_t)value);return;}
    if(a==0xC00004u){md2gba_vdp_control16_direct(bus,(uint16_t)(value>>16));md2gba_vdp_control16_direct(bus,(uint16_t)value);return;}
    if((a&3u)==0u && a>=0xFF0000u && a<=0xFFFFFCu) {
        md2gba_wram_write32_offset(bus,a&0xFFFFu,value); return;
    }
    if(!(a&1u) && a>=0xFF0000u && a<=0xFFFFFCu) {
        md2gba_wram_write32_offset(bus,a&0xFFFFu,value); return;
    }
    md2gba_write32_slow(bus,a,value);
}

#undef MD2GBA_INLINE

/* Generated AOT pages overwhelmingly access ordinary cartridge ROM and 68K
   work RAM. Route byte traffic through the same proven fast front end already
   used by the generic EA helpers. Hardware/SRAM/Z80 addresses still fall back
   to the canonical decoder, so this changes cost rather than semantics. */
#if defined(MD2GBA_AOT_GENERATED_FAST)
#define md2gba_read8(bus,address) md2gba_read8_fast((bus),(address))
#define md2gba_write8(bus,address,value) md2gba_write8_fast((bus),(address),(value))
#endif
#endif
