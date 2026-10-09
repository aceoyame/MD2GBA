#include <stdint.h>
#include <stddef.h>

/* Native PSC/Maxmod audio owns the sound command clock.  Generic Z80
   execution owns a different sound-driver state.  They are individually
   supported, but composing both has no defined owner and previously could
   compile far enough to reference branch-local state. */
#if defined(MD2GBA_PSC_M4A_AUDIO) && defined(MD2GBA_Z80_EXECUTION)
#error "MD2GBA_PSC_M4A_AUDIO and MD2GBA_Z80_EXECUTION are mutually exclusive"
#endif
#include "md2gba_bus.h"
#include "md2gba_aot68k.h"
#include "md2gba_aot_region.h"
#include "md2gba_interp68k.h"
#include "md2gba_timing.h"
#include "md2gba_gba_backend.h"
#include "md2gba_projected_retry.h"
#include "md2gba_gba_soft.h"
#include "md2gba_vdp_soft.h"
#if defined(MD2GBA_PSC_M4A_AUDIO)
#include "md2gba_psc_m4a.h"
#include "md2gba/ps4_m4a_bridge.h"
#else
#include "md2gba_mix_pcm.h"
#include "md2gba_gba_pcm_stream.h"
#include "md2gba_z80.h"
#endif
#ifndef MD2GBA_GUEST_AOT_HEADER
#define MD2GBA_GUEST_AOT_HEADER "ps4_whole_region.h"
#endif
#include MD2GBA_GUEST_AOT_HEADER
#ifndef MD2GBA_GUEST_AOT_RUN
#define MD2GBA_GUEST_AOT_RUN ps4_whole_region_run
#endif

#define REG_DISPCNT (*(volatile uint16_t*)0x04000000u)
#define REG_VCOUNT  (*(volatile uint16_t*)0x04000006u)
#define REG_KEYINPUT (*(volatile uint16_t*)0x04000130u)
#define REG_WAITCNT (*(volatile uint16_t*)0x04000204u)
#define MODE3_FB ((volatile uint16_t*)0x06000000u)

#define SOFT_VIEW_Y 32u
#define AH_SB0 16u
#define BH_SB0 18u
#define AL_SB0 20u
#define BL_SB0 22u
#define AH_SB1 24u
#define BH_SB1 26u
#define AL_SB1 28u
#define BL_SB1 30u

/* Generic audio transport defaults.  Target profiles override rate/block/ring
   at compile time; PSC-native targets select the collection's observed
   samples-per-video-frame policy. */
#ifndef P76_AUDIO_RATE
#define P76_AUDIO_RATE 16384u
#endif
#ifndef P76_AUDIO_BLOCK_SAMPLES
#define P76_AUDIO_BLOCK_SAMPLES 128u
#endif
#ifndef P76_AUDIO_RING_BLOCKS
#define P76_AUDIO_RING_BLOCKS 8u
#endif
#define P76_AUDIO_RING_SAMPLES (P76_AUDIO_BLOCK_SAMPLES*P76_AUDIO_RING_BLOCKS+MD2GBA_GBA_PCM_FIFO_BURST_SAMPLES)
#ifndef MD2GBA_PRESENT_FIT
#define MD2GBA_PRESENT_FIT 0
#endif
#ifndef MD2GBA_PROJECTED_FIT
#define MD2GBA_PROJECTED_FIT 0
#endif

extern const uint8_t ps4_rom_start[], ps4_rom_end[];
#if defined(MD2GBA_PSC_REVERSED_ROM)
extern const uint8_t ps4_rom_reversed_start[], ps4_rom_reversed_end[];
#endif

static Md2GbaBus bus;
#if defined(MD2GBA_GBA_IWRAM)
#define MD2GBA_IWRAM_STATE __attribute__((section(".iwram.state"),aligned(4)))
#else
#define MD2GBA_IWRAM_STATE
#endif
/* P72: the translated core touches the 68K register file on virtually every
   instruction.  Keep the tiny CPU/scheduler state in zero-wait IWRAM rather
   than paying EWRAM waitstates for every D/A/SR/PC access. */
