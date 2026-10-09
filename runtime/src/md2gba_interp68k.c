#include "md2gba_interp68k.h"
#include "md2gba_interp_opclass.h"

typedef struct IEa {
    int kind, reg;
    uint32_t value;
    int32_t disp;
    uint16_t ext;
    uint8_t ext_bytes;
} IEa;

static uint16_t iw(Md2GbaBus*b,uint32_t p){return md2gba_read16(b,p&0xFFFFFFu);}
static uint32_t il(Md2GbaBus*b,uint32_t p){return md2gba_read32(b,p&0xFFFFFFu);}
static int32_t is8(uint32_t x){return md68k_sx(x&0xFFu,8);}
static int32_t is16(uint32_t x){return md68k_sx(x&0xFFFFu,16);}
static unsigned szcode(uint16_t op){unsigned c=(op>>6)&3u;return c==0?MD68K_SZ_B:(c==1?MD68K_SZ_W:(c==2?MD68K_SZ_L:0));}
static unsigned movesz(uint16_t op){unsigned n=(op>>12)&15u;return n==1?MD68K_SZ_B:(n==2?MD68K_SZ_L:(n==3?MD68K_SZ_W:0));}

static int dea(Md2GbaBus*b,uint32_t xp,unsigned mode,unsigned reg,unsigned sz,IEa*e){
    e->kind=-1;e->reg=(int)reg;e->value=0;e->disp=0;e->ext=0;e->ext_bytes=0;
    switch(mode){
    case 0:e->kind=MD68K_EA_DN;return 1;
    case 1:e->kind=MD68K_EA_AN;return 1;
    case 2:e->kind=MD68K_EA_AN_IND;return 1;
    case 3:e->kind=MD68K_EA_AN_POSTINC;return 1;
    case 4:e->kind=MD68K_EA_AN_PREDEC;return 1;
    case 5:e->kind=MD68K_EA_AN_DISP16;e->ext_bytes=2;e->disp=is16(iw(b,xp));return 1;
    case 6:e->kind=MD68K_EA_AN_INDEX8;e->ext_bytes=2;e->ext=iw(b,xp);e->disp=is8(e->ext);return 1;
    case 7:
        switch(reg){
        case 0:e->kind=MD68K_EA_ABS_W;e->ext_bytes=2;e->value=iw(b,xp);return 1;
        case 1:e->kind=MD68K_EA_ABS_L;e->ext_bytes=4;e->value=il(b,xp);return 1;
        case 2:e->kind=MD68K_EA_PC_DISP16;e->ext_bytes=2;e->disp=is16(iw(b,xp));return 1;
        case 3:e->kind=MD68K_EA_PC_INDEX8;e->ext_bytes=2;e->ext=iw(b,xp);e->disp=is8(e->ext);return 1;
        case 4:e->kind=MD68K_EA_IMM;e->ext_bytes=(uint8_t)(sz==MD68K_SZ_L?4:2);e->value=sz==MD68K_SZ_L?il(b,xp):iw(b,xp);if(sz==MD68K_SZ_B)e->value&=0xFFu;return 1;
        default:return 0;
        }
    default:return 0;
    }
}
#define EARGS(e) (e).kind,(e).reg,(e).value,(e).disp,(e).ext
static uint32_t erd(Md2Gba68k*c,Md2GbaBus*b,IEa e,unsigned sz,uint32_t pc){return md68k_rd(c,b,EARGS(e),sz,pc);}
static void ewr(Md2Gba68k*c,Md2GbaBus*b,IEa e,unsigned sz,uint32_t v,uint32_t pc){md68k_wr(c,b,EARGS(e),sz,v,pc);}
static uint32_t eaddr(Md2Gba68k*c,IEa e,unsigned sz,uint32_t pc){return md68k_ea_addr(c,EARGS(e),sz,pc,0);}
static uint32_t eaddr_arch(Md2Gba68k*c,IEa e,uint32_t pc){return md68k_ea_arch_addr(c,EARGS(e),pc);}

