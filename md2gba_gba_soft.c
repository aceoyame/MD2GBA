#include "md2gba_gba_soft.h"
#include "md2gba_vdp_soft.h"
#define REG_DISPCNT (*(volatile uint16_t*)0x04000000u)
#define GBA_PAL ((volatile uint16_t*)0x05000000u)
#define MODE4_PAGE0 ((volatile uint16_t*)0x06000000u)
#define MODE4_PAGE1 ((volatile uint16_t*)0x0600A000u)
#define REG_DMA3SAD (*(volatile uint32_t*)0x040000D4u)
#define REG_DMA3DAD (*(volatile uint32_t*)0x040000D8u)
#define REG_DMA3CNT (*(volatile uint32_t*)0x040000DCu)
void md2gba_gba_soft_commit_palette(const Md2GbaVdp*v){unsigned i;for(i=0;i<64u;i++){GBA_PAL[i]=md2gba_vdp_soft_color_from_genesis(v->cram[i],MD2GBA_VDP_INTENSITY_NORMAL);GBA_PAL[64u+i]=md2gba_vdp_soft_color_from_genesis(v->cram[i],MD2GBA_VDP_INTENSITY_SHADOW);GBA_PAL[128u+i]=md2gba_vdp_soft_color_from_genesis(v->cram[i],MD2GBA_VDP_INTENSITY_HILIGHT);}}
void md2gba_gba_soft_init_mode4(unsigned page){REG_DISPCNT=(uint16_t)(4u|(1u<<10)|((page&1u)<<4));}
void md2gba_gba_soft_show_page(unsigned page){uint16_t d=REG_DISPCNT;d=(uint16_t)((d&~0x0017u)|4u|((page&1u)<<4)|(1u<<10));REG_DISPCNT=d;}
volatile uint16_t*md2gba_gba_soft_page_ptr(unsigned page){return page?MODE4_PAGE1:MODE4_PAGE0;}
void md2gba_gba_soft_copy_page(unsigned dst_page,unsigned src_page){
    if((dst_page&1u)==(src_page&1u))return;
    REG_DMA3CNT=0u;
    REG_DMA3SAD=(uint32_t)md2gba_gba_soft_page_ptr(src_page);
    REG_DMA3DAD=(uint32_t)md2gba_gba_soft_page_ptr(dst_page);
    /* 240*160 8bpp pixels = 38,400 bytes = 9,600 32-bit DMA units. */
    REG_DMA3CNT=(1u<<31)|(1u<<26)|9600u;
}
void md2gba_gba_soft_render_rows_page_coop(const Md2GbaVdp*v,unsigned page,unsigned first,unsigned last,unsigned ox,unsigned oy,Md2GbaVdpSoftYieldFn yield_fn,void*yield_user,unsigned yield_lines){md2gba_vdp_soft_render_rows_240x160_coop(v,md2gba_gba_soft_page_ptr(page),first,last,ox,oy,yield_fn,yield_user,yield_lines);}
void md2gba_gba_soft_render_page_coop(const Md2GbaVdp*v,unsigned page,unsigned ox,unsigned oy,Md2GbaVdpSoftYieldFn yield_fn,void*yield_user,unsigned yield_lines){md2gba_vdp_soft_render_240x160_coop(v,md2gba_gba_soft_page_ptr(page),ox,oy,yield_fn,yield_user,yield_lines);}
void md2gba_gba_soft_render_page(const Md2GbaVdp*v,unsigned page,unsigned ox,unsigned oy){md2gba_gba_soft_render_page_coop(v,page,ox,oy,0,0,160u);}
