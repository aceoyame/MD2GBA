#include "md2gba_gba_backend.h"
#include "md2gba_compiler.h"
#include <stdint.h>
#if defined(MD2GBA_PROFILE_PROJECTED_UI)
#include "md2gba_projected_profile.h"
#endif

#ifndef MD2GBA_PRESENTATION_PROFILE_UI_NATIVE_BLOCK
#define MD2GBA_PRESENTATION_PROFILE_UI_NATIVE_BLOCK(v,index,refx,refy,vieww,viewh,sx,sy,w,h) 0
#endif
#ifndef MD2GBA_PRESENTATION_PROFILE_UI_CANDIDATE
#define MD2GBA_PRESENTATION_PROFILE_UI_CANDIDATE(v,refx,refy,vieww,viewh) 0
#endif
#ifndef MD2GBA_PRESENTATION_PROFILE_UI_BLOCKS_EXHAUSTIVE
#define MD2GBA_PRESENTATION_PROFILE_UI_BLOCKS_EXHAUSTIVE 0
#endif

/* P134 projected regular-tile backend.
   The guest remains a 320x224/VDP-domain machine.  This target-only presenter
   projects world coordinates by 3/4 horizontally and 5/7 vertically into a
   240x160 GBA Mode-0 view.  It deliberately uses no affine BG, framebuffer,
   guest-coordinate mutation, frame skipping, or title-specific camera rule. */

#define REG_DISPCNT (*(volatile uint16_t*)0x04000000u)
#define REG_BG0CNT  (*(volatile uint16_t*)0x04000008u)
#define REG_BG1CNT  (*(volatile uint16_t*)0x0400000Au)
#define REG_BG2CNT  (*(volatile uint16_t*)0x0400000Cu)
#define REG_BG3CNT  (*(volatile uint16_t*)0x0400000Eu)
#define REG_BG0HOFS (*(volatile uint16_t*)0x04000010u)
#define REG_BG0VOFS (*(volatile uint16_t*)0x04000012u)
#define REG_BG1HOFS (*(volatile uint16_t*)0x04000014u)
#define REG_BG1VOFS (*(volatile uint16_t*)0x04000016u)
#define REG_BG2HOFS (*(volatile uint16_t*)0x04000018u)
#define REG_BG2VOFS (*(volatile uint16_t*)0x0400001Au)
#define REG_BG3HOFS (*(volatile uint16_t*)0x0400001Cu)
#define REG_BG3VOFS (*(volatile uint16_t*)0x0400001Eu)
#define REG_DMA3SAD (*(volatile uint32_t*)0x040000D4u)
#define REG_DMA3DAD (*(volatile uint32_t*)0x040000D8u)
#define REG_DMA3CNT (*(volatile uint32_t*)0x040000DCu)
#define REG_IME     (*(volatile uint16_t*)0x04000208u)
#define BG_PAL ((volatile uint16_t*)0x05000000u)
#define OBJ_PAL ((volatile uint16_t*)0x05000200u)
#define BG_VRAM16 ((volatile uint16_t*)0x06000000u)
#define BG_VRAM32 ((volatile uint32_t*)0x06000000u)
#define OBJ_VRAM32 ((volatile uint32_t*)0x06010000u)
#define OAM16 ((volatile uint16_t*)0x07000000u)

#define PROJ_MAGIC 0x50313334u /* P134 */
#define PROJ_LAYERS 4u
#define PROJ_MAP_CELLS 1024u
#define PROJ_TILE_SLOTS 768u
#define PROJ_MAP_WRITES 512u
#define PROJ_COVER_W 31u
#define PROJ_COVER_H 21u
#define PROJ_SB_FIRST 24u
#define PROJ_OBJ_BANK_TILES 512u
#define PROJ_UI_MAX_PANELS 6u

#if defined(MD2GBA_GBA_IWRAM)
#define MD2GBA_PROJECTED_HOT __attribute__((section(".iwram.projected"),noinline))
#else
#define MD2GBA_PROJECTED_HOT __attribute__((noinline))
#endif
#define MD2GBA_PROJECTED_COLD __attribute__((section(".rom.cold.projected"),noinline))

typedef struct ProjUiPanel {
    /* Source name-table rectangle uses an unwrapped tile coordinate near the
       current camera. Destination dx/dy is a wrapped 32x32 GBA map cell.
       ox/oy records the actual screen-space native overlay origin after the
       shared BG scroll phase is accounted for. */
    int16_t sx,sy,ox,oy;
    uint8_t w,h,dx,dy;
} ProjUiPanel;

#if defined(MD2GBA_PROJECTED_HOST_TEST)
static unsigned md2gba_projected_test_bg_builds;
static unsigned md2gba_projected_test_bg_reuses;
static unsigned md2gba_projected_test_inplace_rebuilds;
static unsigned md2gba_projected_test_bg_assemblies;
static unsigned md2gba_projected_test_bg_name_reads;
static unsigned md2gba_projected_test_queue_mask_clears;
static unsigned md2gba_projected_test_bg_source_checks;
static unsigned md2gba_projected_test_obj_builds;
static unsigned md2gba_projected_test_map_range_checks;
static unsigned md2gba_projected_test_ui_scans;
#endif

/* Reuse P72's already-budgeted 32 KiB EWRAM staging arena.  The old renderer
   and this presenter never own it simultaneously; main cancels/reset ownership
   on renderer transitions. */
extern uint16_t md2gba_bg_tile_stage[1024u * 16u];
extern void md2gba_gba_stop_hblank_scroll_dma(void);
extern void md2gba_gba_scroll_latch_clear(void);

/* Generic foreground checkpoint used only by long projector transactions.
   The presenter does not know or care what the callback services; the PSIV
   target uses it for audio so a large transition cannot starve FIFO-A for
   several physical frames. Ordinary incremental walking never calls it. */
static Md2GbaProjectedCheckpointFn projected_checkpoint_fn;
static void *projected_checkpoint_user;
void md2gba_gba_projected_set_checkpoint(Md2GbaProjectedCheckpointFn fn,void *user){projected_checkpoint_fn=fn;projected_checkpoint_user=user;}
static void projected_checkpoint(void){if(projected_checkpoint_fn)projected_checkpoint_fn(projected_checkpoint_user);}

typedef struct ProjMapWrite {
    uint16_t map_index,new_slot,old_slot;
} ProjMapWrite;

typedef struct __attribute__((may_alias)) Md2GbaProjectedState {
    uint32_t magic;
    uint32_t last_vram_epoch,last_cram_epoch,last_vsram_epoch,last_regs_epoch,last_scroll_epoch;
    uint16_t plane_base[2],plane_w,plane_h;
    int32_t origin_x[2],origin_y[2];
    uint16_t active_slot[PROJ_LAYERS][PROJ_MAP_CELLS];
    uint16_t slot_ref[PROJ_TILE_SLOTS];
    uint32_t slot_hash[PROJ_TILE_SLOTS];
    /* 256-bucket exact-content hash index. P134F/P134G linearly scanned up to
       767 slots for every entering/dirty cell, which dominated camera streaming
       after projection itself was made incremental. */
    uint16_t slot_hash_head[256],slot_hash_next[PROJ_TILE_SLOTS],free_scan_cursor;
    uint32_t queue_cell_mask[PROJ_LAYERS][32];
    /* Offscreen cells can retire logically without spending a queued VRAM zero
       write.  A stale bit forces the next entering write even when the desired
       slot is transparent/zero, so hidden old map data can never reappear. */
    uint32_t stale_cell_mask[PROJ_LAYERS][32];
    ProjMapWrite map_write[PROJ_MAP_WRITES];
    uint16_t map_write_count,slot_highwater;
    uint16_t scroll[8];
    uint16_t palette[64];
    uint16_t oam[128][4];
    /* Conservative resident-world dependency mask. It is reset on a full
       rebuild and only grows as the ring streams. False positives cost a
       bounded dirty scan; false negatives are impossible because name-table
       dirtiness independently forces a scan. */
    uint32_t bg_source_mask[64];
    /* P134H: compact per-hardware-cell pattern bloom. High/low layers for one
       source plane share the same source-name dependencies, so two 32x32 maps
       are enough. Bloom collisions only cause an exact fallback check. */
    uint32_t cell_dep_bloom[2][PROJ_MAP_CELLS];
    /* P137: exact name-table dirty candidates use scratch inside the already
       reserved projected staging arena. The removed 2 KiB bloom leaves ample
       room, avoiding a new persistent EWRAM allocation. */
    uint32_t dirty_name_cells[2][32];
    /* Native/detached UI owns text and portrait readability now, so the world
       projector no longer carries the old sparse-glyph dilation cache. */
    uint32_t sprite_pixel_key;
    uint32_t sprite_source_mask[64];
    /* Exact source-geometry descriptor resident in each OBJ pixel bank for each
       Genesis SAT entry. This lets animation update one actor without rebuilding
       every visible sprite into the inactive bank. */
    uint32_t sprite_bank_key[2][80];
    /* A matching source descriptor is not sufficient when SAT visibility/order
       changes: the same actor may move to a different packed OBJ tile offset.
       Track the exact destination tile resident in each bank so menu cursors or
       newly visible NPCs cannot momentarily expose another sprite's pixels. */
    uint16_t sprite_bank_tile[2][80];
    /* Native-detail fixed UI overlay state. Framed screen-space Plane-A panels
       up to 30x20 tiles are copied 1:1 after the world projection; wide dialogue
       panels stay on the existing projected/readability path. */
    /* Target/suppression ownership is map-space, not screen-space. 32 rows are
       required because a fixed overlay may land across any wrapped map row
       while BG0 follows the field camera. */
    uint32_t ui_target_mask[32],ui_suppress_mask[32];
    uint32_t ui_signature,ui_pattern_bloom;
    ProjUiPanel ui_panel[PROJ_UI_MAX_PANELS];
    uint8_t ui_count,ui_candidate; /* 0 none, 1 partial, 2 complete geometry seen once */
    int16_t ui_source_view_x,ui_source_view_y;
    /* Cached non-position SAT geometry and SAT-entry -> OAM ownership. Normal
       field walking rewrites X/Y continuously; if size/link/attributes,
       visibility and the projection-height variant are unchanged, P134H can
       update OAM coordinates without running the two-pass pixel projector. */
    uint16_t sprite_sat_meta[80][2];
    uint8_t sprite_oam_index[80];
    uint16_t sprite_tile_count;
    uint8_t active_obj_bank,sprite_valid;
    uint8_t pending,palette_valid,scroll_valid,sprite_queue_valid,sprite_queue_bank;
    /* Screenblocks 24..27 and 28..31 are alternate regular-BG map banks.
       Full projected rebuilds can prepare the inactive bank while the previous
       complete frame remains visible, then switch banks atomically at VBlank. */
    uint8_t queue_word_stamp[64]; /* two 4-bit generations per mask word */
    uint8_t map_bank,pending_map_bank,bank_switch_pending,rebase_cleanup;
    uint8_t initialized,capacity_failed,queue_stamp_gen;
} Md2GbaProjectedState;

typedef char md2gba_projected_state_fits_stage[(sizeof(Md2GbaProjectedState)<=sizeof(md2gba_bg_tile_stage))?1:-1];
#define PS ((Md2GbaProjectedState*)(void*)md2gba_bg_tile_stage)

/* Exact constant divisors without EABI divide helpers.  Split the 32-bit
   numerator at 16 bits, divide the high half with a bounded reciprocal, then
   fold the divisor remainder into the low half.  This is exact for the full
   uint32_t domain and is much cheaper on ARM7TDMI/Thumb than __aeabi_*div. */
static uint32_t udiv3_small(uint32_t x){return (x*43691u)>>17;}
static uint32_t udiv5_small(uint32_t x){return (x*52429u)>>18;}
static uint32_t udiv7_small(uint32_t x){uint32_t q=(x*9363u)>>16;return q*7u>x?q-1u:q;}
static uint32_t udiv224_small(uint32_t x){uint32_t q=(x*4682u)>>20;return q*224u>x?q-1u:q;}
static uint32_t udiv3_exact(uint32_t n){uint32_t hi=n>>16,lo=n&0xffffu,qh=udiv3_small(hi),r=hi-qh*3u;return (qh<<16)+r*21845u+udiv3_small(lo+r);}
static uint32_t udiv5_exact(uint32_t n){uint32_t hi=n>>16,lo=n&0xffffu,qh=udiv5_small(hi),r=hi-qh*5u;return (qh<<16)+r*13107u+udiv5_small(lo+r);}
static uint32_t udiv7_exact(uint32_t n){uint32_t hi=n>>16,lo=n&0xffffu,qh=udiv7_small(hi),r=hi-qh*7u;return (qh<<16)+r*9362u+udiv7_small(lo+r*2u);}
static uint32_t udiv224_exact(uint32_t n){uint32_t hi=n>>16,lo=n&0xffffu,qh=udiv224_small(hi),r=hi-qh*224u;return (qh<<16)+r*292u+udiv224_small(lo+r*128u);}
static int32_t floordiv3(int32_t n){if(n>=0)return (int32_t)udiv3_exact((uint32_t)n);{uint32_t a=0u-(uint32_t)n,q=udiv3_exact(a),r=a-q*3u;return -(int32_t)q-(r!=0u);}}
static int32_t floordiv5(int32_t n){if(n>=0)return (int32_t)udiv5_exact((uint32_t)n);{uint32_t a=0u-(uint32_t)n,q=udiv5_exact(a),r=a-q*5u;return -(int32_t)q-(r!=0u);}}
static int32_t floordiv7(int32_t n){if(n>=0)return (int32_t)udiv7_exact((uint32_t)n);{uint32_t a=0u-(uint32_t)n,q=udiv7_exact(a),r=a-q*7u;return -(int32_t)q-(r!=0u);}}
static int32_t floorpow2(int32_t n,unsigned sh){if(n>=0)return n>>sh;return -(int32_t)(((uint32_t)(-n)+((1u<<sh)-1u))>>sh);}
static int32_t qx(int32_t x){return floorpow2(x*3,2u);}
MD2GBA_PROJECTED_HOT static int32_t qy(int32_t y){return floordiv7(y*5);}
/* Every supported plane dimension is a power of two, so signed wrapping is
   exactly the low bits of the two's-complement coordinate. */
static unsigned imod(int32_t x,unsigned m){return ((uint32_t)x)&(m-1u);}
static uint16_t vrw(const Md2GbaVdp*v,unsigned a){return (uint16_t)(((uint16_t)v->vram[(uint16_t)a]<<8)|v->vram[(uint16_t)(a+1u)]);}
static uint8_t src_px(const Md2GbaVdp*v,unsigned tile,unsigned x,unsigned y){unsigned a=(tile&0x7ffu)*32u+(y&7u)*4u+((x&7u)>>1);uint8_t b=v->vram[(uint16_t)a];return (uint8_t)((x&1u)?(b&15u):(b>>4));}
static int block_dirty(const Md2GbaVdp*v,unsigned a){unsigned b=((uint16_t)a)>>5;return !!(v->vram_dirty_tiles[b>>5]&(1u<<(b&31u)));}
static int tile_dirty(const Md2GbaVdp*v,unsigned t){return t<2048u && !!(v->vram_dirty_tiles[t>>5]&(1u<<(t&31u)));}
/* P134Z: exact blank-pattern rejection.  The deep Y profile showed that more
   than half of opening projected-pair builds consume only all-zero source
   patterns.  Check the raw 32-byte Genesis pattern directly before entering
   the 64-pixel projector.  This deliberately does not treat tile id 0 as
   transparent and carries no cache/invalidity state: every answer is derived
   from the current VRAM bytes. */
static int pattern_nonzero_exact(const Md2GbaVdp*v,unsigned t){
    typedef uint32_t PatternWord __attribute__((may_alias));
    const PatternWord*w=(const PatternWord*)(const void*)(&v->vram[(t&0x7ffu)*32u]);
    return !!(w[0]|w[1]|w[2]|w[3]|w[4]|w[5]|w[6]|w[7]);
}
static int mask_dirty(const Md2GbaVdp*v,const uint32_t mask[64]){unsigned i;for(i=0u;i<64u;i++)if(v->vram_dirty_tiles[i]&mask[i])return 1;return 0;}

static uint16_t vrw(const Md2GbaVdp*v,unsigned a);
MD2GBA_PROJECTED_HOT static void get_camera(const Md2GbaVdp*v,unsigned plane,int32_t*cx,int32_t*cy){
    int16_t hs=0,vs=0;
    if(v->present_scroll_valid){hs=v->present_hscroll[plane&1u];vs=v->present_vscroll[plane&1u];}
    else {(void)md2gba_vdp_hscroll_line(v,plane,0u,&hs);(void)md2gba_vdp_vscroll_plane(v,plane,&vs);}
    *cx=-(int32_t)hs;*cy=(int32_t)vs;
}


static uint16_t ui_name_word(const Md2GbaVdp*v,uint16_t base,unsigned w,unsigned h,int32_t x,int32_t y){
    unsigned mx=((uint32_t)x)&(w-1u),my=((uint32_t)y)&(h-1u);return vrw(v,(unsigned)base+2u*(my*w+mx));
}
static int ui_rect_overlap(const ProjUiPanel*a,const ProjUiPanel*b){
    int aw=(int)a->w*8,ah=(int)a->h*8,bw=(int)b->w*8,bh=(int)b->h*8;
    return a->ox < b->ox+bw && b->ox < a->ox+aw && a->oy < b->oy+bh && b->oy < a->oy+ah;
}
static void ui_panel_copy(ProjUiPanel*d,const ProjUiPanel*s){d->sx=s->sx;d->sy=s->sy;d->ox=s->ox;d->oy=s->oy;d->w=s->w;d->h=s->h;d->dx=s->dx;d->dy=s->dy;}
static uint32_t ui_sig_mix(uint32_t h,uint32_t x){h^=x;h*=16777619u;return h;}
static int32_t ui_unwrap_near(unsigned raw,int32_t ref,unsigned period){
    /* Plane periods are 32/64. Normalize with the power-of-two mask instead of
       a repeated-subtract loop that LLVM strength-reduced into __aeabi_uidiv. */
    int32_t half=(int32_t)(period>>1);
    int32_t d=(int32_t)(((uint32_t)((int32_t)raw-ref+half))&(period-1u))-half;
    return ref+d;
}
/* Choose a map-cell origin whose shared BG scroll places an unscaled 8-pixel
   tile grid as close as possible to the desired screen coordinate. We prefer
   keeping the entire native surface visible over sub-tile positional fidelity. */
