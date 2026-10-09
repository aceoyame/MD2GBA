#ifndef MD2GBA_CPU_H
#define MD2GBA_CPU_H
#include <stdint.h>

enum {
    MD2GBA_SR_C = 0x0001,
    MD2GBA_SR_V = 0x0002,
    MD2GBA_SR_Z = 0x0004,
    MD2GBA_SR_N = 0x0008,
    MD2GBA_SR_X = 0x0010
};

typedef struct Md2GbaM68kContext {
    uint32_t d[8];
    uint32_t a[8];
    uint32_t pc;
    uint16_t sr;
} Md2GbaM68kContext;

static inline void md2gba_set_nz8(Md2GbaM68kContext *c, uint8_t v) {
    c->sr &= (uint16_t)~(MD2GBA_SR_N | MD2GBA_SR_Z | MD2GBA_SR_V | MD2GBA_SR_C);
    if(!v) c->sr |= MD2GBA_SR_Z;
    if(v & 0x80) c->sr |= MD2GBA_SR_N;
}
static inline void md2gba_set_nz16(Md2GbaM68kContext *c, uint16_t v) {
    c->sr &= (uint16_t)~(MD2GBA_SR_N | MD2GBA_SR_Z | MD2GBA_SR_V | MD2GBA_SR_C);
    if(!v) c->sr |= MD2GBA_SR_Z;
    if(v & 0x8000) c->sr |= MD2GBA_SR_N;
}
static inline void md2gba_set_nz32(Md2GbaM68kContext *c, uint32_t v) {
    c->sr &= (uint16_t)~(MD2GBA_SR_N | MD2GBA_SR_Z | MD2GBA_SR_V | MD2GBA_SR_C);
    if(!v) c->sr |= MD2GBA_SR_Z;
    if(v & 0x80000000u) c->sr |= MD2GBA_SR_N;
}
static inline int md2gba_flag_z(const Md2GbaM68kContext *c) { return !!(c->sr & MD2GBA_SR_Z); }

static inline void md2gba_set_z_only(Md2GbaM68kContext *c, int is_zero) {
    c->sr &= (uint16_t)~MD2GBA_SR_Z;
    if(is_zero) c->sr |= MD2GBA_SR_Z;
}
static inline uint16_t md2gba_add16(Md2GbaM68kContext *c, uint16_t dst, uint16_t src) {
    uint32_t wide=(uint32_t)dst+(uint32_t)src; uint16_t r=(uint16_t)wide;
    c->sr &= (uint16_t)~(MD2GBA_SR_N|MD2GBA_SR_Z|MD2GBA_SR_V|MD2GBA_SR_C|MD2GBA_SR_X);
    if(!r) c->sr|=MD2GBA_SR_Z; if(r&0x8000u)c->sr|=MD2GBA_SR_N;
    if((~(dst^src)&(dst^r)&0x8000u)!=0)c->sr|=MD2GBA_SR_V;
    if(wide&0x10000u)c->sr|=(MD2GBA_SR_C|MD2GBA_SR_X);
    return r;
}
static inline void md2gba_cmp16(Md2GbaM68kContext *c, uint16_t dst, uint16_t src) {
    uint16_t r=(uint16_t)(dst-src);
    c->sr &= (uint16_t)~(MD2GBA_SR_N|MD2GBA_SR_Z|MD2GBA_SR_V|MD2GBA_SR_C);
    if(!r)c->sr|=MD2GBA_SR_Z; if(r&0x8000u)c->sr|=MD2GBA_SR_N;
    if(((dst^src)&(dst^r)&0x8000u)!=0)c->sr|=MD2GBA_SR_V;
    if(src>dst)c->sr|=MD2GBA_SR_C;
}
static inline void md2gba_cmp32(Md2GbaM68kContext *c, uint32_t dst, uint32_t src) {
    uint32_t r=dst-src;
    c->sr &= (uint16_t)~(MD2GBA_SR_N|MD2GBA_SR_Z|MD2GBA_SR_V|MD2GBA_SR_C);
    if(!r)c->sr|=MD2GBA_SR_Z; if(r&0x80000000u)c->sr|=MD2GBA_SR_N;
    if(((dst^src)&(dst^r)&0x80000000u)!=0)c->sr|=MD2GBA_SR_V;
    if(src>dst)c->sr|=MD2GBA_SR_C;
}
static inline int md2gba_condition_true(const Md2GbaM68kContext *c, unsigned cc) {
    int C=!!(c->sr&MD2GBA_SR_C), V=!!(c->sr&MD2GBA_SR_V), Z=!!(c->sr&MD2GBA_SR_Z), N=!!(c->sr&MD2GBA_SR_N);
    switch(cc&15u) {
        case 0: return 1;                 /* T  */
        case 1: return 0;                 /* F  */
        case 2: return !C && !Z;          /* HI */
        case 3: return C || Z;            /* LS */
        case 4: return !C;                /* CC */
        case 5: return C;                 /* CS */
        case 6: return !Z;                /* NE */
        case 7: return Z;                 /* EQ */
        case 8: return !V;                /* VC */
        case 9: return V;                 /* VS */
        case 10:return !N;                /* PL */
        case 11:return N;                 /* MI */
        case 12:return N==V;               /* GE */
        case 13:return N!=V;               /* LT */
        case 14:return !Z && (N==V);       /* GT */
        default:return Z || (N!=V);        /* LE */
    }
}
#endif
