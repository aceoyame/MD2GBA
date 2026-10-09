#!/usr/bin/env python3
from pathlib import Path
import shutil, subprocess, tempfile, re, sys
root=Path(__file__).resolve().parents[1]
script=root/'tools/p131_ccr_dce.py'
pages=[root/'gen_p126/ps4_whole_region_page_0105.c',root/'gen_p116/ps4_whole_region_page_0106.c',root/'gen_p116/ps4_whole_region_page_0108.c']
with tempfile.TemporaryDirectory() as td:
    td=Path(td); copies=[]
    for p in pages:
        q=td/p.name; shutil.copy2(p,q); copies.append(q)
    # Sources in a built P134Y archive may already be transformed. Verify idempotence there.
    r=subprocess.run([sys.executable,str(script),'--no-cross',*map(str,copies)],text=True,capture_output=True,check=True)
    before=[q.read_bytes() for q in copies]
    r2=subprocess.run([sys.executable,str(script),'--no-cross',*map(str,copies)],text=True,capture_output=True,check=True)
    after=[q.read_bytes() for q in copies]
    assert before==after, 'CCR DCE not idempotent'
    assert 'cross-block 0' in r.stdout and 'cross-block 0' in r2.stdout
print('PASS P134Y conservative intra-superblock CCR DCE is idempotent; cross-block disabled')
