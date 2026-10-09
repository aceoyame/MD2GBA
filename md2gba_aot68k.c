#include "md2gba_aot68k.h"
#if defined(MD2GBA_GBA_IWRAM)
#define MD2GBA_CPU_HOT __attribute__((section(".iwram")))
#else
#define MD2GBA_CPU_HOT
#endif

static void zmem(void *p,unsigned n){ uint8_t *q=(uint8_t*)p; while(n--)*q++=0; }
void md68k_reset_context(Md2Gba68k *c){ zmem(c,sizeof(*c)); c->sr=0x2700; }
MD2GBA_CPU_HOT int md68k_raise_autovector(Md2Gba68k *c,Md2GbaBus *b,unsigned level){
 uint16_t oldsr; unsigned mask; uint32_t vector;
 if(level<1u || level>7u)return 0;
 mask=(c->sr>>8)&7u; if(level!=7u && level<=mask)return 0;
 oldsr=c->sr; c->sr=(uint16_t)((c->sr|0x2000u)&~0x0700u); c->sr|=(uint16_t)(level<<8);
 md68k_push32(c,b,c->pc&0xFFFFFFu); md68k_push16(c,b,oldsr);
 vector=md2gba_read32(b,(24u+level)*4u)&0xFFFFFFu;
 if(!vector){ c->sr=md68k_pop16(c,b); c->pc=md68k_pop32(c,b); return 0; }
 c->pc=vector; return 1;
}
uint32_t md68k_mask(unsigned s){ return s==1?0xffu:(s==2?0xffffu:0xffffffffu); }
uint32_t md68k_sign(unsigned s){ return s==1?0x80u:(s==2?0x8000u:0x80000000u); }
MD2GBA_CPU_HOT int32_t md68k_sx(uint32_t v,unsigned bits){ uint32_t m=1u<<(bits-1); return (int32_t)((v^m)-m); }
static unsigned incsz(unsigned s,unsigned r){ return s==1 && r==7?2u:s; }
void md68k_set_nz(Md2Gba68k *c,unsigned s,uint32_t v){ uint32_t m=md68k_mask(s);v&=m;c->sr&=(uint16_t)~(MD68K_N|MD68K_Z|MD68K_V|MD68K_C);if(!v)c->sr|=MD68K_Z;if(v&md68k_sign(s))c->sr|=MD68K_N; }
int md68k_cond(const Md2Gba68k *c,unsigned cc){ int C=!!(c->sr&1),V=!!(c->sr&2),Z=!!(c->sr&4),N=!!(c->sr&8);switch(cc&15){case 0:return 1;case 1:return 0;case 2:return !C&&!Z;case 3:return C||Z;case 4:return !C;case 5:return C;case 6:return !Z;case 7:return Z;case 8:return !V;case 9:return V;case 10:return !N;case 11:return N;case 12:return N==V;case 13:return N!=V;case 14:return !Z&&(N==V);default:return Z||(N!=V);} }
uint32_t md68k_add(Md2Gba68k *c,unsigned s,uint32_t dst,uint32_t src,int setx){ uint32_t m=md68k_mask(s),sg=md68k_sign(s),r,carry;dst&=m;src&=m;carry=(src>(m-dst));r=(dst+src)&m;c->sr&=(uint16_t)~(MD68K_N|MD68K_Z|MD68K_V|MD68K_C|(setx?MD68K_X:0));if(!r)c->sr|=MD68K_Z;if(r&sg)c->sr|=MD68K_N;if((~(dst^src)&(dst^r)&sg)!=0)c->sr|=MD68K_V;if(carry)c->sr|=(uint16_t)(MD68K_C|(setx?MD68K_X:0));return r; }
uint32_t md68k_sub(Md2Gba68k *c,unsigned s,uint32_t dst,uint32_t src,int setx){ uint32_t m=md68k_mask(s),sg=md68k_sign(s),r;dst&=m;src&=m;r=(dst-src)&m;c->sr&=(uint16_t)~(MD68K_N|MD68K_Z|MD68K_V|MD68K_C|(setx?MD68K_X:0));if(!r)c->sr|=MD68K_Z;if(r&sg)c->sr|=MD68K_N;if(((dst^src)&(dst^r)&sg)!=0)c->sr|=MD68K_V;if(src>dst)c->sr|=(uint16_t)(MD68K_C|(setx?MD68K_X:0));return r; }
static void md68k_divzero(Md2Gba68k*c,Md2GbaBus*b,uint32_t fault_pc){
 uint16_t oldsr=c->sr;c->sr|=0x2000u;md68k_push32(c,b,fault_pc&0xFFFFFFu);md68k_push16(c,b,oldsr);c->pc=md2gba_read32(b,5u*4u)&0xFFFFFFu;
}
/* ARM7TDMI has no integer divide instruction. Keep 68K DIVU/DIVS self-contained
   instead of depending on compiler-runtime __aeabi_* division helpers. This cold
   helper is shared by AOT and fallback execution. */
