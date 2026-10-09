#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "md2gba_aot_region.h"
#include "md2gba_interp68k.h"
#include "md2gba_timing.h"
#include "ps4_whole_region.h"

static int run_interp_frame(Md2Gba68k *c, Md2GbaBus *b, Md2GbaTiming *t, uint32_t target_vblank){
    uint32_t steps=0;
    while(t->vblanks < target_vblank){
        uint32_t pc=c->pc;
        if(md68k_interp_step(c,b)!=1) return -1;
        if(md2gba_timing_after_step(c,b,pc,++steps,t)>0 && t->vblanks>=target_vblank) break;
    }
    return 0;
}

int main(int argc,char**argv){
    if(argc<3){fprintf(stderr,"usage: %s rom reversed [frames]\n",argv[0]);return 2;}
    unsigned frames=argc>3?(unsigned)strtoul(argv[3],0,0):120u;
    FILE*f=fopen(argv[1],"rb"); if(!f)return 2; fseek(f,0,SEEK_END); long n=ftell(f); rewind(f);
    uint8_t*rom=malloc((size_t)n),*rev=malloc((size_t)n); if(!rom||!rev)return 2;
    if(fread(rom,1,(size_t)n,f)!=(size_t)n){fclose(f);return 2;} fclose(f);
    f=fopen(argv[2],"rb"); if(!f)return 2; if(fread(rev,1,(size_t)n,f)!=(size_t)n){fclose(f);return 2;} fclose(f);

    Md2GbaBus ba,bi; Md2Gba68k ca,ci; Md2GbaTiming ta,ti; Md2GbaInterpStats st={0}; Md2GbaAotHooks h={0};
    md2gba_bus_init(&ba,rom,(size_t)n); md2gba_bus_init(&bi,rom,(size_t)n);
    md2gba_bus_set_reversed_rom(&ba,rev); md2gba_bus_set_reversed_rom(&bi,rev);
    md68k_reset_context(&ca); md68k_reset_context(&ci);
    ca.a[7]=md2gba_read32(&ba,0)&0xffffffu; ci.a[7]=md2gba_read32(&bi,0)&0xffffffu;
    ca.pc=md2gba_read32(&ba,4)&0xffffffu; ci.pc=md2gba_read32(&bi,4)&0xffffffu;
    md2gba_timing_init(&ta,0,1); md2gba_timing_enable_cycles(&ta,262,224,488); md2gba_timing_enable_scheduled(&ta);
    md2gba_timing_init(&ti,0,1); md2gba_timing_enable_cycles(&ti,262,224,488);
    h.fallback_pc=md68k_interp_fallback_hook; h.fallback_user=&st; h.scheduled_event=md2gba_timing_scheduled_event;h.scheduled_user=&ta;h.scheduled_countdown=&ta.scheduled_countdown;h.scheduled_overshoot=&ta.scheduled_overshoot;
    for(unsigned fr=1;fr<=frames;fr++){
        int rc;
        do{rc=ps4_whole_region_run(&ca,&ba,&h,MD2GBA_AOT_RESUME_PC,0x00ffffffu,100000000u);}while(rc==MD2GBA_AOT_STEP_LIMIT);
        if(rc!=MD2GBA_AOT_YIELD){printf("FAIL aot rc=%d frame=%u pc=%06x\n",rc,fr,ca.pc);return 1;}
        if(run_interp_frame(&ci,&bi,&ti,fr)){printf("FAIL interp frame=%u pc=%06x\n",fr,ci.pc);return 1;}
        if(memcmp(&ca,&ci,sizeof(ca))){
            printf("FAIL cpu frame=%u a=%06x i=%06x sr=%04x/%04x usp=%08x/%08x\n",fr,ca.pc,ci.pc,ca.sr,ci.sr,ca.usp,ci.usp);
            for(int q=0;q<8;q++) if(ca.d[q]!=ci.d[q]) printf(" D%d=%08x/%08x\n",q,ca.d[q],ci.d[q]);
            for(int q=0;q<8;q++) if(ca.a[q]!=ci.a[q]) printf(" A%d=%08x/%08x\n",q,ca.a[q],ci.a[q]);
            return 1;
        }
        if(memcmp(&ba.vdp,&bi.vdp,sizeof(ba.vdp))){printf("FAIL vdp frame=%u pc=%06x\n",fr,ca.pc);return 1;}
        for(unsigned a=0xff0000u;a<=0xffffffu;a++){
            uint8_t xa=md2gba_read8(&ba,a),xi=md2gba_read8(&bi,a);
            if(xa!=xi){printf("FAIL wram frame=%u addr=%06x %02x %02x\n",fr,a,xa,xi);return 1;}
        }
    }
    printf("PASS pschybrid == interpreter for %u guest frames; fallback steps=%u unsupported=%u\n",frames,st.steps,st.unsupported);
    return 0;
}
