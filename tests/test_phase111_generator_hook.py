#!/usr/bin/env python3
from pathlib import Path
import json, subprocess, tempfile, sys
ROOT=Path(__file__).resolve().parents[1]
rom=ROOT/'ps4built.bin'; pcs=ROOT/'ps4_pcs.json'; tool=ROOT/'tools/emit_aot_sharded.py'; hooks=ROOT/'profiles/ps4/m4a/generated/predecode/token_hooks.json'
with tempfile.TemporaryDirectory() as td:
    td=Path(td); a=td/'p110'; b=td/'p111'
    base=[sys.executable,str(tool),str(rom),str(pcs),'--symbol','ps4_whole_region','--page-shift','10','--dispatch-mode','pschybrid','--iwram-hot-pages','0x105']
    subprocess.run(base+['-o',str(a)],check=True,stdout=subprocess.DEVNULL)
    subprocess.run(base+['-o',str(b),'--profile-token-hooks',str(hooks)],check=True,stdout=subprocess.DEVNULL)
    changed=[]
    for p in sorted(b.glob('ps4_whole_region_page_*.c')):
        if (a/p.name).read_bytes()!=p.read_bytes(): changed.append(p.name)
    assert changed==['ps4_whole_region_page_0105.c'],changed
    assert (a/'ps4_whole_region_dispatch.c').read_bytes()==(b/'ps4_whole_region_dispatch.c').read_bytes()
    txt=(b/'ps4_whole_region_page_0105.c').read_text()
    assert txt.count('md2gba_profile_predecode_token(c,left,&pcost,&psteps)')==1
    assert 'c->pc=0x0417C2u;goto L_0417C2;' in txt
    m=json.loads((b/'ps4_whole_region_manifest.json').read_text())
    assert m['profile_token_hook_pcs']==['04178A'],m.get('profile_token_hook_pcs')
print('PASS P111 generated token hook: only page 0105 changes; dispatcher/default policy unchanged')