static int32_t ui_align_map_cell(int desired,int scroll,unsigned pixels,unsigned limit,int*actual){
    int32_t cell=floorpow2((int32_t)desired+(int32_t)scroll+4,3u),a=cell*8-scroll;
    while(a<0){cell++;a+=8;}while(a+(int)pixels>(int)limit){cell--;a-=8;}
    if(actual)*actual=(int)a;return cell;
}
static void ui_place_native(ProjUiPanel*q,int32_t cax,int32_t cay){
    unsigned ow=q->w>30u?30u:(unsigned)q->w;int32_t sxpx=q->sx*8-cax,sypx=q->sy*8-cay;
    int wantx=(int)qx(sxpx+(int32_t)q->w*4)-(int)ow*4;
    int wanty=(int)qy(sypx+(int32_t)q->h*4)-(int)q->h*4;
    int maxx=240-(int)ow*8,maxy=160-(int)q->h*8,ax,ay;
    int scrollx=(int)qx(cax),scrolly=(int)qy(cay);int32_t mx,my;
    if(wantx<0)wantx=0;if(wantx>maxx)wantx=maxx;
    if(wanty<0)wanty=0;if(wanty>maxy)wanty=maxy;
    mx=ui_align_map_cell(wantx,scrollx,ow*8u,240u,&ax);
    my=ui_align_map_cell(wanty,scrolly,(unsigned)q->h*8u,160u,&ay);
    q->dx=(uint8_t)imod(mx,32u);q->dy=(uint8_t)imod(my,32u);q->ox=(int16_t)ax;q->oy=(int16_t)ay;
}
/* Detect fixed framed Plane-A panels in the *visible source window*. Unlike
   P134J this is camera-aware: UI authored into the scrolled field name table is
   still recognized and then copied 1:1 into map-space cells that compensate the
   shared BG0 scroll. Wide >30-tile dialogue remains on the established fitted
   path; ordinary menu/status panels become native-detail overlays. */
static unsigned scan_fixed_ui_panels(const Md2GbaVdp*v,ProjUiPanel out[PROJ_UI_MAX_PANELS],uint32_t*sig_out){
#if defined(MD2GBA_PROJECTED_HOST_TEST)
    md2gba_projected_test_ui_scans++;
#endif
    unsigned w=PS->plane_w,h=PS->plane_h,ly,x0,count=0u;uint16_t base=PS->plane_base[0];uint64_t used[28];
    int32_t cx,cy,stx,sty;uint32_t sig=2166136261u;unsigned ui0;for(ui0=0u;ui0<28u;ui0++)used[ui0]=0ull;
    get_camera(v,0u,&cx,&cy);if(w<32u||h<32u){if(sig_out)*sig_out=0u;return 0u;}
    stx=floorpow2(cx,3u);sty=floorpow2(cy,3u);

    /* Profile blocks are authoritative source metadata, so reserve them before
       generic window recognition.  P134K appended these after the generic scan;
       a false-positive framed region could consume the bounded panel slots and
       leave PSIV's portrait in the scaled world.  The profile gets the current
       source viewport so title-specific resources can be found camera-relatively
       without teaching the reusable renderer their tile IDs. */
    {unsigned idx,profile_count=0u;uint16_t blocks[PROJ_UI_MAX_PANELS][4];
#if defined(MD2GBA_PRESENTATION_PROFILE_UI_SCAN)
        profile_count=MD2GBA_PRESENTATION_PROFILE_UI_SCAN(v,stx,sty,40u,28u,blocks);
#else
        for(idx=0u;idx<PROJ_UI_MAX_PANELS;idx++){
            unsigned sx,sy,bw,bh;
            if(!MD2GBA_PRESENTATION_PROFILE_UI_NATIVE_BLOCK(v,idx,stx,sty,40u,28u,&sx,&sy,&bw,&bh))break;
            blocks[idx][0]=(uint16_t)sx;blocks[idx][1]=(uint16_t)sy;blocks[idx][2]=(uint16_t)bw;blocks[idx][3]=(uint16_t)bh;profile_count++;
        }
#endif
      for(idx=0u;idx<profile_count&&count<PROJ_UI_MAX_PANELS;idx++){
        unsigned rsx=blocks[idx][0],rsy=blocks[idx][1],rw=blocks[idx][2],rh=blocks[idx][3];ProjUiPanel q;unsigned j;int bad=0;
        if(!rw||!rh||rw>40u||rh>20u)continue;
        q.sx=(int16_t)rsx;q.sy=(int16_t)rsy;q.w=(uint8_t)rw;q.h=(uint8_t)rh;ui_place_native(&q,cx,cy);
        for(j=0u;j<count;j++)if(ui_rect_overlap(&q,&out[j])){bad=1;break;}if(bad)continue;
        out[count].sx=q.sx;out[count].sy=q.sy;out[count].ox=q.ox;out[count].oy=q.oy;out[count].w=q.w;out[count].h=q.h;out[count].dx=q.dx;out[count].dy=q.dy;count++;sig=ui_sig_mix(sig,0x50524F46u+idx);
        sig=ui_sig_mix(sig,(uint32_t)(uint16_t)q.sx|((uint32_t)(uint16_t)q.sy<<16));
        sig=ui_sig_mix(sig,(uint32_t)q.dx|((uint32_t)q.dy<<8)|((uint32_t)q.w<<16)|((uint32_t)q.h<<24));
    }}

    if(MD2GBA_PRESENTATION_PROFILE_UI_BLOCKS_EXHAUSTIVE&&
       (count||MD2GBA_PRESENTATION_PROFILE_UI_CANDIDATE(v,stx,sty,40u,28u))){if(sig_out)*sig_out=count?sig:0u;return count;}

    /* Generic fixed-window recognition follows the authored border grammar,
       not the priority bit of every cell.  Real PSIV menus mix priority between
       chrome, fill, glyphs, cursors and portrait/status contents.  P134K/L's
       all-high-priority rectangle test therefore missed the real menu and sent
       its 8x8 font back through the 3/4 x 5/7 world minifier.  Requiring mirrored
       corner/side art plus repeated top/bottom edge art remains a much stronger
       UI signature than priority density while permitting arbitrary interior
       text/icon priority. */
    for(ly=0u;ly<28u && count<PROJ_UI_MAX_PANELS;ly++)for(x0=0u;x0<40u && count<PROJ_UI_MAX_PANELS;x0++){
        unsigned x1,ph,y,x;int32_t ay=sty+(int32_t)ly,ax0=stx+(int32_t)x0;uint16_t tl,ti,li;unsigned pal;ProjUiPanel q;int accepted=0;
        if(used[ly]&(1ull<<x0))continue;tl=ui_name_word(v,base,w,h,ax0,ay);if(!(tl&0x07ffu))continue;pal=tl&0x6000u;
        if(x0+2u>=40u)continue;ti=ui_name_word(v,base,w,h,ax0+1,ay);if(!(ti&0x07ffu)||(ti&0x6000u)!=pal)continue;
        for(x1=x0+2u;x1<40u&&!accepted;x1++){
            uint16_t tr=ui_name_word(v,base,w,h,stx+(int32_t)x1,ay);int bad=0;
            q.w=(uint8_t)(x1-x0+1u);if(q.w>40u)break;
            if((tr&0x07ffu)!=(tl&0x07ffu)||!(tr&0x0800u)||(tr&0x6000u)!=pal)continue;
            /* The complete top edge repeats one source tile; ignore only the
               priority bit, retaining palette/flip/tile identity. */
            for(x=x0+1u;x<x1;x++){uint16_t n=ui_name_word(v,base,w,h,stx+(int32_t)x,ay);if((n&0x7fffu)!=(ti&0x7fffu)){bad=1;break;}}if(bad)continue;
            for(ph=3u;ph<=20u && ly+ph<=28u;ph++){
                int32_t by=ay+(int32_t)ph-1;uint16_t bl=ui_name_word(v,base,w,h,ax0,by),br=ui_name_word(v,base,w,h,stx+(int32_t)x1,by),bi=ui_name_word(v,base,w,h,ax0+1,by);
                /* Bottom edge is the vertical mirror of the top edge. */
                if((bl&0x07ffu)!=(tl&0x07ffu)||(br&0x07ffu)!=(tl&0x07ffu)||(bi&0x07ffu)!=(ti&0x07ffu))continue;
                if(!(bl&0x1000u)||!(br&0x1000u)||!(bi&0x1000u)||!(br&0x0800u))continue;
                if((bl&0x6000u)!=pal||(br&0x6000u)!=pal||(bi&0x6000u)!=pal)continue;
                for(x=x0+1u;x<x1;x++){uint16_t n=ui_name_word(v,base,w,h,stx+(int32_t)x,by);if((n&0x07ffu)!=(ti&0x07ffu)||!(n&0x1000u)||(n&0x6000u)!=pal){bad=1;break;}}if(bad){bad=0;continue;}
                li=0u;bad=0;
                for(y=1u;y+1u<ph;y++){
                    int32_t yy=ay+(int32_t)y;uint16_t l=ui_name_word(v,base,w,h,ax0,yy),r=ui_name_word(v,base,w,h,stx+(int32_t)x1,yy);
                    if(y==1u)li=l;
                    if((l&0x07ffu)!=(li&0x07ffu)||(r&0x07ffu)!=(li&0x07ffu)||!(r&0x0800u)||(l&0x6000u)!=pal||(r&0x6000u)!=pal){bad=1;break;}
                    /* Interior is deliberately unconstrained: glyphs, cursor,
                       portraits and status numbers may use other priorities or
                       palettes while the frame itself remains structurally fixed. */
                }
                if(bad)continue;
                q.sx=(int16_t)ax0;q.sy=(int16_t)ay;q.h=(uint8_t)ph;ui_place_native(&q,cx,cy);
                for(x=0u;x<count;x++)if(ui_rect_overlap(&q,&out[x])){bad=1;break;}if(bad){bad=0;continue;}
                out[count].sx=q.sx;out[count].sy=q.sy;out[count].ox=q.ox;out[count].oy=q.oy;out[count].w=q.w;out[count].h=q.h;out[count].dx=q.dx;out[count].dy=q.dy;count++;for(y=ly;y<ly+ph;y++)for(x=x0;x<=x1;x++)used[y]|=1ull<<x;
                sig=ui_sig_mix(sig,(uint32_t)(uint16_t)q.sx|((uint32_t)(uint16_t)q.sy<<16));
                sig=ui_sig_mix(sig,(uint32_t)q.w|((uint32_t)q.h<<8)|((uint32_t)q.dx<<16)|((uint32_t)q.dy<<24));
                accepted=1;break;
            }
        }
    }
    if(sig_out)*sig_out=count?sig:0u;return count;
}
typedef uint32_t ProjAliasWord __attribute__((may_alias));
static uint32_t hash64_words(const uint8_t p[64],uint32_t*nonzero){
    /* Output tiles are assembled on 4-byte-aligned stack storage. Hash four
       pixels at a time and fold the transparency test into the same pass. The
       old byte-FNV + separate all-zero scan walked each 64-byte tile twice. */
    const ProjAliasWord*w=(const ProjAliasWord*)(const void*)p;uint32_t h=2166136261u,nz=0u;unsigned i;
    for(i=0u;i<16u;i++){uint32_t x=w[i];nz|=x;h^=x;h*=16777619u;}
    if(nonzero)*nonzero=nz;return h?h:1u;
}
static int tile_vram_equal(unsigned slot,const uint8_t p[64]){const uint32_t*w=(const uint32_t*)(const void*)p;volatile uint32_t*v=BG_VRAM32+slot*16u;unsigned i;for(i=0;i<16u;i++)if(v[i]!=w[i])return 0;return 1;}
static void tile_vram_write(unsigned slot,const uint8_t p[64]){const uint32_t*w=(const uint32_t*)(const void*)p;volatile uint32_t*v=BG_VRAM32+slot*16u;unsigned i;for(i=0;i<16u;i++)v[i]=w[i];}

/* Full-fit has exactly 3 horizontal and 5 vertical target-tile phases.
   These PC-verified nearest-neighbor phase plans remove per-pixel division from
   the hot dirty-cell assembler; each tile needs only one source base per axis. */
/* P134O predecodes the inverse-scale tile phase all the way to source-cell
   delta + local pixel. This removes per-output-tile pixel modulo, uniqueness
   searches and bx/by reconstruction from the world hot path. */
static const uint8_t proj_xcell[3][8]={{0,0,0,0,0,0,1,1},{0,0,0,0,1,1,1,1},{0,0,1,1,1,1,1,1}};
static const uint8_t proj_xlocal[3][8]={{0,1,2,4,5,6,0,1},{2,4,5,6,0,1,2,4},{5,6,0,1,2,4,5,6}};
static const uint8_t proj_ycell[5][8]={{0,0,0,0,0,0,1,1},{0,0,0,0,1,1,1,1},{0,0,1,1,1,1,1,2},{0,0,0,0,0,1,1,1},{0,0,0,1,1,1,1,1}};
static const uint8_t proj_ylocal[5][8]={{0,1,2,4,5,7,0,1},{3,4,6,7,0,2,3,5},{6,7,1,2,4,5,6,0},{1,3,4,5,7,0,2,3},{4,6,7,1,2,3,5,6}};
static const uint8_t proj_ybase_rem[5]={0u,1u,2u,4u,5u};

/* Layer order matches the existing renderer contract: A-high, B-high,
   A-low, B-low. P134H assembles one plane's high/low target tiles together so
   source name/pattern fetches are performed once instead of twice. Palette line
   is baked into the 8bpp pixel index so one output tile may sample cells from
   multiple source palette lines without conflicts. */
static unsigned dep_bloom_bit(unsigned t){return 1u<<(((t&0x7ffu)*0x9E3779B1u)>>27);}
/* Host-only oracle for the superseded 8-bit map bloom. */
static unsigned map_bloom_bit(unsigned block){return 1u<<(((block&255u)*0x9E3779B1u)>>29);}


static const uint8_t proj_xsh[3][4][8]={{{4,0,12,20,16,28,4,0},{24,28,16,8,12,0,4,0},{4,0,12,20,16,28,24,28},{24,28,16,8,12,0,24,28}},{{12,20,16,28,4,0,12,20},{16,8,12,0,4,0,12,20},{12,20,16,28,24,28,16,8},{16,8,12,0,24,28,16,8}},{{16,28,4,0,12,20,16,28},{12,0,4,0,12,20,16,28},{16,28,24,28,16,8,12,0},{12,0,24,28,16,8,12,0}}};
static const uint32_t proj_xside[3][2]={{0x00000000u,0xFFFF0000u},{0x00000000u,0xFFFFFFFFu},{0xFFFF0000u,0xFFFFFFFFu}};
MD2GBA_PROJECTED_HOT static uint8_t assemble_bg_tile_pair_rows(const Md2GbaVdp*v,const uint16_t names[3][2],unsigned xphase,unsigned yphase,uint8_t hi[64],uint8_t lo[64]){
#if defined(MD2GBA_PROJECTED_HOST_TEST)
    md2gba_projected_test_bg_assemblies++;
#endif
    unsigned py;uint8_t nz=0u;const uint8_t*xc=proj_xcell[xphase];const uint32_t sm0=proj_xside[xphase][0],sm1=proj_xside[xphase][1];
    ProjAliasWord*H=(ProjAliasWord*)(void*)hi,*L=(ProjAliasWord*)(void*)lo;
    for(py=0u;py<8u;py++){
        unsigned yc=proj_ycell[yphase][py],base_ly=proj_ylocal[yphase][py];uint16_t nl=names[yc][0],nr=names[yc][1];
        unsigned lly=(nl&0x1000u)?7u-base_ly:base_ly,rly=(nr&0x1000u)?7u-base_ly:base_ly;
        uint32_t r[2],w0,w1,pal,pm,m,vv,h,l;
        const uint8_t*sh=proj_xsh[xphase][((nl>>11)&1u)|((nr>>10)&2u)];
        r[0]=*(const ProjAliasWord*)(const void*)(&v->vram[(nl&0x7ffu)*32u+lly*4u]);
        r[1]=*(const ProjAliasWord*)(const void*)(&v->vram[(nr&0x7ffu)*32u+rly*4u]);
        w0=((r[xc[0]]>>sh[0])&15u)|(((r[xc[1]]>>sh[1])&15u)<<8)|(((r[xc[2]]>>sh[2])&15u)<<16)|(((r[xc[3]]>>sh[3])&15u)<<24);
        w1=((r[xc[4]]>>sh[4])&15u)|(((r[xc[5]]>>sh[5])&15u)<<8)|(((r[xc[6]]>>sh[6])&15u)<<16)|(((r[xc[7]]>>sh[7])&15u)<<24);
        {uint32_t pl=((nl>>13)&3u)*0x10101010u,pr=((nr>>13)&3u)*0x10101010u,hl=(nl&0x8000u)?0xFFFFFFFFu:0u,hr=(nr&0x8000u)?0xFFFFFFFFu:0u;
         m=((w0|(w0>>1)|(w0>>2)|(w0>>3))&0x01010101u)*0xFFu;pal=(pl&~sm0)|(pr&sm0);pm=(hl&~sm0)|(hr&sm0);vv=w0|(pal&m);h=vv&pm;l=vv&~pm;
         H[py*2u]=h;L[py*2u]=l;nz|=(uint8_t)((h?1u:0u)|(l?2u:0u));
         m=((w1|(w1>>1)|(w1>>2)|(w1>>3))&0x01010101u)*0xFFu;pal=(pl&~sm1)|(pr&sm1);pm=(hl&~sm1)|(hr&sm1);vv=w1|(pal&m);h=vv&pm;l=vv&~pm;
         H[py*2u+1u]=h;L[py*2u+1u]=l;nz|=(uint8_t)((h?1u:0u)|(l?2u:0u));}
    }
    return nz;
}

