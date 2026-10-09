#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "md2gba_aot68k.h"

static uint32_t mask_for(unsigned s){return s==1?0xffu:(s==2?0xffffu:0xffffffffu);} 
static uint32_t sign_for(unsigned s){return s==1?0x80u:(s==2?0x8000u:0x80000000u);} 

static void ref_shift(Md2Gba68k*c,unsigned s,unsigned dr,unsigned kind,int left,unsigned count){
    uint32_t m=mask_for(s),sg=sign_for(s),v=c->d[dr]&m;
    unsigned oldx=!!(c->sr&MD68K_X),carry=0,ov=0,i;
    count&=63u;
    if(count){
        for(i=0;i<count;i++){
            if(kind==MD68K_SHIFT_LS){if(left){carry=!!(v&sg);v=(v<<1)&m;}else{carry=v&1u;v>>=1;}}
            else if(kind==MD68K_SHIFT_AS){if(left){unsigned os=!!(v&sg);carry=os;v=(v<<1)&m;if(!!(v&sg)!=os)ov=1;}else{carry=v&1u;v=((v>>1)|((v&sg)?sg:0))&m;}}
            else if(kind==MD68K_SHIFT_RO){if(left){carry=!!(v&sg);v=((v<<1)&m)|carry;}else{carry=v&1u;v=(v>>1)|(carry?sg:0);}}
            else {if(left){carry=!!(v&sg);v=((v<<1)&m)|oldx;oldx=carry;}else{carry=v&1u;v=(v>>1)|(oldx?sg:0);oldx=carry;}}
        }
        c->sr&=(uint16_t)~(MD68K_N|MD68K_Z|MD68K_V|MD68K_C);
        if(kind==MD68K_SHIFT_AS||kind==MD68K_SHIFT_LS||kind==MD68K_SHIFT_ROX)c->sr&=(uint16_t)~MD68K_X;
        if(!v)c->sr|=MD68K_Z;if(v&sg)c->sr|=MD68K_N;if(ov)c->sr|=MD68K_V;if(carry)c->sr|=MD68K_C;
        if((kind==MD68K_SHIFT_AS||kind==MD68K_SHIFT_LS||kind==MD68K_SHIFT_ROX)&&carry)c->sr|=MD68K_X;
    }else{
        c->sr&=(uint16_t)~(MD68K_N|MD68K_Z|MD68K_V|MD68K_C);
        if(!v)c->sr|=MD68K_Z;if(v&sg)c->sr|=MD68K_N;
        if(kind==MD68K_SHIFT_ROX && oldx)c->sr|=MD68K_C;
    }
    {uint32_t keep=s==1?0xffffff00u:(s==2?0xffff0000u:0);c->d[dr]=(c->d[dr]&keep)|v;}
}

static uint32_t rnd=0x12345678u;
static uint32_t rng(void){rnd=rnd*1664525u+1013904223u;return rnd;}

static uint32_t ref_divu(Md2Gba68k*c,uint32_t dividend,uint32_t divisor){
    uint32_t q,r; divisor&=0xffffu;
    q=dividend/divisor; r=dividend%divisor;
    c->sr&=(uint16_t)~(MD68K_N|MD68K_Z|MD68K_V|MD68K_C);
    if(q>0xffffu){c->sr|=MD68K_V;return dividend;}
    if(!(q&0xffffu))c->sr|=MD68K_Z;if(q&0x8000u)c->sr|=MD68K_N;
    return ((r&0xffffu)<<16)|(q&0xffffu);
}
static uint32_t ref_divs(Md2Gba68k*c,uint32_t dividend,uint32_t divisor){
    int64_t dv=(int32_t)dividend, ds=(int16_t)(divisor&0xffffu),q=dv/ds,r=dv%ds;
    c->sr&=(uint16_t)~(MD68K_N|MD68K_Z|MD68K_V|MD68K_C);
    if(q < -32768 || q > 32767){c->sr|=MD68K_V;return dividend;}
    if(q==0)c->sr|=MD68K_Z;if(q<0)c->sr|=MD68K_N;
    return ((uint32_t)((uint16_t)r)<<16)|(uint16_t)q;
}
int main(void){
    Md2GbaBus b; memset(&b,0,sizeof(b));
    for(unsigned sidx=0;sidx<3;sidx++){
        unsigned s=sidx==0?1u:(sidx==1?2u:4u);
        for(unsigned kind=0;kind<4;kind++)for(unsigned left=0;left<2;left++)for(unsigned count=0;count<64;count++){
            for(unsigned n=0;n<200;n++){
                Md2Gba68k a={0},r={0}; a.d[0]=r.d[0]=rng(); a.sr=r.sr=(uint16_t)rng(); a.d[1]=count;
                md68k_shift(&a,s,0,kind,(int)left,1,1); ref_shift(&r,s,0,kind,(int)left,count);
                if(a.d[0]!=r.d[0]||a.sr!=r.sr){printf("shift mismatch s=%u k=%u l=%u c=%u %08x/%08x %04x/%04x\n",s,kind,left,count,a.d[0],r.d[0],a.sr,r.sr);return 1;}
            }
        }
    }
    const uint32_t divu_ds[]={1,2,3,7,255,256,32767,32768,65535};
    const uint32_t divs_ds[]={1,2,3,7,0x7fff,0x8000,0xffff,0xfffe};
    for(unsigned n=0;n<200000;n++){
        uint32_t dv=rng(), d=divu_ds[rng()%(sizeof(divu_ds)/sizeof(divu_ds[0]))]; Md2Gba68k a={0},r={0}; a.sr=r.sr=(uint16_t)rng();
        uint32_t av=md68k_divu(&a,&b,dv,d,0),rv=ref_divu(&r,dv,d); if(av!=rv||a.sr!=r.sr){printf("divu mismatch %08x/%x -> %08x/%08x %04x/%04x\n",dv,d,av,rv,a.sr,r.sr);return 2;}
        d=divs_ds[rng()%(sizeof(divs_ds)/sizeof(divs_ds[0]))]; a=(Md2Gba68k){0};r=(Md2Gba68k){0};a.sr=r.sr=(uint16_t)rng();
        av=md68k_divs(&a,&b,dv,d,0);rv=ref_divs(&r,dv,d);if(av!=rv||a.sr!=r.sr){printf("divs mismatch %08x/%04x -> %08x/%08x %04x/%04x\n",dv,d,av,rv,a.sr,r.sr);return 3;}
    }
    puts("PASS P128 closed-form shift + q16 DIV differential");return 0;
}
