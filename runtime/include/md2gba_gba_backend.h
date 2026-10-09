#ifndef MD2GBA_GBA_BACKEND_H
#define MD2GBA_GBA_BACKEND_H
#include "md2gba_bus.h"
#include <stdint.h>

/* GBA renderer seam. Genesis-facing translated code talks only to Md2GbaBus. */
uint16_t md2gba_gba_color_from_genesis(uint16_t md_color);
uint8_t  md2gba_gba_tile_byte_from_genesis(uint8_t md_packed_pixels);
uint16_t md2gba_gba_name_word_from_genesis(uint16_t md_name_word);
void md2gba_gba_commit_cram(const Md2GbaVdp *vdp);
void md2gba_gba_commit_vram_tiles(const Md2GbaVdp *vdp, uint16_t md_vram_addr,
                                  unsigned first_gba_tile, unsigned tile_count);
void md2gba_gba_commit_name_table(const Md2GbaVdp *vdp, uint16_t md_vram_addr,
                                  unsigned gba_screenblock, unsigned width, unsigned height);
void md2gba_gba_init_display(void);
void md2gba_gba_init_display_64x32(unsigned first_screenblock);
void md2gba_gba_commit_name_table_64x32(const Md2GbaVdp *vdp, uint16_t md_vram_addr,
                                        unsigned first_screenblock);
void md2gba_gba_set_bg0_scroll(uint16_t x, uint16_t y);

/* Generic two-plane Mode-0 projection for Genesis VDP states that fit the
   GBA text-BG tile-index range. Plane bases are derived from VDP registers. */
uint16_t md2gba_vdp_plane_a_base(const Md2GbaVdp *vdp);
uint16_t md2gba_vdp_plane_b_base(const Md2GbaVdp *vdp);
unsigned md2gba_vdp_max_tile_64x32(const Md2GbaVdp *vdp, uint16_t plane_base);
void md2gba_gba_init_display_dual_64x32(unsigned plane_a_screenblock,
                                        unsigned plane_b_screenblock);
void md2gba_gba_commit_vdp_planes_64x32(const Md2GbaVdp *vdp,
                                        unsigned plane_a_screenblock,
                                        unsigned plane_b_screenblock);
void md2gba_gba_set_bg_scroll(unsigned bg, uint16_t x, uint16_t y);
/* VDP-register-derived plane geometry. The current GBA text-BG projector
   supports 32/64-cell dimensions directly; 128-cell Genesis planes will use
   a later streaming window/cache path. */
unsigned md2gba_vdp_plane_width(const Md2GbaVdp *vdp);
unsigned md2gba_vdp_plane_height(const Md2GbaVdp *vdp);
unsigned md2gba_vdp_max_tile(const Md2GbaVdp *vdp, uint16_t plane_base,
                             unsigned width, unsigned height);
int md2gba_gba_init_display_dual_vdp(const Md2GbaVdp *vdp,
                                     unsigned plane_a_screenblock,
                                     unsigned plane_b_screenblock);
int md2gba_gba_commit_name_table_geometry(const Md2GbaVdp *vdp,
                                           uint16_t md_vram_addr,
                                           unsigned first_screenblock,
                                           unsigned width, unsigned height);
int md2gba_gba_commit_vdp_planes(const Md2GbaVdp *vdp,
                                 unsigned plane_a_screenblock,
                                 unsigned plane_b_screenblock);

/* Phase 6 live frontend/input helpers. GBA KEYINPUT is active-low. */
uint16_t md2gba_gba_pad_from_keyinput(uint16_t keyinput);
void md2gba_gba_sample_pad(Md2GbaBus *bus,unsigned port,uint16_t keyinput);

/* Generic 11-bit Genesis BG tile remapper. Slot 0 is reserved as a transparent
   blank, leaving 1023 simultaneously referenced source tiles per frame. */
typedef struct Md2GbaBgTileCache {
    uint16_t source_to_slot[2048];
    uint16_t slot_to_source[1024];
    /* P105: bitset of source tiles referenced by the current BG cache. */
    uint32_t source_mask[64];
    /* P134/PF08: real caches set this explicitly so the hot planner can trust
       source_mask without rescanning 64 words to infer whether it is valid. */
    uint32_t source_mask_valid;
    unsigned used_slots;
} Md2GbaBgTileCache;
int md2gba_gba_build_bg_tile_cache(const Md2GbaVdp *vdp,uint16_t plane_a_base,
                                    uint16_t plane_b_base,unsigned width,unsigned height,
                                    Md2GbaBgTileCache *cache);