static void make_bg_tile_pair_ex(const Md2GbaVdp*v,unsigned plane,int32_t tx,int32_t ty,
                              uint8_t hi[64],uint8_t lo[64],uint32_t*dep_bloom,uint8_t*map_bloom,uint8_t*nzmask){
#if defined(MD2GBA_PROJECTED_HOST_TEST)
    md2gba_projected_test_bg_builds++;
#endif
    unsigned w=PS->plane_w,h=PS->plane_h;uint16_t base=PS->plane_base[plane];
    unsigned yd,xd;uint16_t names[3][2];uint32_t bloom=0u;uint8_t nz,all_blank=1u;
    int32_t qx3=floordiv3(tx),qy5=floordiv5(ty);unsigned xphase=(unsigned)(tx-qx3*3),yphase=(unsigned)(ty-qy5*5);
    int32_t basecx=tx+qx3,basecy=qy5*7+(int32_t)proj_ybase_rem[yphase];
    /* Four of the five Y phases touch exactly two source rows. Fetch the
       third only for phase 2; this also avoids false dirty dependencies. */
    for(yd=0u;yd<(yphase==2u?3u:2u);yd++)for(xd=0u;xd<2u;xd++){
#if defined(MD2GBA_PROJECTED_HOST_TEST)
        md2gba_projected_test_bg_name_reads++;
#endif
        unsigned cx=imod(basecx+(int32_t)xd,w),cy=imod(basecy+(int32_t)yd,h),name_index=cy*w+cx;uint16_t n=vrw(v,(unsigned)base+2u*name_index);unsigned t=n&0x7ffu;
        names[yd][xd]=n;PS->bg_source_mask[t>>5]|=1u<<(t&31u);bloom|=dep_bloom_bit(t);
        if(all_blank&&pattern_nonzero_exact(v,t))all_blank=0u;
    }
    if(all_blank){
        ProjAliasWord*hw=(ProjAliasWord*)(void*)hi,*lw=(ProjAliasWord*)(void*)lo;
        for(xd=0u;xd<16u;xd++)hw[xd]=lw[xd]=0u;
        nz=0u;
    }else nz=assemble_bg_tile_pair_rows(v,names,xphase,yphase,hi,lo);
    if(dep_bloom)*dep_bloom=bloom;if(map_bloom)*map_bloom=0u;if(nzmask)*nzmask=nz;
}

static void make_bg_tile_pair(const Md2GbaVdp*v,unsigned plane,int32_t tx,int32_t ty,
                              uint8_t hi[64],uint8_t lo[64],uint32_t*dep_bloom,uint8_t*map_bloom){
    make_bg_tile_pair_ex(v,plane,tx,ty,hi,lo,dep_bloom,map_bloom,0);
}

/* P135: identical footprint to bg_cell_sources_dirty().  Returns 1 when every
   source name block and pattern of the cell is clean, and records its patterns
   in bg_source_mask exactly as make_bg_tile_pair_ex() would. */
static int bg_cell_reuse_sources(const Md2GbaVdp*v,unsigned plane,int32_t tx,int32_t ty){
    unsigned w=PS->plane_w,h=PS->plane_h;uint16_t base=PS->plane_base[plane];
    int32_t qx3=floordiv3(tx),qy5=floordiv5(ty),cxa=tx+qx3,cybase=ty-qy5*5,cya=qy5*7+(int32_t)proj_ybase_rem[(unsigned)cybase],cx,cy;
    int32_t cxb=cxa+1,cyb=cya+(cybase==2?2:1);unsigned t[6],k=0u,i;
    for(cy=cya;cy<=cyb;cy++)for(cx=cxa;cx<=cxb;cx++){
        unsigned mx=imod(cx,w),my=imod(cy,h),a=(unsigned)base+2u*(my*w+mx);uint16_t n=vrw(v,a);
        if(block_dirty(v,a)||tile_dirty(v,n&0x7ffu))return 0;
        t[k++]=n&0x7ffu;
    }
    for(i=0u;i<k;i++)PS->bg_source_mask[t[i]>>5]|=1u<<(t[i]&31u);
    return 1;
}
MD2GBA_PROJECTED_HOT static int bg_cell_sources_dirty(const Md2GbaVdp*v,unsigned plane,int32_t tx,int32_t ty){
#if defined(MD2GBA_PROJECTED_HOST_TEST)
    md2gba_projected_test_bg_source_checks++;
#endif
    unsigned w=PS->plane_w,h=PS->plane_h;uint16_t base=PS->plane_base[plane];
    int32_t qx3=floordiv3(tx),qy5=floordiv5(ty),cxa=tx+qx3,cybase=ty-qy5*5,cya=qy5*7+(int32_t)proj_ybase_rem[(unsigned)cybase],cx,cy;
    int32_t cxb=cxa+1,cyb=cya+(cybase==2?2:1);
    /* This is the exact source-cell footprint of the predecoded 3x5
       projection phases, including negative/wrapped camera coordinates.
       Avoid four inverse-pixel transforms for every dirty-cell probe. */
    for(cy=cya;cy<=cyb;cy++)for(cx=cxa;cx<=cxb;cx++){
        unsigned mx=imod(cx,w),my=imod(cy,h),a=(unsigned)base+2u*(my*w+mx);uint16_t n=vrw(v,a);
        if(block_dirty(v,a)||tile_dirty(v,n&0x7ffu))return 1;
    }
    return 0;
}

/* P138: exact visible-output equivalence probe for large dirty transactions.
   Source writes can be observationally neutral (write-then-revert, a name remap
   to an identical pattern, priority changes on transparent pixels).  The old
   >512-cell preflight treated every source-dependent cell as a required map
   publication and escalated directly to a bank rebuild.  Compare the projected
   pixels against the currently displayed slots first.  This performs no slot
   allocation/hash work and, on an exact match, refreshes the dependency bloom
   so future writes to newly referenced pattern IDs remain observable. */
static int bg_cell_output_matches_active(const Md2GbaVdp*v,unsigned plane,int32_t tx,int32_t ty){
    unsigned cell=(imod(ty,32u)<<5)|imod(tx,32u),w=PS->plane_w,h=PS->plane_h;
    unsigned hi_slot=PS->active_slot[plane][cell],lo_slot=PS->active_slot[plane+2u][cell];
    volatile const uint8_t *hi_v=(volatile const uint8_t*)(const void*)(BG_VRAM32+hi_slot*16u);
    volatile const uint8_t *lo_v=(volatile const uint8_t*)(const void*)(BG_VRAM32+lo_slot*16u);
    uint16_t base=PS->plane_base[plane],names[3][2];uint32_t bloom=0u;unsigned yd,xd,py,px;
    int32_t qx3=floordiv3(tx),qy5=floordiv5(ty);unsigned xphase=(unsigned)(tx-qx3*3),yphase=(unsigned)(ty-qy5*5);
    int32_t basecx=tx+qx3,basecy=qy5*7+(int32_t)proj_ybase_rem[yphase];
    if(plane==0u&&(PS->ui_suppress_mask[(cell>>5)&31u]&(1u<<(cell&31u))))return 1;
    for(yd=0u;yd<(yphase==2u?3u:2u);yd++)for(xd=0u;xd<2u;xd++){
        unsigned cx=imod(basecx+(int32_t)xd,w),cy=imod(basecy+(int32_t)yd,h),name_index=cy*w+cx;uint16_t n=vrw(v,(unsigned)base+2u*name_index);unsigned t=n&0x7ffu;
        names[yd][xd]=n;PS->bg_source_mask[t>>5]|=1u<<(t&31u);bloom|=dep_bloom_bit(t);
    }
    for(py=0u;py<8u;py++){
        unsigned yc=proj_ycell[yphase][py],base_ly=proj_ylocal[yphase][py],oi=py*8u;uint16_t nl=names[yc][0],nr=names[yc][1];
        unsigned lly=(nl&0x1000u)?7u-base_ly:base_ly,rly=(nr&0x1000u)?7u-base_ly:base_ly;
        const ProjAliasWord*lp=(const ProjAliasWord*)(const void*)(&v->vram[(nl&0x7ffu)*32u+lly*4u]);
        const ProjAliasWord*rp=(const ProjAliasWord*)(const void*)(&v->vram[(nr&0x7ffu)*32u+rly*4u]);uint32_t lr=*lp,rr=*rp;
        for(px=0u;px<8u;px++){
            unsigned side=proj_xcell[xphase][px],lx=proj_xlocal[xphase][px];uint16_t n=side?nr:nl;uint32_t row=side?rr:lr;uint8_t b,c,eh=0u,el=0u;
            if(n&0x0800u)lx=7u-lx;b=(uint8_t)(row>>((lx>>1u)*8u));c=(uint8_t)((lx&1u)?(b&15u):(b>>4));
            if(c){uint8_t q=(uint8_t)((((n>>13)&3u)*16u)+c);if(n&0x8000u)eh=q;else el=q;}
            if((hi_slot?hi_v[oi+px]:0u)!=eh||(lo_slot?lo_v[oi+px]:0u)!=el)return 0;
        }
    }
    PS->cell_dep_bloom[plane][cell]=bloom;return 1;
}

static void slot_hash_unlink(unsigned slot,uint32_t h){
    unsigned b=h&255u,cur=PS->slot_hash_head[b],prev=0u;while(cur){if(cur==slot){if(prev)PS->slot_hash_next[prev]=PS->slot_hash_next[cur];else PS->slot_hash_head[b]=PS->slot_hash_next[cur];PS->slot_hash_next[cur]=0u;return;}prev=cur;cur=PS->slot_hash_next[cur];}
}
static void slot_hash_link(unsigned slot,uint32_t h){unsigned b=h&255u;PS->slot_hash_next[slot]=PS->slot_hash_head[b];PS->slot_hash_head[b]=(uint16_t)slot;}
static unsigned find_or_alloc_slot(const uint8_t p[64]){
    uint32_t h,nz;unsigned i,free_slot=0u,hi=PS->slot_highwater,b;
    h=hash64_words(p,&nz);if(!nz)return 0u;b=h&255u;
    for(i=PS->slot_hash_head[b];i;i=PS->slot_hash_next[i])if(PS->slot_hash[i]==h&&tile_vram_equal(i,p))return i;
    if(hi+1u<PROJ_TILE_SLOTS)free_slot=++PS->slot_highwater;
    else if(hi){unsigned n=hi,cur=PS->free_scan_cursor;if(cur<1u||cur>hi)cur=1u;while(n--){if(!PS->slot_ref[cur]){free_slot=cur;break;}cur++;if(cur>hi)cur=1u;}PS->free_scan_cursor=(uint16_t)(cur<hi?cur+1u:1u);}
    if(!free_slot)return 0xffffu;
    if(PS->slot_hash[free_slot])slot_hash_unlink(free_slot,PS->slot_hash[free_slot]);
    tile_vram_write(free_slot,p);PS->slot_hash[free_slot]=h;slot_hash_link(free_slot,h);return free_slot;
}
MD2GBA_PROJECTED_HOT static unsigned find_or_alloc_slot_nonzero(const uint8_t p[64]){
    const ProjAliasWord*w=(const ProjAliasWord*)(const void*)p;uint32_t h=2166136261u;unsigned i,free_slot=0u,hi=PS->slot_highwater,b;
    for(i=0u;i<16u;i++){h^=w[i];h*=16777619u;}if(!h)h=1u;b=h&255u;
    for(i=PS->slot_hash_head[b];i;i=PS->slot_hash_next[i])if(PS->slot_hash[i]==h&&tile_vram_equal(i,p))return i;
    if(hi+1u<PROJ_TILE_SLOTS)free_slot=++PS->slot_highwater;
    else if(hi){unsigned n=hi,cur=PS->free_scan_cursor;if(cur<1u||cur>hi)cur=1u;while(n--){if(!PS->slot_ref[cur]){free_slot=cur;break;}cur++;if(cur>hi)cur=1u;}PS->free_scan_cursor=(uint16_t)(cur<hi?cur+1u:1u);}
    if(!free_slot)return 0xffffu;if(PS->slot_hash[free_slot])slot_hash_unlink(free_slot,PS->slot_hash[free_slot]);
    tile_vram_write(free_slot,p);PS->slot_hash[free_slot]=h;slot_hash_link(free_slot,h);return free_slot;
}


static void ui_native_tile(const Md2GbaVdp*v,uint16_t n,uint8_t out[64]){
    unsigned y,x,t=n&0x7ffu,pal=(n>>13)&3u;for(y=0u;y<8u;y++)for(x=0u;x<8u;x++){unsigned sx=(n&0x0800u)?7u-x:x,sy=(n&0x1000u)?7u-y:y;uint8_t c=src_px(v,t,sx,sy);out[y*8u+x]=c?(uint8_t)(pal*16u+c):0u;}
}
static unsigned ui_panel_out_w(const ProjUiPanel*p){return p->w>30u?30u:(unsigned)p->w;}
static unsigned ui_source_x(unsigned source_w,unsigned out_w,unsigned gx){
    unsigned sx;
    if(gx<8u)return gx;
    if(gx>=(out_w-1u)*8u)return (source_w-1u)*8u+gx-(out_w-1u)*8u;
    sx=8u+udiv224_exact((gx-8u)*((source_w-2u)*8u)+112u);
    return sx>=(source_w-1u)*8u?(source_w-1u)*8u-1u:sx;
}
/* Wide Genesis dialogue is 34 tiles / 272 px. Do not run it through the world
   3/4 x 5/7 sampler: that is what made the 8-pixel font lose vertical strokes.
   Preserve the complete 8-pixel height and both border tiles, and fit only the
   interior horizontally to 28 tiles (the established P83 34->30 contract). */
static void ui_panel_tile(const Md2GbaVdp*v,const ProjUiPanel*p,unsigned ox,unsigned oy,uint8_t out[64]){
    unsigned outw=ui_panel_out_w(p),py,px,last_cell=~0u;uint16_t names[8],last_name=0u;uint8_t local_x[8];
    if(p->w<=30u){uint16_t n=ui_name_word(v,PS->plane_base[0],PS->plane_w,PS->plane_h,(int32_t)p->sx+(int32_t)ox,(int32_t)p->sy+(int32_t)oy);ui_native_tile(v,n,out);return;}
    /* A wide output tile has only eight horizontal source samples. Resolve
       their source names once for all eight scanlines, retaining the previous
       name while adjacent samples remain in the same source tile. */
    for(px=0u;px<8u;px++){
        unsigned sx=ui_source_x(p->w,outw,ox*8u+px),cell=sx>>3;
        if(cell!=last_cell){last_name=ui_name_word(v,PS->plane_base[0],PS->plane_w,PS->plane_h,
                                                  (int32_t)p->sx+(int32_t)cell,(int32_t)p->sy+(int32_t)oy);last_cell=cell;}
        names[px]=last_name;local_x[px]=(uint8_t)(sx&7u);
    }
    for(py=0u;py<8u;py++)for(px=0u;px<8u;px++){
        uint16_t n=names[px];unsigned lx=local_x[px];uint8_t c;
        if(n&0x0800u)lx=7u-lx;{unsigned ly=(n&0x1000u)?7u-py:py;c=src_px(v,n&0x7ffu,lx,ly);}
        out[py*8u+px]=c?(uint8_t)((((n>>13)&3u)*16u)+c):0u;
    }
}
static void ui_masks_clear(void){unsigned i;for(i=0u;i<32u;i++){PS->ui_target_mask[i]=0u;PS->ui_suppress_mask[i]=0u;}}
static void ui_mark_rect(uint32_t mask[32],int32_t x,int32_t y,unsigned w,unsigned h){unsigned yy,xx;for(yy=0u;yy<h;yy++){unsigned ry=imod(y+(int32_t)yy,32u);for(xx=0u;xx<w;xx++)mask[ry]|=1u<<imod(x+(int32_t)xx,32u);}}
static int ui_mask_has(const uint32_t mask[32],unsigned cell){return !!(mask[(cell>>5)&31u]&(1u<<(cell&31u)));}
#ifndef MD2GBA_PROJECTED_REBUILD_REUSE
#define MD2GBA_PROJECTED_REBUILD_REUSE 1
#endif
/* P135: Plane-A ring cells whose *displayed* hi/lo entries are a native UI
   tile or a UI-suppressed blank rather than the fitted world tile.  Unlike
   ui_target_mask/ui_suppress_mask (which an activation attempt may rewrite
   before failing), this is snapshotted only when a complete presentation
   state is established.  Kept outside the 32,768-byte projected arena. */
static uint32_t proj_ui_shown[32];static uint8_t proj_ui_shown_valid;
static void ui_shown_sync(void){unsigned i;for(i=0u;i<32u;i++)proj_ui_shown[i]=PS->ui_target_mask[i]|PS->ui_suppress_mask[i];proj_ui_shown_valid=1u;}
static void ui_build_masks(const ProjUiPanel*p,unsigned n){
    unsigned i;ui_masks_clear();for(i=0u;i<n;i++){
        int32_t px0=qx((int32_t)p[i].sx*8),py0=qy((int32_t)p[i].sy*8),px1=qx(((int32_t)p[i].sx+(int32_t)p[i].w)*8-1),py1=qy(((int32_t)p[i].sy+(int32_t)p[i].h)*8-1);
        int32_t tx0=floorpow2(px0,3u),ty0=floorpow2(py0,3u),tx1=floorpow2(px1,3u),ty1=floorpow2(py1,3u);
        ui_mark_rect(PS->ui_suppress_mask,tx0,ty0,(unsigned)(tx1-tx0+1),(unsigned)(ty1-ty0+1));
        ui_mark_rect(PS->ui_target_mask,p[i].dx,p[i].dy,ui_panel_out_w(&p[i]),p[i].h);
    }
}
static uint32_t ui_build_pattern_bloom(const Md2GbaVdp*v,const ProjUiPanel*p,unsigned n);
static void ui_capture_source_view(const Md2GbaVdp*v){
    int32_t cx,cy;get_camera(v,0u,&cx,&cy);
    PS->ui_source_view_x=(int16_t)floorpow2(cx,3u);
    PS->ui_source_view_y=(int16_t)floorpow2(cy,3u);
}
static int ui_apply_direct(const Md2GbaVdp*v){
    ProjUiPanel p[PROJ_UI_MAX_PANELS];uint32_t sig;unsigned n=scan_fixed_ui_panels(v,p,&sig),i,x,y;uint16_t base=PS->plane_base[0];ui_build_masks(p,n);PS->ui_count=(uint8_t)n;PS->ui_signature=sig;PS->ui_pattern_bloom=ui_build_pattern_bloom(v,p,n);ui_capture_source_view(v);{unsigned k;for(k=0u;k<n;k++)ui_panel_copy(&PS->ui_panel[k],&p[k]);}
    /* Remove each scaled world copy before drawing the native overlay. Masks are
       map-space so this remains correct with non-zero field scroll. */
    for(y=0u;y<32u;y++)for(x=0u;x<32u;x++){unsigned cell=y*32u+x,old;if(!ui_mask_has(PS->ui_suppress_mask,cell))continue;
        old=PS->active_slot[0][cell];if(old&&PS->slot_ref[old])PS->slot_ref[old]--;PS->active_slot[0][cell]=0u;BG_VRAM16[(PROJ_SB_FIRST+PS->map_bank*4u)*1024u+cell]=0u;
        old=PS->active_slot[2][cell];if(old&&PS->slot_ref[old])PS->slot_ref[old]--;PS->active_slot[2][cell]=0u;BG_VRAM16[(PROJ_SB_FIRST+PS->map_bank*4u+2u)*1024u+cell]=0u;}
    for(i=0u;i<n;i++)for(y=0u;y<p[i].h;y++)for(x=0u;x<ui_panel_out_w(&p[i]);x++){
        unsigned cell=(imod((int32_t)p[i].dy+(int32_t)y,32u)<<5)|imod((int32_t)p[i].dx+(int32_t)x,32u),slot,old;uint8_t pix[64] __attribute__((aligned(4)));ui_panel_tile(v,&p[i],x,y,pix);slot=find_or_alloc_slot(pix);if(slot==0xffffu)return -1;old=PS->active_slot[0][cell];if(old&&PS->slot_ref[old])PS->slot_ref[old]--;PS->active_slot[0][cell]=(uint16_t)slot;if(slot)PS->slot_ref[slot]++;BG_VRAM16[(PROJ_SB_FIRST+PS->map_bank*4u)*1024u+cell]=(uint16_t)slot;
    }
    return 0;
}
/* Same UI transform while constructing an inactive full-map bank. The old
   active_slot/ref ownership remains untouched; this helper edits only the new
   bank and its temporary references so the previous complete frame can remain
   visible throughout a map/load transition. */
