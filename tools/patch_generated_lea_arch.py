#!/usr/bin/env python3
"""Repair pre-generated 68k AOT LEA assignments after architectural-EA correction.

Older MD2GBA generated pages materialized LEA results in the 24-bit bus domain.
LEA writes an architectural address register value; bus masking belongs at memory
access time. This tool performs the minimal semantic repair in already-generated
page sources without regenerating them through newer unrelated optimizer passes.

Usage:
  patch_generated_lea_arch.py ROM PCS_JSON GRAPH_DIR [GRAPH_DIR ...]

The tool is deterministic and idempotent. It patches every duplicate copy of a
LEA semantic inside a page (ordinary continuation, exact fallback, and fast
superblock). Ambiguous old textual forms are allowed only when they are
PC-relative LEAs whose old numeric target is already the same architectural
value; those require no textual change.
"""
from __future__ import annotations
import json
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT))
from raw68k_core import decode  # type: ignore
from aot68k_codegen import addr_arch  # type: ignore

EA_KIND={
'dn':'MD68K_EA_DN','an':'MD68K_EA_AN','an_ind':'MD68K_EA_AN_IND','an_postinc':'MD68K_EA_AN_POSTINC',
'an_predec':'MD68K_EA_AN_PREDEC','an_disp16':'MD68K_EA_AN_DISP16','an_index8':'MD68K_EA_AN_INDEX8',
'abs_w':'MD68K_EA_ABS_W','abs_l':'MD68K_EA_ABS_L','pc_disp16':'MD68K_EA_PC_DISP16','pc_index8':'MD68K_EA_PC_INDEX8','imm':'MD68K_EA_IMM'}

def eaargs(e):
    return f"{EA_KIND[e['kind']]},{e.get('reg') or 0},{(e.get('value') or 0)}u,{(e.get('disp') or 0)},{(e.get('index_ext') or 0)}u"

def absw_bus(value:int)->int:
    a=value & 0xFFFF
    if a & 0x8000: a |= 0xFF0000
    return a & 0xFFFFFF

def old_addr(e,pc:int)->str:
    k=e['kind']; r=e.get('reg') or 0; v=e.get('value') or 0; d=e.get('disp') or 0
    if k in ('an_ind','an_postinc'): return f"(c->a[{r}]&0xFFFFFFu)"
    if k=='an_disp16': return f"((c->a[{r}]+({d}))&0xFFFFFFu)"
    if k=='abs_w': return f"0x{absw_bus(v):06X}u"
    if k=='abs_l': return f"0x{v&0xFFFFFF:06X}u"
    if k=='pc_disp16': return f"0x{(pc+2+d)&0xFFFFFF:06X}u"
    return f"md68k_ea_addr(c,{eaargs(e)},MD68K_SZ_L,0x{pc:06X}u,0)"

def main()->int:
    if len(sys.argv)<4:
        print(__doc__,file=sys.stderr); return 2
    rom=Path(sys.argv[1]).read_bytes()
    raw=json.loads(Path(sys.argv[2]).read_text())
    pcs=[int(x,16) if isinstance(x,str) else int(x) for x in raw['pcs']]
    # page -> old assignment -> {(new assignment, ea kind, pc)}
    by_page=defaultdict(lambda: defaultdict(list))
    lea_count=0
    for pc in pcs:
        try: ins=decode(rom,pc)
        except Exception: continue
        if ins.get('mnemonic')!='lea': continue
        lea_count+=1
        old=f"c->a[{ins['dst_a']}]={old_addr(ins['ea'],pc)};"
        new=f"c->a[{ins['dst_a']}]={addr_arch(ins['ea'],pc)};"
        by_page[pc>>10][old].append((new,ins['ea']['kind'],pc))

    total_files=total_repls=0
    changed=[]
    for ds in map(Path,sys.argv[3:]):
        if not ds.is_dir():
            raise SystemExit(f"not a graph directory: {ds}")
        for page,mappings in by_page.items():
            p=ds/f"ps4_whole_region_page_{page:04x}.c"
            if not p.exists(): continue
            s=p.read_text()
            before=s
            page_repls=0
            for old,rows in mappings.items():
                news={r[0] for r in rows}
                if len(news)>1:
                    # The only legitimate textual collision is a PC-relative LEA:
                    # older code folded several different PC+disp expressions to
                    # the same <16 MiB numeric address. That numeric value is
                    # already the correct architectural value, so no repair is
                    # required for this collided form.
                    kinds={r[1] for r in rows}
                    if kinds=={'pc_disp16'}:
                        continue
                    detail=', '.join(f"{pc:06x}:{kind}:{new}" for new,kind,pc in rows)
                    raise SystemExit(f"ambiguous LEA repair in {p}: {old} -> {detail}")
                new=next(iter(news))
                if old==new: continue
                n=s.count(old)
                if n:
                    s=s.replace(old,new)
                    page_repls+=n
            if s!=before:
                p.write_text(s)
                total_files+=1; total_repls+=page_repls
                changed.append(str(p))
    print(f"LEA_REPAIR lea_instructions={lea_count} changed_files={total_files} replacements={total_repls}")
    for p in changed: print(f"CHANGED {p}")
    return 0

if __name__=='__main__': raise SystemExit(main())
