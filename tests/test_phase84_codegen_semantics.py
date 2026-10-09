#!/usr/bin/env python3
from pathlib import Path
import random
N=0x08; Z=0x04; V=0x02; C=0x01; X=0x10

def arith_ref(bits,dst,src,sub,setx,sr):
    m=(1<<bits)-1; sg=1<<(bits-1); dst&=m; src&=m
    r=(dst-src)&m if sub else (dst+src)&m
    clear=N|Z|V|C|(X if setx else 0); sr &= ~clear
    if r==0: sr|=Z
    if r&sg: sr|=N
    if sub:
        if ((dst^src)&(dst^r)&sg): sr|=V
        carry=src>dst
    else:
        if ((~(dst^src)&(dst^r)&sg)): sr|=V
        carry=src>(m-dst)
    if carry: sr|=C|(X if setx else 0)
    return r,sr&0xffff

def arith_p84(bits,dst,src,sub,setx,sr):
    # Mirrors the width-specialized expression emitted by arith_inline().
    m=(1<<bits)-1; sg=1<<(bits-1); d=dst&m; q=src&m
    r=(d-q)&m if sub else (d+q)&m
    sr &= ~(N|Z|V|C|(X if setx else 0))
    if not r: sr|=Z
    if r&sg: sr|=N
    if sub:
        if ((d^q)&(d^r)&sg): sr|=V
        carry=q>d
    else:
        if ((~(d^q)&(d^r)&sg)): sr|=V
        carry=q>(m-d)
    if carry: sr|=C|(X if setx else 0)
    return r,sr&0xffff

def shift_ref(bits,v,kind,left,n,sr):
    m=(1<<bits)-1; sg=1<<(bits-1); v &= m; oldx=bool(sr&X); carry=0; ov=0
    for _ in range(n):
        if kind=='ls':
            if left: carry=bool(v&sg); v=(v<<1)&m
            else: carry=v&1; v>>=1
        elif kind=='as':
            if left:
                os=bool(v&sg); carry=os; v=(v<<1)&m; ov |= bool(v&sg)!=os
            else: carry=v&1; v=((v>>1)|(sg if v&sg else 0))&m
        elif kind=='ro':
            if left: carry=bool(v&sg); v=((v<<1)&m)|(1 if carry else 0)
            else: carry=v&1; v=(v>>1)|(sg if carry else 0)
        else: raise AssertionError(kind)
    sr &= ~(N|Z|V|C)
    if kind in ('as','ls'): sr &= ~X
    if not v: sr|=Z
    if v&sg: sr|=N
    if ov: sr|=V
    if carry: sr|=C
    if kind in ('as','ls') and carry: sr|=X
    return v,sr&0xffff

def shift_p84(bits,v,kind,left,n,sr):
    m=(1<<bits)-1; sg=1<<(bits-1); t0=v&m
    if kind=='ls':
        if left: t1=(t0<<n)&m; carry=(t0>>(bits-n))&1
        else: t1=t0>>n; carry=(t0>>(n-1))&1
        ov=False; clearx=True
    elif kind=='as':
        if left:
            t1=(t0<<n)&m; carry=(t0>>(bits-n))&1
            if n==bits: ov=t0!=0
            else:
                topmask=((1<<(n+1))-1)<<(bits-(n+1))
                ov=(t0&topmask) not in (0,topmask)
        else:
            fill=m^(m>>n); t1=(t0>>n)|(fill if t0&sg else 0); carry=(t0>>(n-1))&1; ov=False
        clearx=True
    elif kind=='ro':
        if n==bits: t1=t0
        elif left: t1=((t0<<n)|(t0>>(bits-n)))&m
        else: t1=((t0>>n)|(t0<<(bits-n)))&m
        carry=((t0>>(bits-n))&1) if left else ((t0>>(n-1))&1); ov=False; clearx=False
    else: raise AssertionError(kind)
    sr &= ~(N|Z|V|C|(X if clearx else 0))
    if not t1: sr|=Z
    if t1&sg: sr|=N
    if ov: sr|=V
    if carry: sr|=C
    if clearx and carry: sr|=X
    return t1&m,sr&0xffff

# Exhaustive byte arithmetic, both X-preserving CMP-style and X-setting ADD/SUB.
for d in range(256):
    for s in range(256):
        for sub in (False,True):
            for setx in (False,True):
                sr=((d^s)&0x1f)|0x2000
                assert arith_ref(8,d,s,sub,setx,sr)==arith_p84(8,d,s,sub,setx,sr)
# Random wider arithmetic.
r=random.Random(0x84A0)
for bits in (16,32):
    for _ in range(100000):
        d=r.getrandbits(32); s=r.getrandbits(32); sr=r.getrandbits(16)
        sub=bool(r.getrandbits(1)); setx=bool(r.getrandbits(1))
        assert arith_ref(bits,d,s,sub,setx,sr)==arith_p84(bits,d,s,sub,setx,sr)
# Immediate shift specialization. Byte is exhaustive; wider operands randomized.
for kind in ('as','ls','ro'):
  for left in (False,True):
    for n in range(1,9):
      for v in range(256):
        for sr in (0, X, 0x201f):
          assert shift_ref(8,v,kind,left,n,sr)==shift_p84(8,v,kind,left,n,sr)
for bits in (16,32):
  for _ in range(100000):
    kind=r.choice(('as','ls','ro')); left=bool(r.getrandbits(1)); n=r.randint(1,8); v=r.getrandbits(bits); sr=r.getrandbits(16)
    assert shift_ref(bits,v,kind,left,n,sr)==shift_p84(bits,v,kind,left,n,sr)
# Whole generated graph gate.
gen=Path('gen_p85')
text=''.join(p.read_text(errors='ignore') for p in gen.glob('ps4_whole_region_page_*.c'))
counts={k:text.count(k) for k in ('md68k_shift(c','md68k_add(c','md68k_sub(c')}
assert counts=={'md68k_shift(c':67,'md68k_add(c':0,'md68k_sub(c':2},counts
pages=len(list(gen.glob('ps4_whole_region_page_*.c')))
assert pages==410,pages
print('PASS P84 arithmetic/shift equivalence + generated helper counts:',counts,'pages=',pages)