static int ui_apply_build_bank(const Md2GbaVdp*v,unsigned sb){
    ProjUiPanel p[PROJ_UI_MAX_PANELS];uint32_t sig;unsigned n=scan_fixed_ui_panels(v,p,&sig),i,x,y;ui_build_masks(p,n);PS->ui_count=(uint8_t)n;PS->ui_signature=sig;PS->ui_pattern_bloom=ui_build_pattern_bloom(v,p,n);ui_capture_source_view(v);{unsigned k;for(k=0u;k<n;k++)ui_panel_copy(&PS->ui_panel[k],&p[k]);}
    for(y=0u;y<32u;y++)for(x=0u;x<32u;x++){unsigned cell=y*32u+x,old;if(!ui_mask_has(PS->ui_suppress_mask,cell))continue;
        old=BG_VRAM16[sb*1024u+cell]&0x03ffu;if(old&&PS->slot_ref[old])PS->slot_ref[old]--;BG_VRAM16[sb*1024u+cell]=0u;
        old=BG_VRAM16[(sb+2u)*1024u+cell]&0x03ffu;if(old&&PS->slot_ref[old])PS->slot_ref[old]--;BG_VRAM16[(sb+2u)*1024u+cell]=0u;}
    for(i=0u;i<n;i++)for(y=0u;y<p[i].h;y++)for(x=0u;x<ui_panel_out_w(&p[i]);x++){
        unsigned cell=(imod((int32_t)p[i].dy+(int32_t)y,32u)<<5)|imod((int32_t)p[i].dx+(int32_t)x,32u),slot,old;uint8_t pix[64] __attribute__((aligned(4)));ui_panel_tile(v,&p[i],x,y,pix);slot=find_or_alloc_slot(pix);if(slot==0xffffu)return -1;old=BG_VRAM16[sb*1024u+cell]&0x03ffu;if(old&&PS->slot_ref[old])PS->slot_ref[old]--;if(slot)PS->slot_ref[slot]++;BG_VRAM16[sb*1024u+cell]=(uint16_t)slot;
    }
    return 0;
}

static void queue_rollback(void){unsigned i;for(i=0u;i<PS->map_write_count;i++){unsigned s=PS->map_write[i].new_slot;if(s&&s<PROJ_TILE_SLOTS&&PS->slot_ref[s])PS->slot_ref[s]--;}PS->map_write_count=0u;}
static void queue_reset(void){
    unsigned i;PS->map_write_count=0u;PS->palette_valid=PS->scroll_valid=PS->sprite_queue_valid=0u;
    PS->queue_stamp_gen=(uint8_t)((PS->queue_stamp_gen+1u)&15u);
    if(!PS->queue_stamp_gen){for(i=0u;i<64u;i++)PS->queue_word_stamp[i]=0u;PS->queue_stamp_gen=1u;
#if defined(MD2GBA_PROJECTED_HOST_TEST)
        md2gba_projected_test_queue_mask_clears++;
#endif
    }
}
static void queue_mask_touch(unsigned layer,unsigned word){
    unsigned idx=layer*32u+word,bi=idx>>1u,sh=(idx&1u)*4u;uint8_t b=PS->queue_word_stamp[bi],g=(uint8_t)((b>>sh)&15u);
    if(g!=PS->queue_stamp_gen){PS->queue_cell_mask[layer][word]=0u;b=(uint8_t)((b&~(15u<<sh))|(PS->queue_stamp_gen<<sh));PS->queue_word_stamp[bi]=b;}
}

static int queue_slot_change(unsigned layer,int32_t tx,int32_t ty,unsigned newslot){
    unsigned cell=(imod(ty,32u)<<5)|imod(tx,32u),bit=cell&31u,word=cell>>5,old;int stale;ProjMapWrite*q;
    queue_mask_touch(layer,word);
    if(PS->queue_cell_mask[layer][word]&(1u<<bit)){
        unsigned i,map=(PROJ_SB_FIRST+PS->map_bank*4u+layer)*1024u+cell;
        for(i=0u;i<PS->map_write_count;i++)if(PS->map_write[i].map_index==map){
            q=&PS->map_write[i];if(q->new_slot!=newslot){
                if(q->new_slot&&PS->slot_ref[q->new_slot])PS->slot_ref[q->new_slot]--;
                if(newslot)PS->slot_ref[newslot]++;
                q->new_slot=(uint16_t)newslot;
            }
            return 0;
        }
        return -3;
    }
    old=PS->active_slot[layer][cell];stale=!!(PS->stale_cell_mask[layer][word]&(1u<<bit));if(newslot==old&&!stale)return 0;
    if(PS->map_write_count>=PROJ_MAP_WRITES)return -3;
    if(newslot)PS->slot_ref[newslot]++;
    q=&PS->map_write[PS->map_write_count++];q->map_index=(uint16_t)((PROJ_SB_FIRST+PS->map_bank*4u+layer)*1024u+cell);q->new_slot=(uint16_t)newslot;q->old_slot=(uint16_t)old;
    PS->queue_cell_mask[layer][word]|=1u<<bit;return 0;
}
MD2GBA_PROJECTED_HOT static int queue_cell_pair(const Md2GbaVdp*v,unsigned plane,int32_t tx,int32_t ty){
    unsigned hs,ls,cell=(imod(ty,32u)<<5)|imod(tx,32u);uint8_t hi[64] __attribute__((aligned(4))),lo[64] __attribute__((aligned(4))),nz;uint32_t bloom;int r;
    if(plane==0u&&ui_mask_has(PS->ui_suppress_mask,cell)){
        /* A-high native overlay owns target cells; the projected source A
           layers underneath it need no sampling, hashing or tile upload. */
        if(!ui_mask_has(PS->ui_target_mask,cell)){r=queue_slot_change(0u,tx,ty,0u);if(r)return r;}
        r=queue_slot_change(2u,tx,ty,0u);
        if(!r){PS->cell_dep_bloom[0][cell]=0u;}
        return r;
    }
    make_bg_tile_pair_ex(v,plane,tx,ty,hi,lo,&bloom,0,&nz);
    hs=(nz&1u)?find_or_alloc_slot_nonzero(hi):0u;if(hs==0xffffu)return -2;
    /* P134V: reserve the first output before allocating the second.  At slot
       high-water pressure an unreferenced freshly allocated high tile was
       otherwise immediately eligible for reuse by the low tile.  These are
       temporary preparation references; queue_slot_change() takes the actual
       queued ownership below. */
    if(hs)PS->slot_ref[hs]++;
    ls=(nz&2u)?find_or_alloc_slot_nonzero(lo):0u;
    if(ls==0xffffu){if(hs&&PS->slot_ref[hs])PS->slot_ref[hs]--;return -2;}
    if(ls)PS->slot_ref[ls]++;
    if(plane==0u&&ui_mask_has(PS->ui_target_mask,cell)){/* native UI overlay owns A-high here */}
    else if(plane==0u&&ui_mask_has(PS->ui_suppress_mask,cell)){r=queue_slot_change(plane,tx,ty,0u);if(r){if(hs&&PS->slot_ref[hs])PS->slot_ref[hs]--;if(ls&&PS->slot_ref[ls])PS->slot_ref[ls]--;return r;}}
    else {r=queue_slot_change(plane,tx,ty,hs);if(r){if(hs&&PS->slot_ref[hs])PS->slot_ref[hs]--;if(ls&&PS->slot_ref[ls])PS->slot_ref[ls]--;return r;}}
    if(plane==0u&&ui_mask_has(PS->ui_suppress_mask,cell))r=queue_slot_change(plane+2u,tx,ty,0u);
    else r=queue_slot_change(plane+2u,tx,ty,ls);
    /* Drop the temporary pair reservations regardless of queue result.  Any
       successful queue_slot_change() already owns its own reference and the
       caller's queue rollback handles those references on failure. */
    if(hs&&PS->slot_ref[hs])PS->slot_ref[hs]--;
    if(ls&&PS->slot_ref[ls])PS->slot_ref[ls]--;
    if(r)return r;PS->cell_dep_bloom[plane][cell]=bloom;return 0;
}
static int queue_clear_pair(unsigned plane,int32_t tx,int32_t ty){
    unsigned cell=(imod(ty,32u)<<5)|imod(tx,32u);int r=queue_slot_change(plane,tx,ty,0u);if(r)return r;r=queue_slot_change(plane+2u,tx,ty,0u);if(!r){PS->cell_dep_bloom[plane][cell]=0u;}return r;
}
/* Camera-edge retirement is bookkeeping only. The cell is outside the 31x21
   coverage as soon as the projected origin advances, so erasing its map word at
   VBlank is wasted work. Mark it stale instead; queue_slot_change() guarantees
   an exact overwrite before that ring cell can become visible again. */
static void retire_offscreen_pair(unsigned plane,int32_t tx,int32_t ty){
    unsigned cell=(imod(ty,32u)<<5)|imod(tx,32u),word=cell>>5,bit=cell&31u;
    /* P134V: the old hardware map remains displayed until VBlank.  Keep its
       tile references pinned during preparation so an entering tile cannot
       overwrite a slot still visible in the current frame.  The VBlank commit
       releases active_slot ownership for stale cells after queued replacements
       have been published.  A failed preparation therefore also needs no
       reference-count rollback for retired edges. */
    PS->stale_cell_mask[plane][word]|=1u<<bit;
    PS->stale_cell_mask[plane+2u][word]|=1u<<bit;
    PS->cell_dep_bloom[plane][cell]=0u;
}

/* Activate a completed fixed UI without reprojecting the whole world.  The last
   clean world remains resident while the panel was being authored; once the
   source frame is complete we queue only the covered A-high/A-low cells and the
   final native/fitted UI tiles, then expose them together at VBlank. */
static int ui_queue_activate(const Md2GbaVdp*v,const ProjUiPanel p[PROJ_UI_MAX_PANELS],unsigned n,uint32_t sig){
    unsigned i,x,y;int r;
    if(!n)return 1;ui_build_masks(p,n);PS->ui_count=(uint8_t)n;PS->ui_signature=sig;PS->ui_pattern_bloom=ui_build_pattern_bloom(v,p,n);ui_capture_source_view(v);for(i=0u;i<n;i++)ui_panel_copy(&PS->ui_panel[i],&p[i]);
    for(y=0u;y<32u;y++)for(x=0u;x<32u;x++)if(ui_mask_has(PS->ui_suppress_mask,y*32u+x)){
        if(!ui_mask_has(PS->ui_target_mask,y*32u+x)){r=queue_slot_change(0u,(int32_t)x,(int32_t)y,0u);if(r)return r;}
        r=queue_slot_change(2u,(int32_t)x,(int32_t)y,0u);if(r)return r;
    }
    for(i=0u;i<n;i++)for(y=0u;y<p[i].h;y++)for(x=0u;x<ui_panel_out_w(&p[i]);x++){
        unsigned slot;uint8_t pix[64] __attribute__((aligned(4)));ui_panel_tile(v,&p[i],x,y,pix);slot=find_or_alloc_slot(pix);if(slot==0xffffu)return 1;
        r=queue_slot_change(0u,(int32_t)p[i].dx+(int32_t)x,(int32_t)p[i].dy+(int32_t)y,slot);if(r)return r;
    }
    return 0;
}

static uint32_t ui_build_pattern_bloom(const Md2GbaVdp*v,const ProjUiPanel*p,unsigned n){
    unsigned i,x,y;uint32_t b=0u;for(i=0u;i<n;i++)for(y=0u;y<p[i].h;y++)for(x=0u;x<p[i].w;x++){uint16_t name=ui_name_word(v,PS->plane_base[0],PS->plane_w,PS->plane_h,(int32_t)p[i].sx+(int32_t)x,(int32_t)p[i].sy+(int32_t)y);b|=dep_bloom_bit(name&0x7ffu);}return b;
}
static int ui_patterns_dirty(const Md2GbaVdp*v,uint32_t dirty_bloom){
    unsigned i,x,y;if(!(dirty_bloom&PS->ui_pattern_bloom))return 0;
    for(i=0u;i<PS->ui_count;i++){const ProjUiPanel*p=&PS->ui_panel[i];for(y=0u;y<p->h;y++)for(x=0u;x<p->w;x++){uint16_t name=ui_name_word(v,PS->plane_base[0],PS->plane_w,PS->plane_h,(int32_t)p->sx+(int32_t)x,(int32_t)p->sy+(int32_t)y);if(tile_dirty(v,name&0x7ffu))return 1;}}
    return 0;
}
static int ui_native_source_dirty(const Md2GbaVdp*v,const ProjUiPanel*p,unsigned x,unsigned y){
    unsigned mx=imod((int32_t)p->sx+(int32_t)x,PS->plane_w),my=imod((int32_t)p->sy+(int32_t)y,PS->plane_h);
    unsigned a=(unsigned)PS->plane_base[0]+2u*(my*(unsigned)PS->plane_w+mx);uint16_t name=vrw(v,a);
    return block_dirty(v,a)||tile_dirty(v,name&0x7ffu);
}
static int ui_wide_output_dirty(const Md2GbaVdp*v,const ProjUiPanel*p,unsigned ox,unsigned y){
    unsigned outw=ui_panel_out_w(p),gx0=ox*8u;
    unsigned s0=ui_source_x(p->w,outw,gx0),s1=ui_source_x(p->w,outw,gx0+7u),tx;
    for(tx=s0>>3;tx<=s1>>3;tx++){unsigned mx=imod((int32_t)p->sx+(int32_t)tx,PS->plane_w),my=imod((int32_t)p->sy+(int32_t)y,PS->plane_h),a=(unsigned)PS->plane_base[0]+2u*(my*(unsigned)PS->plane_w+mx);uint16_t name=vrw(v,a);if(block_dirty(v,a)||tile_dirty(v,name&0x7ffu))return 1;}
    return 0;
}
static int ui_queue_refresh(const Md2GbaVdp*v){
    unsigned i,x,y;int r;
    /* Geometry is already proven and cached.  Native <=30-tile UI is updated
       only where the source name word or source pattern is dirty.  P134M rebuilt
       every tile of every menu/status box on any UI write, which was especially
       expensive while the cursor/text was being assembled.  Wide dialogue is
       horizontally resampled and may depend on multiple source cells, so retain
       the conservative full-panel refresh there. */
    for(i=0u;i<PS->ui_count;i++){
        const ProjUiPanel*p=&PS->ui_panel[i];unsigned ow=ui_panel_out_w(p);
        for(y=0u;y<p->h;y++)for(x=0u;x<ow;x++){
            unsigned slot;uint8_t pix[64] __attribute__((aligned(4)));
            if(p->w<=30u){if(!ui_native_source_dirty(v,p,x,y))continue;}else if(!ui_wide_output_dirty(v,p,x,y))continue;
            ui_panel_tile(v,p,x,y,pix);slot=find_or_alloc_slot(pix);if(slot==0xffffu)return 1;
            r=queue_slot_change(0u,(int32_t)p->dx+(int32_t)x,(int32_t)p->dy+(int32_t)y,slot);if(r)return 1;
        }
    }
    return 0;
}

static void set_scroll_payload(const Md2GbaVdp*v,uint16_t s[8],int32_t*oax,int32_t*oay,int32_t*obx,int32_t*oby){
    int32_t cax,cay,cbx,cby,qa,qv,qb,qw;get_camera(v,0u,&cax,&cay);get_camera(v,1u,&cbx,&cby);
    qa=qx(cax);qv=qy(cay);qb=qx(cbx);qw=qy(cby);
    s[0]=s[4]=(uint16_t)qa;s[1]=s[5]=(uint16_t)qv;s[2]=s[6]=(uint16_t)qb;s[3]=s[7]=(uint16_t)qw;
    if(oax)*oax=floorpow2(qa,3u);if(oay)*oay=floorpow2(qv,3u);if(obx)*obx=floorpow2(qb,3u);if(oby)*oby=floorpow2(qw,3u);
}
static void write_scroll_now(const uint16_t*s){REG_BG0HOFS=s[0];REG_BG0VOFS=s[1];REG_BG1HOFS=s[2];REG_BG1VOFS=s[3];REG_BG2HOFS=s[4];REG_BG2VOFS=s[5];REG_BG3HOFS=s[6];REG_BG3VOFS=s[7];}

static void copy_palette_snapshot(const Md2GbaVdp*v,uint16_t out[64]){unsigned i;for(i=0u;i<64u;i++)out[i]=v->cram_gba[i];out[0]=v->cram_gba[v->regs[7]&0x3fu];}
static void dma16(volatile uint16_t*dst,const uint16_t*src,unsigned n);
static void apply_palette(const uint16_t*p){dma16(BG_PAL,p,64u);dma16(OBJ_PAL,p,64u);}

