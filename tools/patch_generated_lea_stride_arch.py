#!/usr/bin/env python3
"""Repair old generated LEA/DBF fusions that truncated An to the 24-bit bus.

The recognized fusion updates an architectural address register with LEA d16(An),An.
Memory accesses remain bus-domain in md2gba_read/write; only the An stride update loses
its historical &0xFFFFFF mask. Deterministic and idempotent.
"""
from pathlib import Path
import re,sys
pat=re.compile(r'dbase=\(dbase\+\(uint32_t\)\(([-+]?\d+)\)\)&0xFFFFFFu;')
if len(sys.argv)<2: raise SystemExit('usage: patch_generated_lea_stride_arch.py GRAPH_DIR [...]')
total=files=0
for root in map(Path,sys.argv[1:]):
    for p in sorted(root.glob('ps4_whole_region_page_*.c')):
        s=p.read_text(); ns,n=pat.subn(r'dbase=dbase+(uint32_t)(\1);',s)
        if n:
            p.write_text(ns); total+=n; files+=1
print(f'LEA_STRIDE_ARCH changed_files={files} replacements={total}')
