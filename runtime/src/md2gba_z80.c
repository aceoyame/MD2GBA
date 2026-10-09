#include "md2gba_z80.h"
#include "md2gba_audio.h"
#include <stddef.h>

#define F_S 0x80u
#define F_Z 0x40u
#define F_Y 0x20u
#define F_H 0x10u
#define F_X 0x08u
#define F_PV 0x04u
#define F_N 0x02u
#define F_C 0x01u

#if defined(MD2GBA_Z80_PROFILE)
uint32_t md2gba_z80_profile[0x2000];
#endif

#if defined(MD2GBA_GBA_IWRAM)
#define Z80_HOT __attribute__((section(".iwram.z80"),noinline))
#else
#define Z80_HOT
#endif

static void zzero(void*p,unsigned n){uint8_t*q=(uint8_t*)p;while(n--)*q++=0;}
static uint16_t rp_bc(const Md2GbaZ80*z){return (uint16_t)(((uint16_t)z->b<<8)|z->c);} static uint16_t rp_de(const Md2GbaZ80*z){return (uint16_t)(((uint16_t)z->d<<8)|z->e);} static uint16_t rp_hl(const Md2GbaZ80*z){return (uint16_t)(((uint16_t)z->h<<8)|z->l);} static uint16_t rp_af(const Md2GbaZ80*z){return (uint16_t)(((uint16_t)z->a<<8)|z->f);}
static void set_bc(Md2GbaZ80*z,uint16_t v){z->b=(uint8_t)(v>>8);z->c=(uint8_t)v;} static void set_de(Md2GbaZ80*z,uint16_t v){z->d=(uint8_t)(v>>8);z->e=(uint8_t)v;} static void set_hl(Md2GbaZ80*z,uint16_t v){z->h=(uint8_t)(v>>8);z->l=(uint8_t)v;} static void set_af(Md2GbaZ80*z,uint16_t v){z->a=(uint8_t)(v>>8);z->f=(uint8_t)(v&0xD7u);}
static unsigned parity8(uint8_t v){v^=v>>4;v^=v>>2;v^=v>>1;return (~v)&1u;}
static uint8_t szp(uint8_t v){return (uint8_t)((v&0xA8u)|(v?0:F_Z)|(parity8(v)?F_PV:0));}

static uint8_t zread(Md2GbaZ80*z,Md2GbaBus*b,uint16_t a){
    if(a<0x4000u)return b->z80_ram[a&0x1fffu];
    if(a>=0x4000u&&a<=0x4003u)return (a&1u)?0u:md2gba_audio_ym_status(&b->audio);
    if(a>=0x8000u){uint32_t ga=((uint32_t)(z->bank&0x1ffu)<<15)|(uint32_t)(a&0x7fffu);return md2gba_read8(b,ga);}
    return 0xffu;
}
static void zwrite(Md2GbaZ80*z,Md2GbaBus*b,uint16_t a,uint8_t v){
    if(a<0x4000u){uint16_t o=(uint16_t)(a&0x1fffu);b->z80_ram[o]=v;return;}
    if(a>=0x4000u&&a<=0x4003u){unsigned p=(unsigned)(a-0x4000u);unsigned bank=(p>>1)&1u;if(!(p&1u))b->ym2612_addr[bank]=v;else b->ym2612_data[bank]=v;b->ym2612_writes++;md2gba_audio_write_ym_port(&b->audio,p,v);z->ym_writes++;return;}
    if(a>=0x6000u&&a<0x6100u){z->bank=(uint16_t)(((z->bank>>1)|((uint16_t)(v&1u)<<8))&0x1ffu);z->bank_writes++;return;}
    if(a==0x7f11u){md2gba_audio_write_psg(&b->audio,v);z->psg_writes++;return;}
    if(a>=0x8000u){uint32_t ga=((uint32_t)(z->bank&0x1ffu)<<15)|(uint32_t)(a&0x7fffu);md2gba_write8(b,ga,v);return;}
}
static uint8_t fetch8(Md2GbaZ80*z,Md2GbaBus*b){uint8_t v=zread(z,b,z->pc);z->pc=(uint16_t)(z->pc+1u);z->r=(uint8_t)((z->r&0x80u)|((z->r+1u)&0x7fu));return v;}
static uint16_t fetch16(Md2GbaZ80*z,Md2GbaBus*b){uint8_t lo=fetch8(z,b),hi=fetch8(z,b);return (uint16_t)(lo|((uint16_t)hi<<8));}
static uint16_t read16(Md2GbaZ80*z,Md2GbaBus*b,uint16_t a){uint8_t lo=zread(z,b,a),hi=zread(z,b,(uint16_t)(a+1u));return (uint16_t)(lo|((uint16_t)hi<<8));}
static void write16(Md2GbaZ80*z,Md2GbaBus*b,uint16_t a,uint16_t v){zwrite(z,b,a,(uint8_t)v);zwrite(z,b,(uint16_t)(a+1u),(uint8_t)(v>>8));}
static void push16(Md2GbaZ80*z,Md2GbaBus*b,uint16_t v){z->sp=(uint16_t)(z->sp-1u);zwrite(z,b,z->sp,(uint8_t)(v>>8));z->sp=(uint16_t)(z->sp-1u);zwrite(z,b,z->sp,(uint8_t)v);}
static uint16_t pop16(Md2GbaZ80*z,Md2GbaBus*b){uint8_t lo=zread(z,b,z->sp);z->sp=(uint16_t)(z->sp+1u);uint8_t hi=zread(z,b,z->sp);z->sp=(uint16_t)(z->sp+1u);return (uint16_t)(lo|((uint16_t)hi<<8));}

