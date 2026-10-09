#define main inherited_oracle_main
#include "test_p134_projected_runtime.c"
#undef main

static unsigned rng=0x1375A11u;
static unsigned rnd(void){rng=rng*1664525u+1013904223u;return rng;}
static void mark_block(Md2GbaVdp*v,unsigned b){v->vram_dirty_tiles[(b&2047u)>>5]|=1u<<((b&2047u)&31u);}
static int brute_cell(const Md2GbaVdp*v,unsigned plane,int32_t tx,int32_t ty){
    unsigned w=PS->plane_w,h=PS->plane_h;uint16_t base=PS->plane_base[plane];
    int32_t qx=floordiv3(tx),qy=floordiv5(ty),xp=tx-qx*3,yp=ty-qy*5;
    int32_t bx=tx+qx,by=qy*7+(int32_t)proj_ybase_rem[(unsigned)yp];
    unsigned rows=yp==2?3u:2u; (void)xp;
    for(unsigned y=0;y<rows;y++)for(unsigned x=0;x<2;x++){
        unsigned sx=imod(bx+(int32_t)x,w),sy=imod(by+(int32_t)y,h);
        unsigned a=(unsigned)base+2u*(sy*w+sx);
        if(block_dirty(v,a))return 1;
    }
    return 0;
}
int main(void){
    Md2GbaVdp v; uint32_t got[32]; unsigned cases=0;
    if(mmap((void*)0x04000000u,0x04000000u,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)==MAP_FAILED)return 99;
    memset(&v,0,sizeof v); md2gba_gba_projected_reset();
    for(unsigned it=0;it<5000;it++){
        memset(v.vram_dirty_tiles,0,sizeof v.vram_dirty_tiles);
        PS->plane_w=(rnd()&1u)?64u:32u;PS->plane_h=(rnd()&1u)?64u:32u;
        /* Real VDP name bases are aligned and may end exactly at VRAM wrap. */
        PS->plane_base[0]=(uint16_t)((rnd()&0x1fu)<<10);
        PS->plane_base[1]=(uint16_t)((rnd()&7u)<<13);
        unsigned plane=rnd()&1u;int32_t ox=(int32_t)(rnd()%401u)-200,oy=(int32_t)(rnd()%401u)-200;
        unsigned n=1u+(rnd()%12u);for(unsigned k=0;k<n;k++)mark_block(&v,rnd()%2048u);
        projected_dirty_name_cells(&v,plane,ox,oy,got);
        for(unsigned y=0;y<PROJ_COVER_H;y++)for(unsigned x=0;x<PROJ_COVER_W;x++){
            int32_t tx=ox+(int32_t)x,ty=oy+(int32_t)y;unsigned cell=(imod(ty,32u)<<5)|imod(tx,32u);
            int a=!!(got[cell>>5]&(1u<<(cell&31u))),b=brute_cell(&v,plane,tx,ty);
            if(a!=b){printf("FAIL it=%u p=%u origin=%ld,%ld cell=%u,%u got=%d want=%d\n",it,plane,(long)ox,(long)oy,x,y,a,b);return 1;}
            cases++;
        }
    }
    printf("PASS P137 exact name-dirty geometry: %u projected cells, zero false +/-\n",cases);return 0;
}
