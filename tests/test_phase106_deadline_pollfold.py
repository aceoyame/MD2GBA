#!/usr/bin/env python3
from pathlib import Path
import re
root=Path(__file__).resolve().parents[1]
emit=(root/'tools/emit_aot_sharded.py').read_text()
page=(root/'gen_p106_pollfold/ps4_whole_region_page_0108.c').read_text()
# P96's retired early-seam shortcut must stay disabled.
pos=emit.index('def _fastchain_poll_branch')
assert re.search(r'return False\s+if idx <= 0:', emit[pos:pos+1200])
# P106 must use a separate deadline-local fold and must not force left to zero.
assert 'def _deadline_poll_pair' in emit
assert re.search(r'never advances a\s+hardware deadline', emit)
blk=page[page.index('L_042076:'):page.index('L_04207A:')]
assert 'P106 deadline-local fixed-WRAM poll folding' in blk
assert 'FAST_RETURN(MD2GBA_AOT_PAGE_EXIT)' in blk
assert 'left=0u' not in blk.split('if(left>20u){',1)[1].split('if(left>20u){',1)[0]
assert 'md2gba_wram_read8_offset(b,0xEF12u)' in blk

# Exhaustively compare the logical TST.B abs.w + taken/not-taken BNE loop state
# at the next scheduler seam for every possible scanline countdown.
# P96 semantics: an instruction that reaches/crosses left records overshoot and
# triggers the scheduler at that exact post-instruction PC.
TST=10; BTAKEN=10; BNOT=8
POLL=0x42076; BR=0x4207A; EXIT=0x4207C

def spend(left,cyc):
    if left <= cyc:
        return 0,cyc-left,True
    return left-cyc,0,False

def ref(left,nonzero):
    steps=0; over=0; pc=POLL
    while True:
        left,o,seam=spend(left,TST); over+=o; steps+=1; pc=BR
        if seam:return left,over,pc,steps
        if nonzero:
            left,o,seam=spend(left,BTAKEN); over+=o; steps+=1; pc=POLL
            if seam:return left,over,pc,steps
        else:
            left,o,seam=spend(left,BNOT); over+=o; steps+=1; pc=EXIT
            return left,over,pc,steps

def fold_batch(left):
    for n in (16,8,4,2,1):
        c=n*(TST+BTAKEN)
        if left>c:return n
    return 0

def opt(left,nonzero):
    steps=0; over=0; pc=POLL
    while True:
        if nonzero and left>(TST+BTAKEN):
            n=fold_batch(left)
            if n:
                left-=n*(TST+BTAKEN); steps+=n*2; pc=POLL
                continue
        # Existing exact/final path.
        left,o,seam=spend(left,TST); over+=o; steps+=1; pc=BR
        if seam:return left,over,pc,steps
        if nonzero:
            left,o,seam=spend(left,BTAKEN); over+=o; steps+=1; pc=POLL
            if seam:return left,over,pc,steps
        else:
            left,o,seam=spend(left,BNOT); over+=o; steps+=1; pc=EXIT
            return left,over,pc,steps

for left in range(1,489):
    for nz in (False,True):
        a=ref(left,nz); b=opt(left,nz)
        assert a==b,(left,nz,a,b)
print('PASS P106 deadline-local poll fold matches exact P96 seam for countdowns 1..488 and both branch outcomes')