static int cond(const Md2GbaZ80*z,unsigned y){switch(y&7u){case 0:return !(z->f&F_Z);case 1:return !!(z->f&F_Z);case 2:return !(z->f&F_C);case 3:return !!(z->f&F_C);case 4:return !(z->f&F_PV);case 5:return !!(z->f&F_PV);case 6:return !(z->f&F_S);default:return !!(z->f&F_S);}}
static uint8_t inc8(Md2GbaZ80*z,uint8_t v){uint8_t r=(uint8_t)(v+1u);z->f=(uint8_t)((z->f&F_C)|(r&0xA8u)|(r?0:F_Z)|(((v&0x0fu)==0x0fu)?F_H:0)|(v==0x7fu?F_PV:0));return r;}
static uint8_t dec8(Md2GbaZ80*z,uint8_t v){uint8_t r=(uint8_t)(v-1u);z->f=(uint8_t)((z->f&F_C)|F_N|(r&0xA8u)|(r?0:F_Z)|((v&0x0fu)?0:F_H)|(v==0x80u?F_PV:0));return r;}
static uint8_t add8(Md2GbaZ80*z,uint8_t a,uint8_t b,unsigned carry){unsigned q=(unsigned)a+(unsigned)b+carry;uint8_t r=(uint8_t)q;z->f=(uint8_t)((r&0xA8u)|(r?0:F_Z)|(((a^b^r)&0x10u)?F_H:0)|((~(a^b)&(a^r)&0x80u)?F_PV:0)|(q>255u?F_C:0));return r;}
static uint8_t sub8(Md2GbaZ80*z,uint8_t a,uint8_t b,unsigned carry){unsigned q=(unsigned)b+carry;uint8_t r=(uint8_t)(a-q);z->f=(uint8_t)(F_N|(r&0xA8u)|(r?0:F_Z)|(((a^b^r)&0x10u)?F_H:0)|(((a^b)&(a^r)&0x80u)?F_PV:0)|((unsigned)a<q?F_C:0));return r;}
static void cp8(Md2GbaZ80*z,uint8_t b){uint8_t a=z->a;unsigned q=b;uint8_t r=(uint8_t)(a-q);z->f=(uint8_t)(F_N|(b&0x28u)|(r&F_S)|(r?0:F_Z)|(((a^b^r)&0x10u)?F_H:0)|(((a^b)&(a^r)&0x80u)?F_PV:0)|(a<q?F_C:0));}
static void alu(Md2GbaZ80*z,unsigned op,uint8_t v){switch(op&7u){case 0:z->a=add8(z,z->a,v,0);break;case 1:z->a=add8(z,z->a,v,(z->f&F_C)?1u:0u);break;case 2:z->a=sub8(z,z->a,v,0);break;case 3:z->a=sub8(z,z->a,v,(z->f&F_C)?1u:0u);break;case 4:z->a&=v;z->f=(uint8_t)(szp(z->a)|F_H);break;case 5:z->a^=v;z->f=szp(z->a);break;case 6:z->a|=v;z->f=szp(z->a);break;default:cp8(z,v);break;}}
static uint16_t get_rp(Md2GbaZ80*z,unsigned p,unsigned idx){switch(p&3u){case 0:return rp_bc(z);case 1:return rp_de(z);case 2:return idx==1?z->ix:idx==2?z->iy:rp_hl(z);default:return z->sp;}}
static void set_rp(Md2GbaZ80*z,unsigned p,unsigned idx,uint16_t v){switch(p&3u){case 0:set_bc(z,v);break;case 1:set_de(z,v);break;case 2:if(idx==1)z->ix=v;else if(idx==2)z->iy=v;else set_hl(z,v);break;default:z->sp=v;break;}}
static uint16_t get_rp2(Md2GbaZ80*z,unsigned p,unsigned idx){if((p&3u)==3u)return rp_af(z);return get_rp(z,p,idx);} static void set_rp2(Md2GbaZ80*z,unsigned p,unsigned idx,uint16_t v){if((p&3u)==3u)set_af(z,v);else set_rp(z,p,idx,v);}
static uint16_t idx_addr(Md2GbaZ80*z,unsigned idx,int8_t d){uint16_t base=idx==1?z->ix:idx==2?z->iy:rp_hl(z);return (uint16_t)(base+(int16_t)d);}
static uint8_t get_r(Md2GbaZ80*z,Md2GbaBus*b,unsigned r,unsigned idx,int8_t d){switch(r&7u){case 0:return z->b;case 1:return z->c;case 2:return z->d;case 3:return z->e;case 4:return idx==1?(uint8_t)(z->ix>>8):idx==2?(uint8_t)(z->iy>>8):z->h;case 5:return idx==1?(uint8_t)z->ix:idx==2?(uint8_t)z->iy:z->l;case 6:return zread(z,b,idx_addr(z,idx,d));default:return z->a;}}
static void set_r(Md2GbaZ80*z,Md2GbaBus*b,unsigned r,unsigned idx,int8_t d,uint8_t v){switch(r&7u){case 0:z->b=v;break;case 1:z->c=v;break;case 2:z->d=v;break;case 3:z->e=v;break;case 4:if(idx==1)z->ix=(uint16_t)((z->ix&0xffu)|((uint16_t)v<<8));else if(idx==2)z->iy=(uint16_t)((z->iy&0xffu)|((uint16_t)v<<8));else z->h=v;break;case 5:if(idx==1)z->ix=(uint16_t)((z->ix&0xff00u)|v);else if(idx==2)z->iy=(uint16_t)((z->iy&0xff00u)|v);else z->l=v;break;case 6:zwrite(z,b,idx_addr(z,idx,d),v);break;default:z->a=v;break;}}