static void dma16(volatile uint16_t*dst,const uint16_t*src,unsigned n){
#if defined(MD2GBA_PROJECTED_HOST_TEST)
    unsigned i;for(i=0u;i<n;i++)dst[i]=src[i];
#else
    if(!n)return;REG_DMA3CNT=0u;REG_DMA3SAD=(uint32_t)(uintptr_t)src;REG_DMA3DAD=(uint32_t)(uintptr_t)dst;MD2GBA_COMPILER_BARRIER();REG_DMA3CNT=(n&0xffffu)|(1u<<31);while(REG_DMA3CNT&(1u<<31)){}
#endif
}

static void obj_shape(unsigned wt,unsigned ht,unsigned*shape,unsigned*size){
    if(wt==ht){*shape=0u;*size=wt==1u?0u:wt==2u?1u:2u;}
    else if(wt>ht){*shape=1u;*size=(wt==2u&&ht==1u)?0u:(wt==4u&&ht==1u)?1u:2u;}
    else {*shape=2u;*size=(wt==1u&&ht==2u)?0u:(wt==1u&&ht==4u)?1u:2u;}
}
static void obj_container(unsigned dw,unsigned dh,unsigned*wt,unsigned*ht){
    if(dw<=8u&&dh<=8u){*wt=*ht=1u;return;}if(dw<=16u&&dh<=8u){*wt=2u;*ht=1u;return;}if(dw<=8u&&dh<=16u){*wt=1u;*ht=2u;return;}
    if(dw<=16u&&dh<=16u){*wt=*ht=2u;return;}if(dw<=32u&&dh<=8u){*wt=4u;*ht=1u;return;}if(dw<=8u&&dh<=32u){*wt=1u;*ht=4u;return;}
    if(dw<=32u&&dh<=16u){*wt=4u;*ht=2u;return;}if(dw<=16u&&dh<=32u){*wt=2u;*ht=4u;return;}*wt=*ht=4u;
}
static int obj_visible(int x,int y,unsigned w,unsigned h){return x<240&&y<160&&x+(int)w>0&&y+(int)h>0;}
static uint32_t keymix(uint32_t h,uint32_t x){h^=x;h*=16777619u;return h;}

/* P134I quality-first OBJ policy.  The world/background remains projected
   320x224 -> 240x160, but Genesis sprite artwork is kept at native source
   pixel size.  P134F-H scaled a common 16x24 actor to roughly 12x17, which
   made faces/eyes and silhouettes too small to read on the GBA display.

   Position is still derived from the projected guest coordinate.  Each native
   sprite piece is centered horizontally on the projected piece and bottom-
   anchored vertically, so actor feet continue to meet the projected world.
   This is target presentation only: SAT, collision, camera and guest state are
   untouched.  Importantly, the required GBA OBJ containers do not grow for
   the common 8/16/24/32-pixel Genesis piece sizes: e.g. both 12x17 and 16x24
   occupy the same 16x32 container. */
#define MD2GBA_PROJECTED_NATIVE_OBJ_QUALITY 1
static const uint8_t obj_xcount[4]={8u,16u,24u,32u};
/* P134J visual tuning: keep source width but compress height only to 7/8.
   User runtime showed full-native 16x24 actors were slightly too tall, while
   the old ~12x17 projection was too small. This gives 16x21: same readable
   width, intermediate height, fixed across all source Y phases. */
static const uint8_t obj_ycount_phase[4][7]={
 {7,7,7,7,7,7,7},
 {14,14,14,14,14,14,14},
 {21,21,21,21,21,21,21},
 {28,28,28,28,28,28,28}
};
static const uint8_t obj_ycount_min[4]={7u,14u,21u,28u};
/* Endpoint-preserving nearest maps for 8/16/24/32 -> 7/14/21/28. */
static const uint8_t obj_ymap_small[4][28]={
 {0,1,2,4,5,6,7},
 {0,1,2,3,5,6,7,8,9,10,12,13,14,15},
 {0,1,2,3,5,6,7,8,9,10,12,13,14,15,16,17,18,20,21,22,23},
 {0,1,2,3,5,6,7,8,9,10,11,13,14,15,16,17,18,20,21,22,23,24,25,26,28,29,30,31}
};
static const uint8_t obj_ymap_large[4][28]={
 {0,1,2,4,5,6,7},
 {0,1,2,3,5,6,7,8,9,10,12,13,14,15},
 {0,1,2,3,5,6,7,8,9,10,12,13,14,15,16,17,18,20,21,22,23},
 {0,1,2,3,5,6,7,8,9,10,11,13,14,15,16,17,18,20,21,22,23,24,25,26,28,29,30,31}
};

static int32_t floor_mod7(int32_t x){return x-floordiv7(x)*7;}
static const uint8_t obj_ymap_native[32]={0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31};
/* Small UI cursors/markers are Genesis screen-space sprites. Once a Plane-A
   window is detached and moved/resized independently of the world projection,
   those sprites must follow the same native panel coordinates or their error
   grows down the menu. Field actors are taller than 16 px, so they remain on
   the normal world projection path even when standing behind a window. */
static int ui_sprite_native_position(int sx,int sy,unsigned sw,unsigned sh,int32_t cax,int32_t cay,int*px,int*py){
    unsigned i;if(sw>16u||sh>16u||!PS->ui_count)return 0;
    for(i=0u;i<PS->ui_count;i++){const ProjUiPanel*p=&PS->ui_panel[i];int srcx=(int)p->sx*8-(int)cax,srcy=(int)p->sy*8-(int)cay;int cx=sx+(int)(sw>>1),cy=sy+(int)(sh>>1);
        if(cx>=srcx&&cx<srcx+(int)p->w*8&&cy>=srcy&&cy<srcy+(int)p->h*8){*px=(int)p->ox+(sx-srcx);*py=(int)p->oy+(sy-srcy);return 1;}
    }
    return 0;
}
static uint32_t sprite_desc(unsigned base,unsigned fw,unsigned fh,unsigned hf,unsigned vf,unsigned yvariant){
    /* Pixel content has only two vertical variants per source height (small or
       large). Encoding all seven absolute Y phases made identical pixel data
       look stale six times out of seven and forced needless OBJ reconversion. */
    return 0x80000000u|(base&0x7ffu)|((fw-1u)<<11)|((fh-1u)<<13)|(hf<<15)|(vf<<16)|((yvariant&3u)<<17);
}
static int sprite_tiles_dirty_now(const Md2GbaVdp*v,unsigned base,unsigned fw,unsigned fh){unsigned i;for(i=0u;i<fw*fh;i++)if(tile_dirty(v,(base+i)&0x7ffu))return 1;return 0;}

static void build_obj_pixels(const Md2GbaVdp*v,unsigned bank_base,unsigned dst,unsigned base,unsigned fw,unsigned fh,unsigned hf,unsigned vf,unsigned dw,unsigned dh,unsigned wt,unsigned ht,const uint8_t*ymap){
#if defined(MD2GBA_PROJECTED_HOST_TEST)
    md2gba_projected_test_obj_builds++;
#endif
    /* Assemble directly in GBA 4bpp 1D-tile word order. P134F/G first wrote a
       32x32 byte-per-pixel scratch image and then repacked every container pixel,
       including transparent padding. Direct nibble placement removes that second
       full-container pass and halves the worst-case stack scratch. */
    uint32_t words[16u*8u];unsigned py,px,i,need=wt*ht;
    for(i=0u;i<need*8u;i++)words[i]=0u;
    for(py=0u;py<dh;py++)for(px=0u;px<dw;px++){
        unsigned sx=px,sy=ymap[py],tx,ty,tile,c,dt,row,shift;
        if(hf)sx=fw*8u-1u-sx;if(vf)sy=fh*8u-1u-sy;tx=sx>>3;ty=sy>>3;tile=base+tx*fh+ty;c=src_px(v,tile,sx&7u,sy&7u);
        if(!c)continue;dt=(py>>3)*wt+(px>>3);row=py&7u;shift=(px&7u)*4u;words[dt*8u+row]|=(uint32_t)(c&15u)<<shift;
    }
    {volatile uint32_t*d=OBJ_VRAM32+(bank_base+dst)*8u;for(i=0u;i<need*8u;i++)d[i]=words[i];}
}

static void quality_obj_position(int sx,int sy,int32_t cax,int32_t cay,unsigned sw,unsigned sh,unsigned dw,unsigned dh,int*px,int*py){
    int pl=qx((int32_t)sx+cax)-qx(cax),pt=qy((int32_t)sy+cay)-qy(cay);
    int pr=qx((int32_t)sx+(int32_t)sw+cax)-qx(cax);
    int pb=qy((int32_t)sy+(int32_t)sh+cay)-qy(cay);
    int pw=pr-pl;
    *px=pl-((int)dw-pw)/2;
    *py=pb-(int)dh;
    (void)pt;
}

/* Build the complete projected OAM image. Pixel banks now carry an exact
   per-SAT-entry source descriptor. An actor animation updates only entries whose
   source pixels/geometry are stale in the target bank; SAT-only motion remains
   an OAM update with zero pixel conversion. */
static int stage_sprites(const Md2GbaVdp*v,int direct){
    unsigned pass,entry,guard,base_sat=md2gba_vdp_sat_base(v),obj=0u,tiles=0u,bank=PS->active_obj_bank;int any_pixels=!PS->sprite_valid;
    /* SAT position/link traffic is common while walking; source pattern changes
       are not. Check the aggregate old sprite dependency mask once before pass 0
       clears/rebuilds it, then inspect per-actor tiles only when that aggregate
       test says some resident sprite pattern actually changed. */
    int pattern_dirty_any=PS->sprite_valid&&mask_dirty(v,PS->sprite_source_mask);
    int32_t cax,cay;get_camera(v,0u,&cax,&cay);
    for(pass=0u;pass<2u;pass++){
        entry=guard=obj=tiles=0u;
        if(pass==0u){unsigned mi;for(mi=0u;mi<64u;mi++)PS->sprite_source_mask[mi]=0u;for(mi=0u;mi<80u;mi++)PS->sprite_oam_index[mi]=0xffu;}
        else bank=any_pixels?(PS->sprite_valid?(PS->active_obj_bank^1u):0u):PS->active_obj_bank;
        while(guard++<80u){
            unsigned a=(base_sat+entry*8u)&0xffffu;uint16_t yw=vrw(v,a),attr=vrw(v,a+4u),xw=vrw(v,a+6u);unsigned sz=v->vram[(uint16_t)(a+2u)],link=v->vram[(uint16_t)(a+3u)]&0x7fu;
            unsigned fw=((sz>>2)&3u)+1u,fh=(sz&3u)+1u,bt=attr&0x7ffu,hf=!!(attr&0x0800u),vf=!!(attr&0x1000u),pal=(attr>>13)&3u,hi=!!(attr&0x8000u);
            unsigned dw=obj_xcount[fw-1u],phase=(unsigned)floor_mod7((int32_t)((int)(yw&0x3ffu)-128)+cay),dh=obj_ycount_phase[fh-1u][phase],wt,ht,need,shape,size,i;int sx=(int)(xw&0x1ffu)-128,sy=(int)(yw&0x3ffu)-128;
            if(pass==0u){PS->sprite_sat_meta[entry][0]=vrw(v,a+2u);PS->sprite_sat_meta[entry][1]=attr;}
            int px,py,ui_native=ui_sprite_native_position(sx,sy,fw*8u,fh*8u,cax,cay,&px,&py);unsigned yvariant;const uint8_t*ymap;uint32_t desc;int srcdirty=pattern_dirty_any&&sprite_tiles_dirty_now(v,bt,fw,fh);
            if(ui_native){dw=fw*8u;dh=fh*8u;yvariant=2u;ymap=obj_ymap_native;}
            else {yvariant=(dh!=obj_ycount_min[fh-1u]);ymap=yvariant?obj_ymap_large[fh-1u]:obj_ymap_small[fh-1u];quality_obj_position(sx,sy,cax,cay,fw*8u,fh*8u,dw,dh,&px,&py);}
            desc=sprite_desc(bt,fw,fh,hf,vf,yvariant);
            obj_container(dw,dh,&wt,&ht);need=wt*ht;
            if(obj_visible(px,py,wt*8u,ht*8u)){
                if(pass==0u){PS->sprite_oam_index[entry]=(uint8_t)obj;for(i=0u;i<fw*fh;i++){unsigned st=(bt+i)&0x7ffu;PS->sprite_source_mask[st>>5]|=1u<<(st&31u);}if(srcdirty){PS->sprite_bank_key[0][entry]=PS->sprite_bank_key[1][entry]=0u;PS->sprite_bank_tile[0][entry]=PS->sprite_bank_tile[1][entry]=0xffffu;}if(PS->sprite_bank_key[PS->active_obj_bank][entry]!=desc||PS->sprite_bank_tile[PS->active_obj_bank][entry]!=tiles)any_pixels=1;}
                if(obj>=128u||tiles+need>PROJ_OBJ_BANK_TILES)return -4;
                if(pass==1u){obj_shape(wt,ht,&shape,&size);if(PS->sprite_bank_key[bank][entry]!=desc||PS->sprite_bank_tile[bank][entry]!=tiles){build_obj_pixels(v,bank?512u:0u,tiles,bt,fw,fh,hf,vf,dw,dh,wt,ht,ymap);PS->sprite_bank_key[bank][entry]=desc;PS->sprite_bank_tile[bank][entry]=(uint16_t)tiles;}
                    PS->oam[obj][0]=(uint16_t)((py&0xff)|(shape<<14));PS->oam[obj][1]=(uint16_t)((px&0x1ff)|(size<<14));PS->oam[obj][2]=(uint16_t)(((bank?512u:0u)+tiles)|((hi?0u:2u)<<10)|(pal<<12));PS->oam[obj][3]=0u;}
                tiles+=need;obj++;
            }else if(pass==0u){PS->sprite_bank_key[0][entry]=PS->sprite_bank_key[1][entry]=0u;PS->sprite_bank_tile[0][entry]=PS->sprite_bank_tile[1][entry]=0xffffu;}
            if(!link)break;entry=link;
        }
    }
    while(obj<128u){PS->oam[obj][0]=0x0200u;PS->oam[obj][1]=PS->oam[obj][2]=PS->oam[obj][3]=0u;obj++;}
    if(direct){dma16(OAM16,&PS->oam[0][0],512u);PS->active_obj_bank=(uint8_t)bank;PS->sprite_valid=1u;PS->sprite_tile_count=(uint16_t)tiles;}
    else {PS->sprite_queue_valid=1u;PS->sprite_queue_bank=(uint8_t)bank;PS->sprite_tile_count=(uint16_t)tiles;}
    return 0;
}

/* Return 1 when a SAT update is provably position-only and the cached projected
   pixels/OAM topology can be reused. Return 0 to request the canonical full
   sprite staging path. No target state is published until VBlank. */
static int stage_sprite_positions_only(const Md2GbaVdp*v){
    unsigned entry=0u,guard=0u,base_sat=md2gba_vdp_sat_base(v),tiles=0u;int32_t cax,cay;
    if(!PS->sprite_valid)return 0;get_camera(v,0u,&cax,&cay);
    while(guard++<80u){
        unsigned a=(base_sat+entry*8u)&0xffffu;uint16_t yw=vrw(v,a),meta=vrw(v,a+2u),attr=vrw(v,a+4u),xw=vrw(v,a+6u);
        unsigned sz=v->vram[(uint16_t)(a+2u)],link=v->vram[(uint16_t)(a+3u)]&0x7fu,fw=((sz>>2)&3u)+1u,fh=(sz&3u)+1u;
        unsigned bt=attr&0x7ffu,hf=!!(attr&0x0800u),vf=!!(attr&0x1000u),dw=obj_xcount[fw-1u],phase,dh,wt,ht,oi=PS->sprite_oam_index[entry];
        int sx=(int)(xw&0x1ffu)-128,sy=(int)(yw&0x3ffu)-128,px,py,vis,ui_native;uint32_t desc;unsigned yvariant;
        if(meta!=PS->sprite_sat_meta[entry][0]||attr!=PS->sprite_sat_meta[entry][1])return 0;
        phase=(unsigned)floor_mod7((int32_t)sy+cay);dh=obj_ycount_phase[fh-1u][phase];ui_native=ui_sprite_native_position(sx,sy,fw*8u,fh*8u,cax,cay,&px,&py);
        if(ui_native){dw=fw*8u;dh=fh*8u;yvariant=2u;}else{yvariant=(dh!=obj_ycount_min[fh-1u]);quality_obj_position(sx,sy,cax,cay,fw*8u,fh*8u,dw,dh,&px,&py);}
        desc=sprite_desc(bt,fw,fh,hf,vf,yvariant);if(PS->sprite_bank_key[PS->active_obj_bank][entry]!=desc)return 0;
        obj_container(dw,dh,&wt,&ht);vis=obj_visible(px,py,wt*8u,ht*8u);
        if(vis!=(oi!=0xffu))return 0;
        if(vis){unsigned need=wt*ht;if(PS->sprite_bank_tile[PS->active_obj_bank][entry]!=tiles)return 0;{uint16_t a0=PS->oam[oi][0],a1=PS->oam[oi][1];PS->oam[oi][0]=(uint16_t)((a0&0xff00u)|(py&0xff));PS->oam[oi][1]=(uint16_t)((a1&0xfe00u)|(px&0x1ff));}tiles+=need;}
        if(!link)break;entry=link;
    }
    PS->sprite_queue_valid=1u;PS->sprite_queue_bank=PS->active_obj_bank;return 1;
}

static void configure_display_bank(unsigned bank){
    unsigned sb=PROJ_SB_FIRST+(bank&1u)*4u;
    REG_BG0CNT=(uint16_t)(1u|(1u<<7)|((sb+0u)<<8));
    REG_BG1CNT=(uint16_t)(1u|(1u<<7)|((sb+1u)<<8));
    REG_BG2CNT=(uint16_t)(3u|(1u<<7)|((sb+2u)<<8));
    REG_BG3CNT=(uint16_t)(3u|(1u<<7)|((sb+3u)<<8));
    REG_DISPCNT=(uint16_t)((1u<<8)|(1u<<9)|(1u<<10)|(1u<<11)|(1u<<12)|(1u<<6));
}
static void configure_display(void){configure_display_bank(PS->map_bank);}

