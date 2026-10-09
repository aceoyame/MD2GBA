#!/usr/bin/env python3
from pathlib import Path
import json, subprocess, tempfile, sys
ROOT=Path(__file__).resolve().parents[1]
rom=ROOT/'ps4built.bin'; pcs=ROOT/'ps4_pcs.json'; tool=ROOT/'tools/emit_aot_sharded.py'
with tempfile.TemporaryDirectory() as td:
    td=Path(td); a=td/'a'; b=td/'b'
    base=[sys.executable,str(tool),str(rom),str(pcs),'--symbol','ps4_whole_region','--page-shift','10','--dispatch-mode','pschybrid']
    subprocess.run(base+['-o',str(a)],check=True,stdout=subprocess.DEVNULL)
    subprocess.run(base+['-o',str(b),'--iwram-hot-pages','0x105'],check=True,stdout=subprocess.DEVNULL)
    changed=[]
    for p in b.glob('ps4_whole_region_page_*.c'):
        q=a/p.name
        if q.read_bytes()!=p.read_bytes(): changed.append(p.name)
    assert changed==['ps4_whole_region_page_0105.c'],changed
    txt=(b/'ps4_whole_region_page_0105.c').read_text()
    assert txt.count('section(".iwram.aot_hotpage")')==1
    assert (a/'ps4_whole_region_dispatch.c').read_bytes()==(b/'ps4_whole_region_dispatch.c').read_bytes()
    m=json.loads((b/'ps4_whole_region_manifest.json').read_text())
    assert m['iwram_hot_pages']==['105'],m.get('iwram_hot_pages')
print('PASS P110 profile-guided IWRAM page placement: only requested hot page changes')