/* B11/P128: after the 68K overflow precheck the quotient is guaranteed to
   fit in 16 bits.  Seed the restoring divider with the high dividend word and
   compute only the 16 quotient bits that can survive, instead of walking all
   32 bits on an ARM7TDMI with no hardware divide. */
static uint32_t md_u32_divmod_q16(uint32_t n,uint32_t d,uint32_t *rem){
 uint32_t q=0,r=n>>16; int i;
 for(i=15;i>=0;i--){ r=(r<<1)|((n>>(unsigned)i)&1u); if(r>=d){r-=d;q|=1u<<(unsigned)i;} }
 if(rem) *rem=r;
 return q;
}
uint32_t md68k_divu(Md2Gba68k*c,Md2GbaBus*b,uint32_t dividend,uint32_t divisor,uint32_t fault_pc){
 uint32_t q,r; divisor&=0xFFFFu; if(!divisor){md68k_divzero(c,b,fault_pc);return dividend;}
 c->sr&=(uint16_t)~(MD68K_N|MD68K_Z|MD68K_V|MD68K_C);
 /* dividend >= divisor*65536 iff the high dividend word >= divisor. */
 if((dividend>>16)>=divisor){c->sr|=MD68K_V;return dividend;}
 q=md_u32_divmod_q16(dividend,divisor,&r);
 if(!(q&0xFFFFu))c->sr|=MD68K_Z;if(q&0x8000u)c->sr|=MD68K_N;return ((r&0xFFFFu)<<16)|(q&0xFFFFu);
}
uint32_t md68k_divs(Md2Gba68k*c,Md2GbaBus*b,uint32_t dividend,uint32_t divisor,uint32_t fault_pc){
 int32_t dv=(int32_t)dividend,ds=md68k_sx(divisor&0xFFFFu,16); uint32_t ad,as,qm,rm,qbits,rbits,limit; int qneg,dneg;
 if(!ds){md68k_divzero(c,b,fault_pc);return dividend;}
 dneg=dv<0;qneg=dneg^(ds<0);ad=dneg?(0u-(uint32_t)dv):(uint32_t)dv;as=ds<0?(0u-(uint32_t)ds):(uint32_t)ds;
 c->sr&=(uint16_t)~(MD68K_N|MD68K_Z|MD68K_V|MD68K_C);
 limit=qneg?32768u:32767u;
 /* q > limit iff dividend magnitude >= divisor magnitude*(limit+1).
    The largest product is 32768*32769 and fits safely in uint32_t. */
 if(ad>=as*(limit+1u)){c->sr|=MD68K_V;return dividend;}
 qm=md_u32_divmod_q16(ad,as,&rm);
 qbits=qneg?(0u-qm):qm;rbits=dneg?(0u-rm):rm;if(!(qbits&0xFFFFu))c->sr|=MD68K_Z;if(qneg && qm)c->sr|=MD68K_N;return ((rbits&0xFFFFu)<<16)|(qbits&0xFFFFu);
}
static int32_t indexv(const Md2Gba68k *c,uint16_t e){ unsigned a=!!(e&0x8000),r=(e>>12)&7,l=!!(e&0x0800);uint32_t v=a?c->a[r]:c->d[r];return l?(int32_t)v:md68k_sx(v&0xffff,16); }
MD2GBA_CPU_HOT uint32_t md68k_ea_addr(Md2Gba68k *c,int k,int r,uint32_t value,int32_t disp,uint16_t ext,unsigned s,uint32_t pc,int pre){ uint32_t a;switch(k){case MD68K_EA_AN_IND:case MD68K_EA_AN_POSTINC:return c->a[r]&0xffffffu;case MD68K_EA_AN_PREDEC:if(pre)c->a[r]-=incsz(s,r);return c->a[r]&0xffffffu;case MD68K_EA_AN_DISP16:return (c->a[r]+disp)&0xffffffu;case MD68K_EA_AN_INDEX8:return (c->a[r]+disp+indexv(c,ext))&0xffffffu;case MD68K_EA_ABS_W:a=value&0xffffu;return ((a&0x8000u)?(a|0xff0000u):a)&0xffffffu;case MD68K_EA_ABS_L:return value&0xffffffu;case MD68K_EA_PC_DISP16:return (pc+2+disp)&0xffffffu;case MD68K_EA_PC_INDEX8:return (pc+2+disp+indexv(c,ext))&0xffffffu;default:return 0;} }
/* Architectural effective address for instructions such as LEA.  The 68000's
   external address bus is 24-bit, but address registers are 32-bit.  In
   particular, absolute-short effective addresses are sign-extended to 32 bits
   when loaded into An.  Keep bus masking in md68k_ea_addr(); never collapse
   an architectural An value to 24 bits here. */
