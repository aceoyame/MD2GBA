/* P134X: regression/performance-work bounds for the projected renderer.
   This is a host oracle: it checks that previously reproduced pathological
   work amplification stays eliminated while preserving the exact P134O
   renderer implementation included below. */
#define main inherited_oracle_main
#include "test_p134_projected_runtime.c"
#undef main

static unsigned checkpoints,dark_checkpoints;
static void checkpoint(void *unused){(void)unused;checkpoints++;if(!(REG_DISPCNT&0x1f00u))dark_checkpoints++;}
static void seed_cost(Md2GbaVdp *v){
    unsigned i;md2gba_gba_projected_reset();memset(v,0,sizeof(*v));
    v->regs[2]=0x10;v->regs[4]=3;v->regs[5]=0x38;v->regs[16]=1;
    v->regs[12]=0x81;v->regs[1]=0x40;v->present_scroll_valid=1;
    tile(v,1,1);tile(v,2,2);tile(v,0x6e9,2);tile(v,0x6ea,3);tile(v,0x6f3,4);tile(v,7,5);
    for(i=0;i<2048;i++){namew(v,0x4000,i,1);namew(v,0x6000,i,0);}for(i=0;i<64;i++)v->cram_gba[i]=(uint16_t)i;
    if(md2gba_gba_projected_present(v))abort();
}
static void mark_cost(Md2GbaVdp*v,unsigned addr){unsigned b=addr>>5;v->vram_dirty_tiles[b>>5]|=1u<<(b&31);}
static void panel_cost(Md2GbaVdp*v,int remove){
    unsigned x,y,sx=5,sy=5,w=10,h=6;
    for(y=0;y<h;y++)for(x=0;x<w;x++){
        unsigned n=7;if(y==0||y==h-1)n=(x==0?0x6e9:x==w-1?0xee9:0x6ea)|(y?0x1000:0);else if(x==0||x==w-1)n=x?0xef3:0x6f3;
        unsigned idx=(sy+y)*64+sx+x;namew(v,0x4000,idx,remove?1:n);mark_cost(v,0x4000+2*idx);
    }v->vram_epoch++;
}
static void counters_cost(void){md2gba_projected_test_bg_builds=md2gba_projected_test_bg_source_checks=checkpoints=dark_checkpoints=0;}
static int fail_cost(const char*what,unsigned got,unsigned lim){fprintf(stderr,"FAIL %s got=%u limit=%u\n",what,got,lim);return 1;}
int main(void){
    static Md2GbaVdp v;int r;
    void*m=mmap((void*)0x04000000,0x04000000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0);if(m==MAP_FAILED)return 99;
    md2gba_gba_projected_set_checkpoint(checkpoint,0);

    seed_cost(&v);panel_cost(&v,0);md2gba_gba_projected_present(&v);md2gba_gba_projected_present(&v);md2gba_gba_projected_vblank_commit_pending();
    counters_cost();panel_cost(&v,1);r=md2gba_gba_projected_present(&v);if(r)return fail_cost("panel close rc",(unsigned)r,0);
    if(md2gba_projected_test_bg_builds>200u)return fail_cost("10x6 panel close pair builds",md2gba_projected_test_bg_builds,200u);
    if(PS->bank_switch_pending)return fail_cost("10x6 panel close bank switch",PS->bank_switch_pending,0u);
    if(dark_checkpoints)return fail_cost("10x6 panel close dark checkpoints",dark_checkpoints,0u);

    seed_cost(&v);counters_cost();tile(&v,1,3);mark_cost(&v,32);v.vram_epoch++;r=md2gba_gba_projected_present(&v);if(r)return fail_cost("651-pattern rc",(unsigned)r,0);
    if(md2gba_projected_test_bg_builds>1302u)return fail_cost("651-pattern speculative pair builds",md2gba_projected_test_bg_builds,1302u);

    seed_cost(&v);memset(PS->slot_hash_head,0,sizeof PS->slot_hash_head);memset(PS->slot_hash,0,sizeof PS->slot_hash);
    for(unsigned i=1;i<768;i++)PS->slot_ref[i]=1;PS->slot_ref[767]=0;PS->slot_highwater=767;PS->free_scan_cursor=767;memset(PS->active_slot,0,sizeof PS->active_slot);
    for(unsigned y=0;y<3;y++){namew(&v,0x4000,y*64,0x8001);namew(&v,0x4000,y*64+1,2);}queue_reset();r=queue_cell_pair(&v,0,0,0);
    if(r!=-2)return fail_cost("one-free-slot pair must refuse",(unsigned)r,(unsigned)-2);
    if(PS->map_write_count)return fail_cost("one-free-slot partial writes",PS->map_write_count,0u);
    if(PS->slot_ref[767])return fail_cost("one-free-slot leaked reservation",PS->slot_ref[767],0u);

    printf("PASS P134X projected work bounds: close_builds<=200 dirty651<=1302 pair-slot capacity atomic\n");return 0;
}
