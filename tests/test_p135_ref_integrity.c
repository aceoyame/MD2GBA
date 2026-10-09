#define P135_NO_MAIN 1
#include "test_p135_rebuild_reuse.c"
static int check_refs(unsigned scen,unsigned step){
    unsigned counts[PROJ_TILE_SLOTS]={0},l,c,s,sb=PROJ_SB_FIRST+PS->map_bank*4u;
    if(PS->pending||PS->bank_switch_pending||PS->rebase_cleanup){fprintf(stderr,"unsettled s=%u step=%u p=%u bs=%u rb=%u\n",scen,step,PS->pending,PS->bank_switch_pending,PS->rebase_cleanup);return 1;}
    for(l=0;l<PROJ_LAYERS;l++)for(c=0;c<PROJ_MAP_CELLS;c++){
        s=PS->active_slot[l][c];
        if(s>=PROJ_TILE_SLOTS){fprintf(stderr,"bad active slot %u\n",s);return 2;}
        if(s)counts[s]++;
    }
    for(l=0;l<PROJ_LAYERS;l++){ unsigned plane=l&1u; int32_t ox=PS->origin_x[plane],oy=PS->origin_y[plane]; unsigned yy,xx;
      for(yy=0;yy<PROJ_COVER_H;yy++)for(xx=0;xx<PROJ_COVER_W;xx++){ unsigned cell=(imod(oy+(int32_t)yy,32u)<<5)|imod(ox+(int32_t)xx,32u); s=PS->active_slot[l][cell];
        if((BG_VRAM16[(sb+l)*1024u+cell]&0x3ffu)!=s){fprintf(stderr,"visible map mismatch s=%u step=%u l=%u c=%u active=%u map=%u\n",scen,step,l,cell,s,BG_VRAM16[(sb+l)*1024u+cell]&0x3ffu);return 3;}
      }}
    for(s=1;s<PROJ_TILE_SLOTS;s++)if(PS->slot_ref[s]!=counts[s]){fprintf(stderr,"ref mismatch s=%u step=%u slot=%u ref=%u expected=%u\n",scen,step,s,PS->slot_ref[s],counts[s]);return 4;}
    return 0;
}
int main(void){
    static Md2GbaVdp v; unsigned scen,step,i;
    void*m=mmap((void*)0x04000000,0x04000000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0); if(m==MAP_FAILED)return 99;
    for(scen=0;scen<40;scen++){
        rs=0x9E3779B97F4A7C15ull*(scen+1u); md2gba_gba_projected_reset(); memset(&v,0,sizeof v);
        v.regs[2]=0x10;v.regs[4]=3;v.regs[5]=0x38;v.regs[16]=1;v.regs[12]=0x81;v.regs[1]=0x40;v.present_scroll_valid=1;
        tile(&v,0x6e9,2);tile(&v,0x6ea,3);tile(&v,0x6f3,4);tile(&v,7,5);for(i=0;i<24;i++)pattern(&v,i);tile(&v,0,0);
        for(i=0;i<2048;i++){namew(&v,0x4000,i,mapname(i%64u,i/64u,1u));namew(&v,0x6000,i,mapname(i%64u,i/64u,3u));}
        for(i=0;i<64;i++)v.cram_gba[i]=(uint16_t)(i*977u);
        v.present_hscroll[0]=(int16_t)(rnd()%512);v.present_vscroll[0]=(int16_t)(rnd()%256);v.present_hscroll[1]=(int16_t)(rnd()%512);v.present_vscroll[1]=(int16_t)(rnd()%256);
        memset(v.vram_dirty_tiles,0,sizeof v.vram_dirty_tiles);panel_open=0;settle(&v); if(check_refs(scen,9999))return 10;
        for(step=0;step<80;step++){
            unsigned op=rnd()%10u,x,y,p;
            if(op==0||op==1){pattern(&v,rnd()%24u);} else if(op==2){p=rnd()%2u;unsigned w=1+rnd()%4,h=1+rnd()%4,sx=rnd()%60,sy=rnd()%28;for(y=0;y<h;y++)for(x=0;x<w;x++)set_name(&v,p?0x6000:0x4000,(sy+y)*64+sx+x,rname());}
            else if(op==3){p=rnd()%2u;unsigned sy=rnd()%8,salt=rnd()%9u;for(y=0;y<24;y++)for(x=0;x<40;x++)set_name(&v,p?0x6000:0x4000,((sy+y)&31)*64+x,mapname(x,y,salt));}
            else if(op==4||op==5){if(panel_open)panel(&v,0);else panel(&v,1);} else if(op==6){p=rnd()%2u;v.present_hscroll[p]=(int16_t)(v.present_hscroll[p]+(int)(rnd()%200)-100);v.present_vscroll[p]=(int16_t)(v.present_vscroll[p]+(int)(rnd()%120)-60);v.present_scroll_epoch++;}
            else if(op==7){p=rnd()%2u;v.present_hscroll[p]=(int16_t)(v.present_hscroll[p]+(int)(rnd()%5)-2);v.present_vscroll[p]=(int16_t)(v.present_vscroll[p]+(int)(rnd()%5)-2);v.present_scroll_epoch++;}
            else if(op==8){v.cram_gba[rnd()%64]=(uint16_t)rnd();v.cram_epoch++;} else {unsigned t=rnd()%24u;markw(&v,t*32u);}
            v.vram_epoch++; settle(&v); if(check_refs(scen,step)) return 20;
        }
    }
    puts("PASS P135 slot ref/map ownership integrity across 3200 randomized settled states"); return 0;
}
