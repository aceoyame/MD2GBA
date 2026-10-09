#include "md2gba_vdp_soft.h"
#include "md2gba_gba_backend.h"
#if defined(MD2GBA_GBA_IWRAM)
#define MD2GBA_SOFT_COLD __attribute__((section(".rom.cold"),noinline))
#else
#define MD2GBA_SOFT_COLD
#endif
#if defined(__GNUC__) || defined(__clang__)
#define MD2GBA_SOFT_INLINE static __attribute__((always_inline)) inline
#else
#define MD2GBA_SOFT_INLINE static inline
#endif

typedef struct SoftPixel { uint8_t color, priority, present; } SoftPixel;

MD2GBA_SOFT_INLINE uint16_t vrw(const Md2GbaVdp*v,unsigned a){return (uint16_t)(((uint16_t)v->vram[(uint16_t)a]<<8)|v->vram[(uint16_t)(a+1u)]);}

unsigned md2gba_vdp_visible_columns(const Md2GbaVdp*v){return ((v->regs[12]&0x81u)==0x81u)?40u:32u;}
unsigned md2gba_vdp_visible_rows(const Md2GbaVdp*v){return (v->regs[1]&0x08u)?30u:28u;}
uint16_t md2gba_vdp_window_base(const Md2GbaVdp*v){unsigned mask=md2gba_vdp_visible_columns(v)==40u?0x3Cu:0x3Eu;return (uint16_t)((v->regs[3]&mask)<<10);}
unsigned md2gba_vdp_window_table_width(const Md2GbaVdp*v){return md2gba_vdp_visible_columns(v)==40u?64u:32u;}
int md2gba_vdp_window_cell_active(const Md2GbaVdp*v,unsigned x,unsigned y){
    unsigned hc=md2gba_vdp_visible_columns(v),vr=md2gba_vdp_visible_rows(v),hp=(v->regs[17]&0x1Fu)*2u,vp=v->regs[18]&0x1Fu;
    int h,vv;if(x>=hc||y>=vr)return 0;if(hp>hc)hp=hc;if(vp>vr)vp=vr;
    h=(v->regs[17]&0x80u)?(x>=hp):(x<hp);
    vv=(v->regs[18]&0x80u)?(y>=vp):(y<vp);
    /* The MD window replaces Scroll A wherever either configured split selects it.
       This also lets either axis be disabled naturally with a zero left/top split. */
    return h||vv;
}
uint32_t md2gba_vdp_complex_features(const Md2GbaVdp*v){
    uint32_t f=0;unsigned hc=md2gba_vdp_visible_columns(v),vr=md2gba_vdp_visible_rows(v);
    unsigned hp=(v->regs[17]&0x1Fu)*2u,vp=v->regs[18]&0x1Fu;
    if(v->regs[11]&4u)f|=MD2GBA_VDP_COMPLEX_COLUMN_VSCROLL;
    if(v->regs[12]&8u)f|=MD2GBA_VDP_COMPLEX_SHADOW_HILIGHT;
    if(hp>hc)hp=hc;if(vp>vr)vp=vr;
    /* P79: window presence is a register-geometry property; P78 scanned up to
       every visible cell at each translated yield just to discover this bit. */
    if(((v->regs[17]&0x80u)?(hp<hc):(hp>0u)) || ((v->regs[18]&0x80u)?(vp<vr):(vp>0u)))
        f|=MD2GBA_VDP_COMPLEX_WINDOW;
    return f;
}
int md2gba_vdp_prefers_software_compositor(const Md2GbaVdp*v){return md2gba_vdp_complex_features(v)!=0u;}

MD2GBA_SOFT_INLINE uint8_t tile_pixel(const Md2GbaVdp*v,unsigned tile,unsigned x,unsigned y){unsigned a=(tile&0x7FFu)*32u+(y&7u)*4u+((x&7u)>>1);uint8_t b=v->vram[(uint16_t)a];return (x&1u)?(b&15u):(b>>4);}
static SoftPixel map_pixel(const Md2GbaVdp*v,uint16_t base,unsigned mapw,unsigned maph,int sx,int sy){
    SoftPixel p={0,0,0};unsigned px=((unsigned)sx)&(mapw*8u-1u),py=((unsigned)sy)&(maph*8u-1u);unsigned cx=px>>3,cy=py>>3;uint16_t n=vrw(v,(unsigned)base+2u*(cy*mapw+cx));unsigned tx=px&7u,ty=py&7u,c;if(n&0x0800u)tx=7u-tx;if(n&0x1000u)ty=7u-ty;c=tile_pixel(v,n&0x7FFu,tx,ty);if(!c)return p;p.color=(uint8_t)(((n>>13)&3u)*16u+c);p.priority=(uint8_t)!!(n&0x8000u);p.present=1;return p;
}
static MD2GBA_SOFT_COLD SoftPixel plane_pixel(const Md2GbaVdp*v,unsigned plane,unsigned x,unsigned y){int16_t hs=0,vs=0;unsigned w=md2gba_vdp_plane_width(v),h=md2gba_vdp_plane_height(v);uint16_t base=plane?md2gba_vdp_plane_b_base(v):md2gba_vdp_plane_a_base(v);if(w!=32u&&w!=64u&&w!=128u)w=32u;if(h!=32u&&h!=64u&&h!=128u)h=32u;(void)md2gba_vdp_hscroll_line(v,plane,y,&hs);if(v->regs[11]&4u){unsigned col=x>>4;if(md2gba_vdp_vscroll_column(v,plane,col,&vs))vs=0;}else if(md2gba_vdp_vscroll_plane(v,plane,&vs))vs=0;return map_pixel(v,base,w,h,(int)x-(int)hs,(int)y+(int)vs);}
static MD2GBA_SOFT_COLD SoftPixel window_pixel(const Md2GbaVdp*v,unsigned x,unsigned y){return map_pixel(v,md2gba_vdp_window_base(v),md2gba_vdp_window_table_width(v),32u,(int)x,(int)y);}
static MD2GBA_SOFT_COLD SoftPixel a_pixel(const Md2GbaVdp*v,unsigned x,unsigned y){if(md2gba_vdp_window_cell_active(v,x>>3,y>>3))return window_pixel(v,x,y);return plane_pixel(v,0,x,y);}