static uint16_t add16_hl(Md2GbaZ80*z,uint16_t a,uint16_t b){uint32_t q=(uint32_t)a+b;uint16_t r=(uint16_t)q;z->f=(uint8_t)((z->f&(F_S|F_Z|F_PV))|((r>>8)&0x28u)|(((a^b^r)&0x1000u)?F_H:0)|(q>0xffffu?F_C:0));return r;}
static uint16_t adc16(Md2GbaZ80*z,uint16_t a,uint16_t b,unsigned carry,unsigned sub){uint32_t q;uint16_t r;if(!sub){q=(uint32_t)a+b+carry;r=(uint16_t)q;z->f=(uint8_t)((r>>8)&0xA8u);if(!r)z->f|=F_Z;if(((a^b^r)&0x1000u))z->f|=F_H;if((~(a^b)&(a^r)&0x8000u))z->f|=F_PV;if(q>0xffffu)z->f|=F_C;}else{uint32_t bb=(uint32_t)b+carry;r=(uint16_t)(a-bb);z->f=(uint8_t)(F_N|((r>>8)&0xA8u));if(!r)z->f|=F_Z;if((a^b^r)&0x1000u)z->f|=F_H;if((a^b)&(a^r)&0x8000u)z->f|=F_PV;if((uint32_t)a<bb)z->f|=F_C;}return r;}
static uint8_t rot_cb(Md2GbaZ80*z,unsigned y,uint8_t v){uint8_t r;unsigned c;switch(y&7u){case 0:c=v>>7;r=(uint8_t)((v<<1)|c);break;case 1:c=v&1u;r=(uint8_t)((v>>1)|(c<<7));break;case 2:c=v>>7;r=(uint8_t)((v<<1)|((z->f&F_C)?1u:0u));break;case 3:c=v&1u;r=(uint8_t)((v>>1)|((z->f&F_C)?0x80u:0u));break;case 4:c=v>>7;r=(uint8_t)(v<<1);break;case 5:c=v&1u;r=(uint8_t)((v>>1)|(v&0x80u));break;case 6:c=0;r=(uint8_t)((v<<4)|(v>>4));break;default:c=v&1u;r=(uint8_t)(v>>1);break;}z->f=(uint8_t)(szp(r)|(c?F_C:0));return r;}
static unsigned exec_cb(Md2GbaZ80*z,Md2GbaBus*b,unsigned idx,int8_t disp,uint8_t op,unsigned indexed){unsigned x=op>>6,y=(op>>3)&7u,r=op&7u;uint8_t v=get_r(z,b,indexed?6u:r,idx,disp),nv=v;if(x==0u){nv=rot_cb(z,y,v);set_r(z,b,indexed?6u:r,idx,disp,nv);if(indexed&&r!=6u)set_r(z,b,r,0,0,nv);}else if(x==1u){uint8_t keep=(uint8_t)(z->f&F_C);z->f=(uint8_t)(keep|F_H|(v&0x28u)|((v&(1u<<y))?0:(F_Z|F_PV))|((y==7u&&(v&0x80u))?F_S:0));}else {nv=(uint8_t)(x==2u?(v&~(1u<<y)):(v|(1u<<y)));set_r(z,b,indexed?6u:r,idx,disp,nv);if(indexed&&r!=6u)set_r(z,b,r,0,0,nv);}return indexed?23u:(r==6u?15u:8u);}

