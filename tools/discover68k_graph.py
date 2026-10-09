#!/usr/bin/env python3
"""Conservative title-independent 68000 static graph discovery for MD2GBA.

Seeds standard exception/vector targets, follows statically known direct flow,
and stops at computed control flow or decoder gaps instead of guessing. Runtime
profiles can later add verified computed destinations with augment_aot_profile.py.
"""
from __future__ import annotations
import argparse,json,hashlib,sys
from collections import deque
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from raw68k_core import decode


def be32(b,o):return int.from_bytes(b[o:o+4],'big')

def successors(ins):
    pc=ins['address']; nxt=(pc+ins['size'])&0xffffff; flow=ins.get('flow','fallthrough');t=ins.get('target')
    if flow=='fallthrough':return [nxt]
    if flow=='cond':return [nxt]+([] if t is None else [t&0xffffff])
    if flow=='jump':return [] if ins.get('computed_target') or t is None else [t&0xffffff]
    if flow=='call':return [nxt]+([] if ins.get('computed_target') or t is None else [t&0xffffff])
    return []

def _is_table_stub(ins):
    # Common compiler-generated 68000 switch tables are sequences of BRA.S,
    # BRA.W and sometimes RTS entries.  Resolve only these explicit stubs;
    # never interpret arbitrary data words as destination pointers.
    return ins.get('mnemonic') in ('bra','rts')


def resolve_pc_index_jump_table(rom: bytes, ins, max_entries=256):
    """Conservatively recover a PC-indexed branch-stub table.

    68000 d8(PC,Xn) uses the extension-word PC as its base.  We accept a table
    only when >=2 consecutive entries at one natural stride independently
    decode as unconditional BRA/RTS stubs.  This is title-independent and has
    no guessed destination-pointer scanning.
    """
    ea=ins.get('ea') or {}
    if ins.get('mnemonic')!='jmp' or ea.get('kind')!='pc_index8':
        return None
    disp=ea.get('disp')
    if disp is None:return None
    base=(ins['address']+2+int(disp)) & 0xffffff
    if base&1 or base>=len(rom):return None
    candidates=[]
    for stride in (2,4,8):
        rows=[]
        for i in range(max_entries):
            p=base+i*stride
            if p+1>=len(rom) or p&1:break
            try:q=decode(rom,p)
            except Exception:break
            if not _is_table_stub(q):break
            # A table entry must fit wholly within its own stride; otherwise
            # sampling extension words could masquerade as independent entries.
            if q.get('size',0)>stride:break
            t=q.get('target')
            if t is not None and ((t&1) or t>=len(rom)):break
            rows.append({'pc':p,'target':None if t is None else (t&0xffffff),'mnemonic':q['mnemonic'],'size':q['size']})
        if len(rows)>=2:candidates.append((len(rows),-stride,stride,rows))
    if not candidates:return None
    # Longest contiguous verified table wins; on ties prefer the smaller stride.
    _,_,stride,rows=max(candidates)
    return {'base':base,'stride':stride,'entries':rows}


def discover(rom:bytes,max_pcs=250000):
    seeds=[]
    if len(rom)>=8:
        reset=be32(rom,4)&0xffffff
        if reset<len(rom) and not(reset&1):seeds.append(reset)
    # Standard 68000 exception/autovector/TRAP area. Do not treat arbitrary ROM
    # words after vector 63 as code roots.
    for v in range(2,64):
        o=v*4
        if o+4>len(rom):break
        p=be32(rom,o)&0xffffff
        if p and p!=0xffffff and p<len(rom) and not(p&1):seeds.append(p)
    seeds=sorted(set(seeds)); q=deque(seeds); seen=set(); errors=[];computed=[]
    while q:
        pc=q.popleft()&0xffffff
        if pc in seen:continue
        if pc+1>=len(rom):continue
        try:ins=decode(rom,pc)
        except Exception as e:
            errors.append({'pc':f'{pc:06X}','error':str(e)});continue
        seen.add(pc)
        if len(seen)>max_pcs:raise RuntimeError(f'discovery exceeded --max-pcs={max_pcs}')
        if ins.get('computed_target'):
            rec={'pc':f'{pc:06X}','mnemonic':ins.get('mnemonic'),'ea':ins.get('ea')}
            tab=resolve_pc_index_jump_table(rom,ins)
            if tab:
                rec['resolved_kind']='pc_index_branch_stub_table'
                rec['table_base']=f"{tab['base']:06X}"
                rec['table_stride']=tab['stride']
                rec['table_entries']=len(tab['entries'])
                # Seed the verified branch destinations, not the table-stub
                # addresses themselves.  Runtime/codegen may later fuse the
                # switch, while the fallback can still execute a stub if needed.
                # This avoids treating compiler jump-table data as ordinary
                # linear code during graph discovery.
                for row in tab['entries']:
                    tp=row.get('target')
                    if tp is not None and tp not in seen:q.append(tp)
            computed.append(rec)
        for p in successors(ins):
            if p not in seen and p<len(rom) and not(p&1):q.append(p)
    resolved=sum(1 for e in computed if e.get('resolved_kind'))
    unresolved=len(computed)-resolved
    return {'rom_sha256':hashlib.sha256(rom).hexdigest(),'reset_pc':f'{(be32(rom,4)&0xffffff):06X}' if len(rom)>=8 else None,
        'seed_count':len(seeds),'seeds':[f'{p:06X}' for p in seeds],'pc_count':len(seen),
        'pcs':[f'{p:06X}' for p in sorted(seen)],'computed_edges':computed,'decode_errors':errors,
        'resolved_computed_edges':resolved,'unresolved_computed_edges':unresolved,
        'complete':unresolved==0 and not errors,
        'note':'PC-indexed branch-stub tables are resolved only when their entries decode as explicit unconditional control stubs; other computed destinations and decode gaps are intentionally not guessed. Runtime profile seeds can augment this graph.'}

def main():
    ap=argparse.ArgumentParser();ap.add_argument('rom',type=Path);ap.add_argument('-o','--output',type=Path,required=True);ap.add_argument('--report',type=Path);ap.add_argument('--max-pcs',type=int,default=250000);a=ap.parse_args()
    r=discover(a.rom.read_bytes(),a.max_pcs);a.output.write_text(json.dumps({'pcs':r['pcs']},indent=2)+'\n')
    if a.report:a.report.write_text(json.dumps({k:v for k,v in r.items() if k!='pcs'},indent=2)+'\n')
    print(json.dumps({k:v for k,v in r.items() if k not in ('pcs','seeds','computed_edges','decode_errors')}|{'computed_edges':len(r['computed_edges']),'decode_errors':len(r['decode_errors'])},indent=2))
if __name__=='__main__':main()