static MD2GBA_SOFT_COLD SoftPixel sprite_pixel(const Md2GbaVdp*v,unsigned x,unsigned y,unsigned want_hi,int *modifier){
    unsigned entry=0,guard=0,base=md2gba_vdp_sat_base(v);SoftPixel none={0,0,0};if(modifier)*modifier=0;
    while(guard++<80u){unsigned a=(base+entry*8u)&0xFFFFu;uint16_t yw=vrw(v,a),attr=vrw(v,a+4u),xw=vrw(v,a+6u);unsigned sz=v->vram[(uint16_t)(a+2u)],link=v->vram[(uint16_t)(a+3u)]&0x7Fu,sw=((sz>>2)&3u)+1u,sh=(sz&3u)+1u,hi=!!(attr&0x8000u);int ox=(int)(xw&0x1FFu)-128,oy=(int)(yw&0x3FFu)-128;
        if(hi==want_hi&&(int)x>=ox&&(int)x<ox+(int)sw*8&&(int)y>=oy&&(int)y<oy+(int)sh*8){unsigned lx=(unsigned)((int)x-ox),ly=(unsigned)((int)y-oy);unsigned ux=(attr&0x0800u)?sw*8u-1u-lx:lx,uy=(attr&0x1000u)?sh*8u-1u-ly:ly;unsigned tx=ux>>3,ty=uy>>3,t=(attr&0x7FFu)+tx*sh+ty,c=tile_pixel(v,t,ux&7u,uy&7u),pal=(attr>>13)&3u;if(c){unsigned ci=pal*16u+c;if((v->regs[12]&8u)&&pal==3u&&(c==14u||c==15u)){if(modifier)*modifier=(c==14u)?1:-1;return none;}none.color=(uint8_t)ci;none.priority=(uint8_t)hi;none.present=1;return none;}}
        if(!link){break;}
        entry=link;
    }
    return none;
}
static MD2GBA_SOFT_COLD SoftPixel choose_normal(const Md2GbaVdp*v,unsigned x,unsigned y,int *mod){SoftPixel sh,ah,bh,sl,al,bl;int mh=0,ml=0;sh=sprite_pixel(v,x,y,1,&mh);ah=a_pixel(v,x,y);bh=plane_pixel(v,1,x,y);sl=sprite_pixel(v,x,y,0,&ml);al=ah;bl=bh;if(mod)*mod=mh?mh:ml;if(sh.present)return sh;if(ah.present&&ah.priority)return ah;if(bh.present&&bh.priority)return bh;if(sl.present)return sl;if(al.present&&!al.priority)return al;if(bl.present&&!bl.priority)return bl;{SoftPixel bg={(uint8_t)(v->regs[7]&0x3Fu),0,1};return bg;}}
MD2GBA_SOFT_COLD uint8_t md2gba_vdp_soft_pixel(const Md2GbaVdp*v,unsigned x,unsigned y){int mod=0;SoftPixel p=choose_normal(v,x,y,&mod);unsigned intensity=MD2GBA_VDP_INTENSITY_NORMAL;if(v->regs[12]&8u){if(mod>0)intensity=MD2GBA_VDP_INTENSITY_HILIGHT;else if(mod<0)intensity=MD2GBA_VDP_INTENSITY_SHADOW;else if(!p.priority)intensity=MD2GBA_VDP_INTENSITY_SHADOW;}return (uint8_t)(p.color+(intensity==MD2GBA_VDP_INTENSITY_SHADOW?64u:intensity==MD2GBA_VDP_INTENSITY_HILIGHT?128u:0u));}
MD2GBA_SOFT_COLD uint16_t md2gba_vdp_soft_color_from_genesis(uint16_t c,unsigned intensity){unsigned r=(c>>1)&7u,g=(c>>5)&7u,b=(c>>9)&7u,rl,gl,bl;if(intensity==MD2GBA_VDP_INTENSITY_SHADOW){rl=r;gl=g;bl=b;}else if(intensity==MD2GBA_VDP_INTENSITY_HILIGHT){rl=r+7u;gl=g+7u;bl=b+7u;}else{rl=r*2u;gl=g*2u;bl=b*2u;}return (uint16_t)(((rl*31u+7u)/14u)|(((gl*31u+7u)/14u)<<5)|(((bl*31u+7u)/14u)<<10));}
static void sprite_line(const Md2GbaVdp*v,unsigned gy,unsigned ox,unsigned width,uint8_t*hc,uint8_t*hm,uint8_t*lc,uint8_t*lm){
    unsigned i,entry=0,guard=0,base=md2gba_vdp_sat_base(v);for(i=0;i<width;i++){hc[i]=hm[i]=lc[i]=lm[i]=0;}
    while(guard++<80u){unsigned a=(base+entry*8u)&0xFFFFu;uint16_t yw=vrw(v,a),attr=vrw(v,a+4u),xw=vrw(v,a+6u);unsigned sz=v->vram[(uint16_t)(a+2u)],link=v->vram[(uint16_t)(a+3u)]&0x7Fu,sw=((sz>>2)&3u)+1u,sh=(sz&3u)+1u,hi=!!(attr&0x8000u),pal=(attr>>13)&3u;int sx=(int)(xw&0x1FFu)-128,sy=(int)(yw&0x3FFu)-128;
        if((int)gy>=sy&&(int)gy<sy+(int)sh*8){unsigned ly=(unsigned)((int)gy-sy),uy=(attr&0x1000u)?sh*8u-1u-ly:ly,tx;for(tx=0;tx<sw*8u;tx++){int gx=sx+(int)tx,dx=gx-(int)ox;unsigned ux,tile,c;if(dx<0||dx>=(int)width)continue;if((hi?hc[dx]||hm[dx]:lc[dx]||lm[dx]))continue;ux=(attr&0x0800u)?sw*8u-1u-tx:tx;tile=(attr&0x7FFu)+(ux>>3)*sh+(uy>>3);c=tile_pixel(v,tile,ux&7u,uy&7u);if(!c)continue;if((v->regs[12]&8u)&&pal==3u&&(c==14u||c==15u)){(hi?hm:lm)[dx]=(uint8_t)(c==14u?1u:2u);}else{(hi?hc:lc)[dx]=(uint8_t)(pal*16u+c);}}}
        if(!link){break;}
        entry=link;
    }
}
static MD2GBA_SOFT_COLD uint8_t composed_line_pixel(const Md2GbaVdp*v,unsigned gx,unsigned gy,unsigned dx,const uint8_t*hc,const uint8_t*hm,const uint8_t*lc,const uint8_t*lm){
    SoftPixel ah=a_pixel(v,gx,gy),bh=plane_pixel(v,1,gx,gy),p;unsigned intensity=MD2GBA_VDP_INTENSITY_NORMAL,mod=0;
    if(hc[dx]){p.color=hc[dx];p.priority=1;p.present=1;}
    else if(hm[dx]){mod=hm[dx];if(ah.present&&ah.priority)p=ah;else if(bh.present&&bh.priority)p=bh;else if(lc[dx]){p.color=lc[dx];p.priority=0;p.present=1;}else if(ah.present&&!ah.priority)p=ah;else if(bh.present&&!bh.priority)p=bh;else{p.color=(uint8_t)(v->regs[7]&0x3Fu);p.priority=0;p.present=1;}}
    else if(ah.present&&ah.priority)p=ah;
    else if(bh.present&&bh.priority)p=bh;
    else if(lc[dx]){p.color=lc[dx];p.priority=0;p.present=1;}
    else if(lm[dx]){mod=lm[dx];if(ah.present&&!ah.priority)p=ah;else if(bh.present&&!bh.priority)p=bh;else{p.color=(uint8_t)(v->regs[7]&0x3Fu);p.priority=0;p.present=1;}}
    else if(ah.present&&!ah.priority)p=ah;
    else if(bh.present&&!bh.priority)p=bh;
    else{p.color=(uint8_t)(v->regs[7]&0x3Fu);p.priority=0;p.present=1;}
    if(v->regs[12]&8u){if(mod==1u)intensity=MD2GBA_VDP_INTENSITY_HILIGHT;else if(mod==2u)intensity=MD2GBA_VDP_INTENSITY_SHADOW;else if(!p.priority)intensity=MD2GBA_VDP_INTENSITY_SHADOW;}
    return (uint8_t)(p.color+(intensity==MD2GBA_VDP_INTENSITY_SHADOW?64u:intensity==MD2GBA_VDP_INTENSITY_HILIGHT?128u:0u));
}

