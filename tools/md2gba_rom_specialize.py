#!/usr/bin/env python3
"""Build a title-independent MD2GBA offline-specialization sidecar.

The input is a Genesis ROM plus a verified/discovered set of instruction PCs.
Every candidate is recognized from decoded 68000 structure.  No title, game ID,
or hard-coded guest PC participates in selection.  Each record carries a hash of
the exact guest instruction span so stale ROM revisions or hacks cannot silently
reuse an optimization.
"""
from __future__ import annotations
import argparse, hashlib, json, sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from raw68k_core import decode
from aot68k_loop_fusion import (
    expand_bits_loop,counted_store_loop,counted_copy_loop,counted_compare_loop,
    nibble_accumulate_loop,strided_move_loop,strided_clear_loop,strided_pair_store_loop,
)

FAMILIES=[
 ('bitmap_expand',expand_bits_loop),('counted_store',counted_store_loop),
 ('counted_copy',counted_copy_loop),('cmpm_dbne_scan',counted_compare_loop),
 ('nibble_accumulate',nibble_accumulate_loop),('strided_move',strided_move_loop),
 ('strided_clear',strided_clear_loop),('strided_pair_store',strided_pair_store_loop),
]

def load_pcs(path:Path):
    x=json.loads(path.read_text()); vals=x.get('pcs',x) if isinstance(x,dict) else x
    out=[]
    for q in vals: out.append(int(q,16) if isinstance(q,str) else int(q))
    return sorted(set(x&0xffffff for x in out))

def safe_meta(m):
    return {k:v for k,v in m.items() if isinstance(v,(int,str,bool,type(None)))}

def plan(rom:bytes,pcs:list[int]):
    decoded=[]; errors=[]
    for pc in pcs:
        try: decoded.append(decode(rom,pc))
        except Exception as e: errors.append({'pc':f'{pc:06X}','error':str(e)})
    # Preserve address ordering; recognizers also require exact adjacency.
    decoded.sort(key=lambda x:x['address'])
    sites=[];counts={name:0 for name,_ in FAMILIES}
    for i in range(len(decoded)):
        for name,fn in FAMILIES:
            m=fn(decoded,i)
            if not m: continue
            start=int(m.get('start',decoded[i]['address']))&0xffffff
            end=int(m.get('next',start))&0xffffff
            if end<=start or end>len(rom): continue
            raw=rom[start:end]
            sites.append({'family':name,'start':f'{start:06X}','end':f'{end:06X}',
                'bytes':len(raw),'guest_sha256':hashlib.sha256(raw).hexdigest(),
                'shape':safe_meta(m)})
            counts[name]+=1
    return {'rom_sha256':hashlib.sha256(rom).hexdigest(),'rom_size':len(rom),
        'pc_count':len(pcs),'decoded_count':len(decoded),'decode_errors':errors,
        'counts':counts,'total_sites':sum(counts.values()),'sites':sites,
        'contract':'Apply a site only after the original guest span hash matches. Hardware/bus admission remains a runtime semantic check.'}

def main():
    ap=argparse.ArgumentParser();ap.add_argument('rom',type=Path);ap.add_argument('addresses',type=Path)
    ap.add_argument('-o','--output',type=Path,required=True);a=ap.parse_args()
    r=plan(a.rom.read_bytes(),load_pcs(a.addresses));a.output.write_text(json.dumps(r,indent=2)+'\n')
    print(json.dumps({'pc_count':r['pc_count'],'decoded_count':r['decoded_count'],'counts':r['counts'],'total_sites':r['total_sites'],'decode_errors':len(r['decode_errors'])},indent=2))
if __name__=='__main__':main()
