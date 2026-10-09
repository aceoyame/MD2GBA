#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "md2gba_gba_pcm_stream.h"
#include "md2gba_psg_pcm.h"

#define B 128u
#define RB 8u
#define LOGICAL_BLOCKS 48u
#define TOTAL (B*LOGICAL_BLOCKS)
#define SERVICE_EVERY_QUANTA 4u

static int req(int ok,const char *msg){if(!ok){fprintf(stderr,"FAIL %s\n",msg);return 0;}return 1;}
static void setup(Md2GbaPsgPcm *p){
    static const uint8_t w[]={0x80,0x10,0x92,0xA0,0x18,0xB4,0xC0,0x20,0xD6,0xE4,0xF8};
    unsigned i; md2gba_psg_pcm_init(p,MD2GBA_PSG_NTSC_CLOCK,16384u);
    for(i=0;i<sizeof(w);i++)md2gba_psg_pcm_write(p,w[i]);
}

int main(void){
    static int8_t reference[TOTAL],output[TOTAL],ring[B*RB];
    Md2GbaPsgPcm ref,run;
    uint32_t ready=(1u<<RB)-1u,refill_mask=0u;
    unsigned physical=0u,offset=0u,out=0u,refill_cursor=0u;
    unsigned refill_active=0u,refill_block=0u,refill_offset=0u;
    unsigned underruns=0u,completed_refills=0u,quantum=0u,i;
    int ok=1;

    ok&=req(MD2GBA_GBA_PCM_FIFO_BURST_SAMPLES==16u,"IRQ transport quantum is 16 samples");
    ok&=req(MD2GBA_GBA_PCM_REFILL_SLICE_SAMPLES==64u,"foreground producer slice is 64 samples");
    if(!ok)return 1;

    setup(&ref); setup(&run);
    md2gba_psg_pcm_render(&ref,reference,sizeof(reference));
    for(i=0;i<RB;i++)md2gba_psg_pcm_render(&run,ring+i*B,B);

    memcpy(output,ring,32u); out=32u; offset=32u; ready&=~1u;
    while(out<TOTAL){
        unsigned n=(TOTAL-out)>=16u?16u:(TOTAL-out);
        memcpy(output+out,ring+physical*B+offset,n); out+=n; quantum++;
        if(n<16u)break;
        offset+=16u;
        if(offset>=B){
            unsigned old=physical,next=(physical+1u)%RB;
            if(!(ready&(1u<<next)))underruns++; else ready&=~(1u<<next);
            physical=next; offset=0u; refill_mask|=(1u<<old);
        }

        /* Simulate a foreground that only gets one producer opportunity every
           four IRQ quanta (~3.9 ms), much less often than the IRQ consumer. */
        if((quantum%SERVICE_EVERY_QUANTA)==0u){
            if(!refill_active && refill_mask){
                unsigned scan,found=RB;
                for(scan=0;scan<RB;scan++){
                    unsigned b=(refill_cursor+scan)%RB;
                    if(refill_mask&(1u<<b)){found=b;break;}
                }
                if(found<RB){refill_mask&=~(1u<<found);refill_active=1u;refill_block=found;refill_offset=0u;}
            }
            if(refill_active){
                unsigned rem=B-refill_offset;
                unsigned slice=rem<MD2GBA_GBA_PCM_REFILL_SLICE_SAMPLES?rem:MD2GBA_GBA_PCM_REFILL_SLICE_SAMPLES;
                md2gba_psg_pcm_render(&run,ring+refill_block*B+refill_offset,slice);
                refill_offset+=slice;
                if(refill_offset>=B){
                    ready|=(1u<<refill_block); refill_cursor=(refill_block+1u)%RB;
                    refill_active=0u; completed_refills++;
                }
            }
        }
    }

    ok&=req(out==TOTAL,"IRQ consumer emitted full span");
    ok&=req(underruns==0u,"64-sample producer stays ahead with 3.9ms service cadence");
    ok&=req(memcmp(output,reference,TOTAL)==0,"IRQ-consumer / foreground-producer ring stays sample-continuous");
    ok&=req(completed_refills>=LOGICAL_BLOCKS-RB,"consumed blocks were regenerated");
    if(ok)puts("PASS P85 eight-block 128-sample ring / 64-sample producer continuity");
    return ok?0:1;
}