/* P79 software-compositor fast path.
   Complex Genesis states still use the canonical software compositor, but the
   ordinary plane-V-scroll case no longer re-reads and re-decodes the same
   name-table entry at every output pixel.  Per-layer scanline caches retain the
   current name word until the source crosses into a different tile cell. */
typedef struct SoftMapCache {
    uint32_t tag;
    uint16_t row_addr;
    uint8_t pal_base;
    uint8_t priority;
    uint8_t hflip;
} SoftMapCache;

typedef struct SoftLineCtx {
    int16_t ha,hb,va,vb;
    unsigned w,h;
    uint16_t abase,bbase,wbase;
    unsigned wwidth,hcells,vrows,hp,vp;
    uint8_t hright,vbelow,vertical_window,fast;
    SoftMapCache acache,bcache,wcache;
} SoftLineCtx;

MD2GBA_SOFT_INLINE SoftPixel map_pixel_cached(const Md2GbaVdp*v,uint16_t base,unsigned mapw,unsigned maph,int sx,int sy,SoftMapCache*c){
    SoftPixel p={0,0,0};
    unsigned px=((unsigned)sx)&(mapw*8u-1u),py=((unsigned)sy)&(maph*8u-1u);
    unsigned cx=px>>3,cy=py>>3,tag=cy*mapw+cx,tx=px&7u,col;
    if(c->tag!=tag){
        uint16_t n=vrw(v,(unsigned)base+2u*tag);
        unsigned ty=py&7u;
        if(n&0x1000u)ty=7u-ty;
        c->tag=tag;
        c->row_addr=(uint16_t)(((n&0x7FFu)*32u)+(ty<<2));
        c->pal_base=(uint8_t)(((n>>13)&3u)<<4);
        c->priority=(uint8_t)!!(n&0x8000u);
        c->hflip=(uint8_t)!!(n&0x0800u);
    }
    if(c->hflip)tx=7u-tx;
    {uint8_t b=v->vram[(uint16_t)(c->row_addr+(tx>>1))];col=(tx&1u)?(b&15u):(b>>4);}
    if(!col)return p;
    p.color=(uint8_t)(c->pal_base+col);
    p.priority=c->priority;
    p.present=1u;
    return p;
}

