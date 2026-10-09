#ifndef MD2GBA_GBA_SOFT_H
#define MD2GBA_GBA_SOFT_H
#include "md2gba_bus.h"
#include "md2gba_vdp_soft.h"
#include <stdint.h>

/* Slow correctness fallback for VDP states that cannot be represented exactly
   with GBA text BGs (column V-scroll, Window+scroll combinations, S/H).
   Mode 4 uses two 240x160 8bpp pages and the 192-entry compositor palette. */
void md2gba_gba_soft_commit_palette(const Md2GbaVdp *vdp);
void md2gba_gba_soft_init_mode4(unsigned visible_page);
void md2gba_gba_soft_show_page(unsigned page);
volatile uint16_t *md2gba_gba_soft_page_ptr(unsigned page);
void md2gba_gba_soft_copy_page(unsigned dst_page,unsigned src_page);
void md2gba_gba_soft_render_rows_page_coop(const Md2GbaVdp *vdp,unsigned page,
                                            unsigned first_row,unsigned last_row,
                                            unsigned genesis_origin_x,unsigned genesis_origin_y,
                                            Md2GbaVdpSoftYieldFn yield_fn,void *yield_user,
                                            unsigned yield_lines);
void md2gba_gba_soft_render_page(const Md2GbaVdp *vdp,unsigned page,
                                  unsigned genesis_origin_x,unsigned genesis_origin_y);
void md2gba_gba_soft_render_page_coop(const Md2GbaVdp *vdp,unsigned page,
                                       unsigned genesis_origin_x,unsigned genesis_origin_y,
                                       Md2GbaVdpSoftYieldFn yield_fn,void *yield_user,
                                       unsigned yield_lines);


/* P80 non-blocking Mode-4 presentation.  The request is applied by the shared
   physical VBlank IRQ, so translated CPU work does not stall waiting for the
   next display blank. */
void md2gba_gba_soft_request_page_flip(unsigned page);
int md2gba_gba_soft_page_flip_pending(void);
void md2gba_gba_soft_cancel_page_flip(void);
unsigned md2gba_gba_soft_visible_page(void);
#endif
