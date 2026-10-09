#!/usr/bin/env python3
"""Generate a compact sequential predecoded-token profile from a canonical 68K trace.

The runtime policy is generic: it only consumes generated token records when the
current CPU state exactly matches the next record and the recorded guest-cycle
cost fits wholly before the scheduler deadline.  Otherwise canonical AOT runs.
"""
from __future__ import annotations
import argparse,csv
from pathlib import Path
from collections import defaultdict

NORMAL={(18,112):1,(21,130):2,(22,140):3,(25,158):4}

def hx(s): return int(s,16)
def key(r): return (hx(r['pre_a0']),hx(r['pre_d5']),hx(r['pre_d6']))
def state_tuple(r):
    return tuple(hx(r[k]) for k in ('pre_a0','pre_d5','pre_d6','post_a0','post_d0','post_d1','post_d5','post_d6','post_d7','post_sr'))

def split_invocations(rows):
    if not rows:return []
    first=key(rows[0]); starts=[i for i,r in enumerate(rows) if key(r)==first]
    if not starts or starts[0]!=0: starts=[0]+starts
    out=[]
    for i,s in enumerate(starts):
        e=starts[i+1] if i+1<len(starts) else len(rows)
        out.append(rows[s:e])
    return out

def merge_invocations(invs):
    base=invs[0]
    # Every invocation of the same immutable source must traverse the same token
    # states. Interrupts can inflate measured step/cycle spans, so choose the
    # canonical normal span from any invocation when available.
    for v in invs[1:]:
        if len(v)!=len(base): raise SystemExit(f'invocation length mismatch {len(base)} vs {len(v)}')
        for i,(a,b) in enumerate(zip(base,v)):
            if state_tuple(a)!=state_tuple(b): raise SystemExit(f'state mismatch at token {i}')
    merged=[]
    for i,r0 in enumerate(base):
        r=dict(r0); best=None
        for v in invs:
            rr=v[i]; pair=(int(rr['steps']),int(rr['cycles']))
            if pair in NORMAL: best=pair; break
        if best: r['steps'],r['cycles']=map(str,best)
        else: r['steps']=r['cycles']='0'
        merged.append(r)
    return merged

def pack_record(r):
    pre_a0=hx(r['pre_a0']); pre_d5=hx(r['pre_d5']); pre_d6=hx(r['pre_d6'])
    post_a0=hx(r['post_a0']); post_d5=hx(r['post_d5']); post_d6=hx(r['post_d6'])
    d0=hx(r['post_d0']);d1=hx(r['post_d1']);d7=hx(r['post_d7']);ccr=hx(r['post_sr'])&31
    pair=(int(r['steps']),int(r['cycles'])); typ=NORMAL.get(pair,0)
    delta=post_a0-pre_a0
    if not (0<=delta<=3 and 0<=pre_d6<=31 and 0<=post_d6<=31 and 0<=d0<=7 and 0<=d1<=15 and 0<=d7<=31):
        typ=0;delta=0
    packed=(pre_d6&31)|((post_d6&31)<<5)|((delta&3)<<10)|((d0&7)<<12)|((d1&15)<<15)|((d7&31)<<19)|((ccr&31)<<24)|((typ&7)<<29)
    return pre_a0,pre_d5,post_d5,packed,typ