static void soft_line_ctx(const Md2GbaVdp*v,unsigned gy,SoftLineCtx*c){
    unsigned cy=gy>>3;
    c->w=md2gba_vdp_plane_width(v);c->h=md2gba_vdp_plane_height(v);
    if(c->w!=32u&&c->w!=64u&&c->w!=128u)c->w=32u;
    if(c->h!=32u&&c->h!=64u&&c->h!=128u)c->h=32u;
    c->abase=md2gba_vdp_plane_a_base(v);c->bbase=md2gba_vdp_plane_b_base(v);
    c->wbase=md2gba_vdp_window_base(v);c->wwidth=md2gba_vdp_window_table_width(v);
    c->hcells=md2gba_vdp_visible_columns(v);c->vrows=md2gba_vdp_visible_rows(v);
    c->hp=(v->regs[17]&0x1Fu)*2u;if(c->hp>c->hcells)c->hp=c->hcells;
    c->vp=v->regs[18]&0x1Fu;if(c->vp>c->vrows)c->vp=c->vrows;
    c->hright=(uint8_t)!!(v->regs[17]&0x80u);c->vbelow=(uint8_t)!!(v->regs[18]&0x80u);
    c->vertical_window=(uint8_t)(cy<c->vrows && (c->vbelow?(cy>=c->vp):(cy<c->vp)));
    c->ha=c->hb=c->va=c->vb=0;c->fast=(uint8_t)!(v->regs[11]&4u);
    c->acache.tag=c->bcache.tag=c->wcache.tag=0xFFFFFFFFu;
    if(c->fast){
        (void)md2gba_vdp_hscroll_line(v,0u,gy,&c->ha);
        (void)md2gba_vdp_hscroll_line(v,1u,gy,&c->hb);
        (void)md2gba_vdp_vscroll_plane(v,0u,&c->va);
        (void)md2gba_vdp_vscroll_plane(v,1u,&c->vb);
    }
}

MD2GBA_SOFT_INLINE int line_window_active(const SoftLineCtx*c,unsigned gx){
    unsigned cx=gx>>3;
    int h;
    if(cx>=c->hcells)return 0;
    if(c->vertical_window)return 1;
    h=c->hright?(cx>=c->hp):(cx<c->hp);
    return h;
}

MD2GBA_SOFT_INLINE SoftPixel line_a_pixel_cached(const Md2GbaVdp*v,SoftLineCtx*c,unsigned gx,unsigned gy){
    if(line_window_active(c,gx))return map_pixel_cached(v,c->wbase,c->wwidth,32u,(int)gx,(int)gy,&c->wcache);
    return map_pixel_cached(v,c->abase,c->w,c->h,(int)gx-(int)c->ha,(int)gy+(int)c->va,&c->acache);
}
MD2GBA_SOFT_INLINE SoftPixel line_b_pixel_cached(const Md2GbaVdp*v,SoftLineCtx*c,unsigned gx,unsigned gy){
    return map_pixel_cached(v,c->bbase,c->w,c->h,(int)gx-(int)c->hb,(int)gy+(int)c->vb,&c->bcache);
}

MD2GBA_SOFT_INLINE uint8_t composed_line_pixel_ctx(const Md2GbaVdp*v,SoftLineCtx*c,unsigned gx,unsigned gy,unsigned dx,const uint8_t*hc,const uint8_t*hm,const uint8_t*lc,const uint8_t*lm){
    SoftPixel ah,bh,p;unsigned intensity=MD2GBA_VDP_INTENSITY_NORMAL,mod=0;
    if(!c->fast)return composed_line_pixel(v,gx,gy,dx,hc,hm,lc,lm);
    ah=line_a_pixel_cached(v,c,gx,gy);bh=line_b_pixel_cached(v,c,gx,gy);
    if(hc[dx]){p.color=hc[dx];p.priority=1;p.present=1;}
    else if(hm[dx]){mod=hm[dx];if(ah.present&&ah.priority)p=ah;else if(bh.present&&bh.priority)p=bh;else if(lc[dx]){p.color=lc[dx];p.priority=0;p.present=1;}else if(ah.present&&!ah.priority)p=ah;else if(bh.present&&!bh.priority)p=bh;else{p.color=(uint8_t)(v->regs[7]&0x3Fu);p.priority=0;p.present=1;}}
    else if(ah.present&&ah.priority)p=ah;
    else if(bh.present&&bh.priority)p=bh;
    else if(lc[dx]){p.color=lc[dx];p.priority=0;p.present=1;}
    else if(lm[dx]){mod=lm[dx];if(ah.present&&!ah.priority)p=ah;else if(bh.present&&!bh.priority)p=bh;else{p.color=(uint8_t)(v->regs[7]&0x3Fu);p.priority=0;p.present=1;}}
    else if(ah.present&&!ah.priority)p=ah;
    else if(bh.present&&!bh.priority)p=bh;
    else{p.color=(uint8_t)(v->regs[7]&0x3Fu);p.priority=0;p.present=1;}
    if(v->regs[12]&8u){if(mod==1u)intensity=MD2GBA_VDP_INTENSITY_HILIGHT;else if(mod==2u)intensity=MD2GBA_VDP_INTENSITY_SHADOW;else if(!p.priority)intensity=MD2GBA_VDP_INTENSITY_SHADOW;}
    return (uint8_t)(p.color+(intensity==MD2GBA_VDP_INTENSITY_SHADOW?64u:intensity==MD2GBA_VDP_INTENSITY_HILIGHT?128u:0u));
}

