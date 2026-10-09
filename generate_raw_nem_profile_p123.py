#!/usr/bin/env python3
import csv,argparse
from pathlib import Path
ap=argparse.ArgumentParser();ap.add_argument('csv',type=Path);ap.add_argument('-o','--out',type=Path,required=True);a=ap.parse_args();a.out.mkdir(parents=True,exist_ok=True)
rows=list(csv.DictReader(a.csv.open()))
assets=[]
for src in ['1CD370','1CD2A6','1CD462','2A2B7E','2A303A']:
    rs=[r for r in rows if r['asset'].upper()==src]
    if src=='2A303A':
        first=rs[0]['inv'];rs=[r for r in rs if r['inv']==first]
    assets.append((src,rs))
all2=[r for r in rows if r['asset'].upper()=='2A303A']; invs=sorted(set(r['inv'] for r in all2),key=int)
if len(invs)>1:
    x=[r for r in all2 if r['inv']==invs[0]];y=[r for r in all2 if r['inv']==invs[1]]
    fields=[k for k in x[0] if k not in ('inv','rec')]
    for i,(u,v) in enumerate(zip(x,y)):
        if int(u['valid']) and any(u[f]!=v[f] for f in fields): raise SystemExit(f'2A303A differs at valid record {i}')
def hx(s):return int(s,16)
def bswap32(v):return ((v&0xff)<<24)|((v&0xff00)<<8)|((v>>8)&0xff00)|((v>>24)&0xff)
h=a.out/'md2gba_profile_raw_nem.h';c=a.out/'md2gba_profile_raw_nem.c'
h.write_text('''#ifndef MD2GBA_PROFILE_RAW_NEM_H\n#define MD2GBA_PROFILE_RAW_NEM_H\n#include <stdint.h>\n#include "md2gba_interp68k.h"\n#include "md2gba_bus.h"\n#include "md2gba_timing.h"\nvoid md2gba_profile_raw_nem_reset(void);\nint md2gba_profile_raw_nem(Md2Gba68k*,Md2GbaBus*,uint32_t left,uint32_t*cost,uint32_t*steps);\nint md2gba_profile_raw_nem_vblank_batch(Md2Gba68k*,Md2GbaBus*,Md2GbaTiming*,uint32_t *left,uint32_t *cost,uint32_t *steps,uint32_t *records);\n#endif\n''')
L=[]
L.append('''/* Auto-generated P123 stacked indexed/bulk Nemesis replay.\n   P119 whole-command records remain the exact state oracle/fallback. P122\n   replaces P121's two linear command scans with cumulative cycle/step/output\n   tables and publishes each admitted sequential output run through one exact\n   bulk virtual-VDP helper. */\n#include <stdint.h>\n#include "md2gba_profile_raw_nem.h"\n#if defined(MD2GBA_GBA_IWRAM)\n#define NEM_HOT __attribute__((section(".iwram.nem_rawcmd"),noinline))\n#else\n#define NEM_HOT\n#endif\ntypedef struct {\n uint32_t pre_a0,pre_a5; uint32_t pre_d[8];\n uint32_t post_a0,post_a5; uint32_t post_d[8]; uint32_t outv;\n uint16_t pre_sr,post_sr,cycles,steps; uint8_t valid,outn; uint16_t pad;\n} NemRec;\ntypedef struct {\n const NemRec*r; const uint32_t*cyc; const uint32_t*steps;\n const uint16_t*outp; const uint32_t*outv;\n uint16_t n,valid_n; uint32_t a3;\n} NemAsset;\n''')
for ai,(src,rs) in enumerate(assets):
    valid_n=0
    for r in rs:
        if int(r['valid']): valid_n+=1
        else: break
    if any(int(r['valid']) for r in rs[valid_n:]): raise SystemExit(f'nonterminal invalid record in {src}')
    cyc=[0];steps=[0];outp=[0];outs=[]
    for r in rs[:valid_n]:
        cyc.append(cyc[-1]+int(r['cycles']));steps.append(steps[-1]+int(r['steps']))
        if int(r['outn']): outs.append(bswap32(hx(r['outv'])))
        outp.append(len(outs))
    if len(outs)>0xffff:raise SystemExit('output prefix overflow')
    L.append(f'static const NemRec nem_{ai}[]={{\n')
    for r in rs:
        pre_d=','.join(f'0x{hx(r[f"pre_d{i}"]):08X}u' for i in range(8));post_d=','.join(f'0x{hx(r[f"post_d{i}"]):08X}u' for i in range(8))
        L.append('{0x%06Xu,0x%06Xu,{%s},0x%06Xu,0x%06Xu,{%s},0x%08Xu,0x%04Xu,0x%04Xu,%uu,%uu,%uu,%uu,0u},\n'%(
            hx(r['pre_a0']),hx(r['pre_a5']),pre_d,hx(r['post_a0']),hx(r['post_a5']),post_d,hx(r['outv']),hx(r['pre_sr']),hx(r['post_sr']),int(r['cycles']),int(r['steps']),int(r['valid']),int(r['outn'])))
    L.append('};\n')
    def arr(typ,name,vals,per=12,fmt=str):
        L.append(f'static const {typ} {name}[]={{\n')
        for i in range(0,len(vals),per):L.append(','.join(fmt(v) for v in vals[i:i+per])+',\n')
        L.append('};\n')
    arr('uint32_t',f'nem_cyc_{ai}',cyc,10,lambda v:f'{v}u')
    arr('uint32_t',f'nem_steps_{ai}',steps,10,lambda v:f'{v}u')
    arr('uint16_t',f'nem_outp_{ai}',outp,16,lambda v:f'{v}u')
    arr('uint32_t',f'nem_out_{ai}',outs,8,lambda v:f'0x{v:08X}u')
    print(src,'records',len(rs),'valid',valid_n,'outputs',len(outs),'cycles',cyc[-1])