/* P136A: short-lived proof that old+new slot ownership did not fit for an
   exact displayed geometry/origin.  This only avoids repeated proof work;
   in-place recovery itself remains the canonical capacity fallback. */
static uint8_t projected_capacity_hint_ttl;
static uint16_t projected_capacity_hint_w,projected_capacity_hint_h,projected_capacity_hint_base[2];
static int32_t projected_capacity_hint_ox[2],projected_capacity_hint_oy[2];

void md2gba_gba_projected_reset(void){uint16_t old=REG_IME;REG_IME=0u;proj_ui_shown_valid=0u;projected_capacity_hint_ttl=0u;if(PS->magic==PROJ_MAGIC&&PS->pending)queue_rollback();PS->magic=0u;PS->pending=0u;REG_IME=old;}
int md2gba_gba_projected_pending(void){return PS->magic==PROJ_MAGIC&&PS->pending;}

static void rollback_built_bank(unsigned bank){
    unsigned sb=PROJ_SB_FIRST+(bank&1u)*4u,i;for(i=0u;i<PROJ_LAYERS*PROJ_MAP_CELLS;i++){
        unsigned slot=BG_VRAM16[sb*1024u+i]&0x03ffu;if(slot&&slot<PROJ_TILE_SLOTS&&PS->slot_ref[slot])PS->slot_ref[slot]--;BG_VRAM16[sb*1024u+i]=0u;
        if((i&1023u)==1023u)projected_checkpoint();
    }
}
static void clear_u32_aligned(void*dst,unsigned bytes){
    uint32_t *d=(uint32_t*)dst;unsigned n=bytes>>2u,i;uint8_t *tail;
    for(i=0u;i<n;i++)d[i]=0u;
    tail=(uint8_t*)(void*)(d+n);for(i=n<<2u;i<bytes;i++)*tail++=0u;
}
static void clear_bg_map_halfwords(unsigned halfword_base,unsigned count){
    volatile uint32_t *d=BG_VRAM32+(halfword_base>>1u);unsigned words=count>>1u,i;
    for(i=0u;i<words;i++)d[i]=0u;
    if(count&1u)BG_VRAM16[halfword_base+count-1u]=0u;
}

MD2GBA_PROJECTED_COLD static void finish_bank_rebase(void){
    unsigned layer,cell,sb;if(!PS->rebase_cleanup)return;sb=PROJ_SB_FIRST+PS->map_bank*4u;
    for(layer=0u;layer<PROJ_LAYERS;layer++){for(cell=0u;cell<PROJ_MAP_CELLS;cell++){
        unsigned old=PS->active_slot[layer][cell];if(old&&old<PROJ_TILE_SLOTS&&PS->slot_ref[old])PS->slot_ref[old]--;
        PS->active_slot[layer][cell]=(uint16_t)(BG_VRAM16[(sb+layer)*1024u+cell]&0x03ffu);
    }projected_checkpoint();}
    {unsigned l,w;for(l=0u;l<PROJ_LAYERS;l++)for(w=0u;w<32u;w++)PS->stale_cell_mask[l][w]=0u;}
    PS->rebase_cleanup=0u;
}
/* Prepare a complete replacement in screenblocks 28..31 (or 24..27 when the
   alternate bank is active) while the previous projected frame remains visible.
   Tile slots referenced by the displayed frame stay pinned, so new patterns can
   use only genuinely free/deduplicated slots. If the union does not fit, the
   caller falls back to a backdrop-only rebuild rather than GBA forced-white. */
static int try_transactional_rebuild(Md2GbaVdp*v){
    unsigned plane,x,y,i,newbank=PS->map_bank^1u,sb=PROJ_SB_FIRST+newbank*4u;int32_t ax,ay,bx,by;uint16_t scroll[8];
    unsigned nw=md2gba_vdp_plane_width(v),nh=md2gba_vdp_plane_height(v);if((nw!=32u&&nw!=64u)||(nh!=32u&&nh!=64u))return -1;
#if MD2GBA_PROJECTED_REBUILD_REUSE
    /* P135 exact carry-over.  The fitted world is anchored: a ring cell's pair
       depends only on (plane,tx,ty), plane geometry, and the source names and
       patterns in its 2x2/2x3 footprint.  A cell that was in the displayed
       coverage, is not showing a UI tile or UI suppression, is not stale, and
       whose complete footprint is clean therefore already displays exactly the
       pair make_bg_tile_pair_ex() would synthesize now.  Re-reference that slot
       (new-bank ownership, released normally at rebase) instead of re-running
       the 64-pixel assembler and slot hashing.  Everything else rebuilds. */
    int32_t pox[2],poy[2];uint16_t pbase[2];static uint32_t covered[2][32];  /* EWRAM, not the tight IWRAM stack; non-reentrant */
    int reuse=PS->initialized&&!PS->pending&&!PS->capacity_failed&&!PS->rebase_cleanup&&!PS->bank_switch_pending&&
              proj_ui_shown_valid&&nw==PS->plane_w&&nh==PS->plane_h;
    pox[0]=PS->origin_x[0];pox[1]=PS->origin_x[1];poy[0]=PS->origin_y[0];poy[1]=PS->origin_y[1];
    pbase[0]=PS->plane_base[0];pbase[1]=PS->plane_base[1];
    for(i=0u;i<32u;i++){covered[0][i]=0u;covered[1][i]=0u;}
#else
    const int reuse=0;
#endif
    queue_reset();clear_bg_map_halfwords(sb*1024u,PROJ_LAYERS*PROJ_MAP_CELLS);
    PS->plane_w=(uint16_t)nw;PS->plane_h=(uint16_t)nh;PS->plane_base[0]=md2gba_vdp_plane_a_base(v);PS->plane_base[1]=md2gba_vdp_plane_b_base(v);
    for(i=0u;i<64u;i++){PS->bg_source_mask[i]=0u;PS->sprite_source_mask[i]=0u;}
    if(!reuse){for(i=0u;i<2u*PROJ_MAP_CELLS;i++)((uint32_t*)PS->cell_dep_bloom)[i]=0u;}
    set_scroll_payload(v,scroll,&ax,&ay,&bx,&by);PS->origin_x[0]=ax;PS->origin_y[0]=ay;PS->origin_x[1]=bx;PS->origin_y[1]=by;
    for(plane=0u;plane<2u;plane++){
        int32_t ox=PS->origin_x[plane],oy=PS->origin_y[plane];unsigned hi_layer=plane,lo_layer=plane+2u;
#if MD2GBA_PROJECTED_REBUILD_REUSE
        int plane_reuse=reuse&&pbase[plane]==PS->plane_base[plane];
#endif
        for(y=0u;y<PROJ_COVER_H;y++){for(x=0u;x<PROJ_COVER_W;x++){
            int32_t tx=ox+(int32_t)x,ty=oy+(int32_t)y;unsigned cell=(imod(ty,32u)<<5)|imod(tx,32u),hs,ls;uint8_t hi[64] __attribute__((aligned(4))),lo[64] __attribute__((aligned(4))),nz;uint32_t bloom;
#if MD2GBA_PROJECTED_REBUILD_REUSE
            unsigned cw=cell>>5;uint32_t cb=1u<<(cell&31u);
            if(reuse)covered[plane][cw]|=cb;
            if(plane_reuse&&tx>=pox[plane]&&tx<pox[plane]+(int32_t)PROJ_COVER_W&&ty>=poy[plane]&&ty<poy[plane]+(int32_t)PROJ_COVER_H&&
               !((PS->stale_cell_mask[hi_layer][cw]|PS->stale_cell_mask[lo_layer][cw])&cb)&&
               !(plane==0u&&((proj_ui_shown[cw]|PS->ui_target_mask[cw]|PS->ui_suppress_mask[cw])&cb))&&
               bg_cell_reuse_sources(v,plane,tx,ty)){
                hs=PS->active_slot[hi_layer][cell];ls=PS->active_slot[lo_layer][cell];
                if(hs&&hs<PROJ_TILE_SLOTS)PS->slot_ref[hs]++;if(ls&&ls<PROJ_TILE_SLOTS)PS->slot_ref[ls]++;
                BG_VRAM16[(sb+hi_layer)*1024u+cell]=(uint16_t)hs;BG_VRAM16[(sb+lo_layer)*1024u+cell]=(uint16_t)ls;
#if defined(MD2GBA_PROJECTED_HOST_TEST)
                md2gba_projected_test_bg_reuses++;
#endif
                continue;   /* cell_dep_bloom still describes these exact sources */
            }
#endif
            make_bg_tile_pair_ex(v,plane,tx,ty,hi,lo,&bloom,0,&nz);
            hs=(nz&1u)?find_or_alloc_slot_nonzero(hi):0u;if(hs==0xffffu){rollback_built_bank(newbank);return -2;}
            if(hs)PS->slot_ref[hs]++;
            ls=(nz&2u)?find_or_alloc_slot_nonzero(lo):0u;if(ls==0xffffu){if(hs&&PS->slot_ref[hs])PS->slot_ref[hs]--;rollback_built_bank(newbank);return -2;}
            if(ls)PS->slot_ref[ls]++;
            BG_VRAM16[(sb+hi_layer)*1024u+cell]=(uint16_t)hs;BG_VRAM16[(sb+lo_layer)*1024u+cell]=(uint16_t)ls;PS->cell_dep_bloom[plane][cell]=bloom;
        }projected_checkpoint();}
    }
#if MD2GBA_PROJECTED_REBUILD_REUSE
    if(reuse){unsigned p2,c2;for(p2=0u;p2<2u;p2++)for(c2=0u;c2<PROJ_MAP_CELLS;c2++)if(!(covered[p2][c2>>5]&(1u<<(c2&31u)))){PS->cell_dep_bloom[p2][c2]=0u;}}
#endif
    if(ui_apply_build_bank(v,sb)){rollback_built_bank(newbank);return -5;}
    copy_palette_snapshot(v,PS->palette);PS->palette_valid=1u;if(stage_sprites(v,0)){rollback_built_bank(newbank);return -3;}
    for(i=0u;i<8u;i++)PS->scroll[i]=scroll[i];PS->scroll_valid=1u;PS->pending_map_bank=(uint8_t)newbank;PS->bank_switch_pending=1u;
    PS->last_vram_epoch=v->vram_epoch;PS->last_cram_epoch=v->cram_epoch;PS->last_vsram_epoch=v->vsram_epoch;PS->last_regs_epoch=v->regs_epoch;PS->last_scroll_epoch=v->present_scroll_epoch;PS->capacity_failed=0u;PS->initialized=1u;
    {uint16_t old=REG_IME;REG_IME=0u;MD2GBA_COMPILER_BARRIER();PS->pending=1u;REG_IME=old;}ui_shown_sync();md2gba_vdp_clear_dirty(v);return 0;
}
#ifndef MD2GBA_PROJECTED_INPLACE_RECOVERY
#define MD2GBA_PROJECTED_INPLACE_RECOVERY 1
#endif
#if MD2GBA_PROJECTED_INPLACE_RECOVERY
/* P136: capacity recovery without a black screen.  When old+new frames cannot
   coexist in 768 slots, rebuild the *displayed* bank in place: a new tile is
   written only into a slot no visible cell references (ref==0), then the cell's
   map entry is switched and its old slot released, which frees slots as the
   update proceeds.  Worst case is a brief old/new cell mix, never a hidden
   display.  If slots still run out: release off-coverage cells, then blank
   not-yet-built cells; only if even that fails does the old path run. */
static void inplace_set(unsigned layer,unsigned cell,unsigned ns){
    unsigned sb=PROJ_SB_FIRST+PS->map_bank*4u,old=PS->active_slot[layer][cell];
    if(ns&&ns<PROJ_TILE_SLOTS)PS->slot_ref[ns]++;
    PS->active_slot[layer][cell]=(uint16_t)ns;BG_VRAM16[(sb+layer)*1024u+cell]=(uint16_t)ns;
    if(old&&old<PROJ_TILE_SLOTS&&PS->slot_ref[old])PS->slot_ref[old]--;
}
static uint32_t inplace_cov[2][32],inplace_done[2][32];
static void inplace_release(int include_unbuilt){
    unsigned plane,cell;for(plane=0u;plane<2u;plane++)for(cell=0u;cell<PROJ_MAP_CELLS;cell++){
        uint32_t b=1u<<(cell&31u);if(inplace_done[plane][cell>>5]&b)continue;
        if((inplace_cov[plane][cell>>5]&b)&&!include_unbuilt)continue;
        inplace_set(plane,cell,0u);inplace_set(plane+2u,cell,0u);}
}
static unsigned inplace_alloc(const uint8_t p[64]){
    unsigned s=find_or_alloc_slot_nonzero(p);if(s!=0xffffu)return s;
    inplace_release(0);s=find_or_alloc_slot_nonzero(p);if(s!=0xffffu)return s;
    inplace_release(1);return find_or_alloc_slot_nonzero(p);
}
typedef struct ProjShownGeom{int valid;int32_t ox[2],oy[2];uint16_t base[2];}ProjShownGeom;
static int inplace_rebuild(Md2GbaVdp*v,const ProjShownGeom*g){
    unsigned plane,x,y,i;int32_t ax,ay,bx,by;uint16_t scroll[8];
    unsigned nw=md2gba_vdp_plane_width(v),nh=md2gba_vdp_plane_height(v);
    if((nw!=32u&&nw!=64u)||(nh!=32u&&nh!=64u)||PS->pending||PS->bank_switch_pending||PS->rebase_cleanup)return -1;
    queue_reset();
    PS->plane_w=(uint16_t)nw;PS->plane_h=(uint16_t)nh;PS->plane_base[0]=md2gba_vdp_plane_a_base(v);PS->plane_base[1]=md2gba_vdp_plane_b_base(v);
    for(i=0u;i<64u;i++){PS->bg_source_mask[i]=0u;PS->sprite_source_mask[i]=0u;}
    if(!g->valid){for(i=0u;i<2u*PROJ_MAP_CELLS;i++)((uint32_t*)PS->cell_dep_bloom)[i]=0u;}
    set_scroll_payload(v,scroll,&ax,&ay,&bx,&by);PS->origin_x[0]=ax;PS->origin_y[0]=ay;PS->origin_x[1]=bx;PS->origin_y[1]=by;
    for(plane=0u;plane<2u;plane++)for(i=0u;i<32u;i++){inplace_cov[plane][i]=0u;inplace_done[plane][i]=0u;}
    for(plane=0u;plane<2u;plane++)for(y=0u;y<PROJ_COVER_H;y++)for(x=0u;x<PROJ_COVER_W;x++){
        unsigned cell=(imod(PS->origin_y[plane]+(int32_t)y,32u)<<5)|imod(PS->origin_x[plane]+(int32_t)x,32u);inplace_cov[plane][cell>>5]|=1u<<(cell&31u);}
    for(plane=0u;plane<2u;plane++){
        int32_t ox=PS->origin_x[plane],oy=PS->origin_y[plane];unsigned hi_layer=plane,lo_layer=plane+2u;
        for(y=0u;y<PROJ_COVER_H;y++){for(x=0u;x<PROJ_COVER_W;x++){
            int32_t tx=ox+(int32_t)x,ty=oy+(int32_t)y;unsigned cell=(imod(ty,32u)<<5)|imod(tx,32u),hs,ls;uint8_t hi[64] __attribute__((aligned(4))),lo[64] __attribute__((aligned(4))),nz;uint32_t bloom;
            if(g->valid&&g->base[plane]==PS->plane_base[plane]&&tx>=g->ox[plane]&&tx<g->ox[plane]+(int32_t)PROJ_COVER_W&&ty>=g->oy[plane]&&ty<g->oy[plane]+(int32_t)PROJ_COVER_H&&
               !((PS->stale_cell_mask[hi_layer][cell>>5]|PS->stale_cell_mask[lo_layer][cell>>5])&(1u<<(cell&31u)))&&
               !(plane==0u&&((proj_ui_shown[cell>>5]|PS->ui_target_mask[cell>>5]|PS->ui_suppress_mask[cell>>5])&(1u<<(cell&31u))))&&
               bg_cell_reuse_sources(v,plane,tx,ty)){
                inplace_done[plane][cell>>5]|=1u<<(cell&31u);   /* displayed pair is already exact; blooms retained */
#if defined(MD2GBA_PROJECTED_HOST_TEST)
                md2gba_projected_test_bg_reuses++;
#endif
                continue;
            }
            make_bg_tile_pair_ex(v,plane,tx,ty,hi,lo,&bloom,0,&nz);
            hs=(nz&1u)?inplace_alloc(hi):0u;if(hs==0xffffu)return -2;
            inplace_set(hi_layer,cell,hs);
            ls=(nz&2u)?inplace_alloc(lo):0u;if(ls==0xffffu)return -2;
            inplace_set(lo_layer,cell,ls);
            inplace_done[plane][cell>>5]|=1u<<(cell&31u);
            PS->cell_dep_bloom[plane][cell]=bloom;
        }projected_checkpoint();}
    }
    inplace_release(0);
    if(g->valid){unsigned c2;for(plane=0u;plane<2u;plane++)for(c2=0u;c2<PROJ_MAP_CELLS;c2++)if(!(inplace_cov[plane][c2>>5]&(1u<<(c2&31u)))){PS->cell_dep_bloom[plane][c2]=0u;}}
    {unsigned l,w;for(l=0u;l<PROJ_LAYERS;l++)for(w=0u;w<32u;w++)PS->stale_cell_mask[l][w]=0u;}
    if(ui_apply_direct(v))return -5;
    copy_palette_snapshot(v,PS->palette);apply_palette(PS->palette);if(stage_sprites(v,1))return -3;
    for(i=0u;i<8u;i++)PS->scroll[i]=scroll[i];write_scroll_now(scroll);configure_display();
    PS->capacity_failed=0u;PS->initialized=1u;PS->last_vram_epoch=v->vram_epoch;PS->last_cram_epoch=v->cram_epoch;PS->last_vsram_epoch=v->vsram_epoch;PS->last_regs_epoch=v->regs_epoch;PS->last_scroll_epoch=v->present_scroll_epoch;
#if defined(MD2GBA_PROJECTED_HOST_TEST)
    md2gba_projected_test_inplace_rebuilds++;
#endif
    ui_shown_sync();md2gba_vdp_clear_dirty(v);return 0;
}
#endif
static int full_rebuild(Md2GbaVdp*v){
    unsigned plane,x,y,i;int32_t ax,ay,bx,by;uint16_t scroll[8];
    if(PS->magic==PROJ_MAGIC&&PS->initialized&&!PS->pending){
#if MD2GBA_PROJECTED_INPLACE_RECOVERY
        ProjShownGeom g;g.valid=MD2GBA_PROJECTED_REBUILD_REUSE&&!PS->capacity_failed&&!PS->rebase_cleanup&&!PS->bank_switch_pending&&proj_ui_shown_valid&&
            md2gba_vdp_plane_width(v)==PS->plane_w&&md2gba_vdp_plane_height(v)==PS->plane_h;
        g.ox[0]=PS->origin_x[0];g.ox[1]=PS->origin_x[1];g.oy[0]=PS->origin_y[0];g.oy[1]=PS->origin_y[1];g.base[0]=PS->plane_base[0];g.base[1]=PS->plane_base[1];
#endif
        int tr,attempted=1;
#if MD2GBA_PROJECTED_INPLACE_RECOVERY
        if(projected_capacity_hint_ttl&&g.valid){
            uint16_t hs[8];int32_t hax,hay,hbx,hby;set_scroll_payload(v,hs,&hax,&hay,&hbx,&hby);
            if(projected_capacity_hint_w==md2gba_vdp_plane_width(v)&&projected_capacity_hint_h==md2gba_vdp_plane_height(v)&&
               projected_capacity_hint_base[0]==md2gba_vdp_plane_a_base(v)&&projected_capacity_hint_base[1]==md2gba_vdp_plane_b_base(v)&&
               projected_capacity_hint_ox[0]==hax&&projected_capacity_hint_oy[0]==hay&&projected_capacity_hint_ox[1]==hbx&&projected_capacity_hint_oy[1]==hby){
                tr=-2;attempted=0;
            }else projected_capacity_hint_ttl=0u;
        }else tr=0;
        if(attempted)tr=try_transactional_rebuild(v);
#else
        tr=try_transactional_rebuild(v);
#endif
        if(tr==0){projected_capacity_hint_ttl=0u;return 0;}
#if MD2GBA_PROJECTED_INPLACE_RECOVERY
        /* the failed attempt may have cleared blooms only when it was not reusing */
        if(g.valid&&!(MD2GBA_PROJECTED_REBUILD_REUSE))g.valid=0;
        if(tr!=-1&&inplace_rebuild(v,&g)==0){
            if(attempted&&tr==-2){
                projected_capacity_hint_ttl=8u;projected_capacity_hint_w=PS->plane_w;projected_capacity_hint_h=PS->plane_h;
                projected_capacity_hint_base[0]=PS->plane_base[0];projected_capacity_hint_base[1]=PS->plane_base[1];
                projected_capacity_hint_ox[0]=PS->origin_x[0];projected_capacity_hint_oy[0]=PS->origin_y[0];
                projected_capacity_hint_ox[1]=PS->origin_x[1];projected_capacity_hint_oy[1]=PS->origin_y[1];
            }
            return 0;
        }
        if(!attempted)projected_capacity_hint_ttl=0u;
#endif
    }
    /* Initial/recovery rebuild. Never use GBA forced blank: forced blank is a
       hardware-defined white raster and was the source of the visible white
       flashes during PSIV loads. Hide layers and expose a black backdrop only
       for the rare case where old+new projected patterns cannot coexist. */
    REG_DISPCNT=0u;BG_PAL[0]=0u;md2gba_gba_stop_hblank_scroll_dma();md2gba_gba_scroll_latch_clear();
    clear_u32_aligned((void*)PS,(unsigned)sizeof(*PS));
    PS->magic=PROJ_MAGIC;PS->map_bank=0u;PS->plane_w=(uint16_t)md2gba_vdp_plane_width(v);PS->plane_h=(uint16_t)md2gba_vdp_plane_height(v);
    if((PS->plane_w!=32u&&PS->plane_w!=64u)||(PS->plane_h!=32u&&PS->plane_h!=64u)){PS->capacity_failed=1u;return -1;}
    PS->plane_base[0]=md2gba_vdp_plane_a_base(v);PS->plane_base[1]=md2gba_vdp_plane_b_base(v);
    clear_bg_map_halfwords(PROJ_SB_FIRST*1024u,8u*PROJ_MAP_CELLS);
    for(i=0u;i<16u;i++)BG_VRAM32[i]=0u;PS->slot_ref[0]=0xffffu;PS->slot_hash[0]=1u;PS->slot_highwater=0u;
    set_scroll_payload(v,scroll,&ax,&ay,&bx,&by);PS->origin_x[0]=ax;PS->origin_y[0]=ay;PS->origin_x[1]=bx;PS->origin_y[1]=by;
    for(plane=0u;plane<2u;plane++){
        int32_t ox=PS->origin_x[plane],oy=PS->origin_y[plane];unsigned hi_layer=plane,lo_layer=plane+2u;
        for(y=0u;y<PROJ_COVER_H;y++){for(x=0u;x<PROJ_COVER_W;x++){
            int32_t tx=ox+(int32_t)x,ty=oy+(int32_t)y;unsigned cell=(imod(ty,32u)<<5)|imod(tx,32u),hs,ls;uint8_t hi[64] __attribute__((aligned(4))),lo[64] __attribute__((aligned(4))),nz;uint32_t bloom;
            make_bg_tile_pair_ex(v,plane,tx,ty,hi,lo,&bloom,0,&nz);
            hs=(nz&1u)?find_or_alloc_slot_nonzero(hi):0u;if(hs==0xffffu){PS->capacity_failed=1u;return -2;}
            if(hs)PS->slot_ref[hs]++;
            ls=(nz&2u)?find_or_alloc_slot_nonzero(lo):0u;if(ls==0xffffu){if(hs&&PS->slot_ref[hs])PS->slot_ref[hs]--;PS->capacity_failed=1u;return -2;}
            if(ls)PS->slot_ref[ls]++;
            PS->active_slot[hi_layer][cell]=(uint16_t)hs;PS->active_slot[lo_layer][cell]=(uint16_t)ls;
            BG_VRAM16[(PROJ_SB_FIRST+hi_layer)*1024u+cell]=(uint16_t)hs;BG_VRAM16[(PROJ_SB_FIRST+lo_layer)*1024u+cell]=(uint16_t)ls;PS->cell_dep_bloom[plane][cell]=bloom;
        }projected_checkpoint();}
    }
    if(ui_apply_direct(v)){PS->capacity_failed=1u;return -5;}
    copy_palette_snapshot(v,PS->palette);apply_palette(PS->palette);if(stage_sprites(v,1)){PS->capacity_failed=1u;return -3;}
    for(i=0u;i<8u;i++)PS->scroll[i]=scroll[i];write_scroll_now(scroll);configure_display();
    PS->initialized=1u;PS->last_vram_epoch=v->vram_epoch;PS->last_cram_epoch=v->cram_epoch;PS->last_vsram_epoch=v->vsram_epoch;PS->last_regs_epoch=v->regs_epoch;PS->last_scroll_epoch=v->present_scroll_epoch;
    ui_shown_sync();md2gba_vdp_clear_dirty(v);return 0;
}