static void bitop(Md2Gba68k*c,Md2GbaBus*b,IEa e,uint32_t bit,int action,uint32_t pc){
    unsigned sz=e.kind==MD68K_EA_DN?MD68K_SZ_L:MD68K_SZ_B,mod=sz==MD68K_SZ_L?32u:8u;
    uint32_t v=erd(c,b,e,sz,pc);bit%=mod;c->sr=(uint16_t)((c->sr&~MD68K_Z)|(((v>>bit)&1u)?0u:MD68K_Z));
    if(action==1)v^=1u<<bit;else if(action==2)v&=~(1u<<bit);else if(action==3)v|=1u<<bit;
    if(action)ewr(c,b,e,sz,v,pc);
}

int md68k_interp_step(Md2Gba68k*c,Md2GbaBus*b){
    uint32_t pc=c->pc&0xFFFFFFu,xp,nxt,t0,t1;uint16_t op;uint8_t cl;IEa e,s,d;unsigned sz,mode,reg,dn,opm,q,cond;int32_t disp;
    /* A 68000 cannot fetch an instruction word from an odd address.  Treat
       this as an exact-core fault instead of decoding adjacent bytes as code. */
    if(pc&1u)return MD68K_INTERP_FAULT;
    xp=pc+2u;nxt=pc+2u;op=iw(b,pc);cl=md68k_interp_opclass[op];
    if(!cl)return MD68K_INTERP_UNSUPPORTED;
    c->pc=nxt;
    switch(cl){
    case MD68KI_NOP:return MD68K_INTERP_OK;
    case MD68KI_RTS:c->pc=md68k_pop32(c,b);return MD68K_INTERP_OK;
    case MD68KI_RTE:c->sr=md68k_pop16(c,b);c->pc=md68k_pop32(c,b);return MD68K_INTERP_OK;
    case MD68KI_MOVE_TO_SR_IMM:c->sr=iw(b,xp);c->pc=pc+4;return MD68K_INTERP_OK;
    case MD68KI_ORI_TO_SR:case MD68KI_ANDI_TO_SR:case MD68KI_EORI_TO_SR:
        t0=iw(b,xp);if(cl==MD68KI_ORI_TO_SR)c->sr|=(uint16_t)t0;else if(cl==MD68KI_ANDI_TO_SR)c->sr&=(uint16_t)t0;else c->sr^=(uint16_t)t0;c->pc=pc+4;return MD68K_INTERP_OK;
    case MD68KI_ORI_TO_CCR:case MD68KI_ANDI_TO_CCR:case MD68KI_EORI_TO_CCR:
        t0=iw(b,xp)&0xFFu;t1=c->sr&0xFFu;if(cl==MD68KI_ORI_TO_CCR)t1|=t0;else if(cl==MD68KI_ANDI_TO_CCR)t1&=t0;else t1^=t0;c->sr=(uint16_t)((c->sr&0xFF00u)|(t1&0xFFu));c->pc=pc+4;return MD68K_INTERP_OK;
    case MD68KI_MOVE_AN_TO_USP:c->usp=c->a[op&7u];return MD68K_INTERP_OK;
    case MD68KI_MOVE_USP_TO_AN:c->a[op&7u]=c->usp;return MD68K_INTERP_OK;
    case MD68KI_MOVE_FROM_SR:case MD68KI_MOVE_TO_SR:case MD68KI_MOVE_TO_CCR:
        mode=(op>>3)&7u;reg=op&7u;if(!dea(b,xp,mode,reg,MD68K_SZ_W,&e))return MD68K_INTERP_FAULT;c->pc=pc+2u+e.ext_bytes;
        if(cl==MD68KI_MOVE_FROM_SR)ewr(c,b,e,MD68K_SZ_W,c->sr,pc);else{t0=erd(c,b,e,MD68K_SZ_W,pc);if(cl==MD68KI_MOVE_TO_SR)c->sr=(uint16_t)t0;else c->sr=(uint16_t)((c->sr&0xFF00u)|(t0&0xFFu));}return MD68K_INTERP_OK;
    case MD68KI_BRA:case MD68KI_BSR:case MD68KI_BCC:
        cond=(op>>8)&15u;t0=op&0xFFu;if(t0==0){disp=is16(iw(b,xp));nxt=pc+4;}else{disp=t0==0xFFu?-1:is8(t0);nxt=pc+2;}c->pc=nxt;
        t1=(pc+2u+disp)&0xFFFFFFu;if(cl==MD68KI_BRA)c->pc=t1;else if(cl==MD68KI_BSR){md68k_push32(c,b,nxt);c->pc=t1;}else if(md68k_cond(c,cond))c->pc=t1;return MD68K_INTERP_OK;
    case MD68KI_DBCC:
        cond=(op>>8)&15u;disp=is16(iw(b,xp));c->pc=pc+4;if(!md68k_cond(c,cond)){dn=op&7u;t0=(c->d[dn]-1u)&0xFFFFu;c->d[dn]=(c->d[dn]&0xFFFF0000u)|t0;if(t0!=0xFFFFu)c->pc=(pc+2u+disp)&0xFFFFFFu;}return MD68K_INTERP_OK;
    case MD68KI_MOVEQ:
        dn=(op>>9)&7u;c->d[dn]=(uint32_t)is8(op);md68k_set_nz(c,MD68K_SZ_L,c->d[dn]);return MD68K_INTERP_OK;
    case MD68KI_JSR:case MD68KI_JMP:
        mode=(op>>3)&7u;reg=op&7u;if(!dea(b,xp,mode,reg,MD68K_SZ_L,&e))return MD68K_INTERP_FAULT;nxt=pc+2u+e.ext_bytes;t0=eaddr(c,e,MD68K_SZ_L,pc);c->pc=t0;if(cl==MD68KI_JSR)md68k_push32(c,b,nxt);return MD68K_INTERP_OK;
    case MD68KI_LEA:
        dn=(op>>9)&7u;mode=(op>>3)&7u;reg=op&7u;if(!dea(b,xp,mode,reg,MD68K_SZ_L,&e))return MD68K_INTERP_FAULT;c->a[dn]=eaddr_arch(c,e,pc);c->pc=pc+2u+e.ext_bytes;return MD68K_INTERP_OK;
    case MD68KI_EXT:
        dn=op&7u;if((op&0xFFC0u)==0x4880u){t0=(uint32_t)md68k_sx(c->d[dn]&0xFFu,8)&0xFFFFu;c->d[dn]=(c->d[dn]&0xFFFF0000u)|t0;md68k_set_nz(c,MD68K_SZ_W,t0);}else{c->d[dn]=(uint32_t)md68k_sx(c->d[dn]&0xFFFFu,16);md68k_set_nz(c,MD68K_SZ_L,c->d[dn]);}return MD68K_INTERP_OK;
    case MD68KI_MOVEM_TO_REGS:case MD68KI_MOVEM_FROM_REGS:
        sz=(op&0x40u)?MD68K_SZ_L:MD68K_SZ_W;t0=iw(b,xp);mode=(op>>3)&7u;reg=op&7u;if(!dea(b,pc+4u,mode,reg,sz,&e))return MD68K_INTERP_FAULT;c->pc=pc+4u+e.ext_bytes;
        if(cl==MD68KI_MOVEM_TO_REGS)md68k_movem_load(c,b,(uint16_t)t0,sz,EARGS(e),pc);else md68k_movem_store(c,b,(uint16_t)t0,sz,EARGS(e),pc);return MD68K_INTERP_OK;
    case MD68KI_ORI:case MD68KI_ANDI:case MD68KI_EORI:case MD68KI_ADDI:case MD68KI_SUBI:case MD68KI_CMPI:
        sz=szcode(op);if(!sz)return MD68K_INTERP_FAULT;t0=sz==MD68K_SZ_L?il(b,xp):iw(b,xp);if(sz==MD68K_SZ_B)t0&=0xFFu;xp+=sz==MD68K_SZ_L?4u:2u;mode=(op>>3)&7u;reg=op&7u;if(!dea(b,xp,mode,reg,sz,&e))return MD68K_INTERP_FAULT;c->pc=xp+e.ext_bytes;t1=erd(c,b,e,sz,pc);
        if(cl==MD68KI_ORI){t1|=t0;ewr(c,b,e,sz,t1,pc);md68k_set_nz(c,sz,t1);}else if(cl==MD68KI_ANDI){t1&=t0;ewr(c,b,e,sz,t1,pc);md68k_set_nz(c,sz,t1);}else if(cl==MD68KI_EORI){t1^=t0;ewr(c,b,e,sz,t1,pc);md68k_set_nz(c,sz,t1);}else if(cl==MD68KI_ADDI){t1=md68k_add(c,sz,t1,t0,1);ewr(c,b,e,sz,t1,pc);}else if(cl==MD68KI_SUBI){t1=md68k_sub(c,sz,t1,t0,1);ewr(c,b,e,sz,t1,pc);}else(void)md68k_sub(c,sz,t1,t0,0);return MD68K_INTERP_OK;
    case MD68KI_BTST_IMM:case MD68KI_BCHG_IMM:case MD68KI_BCLR_IMM:case MD68KI_BSET_IMM:
        t0=iw(b,xp)&0xFFu;mode=(op>>3)&7u;reg=op&7u;if(!dea(b,pc+4u,mode,reg,MD68K_SZ_B,&e))return MD68K_INTERP_FAULT;c->pc=pc+4u+e.ext_bytes;bitop(c,b,e,t0,cl==MD68KI_BTST_IMM?0:(cl==MD68KI_BCHG_IMM?1:(cl==MD68KI_BCLR_IMM?2:3)),pc);return MD68K_INTERP_OK;
    case MD68KI_BTST_DN:case MD68KI_BCHG_DN:case MD68KI_BCLR_DN:case MD68KI_BSET_DN:
        mode=(op>>3)&7u;reg=op&7u;if(!dea(b,xp,mode,reg,MD68K_SZ_B,&e))return MD68K_INTERP_FAULT;c->pc=pc+2u+e.ext_bytes;bitop(c,b,e,c->d[(op>>9)&7u],cl==MD68KI_BTST_DN?0:(cl==MD68KI_BCHG_DN?1:(cl==MD68KI_BCLR_DN?2:3)),pc);return MD68K_INTERP_OK;
    case MD68KI_MOVEP:{
        unsigned dr=(op>>9)&7u,ar=op&7u;opm=(op>>6)&3u;disp=is16(iw(b,xp));t0=(c->a[ar]+disp)&0xFFFFFFu;c->pc=pc+4;
        if(opm<2u){if(opm==0)c->d[dr]=(c->d[dr]&0xFFFF0000u)|((uint32_t)md2gba_read8(b,t0)<<8)|md2gba_read8(b,(t0+2)&0xFFFFFFu);else c->d[dr]=((uint32_t)md2gba_read8(b,t0)<<24)|((uint32_t)md2gba_read8(b,(t0+2)&0xFFFFFFu)<<16)|((uint32_t)md2gba_read8(b,(t0+4)&0xFFFFFFu)<<8)|md2gba_read8(b,(t0+6)&0xFFFFFFu);}else if(opm==2){md2gba_write8(b,t0,(uint8_t)(c->d[dr]>>8));md2gba_write8(b,(t0+2)&0xFFFFFFu,(uint8_t)c->d[dr]);}else{md2gba_write8(b,t0,(uint8_t)(c->d[dr]>>24));md2gba_write8(b,(t0+2)&0xFFFFFFu,(uint8_t)(c->d[dr]>>16));md2gba_write8(b,(t0+4)&0xFFFFFFu,(uint8_t)(c->d[dr]>>8));md2gba_write8(b,(t0+6)&0xFFFFFFu,(uint8_t)c->d[dr]);}return MD68K_INTERP_OK;}
    case MD68KI_TRAP:
        md68k_push32(c,b,pc+2u);md68k_push16(c,b,c->sr);c->sr|=0x2000u;c->pc=md2gba_read32(b,(32u+(op&15u))*4u)&0xFFFFFFu;return MD68K_INTERP_OK;
    case MD68KI_ADDQ:case MD68KI_SUBQ:
        sz=szcode(op);mode=(op>>3)&7u;reg=op&7u;q=(op>>9)&7u;if(!q)q=8;if(!dea(b,xp,mode,reg,sz,&e))return MD68K_INTERP_FAULT;c->pc=pc+2u+e.ext_bytes;if(e.kind==MD68K_EA_AN)c->a[e.reg]+=cl==MD68KI_ADDQ?q:(uint32_t)(0u-q);else{t0=erd(c,b,e,sz,pc);t1=cl==MD68KI_ADDQ?md68k_add(c,sz,t0,q,1):md68k_sub(c,sz,t0,q,1);ewr(c,b,e,sz,t1,pc);}return MD68K_INTERP_OK;
    case MD68KI_SCC:
        mode=(op>>3)&7u;reg=op&7u;if(!dea(b,xp,mode,reg,MD68K_SZ_B,&e))return MD68K_INTERP_FAULT;c->pc=pc+2u+e.ext_bytes;ewr(c,b,e,MD68K_SZ_B,md68k_cond(c,(op>>8)&15u)?0xFFu:0u,pc);return MD68K_INTERP_OK;
    case MD68KI_SWAP:
        dn=op&7u;t0=c->d[dn];c->d[dn]=(t0<<16)|(t0>>16);md68k_set_nz(c,MD68K_SZ_L,c->d[dn]);return MD68K_INTERP_OK;
    case MD68KI_SHIFT_REG:
        sz=szcode(op);dn=op&7u;md68k_shift(c,sz,dn,(op>>3)&3u,(op&0x0100u)!=0,(op&0x20u)!=0,(op&0x20u)?((op>>9)&7u):(((op>>9)&7u)?((op>>9)&7u):8u));return MD68K_INTERP_OK;
    case MD68KI_ASR_MEM:case MD68KI_ASL_MEM:case MD68KI_LSR_MEM:case MD68KI_LSL_MEM:case MD68KI_ROXR_MEM:case MD68KI_ROXL_MEM:case MD68KI_ROR_MEM:case MD68KI_ROL_MEM:{
        unsigned kind,left;mode=(op>>3)&7u;reg=op&7u;if(!dea(b,xp,mode,reg,MD68K_SZ_W,&e))return MD68K_INTERP_FAULT;c->pc=pc+2u+e.ext_bytes;t0=erd(c,b,e,MD68K_SZ_W,pc);
        if(cl==MD68KI_ASR_MEM||cl==MD68KI_ASL_MEM){kind=MD68K_SHIFT_AS;left=cl==MD68KI_ASL_MEM;}else if(cl==MD68KI_LSR_MEM||cl==MD68KI_LSL_MEM){kind=MD68K_SHIFT_LS;left=cl==MD68KI_LSL_MEM;}else if(cl==MD68KI_ROXR_MEM||cl==MD68KI_ROXL_MEM){kind=MD68K_SHIFT_ROX;left=cl==MD68KI_ROXL_MEM;}else{kind=MD68K_SHIFT_RO;left=cl==MD68KI_ROL_MEM;}t1=md68k_shift_value(c,MD68K_SZ_W,t0,kind,left,1);ewr(c,b,e,MD68K_SZ_W,t1,pc);return MD68K_INTERP_OK;}
    case MD68KI_EXG:{
        unsigned x=(op>>9)&7u,y=op&7u;uint16_t pat=op&0xF1F8u;t0=pat==0xC148u?c->a[x]:c->d[x];if(pat==0xC140u){c->d[x]=c->d[y];c->d[y]=t0;}else if(pat==0xC148u){c->a[x]=c->a[y];c->a[y]=t0;}else{c->d[x]=c->a[y];c->a[y]=t0;}return MD68K_INTERP_OK;}
    case MD68KI_OR_TO_DN:case MD68KI_AND_TO_DN:case MD68KI_OR_DN_TO_EA:case MD68KI_AND_DN_TO_EA:case MD68KI_DIVU:case MD68KI_DIVS:case MD68KI_MULU:case MD68KI_MULS:
        opm=(op>>6)&7u;dn=(op>>9)&7u;sz=(opm==0u||opm==4u)?MD68K_SZ_B:((opm==1u||opm==5u)?MD68K_SZ_W:(opm==2u||opm==6u?MD68K_SZ_L:MD68K_SZ_W));mode=(op>>3)&7u;reg=op&7u;if(!dea(b,xp,mode,reg,sz,&e))return MD68K_INTERP_FAULT;c->pc=pc+2u+e.ext_bytes;t0=erd(c,b,e,sz,pc);
        if(cl==MD68KI_OR_TO_DN||cl==MD68KI_AND_TO_DN){t1=(c->d[dn]&md68k_mask(sz));t1=cl==MD68KI_OR_TO_DN?(t1|t0):(t1&t0);ewr(c,b,(IEa){MD68K_EA_DN,(int)dn,0,0,0,0},sz,t1,pc);md68k_set_nz(c,sz,t1);}else if(cl==MD68KI_OR_DN_TO_EA||cl==MD68KI_AND_DN_TO_EA){t1=cl==MD68KI_OR_DN_TO_EA?(t0|(c->d[dn]&md68k_mask(sz))):(t0&(c->d[dn]&md68k_mask(sz)));ewr(c,b,e,sz,t1,pc);md68k_set_nz(c,sz,t1);}else if(cl==MD68KI_MULU||cl==MD68KI_MULS){uint32_t a=c->d[dn]&0xFFFFu,bb=t0&0xFFFFu;t1=cl==MD68KI_MULU?a*bb:(uint32_t)(md68k_sx(a,16)*md68k_sx(bb,16));c->d[dn]=t1;md68k_set_nz(c,MD68K_SZ_L,t1);}else c->d[dn]=cl==MD68KI_DIVU?md68k_divu(c,b,c->d[dn],t0,pc):md68k_divs(c,b,c->d[dn],t0,pc);return MD68K_INTERP_OK;
    case MD68KI_NOT:case MD68KI_NEG:case MD68KI_TST:case MD68KI_CLR:
        sz=szcode(op);mode=(op>>3)&7u;reg=op&7u;if(!dea(b,xp,mode,reg,sz,&e))return MD68K_INTERP_FAULT;c->pc=pc+2u+e.ext_bytes;t0=cl==MD68KI_CLR?0:erd(c,b,e,sz,pc);
        if(cl==MD68KI_NOT){t1=(~t0)&md68k_mask(sz);ewr(c,b,e,sz,t1,pc);md68k_set_nz(c,sz,t1);}else if(cl==MD68KI_NEG){t1=md68k_sub(c,sz,0,t0,1);ewr(c,b,e,sz,t1,pc);}else if(cl==MD68KI_TST)md68k_set_nz(c,sz,t0);else{ewr(c,b,e,sz,0,pc);c->sr=(uint16_t)((c->sr&~(MD68K_N|MD68K_V|MD68K_C))|MD68K_Z);}return MD68K_INTERP_OK;
    case MD68KI_MOVE:case MD68KI_MOVEA:
        sz=movesz(op);mode=(op>>3)&7u;reg=op&7u;if(!dea(b,xp,mode,reg,sz,&s))return MD68K_INTERP_FAULT;xp+=s.ext_bytes;t0=erd(c,b,s,sz,pc);if(cl==MD68KI_MOVEA){dn=(op>>9)&7u;c->a[dn]=sz==MD68K_SZ_W?(uint32_t)md68k_sx(t0,16):t0;c->pc=xp;return MD68K_INTERP_OK;}mode=(op>>6)&7u;reg=(op>>9)&7u;if(!dea(b,xp,mode,reg,sz,&d))return MD68K_INTERP_FAULT;c->pc=xp+d.ext_bytes;ewr(c,b,d,sz,t0,pc);md68k_set_nz(c,sz,t0);return MD68K_INTERP_OK;
    case MD68KI_ADD_TO_DN:case MD68KI_ADDA:case MD68KI_ADD_DN_TO_EA:case MD68KI_SUB_FROM_DN:case MD68KI_SUBA:case MD68KI_SUB_DN_TO_EA:case MD68KI_CMP:case MD68KI_CMPA:case MD68KI_CMPM:case MD68KI_EOR_DN_TO_EA:
        opm=(op>>6)&7u;dn=(op>>9)&7u;
        if(cl==MD68KI_CMPM){sz=opm==4?MD68K_SZ_B:(opm==5?MD68K_SZ_W:MD68K_SZ_L);s=(IEa){MD68K_EA_AN_POSTINC,(int)(op&7u),0,0,0,0};d=(IEa){MD68K_EA_AN_POSTINC,(int)dn,0,0,0,0};t0=erd(c,b,s,sz,pc);t1=erd(c,b,d,sz,pc);(void)md68k_sub(c,sz,t1,t0,0);return MD68K_INTERP_OK;}
        sz=(opm==0||opm==4)?MD68K_SZ_B:((opm==1||opm==3||opm==5)?MD68K_SZ_W:MD68K_SZ_L);mode=(op>>3)&7u;reg=op&7u;if(!dea(b,xp,mode,reg,sz,&e))return MD68K_INTERP_FAULT;c->pc=pc+2u+e.ext_bytes;t0=erd(c,b,e,sz,pc);
        if(cl==MD68KI_ADDA||cl==MD68KI_SUBA||cl==MD68KI_CMPA){t1=sz==MD68K_SZ_W?(uint32_t)md68k_sx(t0,16):t0;if(cl==MD68KI_ADDA)c->a[dn]+=t1;else if(cl==MD68KI_SUBA)c->a[dn]-=t1;else(void)md68k_sub(c,MD68K_SZ_L,c->a[dn],t1,0);return MD68K_INTERP_OK;}
        if(cl==MD68KI_ADD_TO_DN||cl==MD68KI_SUB_FROM_DN||cl==MD68KI_CMP){t1=c->d[dn]&md68k_mask(sz);if(cl==MD68KI_CMP){(void)md68k_sub(c,sz,t1,t0,0);return MD68K_INTERP_OK;}t1=cl==MD68KI_ADD_TO_DN?md68k_add(c,sz,t1,t0,1):md68k_sub(c,sz,t1,t0,1);ewr(c,b,(IEa){MD68K_EA_DN,(int)dn,0,0,0,0},sz,t1,pc);return MD68K_INTERP_OK;}
        if(cl==MD68KI_EOR_DN_TO_EA){t1=t0^(c->d[dn]&md68k_mask(sz));ewr(c,b,e,sz,t1,pc);md68k_set_nz(c,sz,t1);return MD68K_INTERP_OK;}t1=cl==MD68KI_ADD_DN_TO_EA?md68k_add(c,sz,t0,c->d[dn],1):md68k_sub(c,sz,t0,c->d[dn],1);ewr(c,b,e,sz,t1,pc);return MD68K_INTERP_OK;
    default:return MD68K_INTERP_UNSUPPORTED;
    }
}

int md68k_interp_fallback_hook(Md2Gba68k*c,Md2GbaBus*b,uint32_t pc,void*user){
    Md2GbaInterpStats*s=(Md2GbaInterpStats*)user;int r;c->pc=pc&0xFFFFFFu;if(s){s->last_pc=c->pc;s->last_opcode=md2gba_read16(b,c->pc);}r=md68k_interp_step(c,b);if(s){if(r>0)s->steps++;else s->unsupported++;}return r;
}
