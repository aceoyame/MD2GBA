#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <stdlib.h>
#define MD2GBA_PROJECTED_HOST_TEST 1
#define MD2GBA_PROFILE_PROJECTED_UI 1
#define MD2GBA_PRESENTATION_PROFILE_UI_NEGATIVE_EXHAUSTIVE 0
uint16_t md2gba_bg_tile_stage[1024u*16u] __attribute__((aligned(4)));
#include "../md2gba_gba_projected.c"

void md2gba_gba_stop_hblank_scroll_dma(void){}
void md2gba_gba_scroll_latch_clear(void){}
unsigned md2gba_vdp_plane_width(const Md2GbaVdp*v){return (v->regs[16]&3u)==1u?64u:32u;}
unsigned md2gba_vdp_plane_height(const Md2GbaVdp*v){return ((v->regs[16]>>4)&3u)==1u?64u:32u;}
uint16_t md2gba_vdp_plane_a_base(const Md2GbaVdp*v){return (uint16_t)((v->regs[2]&0x38u)<<10);}
uint16_t md2gba_vdp_plane_b_base(const Md2GbaVdp*v){return (uint16_t)((v->regs[4]&7u)<<13);}
unsigned md2gba_vdp_sat_base(const Md2GbaVdp*v){return (unsigned)((v->regs[5]&0x7fu)<<9);}
int md2gba_vdp_hscroll_line(const Md2GbaVdp*v,unsigned p,unsigned line,int16_t*out){(void)line;*out=v->present_hscroll[p&1u];return 0;}
int md2gba_vdp_vscroll_plane(const Md2GbaVdp*v,unsigned p,int16_t*out){*out=v->present_vscroll[p&1u];return 0;}
int md2gba_vdp_vram_range_dirty(const Md2GbaVdp*v,uint16_t start,unsigned len){unsigned done;for(done=0;done<len;done+=32u){unsigned b=((uint16_t)(start+done))>>5;if(v->vram_dirty_tiles[b>>5]&(1u<<(b&31u)))return 1;}return 0;}
void md2gba_vdp_clear_dirty(Md2GbaVdp*v){unsigned i;for(i=0;i<64;i++)v->vram_dirty_tiles[i]=0;for(i=0;i<8;i++)v->vram_dirty_pages[i]=0;v->cram_dirty_mask[0]=v->cram_dirty_mask[1]=v->vsram_dirty_mask[0]=v->vsram_dirty_mask[1]=0;v->regs_dirty_mask=0;}
static void namew(Md2GbaVdp*v,unsigned base,unsigned i,uint16_t n){unsigned a=base+2u*i;v->vram[(uint16_t)a]=(uint8_t)(n>>8);v->vram[(uint16_t)(a+1u)]=(uint8_t)n;}
static void tile(Md2GbaVdp*v,unsigned t,unsigned c){unsigned i;uint8_t b=(uint8_t)((c<<4)|c);for(i=0;i<32;i++)v->vram[t*32u+i]=b;}
static unsigned ceil_div_u(unsigned a,unsigned b){return (a+b-1u)/b;}
static int ps4_ui_scan_matches_indexed(const Md2GbaVdp*v){
    uint16_t boxes[6][4];unsigned n=md2gba_profile_ps4_projected_ui_scan(v,0,0,40u,28u,boxes),i;
    for(i=0u;i<=n;i++){
        unsigned sx,sy,w,h,found=md2gba_profile_ps4_projected_ui_native_block(v,i,0,0,40u,28u,&sx,&sy,&w,&h);
        if(found!=(i<n))return 0;
        if(found&&(boxes[i][0]!=sx||boxes[i][1]!=sy||boxes[i][2]!=w||boxes[i][3]!=h))return 0;
    }
    return 1;
}
static unsigned projected_pixel(unsigned layer,unsigned x,unsigned y){
    unsigned sb=PROJ_SB_FIRST+(PS->map_bank&1u)*4u+layer,cell=((y>>3)&31u)*32u+((x>>3)&31u);
    unsigned slot=BG_VRAM16[sb*1024u+cell]&0x3ffu;
    volatile uint8_t *v=(volatile uint8_t*)0x06000000u;
    return v[slot*64u+(y&7u)*8u+(x&7u)];
}
static unsigned projected_screen_pixel(unsigned layer,unsigned x,unsigned y){
    unsigned hx=layer==0u?REG_BG0HOFS:layer==1u?REG_BG1HOFS:layer==2u?REG_BG2HOFS:REG_BG3HOFS;
    unsigned vy=layer==0u?REG_BG0VOFS:layer==1u?REG_BG1VOFS:layer==2u?REG_BG2VOFS:REG_BG3VOFS;
    return projected_pixel(layer,(x+hx)&255u,(y+vy)&255u);
}
/* Frozen P134O sampler: compare the optimized wide dialogue path pixel for
   pixel, including mixed flips, palettes, wrapped names and source widths. */