static int vram_dirty_only_range(const Md2GbaVdp*v,unsigned start,unsigned len){
    unsigned first_byte=start&0xffffu,end_byte,first_page,last_page,wi,first_block,last_block,b;
    if(!len)return 0;end_byte=first_byte+len-1u;if(end_byte>=0x10000u)return 0;
    first_page=first_byte>>8;last_page=end_byte>>8;first_block=first_byte>>5;last_block=end_byte>>5;
    /* P134S: most walking frames only dirty SAT plus scroll state. Reject any
       dirty 256-byte page outside the SAT span using 8 words instead of
       rescanning all 64 fine dirty words. Only the two partial boundary pages
       need exact 32-byte-block checks. */
    {unsigned coarse_any=0u;
     for(wi=0u;wi<8u;wi++){uint32_t bits=v->vram_dirty_pages[wi],allow=0u;unsigned lo=wi*32u,hi=lo+31u;coarse_any|=bits;if(!bits)continue;
        if(!(hi<first_page||lo>last_page)){unsigned a=first_page>lo?first_page-lo:0u,z=last_page<hi?last_page-lo:31u;allow=(z==31u?0xffffffffu:((1u<<(z+1u))-1u))&~(a?((1u<<a)-1u):0u);}
        if(bits&~allow)return 0;
     }
     /* Host/oracle code can author the fine dirty bitmap directly. Production
        VDP writes maintain both summaries, but if the coarse summary is absent
        fall back to the exact fine-bit check rather than misclassifying work. */
     if(!coarse_any){unsigned first=first_block,last=last_block;
        for(wi=0u;wi<64u;wi++){uint32_t bits=v->vram_dirty_tiles[wi],allow=0u,blo=wi*32u,bhi=blo+31u;if(!bits)continue;
            if(!(bhi<first||blo>last)){unsigned lo=first>blo?first-blo:0u,hi=last<bhi?last-blo:31u;allow=(hi==31u?0xffffffffu:((1u<<(hi+1u))-1u))&~(lo?((1u<<lo)-1u):0u);}
            if(bits&~allow)return 0;
        }
        return 1;
     }
    }
    for(b=first_page*8u;b<first_block;b++)if(v->vram_dirty_tiles[b>>5]&(1u<<(b&31u)))return 0;
    for(b=last_block+1u;b<(last_page+1u)*8u;b++)if(v->vram_dirty_tiles[b>>5]&(1u<<(b&31u)))return 0;
    return 1;
}
static int projected_plane_map_dirty(const Md2GbaVdp*v,unsigned plane){
#if defined(MD2GBA_PROJECTED_HOST_TEST)
    md2gba_projected_test_map_range_checks++;
#endif
    {unsigned bytes=(unsigned)PS->plane_w*(unsigned)PS->plane_h*2u;return md2gba_vdp_vram_range_dirty(v,PS->plane_base[plane&1u],bytes);}
}
static int projected_ui_names_dirty(const Md2GbaVdp*v){
    /* UI recognition examines only the 40x28 source-tile viewport. A name
       write to an offscreen part of Plane A must not run the profile and the
       generic framed-window search, especially on animated field maps. */
    int32_t cx,cy,stx,sty;unsigned y,w=PS->plane_w,h=PS->plane_h,vieww=w<40u?w:40u;
    get_camera(v,0u,&cx,&cy);stx=floorpow2(cx,3u);sty=floorpow2(cy,3u);
    for(y=0u;y<28u;y++){
        unsigned row=imod(sty+(int32_t)y,h),x=imod(stx,w),remain=vieww;
        while(remain){
            unsigned run=w-x<remain?w-x:remain;
            uint16_t address=(uint16_t)((unsigned)PS->plane_base[0]+2u*(row*w+x));
            if(md2gba_vdp_vram_range_dirty(v,address,run*2u))return 1;
            remain-=run;x=0u;
        }
    }
    return 0;
}
static unsigned projected_lowbit_index(uint32_t b){
    unsigned pos=0u;
    static const uint8_t lo4[16]={0,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0};
    if(!(b&0xffffu)){b>>=16;pos+=16u;}
    if(!(b&0xffu)){b>>=8;pos+=8u;}
    if(!(b&0xfu)){b>>=4;pos+=4u;}
    return pos+lo4[b&15u];
}
#if defined(MD2GBA_PROJECTED_HOST_TEST)
static uint8_t projected_dirty_map_bloom(const Md2GbaVdp*v,unsigned plane){
    unsigned bytes=(unsigned)PS->plane_w*(unsigned)PS->plane_h*2u,wi;
    uint16_t base=PS->plane_base[plane&1u];uint8_t out=0u;
    for(wi=0u;wi<64u;wi++){
        uint32_t bits=v->vram_dirty_tiles[wi];
        while(bits){
            unsigned pos=projected_lowbit_index(bits),offset;bits&=bits-1u;
            offset=(uint16_t)((wi*32u+pos)*32u-base);
            if(offset<bytes)out|=(uint8_t)map_bloom_bit(offset>>5);
            if(out==0xffu)return out;
        }
    }
    return out;
}
#endif
/* P137 PSC-style pretranslated dirtiness: name-table writes are already
   represented exactly by the VDP's 32-byte dirty blocks. Translate those
   blocks directly into the current projected ring once per prepare pass rather
   than storing an 8-bit lossy per-cell bloom and then rereading source names to
   reject collisions. This is exact for the 2x2 / 2x3 projection footprint and
   performs no source-name or pattern reads. */
MD2GBA_PROJECTED_HOT static void projected_dirty_name_cells(const Md2GbaVdp*v,unsigned plane,int32_t ox,int32_t oy,uint32_t out[32]){
    unsigned wi,x,y,w=PS->plane_w,h=PS->plane_h;uint16_t base=PS->plane_base[plane&1u];
    for(wi=0u;wi<32u;wi++)out[wi]=0u;
    for(y=0u;y<PROJ_COVER_H;y++)for(x=0u;x<PROJ_COVER_W;x++){
        int32_t tx=ox+(int32_t)x,ty=oy+(int32_t)y;
        int32_t qx3=floordiv3(tx),qy5=floordiv5(ty),cxa=tx+qx3,cybase=ty-qy5*5;
        int32_t cya=qy5*7+(int32_t)proj_ybase_rem[(unsigned)cybase],cx,cy;
        int32_t cxb=cxa+1,cyb=cya+(cybase==2?2:1);int dirty=0;
        for(cy=cya;cy<=cyb&&!dirty;cy++)for(cx=cxa;cx<=cxb;cx++){
            unsigned mx=imod(cx,w),my=imod(cy,h),a=(unsigned)base+2u*(my*w+mx);
            if(block_dirty(v,a)){dirty=1;break;}
        }
        if(dirty){unsigned cell=(imod(ty,32u)<<5)|imod(tx,32u);out[cell>>5]|=1u<<(cell&31u);}
    }
}
static int projected_bg_patterns_dirty(const Md2GbaVdp*v){return mask_dirty(v,PS->bg_source_mask);}
static int sprite_patterns_dirty(const Md2GbaVdp*v){return mask_dirty(v,PS->sprite_source_mask);}
static uint32_t vram_dirty_bloom_all(const Md2GbaVdp*v){
    unsigned wi;uint32_t out=0u;
    for(wi=0u;wi<64u;wi++){
        uint32_t bits=v->vram_dirty_tiles[wi];
        while(bits){unsigned pos=projected_lowbit_index(bits);out|=dep_bloom_bit(wi*32u+pos);bits&=bits-1u;}
    }
    return out;
}
static uint32_t projected_dirty_bloom(const Md2GbaVdp*v){
    unsigned wi;uint32_t out=0u;
    for(wi=0u;wi<64u;wi++){
        uint32_t bits=v->vram_dirty_tiles[wi]&PS->bg_source_mask[wi];
        while(bits){unsigned pos=projected_lowbit_index(bits);out|=dep_bloom_bit(wi*32u+pos);bits&=bits-1u;}
    }
    return out;
}

/* P136A: transaction-local world preparation ownership. UI restoration, camera
   edge repair and dirty-world handling can target the same projected cell in one
   prepare pass. Queue coalescing avoided duplicate map writes but did not avoid
   the expensive pixel synthesis. Keep an exact non-reentrant EWRAM bitset and
   skip any world cell already synthesized from the current VDP state. */