L.append('static const NemAsset nem_assets[]={\n')
for ai,(src,rs) in enumerate(assets):
    valid_n=next((i for i,r in enumerate(rs) if not int(r['valid'])),len(rs))
    L.append(f'{{nem_{ai},nem_cyc_{ai},nem_steps_{ai},nem_outp_{ai},nem_out_{ai},{len(rs)}u,{valid_n}u,0x{hx(rs[0]["pre_a3"]):06X}u}}, /* {src} */\n')
L.append('};\nstatic uint16_t nem_asset=0xFFFFu,nem_index;\nvoid md2gba_profile_raw_nem_reset(void){nem_asset=0xFFFFu;nem_index=0u;}\n')
L.append(r'''static inline uint32_t nem_arch_restore(uint32_t olda,uint32_t bus24){return (olda&0xFF000000u)|(bus24&0xFFFFFFu);}
static int nem_match(const NemRec*r,const NemAsset*a,const Md2Gba68k*c){
 uint32_t x;
 /* P134Z: late-game profiling found thousands of raw-Nemesis attempts that
    all refuse.  Reject on the most discriminating cheap keys before loading
    the full register/SR state.  This is semantically identical to the old
    OR-reduction: it only changes evaluation order. */
 if((c->a[0]&0xFFFFFFu)!=r->pre_a0||(c->a[5]&0xFFFFFFu)!=r->pre_a5)return 0;
 if((c->a[3]&0xFFFFFFu)!=a->a3||(c->a[1]&0xFFFFFFu)!=0xFFE200u||(c->a[2]&0xFFFFFFu)!=0xFFFB0Eu)return 0;
 if((c->a[4]&0xFFFFFFu)!=0xC00000u||(c->a[6]&0xFFFFFFu)!=0xC00004u||c->sr!=r->pre_sr)return 0;
 x=c->d[0]^r->pre_d[0];x|=c->d[1]^r->pre_d[1];x|=c->d[2]^r->pre_d[2];x|=c->d[3]^r->pre_d[3];
 x|=c->d[4]^r->pre_d[4];x|=c->d[5]^r->pre_d[5];x|=c->d[6]^r->pre_d[6];x|=c->d[7]^r->pre_d[7];return x==0u;
}
static unsigned nem_find_end(const NemAsset*a,unsigned ix,uint32_t budget){
 uint32_t base,limit;unsigned lo,hi,best;
 if(!a||ix>=a->valid_n||!budget)return ix;base=a->cyc[ix];limit=base+budget;lo=ix+1u;hi=a->valid_n;best=ix;
 while(lo<=hi){unsigned mid=lo+((hi-lo)>>1);if(a->cyc[mid]<limit){best=mid;lo=mid+1u;}else{if(!mid)break;hi=mid-1u;}}
 return best;
}
NEM_HOT int md2gba_profile_raw_nem(Md2Gba68k*c,Md2GbaBus*b,uint32_t left,uint32_t*cost,uint32_t*steps){
 const NemRec*r=0;const NemAsset*a=0;unsigned ai=nem_asset,ix=nem_index,k,i;
 if(ai<(sizeof(nem_assets)/sizeof(nem_assets[0]))){a=&nem_assets[ai];if(ix<a->n&&nem_match(&a->r[ix],a,c))r=&a->r[ix];}
 if(!r){
  if(ai<(sizeof(nem_assets)/sizeof(nem_assets[0]))){a=&nem_assets[ai];for(k=ix;k<a->n&&k<ix+4u;k++)if(nem_match(&a->r[k],a,c)){ix=k;r=&a->r[k];break;}}
  if(!r){for(ai=0u;ai<(sizeof(nem_assets)/sizeof(nem_assets[0]));ai++){a=&nem_assets[ai];if(a->n&&nem_match(&a->r[0],a,c)){ix=0u;r=&a->r[0];break;}}}
  if(!r){nem_asset=0xFFFFu;nem_index=0u;return 0;}nem_asset=(uint16_t)ai;nem_index=(uint16_t)ix;
 }
 if(!r->valid || left<=(uint32_t)r->cycles){nem_index=(uint16_t)(ix+1u);return 0;}
 if(r->outn){md2gba_write32(b,0xC00000u,r->outv);}
 c->a[0]=nem_arch_restore(c->a[0],r->post_a0);c->a[5]=nem_arch_restore(c->a[5],r->post_a5);for(i=0;i<8u;i++)c->d[i]=r->post_d[i];c->sr=r->post_sr;c->pc=0x04178Au;
 nem_index=(uint16_t)(ix+1u);*cost=r->cycles;*steps=r->steps;return 1;
}

NEM_HOT int md2gba_profile_raw_nem_vblank_batch(Md2Gba68k*c,Md2GbaBus*b,Md2GbaTiming*t,uint32_t*left,uint32_t*cost,uint32_t*steps,uint32_t*records){
 const NemRec*r=0,*last=0;const NemAsset*a=0;unsigned ai=nem_asset,ix=nem_index,k,i,end,o0,o1;
 uint32_t budget,total,nsteps;
 if(cost)*cost=0u;if(steps)*steps=0u;if(records)*records=0u;
 if(!c||!b||!t||!left||!*left)return 0;if(b->vdp.regs[0]&0x10u)return 0;
 if(ai<(sizeof(nem_assets)/sizeof(nem_assets[0]))){a=&nem_assets[ai];if(ix<a->n&&nem_match(&a->r[ix],a,c))r=&a->r[ix];}
 if(!r){
  if(ai<(sizeof(nem_assets)/sizeof(nem_assets[0]))){a=&nem_assets[ai];for(k=ix;k<a->n&&k<ix+4u;k++)if(nem_match(&a->r[k],a,c)){ix=k;r=&a->r[k];break;}}
  if(!r){for(ai=0u;ai<(sizeof(nem_assets)/sizeof(nem_assets[0]));ai++){a=&nem_assets[ai];if(a->n&&nem_match(&a->r[0],a,c)){ix=0u;r=&a->r[0];break;}}}
  if(!r){nem_asset=0xFFFFu;nem_index=0u;return 0;}nem_asset=(uint16_t)ai;nem_index=(uint16_t)ix;
 }
 budget=md2gba_timing_scheduled_cycles_to_vblank(t,*left);if(!budget)return 0;end=nem_find_end(a,ix,budget);if(end<=ix)return 0;
 total=a->cyc[end]-a->cyc[ix];nsteps=a->steps[end]-a->steps[ix];o0=a->outp[ix];o1=a->outp[end];
 if(o1>o0 && !md2gba_vdp_data32_stream_aligned_ready(b,o1-o0))return 0;
 last=&a->r[end-1u];if(!md2gba_timing_consume_unobservable_span_budgeted(t,b,left,total,budget))return 0;
 if(o1>o0)md2gba_vdp_data32_stream_preswapped_unchecked(b,a->outv+o0,o1-o0);
 c->a[0]=nem_arch_restore(c->a[0],last->post_a0);c->a[5]=nem_arch_restore(c->a[5],last->post_a5);c->d[0]=last->post_d[0];c->d[1]=last->post_d[1];c->d[2]=last->post_d[2];c->d[3]=last->post_d[3];c->d[4]=last->post_d[4];c->d[5]=last->post_d[5];c->d[6]=last->post_d[6];c->d[7]=last->post_d[7];c->sr=last->post_sr;c->pc=0x04178Au;
 nem_index=(uint16_t)end;if(cost)*cost=total;if(steps)*steps=nsteps;if(records)*records=end-ix;return 1;
}
''')
c.write_text(''.join(L))
print('generated P123',sum(len(rs) for _,rs in assets),'records /',len(assets),'assets',c.stat().st_size,'bytes C')
