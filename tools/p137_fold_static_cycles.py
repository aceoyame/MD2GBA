#!/usr/bin/env python3
"""Fold exact-entry 68000 cycle estimates into already-generated page C.

Only rewrites exact cases carrying a literal ``pre_pc`` and the canonical
``md68k_estimate_cycles(b,pre_pc,c->pc)`` call. Dynamic fallback paths retain
runtime estimation. The rule source is emit_aot_sharded._fastchain_cycle_expr,
which is exhaustively checked against runtime md68k_estimate_cycles by the
P137 regression.
"""
from __future__ import annotations
from pathlib import Path
import argparse, re, sys

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT))
from raw68k_core import decode
from emit_aot_sharded import _fastchain_cycle_expr

CALL = re.compile(
    r'uint32_t pre_pc=0x(?P<pc>[0-9A-Fa-f]+)u;(?P<body>.*?)'
    r'uint32_t cyc=md68k_estimate_cycles\(b,pre_pc,c->pc\);'
)

def patch_file(path: Path, rom: bytes) -> tuple[int,int]:
    src = path.read_text()
    seen = changed = 0
    def repl(m: re.Match[str]) -> str:
        nonlocal seen, changed
        pc = int(m.group('pc'), 16)
        seen += 1
        try:
            ins = decode(rom, pc)
        except Exception:
            return m.group(0)
        nxt = (pc + int(ins['size'])) & 0xFFFFFF
        cyc = _fastchain_cycle_expr(ins, nxt)
        changed += 1
        return (f'uint32_t pre_pc=0x{pc:06X}u;' + m.group('body') +
                f'uint32_t cyc={cyc};')
    out = CALL.sub(repl, src)
    if out != src:
        path.write_text(out)
    return seen, changed

def main() -> int:
    ap=argparse.ArgumentParser()
    ap.add_argument('rom')
    ap.add_argument('paths', nargs='+')
    ns=ap.parse_args()
    rom=Path(ns.rom).read_bytes()
    seen=changed=0
    files=[]
    for arg in ns.paths:
        p=Path(arg)
        if p.is_dir(): files += sorted(p.glob('ps4_whole_region_page_*.c'))
        else: files.append(p)
    # De-duplicate explicit paths that overlap a directory argument.
    uniq=[]; used=set()
    for p in files:
        rp=str(p.resolve())
        if rp not in used:
            used.add(rp); uniq.append(p)
    for p in uniq:
        a,b=patch_file(p,rom); seen+=a; changed+=b
    print(f'P137 static exact-cycle folding: files={len(uniq)} exact_calls_seen={seen} folded={changed}')
    return 0
if __name__=='__main__': raise SystemExit(main())