static unsigned exec_ed(Md2GbaZ80*z,Md2GbaBus*b,uint8_t op){unsigned y=(op>>3)&7u,p=y>>1,zv=op&7u;uint16_t nn,v,hl;uint8_t t;int8_t d;
    (void)d;
    if((op&0xC7u)==0x40u){ /* IN r,(C) */ t=0xffu;if(y!=6u)set_r(z,b,y,0,0,t);z->f=(uint8_t)((z->f&F_C)|szp(t));return 12u;}
    if((op&0xC7u)==0x41u){return 12u;} /* OUT (C),r: no Genesis I/O uses port instructions. */
    if((op&0xCFu)==0x42u){hl=adc16(z,rp_hl(z),get_rp(z,p,0),(z->f&F_C)?1u:0u,1u);set_hl(z,hl);return 15u;}
    if((op&0xCFu)==0x4Au){hl=adc16(z,rp_hl(z),get_rp(z,p,0),(z->f&F_C)?1u:0u,0u);set_hl(z,hl);return 15u;}
    if((op&0xCFu)==0x43u){nn=fetch16(z,b);write16(z,b,nn,get_rp(z,p,0));return 20u;}
    if((op&0xCFu)==0x4Bu){nn=fetch16(z,b);set_rp(z,p,0,read16(z,b,nn));return 20u;}
    if((op&0xC7u)==0x44u){z->a=sub8(z,0,z->a,0);return 8u;}
    if((op&0xC7u)==0x45u){z->pc=pop16(z,b);z->iff1=z->iff2;return 14u;}
    if((op&0xC7u)==0x46u){z->im=(uint8_t)((y==2u||y==6u)?1u:(y==3u||y==7u)?2u:0u);return 8u;}
    if(op==0x47u){z->i=z->a;return 9u;} if(op==0x4Fu){z->r=z->a;return 9u;} if(op==0x57u){z->a=z->i;z->f=(uint8_t)((z->f&F_C)|(z->a&0xA8u)|(z->a?0:F_Z)|(z->iff2?F_PV:0));return 9u;} if(op==0x5Fu){z->a=z->r;z->f=(uint8_t)((z->f&F_C)|(z->a&0xA8u)|(z->a?0:F_Z)|(z->iff2?F_PV:0));return 9u;}
    if(op==0x67u||op==0x6Fu){uint16_t a=rp_hl(z);uint8_t m=zread(z,b,a),lo=(uint8_t)(z->a&15u);if(op==0x67u){zwrite(z,b,a,(uint8_t)((m>>4)|(lo<<4)));z->a=(uint8_t)((z->a&0xf0u)|(m&15u));}else{zwrite(z,b,a,(uint8_t)((m<<4)|lo));z->a=(uint8_t)((z->a&0xf0u)|(m>>4));}z->f=(uint8_t)((z->f&F_C)|szp(z->a));return 18u;}
    /* Block transfer/search/I/O families. */
    if((op&0xE4u)==0xA0u){unsigned dec=(op&0x08u)?1u:0u,repeat=(op&0x10u)?1u:0u;uint16_t bc=rp_bc(z),h=rp_hl(z),de=rp_de(z);if(zv==0u){t=zread(z,b,h);zwrite(z,b,de,t);h=(uint16_t)(h+(dec?-1:1));de=(uint16_t)(de+(dec?-1:1));bc=(uint16_t)(bc-1u);set_hl(z,h);set_de(z,de);set_bc(z,bc);z->f=(uint8_t)((z->f&(F_S|F_Z|F_C))|(bc?F_PV:0));if(repeat&&bc){z->pc=(uint16_t)(z->pc-2u);return 21u;}return 16u;}if(zv==1u){t=zread(z,b,h);uint8_t oldc=(uint8_t)(z->f&F_C);uint8_t ar=(uint8_t)(z->a-t);h=(uint16_t)(h+(dec?-1:1));bc=(uint16_t)(bc-1u);set_hl(z,h);set_bc(z,bc);z->f=(uint8_t)(oldc|F_N|(ar&F_S)|(ar?0:F_Z)|(((z->a^t^ar)&0x10u)?F_H:0)|(bc?F_PV:0));if(repeat&&bc&&ar){z->pc=(uint16_t)(z->pc-2u);return 21u;}return 16u;}if(zv==2u){ /* INI/IND */ t=0xffu;zwrite(z,b,h,t);h=(uint16_t)(h+(dec?-1:1));z->b=(uint8_t)(z->b-1u);set_hl(z,h);z->f=(uint8_t)(z->b?F_N:(F_N|F_Z));if(repeat&&z->b){z->pc=(uint16_t)(z->pc-2u);return 21u;}return 16u;}if(zv==3u){ /* OUTI/OUTD */ (void)zread(z,b,h);h=(uint16_t)(h+(dec?-1:1));z->b=(uint8_t)(z->b-1u);set_hl(z,h);z->f=(uint8_t)(z->b?F_N:(F_N|F_Z));if(repeat&&z->b){z->pc=(uint16_t)(z->pc-2u);return 21u;}return 16u;}}
    z->unsupported++;z->last_opcode=op;z->last_prefix=0xEDu;return 8u;
}

static void daa(Md2GbaZ80*z){uint8_t a=z->a,adj=0;unsigned c=(z->f&F_C)?1u:0u;if(!(z->f&F_N)){if((z->f&F_H)||(a&15u)>9u)adj|=6u;if(c||a>0x99u){adj|=0x60u;c=1u;}a=(uint8_t)(a+adj);}else{if(z->f&F_H)adj|=6u;if(c)adj|=0x60u;a=(uint8_t)(a-adj);}z->a=a;z->f=(uint8_t)((z->f&F_N)|(c?F_C:0)|(a&0xA8u)|(a?0:F_Z)|(parity8(a)?F_PV:0));}