static void wide_tile_reference(const Md2GbaVdp*v,const ProjUiPanel*p,unsigned ox,unsigned oy,uint8_t out[64]){
    unsigned outw=p->w>30u?30u:p->w,py,px;
    for(py=0u;py<8u;py++)for(px=0u;px<8u;px++){
        unsigned gx=ox*8u+px,sxp;uint16_t n;unsigned lx;uint8_t c;
        if(gx<8u)sxp=gx;
        else if(gx>=(outw-1u)*8u)sxp=((unsigned)p->w-1u)*8u+(gx-(outw-1u)*8u);
        else {unsigned op=gx-8u,src_inner=((unsigned)p->w-2u)*8u;
              sxp=8u+(op*src_inner+112u)/224u;if(sxp>=((unsigned)p->w-1u)*8u)sxp=((unsigned)p->w-1u)*8u-1u;}
        n=ui_name_word(v,PS->plane_base[0],PS->plane_w,PS->plane_h,(int32_t)p->sx+(int32_t)(sxp>>3),(int32_t)p->sy+(int32_t)oy);lx=sxp&7u;
        if(n&0x0800u)lx=7u-lx;{unsigned ly=(n&0x1000u)?7u-py:py;c=src_px(v,n&0x7ffu,lx,ly);}
        out[py*8u+px]=c?(uint8_t)((((n>>13)&3u)*16u)+c):0u;
    }
}
int main(void){
    void*m=mmap((void*)0x04000000u,0x04000000u,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0);Md2GbaVdp v;unsigned i;
    if(m==MAP_FAILED){perror("mmap");return 99;}memset(&v,0,sizeof(v));v.regs[2]=0x10u;v.regs[4]=3u;v.regs[5]=0x38u;v.regs[16]=0u;v.regs[7]=0u;v.regs[12]=0x81u;v.present_scroll_valid=1u;
    tile(&v,1u,1u);tile(&v,2u,2u);for(i=0;i<1024u;i++){namew(&v,0x4000u,i,1u);namew(&v,0x6000u,i,(uint16_t)(0x8000u|2u));}for(i=0;i<64u;i++)v.cram_gba[i]=(uint16_t)i;
    if(md2gba_gba_projected_present(&v)){puts("FAIL initial present");return 1;}if(!PS->initialized||PS->pending){puts("FAIL initial state");return 2;}if((REG_BG0CNT&0x0080u)==0u||((REG_BG0CNT>>8)&31u)!=24u||((REG_BG3CNT>>8)&31u)!=27u){printf("FAIL BG config %04x %04x\n",REG_BG0CNT,REG_BG3CNT);return 3;}
    if((REG_DISPCNT&0x1f00u)!=0x1f00u){printf("FAIL DISPCNT %04x\n",REG_DISPCNT);return 4;}
    md2gba_projected_test_bg_builds=md2gba_projected_test_bg_source_checks=0u;
    v.present_hscroll[0]=-12;v.present_scroll_epoch++;if(md2gba_gba_projected_present(&v)){puts("FAIL scroll prepare");return 5;}if(!PS->pending||PS->map_write_count==0u||PS->map_write_count>170u){printf("FAIL strip queue pending=%u writes=%u\n",PS->pending,PS->map_write_count);return 6;}
    if(PS->map_write_count!=21u){printf("FAIL P134U logical-retire edge writes=%u expect=21\n",PS->map_write_count);return 7;}
    if(md2gba_projected_test_bg_source_checks!=0u||md2gba_projected_test_bg_builds>48u){printf("FAIL edge-only crossing builds=%u scans=%u\n",md2gba_projected_test_bg_builds,md2gba_projected_test_bg_source_checks);return 70;}
    md2gba_gba_projected_vblank_commit_pending();if(PS->pending||PS->origin_x[0]!=1){printf("FAIL strip commit pending=%u ox=%ld\n",PS->pending,(long)PS->origin_x[0]);return 8;}
    /* P134U retires the offscreen ring cell logically instead of spending a
       queued VRAM-zero write. The stale bit guarantees an exact overwrite if
       the cell later re-enters with transparent output. */
    if(PS->active_slot[2u][0u]!=0u||!(PS->stale_cell_mask[2u][0u]&(1u<<0))){printf("FAIL logical retire slot=%u stale=%08x\n",PS->active_slot[2u][0u],PS->stale_cell_mask[2u][0u]);return 8;}
    md2gba_projected_test_bg_builds=md2gba_projected_test_bg_source_checks=0u;
    v.present_hscroll[0]=-13;v.present_scroll_epoch++;if(md2gba_gba_projected_present(&v)){puts("FAIL fine scroll");return 9;}if(PS->pending){puts("FAIL fine scroll queued");return 10;}if(REG_BG0HOFS!=(uint16_t)qx(13)){printf("FAIL fine HOFS %u\n",REG_BG0HOFS);return 11;}
    if(md2gba_projected_test_bg_builds||md2gba_projected_test_bg_source_checks){printf("FAIL fine-scroll world work builds=%u scans=%u\n",md2gba_projected_test_bg_builds,md2gba_projected_test_bg_source_checks);return 12;}
    /* An offscreen Plane-A name edit should still be tracked by the world,
       while the 40x28 UI recognizer has no changed name to inspect. */
    {unsigned off=30u*32u+5u,a=0x4000u+2u*off,blk=a>>5;
     namew(&v,0x4000u,off,3u);v.vram_dirty_tiles[blk>>5]|=1u<<(blk&31u);v.vram_epoch++;
     md2gba_projected_test_ui_scans=0u;
     if(md2gba_gba_projected_present(&v)||md2gba_projected_test_ui_scans){puts("FAIL offscreen map forced UI scan");return 252;}
     if(PS->pending)md2gba_gba_projected_vblank_commit_pending();
     off=1u*32u+5u;a=0x4000u+2u*off;blk=a>>5;
     namew(&v,0x4000u,off,3u);v.vram_dirty_tiles[blk>>5]|=1u<<(blk&31u);v.vram_epoch++;
     if(md2gba_gba_projected_present(&v)||!md2gba_projected_test_ui_scans){puts("FAIL visible map skipped UI scan");return 253;}
     if(PS->pending)md2gba_gba_projected_vblank_commit_pending();
     namew(&v,0x4000u,off,1u);v.vram_dirty_tiles[blk>>5]|=1u<<(blk&31u);v.vram_epoch++;
     if(md2gba_gba_projected_present(&v)){puts("FAIL visible map restore");return 254;}
     if(PS->pending)md2gba_gba_projected_vblank_commit_pending();}
    /* Walking commonly dirties SAT/OAM with no world mutation. That path must
       rebuild OAM only; P134F incorrectly walked all 2,604 resident BG cells. */
    {unsigned sat=md2gba_vdp_sat_base(&v),blk=sat>>5;md2gba_projected_test_bg_builds=md2gba_projected_test_bg_source_checks=md2gba_projected_test_map_range_checks=md2gba_projected_test_queue_mask_clears=0u;
     v.vram[(uint16_t)sat]^=1u;v.vram_dirty_tiles[blk>>5]|=1u<<(blk&31u);v.vram_epoch++;
     if(md2gba_gba_projected_present(&v)){puts("FAIL SAT-only present");return 13;}
     if(md2gba_projected_test_bg_builds||md2gba_projected_test_bg_source_checks||md2gba_projected_test_map_range_checks){printf("FAIL SAT-only world work builds=%u scans=%u mapchecks=%u\n",md2gba_projected_test_bg_builds,md2gba_projected_test_bg_source_checks,md2gba_projected_test_map_range_checks);return 14;}
     if(md2gba_projected_test_queue_mask_clears){puts("FAIL SAT-only queue mask clear");return 245;}
     if(PS->pending)md2gba_gba_projected_vblank_commit_pending();}
    /* Forty target-tile crossings wrap the 32x32 hardware map. The resident
       color-1 tile must stay bounded at exactly 31x21 references rather than
       accumulating stale off-window references. */
    for(unsigned n=2u;n<=40u;n++){
        unsigned cam=ceil_div_u(32u*n,3u);v.present_hscroll[0]=(int16_t)-(int)cam;v.present_scroll_epoch++;
        if(md2gba_gba_projected_present(&v)){printf("FAIL walk prepare n=%u\n",n);return 12;}
        if(PS->pending)md2gba_gba_projected_vblank_commit_pending();
        if(PS->origin_x[0]!=(int32_t)n){printf("FAIL walk origin n=%u got=%ld cam=%u qx=%ld\n",n,(long)PS->origin_x[0],cam,(long)qx((int32_t)cam));return 13;}
        if(PS->capacity_failed){printf("FAIL capacity leak n=%u\n",n);return 14;}
    }
    {unsigned slot=BG_VRAM16[(PROJ_SB_FIRST+2u)*1024u+((PS->origin_y[0]&31u)<<5)+(PS->origin_x[0]&31u)];
     if(!slot||PS->slot_ref[slot]!=PROJ_COVER_W*PROJ_COVER_H){printf("FAIL bounded refs slot=%u refs=%u expect=%u\n",slot,slot?PS->slot_ref[slot]:0u,PROJ_COVER_W*PROJ_COVER_H);return 15;}}

    /* Independent nearest-neighbor proof: use cell-colored source patterns and
       compare every one of the 38,400 output pixels against floor(4x/3),
       floor(7y/5). This is the declared 320x224 -> 240x160 sampling rule. */
    md2gba_gba_projected_reset();memset(&v,0,sizeof(v));v.regs[2]=0x10u;v.regs[4]=3u;v.regs[5]=0x38u;v.regs[16]=1u;v.regs[7]=0u;v.regs[12]=0x81u;v.present_scroll_valid=1u;
    for(i=1u;i<=15u;i++)tile(&v,i,i);tile(&v,0u,0u);
    for(unsigned y=0;y<32u;y++)for(unsigned x=0;x<64u;x++){unsigned c=1u+((x+3u*y)%15u);namew(&v,0x4000u,y*64u+x,(uint16_t)c);namew(&v,0x6000u,y*64u+x,0u);}
    for(i=0;i<64u;i++)v.cram_gba[i]=(uint16_t)i;
    if(md2gba_gba_projected_present(&v)){puts("FAIL scaling present");return 16;}
    for(unsigned y=0;y<160u;y++)for(unsigned x=0;x<240u;x++){
        unsigned sx=(4u*x)/3u,sy=(7u*y)/5u,c=1u+(((sx>>3)+3u*(sy>>3))%15u),got=projected_pixel(2u,x,y);
        if(got!=c){printf("FAIL scaling xy=%u,%u src=%u,%u expect=%u got=%u\n",x,y,sx,sy,c,got);return 17;}
    }
    /* P134H pattern-only dirtiness should use the per-plane dependency bloom
       before running exact source-cell checks. Dirty one of fifteen repeating
       pattern IDs and require substantially less than the full 1,302-cell
       two-plane resident walk. */
    md2gba_projected_test_bg_builds=md2gba_projected_test_bg_source_checks=0u;
    v.vram[5u*32u]^=0x11u;v.vram_dirty_tiles[5u>>5]|=1u<<(5u&31u);v.vram_epoch++;
    if(md2gba_gba_projected_present(&v)){puts("FAIL pattern-bloom present");return 171;}
    if(md2gba_projected_test_bg_source_checks>=500u){printf("FAIL pattern-bloom exact scans=%u\n",md2gba_projected_test_bg_source_checks);return 172;}
    if(PS->pending)md2gba_gba_projected_vblank_commit_pending();
    /* Name-table edits (menus/dialogue/map animation) are another high-frequency
       path. One dirty 32-byte name block must be filtered by the compact map
       dependency bloom instead of probing the full 1,302-cell two-plane ring. */
    md2gba_projected_test_bg_builds=md2gba_projected_test_bg_source_checks=0u;
    {unsigned a=0x4000u+2u*5u,blk=((uint16_t)a)>>5;namew(&v,0x4000u,5u,3u);v.vram_dirty_tiles[blk>>5]|=1u<<(blk&31u);v.vram_epoch++;}
    if(md2gba_gba_projected_present(&v)){puts("FAIL map-bloom present");return 173;}
    if(md2gba_projected_test_bg_source_checks>=400u){printf("FAIL map-bloom exact scans=%u\n",md2gba_projected_test_bg_source_checks);return 174;}
    if(PS->pending)md2gba_gba_projected_vblank_commit_pending();
    v.regs[11]=2u;if(md2gba_gba_projected_present(&v)!=-6){puts("FAIL row-scroll capability gate");return 18;}v.regs[11]=0u;
    /* P134J quality checkpoint: sprite width stays native while height is 7/8.
       A 16x24 actor becomes 16x21, preserving source endpoints and one-pixel
       facial/edge detail while correcting P134I's slightly-too-tall look. */
    {unsigned base=100u,t,x;volatile uint8_t*ov=(volatile uint8_t*)0x06010000u;
     for(t=0u;t<6u;t++)for(x=0u;x<32u;x++)v.vram[(base+t)*32u+x]=0u;
     /* bottom-right source pixel x=15,y=23 and a small interior detail */
     {unsigned st=base+1u*3u+2u,off=st*32u+7u*4u+3u;v.vram[off]|=0x07u;}
     {unsigned st=base+0u*3u+0u,off=st*32u+5u*4u+3u;v.vram[off]|=0x03u;}
     build_obj_pixels(&v,0u,0u,base,2u,3u,0u,0u,16u,21u,2u,4u,obj_ymap_small[2]);
     {unsigned byte_off=(2u*2u+1u)*32u+4u*4u+3u;if((ov[byte_off]>>4)!=7u){printf("FAIL 7/8 sprite edge got=%u\n",ov[byte_off]>>4);return 19;}}
     {unsigned byte_off=0u*32u+4u*4u+3u;if((ov[byte_off]>>4)!=3u){printf("FAIL 7/8 sprite detail got=%u\n",ov[byte_off]>>4);return 190;}}
     {int px,py;quality_obj_position(40,35,0,0,16u,24u,16u,21u,&px,&py);if(px!=28||py!=21){printf("FAIL quality anchor px=%d py=%d\n",px,py);return 204;}}}
    /* P134O: native/detached UI owns text readability. The world projector is
       back to exact nearest-neighbour sampling and must not dilate sparse
       high-priority world pixels into neighbouring output samples. */
    {uint8_t hi[64],lo[64];uint32_t bloom;unsigned y;memset(&v,0,sizeof(v));PS->plane_w=PS->plane_h=32u;PS->plane_base[0]=0x4000u;
     for(y=0u;y<8u;y++){unsigned a=1u*32u+y*4u+1u;v.vram[a]=(uint8_t)(v.vram[a]|0x01u);} /* source x=3 */
     for(i=0u;i<1024u;i++)namew(&v,0x4000u,i,(uint16_t)(0x8000u|1u));
     {uint8_t mb;make_bg_tile_pair(&v,0u,0,0,hi,lo,&bloom,&mb);}if(hi[2u]!=0u){puts("FAIL world sparse dilation still active");return 191;}}
    /* Per-bank per-SAT-entry cache: after both banks have seen two actors,
       changing actor 0 again must rebuild actor 0 only, not actor 1. */
    {unsigned sat,blk;memset(&v,0,sizeof(v));memset((void*)PS,0,sizeof(*PS));PS->magic=PROJ_MAGIC;PS->initialized=1u;PS->plane_w=PS->plane_h=32u;PS->plane_base[0]=0x4000u;v.regs[5]=0x38u;v.present_scroll_valid=1u;sat=md2gba_vdp_sat_base(&v);
     for(i=100u;i<116u;i++)tile(&v,i,(i&15u)?(i&15u):1u);
     /* SAT entry 0 -> 1, entry 1 -> end; both 2x3 tiles and visible. */
     namew(&v,sat,0u,(uint16_t)(128u+40u));v.vram[(uint16_t)(sat+2u)]=0x06u;v.vram[(uint16_t)(sat+3u)]=1u;namew(&v,sat,2u,(uint16_t)(0x8000u|100u));namew(&v,sat,3u,(uint16_t)(128u+40u));
     namew(&v,sat+8u,0u,(uint16_t)(128u+70u));v.vram[(uint16_t)(sat+10u)]=0x06u;v.vram[(uint16_t)(sat+11u)]=0u;namew(&v,sat+8u,2u,(uint16_t)(0x8000u|110u));namew(&v,sat+8u,3u,(uint16_t)(128u+70u));
     md2gba_projected_test_obj_builds=0u;if(stage_sprites(&v,1)){puts("FAIL sprite cache initial");return 192;}if(md2gba_projected_test_obj_builds!=2u){printf("FAIL sprite initial builds=%u\n",md2gba_projected_test_obj_builds);return 193;}
     blk=100u;v.vram_dirty_tiles[blk>>5]|=1u<<(blk&31u);md2gba_projected_test_obj_builds=0u;if(stage_sprites(&v,1)){puts("FAIL sprite cache bank1");return 194;}if(md2gba_projected_test_obj_builds!=2u){printf("FAIL sprite first alternate builds=%u\n",md2gba_projected_test_obj_builds);return 195;}
     memset(v.vram_dirty_tiles,0,sizeof(v.vram_dirty_tiles));v.vram_dirty_tiles[blk>>5]|=1u<<(blk&31u);md2gba_projected_test_obj_builds=0u;if(stage_sprites(&v,1)){puts("FAIL sprite cache bank0");return 196;}if(md2gba_projected_test_obj_builds!=1u){printf("FAIL sprite selective rebuilds=%u\n",md2gba_projected_test_obj_builds);return 197;}}
    /* Visibility/topology changes can repack later actors to a new destination
       tile offset. Matching source graphics alone must not reuse stale pixels. */
    {unsigned sat;memset(&v,0,sizeof(v));memset((void*)PS,0,sizeof(*PS));PS->magic=PROJ_MAGIC;PS->initialized=1u;PS->plane_w=PS->plane_h=32u;PS->plane_base[0]=0x4000u;v.regs[5]=0x38u;v.present_scroll_valid=1u;sat=md2gba_vdp_sat_base(&v);
     for(i=140u;i<156u;i++)tile(&v,i,(i&15u)?(i&15u):1u);
     namew(&v,sat,0u,(uint16_t)(128u+30u));v.vram[(uint16_t)(sat+2u)]=0x06u;v.vram[(uint16_t)(sat+3u)]=1u;namew(&v,sat,2u,(uint16_t)(0x8000u|140u));namew(&v,sat,3u,(uint16_t)(128u+30u));
     namew(&v,sat+8u,0u,(uint16_t)(128u+60u));v.vram[(uint16_t)(sat+10u)]=0x06u;v.vram[(uint16_t)(sat+11u)]=0u;namew(&v,sat+8u,2u,(uint16_t)(0x8000u|150u));namew(&v,sat+8u,3u,(uint16_t)(128u+60u));
     md2gba_projected_test_obj_builds=0u;if(stage_sprites(&v,1)||md2gba_projected_test_obj_builds!=2u){printf("FAIL pack seed builds=%u\n",md2gba_projected_test_obj_builds);return 205;}
     /* Fill alternate bank too. */md2gba_projected_test_obj_builds=0u;PS->sprite_bank_key[PS->active_obj_bank][0]=0u;if(stage_sprites(&v,1)){puts("FAIL pack alt");return 206;}
     /* Move entry 0 fully offscreen. Entry 1 shifts to tile offset zero and must
        rebuild in the selected bank even though its descriptor is unchanged. */
     namew(&v,sat,3u,(uint16_t)(128u+400u));md2gba_projected_test_obj_builds=0u;if(stage_sprites(&v,1)){puts("FAIL pack shift");return 207;}if(md2gba_projected_test_obj_builds<1u){puts("FAIL stale packed sprite reuse");return 208;}if(PS->sprite_bank_tile[PS->active_obj_bank][1]!=0u){printf("FAIL packed tile offset=%u\n",PS->sprite_bank_tile[PS->active_obj_bank][1]);return 209;}}
    /* Fixed 7/8-height sprites have no 5/7 pixel-height variant. Movement across
       every Y phase must remain eligible for OAM-only updates. */
    {unsigned sat;memset(&v,0,sizeof(v));memset((void*)PS,0,sizeof(*PS));PS->magic=PROJ_MAGIC;PS->initialized=1u;PS->plane_w=PS->plane_h=32u;PS->plane_base[0]=0x4000u;v.regs[5]=0x38u;v.present_scroll_valid=1u;sat=md2gba_vdp_sat_base(&v);
     for(i=120u;i<126u;i++)tile(&v,i,(i&15u)?(i&15u):1u);namew(&v,sat,0u,(uint16_t)(128u+35u));v.vram[(uint16_t)(sat+2u)]=0x06u;v.vram[(uint16_t)(sat+3u)]=0u;namew(&v,sat,2u,(uint16_t)(0x8000u|120u));namew(&v,sat,3u,(uint16_t)(128u+40u));
     md2gba_projected_test_obj_builds=0u;if(stage_sprites(&v,1)||md2gba_projected_test_obj_builds!=1u){printf("FAIL quality sprite seed builds=%u\n",md2gba_projected_test_obj_builds);return 198;}
     for(unsigned yy=36u;yy<=42u;yy++){namew(&v,sat,0u,(uint16_t)(128u+yy));md2gba_projected_test_obj_builds=0u;if(!stage_sprite_positions_only(&v)){printf("FAIL quality position-only y=%u\n",yy);return 199;}if(md2gba_projected_test_obj_builds){printf("FAIL quality movement reconvert=%u\n",md2gba_projected_test_obj_builds);return 200;}}}
    /* P134L: a full/large camera transition prepares the alternate screenblock
       bank while the current projected frame stays enabled. The bank switch is
       deferred to VBlank; GBA forced blank (bit 7) must never be asserted. */
    {unsigned oldbank;
     md2gba_gba_projected_reset();memset(&v,0,sizeof(v));v.regs[2]=0x10u;v.regs[4]=3u;v.regs[5]=0x38u;v.regs[16]=1u;v.regs[7]=0u;v.regs[12]=0x81u;v.present_scroll_valid=1u;
     tile(&v,1u,1u);tile(&v,2u,2u);for(i=0u;i<2048u;i++){namew(&v,0x4000u,i,1u);namew(&v,0x6000u,i,(uint16_t)(0x8000u|2u));}for(i=0;i<64u;i++)v.cram_gba[i]=(uint16_t)i;
     if(md2gba_gba_projected_present(&v)){puts("FAIL transaction seed");return 217;}oldbank=PS->map_bank;
     v.present_hscroll[0]=-96;v.present_hscroll[1]=-96;v.present_scroll_epoch++;
     if(md2gba_gba_projected_present(&v)){puts("FAIL transaction prepare");return 218;}
     if(!PS->pending||!PS->bank_switch_pending||PS->map_bank!=oldbank||(REG_DISPCNT&0x0080u)){printf("FAIL transaction visibility pending=%u switch=%u bank=%u/%u disp=%04x\n",PS->pending,PS->bank_switch_pending,PS->map_bank,oldbank,REG_DISPCNT);return 219;}
     if(((REG_BG0CNT>>8)&31u)!=(PROJ_SB_FIRST+oldbank*4u)){printf("FAIL old map not held bg0=%u\n",(REG_BG0CNT>>8)&31u);return 220;}
     md2gba_gba_projected_vblank_commit_pending();if(PS->map_bank==oldbank||!PS->rebase_cleanup||(REG_DISPCNT&0x0080u)){printf("FAIL transaction commit bank=%u cleanup=%u disp=%04x\n",PS->map_bank,PS->rebase_cleanup,REG_DISPCNT);return 221;}
     if(((REG_BG0CNT>>8)&31u)!=(PROJ_SB_FIRST+PS->map_bank*4u)){printf("FAIL new map switch bg0=%u bank=%u\n",(REG_BG0CNT>>8)&31u,PS->map_bank);return 222;}
     if(md2gba_gba_projected_present(&v)||PS->rebase_cleanup){puts("FAIL transaction cleanup");return 223;}}
    /* Wide 34-tile dialogue must use the gentler 34->30 UI transform: borders
       remain native and the font keeps all eight vertical source pixels. A
       one-pixel baseline at source y=7 therefore survives exactly; the world
       5/7 vertical projection would erase/move that detail. */
    {ProjUiPanel pp[PROJ_UI_MAX_PANELS];uint32_t usig;unsigned n,xx,yy,pw=34u,ph=6u,sx=3u,sy=20u;
     md2gba_gba_projected_reset();memset(&v,0,sizeof(v));v.regs[2]=0x10u;v.regs[4]=3u;v.regs[5]=0x38u;v.regs[16]=1u;v.regs[12]=0x81u;v.present_scroll_valid=1u;
     tile(&v,1u,1u);tile(&v,2u,2u);tile(&v,3u,3u);tile(&v,4u,4u);for(i=0u;i<2048u;i++){namew(&v,0x4000u,i,1u);namew(&v,0x6000u,i,0u);}
     /* glyph/detail tile: only the final scanline is ink */for(xx=0u;xx<32u;xx++)v.vram[5u*32u+xx]=0u;for(xx=0u;xx<4u;xx++)v.vram[5u*32u+7u*4u+xx]=0x77u;
     for(xx=0u;xx<pw;xx++){uint16_t top=(uint16_t)(0xC000u|(xx==0u?2u:xx==pw-1u?(0x0800u|2u):3u));uint16_t bot=(uint16_t)(0xD000u|(xx==0u?2u:xx==pw-1u?(0x0800u|2u):3u));namew(&v,0x4000u,sy*64u+sx+xx,top);namew(&v,0x4000u,(sy+ph-1u)*64u+sx+xx,bot);}
     for(yy=1u;yy<ph-1u;yy++)for(xx=0u;xx<pw;xx++){uint16_t q=(uint16_t)(0xC000u|(xx==0u?4u:xx==pw-1u?(0x0800u|4u):5u));namew(&v,0x4000u,(sy+yy)*64u+sx+xx,q);}
     for(i=0u;i<64u;i++)v.cram_gba[i]=(uint16_t)i;if(md2gba_gba_projected_present(&v)){puts("FAIL wide dialogue present");return 224;}n=scan_fixed_ui_panels(&v,pp,&usig);if(n!=1u||pp[0].w!=34u||ui_panel_out_w(&pp[0])!=30u){printf("FAIL wide dialogue detect n=%u sw=%u ow=%u\n",n,n?pp[0].w:0u,n?ui_panel_out_w(&pp[0]):0u);return 225;}
     {unsigned px=120u,py=(unsigned)pp[0].oy+2u*8u+7u,got=projected_screen_pixel(0u,px,py);if(got!=(32u+7u)){printf("FAIL wide dialogue baseline got=%u at %u,%u oy=%d\n",got,px,py,pp[0].oy);return 226;}}
     if((REG_DISPCNT&0x0080u)!=0u){puts("FAIL wide dialogue forced blank");return 227;}}
    /* P134K: camera-aware framed menu/status panels must remain native 8x8
       detail even when Plane A has normal field scroll. P134J returned zero as
       soon as camera X/Y was nonzero, which is the exact runtime failure seen in
       the user's ITEM/TECH/SKILL screenshot. */
    {ProjUiPanel pp[PROJ_UI_MAX_PANELS];uint32_t usig;unsigned n,pw=12u,ph=6u,xx,yy;int32_t cax=24,cay=80,stx=floorpow2(cax,3u),sty=floorpow2(cay,3u);unsigned sx=(unsigned)(stx+4),sy=(unsigned)(sty+2);
     md2gba_gba_projected_reset();memset(&v,0,sizeof(v));v.regs[2]=0x10u;v.regs[4]=3u;v.regs[5]=0x38u;v.regs[16]=1u;v.regs[12]=0x81u;v.present_scroll_valid=1u;v.present_hscroll[0]=(int16_t)-cax;v.present_vscroll[0]=(int16_t)cay;
     for(i=1u;i<=8u;i++)tile(&v,i,i);for(yy=0u;yy<32u;yy++)for(xx=0u;xx<64u;xx++){namew(&v,0x4000u,yy*64u+xx,1u);namew(&v,0x6000u,yy*64u+xx,0u);}
     /* Real PSIV field menus mix priority inside a single framed window.  Make
        the top edge low-priority, alternate side priority by row, and keep only
        the glyph high-priority.  P134K/L's all-priority detector misses this. */
     for(xx=0u;xx<pw;xx++){uint16_t top=(uint16_t)((xx==0u?0x8000u:0u)|(xx==0u?2u:xx==pw-1u?(0x0800u|2u):3u));uint16_t bot=(uint16_t)((xx==pw-1u?0x8000u:0u)|0x1000u|(xx==0u?2u:xx==pw-1u?(0x0800u|2u):3u));namew(&v,0x4000u,sy*64u+sx+xx,top);namew(&v,0x4000u,(sy+ph-1u)*64u+sx+xx,bot);}
     for(yy=1u;yy<ph-1u;yy++)for(xx=0u;xx<pw;xx++){uint16_t pri=((yy+xx)&1u)?0x8000u:0u;uint16_t q=(uint16_t)(pri|(xx==0u?4u:xx==pw-1u?(0x0800u|4u):5u));namew(&v,0x4000u,(sy+yy)*64u+sx+xx,q);}
     tile(&v,6u,2u);{unsigned a=6u*32u+7u*4u+3u;v.vram[a]=(uint8_t)((v.vram[a]&0xf0u)|7u);}namew(&v,0x4000u,(sy+2u)*64u+sx+5u,(uint16_t)(0xA000u|6u));
     for(i=0u;i<64u;i++)v.cram_gba[i]=(uint16_t)i;if(md2gba_gba_projected_present(&v)){puts("FAIL camera-native UI present");return 210;}n=scan_fixed_ui_panels(&v,pp,&usig);if(n!=1u||PS->ui_count!=1u){printf("FAIL camera UI detect n=%u state=%u\\n",n,PS->ui_count);return 211;}
     {unsigned px=(unsigned)(pp[0].ox+5*8+7),py=(unsigned)(pp[0].oy+2*8+7),got=projected_screen_pixel(0u,px,py);if(got!=(16u+7u)){printf("FAIL camera-native UI detail got=%u screen=%u,%u origin=%d,%d map=%u,%u\\n",got,px,py,pp[0].ox,pp[0].oy,pp[0].dx,pp[0].dy);return 212;}}
     /* Moving a camera a few source pixels need not resample the entire
        world if its source viewport and native UI map cells remain fixed. */
     {unsigned c,found=0u,oldbank=PS->map_bank;
      for(c=(unsigned)cax+1u;c<(unsigned)cax+8u;c++){
          ProjUiPanel q=PS->ui_panel[0];int32_t ncx=(int32_t)c;
          ui_place_native(&q,ncx,cay);
          if(floorpow2(ncx,3u)==PS->ui_source_view_x && q.dx==PS->ui_panel[0].dx &&
             q.dy==PS->ui_panel[0].dy && qx(ncx)!=(int32_t)PS->scroll[0]){
              v.present_hscroll[0]=(int16_t)-ncx;v.present_scroll_epoch++;
              md2gba_projected_test_bg_builds=0u;
              if(md2gba_gba_projected_present(&v)||md2gba_projected_test_bg_builds||PS->bank_switch_pending||PS->map_bank!=oldbank){puts("FAIL unchanged UI scroll rebuilt world");return 246;}
              if(PS->pending)md2gba_gba_projected_vblank_commit_pending();
              {const ProjUiPanel*now=&PS->ui_panel[0];unsigned px=(unsigned)(now->ox+5*8+7),py=(unsigned)(now->oy+2*8+7);
               if(projected_screen_pixel(0u,px,py)!=23u){puts("FAIL unchanged UI scroll native detail");return 247;}}
              found=1u;break;
          }
      }
      if(!found){puts("FAIL UI fine-scroll fixture has no stable layout");return 248;}}
    }
    /* PSIV portrait metadata is a 6x6 / 48x48 Plane-A block at $55C..$57F.
       It must be copied at native resolution, not the old 36x34 world sample.
       Preserve a single 'eye' pixel to prove no 5/7 or 3/4 minification occurs. */
    {ProjUiPanel pp[PROJ_UI_MAX_PANELS];uint32_t usig;unsigned n,xx,yy,t=0x55cu;int32_t cax=24,cay=104,stx=floorpow2(cax,3u),sty=floorpow2(cay,3u);unsigned psx=(unsigned)(stx+5),psy=(unsigned)(sty+13);
     md2gba_gba_projected_reset();memset(&v,0,sizeof(v));v.regs[2]=0x10u;v.regs[4]=3u;v.regs[5]=0x38u;v.regs[16]=1u;v.regs[12]=0x81u;v.present_scroll_valid=1u;v.present_hscroll[0]=(int16_t)-cax;v.present_vscroll[0]=(int16_t)cay;
     tile(&v,1u,1u);for(yy=0u;yy<32u;yy++)for(xx=0u;xx<64u;xx++){namew(&v,0x4000u,yy*64u+xx,1u);namew(&v,0x6000u,yy*64u+xx,0u);}
     for(yy=0u;yy<6u;yy++)for(xx=0u;xx<6u;xx++,t++){tile(&v,t,2u);namew(&v,0x4000u,((psy+yy)&31u)*64u+((psx+xx)&63u),(uint16_t)(0xC000u|t));}
     {unsigned a=0x55cu*32u+3u*4u+2u;v.vram[a]=(uint8_t)((v.vram[a]&0x0fu)|(7u<<4));} /* source x4,y3 */
     for(i=0u;i<64u;i++)v.cram_gba[i]=(uint16_t)i;if(md2gba_gba_projected_present(&v)){puts("FAIL native portrait present");return 213;}n=scan_fixed_ui_panels(&v,pp,&usig);if(n!=1u||pp[0].w!=6u||pp[0].h!=6u){printf("FAIL native portrait detect n=%u wh=%u,%u\\n",n,n?pp[0].w:0u,n?pp[0].h:0u);return 214;}
     if(!ps4_ui_scan_matches_indexed(&v)){puts("FAIL portrait scan enumeration");return 243;}
     {unsigned px=(unsigned)(pp[0].ox+4),py=(unsigned)(pp[0].oy+3),got=projected_screen_pixel(0u,px,py);if(got!=(32u+7u)){printf("FAIL native portrait eye got=%u at %u,%u origin=%d,%d\\n",got,px,py,pp[0].ox,pp[0].oy);return 215;}}
     if((unsigned)pp[0].w*8u!=48u||(unsigned)pp[0].h*8u!=48u){puts("FAIL portrait not 48x48");return 216;}}
    /* P134N: source UI construction is atomic. The first authored WindowTiles
       cell must freeze the last clean projected world instead of entering the
       world minifier. Once the complete PSIV frame exists, activate only the
       UI cells (both A-high and A-low suppression) without a full-world rebuild
       or map-bank switch. */
    {unsigned sx=5u,sy=5u,pw=10u,ph=6u,xx,yy,base=0x4000u,old_epoch;
     md2gba_gba_projected_reset();memset(&v,0,sizeof(v));v.regs[2]=0x10u;v.regs[4]=3u;v.regs[5]=0x38u;v.regs[16]=1u;v.regs[12]=0x81u;v.present_scroll_valid=1u;
     tile(&v,1u,1u);tile(&v,0x6e9u,2u);tile(&v,0x6eau,3u);tile(&v,0x6f3u,4u);tile(&v,7u,5u);
     for(yy=0u;yy<32u;yy++)for(xx=0u;xx<64u;xx++){namew(&v,base,yy*64u+xx,1u);namew(&v,0x6000u,yy*64u+xx,0u);}for(i=0u;i<64u;i++)v.cram_gba[i]=(uint16_t)i;
     if(md2gba_gba_projected_present(&v)){puts("FAIL UI atomic seed");return 228;}old_epoch=PS->last_vram_epoch;
     namew(&v,base,sy*64u+sx,0x86e9u);{unsigned a=base+2u*(sy*64u+sx),blk=((uint16_t)a)>>5;v.vram_dirty_tiles[blk>>5]|=1u<<(blk&31u);}v.vram_epoch++;
     md2gba_projected_test_bg_builds=0u;if(md2gba_gba_projected_present(&v)){puts("FAIL UI candidate hold");return 229;}
     if(!PS->ui_candidate||PS->ui_count||PS->pending||PS->last_vram_epoch!=old_epoch||md2gba_projected_test_bg_builds){printf("FAIL UI hold cand=%u count=%u pend=%u epoch=%u/%u builds=%u\n",PS->ui_candidate,PS->ui_count,PS->pending,PS->last_vram_epoch,old_epoch,md2gba_projected_test_bg_builds);return 230;}
     for(xx=0u;xx<pw;xx++){namew(&v,base,sy*64u+sx+xx,(uint16_t)((xx==0u?0x06e9u:xx==pw-1u?0x0ee9u:0x06eau)));namew(&v,base,(sy+ph-1u)*64u+sx+xx,(uint16_t)(0x1000u|(xx==0u?0x06e9u:xx==pw-1u?0x0ee9u:0x06eau)));}
     for(yy=1u;yy<ph-1u;yy++)for(xx=0u;xx<pw;xx++)namew(&v,base,(sy+yy)*64u+sx+xx,(uint16_t)(xx==0u?0x06f3u:xx==pw-1u?0x0ef3u:7u));
     for(yy=sy;yy<sy+ph;yy++){unsigned a=base+2u*(yy*64u+sx),b=base+2u*(yy*64u+sx+pw-1u),blk=((uint16_t)a)>>5,blk2=((uint16_t)b)>>5;v.vram_dirty_tiles[blk>>5]|=1u<<(blk&31u);v.vram_dirty_tiles[blk2>>5]|=1u<<(blk2&31u);}v.vram_epoch++;
     md2gba_projected_test_bg_builds=0u;if(md2gba_gba_projected_present(&v)){puts("FAIL UI stable-shape hold");return 231;}
     if(PS->pending||PS->ui_count||PS->ui_candidate!=2u||md2gba_projected_test_bg_builds){printf("FAIL UI stable hold pend=%u count=%u cand=%u builds=%u\n",PS->pending,PS->ui_count,PS->ui_candidate,md2gba_projected_test_bg_builds);return 232;}
     if(md2gba_gba_projected_present(&v)){puts("FAIL UI atomic activate");return 235;}
     if(!PS->pending||PS->bank_switch_pending||PS->ui_count!=1u||PS->ui_candidate||md2gba_projected_test_bg_builds){printf("FAIL UI atomic state pend=%u switch=%u count=%u cand=%u builds=%u\n",PS->pending,PS->bank_switch_pending,PS->ui_count,PS->ui_candidate,md2gba_projected_test_bg_builds);return 236;}
     md2gba_gba_projected_vblank_commit_pending();
     if(!ps4_ui_scan_matches_indexed(&v)){puts("FAIL window scan enumeration");return 244;}
     {unsigned found=0u,cell;for(cell=0u;cell<1024u;cell++)if(ui_mask_has(PS->ui_suppress_mask,cell)){unsigned lo=BG_VRAM16[(PROJ_SB_FIRST+PS->map_bank*4u+2u)*1024u+cell]&0x3ffu;if(lo){printf("FAIL UI low ghost cell=%u slot=%u\n",cell,lo);return 233;}found=1u;break;}if(!found){puts("FAIL UI suppress mask empty");return 234;}}
     {const ProjUiPanel*p=&PS->ui_panel[0];unsigned pixel=projected_screen_pixel(0u,(unsigned)p->ox+12u,(unsigned)p->oy+12u);
      if(pixel!=5u){printf("FAIL native UI tile lost to suppressed map write pixel=%u\n",pixel);return 240;}}
    }
    /* P134O: small SAT sprites inside a detached native UI panel must use the
       panel transform rather than world 3/4 x 5/7 coordinates. This is the
       menu cursor path; preserving native row spacing prevents cumulative
       vertical offset and lets the cursor reach the final entry. */
    {int px,py;unsigned row;memset((void*)PS,0,sizeof(*PS));PS->ui_count=1u;PS->ui_panel[0].sx=4;PS->ui_panel[0].sy=3;PS->ui_panel[0].w=12;PS->ui_panel[0].h=18;PS->ui_panel[0].ox=32;PS->ui_panel[0].oy=8;
     if(!ui_sprite_native_position(40,40,8u,8u,0,0,&px,&py)||px!=40||py!=24){printf("FAIL native UI cursor map %d,%d\n",px,py);return 237;}
     /* Every authored menu row must keep the same 16-pixel spacing through the
        native panel transform, including the bottom/MACRO row.  The old world
        transform accumulated vertical error and visibly stopped lining up. */
     for(row=0u;row<7u;row++){int sy=40+(int)row*16,expect=24+(int)row*16;if(!ui_sprite_native_position(40,sy,8u,8u,0,0,&px,&py)||py!=expect){printf("FAIL native UI cursor row %u got=%d expect=%d\n",row,py,expect);return 239;}}
     if(ui_sprite_native_position(100,120,16u,24u,0,0,&px,&py)){puts("FAIL field actor classified as UI cursor");return 238;}}
    {uint8_t hi[64] __attribute__((aligned(4))),lo[64] __attribute__((aligned(4)));uint32_t dep;uint8_t map;
     md2gba_projected_test_bg_name_reads=0u;make_bg_tile_pair(&v,0u,0,0,hi,lo,&dep,&map);
     if(md2gba_projected_test_bg_name_reads!=4u){puts("FAIL two-row projection phase");return 241;}
     md2gba_projected_test_bg_name_reads=0u;make_bg_tile_pair(&v,0u,0,2,hi,lo,&dep,&map);
     if(md2gba_projected_test_bg_name_reads!=6u){puts("FAIL three-row projection phase");return 242;}}
    /* Exercise wrapped plane ranges and sparse/dense dirty blocks against the
       original full name-table scan. A missed bit would leave pixels stale. */
    {uint32_t seed=0x13579bdfu;unsigned iter,wi;
     for(iter=0u;iter<2000u;iter++){
         unsigned off,bytes;uint8_t old=0u,now;uint32_t all=0u,masked=0u;
         seed=seed*1664525u+1013904223u;PS->plane_w=(iter&1u)?64u:32u;
         PS->plane_h=(iter&2u)?64u:32u;PS->plane_base[0]=(uint16_t)((seed&2047u)*32u);
         bytes=(unsigned)PS->plane_w*PS->plane_h*2u;
         for(wi=0u;wi<64u;wi++){
             v.vram_dirty_tiles[wi]=0u;
             seed=seed*1664525u+1013904223u;PS->bg_source_mask[wi]=seed;
         }
         for(wi=0u;wi<(iter&4u?80u:2u);wi++){
             seed=seed*1664525u+1013904223u;
             v.vram_dirty_tiles[(seed>>5)&63u]|=1u<<(seed&31u);
         }
         for(off=0u;off<bytes;off+=32u)if(block_dirty(&v,(unsigned)PS->plane_base[0]+off))old|=(uint8_t)map_bloom_bit(off>>5);
         now=projected_dirty_map_bloom(&v,0u);
         if(old!=now){printf("FAIL map bloom wrap iter=%u old=%u got=%u\n",iter,old,now);return 249;}
         for(wi=0u;wi<64u;wi++)for(unsigned bit=0u;bit<32u;bit++){
             uint32_t mask=1u<<bit;
             if(v.vram_dirty_tiles[wi]&mask)all|=dep_bloom_bit(wi*32u+bit);
             if((v.vram_dirty_tiles[wi]&PS->bg_source_mask[wi])&mask)masked|=dep_bloom_bit(wi*32u+bit);
         }
         if(all!=vram_dirty_bloom_all(&v)||masked!=projected_dirty_bloom(&v)){
             printf("FAIL dirty pattern bloom iter=%u\n",iter);return 251;
         }
     }
    }
    /* Every dialogue pixel must match the previous sampler, even where one
       target tile crosses two different flipped source tiles. */
    {unsigned j,sy,sx,ox,oy,seed=0xdecafbad;ProjUiPanel p;uint8_t a[64],b[64];
     memset(&v,0,sizeof(v));PS->plane_w=PS->plane_h=64u;PS->plane_base[0]=0x4000u;
     for(j=0u;j<64u*64u;j++){seed=seed*1664525u+1013904223u;namew(&v,0x4000u,j,(uint16_t)(((seed>>13)&0xf800u)|(1u+(seed%96u))));}
     for(j=32u;j<97u;j++){unsigned k;for(k=0u;k<32u;k++){seed=seed*1664525u+1013904223u;v.vram[j*32u+k]=(uint8_t)(seed>>24);}}
     for(j=1u;j<32u;j++){unsigned k;for(k=0u;k<32u;k++){seed=seed*1664525u+1013904223u;v.vram[j*32u+k]=(uint8_t)(seed>>24);}}
     for(p.w=31u;p.w<=40u;p.w++){
         p.h=6u;for(sy=0u;sy<3u;sy++)for(sx=0u;sx<4u;sx++){
             p.sy=(int16_t)((int)sy*31-2);p.sx=(int16_t)((int)sx*21-5);
             for(oy=0u;oy<p.h;oy++)for(ox=0u;ox<30u;ox++){
                 wide_tile_reference(&v,&p,ox,oy,a);ui_panel_tile(&v,&p,ox,oy,b);
                 if(memcmp(a,b,64u)){printf("FAIL wide dialogue sample w=%u sx=%d sy=%d ox=%u oy=%u\n",p.w,p.sx,p.sy,ox,oy);return 250;}
             }
         }
     }
    }
    if(sizeof(*PS)>sizeof(md2gba_bg_tile_stage)){printf("FAIL state size %u\n",(unsigned)sizeof(*PS));return 20;}
    printf("PASS P134O projected runtime: atomic PSIV UI ownership, native menu/portrait, no low-layer ghost, transactional no-white rebuild, state=%u/%u bytes\n",(unsigned)sizeof(*PS),(unsigned)sizeof(md2gba_bg_tile_stage));return 0;
}