static Md2Gba68k cpu MD2GBA_IWRAM_STATE;
static Md2GbaInterpStats ist;
static Md2GbaAotHooks hooks MD2GBA_IWRAM_STATE;
static Md2GbaTiming timing MD2GBA_IWRAM_STATE;
static Md2GbaGbaFrameState hw_state;
static uint16_t scroll_stream[160][8];
static uint16_t pad_latched_hold;
static uint32_t pad_latched_until_vblank;
#if defined(MD2GBA_COMPILED_VDP)
static Md2GbaCompiledVdpState compiled_vdp_state;
#endif
#if defined(MD2GBA_PSC_M4A_AUDIO)
static Md2GbaPscM4a audio_stream;
#if defined(MD2GBA_MAXMOD_GBA_AUDIO)
static int8_t audio_ring[MD2GBA_PSC_M4A_PCM_BUFFER_SIZE] __attribute__((section(".iwram.audio"),aligned(4)));
#else
static int8_t audio_ring[MD2GBA_PSC_M4A_PCM_BUFFER_SIZE] __attribute__((aligned(4)));
#endif
static uint32_t psc_m4a_music_commands;
static uint32_t psc_m4a_sfx_commands;
static void ps4_m4a_route_command(void){
    uint8_t cmd=md2gba_wram_read8_offset(&bus,0x500Au);
    if(!cmd)return;
    md2gba_wram_write8_offset(&bus,0x500Au,0u);
    if(cmd>=0x81u && cmd<=0xB4u){
        if(md2gba_ps4_m4a_play(cmd))psc_m4a_music_commands++;
        return;
    }
    if(cmd>=0xB5u && cmd<=0xFAu){
        if(md2gba_ps4_m4a_sfx_play(cmd))psc_m4a_sfx_commands++;
        return;
    }
    /* Original PSIV sound command classes, implemented as separate MP2K-like
       native players rather than by running the Genesis Z80 driver. */
    if(cmd==0xFBu){md2gba_ps4_m4a_fade();return;}
    if(cmd==0xFCu){md2gba_ps4_m4a_sfx_stop_normal();return;}
    if(cmd==0xFDu){md2gba_ps4_m4a_sfx_stop_special();return;}
    if(cmd==0xFEu){md2gba_ps4_m4a_stop();md2gba_ps4_m4a_sfx_stop_all();return;}
}
static void service_audio(void){
#if defined(MD2GBA_MAXMOD_GBA_AUDIO)
    /* Maxmod's GBA API performs one foreground mmFrame() per video frame.
       Avoid hundreds of scanline-level WRAM command polls when no audio frame
       is due; commands are consumed at the next physical-frame audio service. */
    if(!audio_stream.pending_frame) return;
#endif
    ps4_m4a_route_command();
    (void)md2gba_psc_m4a_service(&audio_stream,md2gba_ps4_m4a_mix_mono,0);
}
static void projected_foreground_checkpoint(void *user){(void)user;service_audio();}
#else
static Md2GbaMixPcm audio_mix;
static Md2GbaZ80 z80_cpu MD2GBA_IWRAM_STATE;
static uint16_t z80_inline_credit MD2GBA_IWRAM_STATE;
static uint8_t z80_inline_active MD2GBA_IWRAM_STATE;
static Md2GbaGbaPcmStream audio_stream;
static int8_t audio_ring[P76_AUDIO_RING_SAMPLES] __attribute__((aligned(4)));
static void pcm_fill(void *user,int8_t *dst,size_t samples){ md2gba_mix_pcm_render((Md2GbaMixPcm*)user,dst,samples); }
static void service_audio(void){ (void)md2gba_gba_pcm_stream_service(&audio_stream,pcm_fill,&audio_mix); }
#endif
#if defined(MD2GBA_Z80_EXECUTION)
static __attribute__((section(".iwram"),noinline)) unsigned live_z80_release_sync(void *user,Md2GbaBus*b,unsigned budget){
    unsigned used; (void)user;
    if(z80_inline_active)return 0u;
    z80_inline_active=1u;
    used=md2gba_z80_run(&z80_cpu,b,budget);
    z80_inline_active=0u;
    { unsigned q=(unsigned)z80_inline_credit+used; z80_inline_credit=(uint16_t)(q>0xffffu?0xffffu:q); }
    return used;
}
#endif
static __attribute__((section(".iwram"),noinline)) int live_scheduled_event(Md2Gba68k*c,Md2GbaBus*b,uint32_t steps,void*u){
    Md2GbaTiming*t=(Md2GbaTiming*)u;
    uint32_t old_vblanks=t?t->vblanks:0u;
    int r;
    service_audio();
    r=md2gba_timing_scheduled_event(c,b,steps,u);
#if defined(MD2GBA_Z80_EXECUTION)
    /* NTSC Genesis Z80: ~3.58 MHz / 15.734 kHz ~= 228 T-states per line.
       Bus-request/reset are enforced inside the generic executor. */
    if(t && t->vblanks!=old_vblanks)md2gba_z80_raise_irq(&z80_cpu);
    {
        unsigned budget=228u;
        if(z80_inline_credit>=budget)z80_inline_credit=(uint16_t)(z80_inline_credit-budget);
        else { budget-=(unsigned)z80_inline_credit; z80_inline_credit=0u; (void)md2gba_z80_run(&z80_cpu,b,budget); }
    }
    service_audio();
#endif
    return r;
}