static unsigned step_core(Md2GbaZ80*z,Md2GbaBus*b,unsigned idx,uint8_t op){unsigned x=op>>6,y=(op>>3)&7u,zz=op&7u,p=y>>1,q=y&1u;int8_t disp=0;uint8_t v;uint16_t w,nn;unsigned cyc=4u;
    /* Indexed memory operands carry a displacement after the opcode. */
    if(idx && ((x==1u && (y==6u||zz==6u) && op!=0x76u) || (x==2u&&zz==6u) || (x==0u&&(zz==4u||zz==5u||zz==6u)&&y==6u)))disp=(int8_t)fetch8(z,b);
    if(x==0u){switch(zz){case 0: switch(y){case 0:return 4u;case 1:{uint8_t a=z->a,f=z->f;z->a=z->a2;z->f=z->f2;z->a2=a;z->f2=f;return 4u;}case 2:disp=(int8_t)fetch8(z,b);z->b=(uint8_t)(z->b-1u);if(z->b){z->pc=(uint16_t)(z->pc+(int16_t)disp);return 13u;}return 8u;case 3:disp=(int8_t)fetch8(z,b);z->pc=(uint16_t)(z->pc+(int16_t)disp);return 12u;default:disp=(int8_t)fetch8(z,b);if(cond(z,y-4u)){z->pc=(uint16_t)(z->pc+(int16_t)disp);return 12u;}return 7u;}case 1:if(!q){set_rp(z,p,idx,fetch16(z,b));return idx?14u:10u;}w=get_rp(z,2u,idx);w=add16_hl(z,w,get_rp(z,p,0));set_rp(z,2u,idx,w);return idx?15u:11u;case 2:if(!q){switch(p){case 0:zwrite(z,b,rp_bc(z),z->a);return 7u;case 1:zwrite(z,b,rp_de(z),z->a);return 7u;case 2:nn=fetch16(z,b);write16(z,b,nn,get_rp(z,2u,idx));return idx?20u:16u;default:nn=fetch16(z,b);zwrite(z,b,nn,z->a);return 13u;}}else{switch(p){case 0:z->a=zread(z,b,rp_bc(z));return 7u;case 1:z->a=zread(z,b,rp_de(z));return 7u;case 2:nn=fetch16(z,b);set_rp(z,2u,idx,read16(z,b,nn));return idx?20u:16u;default:nn=fetch16(z,b);z->a=zread(z,b,nn);return 13u;}}case 3:w=get_rp(z,p,idx);set_rp(z,p,idx,(uint16_t)(q?w-1u:w+1u));return idx&&p==2u?10u:6u;case 4:v=get_r(z,b,y,idx,disp);v=inc8(z,v);set_r(z,b,y,idx,disp,v);return y==6u?(idx?23u:11u):4u;case 5:v=get_r(z,b,y,idx,disp);v=dec8(z,v);set_r(z,b,y,idx,disp,v);return y==6u?(idx?23u:11u):4u;case 6:v=fetch8(z,b);set_r(z,b,y,idx,disp,v);return y==6u?(idx?19u:10u):7u;default:switch(y){case 0:{unsigned c=z->a>>7;z->a=(uint8_t)((z->a<<1)|c);z->f=(uint8_t)((z->f&(F_S|F_Z|F_PV))|(z->a&0x28u)|(c?F_C:0));break;}case 1:{unsigned c=z->a&1u;z->a=(uint8_t)((z->a>>1)|(c<<7));z->f=(uint8_t)((z->f&(F_S|F_Z|F_PV))|(z->a&0x28u)|(c?F_C:0));break;}case 2:{unsigned c=z->a>>7;z->a=(uint8_t)((z->a<<1)|((z->f&F_C)?1u:0u));z->f=(uint8_t)((z->f&(F_S|F_Z|F_PV))|(z->a&0x28u)|(c?F_C:0));break;}case 3:{unsigned c=z->a&1u;z->a=(uint8_t)((z->a>>1)|((z->f&F_C)?0x80u:0u));z->f=(uint8_t)((z->f&(F_S|F_Z|F_PV))|(z->a&0x28u)|(c?F_C:0));break;}case 4:daa(z);break;case 5:z->a^=0xffu;z->f=(uint8_t)((z->f&(F_S|F_Z|F_PV|F_C))|F_H|F_N|(z->a&0x28u));break;case 6:z->f=(uint8_t)((z->f&(F_S|F_Z|F_PV))|F_C|(z->a&0x28u));break;case 7:{uint8_t oc=(uint8_t)(z->f&F_C);z->f=(uint8_t)((z->f&(F_S|F_Z|F_PV))|(oc?F_H:F_C)|(z->a&0x28u));break;}}return 4u;}}
    if(x==1u){if(op==0x76u){z->halted=1u;return 4u;}v=get_r(z,b,zz,idx,disp);set_r(z,b,y,idx,disp,v);return (y==6u||zz==6u)?(idx?19u:7u):4u;}
    if(x==2u){v=get_r(z,b,zz,idx,disp);alu(z,y,v);return zz==6u?(idx?19u:7u):4u;}
    switch(zz){case 0:if(cond(z,y)){z->pc=pop16(z,b);return 11u;}return 5u;case 1:if(!q){set_rp2(z,p,idx,pop16(z,b));return idx&&p==2u?14u:10u;}switch(p){case 0:z->pc=pop16(z,b);return 10u;case 1:{uint8_t tb=z->b,tc=z->c,td=z->d,te=z->e,th=z->h,tl=z->l;z->b=z->b2;z->c=z->c2;z->d=z->d2;z->e=z->e2;z->h=z->h2;z->l=z->l2;z->b2=tb;z->c2=tc;z->d2=td;z->e2=te;z->h2=th;z->l2=tl;return 4u;}case 2:z->pc=get_rp(z,2u,idx);return idx?8u:4u;default:z->sp=get_rp(z,2u,idx);return idx?10u:6u;}case 2:nn=fetch16(z,b);if(cond(z,y))z->pc=nn;return 10u;case 3:switch(y){case 0:z->pc=fetch16(z,b);return 10u;case 1:{uint8_t cb=fetch8(z,b);return exec_cb(z,b,0,0,cb,0);}case 2:(void)fetch8(z,b);return 11u;case 3:(void)fetch8(z,b);z->a=0xffu;z->f=(uint8_t)((z->f&F_C)|szp(z->a));return 11u;case 4:w=read16(z,b,z->sp);write16(z,b,z->sp,get_rp(z,2u,idx));set_rp(z,2u,idx,w);return idx?23u:19u;case 5:w=rp_de(z);set_de(z,rp_hl(z));set_hl(z,w);return 4u;case 6:z->iff1=z->iff2=0u;z->ei_delay=0u;return 4u;default:z->ei_delay=2u;return 4u;}case 4:nn=fetch16(z,b);if(cond(z,y)){push16(z,b,z->pc);z->pc=nn;return 17u;}return 10u;case 5:if(!q){push16(z,b,get_rp2(z,p,idx));return idx&&p==2u?15u:11u;}if(p==0u){nn=fetch16(z,b);push16(z,b,z->pc);z->pc=nn;return 17u;}z->unsupported++;z->last_opcode=op;return 4u;case 6:v=fetch8(z,b);alu(z,y,v);return 7u;default:push16(z,b,z->pc);z->pc=(uint16_t)(y*8u);return 11u;}
    return cyc;
}