uint32_t md68k_ea_arch_addr(Md2Gba68k *c,int k,int r,uint32_t value,int32_t disp,uint16_t ext,uint32_t pc){
 uint32_t a;
 switch(k){
 case MD68K_EA_AN_IND:case MD68K_EA_AN_POSTINC:case MD68K_EA_AN_PREDEC:return c->a[r];
 case MD68K_EA_AN_DISP16:return c->a[r]+(uint32_t)disp;
 case MD68K_EA_AN_INDEX8:return c->a[r]+(uint32_t)disp+(uint32_t)indexv(c,ext);
 case MD68K_EA_ABS_W:a=value&0xffffu;return (uint32_t)(int32_t)(int16_t)a;
 case MD68K_EA_ABS_L:return value;
 case MD68K_EA_PC_DISP16:return pc+2u+(uint32_t)disp;
 case MD68K_EA_PC_INDEX8:return pc+2u+(uint32_t)disp+(uint32_t)indexv(c,ext);
 default:return 0u;
 }
}
uint32_t md68k_rd(Md2Gba68k *c,Md2GbaBus *b,int k,int r,uint32_t value,int32_t disp,uint16_t ext,unsigned s,uint32_t pc){ uint32_t a,v,m=md68k_mask(s);if(k==MD68K_EA_DN)return c->d[r]&m;if(k==MD68K_EA_AN)return c->a[r]&m;if(k==MD68K_EA_IMM)return value&m;a=md68k_ea_addr(c,k,r,value,disp,ext,s,pc,1);v=s==1?md2gba_read8_fast(b,a):(s==2?md2gba_read16(b,a):md2gba_read32(b,a));if(k==MD68K_EA_AN_POSTINC)c->a[r]+=incsz(s,r);return v&m; }
void md68k_wr(Md2Gba68k *c,Md2GbaBus *b,int k,int r,uint32_t value,int32_t disp,uint16_t ext,unsigned s,uint32_t v,uint32_t pc){ uint32_t a,m=md68k_mask(s);v&=m;if(k==MD68K_EA_DN){uint32_t keep=s==1?0xffffff00u:(s==2?0xffff0000u:0);c->d[r]=(c->d[r]&keep)|v;return;}if(k==MD68K_EA_AN){c->a[r]=s==2?(uint32_t)md68k_sx(v,16):v;return;}a=md68k_ea_addr(c,k,r,value,disp,ext,s,pc,1);if(s==1)md2gba_write8_fast(b,a,(uint8_t)v);else if(s==2)md2gba_write16(b,a,(uint16_t)v);else md2gba_write32(b,a,v);if(k==MD68K_EA_AN_POSTINC)c->a[r]+=incsz(s,r); }
MD2GBA_CPU_HOT void md68k_push32(Md2Gba68k *c,Md2GbaBus *b,uint32_t v){c->a[7]-=4;md2gba_write32(b,c->a[7],v);}uint32_t md68k_pop32(Md2Gba68k *c,Md2GbaBus *b){uint32_t v=md2gba_read32(b,c->a[7]);c->a[7]+=4;return v;}MD2GBA_CPU_HOT void md68k_push16(Md2Gba68k *c,Md2GbaBus *b,uint16_t v){c->a[7]-=2;md2gba_write16(b,c->a[7],v);}uint16_t md68k_pop16(Md2Gba68k *c,Md2GbaBus *b){uint16_t v=md2gba_read16(b,c->a[7]);c->a[7]+=2;return v;}
void md68k_shift(Md2Gba68k *c,unsigned s,unsigned dr,unsigned kind,int left,int cir,unsigned cor){
 uint32_t m=md68k_mask(s),sg=md68k_sign(s),v=c->d[dr]&m;unsigned count=cir?(c->d[cor]&63u):cor;unsigned oldx=!!(c->sr&MD68K_X),carry=0,ov=0,i,w=s==1u?8u:(s==2u?16u:32u);
 count&=63u;
 if(count){
   /* B10/P128: logical shifts and rotates have exact closed forms.  Keep AS
      and ROX on the proven stepwise path because their overflow/X chains are
      more subtle; this removes the common per-bit loop without broadening risk. */
   if(kind==MD68K_SHIFT_LS){
     if(left){if(count<w){carry=(v>>(w-count))&1u;v=(v<<count)&m;}else if(count==w){carry=v&1u;v=0u;}else{carry=0u;v=0u;}}
     else {if(count<w){carry=(v>>(count-1u))&1u;v>>=count;}else if(count==w){carry=(v>>(w-1u))&1u;v=0u;}else{carry=0u;v=0u;}}
   }else if(kind==MD68K_SHIFT_RO){
     unsigned e=count%w;
     if(e){if(left)v=((v<<e)|(v>>(w-e)))&m;else v=((v>>e)|(v<<(w-e)))&m;}
     carry=left?(v&1u):!!(v&sg);
   }else{
     for(i=0;i<count;i++){
       if(kind==MD68K_SHIFT_AS){if(left){unsigned os=!!(v&sg);carry=os;v=(v<<1)&m;if(!!(v&sg)!=os)ov=1;}else{carry=v&1;v=((v>>1)|((v&sg)?sg:0))&m;}}
       else {if(left){carry=!!(v&sg);v=((v<<1)&m)|oldx;oldx=carry;}else{carry=v&1;v=(v>>1)|(oldx?sg:0);oldx=carry;}}
     }
   }
   c->sr&=(uint16_t)~(MD68K_N|MD68K_Z|MD68K_V|MD68K_C);if(kind==MD68K_SHIFT_AS||kind==MD68K_SHIFT_LS||kind==MD68K_SHIFT_ROX)c->sr&=(uint16_t)~MD68K_X;if(!v)c->sr|=MD68K_Z;if(v&sg)c->sr|=MD68K_N;if(ov)c->sr|=MD68K_V;if(carry)c->sr|=MD68K_C;if((kind==MD68K_SHIFT_AS||kind==MD68K_SHIFT_LS||kind==MD68K_SHIFT_ROX)&&carry)c->sr|=MD68K_X;
 }else{
   md68k_set_nz(c,s,v);
   /* B02 prerequisite: zero-count ROX copies X to C while leaving X intact. */
   if(kind==MD68K_SHIFT_ROX && oldx)c->sr|=MD68K_C;
 }
 {uint32_t keep=s==1?0xffffff00u:(s==2?0xffff0000u:0);c->d[dr]=(c->d[dr]&keep)|v;}
}
uint32_t md68k_shift_value(Md2Gba68k *c,unsigned s,uint32_t value,unsigned kind,int left,unsigned count){
 uint32_t m=md68k_mask(s),sg=md68k_sign(s),v=value&m;unsigned oldx=!!(c->sr&MD68K_X),carry=0,ov=0,i;
 if(count){for(i=0;i<count;i++){if(kind==MD68K_SHIFT_LS){if(left){carry=!!(v&sg);v=(v<<1)&m;}else{carry=v&1;v>>=1;}}else if(kind==MD68K_SHIFT_AS){if(left){unsigned os=!!(v&sg);carry=os;v=(v<<1)&m;if(!!(v&sg)!=os)ov=1;}else{carry=v&1;v=((v>>1)|((v&sg)?sg:0))&m;}}else if(kind==MD68K_SHIFT_RO){if(left){carry=!!(v&sg);v=((v<<1)&m)|carry;}else{carry=v&1;v=(v>>1)|(carry?sg:0);}}else{if(left){carry=!!(v&sg);v=((v<<1)&m)|oldx;oldx=carry;}else{carry=v&1;v=(v>>1)|(oldx?sg:0);oldx=carry;}}}
 c->sr&=(uint16_t)~(MD68K_N|MD68K_Z|MD68K_V|MD68K_C);if(kind==MD68K_SHIFT_AS||kind==MD68K_SHIFT_LS||kind==MD68K_SHIFT_ROX)c->sr&=(uint16_t)~MD68K_X;if(!v)c->sr|=MD68K_Z;if(v&sg)c->sr|=MD68K_N;if(ov)c->sr|=MD68K_V;if(carry)c->sr|=MD68K_C;if((kind==MD68K_SHIFT_AS||kind==MD68K_SHIFT_LS||kind==MD68K_SHIFT_ROX)&&carry)c->sr|=MD68K_X;}else {md68k_set_nz(c,s,v);if(kind==MD68K_SHIFT_ROX && oldx)c->sr|=MD68K_C;}return v&m;}
