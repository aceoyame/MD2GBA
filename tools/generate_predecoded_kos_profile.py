#!/usr/bin/env python3
"""Generate scheduler-safe predecoded Kosinski command replay records.

Records are compiler/profile data for immutable source streams. Runtime shortcuts
are accepted only on an exact CPU-state match and only when the canonical command
fits wholly before the next scheduled machine event. Otherwise canonical AOT runs.
"""
from __future__ import annotations
import argparse,csv,json
from pathlib import Path
from collections import defaultdict

def hx(s): return int(s,16)
DKEY=[f'pre_d{i}' for i in range(7)]
PKEY=[f'post_d{i}' for i in range(7)]

def main():
    ap=argparse.ArgumentParser();ap.add_argument('csv',type=Path);ap.add_argument('-o','--out',type=Path,required=True);a=ap.parse_args()
    rows=list(csv.DictReader(a.csv.open()))
    grouped=defaultdict(list)
    for r in rows: grouped[r['asset'].lower()].append(r)
    a.out.mkdir(parents=True,exist_ok=True)
    h=a.out/'md2gba_profile_predecode_kos.h'
    h.write_text('''#ifndef MD2GBA_PROFILE_PREDECODE_KOS_H\n#define MD2GBA_PROFILE_PREDECODE_KOS_H\n#include <stdint.h>\n#include "md2gba_aot68k.h"\n#include "md2gba_bus.h"\nint md2gba_profile_predecode_kos(Md2Gba68k*,Md2GbaBus*,uint32_t,uint32_t*,uint32_t*);\nvoid md2gba_profile_predecode_kos_reset(void);\n#endif\n''')
    data=bytearray(); assets=[]; rec_groups=[]; total=fast=0
    for src,rs in sorted(grouped.items(),key=lambda kv:int(kv[0],16)):
        packed=[]
        for r in rs:
            total+=1
            out=bytes.fromhex(r['bytes']) if r['bytes'] else b''
            off=len(data);data.extend(out)
            cy=int(r['cycles']);st=int(r['steps']);irq=int(r['irq']);term=int(r['terminal'])
            ok=(not irq and not term and 0<cy<488 and len(out)<=255)
            fast+=ok
            packed.append(dict(pre_a0=hx(r['pre_a0']),pre_a1=hx(r['pre_a1']),pre_d=[hx(r[k]) for k in DKEY],pre_sr=hx(r['pre_sr']),
                               post_a0=hx(r['post_a0']),post_a1=hx(r['post_a1']),post_d=[hx(r[k]) for k in PKEY],post_sr=hx(r['post_sr']),
                               steps=st,cycles=cy,data_off=off,outn=len(out),fast=1 if ok else 0))
        assets.append((int(src,16),len(rec_groups),len(packed)));rec_groups.append(packed)
    lines=['''/* AUTO-GENERATED from canonical immutable-ROM Kosinski command trace. */
#include <stdint.h>
#include "md2gba_profile_predecode_kos.h"
typedef struct KosRec {
 uint32_t pre_a0,pre_a1,pre_d[7]; uint16_t pre_sr,steps,cycles,data_off;
 uint32_t post_a0,post_a1,post_d[7]; uint16_t post_sr; uint8_t outn,fast;
} KosRec;
typedef struct KosAsset { const KosRec *r; uint16_t n,pad; } KosAsset;
''']
    # Keep byte data separate and naturally packed; all are immutable ROM constants.
    lines.append('static const uint8_t kos_bytes[]={')
    for i in range(0,len(data),24):lines.append(','.join(f'0x{x:02X}' for x in data[i:i+24])+',')
    lines.append('};')
    for ai,rs in enumerate(rec_groups):
        lines.append(f'static const KosRec kos_{ai}[]={{')
        for r in rs:
            pre=','.join(f'0x{x:08X}u' for x in r['pre_d']);post=','.join(f'0x{x:08X}u' for x in r['post_d'])
            lines.append('{0x%06Xu,0x%06Xu,{%s},0x%04Xu,%uu,%uu,%uu,0x%06Xu,0x%06Xu,{%s},0x%04Xu,%uu,%uu},' %
                         (r['pre_a0'],r['pre_a1'],pre,r['pre_sr'],r['steps'],r['cycles'],r['data_off'],r['post_a0'],r['post_a1'],post,r['post_sr'],r['outn'],r['fast']))
        lines.append('};')
    lines.append('static const KosAsset kos_assets[]={')
    for src,ai,n in assets:lines.append(f'{{kos_{ai},{n}u,0u}}, /* source 0x{src:06X} */')
    lines.append('};')
    lines.append('''static uint16_t kos_asset=0xFFFFu,kos_index;
void md2gba_profile_predecode_kos_reset(void){kos_asset=0xFFFFu;kos_index=0u;}
static int kos_match(const KosRec*r,const Md2Gba68k*c){
 unsigned i;
 if(r->pre_a0!=(c->a[0]&0xFFFFFFu)||r->pre_a1!=(c->a[1]&0xFFFFFFu)||r->pre_sr!=c->sr)return 0;
 for(i=0;i<7u;i++)if(r->pre_d[i]!=c->d[i])return 0;
 return 1;
}
/* Trace addresses are bus-domain 24-bit snapshots; restore architectural A regs. */
static inline uint32_t kos_arch_restore(uint32_t olda,uint32_t bus24){return (olda&0xFF000000u)|(bus24&0xFFFFFFu);}
int md2gba_profile_predecode_kos(Md2Gba68k*c,Md2GbaBus*b,uint32_t left,uint32_t*cost,uint32_t*steps){
 const KosRec*r=0;unsigned ai=kos_asset,ix=kos_index,k,i;
 if(ai<(sizeof(kos_assets)/sizeof(kos_assets[0]))){const KosAsset*a=&kos_assets[ai];if(ix<a->n&&kos_match(&a->r[ix],c))r=&a->r[ix];}
 if(!r){
  if(ai<(sizeof(kos_assets)/sizeof(kos_assets[0]))){const KosAsset*a=&kos_assets[ai];for(k=ix;k<a->n&&k<ix+4u;k++)if(kos_match(&a->r[k],c)){ix=k;r=&a->r[k];break;}}
  if(!r){for(ai=0u;ai<(sizeof(kos_assets)/sizeof(kos_assets[0]));ai++){const KosAsset*a=&kos_assets[ai];if(a->n&&kos_match(&a->r[0],c)){ix=0u;r=&a->r[0];break;}}}
  if(!r){kos_asset=0xFFFFu;kos_index=0u;return 0;}
  kos_asset=(uint16_t)ai;kos_index=(uint16_t)ix;
 }
 /* Advance the profile even when this command must execute canonically. The
    next command will exact-match after fallback, just like the Nemesis profile. */
 kos_index=(uint16_t)(kos_index+1u);
 if(!r->fast || left<=r->cycles)return 0;
 /* The generated records used here are WRAM-only output. Refuse any profile
    corruption rather than letting a title-specific record touch MMIO. */
 if((r->pre_a1&0xFF0000u)!=0xFF0000u || ((r->pre_a1+r->outn)&0xFFFFFFu)<r->pre_a1)return 0;
 for(i=0;i<r->outn;i++)md2gba_write8(b,(r->pre_a1+i)&0xFFFFFFu,kos_bytes[r->data_off+i]);
 c->a[0]=kos_arch_restore(c->a[0],r->post_a0);c->a[1]=kos_arch_restore(c->a[1],r->post_a1);
 for(i=0;i<7u;i++)c->d[i]=r->post_d[i];c->sr=r->post_sr;
 *cost=r->cycles;*steps=r->steps;return 1;
}
''')
    (a.out/'md2gba_profile_predecode_kos.c').write_text('\n'.join(lines))
    hooks={
      'hooks':{
        '0x04178A':{'next':'0x0417C2','header':'md2gba_profile_predecode_tokens.h','function':'md2gba_profile_predecode_token','with_bus':False},
        '0x041BF6':{'next':'0x041BF6','header':'md2gba_profile_predecode_kos.h','function':'md2gba_profile_predecode_kos','with_bus':True},
      }
    }
    (a.out/'combined_hooks.json').write_text(json.dumps(hooks,indent=2))
    print(f'assets={len(assets)} records={total} shortcuttable={fast} slow={total-fast} output_bytes={len(data)}')
if __name__=='__main__':main()
