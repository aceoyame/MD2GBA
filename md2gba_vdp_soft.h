#ifndef MD2GBA_VDP_SOFT_H
#define MD2GBA_VDP_SOFT_H
#include "md2gba_bus.h"
#include <stdint.h>

enum {
    MD2GBA_VDP_COMPLEX_COLUMN_VSCROLL = 1u << 0,
    MD2GBA_VDP_COMPLEX_WINDOW         = 1u << 1,
    MD2GBA_VDP_COMPLEX_SHADOW_HILIGHT = 1u << 2
};

enum {
    MD2GBA_VDP_INTENSITY_NORMAL = 0,
    MD2GBA_VDP_INTENSITY_SHADOW = 1,
    MD2GBA_VDP_INTENSITY_HILIGHT = 2
};

uint16_t md2gba_vdp_window_base(const Md2GbaVdp *vdp);
unsigned md2gba_vdp_window_table_width(const Md2GbaVdp *vdp);
unsigned md2gba_vdp_visible_columns(const Md2GbaVdp *vdp);
unsigned md2gba_vdp_visible_rows(const Md2GbaVdp *vdp);
int md2gba_vdp_window_cell_active(const Md2GbaVdp *vdp,unsigned x_cell,unsigned y_cell);
uint32_t md2gba_vdp_complex_features(const Md2GbaVdp *vdp);
int md2gba_vdp_prefers_software_compositor(const Md2GbaVdp *vdp);

/* Palette index returned by the software compositor:
   0..63   normal CRAM
   64..127 shadow ramp
   128..191 highlight ramp. */
uint8_t md2gba_vdp_soft_pixel(const Md2GbaVdp *vdp,unsigned screen_x,unsigned screen_y);
void md2gba_vdp_soft_render_240x160(const Md2GbaVdp *vdp,volatile uint16_t *dst,
                                    unsigned genesis_origin_x,unsigned genesis_origin_y);
typedef void (*Md2GbaVdpSoftYieldFn)(void *user);
void md2gba_vdp_soft_render_240x160_coop(const Md2GbaVdp *vdp,volatile uint16_t *dst,
                                         unsigned genesis_origin_x,unsigned genesis_origin_y,
                                         Md2GbaVdpSoftYieldFn yield_fn,void *yield_user,
                                         unsigned yield_lines);
void md2gba_vdp_soft_render_rows_240x160_coop(const Md2GbaVdp *vdp,volatile uint16_t *dst,
                                              unsigned first_row,unsigned last_row,
                                              unsigned genesis_origin_x,unsigned genesis_origin_y,
                                              Md2GbaVdpSoftYieldFn yield_fn,void *yield_user,
                                              unsigned yield_lines);
int md2gba_vdp_soft_fullwidth_window_rows(const Md2GbaVdp *vdp,unsigned *first_row,unsigned *last_row);
int md2gba_vdp_soft_window_only_dirty(const Md2GbaVdp *vdp);
int md2gba_vdp_soft_bottom_ui_rows(const Md2GbaVdp *vdp,unsigned *first_row,unsigned *last_row);
int md2gba_vdp_soft_bottom_ui_only_dirty(const Md2GbaVdp *vdp);
uint16_t md2gba_vdp_soft_color_from_genesis(uint16_t md_color,unsigned intensity);

#endif