static uint32_t getreg(Md2Gba68k*c,int idx){return idx<8?c->d[idx]:c->a[idx-8];}static void setreg(Md2Gba68k*c,int idx,uint32_t v){if(idx<8)c->d[idx]=v;else c->a[idx-8]=v;}
void md68k_movem_store(Md2Gba68k*c,Md2GbaBus*b,uint16_t mask,unsigned s,int k,int r,uint32_t val,int32_t disp,uint16_t ext,uint32_t pc){unsigned step=s,bit;if(k==MD68K_EA_AN_PREDEC){uint32_t initial_ar=c->a[r];for(bit=0;bit<16;bit++)if(mask&(1u<<bit)){int idx=15-(int)bit;uint32_t rv=(idx==8+(int)r)?initial_ar:getreg(c,idx);c->a[r]-=step;if(s==2)md2gba_write16(b,c->a[r],(uint16_t)rv);else md2gba_write32(b,c->a[r],rv);}}else{uint32_t a=md68k_ea_addr(c,k,r,val,disp,ext,s,pc,0);for(bit=0;bit<16;bit++)if(mask&(1u<<bit)){if(s==2)md2gba_write16(b,a,(uint16_t)getreg(c,bit));else md2gba_write32(b,a,getreg(c,bit));a+=step;}if(k==MD68K_EA_AN_POSTINC)c->a[r]=a;}}
void md68k_movem_load(Md2Gba68k*c,Md2GbaBus*b,uint16_t mask,unsigned s,int k,int r,uint32_t val,int32_t disp,uint16_t ext,uint32_t pc){unsigned step=s,bit;uint32_t a=md68k_ea_addr(c,k,r,val,disp,ext,s,pc,0);for(bit=0;bit<16;bit++)if(mask&(1u<<bit)){uint32_t v=s==2?(uint32_t)md68k_sx(md2gba_read16(b,a),16):md2gba_read32(b,a);setreg(c,bit,v);a+=step;}if(k==MD68K_EA_AN_POSTINC)c->a[r]=a;}