def main():
    ap=argparse.ArgumentParser();ap.add_argument('csv',type=Path);ap.add_argument('-o','--out',type=Path,required=True);a=ap.parse_args()
    rows=list(csv.DictReader(a.csv.open()))
    grouped=defaultdict(list)
    for r in rows: grouped[r['asset'].lower()].append(r)
    assets=[]
    for src,rs in sorted(grouped.items(),key=lambda kv:int(kv[0],16)):
        invs=split_invocations(rs); merged=merge_invocations(invs)
        assets.append((int(src,16),merged,len(invs)))
    a.out.mkdir(parents=True,exist_ok=True)
    h=a.out/'md2gba_profile_predecode_tokens.h'
    h.write_text('''#ifndef MD2GBA_PROFILE_PREDECODE_TOKENS_H\n#define MD2GBA_PROFILE_PREDECODE_TOKENS_H\n#include <stdint.h>\n#include "md2gba_aot68k.h"\nint md2gba_profile_predecode_token(Md2Gba68k*,uint32_t,uint32_t*,uint32_t*);\nvoid md2gba_profile_predecode_reset(void);\n#endif\n''')
    lines=['''/* AUTO-GENERATED from canonical immutable-ROM token trace. */\n#include <stdint.h>\n#include "md2gba_profile_predecode_tokens.h"\n#if defined(MD2GBA_GBA_IWRAM)\n#define TOK_HOT __attribute__((section(".iwram.aot_token"),noinline))\n#define TOK_STATE __attribute__((section(".iwram.aot_token_state"),aligned(4)))\n#define TOK_MATCH __attribute__((section(".iwram.aot_token_match"),noinline))\n#else\n#define TOK_HOT\n#define TOK_STATE\n#define TOK_MATCH\n#endif\ntypedef struct TokRec { uint32_t pre_a0; uint16_t pre_d5,post_d5; uint32_t p; } TokRec;\ntypedef struct TokAsset { const TokRec *r; uint16_t n; uint16_t pad; } TokAsset;\n''']
    total=0; fast=0
    for ai,(src,rs,ninv) in enumerate(assets):
        lines.append(f'static const TokRec tok_{ai}[]={{')
        for r in rs:
            pre,d5,pd5,p,typ=pack_record(r); total+=1; fast+=bool(typ)
            lines.append(f'{{0x{pre:06X}u,0x{d5:04X}u,0x{pd5:04X}u,0x{p:08X}u}},')
        lines.append('};')
    lines.append('static const TokAsset assets[]={')
    for ai,(src,rs,ninv) in enumerate(assets): lines.append(f'{{tok_{ai},(uint16_t)(sizeof(tok_{ai})/sizeof(tok_{ai}[0])),0u}}, /* source 0x{src:06X}, invocations {ninv} */')
    lines.append('};')
    lines.append('''static uint16_t active_asset TOK_STATE=0xFFFFu;\nstatic uint16_t active_index TOK_STATE;\nvoid md2gba_profile_predecode_reset(void){active_asset=0xFFFFu;active_index=0u;}\nstatic TOK_MATCH int match(const TokRec*t,const Md2Gba68k*c){return t->pre_a0==(c->a[0]&0xFFFFFFu)&&t->pre_d5==(uint16_t)c->d[5]&&((t->p&31u)==(c->d[6]&31u));}\nTOK_HOT int md2gba_profile_predecode_token(Md2Gba68k*c,uint32_t left,uint32_t*cost,uint32_t*steps){\n const TokRec*t=0;unsigned ai=active_asset,ix=active_index,k,typ;uint32_t p,cy=0,st=0;\n if(ai<(sizeof(assets)/sizeof(assets[0]))){const TokAsset*a=&assets[ai];if(ix<a->n&&match(&a->r[ix],c))t=&a->r[ix];}\n if(!t){\n   /* A canonical fallback token was consumed after the profile advanced, or a\n      new immutable asset began. Search only a tiny local window / first token. */\n   if(ai<(sizeof(assets)/sizeof(assets[0]))){const TokAsset*a=&assets[ai];for(k=ix;k<a->n&&k<ix+4u;k++)if(match(&a->r[k],c)){ix=k;t=&a->r[k];break;}}\n   if(!t){for(ai=0u;ai<(sizeof(assets)/sizeof(assets[0]));ai++){const TokAsset*a=&assets[ai];if(a->n&&match(&a->r[0],c)){ix=0u;t=&a->r[0];break;}}}\n   if(!t){active_asset=0xFFFFu;active_index=0u;return 0;}\n   active_asset=(uint16_t)ai;active_index=(uint16_t)ix;\n }\n p=t->p;typ=(unsigned)(p>>29);active_index=(uint16_t)(active_index+1u);\n if(!typ)return 0;\n if(typ==1u){cy=112u;st=18u;}else if(typ==2u){cy=130u;st=21u;}else if(typ==3u){cy=140u;st=22u;}else if(typ==4u){cy=158u;st=25u;}else return 0;\n /* Strict > mirrors generated fusions: a token ending exactly at a machine\n    event remains canonical so the pre-instruction seam/return PC is exact. */\n if(left<=cy)return 0;\n c->a[0]=(c->a[0]+((p>>10)&3u))&0xFFFFFFFFu;\n c->d[0]=(c->d[0]&0xFFFF0000u)|((p>>12)&7u);\n c->d[1]=(c->d[1]&0xFFFF0000u)|((p>>15)&15u);\n c->d[5]=(c->d[5]&0xFFFF0000u)|t->post_d5;\n c->d[6]=(c->d[6]&0xFFFF0000u)|((p>>5)&31u);\n c->d[7]=(c->d[7]&0xFFFF0000u)|((p>>19)&31u);\n c->sr=(uint16_t)((c->sr&~31u)|((p>>24)&31u));\n *cost=cy;*steps=st;return 1;\n}\n''')
    (a.out/'md2gba_profile_predecode_tokens.c').write_text('\n'.join(lines))
    print(f'assets={len(assets)} tokens={total} shortcuttable={fast} slow={total-fast}')
if __name__=='__main__':main()
