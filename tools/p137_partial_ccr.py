#!/usr/bin/env python3
"""P137: remove dead N/Z/V/C work while preserving live X inside one fast superblock.

Run after conservative p131_ccr_dce.py. A canonical writer that clears NZVCX is
partially dead when the next canonical writer in the same admitted superblock
clears NZVC (but not X), with no barrier between. In that case only X can be
observed later, so retain the original result computation and X clear/set logic,
leaving old NZVC untouched until the next writer overwrites it.

No cross-superblock reasoning. Idempotent: the X-only clear is not a canonical
P131 writer on subsequent runs.
"""
from pathlib import Path
import importlib.util, re, sys
ROOT=Path(__file__).resolve().parent.parent
spec=importlib.util.spec_from_file_location('p131',ROOT/'tools/p131_ccr_dce.py')
p131=importlib.util.module_from_spec(spec); spec.loader.exec_module(p131)

XCLR='c->sr=(uint16_t)(c->sr&~MD68K_X)'
FLAGSEQ=re.compile(r'MD68K_[NZVCX](?:\|MD68K_[NZVCX])*')

def x_only(subs):
    out=[]; changed=False
    for s in subs:
        m=p131.CLEAR.match(s)
        if m:
            if not m.group(1):
                raise AssertionError('partial CCR requested for non-X writer')
            out.append(XCLR); changed=True; continue
        if p131.SETC.match(s) or p131.SETU.match(s):
            if 'MD68K_X' not in s:
                changed=True; continue
            ns=FLAGSEQ.sub('MD68K_X',s)
            out.append(ns); changed |= (ns!=s); continue
        out.append(s)
    if not changed:return None
    return '    { ' + ' '.join(s+';' for s in out) + ' }'

def process(text, stats):
    def fix(m):
        head,body,tail=m.group(1),m.group(3),m.group(4) or ''
        lines=body.rstrip('\n').split('\n'); cls=[p131.classify(l) for l in lines]; out=list(lines)
        for i,(kind,subs,x) in enumerate(cls):
            if kind!='W' or not x: continue
            for j in range(i+1,len(cls)):
                k2,_,x2=cls[j]
                if k2=='B': break
                if k2=='W':
                    if not x2:
                        nl=x_only(subs)
                        if nl is not None:
                            out[i]=nl; stats['partial']+=1
                    break
        return head+'\n'.join(out)+'\n'+tail
    return p131.SB.sub(fix,text)

def main():
    stats={'partial':0}; files=[]
    for a in sys.argv[1:]:
        p=Path(a)
        files.extend(sorted(p.glob('ps4_whole_region_page_*.c')) if p.is_dir() else [p])
    seen=set(); uniq=[]
    for p in files:
        rp=str(p.resolve())
        if rp not in seen:seen.add(rp);uniq.append(p)
    changed=0
    for p in uniq:
        s=p.read_text(); t=process(s,stats)
        if t!=s:p.write_text(t);changed+=1
    print(f'P137 partial CCR: files={len(uniq)} changed_files={changed} x_live_nzvc_dead={stats["partial"]}')
if __name__=='__main__':main()