static const uint16_t p79_x_h32[240] = {
    0u,1u,2u,3u,4u,5u,6u,7u,8u,9u,10u,11u,12u,13u,14u,16u,
    17u,18u,19u,20u,21u,22u,23u,24u,25u,26u,27u,28u,29u,30u,32u,33u,
    34u,35u,36u,37u,38u,39u,40u,41u,42u,43u,44u,45u,46u,48u,49u,50u,
    51u,52u,53u,54u,55u,56u,57u,58u,59u,60u,61u,62u,64u,65u,66u,67u,
    68u,69u,70u,71u,72u,73u,74u,75u,76u,77u,78u,80u,81u,82u,83u,84u,
    85u,86u,87u,88u,89u,90u,91u,92u,93u,94u,96u,97u,98u,99u,100u,101u,
    102u,103u,104u,105u,106u,107u,108u,109u,110u,112u,113u,114u,115u,116u,117u,118u,
    119u,120u,121u,122u,123u,124u,125u,126u,128u,129u,130u,131u,132u,133u,134u,135u,
    136u,137u,138u,139u,140u,141u,142u,144u,145u,146u,147u,148u,149u,150u,151u,152u,
    153u,154u,155u,156u,157u,158u,160u,161u,162u,163u,164u,165u,166u,167u,168u,169u,
    170u,171u,172u,173u,174u,176u,177u,178u,179u,180u,181u,182u,183u,184u,185u,186u,
    187u,188u,189u,190u,192u,193u,194u,195u,196u,197u,198u,199u,200u,201u,202u,203u,
    204u,205u,206u,208u,209u,210u,211u,212u,213u,214u,215u,216u,217u,218u,219u,220u,
    221u,222u,224u,225u,226u,227u,228u,229u,230u,231u,232u,233u,234u,235u,236u,237u,
    238u,240u,241u,242u,243u,244u,245u,246u,247u,248u,249u,250u,251u,252u,253u,255u,
};
static const uint16_t p79_x_h40[240] = {
    0u,1u,2u,4u,5u,6u,8u,9u,10u,12u,13u,14u,16u,17u,18u,20u,
    21u,22u,24u,25u,26u,28u,29u,30u,32u,33u,34u,36u,37u,38u,40u,41u,
    42u,44u,45u,46u,48u,49u,50u,52u,53u,54u,56u,57u,58u,60u,61u,62u,
    64u,65u,66u,68u,69u,70u,72u,73u,74u,76u,77u,78u,80u,81u,82u,84u,
    85u,86u,88u,89u,90u,92u,93u,94u,96u,97u,98u,100u,101u,102u,104u,105u,
    106u,108u,109u,110u,112u,113u,114u,116u,117u,118u,120u,121u,122u,124u,125u,126u,
    128u,129u,130u,132u,133u,134u,136u,137u,138u,140u,141u,142u,144u,145u,146u,148u,
    149u,150u,152u,153u,154u,156u,157u,158u,160u,161u,162u,164u,165u,166u,168u,169u,
    170u,172u,173u,174u,176u,177u,178u,180u,181u,182u,184u,185u,186u,188u,189u,190u,
    192u,193u,194u,196u,197u,198u,200u,201u,202u,204u,205u,206u,208u,209u,210u,212u,
    213u,214u,216u,217u,218u,220u,221u,222u,224u,225u,226u,228u,229u,230u,232u,233u,
    234u,236u,237u,238u,240u,241u,242u,244u,245u,246u,248u,249u,250u,252u,253u,254u,
    256u,257u,258u,260u,261u,262u,264u,265u,266u,268u,269u,270u,272u,273u,274u,276u,
    277u,278u,280u,281u,282u,284u,285u,286u,288u,289u,290u,292u,293u,294u,296u,297u,
    298u,300u,301u,302u,304u,305u,306u,308u,309u,310u,312u,313u,314u,316u,317u,319u,
};
static const uint8_t p79_y_224[160] = {
    0u,1u,2u,4u,5u,7u,8u,9u,11u,12u,14u,15u,16u,18u,19u,21u,
    22u,23u,25u,26u,28u,29u,30u,32u,33u,35u,36u,37u,39u,40u,42u,43u,
    44u,46u,47u,49u,50u,51u,53u,54u,56u,57u,58u,60u,61u,63u,64u,65u,
    67u,68u,70u,71u,72u,74u,75u,77u,78u,79u,81u,82u,84u,85u,86u,88u,
    89u,91u,92u,93u,95u,96u,98u,99u,100u,102u,103u,105u,106u,107u,109u,110u,
    112u,113u,115u,116u,117u,119u,120u,122u,123u,124u,126u,127u,129u,130u,131u,133u,
    134u,136u,137u,138u,140u,141u,143u,144u,145u,147u,148u,150u,151u,152u,154u,155u,
    157u,158u,159u,161u,162u,164u,165u,166u,168u,169u,171u,172u,173u,175u,176u,178u,
    179u,180u,182u,183u,185u,186u,187u,189u,190u,192u,193u,194u,196u,197u,199u,200u,
    201u,203u,204u,206u,207u,208u,210u,211u,213u,214u,215u,217u,218u,220u,221u,223u,
};
static const uint8_t p79_y_240[160] = {
    0u,1u,3u,4u,6u,7u,9u,10u,12u,13u,15u,16u,18u,19u,21u,22u,
    24u,25u,27u,28u,30u,31u,33u,34u,36u,37u,39u,40u,42u,43u,45u,46u,
    48u,49u,51u,52u,54u,55u,57u,58u,60u,61u,63u,64u,66u,67u,69u,70u,
    72u,73u,75u,76u,78u,79u,81u,82u,84u,85u,87u,88u,90u,91u,93u,94u,
    96u,97u,99u,100u,102u,103u,105u,106u,108u,109u,111u,112u,114u,115u,117u,118u,
    120u,121u,123u,124u,126u,127u,129u,130u,132u,133u,135u,136u,138u,139u,141u,142u,
    144u,145u,147u,148u,150u,151u,153u,154u,156u,157u,159u,160u,162u,163u,165u,166u,
    168u,169u,171u,172u,174u,175u,177u,178u,180u,181u,183u,184u,186u,187u,189u,190u,
    192u,193u,195u,196u,198u,199u,201u,202u,204u,205u,207u,208u,210u,211u,213u,214u,
    216u,217u,219u,220u,222u,223u,225u,226u,228u,229u,231u,232u,234u,235u,237u,239u,
};

