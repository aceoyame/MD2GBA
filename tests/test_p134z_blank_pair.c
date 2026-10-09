#define main inherited_oracle_main
#include "test_p134_projected_runtime.c"
#undef main
int main(void){
    Md2GbaVdp v; uint8_t hi[64] __attribute__((aligned(4))),lo[64] __attribute__((aligned(4)));uint32_t dep;uint8_t map;unsigned i;
    memset(&v,0,sizeof(v));memset((void*)md2gba_bg_tile_stage,0,sizeof(md2gba_bg_tile_stage));
    v.regs[2]=0x10;v.regs[4]=3;v.regs[16]=0;PS->magic=PROJ_MAGIC;PS->plane_w=32;PS->plane_h=32;PS->plane_base[0]=0x4000;PS->plane_base[1]=0x6000;
    for(i=0;i<1024;i++)namew(&v,0x4000,i,5); /* pattern 5 remains all-zero */
    memset(hi,0xA5,sizeof hi);memset(lo,0x5A,sizeof lo);md2gba_projected_test_bg_assemblies=0;
    make_bg_tile_pair(&v,0,0,0,hi,lo,&dep,&map);
    if(md2gba_projected_test_bg_assemblies){puts("FAIL blank pair entered pixel assembler");return 1;}
    for(i=0;i<64;i++)if(hi[i]||lo[i]){puts("FAIL blank pair outputs nonzero");return 2;}
    if(!(PS->bg_source_mask[5>>5]&(1u<<(5&31)))){puts("FAIL blank dependency not recorded");return 3;}
    tile(&v,5,3);md2gba_projected_test_bg_assemblies=0;make_bg_tile_pair(&v,0,0,0,hi,lo,&dep,&map);
    if(md2gba_projected_test_bg_assemblies!=1){puts("FAIL nonblank pair skipped assembler");return 4;}
    for(i=0;i<64;i++)if(hi[i]||lo[i])break;if(i==64){puts("FAIL nonblank pair stayed zero");return 5;}
    puts("PASS P134Z exact blank-pair rejection + dependency retention");return 0;
}
