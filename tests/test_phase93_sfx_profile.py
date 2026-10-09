#!/usr/bin/env python3
from pathlib import Path
import re, sys
root=Path(__file__).resolve().parents[1]
h=(root/'profiles/ps4/m4a/generated/ps4_sfx_bank_13379.h').read_text()
bank=(root/'profiles/ps4/m4a/generated/ps4_sfx_bank_13379.pcm').read_bytes()
rows=[]
for off,length,flags,ident in re.findall(r'\{(\d+)u,(\d+)u,(\d+)u\}, /\* \$([0-9A-F]{2})',h):
    rows.append((int(ident,16),int(off),int(length),int(flags)))
assert len(rows)==70, len(rows)
assert rows[0][0]==0xB5 and rows[-1][0]==0xFA
assert len(bank)==798050, len(bank)
end=0
for i,(ident,off,length,flags) in enumerate(rows):
    assert ident==0xB5+i
    assert off==end,(hex(ident),off,end)
    assert length>0
    assert off+length<=len(bank)
    end=off+length
assert end==len(bank),(end,len(bank))
silent=[i for i,_,_,f in rows if f&2]
loops=[i for i,_,_,f in rows if f&1]
assert silent==[0xF6,0xF7],silent
assert loops==[0xF8,0xF9,0xFA],loops
# Every non-silent effect must contain at least one non-zero sample.
for ident,off,length,flags in rows:
    if flags&2: continue
    assert any(bank[off:off+length]),hex(ident)
print('PASS P93 PSC/M4A SFX profile: 70 IDs, exact bank coverage, F6/F7 silent, F8-FA looping')