/* P80 projection: when the Genesis Window plane occupies a top/bottom strip,
   preserve that strip at 1:1 vertical resolution whenever practical and spend
   the necessary compression on the scrolling scene.  Dialogue/menu glyphs
   therefore keep all of their source rows instead of losing rows to a blanket
   224/240 -> 160 nearest-neighbour reduction.  This follows VDP window geometry
   generically; there are no PSIV scene coordinates here. */
static MD2GBA_SOFT_COLD void p80_resample_span(uint8_t*out,unsigned out_start,unsigned out_n,unsigned src_start,unsigned src_n){
    unsigned i,src=0u,err=0u,den,step;
    if(!out_n||!src_n)return;
    if(out_n==1u){out[out_start]=(uint8_t)src_start;return;}
    den=out_n-1u;step=src_n-1u;
    for(i=0u;i<out_n;i++){
        unsigned q=src<src_n?src:(src_n-1u);
        out[out_start+i]=(uint8_t)(src_start+q);
        err+=step;
        if(err>=den){err-=den;src++;}
        if(err>=den){err-=den;src++;}
        if(err>=den){err-=den;src++;}
    }
}
static MD2GBA_SOFT_COLD int p82_bottom_ui_active(const Md2GbaVdp*v){
    unsigned srch=md2gba_vdp_visible_rows(v)*8u;
    unsigned center=srch>160u?(srch-160u)/2u:0u;
    unsigned bottom=srch>160u?srch-160u:0u;
    return bottom!=center && md2gba_gba_suggest_view_y(v)==bottom;
}

static MD2GBA_SOFT_COLD void p80_build_y_map(const Md2GbaVdp*v,uint8_t out[160]){
    unsigned rows=md2gba_vdp_visible_rows(v),srch=rows*8u;
    unsigned vp=v->regs[18]&0x1Fu;
    unsigned below=!!(v->regs[18]&0x80u);
    unsigned split,win_h;
    if(vp>rows)vp=rows;
    split=vp*8u;
    win_h=below?(srch-split):split;
    /* Preserve normal dialogue/menu-sized window strips.  Very large windows
       fall back to the full-frame map so cutscenes that intentionally use the
       Window as a major picture plane still fit the complete source frame. */
    if(win_h && win_h<=96u && win_h<160u){
        unsigned scene_out=160u-win_h;
        if(below){
            p80_resample_span(out,0u,scene_out,0u,split);
            p80_resample_span(out,scene_out,win_h,split,win_h);
        }else{
            p80_resample_span(out,0u,win_h,0u,win_h);
            p80_resample_span(out,win_h,scene_out,split,srch-split);
        }
        return;
    }
    /* P82: PSIV and other MD software can build fixed dialogue/menu UI on
       high-priority Scroll A instead of the Window plane.  The hardware-path
       detector already proves a dense fixed overlay in the final 64 source
       lines.  Preserve those 64 lines 1:1 (all glyph rows/borders) and spend
       the vertical compression on the scrolling scene above it.  H40/H32 are
       still horizontally projected by the normal 320/256 -> 240 x-map. */
    if(srch>=160u && p82_bottom_ui_active(v)){
        const unsigned ui_h=64u,scene_out=160u-ui_h;
        p80_resample_span(out,0u,scene_out,0u,srch-ui_h);
        p80_resample_span(out,scene_out,ui_h,srch-ui_h,ui_h);
        return;
    }
    p80_resample_span(out,0u,160u,0u,srch);
}

