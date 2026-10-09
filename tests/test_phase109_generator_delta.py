#!/usr/bin/env python3
from pathlib import Path
root=Path(__file__).resolve().parents[1]
a=root/'gen_p108_bulk'; b=root/'gen_p109'
changed=[]
for p in sorted(a.glob('ps4_whole_region_page_*.c')):
    q=b/p.name
    if q.exists() and p.read_bytes()!=q.read_bytes(): changed.append(p.name)
assert changed==['ps4_whole_region_page_0108.c','ps4_whole_region_page_016c.c'],changed
print('PASS P109 generator delta: exactly pages 0108 + 016c')
