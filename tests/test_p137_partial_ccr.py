#!/usr/bin/env python3
from pathlib import Path
import importlib.util, shutil, subprocess, tempfile, sys
root=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('dce',root/'tools/p131_ccr_dce.py');d=importlib.util.module_from_spec(spec);spec.loader.exec_module(d)
pages={p.stem:p for p in (root/'gen_p96_aotfix').glob('ps4_whole_region_page_*.c')}
for n in ['0106','0108','016c','01aa','01ab']: pages['ps4_whole_region_page_'+n]=root/f'gen_p116/ps4_whole_region_page_{n}.c'
pages['ps4_whole_region_page_0105']=root/'gen_p126/ps4_whole_region_page_0105.c'
pages['ps4_whole_region_page_0340']=root/'profiles/ps4/m4a/generated/ps4_whole_region_page_0340.c'
remaining=0
for p in pages.values():
 for m in d.SB.finditer(p.read_text()):
  cls=[d.classify(l) for l in m.group(3).rstrip('\n').split('\n')]
  for i,(k,subs,x) in enumerate(cls):
   if k!='W' or not x:continue
   for k2,_,x2 in cls[i+1:]:
    if k2=='B':break
    if k2=='W':
     remaining += (not x2); break
if remaining: raise SystemExit(f'FAIL remaining partial-CCR opportunities={remaining}')
# Verify the transformer is idempotent on representative transformed pages.
with tempfile.TemporaryDirectory() as td:
 qs=[]
 for p in list(pages.values())[:12]:
  q=Path(td)/p.name;shutil.copy2(p,q);qs.append(q)
 before=[q.read_bytes() for q in qs]
 subprocess.run([sys.executable,str(root/'tools/p137_partial_ccr.py'),*map(str,qs)],check=True,stdout=subprocess.DEVNULL)
 after=[q.read_bytes() for q in qs]
 if before!=after:raise SystemExit('FAIL partial CCR not idempotent')
print(f'PASS P137 partial CCR: active_pages={len(pages)} remaining_xlive_nzvc_dead=0 idempotent')
