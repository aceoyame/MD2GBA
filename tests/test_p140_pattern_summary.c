#define MD2GBA_PROJECTED_TEST_PATTERN_CACHE 1
#define main inherited_oracle_main
#include "test_p134_projected_runtime.c"
#undef main

int main(void){
    Md2GbaVdp v; uint8_t hi[64] __attribute__((aligned(4))),lo[64] __attribute__((aligned(4))); uint32_t dep; uint8_t map; unsigned i,wi,bit;
    memset(&v,0,sizeof v); memset((void*)md2gba_bg_tile_stage,0,sizeof(md2gba_bg_tile_stage));
    v.regs[2]=0x10; v.regs[4]=3; v.regs[16]=0; PS->magic=PROJ_MAGIC; PS->plane_w=32; PS->plane_h=32; PS->plane_base[0]=0x4000; PS->plane_base[1]=0x6000;
    for(i=0;i<1024;i++)namew(&v,0x4000,i,5);
    wi=5u>>5; bit=1u<<(5u&31u);
    make_bg_tile_pair(&v,0,0,0,hi,lo,&dep,&map);
    if(!(PS->pattern_nonzero_valid[wi]&bit) || (PS->pattern_nonzero_bits[wi]&bit)){puts("FAIL initial zero summary");return 1;}
    tile(&v,5,3); v.vram_dirty_tiles[wi]|=bit; pattern_summary_invalidate_dirty(&v);
    if(PS->pattern_nonzero_valid[wi]&bit){puts("FAIL dirty invalidate");return 2;}
    make_bg_tile_pair(&v,0,0,0,hi,lo,&dep,&map);
    if(!(PS->pattern_nonzero_valid[wi]&bit) || !(PS->pattern_nonzero_bits[wi]&bit)){puts("FAIL nonzero refresh");return 3;}
    memset(&v.vram[5u*32u],0,32u); v.vram_dirty_tiles[wi]|=bit; pattern_summary_invalidate_dirty(&v);
    make_bg_tile_pair(&v,0,0,0,hi,lo,&dep,&map);
    if(!(PS->pattern_nonzero_valid[wi]&bit) || (PS->pattern_nonzero_bits[wi]&bit)){puts("FAIL zero refresh");return 4;}
    puts("PASS P140 lazy exact pattern-summary invalidate/refresh"); return 0;
}
