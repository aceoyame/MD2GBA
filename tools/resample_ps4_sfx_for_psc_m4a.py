#!/usr/bin/env python3
from __future__ import annotations
import argparse,json
from pathlib import Path

def lerp_resample(src: bytes, src_rate:int, dst_rate:int)->bytes:
    if not src: return b''
    n=max(1,(len(src)*dst_rate + src_rate//2)//src_rate)
    out=bytearray(n)
    # signed 8-bit linear interpolation; offline cost only.
    for i in range(n):
        pos_num=i*src_rate
        j=pos_num//dst_rate
        frac=pos_num - j*dst_rate
        if j>=len(src)-1:
            v=src[-1]
            sv=v-256 if v>=128 else v
        else:
            a=src[j]; b=src[j+1]
            a=a-256 if a>=128 else a; b=b-256 if b>=128 else b
            sv=(a*(dst_rate-frac)+b*frac + dst_rate//2)//dst_rate
        if sv< -128: sv=-128
        if sv>127: sv=127
        out[i]=sv & 0xff
    return bytes(out)

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--manifest',required=True); ap.add_argument('--pcm',required=True)
    ap.add_argument('--out-pcm',required=True); ap.add_argument('--out-header',required=True)
    ap.add_argument('--rate',type=int,default=13379)
    a=ap.parse_args(); m=json.load(open(a.manifest)); src=Path(a.pcm).read_bytes(); sr=int(m['sample_rate_hz'])
    bank=bytearray(); desc=[]
    for r in m['records']:
        q=src[int(r['offset']):int(r['offset'])+int(r['length'])]
        d=lerp_resample(q,sr,a.rate)
        off=len(bank); bank.extend(d)
        flags=(1 if r.get('special_loop') else 0)|(2 if r.get('expected_silent') else 0)
        desc.append((r['id'],r['name'],off,len(d),flags))
    Path(a.out_pcm).write_bytes(bank)
    h=['#ifndef MD2GBA_PS4_M4A_SFX_BANK_H','#define MD2GBA_PS4_M4A_SFX_BANK_H','#include <stdint.h>',
       '#define MD2GBA_PS4_SFX_FIRST_ID 0xB5u','#define MD2GBA_PS4_SFX_LAST_ID 0xFAu',f'#define MD2GBA_PS4_SFX_BANK_BYTES {len(bank)}u',
       'typedef struct { uint32_t offset,length; uint8_t flags; } Md2GbaPs4SfxDesc;',
       '#define MD2GBA_PS4_SFX_SPECIAL_LOOP 1u','#define MD2GBA_PS4_SFX_SILENT 2u',
       'static const Md2GbaPs4SfxDesc md2gba_ps4_sfx_descs[70] = {']
    for ident,name,off,n,flags in desc: h.append(f'  {{{off}u,{n}u,{flags}u}}, /* ${ident:02X} {name} */')
    h += ['};','#endif','']
    Path(a.out_header).write_text('\n'.join(h))
    print(f'{len(src)} bytes @ {sr} -> {len(bank)} bytes @ {a.rate}; {len(desc)} effects')
if __name__=='__main__': main()