MD2GBA_SOFT_INLINE int p81_span_has_block(unsigned addr,unsigned base,unsigned bytes){
    unsigned end=(base+bytes)&0xFFFFu;
    addr&=0xFFFFu;base&=0xFFFFu;
    if(!bytes)return 0;
    if(base+bytes<=0x10000u)return addr>=base&&addr<base+bytes;
    return addr>=base||addr<end;
}
MD2GBA_SOFT_INLINE int p81_bit_test(const uint32_t bits[64],unsigned n){return !!(bits[n>>5]&(1u<<(n&31u)));}
MD2GBA_SOFT_INLINE void p81_bit_set(uint32_t bits[64],unsigned n){bits[n>>5]|=1u<<(n&31u);}
static MD2GBA_SOFT_COLD int p81_map_refs_dirty_candidate(const Md2GbaVdp*v,uint16_t base,unsigned w,unsigned h,const uint32_t cand[64]){
    unsigned i,cells=w*h;
    for(i=0;i<cells;i++){unsigned src=vrw(v,(unsigned)base+i*2u)&0x7FFu;if(p81_bit_test(cand,src))return 1;}
    return 0;
}
static MD2GBA_SOFT_COLD int p81_sprite_refs_dirty_candidate(const Md2GbaVdp*v,const uint32_t cand[64]){
    unsigned entry=0u,guard=0u,base=md2gba_vdp_sat_base(v);
    while(guard++<80u){
        unsigned a=(base+entry*8u)&0xFFFFu,sz=v->vram[(uint16_t)(a+2u)],link=v->vram[(uint16_t)(a+3u)]&0x7Fu;
        unsigned sw=((sz>>2)&3u)+1u,sh=(sz&3u)+1u,t=vrw(v,a+4u)&0x7FFu,x,y;
        for(x=0u;x<sw;x++)for(y=0u;y<sh;y++)if(p81_bit_test(cand,(t+x*sh+y)&0x7FFu))return 1;
        if(!link)break;entry=link;
    }
    return 0;
}

/* P81 conservative partial-frame proof.  This is intentionally narrow: only a
   full-width vertical Window strip may use it, and every changed pattern tile
   must be referenced by Window while being absent from Scroll A/B and sprites.
   Any ambiguous VDP dirtiness returns false and takes the canonical full render. */
MD2GBA_SOFT_COLD int md2gba_vdp_soft_window_only_dirty(const Md2GbaVdp*v){
    uint32_t cand[64],wref[64];
    unsigned w=md2gba_vdp_plane_width(v),h=md2gba_vdp_plane_height(v),ww=md2gba_vdp_window_table_width(v);
    uint16_t abase=md2gba_vdp_plane_a_base(v),bbase=md2gba_vdp_plane_b_base(v),wbase=md2gba_vdp_window_base(v);
    unsigned abytes=w*h*2u,wbytes=ww*32u*2u,sbase=md2gba_vdp_sat_base(v),hsbase=md2gba_vdp_hscroll_base(v);
    unsigned blk,i,cells;
    if((w!=32u&&w!=64u)||(h!=32u&&h!=64u))return 0;
    /* Avoid freestanding compiler memset helpers in the target image. */
    { volatile uint32_t *a=cand,*b=wref; for(i=0u;i<64u;i++){a[i]=0u;b[i]=0u;} }
    for(blk=0u;blk<2048u;blk++){
        unsigned a=blk*32u;
        if(!(v->vram_dirty_tiles[blk>>5]&(1u<<(blk&31u))))continue;
        if(p81_span_has_block(a,wbase,wbytes))continue;
        if(p81_span_has_block(a,abase,abytes)||p81_span_has_block(a,bbase,abytes)||
           p81_span_has_block(a,sbase,640u)||p81_span_has_block(a,hsbase,640u))return 0;
        p81_bit_set(cand,blk);
    }
    if(p81_map_refs_dirty_candidate(v,abase,w,h,cand)||p81_map_refs_dirty_candidate(v,bbase,w,h,cand)||
       p81_sprite_refs_dirty_candidate(v,cand))return 0;
    cells=ww*32u;
    for(i=0u;i<cells;i++){unsigned src=vrw(v,(unsigned)wbase+i*2u)&0x7FFu;if(p81_bit_test(cand,src))p81_bit_set(wref,src);}
    for(i=0u;i<64u;i++)if(cand[i]&~wref[i])return 0;
    return 1;
}

/* P82 Scroll-A bottom-UI projection.  When the generic hardware viewport
   detector identifies a fixed bottom overlay, p80_build_y_map preserves the
   final 64 Genesis lines exactly in output rows 96..159. */
MD2GBA_SOFT_COLD int md2gba_vdp_soft_bottom_ui_rows(const Md2GbaVdp*v,unsigned*first,unsigned*last){
    if(!p82_bottom_ui_active(v))return 0;
    if(first)*first=96u;if(last)*last=160u;return 1;
}

MD2GBA_SOFT_INLINE int p82_row_in_set(unsigned row,const uint8_t set[128],unsigned h){return row<h&&set[row]!=0u;}

/* Conservative proof that all changed visible VRAM state is confined to the
   bottom Scroll-A band that P82 will redraw.  It accepts Plane-A map blocks
   only when the entire 32-byte dirty block belongs to source rows in the
   preserved final 64 lines.  Pattern blocks may be redrawn partially only if
   every Scroll-A reference is in that row set and the tile is absent from
   Scroll B and sprites.  SAT/H-scroll changes, column V-scroll, shared tiles,
   or any ambiguity force a canonical full render. */