/* P138 / PSC-style specialization for the dominant translated stack MOVEMs.
   The generated instruction already proves longword + A7 predecrement/postincrement,
   so do not redispatch EA mode/size/register state in the generic helper. */
MD2GBA_CPU_HOT void md68k_movem_store_a7_l(Md2Gba68k*c,Md2GbaBus*b,uint16_t mask){
    uint32_t a=c->a[7],initial=a,v;unsigned bit;
    for(bit=0u;bit<8u;bit++)if(mask&(1u<<bit)){a-=4u;v=bit==0u?initial:c->a[7u-bit];md2gba_write32(b,a,v);}
    for(bit=8u;bit<16u;bit++)if(mask&(1u<<bit)){a-=4u;md2gba_write32(b,a,c->d[15u-bit]);}
    c->a[7]=a;
}
MD2GBA_CPU_HOT void md68k_movem_load_a7_l(Md2Gba68k*c,Md2GbaBus*b,uint16_t mask){
    uint32_t a=c->a[7],v;unsigned bit;
    for(bit=0u;bit<8u;bit++)if(mask&(1u<<bit)){v=md2gba_read32(b,a);c->d[bit]=v;a+=4u;}
    for(bit=8u;bit<16u;bit++)if(mask&(1u<<bit)){v=md2gba_read32(b,a);if(bit!=15u)c->a[bit-8u]=v;a+=4u;}
    c->a[7]=a;
}