static void wait_vblank(void) {
    /* P79: if composition finishes inside the current VBlank, the Mode-4 page
       bit can be switched immediately. P78 first waited for VBlank to end and
       then for the *next* VBlank, throwing away almost a full video frame. */
    if (REG_VCOUNT < 160u) while (REG_VCOUNT < 160u) {}
}

static void fatal_screen(uint16_t color) {
#if defined(MD2GBA_PSC_M4A_AUDIO)
    md2gba_psc_m4a_stop(&audio_stream); md2gba_ps4_m4a_stop();
#else
    #if defined(MD2GBA_GBA_LEGACY_PSG)
    md2gba_mix_pcm_gba_legacy_stop(&audio_mix);
    #endif
    md2gba_gba_pcm_stream_stop(&audio_stream);
#endif
    md2gba_gba_stop_hblank_scroll_dma();
    REG_DISPCNT = 0x0080u;
    for (unsigned i = 0; i < 240u * 160u; ++i) MODE3_FB[i] = color;
    REG_DISPCNT = 0x0403u;
    for (;;) {}
}

extern void md2gba_gba_vblank_irq_install(void);
extern uint16_t md2gba_gba_take_key_press_latch(void);

static void reset_hw_state(void) {
    uint8_t *p = (uint8_t *)&hw_state;
    for (unsigned i = 0; i < sizeof(hw_state); ++i) p[i] = 0u;
}

static uint16_t poll_live_pad(void *user,unsigned port) {
    uint16_t k=REG_KEYINPUT;
    uint16_t latched=md2gba_gba_take_key_press_latch();
    (void)user;
    if(port!=0u)return 0u;
    /* P78: sample at the actual Genesis A10003 read, not only before a long
       translated run.  Keep IRQ-captured taps alive for two virtual VBlanks so
       both TH phases of the real 3-button protocol observe the same press. */
    if(latched){pad_latched_hold|=latched;pad_latched_until_vblank=timing.vblanks+2u;}
    if(pad_latched_hold && (int32_t)(timing.vblanks-pad_latched_until_vblank)>=0)pad_latched_hold=0u;
    k=(uint16_t)(k&~pad_latched_hold); /* KEYINPUT is active-low. */
    return md2gba_gba_pad_from_keyinput(k);
}
static void sample_live_pad(void) { md2gba_bus_set_pad(&bus,0u,poll_live_pad(0,0u)); }

static unsigned live_view_x(void){
    unsigned px=md2gba_vdp_visible_columns(&bus.vdp)*8u;
    return px>240u?(px-240u)/2u:0u;
}