MD2GBA_SOFT_COLD int md2gba_vdp_soft_bottom_ui_only_dirty(const Md2GbaVdp*v){
    uint32_t cand[64];uint8_t allowed_rows[128];
    unsigned w=md2gba_vdp_plane_width(v),h=md2gba_vdp_plane_height(v);
    uint16_t abase=md2gba_vdp_plane_a_base(v),bbase=md2gba_vdp_plane_b_base(v);
    unsigned abytes=w*h*2u,sbase=md2gba_vdp_sat_base(v),hsbase=md2gba_vdp_hscroll_base(v);
    unsigned srch=md2gba_vdp_visible_rows(v)*8u,blk,i,wshift;int16_t vs=0;
    if(!p82_bottom_ui_active(v)||(w!=32u&&w!=64u)||(h!=32u&&h!=64u))return 0;
    wshift=(w==64u)?6u:5u;
    /* Restrict the proof to whole-plane H/V scroll.  This is the common
       dialogue configuration and makes the source-row -> map-row relation
       exact at 8-pixel granularity. */
    if((v->regs[11]&7u)!=0u || md2gba_vdp_vscroll_plane(v,0u,&vs) || (vs&7))return 0;
    for(i=0u;i<64u;i++)cand[i]=0u;
    for(i=0u;i<128u;i++)allowed_rows[i]=0u;
    for(i=srch-64u;i<srch;i+=8u)allowed_rows[(((unsigned)((int)i+(int)vs))>>3)&(h-1u)]=1u;

    for(blk=0u;blk<2048u;blk++){
        unsigned a=blk*32u;
        if(!(v->vram_dirty_tiles[blk>>5]&(1u<<(blk&31u))))continue;
        if(p81_span_has_block(a,bbase,abytes)||p81_span_has_block(a,sbase,640u)||p81_span_has_block(a,hsbase,640u))return 0;
        if(p81_span_has_block(a,abase,abytes)){
            unsigned off=((a-(unsigned)abase)&0xFFFFu),first_cell=off>>1,cells=16u;
            for(i=0u;i<cells;i++)if(!p82_row_in_set((first_cell+i)>>wshift,allowed_rows,h))return 0;
            continue;
        }
        p81_bit_set(cand,blk);
    }
    if(p81_map_refs_dirty_candidate(v,bbase,w,h,cand)||p81_sprite_refs_dirty_candidate(v,cand))return 0;
    /* A candidate pattern may affect Scroll A only inside the redraw band. */
    for(i=0u;i<w*h;i++){
        unsigned src=vrw(v,(unsigned)abase+i*2u)&0x7FFu;
        if(p81_bit_test(cand,src)&&!p82_row_in_set(i>>wshift,allowed_rows,h))return 0;
    }
    return 1;
}

/* Return the output-row interval occupied by a full-width vertical Window.
   Horizontal Window selection must be disabled so rewriting only these rows is
   provably sufficient. */
MD2GBA_SOFT_COLD int md2gba_vdp_soft_fullwidth_window_rows(const Md2GbaVdp*v,unsigned*first,unsigned*last){
    uint8_t ym[160];unsigned hc=md2gba_vdp_visible_columns(v),vr=md2gba_vdp_visible_rows(v);
    unsigned hp=(v->regs[17]&0x1Fu)*2u,vp=v->regs[18]&0x1Fu,y,f=160u,l=0u;
    int hactive,vactive,below=!!(v->regs[18]&0x80u);
    if(hp>hc)hp=hc;if(vp>vr)vp=vr;
    hactive=(v->regs[17]&0x80u)?(hp<hc):(hp>0u);
    vactive=below?(vp<vr):(vp>0u);
    if(hactive||!vactive)return 0;
    p80_build_y_map(v,ym);
    for(y=0u;y<160u;y++){
        unsigned sy=(unsigned)ym[y]>>3;
        int in=below?(sy>=vp):(sy<vp);
        if(in){if(f==160u)f=y;l=y+1u;}
    }
    if(f>=l)return 0;if(first)*first=f;if(last)*last=l;return 1;
}

void md2gba_vdp_soft_render_rows_240x160_coop(const Md2GbaVdp*v,volatile uint16_t*dst,unsigned first,unsigned last,unsigned ox,unsigned oy,Md2GbaVdpSoftYieldFn yield_fn,void*yield_user,unsigned yield_lines){
    uint8_t hc[320],hm[320],lc[320],lm[320];unsigned y,x,yc=0u;
    unsigned srcw=md2gba_vdp_visible_columns(v)*8u;
    const uint16_t*xmap=srcw==320u?p79_x_h40:p79_x_h32;
    uint8_t ymap_local[160]; const uint8_t*ymap=ymap_local;
    p80_build_y_map(v,ymap_local);
    (void)ox;(void)oy;
    if(first>160u)first=160u;if(last>160u)last=160u;if(last<first)last=first;
    if(!yield_lines)yield_lines=1u;
    for(y=first;y<last;y++){
        unsigned gy=(unsigned)ymap[y];SoftLineCtx ctx;soft_line_ctx(v,gy,&ctx);
        sprite_line(v,gy,0u,srcw,hc,hm,lc,lm);
        for(x=0;x<240u;x+=2u){
            unsigned gx0=(unsigned)xmap[x],gx1=(unsigned)xmap[x+1u];
            uint8_t a=composed_line_pixel_ctx(v,&ctx,gx0,gy,gx0,hc,hm,lc,lm);
            uint8_t b=composed_line_pixel_ctx(v,&ctx,gx1,gy,gx1,hc,hm,lc,lm);
            dst[y*120u+(x>>1)]=(uint16_t)(a|((uint16_t)b<<8));
        }
        if(yield_fn){yc++;if(yc>=yield_lines){yield_fn(yield_user);yc=0u;}}
    }
}
void md2gba_vdp_soft_render_240x160_coop(const Md2GbaVdp*v,volatile uint16_t*dst,unsigned ox,unsigned oy,Md2GbaVdpSoftYieldFn yield_fn,void*yield_user,unsigned yield_lines){md2gba_vdp_soft_render_rows_240x160_coop(v,dst,0u,160u,ox,oy,yield_fn,yield_user,yield_lines);}
void md2gba_vdp_soft_render_240x160(const Md2GbaVdp*v,volatile uint16_t*dst,unsigned ox,unsigned oy){md2gba_vdp_soft_render_240x160_coop(v,dst,ox,oy,0,0,160u);}
