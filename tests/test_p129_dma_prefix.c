#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../md2gba_bus.c"

static Md2GbaBus a,b;
static uint8_t rom[0x20000];
static uint32_t rng=0x129d00d5u;
static uint32_t next32(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}

static int ref_bulk(Md2GbaBus*x,uint32_t src,unsigned words){
    Md2GbaVdp*v=&x->vdp; const uint8_t*p; unsigned bytes=words*2u,i=0; uint16_t dst=v->address; uint32_t diff=0u;
    if((v->regs[15]?v->regs[15]:2u)!=2u || !vdp_dma_source_ptr(x,src,bytes,&p))return 0;
    if((unsigned)dst+bytes<=0x10000u && ((((uintptr_t)(v->vram+dst))|(uintptr_t)p)&3u)==0u){
        for(;i+16u<=bytes;i+=16u){
            uint32_t*n=(uint32_t*)(void*)(v->vram+dst+i); const uint32_t*q=(const uint32_t*)(const void*)(p+i);
            uint32_t q0=q[0],q1=q[1],q2=q[2],q3=q[3];
            diff|=n[0]^q0;diff|=n[1]^q1;diff|=n[2]^q2;diff|=n[3]^q3;
            n[0]=q0;n[1]=q1;n[2]=q2;n[3]=q3;
        }
        for(;i+4u<=bytes;i+=4u){uint32_t*n=(uint32_t*)(void*)(v->vram+dst+i),q=*(const uint32_t*)(const void*)(p+i);diff|=*n^q;*n=q;}
        for(;i<bytes;i++){uint8_t q=p[i];diff|=(uint32_t)(v->vram[dst+i]^q);v->vram[dst+i]=q;}
    }else{
        for(i=0;i<bytes;i++){uint16_t aa=(uint16_t)(dst+i);uint8_t q=p[i];diff|=(uint32_t)(v->vram[aa]^q);v->vram[aa]=q;}
    }
    if(diff){vdp_mark_vram_span(v,dst,bytes);if(vdp_vram_span_touches_present_scroll(v,dst,bytes))vdp_refresh_present_scroll(v);}
    v->address=(uint16_t)(dst+bytes); return 1;
}

int main(void){
    for(unsigned i=0;i<sizeof rom;i++)rom[i]=(uint8_t)next32();
    for(unsigned it=0;it<12000u;it++){
        md2gba_bus_init(&a,rom,sizeof rom);
        a.vdp.target=MD2GBA_VDP_VRAM;a.vdp.regs[15]=2u;
        unsigned words=1u+(next32()%2048u),bytes=words*2u;
        uint32_t src=(next32()%(sizeof rom-bytes))&~3u;
        unsigned maxdst=0x10000u-bytes; uint16_t dst=(uint16_t)((next32()%(maxdst+1u))&~3u);
        a.vdp.address=dst;
        for(unsigned i=0;i<0x10000u;i++)a.vdp.vram[i]=(uint8_t)next32();
        /* Bias heavily toward the repeated-DMA workload but keep dense and
           late-change cases. Prefix is rounded to the 16-byte helper granule. */
        unsigned chunks=bytes/16u,match_chunks=0u;
        switch(it%4u){
          case 0: match_chunks=chunks; break;
          case 1: match_chunks=chunks?next32()%(chunks+1u):0u; break;
          case 2: match_chunks=0u; break;
          default: match_chunks=chunks?(chunks*3u)/4u:0u; break;
        }
        if(match_chunks)memcpy(a.vdp.vram+dst,rom+src,match_chunks*16u);
        b=a;
        int ra=ref_bulk(&a,src,words),rb=vdp_dma_bulk_vram(&b,src,words);
        if(ra!=rb || memcmp(&a.vdp,&b.vdp,sizeof a.vdp)!=0){
            printf("FAIL DMA differential it=%u words=%u src=%x dst=%x prefix_chunks=%u ra=%d rb=%d\n",it,words,src,dst,match_chunks,ra,rb);return 1;
        }
    }
    puts("PASS P129 DMA unchanged-prefix differential: 12,000 canonical bus states");
    return 0;
}
