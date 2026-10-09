#ifndef MD2GBA_PROJECTED_RETRY_H
#define MD2GBA_PROJECTED_RETRY_H
#include <stdint.h>

/* Retry policy for a target renderer that can decline current VDP geometry or
   a transient tile/OBJ capacity state. Geometry refusals need register changes;
   capacity refusals can recover after source writes, but are rate limited by
   guest VBlanks so many AOT yields cannot rebuild the same scene repeatedly. */
typedef struct Md2GbaProjectedRetry {
    uint32_t geometry_a,source_epoch,next_guest_vblank;
    uint16_t geometry_b;
    uint8_t blocked,failures; /* blocked: 0=ready, 1=geometry, 2=capacity */
} Md2GbaProjectedRetry;

static inline uint32_t md2gba_projected_geometry_a(const uint8_t *reg){
    return ((uint32_t)reg[1]<<24)|((uint32_t)reg[11]<<16)|((uint32_t)reg[12]<<8)|reg[16];
}
static inline uint16_t md2gba_projected_geometry_b(const uint8_t *reg){
    return (uint16_t)(((uint16_t)reg[2]<<8)|reg[4]);
}
static inline int md2gba_projected_retry_ready(const Md2GbaProjectedRetry *s,const uint8_t *reg,
                                                uint32_t source_epoch,uint32_t guest_vblank){
    if(!s->blocked||s->geometry_a!=md2gba_projected_geometry_a(reg)||
       s->geometry_b!=md2gba_projected_geometry_b(reg))return 1;
    return s->blocked==2u && s->source_epoch!=source_epoch &&
           (int32_t)(guest_vblank-s->next_guest_vblank)>=0;
}
static inline void md2gba_projected_retry_result(Md2GbaProjectedRetry *s,int result,
                                                  const uint8_t *reg,uint32_t source_epoch,uint32_t guest_vblank){
    uint32_t a=md2gba_projected_geometry_a(reg);uint16_t b=md2gba_projected_geometry_b(reg);
    if(result==0){s->blocked=0u;s->failures=0u;return;}
    if(result==-6||result==-1){s->blocked=1u;s->failures=0u;}
    else {
        if(s->blocked==2u&&s->geometry_a==a&&s->geometry_b==b){
            if(s->failures<3u)s->failures++;
        }else s->failures=0u;
        s->blocked=2u;
        s->next_guest_vblank=guest_vblank+(4u<<s->failures);
    }
    s->geometry_a=a;s->geometry_b=b;s->source_epoch=source_epoch;
}
#endif