int md2gba_gba_commit_cached_tiles(const Md2GbaVdp *vdp,const Md2GbaBgTileCache *cache);
int md2gba_gba_commit_name_table_cached(const Md2GbaVdp *vdp,uint16_t base,
                                         unsigned first_sb,unsigned width,unsigned height,
                                         const Md2GbaBgTileCache *cache,int priority_filter);
int md2gba_gba_commit_vdp_planes_cached(const Md2GbaVdp *vdp,unsigned a_sb,unsigned b_sb,
                                         Md2GbaBgTileCache *cache);

/* Genesis SAT -> GBA OBJ projection. Returns emitted 8x8 OBJ cells; dropped
   receives the number that could not fit the caller's cell budget/cache. */
unsigned md2gba_vdp_sat_base(const Md2GbaVdp *vdp);
unsigned md2gba_gba_commit_sprites(const Md2GbaVdp *vdp,unsigned max_cells,unsigned *dropped);
unsigned md2gba_vdp_sprite_piece_count(const Md2GbaVdp *vdp);
unsigned md2gba_gba_commit_sprites_packed(const Md2GbaVdp *vdp,unsigned max_objs,unsigned *dropped);

/* Hardware-visible Genesis scrolling. H-scroll supports full-screen, 8-line,
   and per-line modes. V-scroll returns whole-plane state; two-cell-column mode
   is decoded by md2gba_vdp_vscroll_column but cannot yet be projected directly. */
uint16_t md2gba_vdp_hscroll_base(const Md2GbaVdp *vdp);
int md2gba_vdp_hscroll_line(const Md2GbaVdp *vdp,unsigned plane,unsigned line,int16_t *value);
int md2gba_vdp_vscroll_plane(const Md2GbaVdp *vdp,unsigned plane,int16_t *value);
int md2gba_vdp_vscroll_column(const Md2GbaVdp *vdp,unsigned plane,unsigned column,int16_t *value);
int md2gba_gba_build_scroll_stream(const Md2GbaVdp *vdp,uint16_t stream[160][8]);
int md2gba_gba_build_scroll_stream_viewport(const Md2GbaVdp *vdp,uint16_t stream[160][8],unsigned ox,unsigned oy);
int md2gba_gba_build_direct_scroll_viewport(const Md2GbaVdp *vdp,uint16_t scroll[8],unsigned ox,unsigned oy);
int md2gba_gba_publish_live_whole_plane_scroll(const Md2GbaVdp *vdp,unsigned ox,unsigned oy);
void md2gba_gba_start_hblank_scroll_dma(const uint16_t stream[160][8]);
void md2gba_gba_stop_hblank_scroll_dma(void);
void md2gba_gba_scroll_latch_clear(void);

/* Optional four-BG projection that preserves Genesis plane priority classes:
   high OBJ -> A-high -> B-high -> low OBJ -> A-low -> B-low. */
int md2gba_gba_init_display_priority_vdp(const Md2GbaVdp *vdp,
                                         unsigned ah_sb,unsigned bh_sb,
                                         unsigned al_sb,unsigned bl_sb);
int md2gba_gba_commit_vdp_priority(const Md2GbaVdp *vdp,
                                   unsigned ah_sb,unsigned bh_sb,
                                   unsigned al_sb,unsigned bl_sb,
                                   Md2GbaBgTileCache *cache);

/* Phase 7 incremental frame planning. Planning is pure/host-testable; the
   commit helper applies the plan to GBA hardware and then clears VDP dirtiness. */
enum {
    MD2GBA_FRAME_PALETTE = 1u<<0,
    MD2GBA_FRAME_MAPS    = 1u<<1,
    MD2GBA_FRAME_TILES   = 1u<<2,
    MD2GBA_FRAME_SPRITES = 1u<<3,
    MD2GBA_FRAME_SCROLL  = 1u<<4,
    MD2GBA_FRAME_FULL    = 1u<<5,
    /* Phase 75: a small name-table change can be applied directly to the
       active GBA map while preserving the existing source->slot mapping. */
    MD2GBA_FRAME_MAP_PATCH = 1u<<6
};
typedef struct Md2GbaGbaUiPanel {
    uint8_t active,x,y,w,h;
    uint8_t palette;
} Md2GbaGbaUiPanel;

