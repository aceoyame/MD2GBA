#!/usr/bin/env python3
"""P141: keep architectural PC in a native local on selected generated pages.
Only used on pages where ARM7TDMI codegen is demonstrably smaller/less spill-heavy.
The exact seam and every page return synchronize c->pc; exact execution reloads it.
Idempotent. Current admitted page: gen_p116 page 01ab.
"""
from pathlib import Path
import argparse,re
MARK='/* P141 PSC native-PC residency */'

def transform(s):
    if MARK in s: return s,0
    sep='\nlocal_exact:'
    if sep not in s: raise SystemExit('no local_exact seam')
    fast,tail=s.split(sep,1)
    if any(x in fast for x in ('md2gba_profile_','md68k_divu(','md68k_divs(','md68k_raise_autovector(')):
        raise SystemExit('page has PC-observing/changing helper; not admitted')
    m=re.search(r'(int\s+ps4_whole_region_page_[0-9A-Fa-f]+_pschybrid\([^\n]+\)\{\n\s*uint32_t\s+t0=0,t1=0,steps=st->steps,left=st->left,over=st->over;\s*\(void\)hooks;)',fast)
    if not m: raise SystemExit('prologue not found')
    pe=m.end(); init=' uint32_t pc=c->pc; '+MARK
    fast=fast[:pe]+init+fast[pe:]; start=pe+len(init)
    fast=fast[:start]+fast[start:].replace('c->pc','pc')
    old='#define FAST_RETURN(v) do{st->steps=steps;st->left=left;st->over=over;return (v);}while(0)'
    new='#define FAST_RETURN(v) do{c->pc=pc;st->steps=steps;st->left=left;st->over=over;return (v);}while(0)'
    if old not in fast: raise SystemExit('FAST_RETURN not found')
    fast=fast.replace(old,new,1)
    tail='\n c->pc=pc;'+tail
    pos=tail.find('int er=ps4_whole_region_page_')
    if pos<0: raise SystemExit('exact call not found')
    semi=tail.find(';',pos)
    tail=tail[:semi+1]+' pc=c->pc;'+tail[semi+1:]
    return fast+sep+tail,1

def main():
    ap=argparse.ArgumentParser(); ap.add_argument('page'); a=ap.parse_args(); p=Path(a.page)
    s=p.read_text(); o,n=transform(s)
    if n: p.write_text(o)
    print(f'P141 native-PC residency: transformed={n} page={p}')
if __name__=='__main__': main()