static unsigned step_one(Md2GbaZ80*z,Md2GbaBus*b){uint8_t op,op2;unsigned idx=0u;int8_t d;z->last_pc=z->pc;
#if defined(MD2GBA_Z80_PROFILE)
    if(z->pc<0x2000u) md2gba_z80_profile[z->pc]++;
#endif
    if(z->halted)return 4u;
    op=fetch8(z,b);z->last_prefix=0u;z->last_opcode=op;
    if(op==0xDDu||op==0xFDu){idx=(op==0xDDu)?1u:2u;z->last_prefix=op;op=fetch8(z,b);z->last_opcode=op;if(op==0xDDu||op==0xFDu){idx=(op==0xDDu)?1u:2u;op=fetch8(z,b);z->last_opcode=op;}if(op==0xCBu){d=(int8_t)fetch8(z,b);op2=fetch8(z,b);return exec_cb(z,b,idx,d,op2,1u);}if(op==0xEDu){op2=fetch8(z,b);return exec_ed(z,b,op2)+4u;}return step_core(z,b,idx,op)+4u;}
    if(op==0xCBu){op2=fetch8(z,b);return exec_cb(z,b,0,0,op2,0);}
    if(op==0xEDu){op2=fetch8(z,b);z->last_prefix=0xEDu;z->last_opcode=op2;return exec_ed(z,b,op2);}
    return step_core(z,b,0,op);
}

void md2gba_z80_reset(Md2GbaZ80*z){uint8_t reset=z->reset_active;zzero(z,sizeof(*z));z->sp=0xffffu;z->reset_active=reset;}
void md2gba_z80_init(Md2GbaZ80*z){zzero(z,sizeof(*z));z->sp=0xffffu;z->reset_active=1u;}
void md2gba_z80_raise_irq(Md2GbaZ80*z){if(z)z->irq_pending=1u;}
static unsigned take_irq(Md2GbaZ80*z,Md2GbaBus*b){uint16_t vec;if(!z->irq_pending||!z->iff1||z->ei_delay)return 0u;z->irq_pending=0u;z->iff1=z->iff2=0u;z->halted=0u;push16(z,b,z->pc);if(z->im==2u){vec=(uint16_t)(((uint16_t)z->i<<8)|0xffu);z->pc=read16(z,b,vec);return 19u;}z->pc=0x0038u;return 13u;}
static __attribute__((always_inline)) inline int idle_wait_holds(Md2GbaZ80*z,Md2GbaBus*b){
    uint8_t v;
    if(!z->idle_wait_valid || z->pc!=z->idle_wait_pc)return 0;
    v=zread(z,b,z->idle_wait_addr);
    return z->idle_wait_sense ? v!=0u : v==0u;
}
static int detect_idle_wait(Md2GbaZ80*z,Md2GbaBus*b){
    /* Generic Z80 driver pattern: LD A,(nn); OR A; JR Z/NZ back to the load.
       Once the branch is known to wait on external/shared state, yielding the
       remainder of this scanline is cycle-equivalent for observable sound I/O
       and avoids interpreting the tiny poll loop thousands of times. */
    uint16_t pc=z->pc,nn,target; uint8_t rel,op;
    if(zread(z,b,pc)!=0x3Au || zread(z,b,(uint16_t)(pc+3u))!=0xB7u)return 0;
    op=zread(z,b,(uint16_t)(pc+4u));if(op!=0x28u && op!=0x20u)return 0;
    rel=zread(z,b,(uint16_t)(pc+5u));target=(uint16_t)(pc+6u+(int8_t)rel);if(target!=pc)return 0;
    nn=(uint16_t)(zread(z,b,(uint16_t)(pc+1u))|((uint16_t)zread(z,b,(uint16_t)(pc+2u))<<8));
    z->idle_wait_pc=pc;z->idle_wait_addr=nn;z->idle_wait_sense=(uint8_t)(op==0x20u);z->idle_wait_valid=1u;
    return idle_wait_holds(z,b);
}

static __attribute__((always_inline)) inline void z80_r_advance(Md2GbaZ80*z,unsigned n){
    unsigned lo=((unsigned)z->r&0x7fu);lo=(lo+n)&0x7fu;z->r=(uint8_t)((z->r&0x80u)|lo);
}


/* P91: compiler-generated native Z80 continuations.  The generated include
   is specific to the compiler input/profile; this reusable runtime contains no
   title-specific Z80 PCs.  Every generated continuation guards the bytes it
   was compiled from and returns zero on mismatch, preserving interpreter
   fallback for self-modifying/dynamically uploaded code. */
#if defined(MD2GBA_Z80_NATIVE_MODULE)
#include "md2gba_z80_native.inc"
#else
static __attribute__((always_inline)) inline unsigned md2gba_z80_native_step_generated(Md2GbaZ80*z,Md2GbaBus*b,unsigned budget,unsigned*guest){
    (void)z;(void)b;(void)budget;(void)guest;return 0u;
}
#endif

static __attribute__((noinline)) unsigned native_hot_chain(Md2GbaZ80*z,Md2GbaBus*b,unsigned budget,unsigned*guest){
    unsigned used=0u,count=0u,c,g;
    if(z->ei_delay||z->halted)return 0u;
    while(used<budget){
        g=0u;c=md2gba_z80_native_step_generated(z,b,budget-used,&g);
        if(!c)break;
        used+=c;count+=g;
    }
    *guest=count;return used;
}

/* P90: PSC-style structural lowering for stable Z80 hot blocks.  These are
   byte-pattern recognizers, never title/PC checks.  They preserve the current
   interpreter's register/flag/memory/audio semantics while collapsing common
   driver idioms into one native host block. */
