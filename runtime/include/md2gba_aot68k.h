#ifndef MD2GBA_AOT68K_H
#define MD2GBA_AOT68K_H
#include <stdint.h>
#include "md2gba_bus.h"

/* Minimal native 68000 context used by ahead-of-time generated regions.
   The generated code contains no 68000 opcode decoder. */
typedef struct Md2Gba68k {
    uint32_t d[8];
    uint32_t a[8];
    uint32_t pc;
    uint32_t usp;
    uint16_t sr;
} Md2Gba68k;

enum {
    MD68K_EA_DN=0, MD68K_EA_AN, MD68K_EA_AN_IND, MD68K_EA_AN_POSTINC,
    MD68K_EA_AN_PREDEC, MD68K_EA_AN_DISP16, MD68K_EA_AN_INDEX8,
    MD68K_EA_ABS_W, MD68K_EA_ABS_L, MD68K_EA_PC_DISP16,
    MD68K_EA_PC_INDEX8, MD68K_EA_IMM
};
enum { MD68K_SZ_B=1, MD68K_SZ_W=2, MD68K_SZ_L=4 };
enum { MD68K_C=1, MD68K_V=2, MD68K_Z=4, MD68K_N=8, MD68K_X=16 };
enum { MD68K_SHIFT_AS=0, MD68K_SHIFT_LS=1, MD68K_SHIFT_ROX=2, MD68K_SHIFT_RO=3 };

void md68k_reset_context(Md2Gba68k *c);
int md68k_raise_autovector(Md2Gba68k *c, Md2GbaBus *b, unsigned level);
uint32_t md68k_mask(unsigned sz);
uint32_t md68k_sign(unsigned sz);
int32_t md68k_sx(uint32_t v,unsigned bits);
void md68k_set_nz(Md2Gba68k *c,unsigned sz,uint32_t v);
int md68k_cond(const Md2Gba68k *c,unsigned cc);
uint32_t md68k_add(Md2Gba68k *c,unsigned sz,uint32_t dst,uint32_t src,int setx);
uint32_t md68k_sub(Md2Gba68k *c,unsigned sz,uint32_t dst,uint32_t src,int setx);
uint32_t md68k_divu(Md2Gba68k *c,Md2GbaBus *b,uint32_t dividend,uint32_t divisor,uint32_t fault_pc);
uint32_t md68k_divs(Md2Gba68k *c,Md2GbaBus *b,uint32_t dividend,uint32_t divisor,uint32_t fault_pc);
uint32_t md68k_ea_addr(Md2Gba68k *c,int kind,int reg,uint32_t value,int32_t disp,uint16_t ext,unsigned sz,uint32_t pc,int pre);
uint32_t md68k_ea_arch_addr(Md2Gba68k *c,int kind,int reg,uint32_t value,int32_t disp,uint16_t ext,uint32_t pc);
uint32_t md68k_rd(Md2Gba68k *c,Md2GbaBus *b,int kind,int reg,uint32_t value,int32_t disp,uint16_t ext,unsigned sz,uint32_t pc);
void md68k_wr(Md2Gba68k *c,Md2GbaBus *b,int kind,int reg,uint32_t value,int32_t disp,uint16_t ext,unsigned sz,uint32_t v,uint32_t pc);
void md68k_push32(Md2Gba68k *c,Md2GbaBus *b,uint32_t v);
uint32_t md68k_pop32(Md2Gba68k *c,Md2GbaBus *b);
void md68k_push16(Md2Gba68k *c,Md2GbaBus *b,uint16_t v);
uint16_t md68k_pop16(Md2Gba68k *c,Md2GbaBus *b);
void md68k_shift(Md2Gba68k *c,unsigned sz,unsigned dst_d,unsigned kind,int left,int count_is_reg,unsigned count_or_reg);
uint32_t md68k_shift_value(Md2Gba68k *c,unsigned sz,uint32_t value,unsigned kind,int left,unsigned count);
void md68k_movem_store(Md2Gba68k *c,Md2GbaBus *b,uint16_t mask,unsigned sz,int ea_kind,int ea_reg,uint32_t ea_value,int32_t ea_disp,uint16_t ea_ext,uint32_t pc);
void md68k_movem_load(Md2Gba68k *c,Md2GbaBus *b,uint16_t mask,unsigned sz,int ea_kind,int ea_reg,uint32_t ea_value,int32_t ea_disp,uint16_t ea_ext,uint32_t pc);
void md68k_movem_store_a7_l(Md2Gba68k *c,Md2GbaBus *b,uint16_t mask);
void md68k_movem_load_a7_l(Md2Gba68k *c,Md2GbaBus *b,uint16_t mask);


