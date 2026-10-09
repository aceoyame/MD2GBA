#include <stdint.h>
#include <stdio.h>

static uint32_t ref_udiv32(uint32_t n,uint32_t d){uint32_t q=0,r=0;int i;if(!d)return 0;for(i=31;i>=0;i--){r=(r<<1)|((n>>(unsigned)i)&1u);if(r>=d){r-=d;q|=1u<<(unsigned)i;}}return q;}
static uint32_t p129_udiv32(uint32_t n,uint32_t d){uint32_t q=0u,bit=1u;if(!d)return 0u;if(n<d)return 0u;while(d<=(n>>1)){d<<=1;bit<<=1;}do{if(n>=d){n-=d;q|=bit;}d>>=1;bit>>=1;}while(bit);return q;}
static uint32_t rng=0x1295a17du;
static uint32_t next32(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
int main(void){
    const unsigned total=20800000u;
    for(unsigned i=0;i<total;i++){
        uint32_t n=next32(),d=next32();
        if((i&255u)==0u)d=0u;
        else if((i&255u)==1u)d=1u;
        else if((i&255u)==2u)d=0x80000000u;
        else if((i&255u)==3u)n=d;
        else if((i&255u)==4u)n=d?d-1u:0u;
        uint32_t a=p129_udiv32(n,d),b=ref_udiv32(n,d),c=d?n/d:0u;
        if(a!=b||a!=c){printf("FAIL i=%u n=%08x d=%08x got=%08x ref=%08x native=%08x\n",i,n,d,a,b,c);return 1;}
    }
    puts("PASS P129 exact divider: 20,800,000 comparisons vs P128 restoring divider and native division");
    return 0;
}