static __attribute__((noinline)) unsigned fast_nop_run(Md2GbaZ80*z,Md2GbaBus*b,unsigned budget,unsigned*guest){
    unsigned pc=z->pc,n=0u,limit=budget>>2;
    if(pc>=0x2000u||limit<2u)return 0u;if(limit>32u)limit=32u;
    while(n<limit && (pc+n)<0x2000u && b->z80_ram[pc+n]==0x00u)n++;
    if(n<2u)return 0u;
    z->last_pc=(uint16_t)(pc+n-1u);z->last_opcode=0u;z->last_prefix=0u;
    z->pc=(uint16_t)(pc+n);z80_r_advance(z,n);*guest=n;return n*4u;
}
static __attribute__((noinline)) unsigned fast_repeat_ldhl(Md2GbaZ80*z,Md2GbaBus*b,unsigned budget,unsigned*guest){
    unsigned pc=z->pc,n=0u,limit=0u;uint8_t lo,hi;
    if(pc+5u>=0x2000u||budget<20u||b->z80_ram[pc]!=0x21u)return 0u;
    { volatile unsigned rem=budget; while(rem>=10u && limit<12u){rem-=10u;limit++;} }
    lo=b->z80_ram[pc+1u];hi=b->z80_ram[pc+2u];
    while(n<limit){unsigned q=pc+3u*n;if(q+2u>=0x2000u||b->z80_ram[q]!=0x21u||b->z80_ram[q+1u]!=lo||b->z80_ram[q+2u]!=hi)break;n++;}
    if(n<2u)return 0u;
    set_hl(z,(uint16_t)(lo|((uint16_t)hi<<8)));z->pc=(uint16_t)(pc+3u*n);z80_r_advance(z,3u*n);
    z->last_pc=(uint16_t)(pc+3u*(n-1u));z->last_opcode=0x21u;z->last_prefix=0u;*guest=n;return 10u*n;
}
static __attribute__((noinline)) unsigned fast_table_dispatch(Md2GbaZ80*z,Md2GbaBus*b,unsigned budget,unsigned*guest){
    unsigned pc=z->pc;uint8_t*m;uint16_t table,seladdr,fallback,savedbc;uint8_t base;
    if(pc+22u>=0x2000u||budget<51u)return 0u;m=b->z80_ram+pc;
    if(m[0]!=0xC5u||m[1]!=0x21u||m[4]!=0x3Au||m[7]!=0xD6u||m[9]!=0xFAu||
       m[12]!=0x06u||m[13]!=0x00u||m[14]!=0x4Fu||m[15]!=0x09u||m[16]!=0x09u||
       m[17]!=0x7Eu||m[18]!=0x23u||m[19]!=0x66u||m[20]!=0x6Fu||m[21]!=0xC1u||m[22]!=0xE9u)return 0u;
    table=(uint16_t)(m[2]|((uint16_t)m[3]<<8));seladdr=(uint16_t)(m[5]|((uint16_t)m[6]<<8));base=m[8];fallback=(uint16_t)(m[10]|((uint16_t)m[11]<<8));
    if(seladdr>=0x4000u)return 0u;
    savedbc=rp_bc(z);push16(z,b,savedbc);set_hl(z,table);z->a=zread(z,b,seladdr);z->a=sub8(z,z->a,base,0);
    z80_r_advance(z,12u);z->last_pc=(uint16_t)(pc+9u);z->last_opcode=0xFAu;z->last_prefix=0u;*guest=5u;
    z->pc=(z->f&F_S)?fallback:(uint16_t)(pc+12u);return 51u;
}
static __attribute__((noinline)) unsigned fast_table_dispatch_suffix(Md2GbaZ80*z,Md2GbaBus*b,unsigned budget,unsigned*guest){
    unsigned pc=z->pc;uint8_t*m;
    if(pc+10u>=0x2000u||budget<71u)return 0u;m=b->z80_ram+pc;
    if(m[0]!=0x06u||m[1]!=0x00u||m[2]!=0x4Fu||m[3]!=0x09u||m[4]!=0x09u||m[5]!=0x7Eu||m[6]!=0x23u||m[7]!=0x66u||m[8]!=0x6Fu||m[9]!=0xC1u||m[10]!=0xE9u)return 0u;
    z->b=0u;z->c=z->a;set_hl(z,add16_hl(z,rp_hl(z),rp_bc(z)));set_hl(z,add16_hl(z,rp_hl(z),rp_bc(z)));
    z->a=zread(z,b,rp_hl(z));set_hl(z,(uint16_t)(rp_hl(z)+1u));z->h=zread(z,b,rp_hl(z));z->l=z->a;set_bc(z,pop16(z,b));z->pc=rp_hl(z);
    z80_r_advance(z,11u);z->last_pc=(uint16_t)(pc+10u);z->last_opcode=0xE9u;z->last_prefix=0u;*guest=10u;return 71u;
}
static __attribute__((noinline)) unsigned fast_ym_dac_block(Md2GbaZ80*z,Md2GbaBus*b,unsigned budget,unsigned*guest){
    unsigned pc=z->pc;uint8_t*m,imm;uint16_t gate,flag,exit;
    if(pc+29u>=0x2000u||budget<104u)return 0u;m=b->z80_ram+pc;
    if(m[0]!=0x3Eu||m[2]!=0x32u||m[5]!=0x21u||m[6]!=0x00u||m[7]!=0x40u||m[8]!=0x1Au||
       m[9]!=0x36u||m[10]!=0x2Au||m[11]!=0x23u||m[12]!=0x77u||m[13]!=0xAFu||m[14]!=0x32u||
       m[17]!=0x3Au||m[20]!=0xB7u||m[21]!=0xF2u||m[24]!=0x13u||m[25]!=0x0Bu||m[26]!=0x78u||m[27]!=0xB1u||m[28]!=0x20u)return 0u;
    if(m[3]!=m[15]||m[4]!=m[16])return 0u;
    gate=(uint16_t)(m[3]|((uint16_t)m[4]<<8));flag=(uint16_t)(m[18]|((uint16_t)m[19]<<8));exit=(uint16_t)(m[22]|((uint16_t)m[23]<<8));imm=m[1];
    if(flag>=0x4000u||gate>=0x4000u)return 0u;
    z->a=imm;zwrite(z,b,gate,z->a);set_hl(z,0x4000u);z->a=zread(z,b,rp_de(z));zwrite(z,b,0x4000u,0x2Au);set_hl(z,0x4001u);zwrite(z,b,0x4001u,z->a);
    z->a=0u;z->f=szp(0u);zwrite(z,b,gate,0u);z->a=zread(z,b,flag);z->f=szp(z->a);
    z80_r_advance(z,24u);z->last_pc=(uint16_t)(pc+21u);z->last_opcode=0xF2u;z->last_prefix=0u;*guest=12u;
    z->pc=(!(z->f&F_S))?exit:(uint16_t)(pc+24u);return 104u;
}
static __attribute__((noinline)) unsigned fast_ym_dac_suffix(Md2GbaZ80*z,Md2GbaBus*b,unsigned budget,unsigned*guest){
    unsigned pc=z->pc;uint8_t*m,rel;uint16_t bc;unsigned nz,need;
    (void)b;if(pc+5u>=0x2000u)return 0u;m=b->z80_ram+pc;
    if(m[0]!=0x13u||m[1]!=0x0Bu||m[2]!=0x78u||m[3]!=0xB1u||m[4]!=0x20u)return 0u;
    bc=rp_bc(z);nz=((uint16_t)(bc-1u)!=0u);need=nz?32u:27u;if(budget<need)return 0u;rel=m[5];
    set_de(z,(uint16_t)(rp_de(z)+1u));set_bc(z,(uint16_t)(bc-1u));z->a=z->b;z->a=(uint8_t)(z->a|z->c);z->f=szp(z->a);
    z->pc=nz?(uint16_t)(pc+6u+(int8_t)rel):(uint16_t)(pc+6u);z80_r_advance(z,6u);z->last_pc=(uint16_t)(pc+4u);z->last_opcode=0x20u;z->last_prefix=0u;*guest=5u;return need;
}
static __attribute__((noinline)) unsigned fast_ym_tail(Md2GbaZ80*z,Md2GbaBus*b,unsigned budget,unsigned*guest){
    unsigned pc=z->pc,n=0u;uint8_t*m;uint16_t flag,target;uint8_t v;
    if(pc+9u>=0x2000u||budget<41u)return 0u;m=b->z80_ram+pc;
    if(m[0]!=0x3Au||m[3]!=0xB7u||m[4]!=0x7Eu||m[5]!=0x7Eu)return 0u;
    /* Accept two or more consecutive LD HL,$4000 timing loads followed by JP nn. */
    {unsigned q=6u;while(q+2u<0x2000u-pc && m[q]==0x21u&&m[q+1u]==0x00u&&m[q+2u]==0x40u&&n<12u){n++;q+=3u;}if(n<2u||m[q]!=0xC3u)return 0u;target=(uint16_t)(m[q+1u]|((uint16_t)m[q+2u]<<8));
     {unsigned cyc=13u+4u+7u+7u+10u*n+10u;unsigned bytes=q+3u;if(budget<cyc)return 0u;flag=(uint16_t)(m[1]|((uint16_t)m[2]<<8));z->a=zread(z,b,flag);z->f=szp(z->a);v=zread(z,b,rp_hl(z));z->a=v;v=zread(z,b,rp_hl(z));z->a=v;set_hl(z,0x4000u);z->pc=target;z80_r_advance(z,bytes);z->last_pc=(uint16_t)(pc+q);z->last_opcode=0xC3u;z->last_prefix=0u;*guest=5u+n;return cyc;}}
}
static __attribute__((always_inline)) inline unsigned try_fast_block(Md2GbaZ80*z,Md2GbaBus*b,unsigned budget,unsigned*guest){
    uint8_t op;
    if(z->ei_delay||z->halted||z->pc>=0x2000u)return 0u;
    op=b->z80_ram[z->pc];
    /* Dispatch by the first opcode so an ordinary scalar instruction pays at
       most one recognizer call.  This keeps the structural fusions useful
       even when their bodies remain in prefetched ROM to conserve IWRAM. */
    switch(op){
        case 0x00u:return fast_nop_run(z,b,budget,guest);
        case 0x21u:return fast_repeat_ldhl(z,b,budget,guest);
        case 0xC5u:return fast_table_dispatch(z,b,budget,guest);
        case 0x06u:return fast_table_dispatch_suffix(z,b,budget,guest);
        case 0x3Eu:return fast_ym_dac_block(z,b,budget,guest);
        case 0x13u:return fast_ym_dac_suffix(z,b,budget,guest);
        case 0x3Au:return fast_ym_tail(z,b,budget,guest);
        default:return 0u;
    }
}

Z80_HOT unsigned md2gba_z80_run(Md2GbaZ80*z,Md2GbaBus*b,unsigned budget){unsigned used=0u,c,g;if(!z||!b)return 0u;
    if(b->z80_reset==0u){if(!z->reset_active){z->reset_active=1u;md2gba_z80_reset(z);z->reset_active=1u;}return 0u;}
    if(z->reset_active){z->reset_active=0u;md2gba_z80_reset(z);z->reset_active=0u;}
    if(b->z80_bus_request&0x0100u)return 0u;
    while(used<budget){
        g=1u;c=take_irq(z,b);
        if(!c){if(idle_wait_holds(z,b) || detect_idle_wait(z,b))break;g=0u;c=native_hot_chain(z,b,budget-used,&g);if(!c)c=try_fast_block(z,b,budget-used,&g);if(!c){c=step_one(z,b);g=1u;}}
        used+=c;z->executed+=g;if(z->ei_delay){z->ei_delay--;if(!z->ei_delay)z->iff1=z->iff2=1u;}if(z->unsupported)break;
    }
    return used;
}