static uint32_t prepared_world_cells[2][32];
static int prepare_incremental(Md2GbaVdp*v){
    int32_t nx[2],ny[2];uint16_t scroll[8];unsigned plane,x,y;int r,sat_dirty;
    int cam[2],vram_changed,map_dirty[2],ui_names_dirty,patterns_dirty,world_dirty,ui_dirty=0,ui_activated=0,sat_only=0;
    int preflight_world=0,ui_restore=0;unsigned dirty_cell_count=0u;uint32_t dirty_cells[2][32],ui_restore_mask[32];
    uint32_t dirty_bloom=0u,all_dirty_bloom=0u;uint32_t (*dirty_name_cells)[32]=PS->dirty_name_cells;
    {unsigned wi;for(plane=0u;plane<2u;plane++)for(wi=0u;wi<32u;wi++)prepared_world_cells[plane][wi]=0u;}
    queue_reset();set_scroll_payload(v,scroll,&nx[0],&ny[0],&nx[1],&ny[1]);
    /* Teleports or large camera jumps are cheaper/safer as one blanked rebuild
       than hundreds of random cell transactions. */
    if(nx[0]-PS->origin_x[0]>1||PS->origin_x[0]-nx[0]>1||ny[0]-PS->origin_y[0]>1||PS->origin_y[0]-ny[0]>1||
       nx[1]-PS->origin_x[1]>1||PS->origin_x[1]-nx[1]>1||ny[1]-PS->origin_y[1]>1||PS->origin_y[1]-ny[1]>1)return 1;
    cam[0]=(nx[0]!=PS->origin_x[0]||ny[0]!=PS->origin_y[0]);cam[1]=(nx[1]!=PS->origin_x[1]||ny[1]!=PS->origin_y[1]);
    if(PS->ui_count&&(scroll[0]!=PS->scroll[0]||scroll[1]!=PS->scroll[1])){
        int32_t cx,cy;int16_t ox[PROJ_UI_MAX_PANELS],oy[PROJ_UI_MAX_PANELS];unsigned u;
        get_camera(v,0u,&cx,&cy);
        /* A source viewport crossing can expose or clip another framed panel.
           Retain the existing full-scan path in that case. When the authored
           source viewport and every native destination cell are unchanged,
           moving the BG scroll needs no 1,302-pair rebuild. */
        if(floorpow2(cx,3u)!=PS->ui_source_view_x||floorpow2(cy,3u)!=PS->ui_source_view_y)return 1;
        for(u=0u;u<PS->ui_count;u++){
            ProjUiPanel q=PS->ui_panel[u];ui_place_native(&q,cx,cy);
            if(q.dx!=PS->ui_panel[u].dx||q.dy!=PS->ui_panel[u].dy)return 1;
            ox[u]=q.ox;oy[u]=q.oy;
        }
        for(u=0u;u<PS->ui_count;u++){PS->ui_panel[u].ox=ox[u];PS->ui_panel[u].oy=oy[u];}
    }
    vram_changed=v->vram_epoch!=PS->last_vram_epoch;if(vram_changed)sat_only=vram_dirty_only_range(v,md2gba_vdp_sat_base(v),80u*8u);
    map_dirty[0]=vram_changed&&!sat_only&&projected_plane_map_dirty(v,0u);map_dirty[1]=vram_changed&&!sat_only&&projected_plane_map_dirty(v,1u);ui_names_dirty=map_dirty[0]&&projected_ui_names_dirty(v);patterns_dirty=vram_changed&&!sat_only&&projected_bg_patterns_dirty(v);world_dirty=map_dirty[0]||map_dirty[1]||patterns_dirty;if(patterns_dirty)dirty_bloom=projected_dirty_bloom(v);if(vram_changed&&PS->ui_count)all_dirty_bloom=vram_dirty_bloom_all(v);if(map_dirty[0])projected_dirty_name_cells(v,0u,nx[0],ny[0],dirty_name_cells[0]);else {unsigned wi;for(wi=0u;wi<32u;wi++)dirty_name_cells[0][wi]=0u;}if(map_dirty[1])projected_dirty_name_cells(v,1u,nx[1],ny[1],dirty_name_cells[1]);else {unsigned wi;for(wi=0u;wi<32u;wi++)dirty_name_cells[1][wi]=0u;}
    if(ui_names_dirty){
        ProjUiPanel up[PROJ_UI_MAX_PANELS];uint32_t us;unsigned un=scan_fixed_ui_panels(v,up,&us);
        int32_t ucx,ucy;int candidate=0;
        if(!PS->ui_count){get_camera(v,0u,&ucx,&ucy);candidate=MD2GBA_PRESENTATION_PROFILE_UI_CANDIDATE(v,floorpow2(ucx,3u),floorpow2(ucy,3u),40u,28u);}
        /* PSIV constructs menus/dialogue over several guest yields.  Do not let
           partial window writes enter the fitted world and then replace them a
           frame later with native UI.  Hold the last coherent frame until a
           complete authored window/portrait block exists.  Dirtiness is left
           accumulated intentionally; the completed UI or vanished candidate is
           handled from the exact source state on the next presentation pass. */
        if(!PS->ui_count&&candidate&&!un){PS->ui_candidate=1u;PS->ui_signature=0u;return 0;}
        if(!PS->ui_count&&un){
            /* A source window can briefly form a valid smaller rectangle while
               its right/bottom edge is still being extended. Do not expose that
               transient shape. Require the exact geometry signature twice; the
               completed UI then appears once at its final size. */
            if(PS->ui_candidate!=2u||PS->ui_signature!=us){PS->ui_candidate=2u;PS->ui_signature=us;return 0;}
            PS->ui_candidate=0u;if(ui_queue_activate(v,up,un,us)){queue_rollback();return 1;}ui_activated=1;map_dirty[0]=0;ui_names_dirty=0;world_dirty=map_dirty[1]||patterns_dirty;
        }
        else {
            if(PS->ui_candidate&&!candidate){PS->ui_candidate=0u;PS->ui_signature=0u;}
            if(PS->ui_count&&!un){
                /* P134W: closing a native panel does not require rebuilding all
                   1,302 world pairs. Preserve the exact old target/suppression
                   footprint, clear UI ownership, and restore only visible Plane-A
                   cells that the detached panel had replaced or suppressed. Any
                   simultaneous source dirtiness remains live below and is handled
                   by the ordinary world-dirty path in the same transaction. */
                unsigned wi;for(wi=0u;wi<32u;wi++)ui_restore_mask[wi]=PS->ui_target_mask[wi]|PS->ui_suppress_mask[wi];
                ui_masks_clear();PS->ui_count=0u;PS->ui_signature=0u;PS->ui_pattern_bloom=0u;ui_restore=1;
            }else if(us!=PS->ui_signature||un!=PS->ui_count)return 1;
        }
    }
    if(ui_restore){
        int32_t ox=nx[0],oy=ny[0];
        for(y=0u;y<PROJ_COVER_H;y++)for(x=0u;x<PROJ_COVER_W;x++){
            int32_t tx=ox+(int32_t)x,ty=oy+(int32_t)y;unsigned cell=(imod(ty,32u)<<5)|imod(tx,32u);
            if(ui_restore_mask[cell>>5]&(1u<<(cell&31u))){r=queue_cell_pair(v,0u,tx,ty);if(r){queue_rollback();return 1;}prepared_world_cells[0][cell>>5]|=1u<<(cell&31u);}
        }
    }
    ui_dirty=!ui_activated&&PS->ui_count&&(ui_names_dirty||(vram_changed&&ui_patterns_dirty(v,all_dirty_bloom)));
    /* P134W: before constructing hundreds of projected tiles, prove whether a
       stationary dirty-world update can fit the 512-entry map transaction. The
       exact source-footprint checks are cached in a 256-byte stack bitset and
       reused below, so this preflight replaces rather than duplicates the old
       dependency scan. A >512-cell update cannot possibly fit if each affected
       cell needs even one publication; go directly to the coherent rebuild and
       avoid the observed 513 builds followed by another 1,302-pair rebuild. */
    if(world_dirty&&!cam[0]&&!cam[1]){
        unsigned wi;for(plane=0u;plane<2u;plane++)for(wi=0u;wi<32u;wi++)dirty_cells[plane][wi]=0u;
        for(plane=0u;plane<2u;plane++){
            int32_t ox=nx[plane],oy=ny[plane];
            if(!map_dirty[plane]&&!patterns_dirty)continue;
            for(y=0u;y<PROJ_COVER_H;y++)for(x=0u;x<PROJ_COVER_W;x++){
                int32_t tx=ox+(int32_t)x,ty=oy+(int32_t)y;unsigned cell=(imod(ty,32u)<<5)|imod(tx,32u);
                int maybe_pattern=patterns_dirty&&!!(PS->cell_dep_bloom[plane][cell]&dirty_bloom);
                int maybe_map=map_dirty[plane]&&!!(dirty_name_cells[plane][cell>>5]&(1u<<(cell&31u)));
                if(!maybe_pattern&&!maybe_map)continue;
                if(maybe_map||bg_cell_sources_dirty(v,plane,tx,ty)){
                    uint32_t bit=1u<<(cell&31u),*word=&dirty_cells[plane][cell>>5];
                    if(!(*word&bit)){*word|=bit;dirty_cell_count++;}
                }
            }
        }
        if(dirty_cell_count>PROJ_MAP_WRITES){
            /* P138: source dirtiness can be visually neutral.  Sample exact
               projected output first; only continue the complete equivalence
               probe when the sample is mostly unchanged.  Exact matches retain
               refreshed source dependencies, but require no synthesis/write. */
            unsigned sample=0u,sample_miss=0u;int abort_probe=0;
            for(plane=0u;plane<2u&&sample<32u;plane++)for(y=0u;y<PROJ_COVER_H&&sample<32u;y++)for(x=0u;x<PROJ_COVER_W&&sample<32u;x++){
                int32_t tx=nx[plane]+(int32_t)x,ty=ny[plane]+(int32_t)y;unsigned cell=(imod(ty,32u)<<5)|imod(tx,32u);uint32_t bit=1u<<(cell&31u);
                if(!(dirty_cells[plane][cell>>5]&bit))continue;sample++;
                if(bg_cell_output_matches_active(v,plane,tx,ty)){dirty_cells[plane][cell>>5]&=~bit;dirty_cell_count--;}else sample_miss++;
            }
            if(sample>=16u&&sample_miss*4u>=sample*3u)abort_probe=1;
            if(!abort_probe){
                for(plane=0u;plane<2u;plane++)for(y=0u;y<PROJ_COVER_H;y++)for(x=0u;x<PROJ_COVER_W;x++){
                    int32_t tx=nx[plane]+(int32_t)x,ty=ny[plane]+(int32_t)y;unsigned cell=(imod(ty,32u)<<5)|imod(tx,32u);uint32_t bit=1u<<(cell&31u);
                    if(!(dirty_cells[plane][cell>>5]&bit))continue;
                    if(bg_cell_output_matches_active(v,plane,tx,ty)){dirty_cells[plane][cell>>5]&=~bit;dirty_cell_count--;}
                }
            }
            if(abort_probe||dirty_cell_count>PROJ_MAP_WRITES)return 1;
        }
        preflight_world=1;
    }
    if(cam[0]||cam[1]||world_dirty){
        for(plane=0u;plane<2u;plane++){
            int32_t ox=nx[plane],oy=ny[plane],oldx=PS->origin_x[plane],oldy=PS->origin_y[plane];
            /* A one-cell camera crossing touches only entering/leaving plane-cell
               pairs. High/low outputs are assembled together. */
            if(ox>oldx){for(y=0u;y<PROJ_COVER_H;y++){int32_t qx=ox+(int32_t)PROJ_COVER_W-1,qy=oy+(int32_t)y;unsigned qc=(imod(qy,32u)<<5)|imod(qx,32u);retire_offscreen_pair(plane,oldx,oldy+(int32_t)y);r=queue_cell_pair(v,plane,qx,qy);if(r){queue_rollback();return 1;}prepared_world_cells[plane][qc>>5]|=1u<<(qc&31u);}}
            else if(ox<oldx){for(y=0u;y<PROJ_COVER_H;y++){int32_t qx=ox,qy=oy+(int32_t)y;unsigned qc=(imod(qy,32u)<<5)|imod(qx,32u);retire_offscreen_pair(plane,oldx+(int32_t)PROJ_COVER_W-1,oldy+(int32_t)y);r=queue_cell_pair(v,plane,qx,qy);if(r){queue_rollback();return 1;}prepared_world_cells[plane][qc>>5]|=1u<<(qc&31u);}}
            if(oy>oldy){for(x=0u;x<PROJ_COVER_W;x++){int32_t qx=ox+(int32_t)x,qy=oy+(int32_t)PROJ_COVER_H-1;unsigned qc=(imod(qy,32u)<<5)|imod(qx,32u);retire_offscreen_pair(plane,oldx+(int32_t)x,oldy);r=queue_cell_pair(v,plane,qx,qy);if(r){queue_rollback();return 1;}prepared_world_cells[plane][qc>>5]|=1u<<(qc&31u);}}
            else if(oy<oldy){for(x=0u;x<PROJ_COVER_W;x++){int32_t qx=ox+(int32_t)x,qy=oy;unsigned qc=(imod(qy,32u)<<5)|imod(qx,32u);retire_offscreen_pair(plane,oldx+(int32_t)x,oldy+(int32_t)PROJ_COVER_H-1);r=queue_cell_pair(v,plane,qx,qy);if(r){queue_rollback();return 1;}prepared_world_cells[plane][qc>>5]|=1u<<(qc&31u);}}
            if(world_dirty){
                for(y=0u;y<PROJ_COVER_H;y++)for(x=0u;x<PROJ_COVER_W;x++){
                    int32_t tx=ox+(int32_t)x,ty=oy+(int32_t)y;unsigned cell=(imod(ty,32u)<<5)|imod(tx,32u);
                    if(prepared_world_cells[plane][cell>>5]&(1u<<(cell&31u)))continue;
                    if(preflight_world){if(!(dirty_cells[plane][cell>>5]&(1u<<(cell&31u))))continue;}
                    else {
                        /* Pattern-only changes first hit a cheap per-cell bloom. Name
                           table dirtiness bypasses it because dependencies may change. */
                        int maybe_pattern=patterns_dirty&&!!(PS->cell_dep_bloom[plane][cell]&dirty_bloom);int maybe_map=map_dirty[plane]&&!!(dirty_name_cells[plane][cell>>5]&(1u<<(cell&31u)));
                        if(!maybe_pattern&&!maybe_map)continue;
                        if(!maybe_map&&!bg_cell_sources_dirty(v,plane,tx,ty))continue;
                    }
                    r=queue_cell_pair(v,plane,tx,ty);if(r){queue_rollback();return 1;}prepared_world_cells[plane][cell>>5]|=1u<<(cell&31u);
                }
            }
        }
    }
    if(ui_dirty){r=ui_queue_refresh(v);if(r){queue_rollback();return 1;}
        /* A name-table rewrite may introduce a pattern absent from the old
           UI bloom. Track the new source names for later pattern-only DMA. */
        if(ui_names_dirty)PS->ui_pattern_bloom=ui_build_pattern_bloom(v,PS->ui_panel,PS->ui_count);
    }
    if(v->cram_epoch!=PS->last_cram_epoch || (v->regs_dirty_mask&(1u<<7))){copy_palette_snapshot(v,PS->palette);PS->palette_valid=1u;}
    sat_dirty=vram_changed&&(sat_only||md2gba_vdp_vram_range_dirty(v,(uint16_t)md2gba_vdp_sat_base(v),80u*8u));
    {int sprite_pixels_dirty=vram_changed&&sprite_patterns_dirty(v);
     if(sat_dirty&&!sprite_pixels_dirty&&stage_sprite_positions_only(v)){/* OAM-only movement fast path. */}
     else if(sat_dirty||sprite_pixels_dirty){r=stage_sprites(v,0);if(r){queue_rollback();return 1;}}}
    for(x=0u;x<8u;x++)PS->scroll[x]=scroll[x];
    if(PS->map_write_count)PS->scroll_valid=1u;else write_scroll_now(scroll);
    PS->origin_x[0]=nx[0];PS->origin_y[0]=ny[0];PS->origin_x[1]=nx[1];PS->origin_y[1]=ny[1];
    PS->last_vram_epoch=v->vram_epoch;PS->last_cram_epoch=v->cram_epoch;PS->last_vsram_epoch=v->vsram_epoch;PS->last_regs_epoch=v->regs_epoch;PS->last_scroll_epoch=v->present_scroll_epoch;
    ui_shown_sync();
    if(PS->map_write_count||PS->palette_valid||PS->sprite_queue_valid||PS->scroll_valid){uint16_t old=REG_IME;REG_IME=0u;MD2GBA_COMPILER_BARRIER();PS->pending=1u;REG_IME=old;md2gba_vdp_clear_dirty(v);return 0;}
    md2gba_vdp_clear_dirty(v);return 0;
}

MD2GBA_PROJECTED_COLD static int projected_present_changed(Md2GbaVdp*v,unsigned w,unsigned h,uint16_t a,uint16_t b){
    if((w!=32u&&w!=64u)||(h!=32u&&h!=64u)||w!=PS->plane_w||h!=PS->plane_h||a!=PS->plane_base[0]||b!=PS->plane_base[1])return full_rebuild(v);
    if(v->regs_dirty_mask&((1u<<2)|(1u<<4)|(1u<<12)|(1u<<16)))return full_rebuild(v);
    {int r=prepare_incremental(v);if(r>0)return full_rebuild(v);return r;}
}

/* P134S: the overwhelmingly common presenter call changes no VDP state. Keep
   only this gate in IWRAM; all expensive projection/rebuild work remains in ROM. */
MD2GBA_PROJECTED_HOT int md2gba_gba_projected_present(Md2GbaVdp*v){
    unsigned w,h;uint16_t a,b;if(projected_capacity_hint_ttl)projected_capacity_hint_ttl--;
    if((v->regs[12]&0x81u)!=0x81u || (v->regs[1]&0x08u) || (v->regs[11]&2u))return -6;
    if(PS->magic==PROJ_MAGIC&&PS->pending)return 0;
    if(PS->magic==PROJ_MAGIC&&PS->rebase_cleanup)finish_bank_rebase();
    if(PS->magic!=PROJ_MAGIC||!PS->initialized)return full_rebuild(v);
    if(v->vram_epoch==PS->last_vram_epoch&&v->cram_epoch==PS->last_cram_epoch&&
       v->vsram_epoch==PS->last_vsram_epoch&&v->regs_epoch==PS->last_regs_epoch&&
       v->present_scroll_epoch==PS->last_scroll_epoch)return 0;
    w=md2gba_vdp_plane_width(v);h=md2gba_vdp_plane_height(v);a=md2gba_vdp_plane_a_base(v);b=md2gba_vdp_plane_b_base(v);
    return projected_present_changed(v,w,h,a,b);
}

static void release_committed_stale_refs(void){
    unsigned layer,word;
    for(layer=0u;layer<PROJ_LAYERS;layer++)for(word=0u;word<32u;word++){
        uint32_t bits=PS->stale_cell_mask[layer][word];
        while(bits){
            unsigned bit=(unsigned)__builtin_ctz(bits),cell=(word<<5)+bit,slot=PS->active_slot[layer][cell];
            if(slot&&slot<PROJ_TILE_SLOTS&&PS->slot_ref[slot])PS->slot_ref[slot]--;
            PS->active_slot[layer][cell]=0u;
            bits&=bits-1u;
        }
    }
}

MD2GBA_PROJECTED_COLD static void projected_vblank_commit_slow(void){
    unsigned i;MD2GBA_COMPILER_BARRIER();
    if(PS->palette_valid)apply_palette(PS->palette);
    for(i=0u;i<PS->map_write_count;i++){
        unsigned sb=PROJ_SB_FIRST+PS->map_bank*4u;ProjMapWrite*q=&PS->map_write[i];unsigned layer=(q->map_index>>10)-sb,cell=q->map_index&1023u,old=q->old_slot;
        BG_VRAM16[q->map_index]=q->new_slot;PS->active_slot[layer][cell]=q->new_slot;PS->stale_cell_mask[layer][cell>>5]&=~(1u<<(cell&31u));if(old&&old<PROJ_TILE_SLOTS&&PS->slot_ref[old])PS->slot_ref[old]--;
    }
    release_committed_stale_refs();
    if(PS->sprite_queue_valid){dma16(OAM16,&PS->oam[0][0],512u);PS->active_obj_bank=PS->sprite_queue_bank;PS->sprite_valid=1u;}
    if(PS->scroll_valid)write_scroll_now(PS->scroll);
    if(PS->bank_switch_pending){configure_display_bank(PS->pending_map_bank);PS->map_bank=PS->pending_map_bank;PS->bank_switch_pending=0u;PS->rebase_cleanup=1u;}
    PS->map_write_count=0u;PS->palette_valid=PS->scroll_valid=PS->sprite_queue_valid=0u;MD2GBA_COMPILER_BARRIER();PS->pending=0u;
}

/* Physical VBlank calls this every frame. Avoid a GamePak-ROM entry when no
   projected transaction exists. */
MD2GBA_PROJECTED_HOT void md2gba_gba_projected_vblank_commit_pending(void){
    if(PS->magic!=PROJ_MAGIC||!PS->pending)return;projected_vblank_commit_slow();
}

unsigned md2gba_gba_projected_state_bytes(void){return (unsigned)sizeof(Md2GbaProjectedState);}