typedef struct Md2GbaGbaFrameState {
    Md2GbaBgTileCache bg_cache;
    uint32_t last_vram_epoch,last_cram_epoch,last_vsram_epoch,last_regs_epoch;
    uint32_t frames,full_rebuilds,map_rebuilds,map_patches,tile_updates,sprite_rebuilds,palette_updates,scroll_updates;
    uint16_t sprite_source_tiles[128];
    unsigned sprite_source_count;
    uint16_t last_view_x,last_view_y;
    uint8_t initialized;
    /* P83: presentation-only description of a source VDP framed bottom panel.
       The source VDP/name table remains untouched; this only tracks whether the
       GBA high-priority Plane-A map currently contains the width-fit projection. */
    uint8_t ui_active,ui_x,ui_y,ui_w,ui_h,ui_palette;
} Md2GbaGbaFrameState;
unsigned md2gba_vdp_collect_sprite_tiles(const Md2GbaVdp *vdp,uint16_t *out,unsigned max_tiles);
uint32_t md2gba_gba_plan_frame(const Md2GbaVdp *vdp,const Md2GbaGbaFrameState *state);
unsigned md2gba_gba_commit_dirty_cached_tiles(const Md2GbaVdp *vdp,const Md2GbaBgTileCache *cache);
/* P82: choose a hardware viewport that keeps a dense fixed high-priority
   bottom overlay (dialogue/menu UI) visible without title-specific coordinates. */
int md2gba_gba_detect_bottom_ui_panel(const Md2GbaVdp *vdp,Md2GbaGbaUiPanel *out);
unsigned md2gba_gba_suggest_view_y(const Md2GbaVdp *vdp);
unsigned md2gba_gba_suggest_view_y_cached(const Md2GbaVdp *vdp,const Md2GbaGbaFrameState *state,uint32_t plan);
/* Small hardware updates can be staged and applied by the physical VBlank IRQ
   instead of stalling translated 68K execution waiting for VBlank. */
int md2gba_gba_hw_commit_pending(void);
void md2gba_gba_hw_cancel_pending(void);
void md2gba_gba_hw_vblank_commit_pending(void);
int md2gba_gba_commit_priority_incremental(Md2GbaVdp *vdp,Md2GbaGbaFrameState *state,
                                           unsigned ah_sb,unsigned bh_sb,
                                           unsigned al_sb,unsigned bl_sb,
                                           uint16_t scroll_stream[160][8]);
/* Viewport-aware variant for projecting a larger Genesis raster into the
   240x160 GBA display.  ox/oy are source-pixel offsets into the Genesis view.
   The zero-offset legacy entry point remains unchanged. */
int md2gba_gba_commit_priority_incremental_viewport(Md2GbaVdp *vdp,Md2GbaGbaFrameState *state,
                                                    unsigned ah_sb,unsigned bh_sb,
                                                    unsigned al_sb,unsigned bl_sb,
                                                    uint16_t scroll_stream[160][8],
                                                    unsigned ox,unsigned oy);
/* P84: caller may pass the already-computed dirty plan to avoid a second
   complete plan traversal inside the commit path. */
int md2gba_gba_commit_priority_incremental_viewport_planned(Md2GbaVdp *vdp,Md2GbaGbaFrameState *state,
                                                            unsigned ah_sb,unsigned bh_sb,
                                                            unsigned al_sb,unsigned bl_sb,
                                                            uint16_t scroll_stream[160][8],
                                                            unsigned ox,unsigned oy,uint32_t plan);
unsigned md2gba_gba_commit_sprites_packed_viewport(const Md2GbaVdp *vdp,unsigned max_objs,
                                                    unsigned *dropped,unsigned ox,unsigned oy);
/* P134 hardware-only 320x224 -> 240x160 regular-tile projection. */
typedef void (*Md2GbaProjectedCheckpointFn)(void *user);
void md2gba_gba_projected_set_checkpoint(Md2GbaProjectedCheckpointFn fn,void *user);
void md2gba_gba_projected_reset(void);
int md2gba_gba_projected_pending(void);
int md2gba_gba_projected_present(Md2GbaVdp *vdp);
void md2gba_gba_projected_vblank_commit_pending(void);
unsigned md2gba_gba_projected_state_bytes(void);

typedef void (*Md2GbaGbaYieldFn)(void *user);
int md2gba_gba_commit_priority_incremental_coop(Md2GbaVdp *vdp,Md2GbaGbaFrameState *state,
                                                unsigned ah_sb,unsigned bh_sb,
                                                unsigned al_sb,unsigned bl_sb,
                                                uint16_t scroll_stream[160][8],
                                                Md2GbaGbaYieldFn yield_fn,void *yield_user);

#endif
