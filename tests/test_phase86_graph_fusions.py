#!/usr/bin/env python3
"""Static whole-graph gate for address-independent Phase 86 fusion coverage."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
gen=root/'gen_p86'
pages=list(gen.glob('ps4_whole_region_page_*.c'))
assert len(pages)==410,len(pages)
text=''.join(p.read_text(errors='strict') for p in pages)
expected={
    'bitmap':('guarded register-only bit -> nibble expansion',2),
    'store':('same ordered bus writes, amortized loop/CCR bookkeeping',56),
    'copy':('ordered postincrement copy/stream loop fusion',77),
    'compare':('bounded CMPM/DBNE equal-prefix scan fusion',1),
    'nibble':('fast-arm nibble accumulator; never crosses its slow seam',1),
}
got={k:text.count(needle) for k,(needle,_) in expected.items()}
want={k:n for k,(_,n) in expected.items()}
assert got==want,(got,want)
print('PASS P86 whole graph: pages=410 fusions=',got)
