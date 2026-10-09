/* Analysis-only observer of the unchanged P134U implementation. */
#define main inherited_oracle_main
#include "test_p134_projected_runtime.c"
#undef main
static unsigned checkpoints,dark_checkpoints;
static void checkpoint(void *unused){(void)unused;checkpoints++;if(!(REG_DISPCNT&0x1f00u))dark_checkpoints++;}
static void seed(Md2GbaVdp *v){
    unsigned i;
    md2gba_gba_projected_reset();memset(v,0,sizeof(*v));
    v->regs[2]=0x10;v->regs[4]=3;v->regs[5]=0x38;v->regs[16]=1;
    v->regs[12]=0x81;v->regs[1]=0x40;v->present_scroll_valid=1;
    tile(v,1,1);tile(v,2,2);tile(v,0x6e9,2);tile(v,0x6ea,3);tile(v,0x6f3,4);tile(v,7,5);
    for(i=0;i<2048;i++){namew(v,0x4000,i,1);namew(v,0x6000,i,0);}
    for(i=0;i<64;i++)v->cram_gba[i]=i;
    md2gba_gba_projected_present(v);
}
static void mark(Md2GbaVdp*v,unsigned addr){unsigned b=addr>>5;v->vram_dirty_tiles[b>>5]|=1u<<(b&31);}
static void panel(Md2GbaVdp*v,int remove){
    unsigned x,y,sx=5,sy=5,w=10,h=6;
    for(y=0;y<h;y++)for(x=0;x<w;x++){
        unsigned n=7;
        if(y==0||y==h-1)n=(x==0?0x6e9:x==w-1?0xee9:0x6ea)|(y?0x1000:0);
        else if(x==0||x==w-1)n=x?0xef3:0x6f3;
        unsigned idx=(sy+y)*64+sx+x;
        namew(v,0x4000,idx,remove?1:n);mark(v,0x4000+2*idx);
    }
    v->vram_epoch++;
}
static void counters(void){md2gba_projected_test_bg_builds=md2gba_projected_test_bg_source_checks=checkpoints=dark_checkpoints=0;}
static void report(const char *name,int rc){
    printf("%s rc=%d builds=%u checks=%u checkpoints=%u dark_checkpoints=%u map_writes=%u bank_switch=%u pending=%u ui=%u\n",name,rc,md2gba_projected_test_bg_builds,md2gba_projected_test_bg_source_checks,checkpoints,dark_checkpoints,PS->map_write_count,PS->bank_switch_pending,PS->pending,PS->ui_count);
}

static unsigned char before_pixels[4][160][240];
static void capture(void){for(unsigned l=0;l<4;l++)for(unsigned y=0;y<160;y++)for(unsigned x=0;x<240;x++)before_pixels[l][y][x]=projected_screen_pixel(l,x,y);}
static unsigned differences(void){unsigned n=0;for(unsigned l=0;l<4;l++)for(unsigned y=0;y<160;y++)for(unsigned x=0;x<240;x++)n+=before_pixels[l][y][x]!=projected_screen_pixel(l,x,y);return n;}
static void done(const char*name,Md2GbaVdp*v){int r=md2gba_gba_projected_present(v);report(name,r);md2gba_gba_projected_vblank_commit_pending();printf("%s changed_layer_pixels=%u\n",name,differences());}
int main(void){
 static Md2GbaVdp v;
 if(mmap((void*)0x04000000,0x04000000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)==MAP_FAILED)return 99;
 md2gba_gba_projected_set_checkpoint(checkpoint,0);
 seed(&v);capture();counters();tile(&v,1,3);tile(&v,1,1);mark(&v,32);v.vram_epoch+=2;done("pattern_net_revert",&v);
 seed(&v);tile(&v,2,1);capture();counters();for(unsigned i=0;i<2048;i++){namew(&v,0x4000,i,2);mark(&v,0x4000+2*i);}v.vram_epoch++;done("same_pixels_different_pattern_id",&v);
 seed(&v);capture();counters();for(unsigned i=0;i<2048;i++){namew(&v,0x6000,i,0x8000);mark(&v,0x6000+2*i);}v.vram_epoch++;done("empty_plane_priority_change",&v);
 seed(&v);counters();v.present_hscroll[0]=-12;v.present_vscroll[0]=12;v.present_scroll_epoch++;int r=md2gba_gba_projected_present(&v);report("diagonal_one_cell",r);
 seed(&v);panel(&v,0);md2gba_gba_projected_present(&v);md2gba_gba_projected_present(&v);md2gba_gba_projected_vblank_commit_pending();
 unsigned footprint=0;for(unsigned w=0;w<32;w++)footprint+=__builtin_popcount(PS->ui_target_mask[w]|PS->ui_suppress_mask[w]);printf("close_panel_old_footprint=%u\n",footprint);
 counters();panel(&v,1);r=md2gba_gba_projected_present(&v);report("close_panel",r);
 printf("projected_state_bytes=%zu vdp_bytes=%zu\n",sizeof(*PS),sizeof(v));
 return 0;
}