void md2gba_ps4_whole_live_main(void) {
    size_t n = (size_t)(ps4_rom_end - ps4_rom_start);
    int rc = 0;
#if !defined(MD2GBA_PSC_M4A_AUDIO) && !defined(MD2GBA_GENERIC_SILENT)
    Md2GbaAudioSink audio_sink;
#endif
    unsigned started = 0u;
    uint32_t guest_reset_pc = 0u;
    unsigned renderer = 0u; /* 1 = generic hardware, 3 = projected, 4 = offline-compiled direct VDP */
    Md2GbaProjectedRetry projected_retry={0};
    unsigned map_bank = 0u; /* coherent text-map double buffer: 16..23 / 24..31.
                                  Screenblocks 0..15 overlap BG char data and MUST
                                  never be used for maps while charblock 0 spans
                                  Genesis tile slots 0..1023. */

    /* Fast sequential Game Pak fetch + prefetch. The full AOT graph executes from ROM,
       so leaving BIOS-default waitstates wastes most ARM7 bandwidth. */
    REG_WAITCNT = 0x4317u;

    md2gba_bus_init(&bus, ps4_rom_start, n);
#if defined(MD2GBA_PSC_REVERSED_ROM)
    if((size_t)(ps4_rom_reversed_end-ps4_rom_reversed_start)==n)
        md2gba_bus_set_reversed_rom(&bus,ps4_rom_reversed_start);
#endif
    md2gba_bus_set_pad_poll(&bus,poll_live_pad,0);
#if !defined(MD2GBA_GENERIC_SILENT)
#if defined(MD2GBA_PSC_M4A_AUDIO)
    md2gba_ps4_m4a_reset();
    md2gba_psc_m4a_init(&audio_stream,audio_ring);
    psc_m4a_music_commands=0u; psc_m4a_sfx_commands=0u;
#else
    md2gba_mix_pcm_init(&audio_mix,MD2GBA_PSG_NTSC_CLOCK,MD2GBA_YM2612_NTSC_CLOCK,P76_AUDIO_RATE);
    #if defined(MD2GBA_Z80_EXECUTION)
    md2gba_z80_init(&z80_cpu);
    z80_inline_credit=0u; z80_inline_active=0u;
    md2gba_bus_set_z80_release_sync(&bus,live_z80_release_sync,0);
    #endif
    audio_sink=md2gba_mix_pcm_sink(&audio_mix);
    md2gba_bus_set_audio_sink(&bus,&audio_sink);
    md2gba_gba_pcm_stream_init_conveyor(&audio_stream,audio_ring,
                                        P76_AUDIO_BLOCK_SAMPLES,P76_AUDIO_RING_BLOCKS,
                                        P76_AUDIO_RATE);
    md2gba_gba_pcm_stream_prefill_silence(&audio_stream);
#endif
#endif
    md68k_reset_context(&cpu);
    cpu.a[7] = md2gba_read32(&bus, 0u) & 0xffffffu;
    guest_reset_pc = md2gba_read32(&bus, 4u) & 0xffffffu;
    ist.steps = ist.unsupported = ist.last_pc = ist.last_opcode = 0u;
    reset_hw_state();

    /* NTSC Mega Drive 68000 timing: ~7.67 MHz / 15.734 kHz = 487.5 core
       cycles per scanline.  P69 schedules scanlines in generated 68K cycle
       units instead of the old 50,000-instruction/frame approximation. */
    md2gba_timing_init(&timing, 0u, 1);
    md2gba_timing_enable_cycles(&timing, 262u, 224u, 488u);

    hooks.handle_pc = 0;
    hooks.user = 0;
    hooks.fallback_pc = md68k_interp_fallback_hook;
    hooks.fallback_user = &ist;
    md2gba_timing_enable_scheduled(&timing);
    hooks.event_step = 0;
    hooks.event_user = 0;
    hooks.after_step = 0;
    hooks.after_user = 0;
    hooks.scheduled_event = live_scheduled_event;
    hooks.scheduled_user = &timing;
    hooks.scheduled_countdown = &timing.scheduled_countdown;
    hooks.scheduled_overshoot = &timing.scheduled_overshoot;
#if defined(MD2GBA_DISABLE_IDLE_BATCH) || defined(MD2GBA_Z80_EXECUTION)
    hooks.scheduled_idle_hint = 0;
    hooks.scheduled_idle_descriptor = 0;
    hooks.scheduled_idle_steps = 0;
#else
    hooks.scheduled_idle_hint = &timing.idle_poll_hint;
    hooks.scheduled_idle_descriptor = &timing.idle_poll_pc;
    hooks.scheduled_idle_steps = &timing.idle_steps_advanced;
#endif

    REG_DISPCNT = 0u;
    md2gba_gba_vblank_irq_install();
#if !defined(MD2GBA_GENERIC_SILENT)
#if defined(MD2GBA_PSC_M4A_AUDIO)
    md2gba_psc_m4a_start(&audio_stream);
    md2gba_gba_projected_set_checkpoint(projected_foreground_checkpoint,0);
#else
    #if defined(MD2GBA_AUDIO_DMA_CONVEYOR)
    md2gba_gba_pcm_stream_start(&audio_stream);
    #else
    md2gba_gba_pcm_stream_start_irq_pumped(&audio_stream);
    #endif
    #if defined(MD2GBA_GBA_LEGACY_PSG)
    md2gba_mix_pcm_gba_legacy_start(&audio_mix);
    #endif
#endif
#endif

    for (;;) {
#if !defined(MD2GBA_GENERIC_SILENT)
        service_audio();
#endif
        sample_live_pad();
        rc = MD2GBA_GUEST_AOT_RUN(&cpu, &bus, &hooks,
                                  started ? MD2GBA_AOT_RESUME_PC : guest_reset_pc,
                                  0x00ffffffu, 65535u);
        started = 1u;
#if !defined(MD2GBA_GENERIC_SILENT)
        service_audio();
#endif

        if (rc == MD2GBA_AOT_YIELD) {
            uint32_t complex;
            /* Asset setup often runs with the Genesis display deliberately off.
               Let translated code/DMA finish without wasting an expensive GBA
               projection; accumulated dirty state is rendered when display returns. */
            if(!(bus.vdp.regs[1]&0x40u)) continue;
            complex = md2gba_vdp_complex_features(&bus.vdp);
            unsigned view_x=live_view_x();
            unsigned suggested_y=hw_state.initialized?hw_state.last_view_y:SOFT_VIEW_Y;

#if defined(MD2GBA_COMPILED_VDP)
            /* P150: when the desktop profile proves a direct identity tile
               mapping safe, use the generated/native publisher before either
               projected-fit or the cache-building generic renderer. Runtime
               guards remain authoritative so an unseen VDP mode falls back. */
            if(md2gba_gba_compiled_vdp_eligible(&bus.vdp)){
                int cr;
                if(renderer!=4u){
                    md2gba_gba_soft_cancel_page_flip();
                    md2gba_gba_projected_reset();
                    md2gba_gba_hw_cancel_pending();
                    reset_hw_state(); map_bank=0u;
                    md2gba_gba_compiled_vdp_reset(&compiled_vdp_state);
                    renderer=4u;
                }
                cr=md2gba_gba_compiled_vdp_present(&bus.vdp,&compiled_vdp_state,scroll_stream,view_x,SOFT_VIEW_Y);
                if(cr==0)continue;
                /* Guard refusal is not fatal. Preserve the guest VDP state and
                   let the canonical renderer own this scene. */
                md2gba_gba_compiled_vdp_reset(&compiled_vdp_state);
                renderer=0u;reset_hw_state();map_bank=0u;
            }else if(renderer==4u){
                md2gba_gba_compiled_vdp_reset(&compiled_vdp_state);
                renderer=0u;reset_hw_state();map_bank=0u;
            }
#endif

            /* P134 screen-fit: first offer simple VDP states to the hardware-only
               regular-tile projector. The guest camera/coordinates remain untouched;
               the target presenter owns only the 320x224 -> 240x160 transform.
               A capacity/geometry refusal falls back to the conservative P134
               1:1 hardware renderer, never to an affine/live compositor. */
            if(MD2GBA_PROJECTED_FIT && complex==0u &&
               md2gba_projected_retry_ready(&projected_retry,bus.vdp.regs,
                                            bus.vdp.vram_epoch,timing.vblanks)) {
                int pr;
                if(renderer!=3u){
                    md2gba_gba_soft_cancel_page_flip();
                    md2gba_gba_stop_hblank_scroll_dma();
                    md2gba_gba_hw_cancel_pending();
                    md2gba_gba_projected_reset();
                    reset_hw_state(); map_bank=0u; renderer=3u;
                }
                pr=md2gba_gba_projected_present(&bus.vdp);
                md2gba_projected_retry_result(&projected_retry,pr,bus.vdp.regs,
                                               bus.vdp.vram_epoch,timing.vblanks);
                if(pr==0)continue;
                md2gba_gba_projected_reset();renderer=0u;reset_hw_state();map_bank=0u;
            }

            /* P82/P84: ordinary framed bottom dialogue stays on the hardware BG
               renderer.  The backend derives a 30-tile presentation map from
               the source 34-tile Plane-A panel while keeping the original VDP
               state untouched.  Only genuinely complex VDP modes need Mode 4. */
            /* P91/PSC-native: wide framed UI stays on the hardware BG path.
               The backend compacts the source 34-tile frame to a 30-tile GBA
               window by preserving borders and resampling only the interior.
               Do not force the full-screen software compositor for dialogue. */
            {
                int hr;
                uint32_t plan;
                unsigned ah,bh,al,bl;
                unsigned view_y=suggested_y;
                if (renderer != 1u) {
                    md2gba_gba_soft_cancel_page_flip();
                    md2gba_gba_stop_hblank_scroll_dma();
                    md2gba_gba_hw_cancel_pending();
                    reset_hw_state();
                    map_bank = 0u;
                    renderer = 1u;
                }
                /* A previously staged small hardware update owns the shared BG
                   tile staging buffer until physical VBlank consumes it. Keep
                   running translated 68K code and accumulate new VDP dirtiness,
                   but never let that queue starve live whole-plane camera state.
                   This O(1) publish is deliberately cheaper than replanning the
                   whole renderer while the shared staging buffers are busy. */
                if(md2gba_gba_hw_commit_pending()) {
                    md2gba_gba_publish_live_whole_plane_scroll(
                        &bus.vdp, view_x, hw_state.initialized ? hw_state.last_view_y : SOFT_VIEW_Y);
                    continue;
                }
                plan=md2gba_gba_plan_frame(&bus.vdp,&hw_state);
                if(plan&(MD2GBA_FRAME_FULL|MD2GBA_FRAME_MAPS|MD2GBA_FRAME_MAP_PATCH|MD2GBA_FRAME_SCROLL))
                    view_y=md2gba_gba_suggest_view_y_cached(&bus.vdp,&hw_state,plan);
                if((plan&MD2GBA_FRAME_MAPS) && hw_state.initialized) map_bank^=1u;
                if(map_bank){ah=AH_SB1;bh=BH_SB1;al=AL_SB1;bl=BL_SB1;}
                else {ah=AH_SB0;bh=BH_SB0;al=AL_SB0;bl=BL_SB0;}
                /* P72 prepares inactive maps + BG tile staging first, then the
                   backend performs the live VRAM/palette/OAM swap at physical VBlank. */
                hr = md2gba_gba_commit_priority_incremental_viewport_planned(
                    &bus.vdp, &hw_state, ah, bh, al, bl,
                    scroll_stream, view_x, view_y, plan);
                if (hr != 0) {
                    /* Retain hardware-only presentation on unsupported VDP
                       states and cache pressure. Retry from a clean hardware
                       state on the next guest yield, preserving VDP dirtiness. */
                    md2gba_gba_hw_cancel_pending();
                    reset_hw_state();map_bank=0u;
                }
            }
            continue;
        }

        if (rc == MD2GBA_AOT_STEP_LIMIT) {
            continue;
        }

        fatal_screen(0x001fu);
    }
}
