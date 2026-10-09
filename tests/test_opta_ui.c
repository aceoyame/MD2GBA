#include <stdio.h>
#include <string.h>
#include "../md2gba_gba_backend.c"
#include "phase121_ui_reference.inc"
static Md2GbaVdp test_vdp;
static uint16_t expected_stage[16384];
static unsigned seed=122;
static unsigned rnd(void){seed=seed*1664525u+1013904223u;return seed;}
int main(void){
    unsigned count=0;
    for(unsigned width=31;width<=40;width++)for(unsigned height=3;height<=8;height++)for(unsigned iter=0;iter<10;iter++){
        Md2GbaGbaUiPanel p={0};unsigned pw=64;
        for(unsigned i=0;i<65536;i++)test_vdp.vram[i]=(uint8_t)(rnd()>>24);
        test_vdp.regs[2]=(uint8_t)(rnd()&0x38u);
        p.active=1;p.w=width;p.h=height;p.x=rnd()%(pw-width+1);p.y=rnd()%(64-height+1);
        memset(md2gba_bg_tile_stage,0x5a,sizeof(md2gba_bg_tile_stage));
        phase121_ui_stage_tiles(&test_vdp,&p,pw);
        memcpy(expected_stage,md2gba_bg_tile_stage,sizeof(expected_stage));
        memset(md2gba_bg_tile_stage,0x5a,sizeof(md2gba_bg_tile_stage));
        md2gba_ui_stage_tiles(&test_vdp,&p,pw);
        if(memcmp(expected_stage,md2gba_bg_tile_stage,sizeof(expected_stage))){printf("FAIL %u %u %u\n",width,height,iter);return 1;}
        count++;
    }
    printf("PASS OPT-A UI: %u randomized panels, every supported width/height, flips/palettes/wrap, full staging bytes equal\n",count);
}