#if defined(MD2GBA_AOT_GENERATED_FAST)
#if defined(__GNUC__) || defined(__clang__)
#define MD68K_GEN_INLINE static __attribute__((always_inline)) inline
#else
#define MD68K_GEN_INLINE static inline
#endif
MD68K_GEN_INLINE uint32_t md68k_gen_mask(unsigned s){return s==1u?0xffu:(s==2u?0xffffu:0xffffffffu);}
MD68K_GEN_INLINE uint32_t md68k_gen_sign(unsigned s){return s==1u?0x80u:(s==2u?0x8000u:0x80000000u);}
MD68K_GEN_INLINE int32_t md68k_gen_sx(uint32_t v,unsigned bits){uint32_t m=1u<<(bits-1u);return (int32_t)((v^m)-m);}
MD68K_GEN_INLINE unsigned md68k_gen_incsz(unsigned s,unsigned r){return s==1u&&r==7u?2u:s;}
MD68K_GEN_INLINE int32_t md68k_gen_indexv(const Md2Gba68k*c,uint16_t e){unsigned a=!!(e&0x8000u),r=(e>>12)&7u,l=!!(e&0x0800u);uint32_t v=a?c->a[r]:c->d[r];return l?(int32_t)v:md68k_gen_sx(v&0xffffu,16u);}
MD68K_GEN_INLINE uint32_t md68k_add_gen(Md2Gba68k*c,unsigned s,uint32_t dst,uint32_t src,int setx){uint32_t m=md68k_gen_mask(s),sg=md68k_gen_sign(s),r,carry;dst&=m;src&=m;carry=(src>(m-dst));r=(dst+src)&m;c->sr&=(uint16_t)~(MD68K_N|MD68K_Z|MD68K_V|MD68K_C|(setx?MD68K_X:0));if(!r)c->sr|=MD68K_Z;if(r&sg)c->sr|=MD68K_N;if((~(dst^src)&(dst^r)&sg)!=0)c->sr|=MD68K_V;if(carry)c->sr|=(uint16_t)(MD68K_C|(setx?MD68K_X:0));return r;}
MD68K_GEN_INLINE uint32_t md68k_sub_gen(Md2Gba68k*c,unsigned s,uint32_t dst,uint32_t src,int setx){uint32_t m=md68k_gen_mask(s),sg=md68k_gen_sign(s),r;dst&=m;src&=m;r=(dst-src)&m;c->sr&=(uint16_t)~(MD68K_N|MD68K_Z|MD68K_V|MD68K_C|(setx?MD68K_X:0));if(!r)c->sr|=MD68K_Z;if(r&sg)c->sr|=MD68K_N;if(((dst^src)&(dst^r)&sg)!=0)c->sr|=MD68K_V;if(src>dst)c->sr|=(uint16_t)(MD68K_C|(setx?MD68K_X:0));return r;}
MD68K_GEN_INLINE uint32_t md68k_ea_addr_gen(Md2Gba68k*c,int k,int r,uint32_t value,int32_t disp,uint16_t ext,unsigned s,uint32_t pc,int pre){uint32_t a;switch(k){case MD68K_EA_AN_IND:case MD68K_EA_AN_POSTINC:return c->a[r]&0xffffffu;case MD68K_EA_AN_PREDEC:if(pre)c->a[r]-=md68k_gen_incsz(s,(unsigned)r);return c->a[r]&0xffffffu;case MD68K_EA_AN_DISP16:return (c->a[r]+(uint32_t)disp)&0xffffffu;case MD68K_EA_AN_INDEX8:return (c->a[r]+(uint32_t)disp+(uint32_t)md68k_gen_indexv(c,ext))&0xffffffu;case MD68K_EA_ABS_W:a=value&0xffffu;return ((a&0x8000u)?(a|0xff0000u):a)&0xffffffu;case MD68K_EA_ABS_L:return value&0xffffffu;case MD68K_EA_PC_DISP16:return (pc+2u+(uint32_t)disp)&0xffffffu;case MD68K_EA_PC_INDEX8:return (pc+2u+(uint32_t)disp+(uint32_t)md68k_gen_indexv(c,ext))&0xffffffu;default:return 0u;}}
MD68K_GEN_INLINE uint32_t md68k_ea_arch_addr_gen(Md2Gba68k*c,int k,int r,uint32_t value,int32_t disp,uint16_t ext,uint32_t pc){uint32_t a;switch(k){case MD68K_EA_AN_IND:case MD68K_EA_AN_POSTINC:case MD68K_EA_AN_PREDEC:return c->a[r];case MD68K_EA_AN_DISP16:return c->a[r]+(uint32_t)disp;case MD68K_EA_AN_INDEX8:return c->a[r]+(uint32_t)disp+(uint32_t)md68k_gen_indexv(c,ext);case MD68K_EA_ABS_W:a=value&0xffffu;return (uint32_t)(int32_t)(int16_t)a;case MD68K_EA_ABS_L:return value;case MD68K_EA_PC_DISP16:return pc+2u+(uint32_t)disp;case MD68K_EA_PC_INDEX8:return pc+2u+(uint32_t)disp+(uint32_t)md68k_gen_indexv(c,ext);default:return 0u;}}
MD68K_GEN_INLINE uint32_t md68k_rd_gen(Md2Gba68k*c,Md2GbaBus*b,int k,int r,uint32_t value,int32_t disp,uint16_t ext,unsigned s,uint32_t pc){uint32_t a,v,m=md68k_gen_mask(s);if(k==MD68K_EA_DN)return c->d[r]&m;if(k==MD68K_EA_AN)return c->a[r]&m;if(k==MD68K_EA_IMM)return value&m;a=md68k_ea_addr_gen(c,k,r,value,disp,ext,s,pc,1);v=s==1u?md2gba_read8_fast(b,a):(s==2u?md2gba_read16(b,a):md2gba_read32(b,a));if(k==MD68K_EA_AN_POSTINC)c->a[r]+=md68k_gen_incsz(s,(unsigned)r);return v&m;}
MD68K_GEN_INLINE void md68k_wr_gen(Md2Gba68k*c,Md2GbaBus*b,int k,int r,uint32_t value,int32_t disp,uint16_t ext,unsigned s,uint32_t v,uint32_t pc){uint32_t a,m=md68k_gen_mask(s);v&=m;if(k==MD68K_EA_DN){uint32_t keep=s==1u?0xffffff00u:(s==2u?0xffff0000u:0u);c->d[r]=(c->d[r]&keep)|v;return;}if(k==MD68K_EA_AN){c->a[r]=s==2u?(uint32_t)md68k_gen_sx(v,16u):v;return;}a=md68k_ea_addr_gen(c,k,r,value,disp,ext,s,pc,1);if(s==1u)md2gba_write8_fast(b,a,(uint8_t)v);else if(s==2u)md2gba_write16(b,a,(uint16_t)v);else md2gba_write32(b,a,v);if(k==MD68K_EA_AN_POSTINC)c->a[r]+=md68k_gen_incsz(s,(unsigned)r);}
#define md68k_add md68k_add_gen
#define md68k_sub md68k_sub_gen
#define md68k_ea_addr md68k_ea_addr_gen
#define md68k_ea_arch_addr md68k_ea_arch_addr_gen
/* PSC-style A/B: keep arithmetic/address-only transforms inline, but allow
   memory operand reads/writes to use the shared IWRAM helpers. PSC's native
   modules lean on a small shared compatibility core; on ARM7/GamePak this can
   trade a cheap Thumb call for substantially less ROM fetch and register
   pressure. Default remains fully inline until hardware A/B proves otherwise. */
#if !defined(MD2GBA_AOT_SHARED_MEM_HELPERS)
#define md68k_rd md68k_rd_gen
#define md68k_wr md68k_wr_gen
#endif
#undef MD68K_GEN_INLINE
#endif

#endif
