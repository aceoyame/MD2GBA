#!/usr/bin/env python3
from pathlib import Path
import re,sys
store=re.compile(r'md68k_movem_store\(c,b,(0x[0-9A-Fa-f]+u),MD68K_SZ_L,MD68K_EA_AN_PREDEC,7,0u,0,0u,0x[0-9A-Fa-f]+u\)')
load=re.compile(r'md68k_movem_load\(c,b,(0x[0-9A-Fa-f]+u),MD68K_SZ_L,MD68K_EA_AN_POSTINC,7,0u,0,0u,0x[0-9A-Fa-f]+u\)')
ns=nl=0
for arg in sys.argv[1:]:
    paths=sorted(Path().glob(arg)) if any(ch in arg for ch in '*?[') else [Path(arg)]
    for p in paths:
        if not p.is_file(): continue
        s=p.read_text(); s,n1=store.subn(r'md68k_movem_store_a7_l(c,b,\1)',s); s,n2=load.subn(r'md68k_movem_load_a7_l(c,b,\1)',s)
        if n1+n2: p.write_text(s)
        ns+=n1;nl+=n2
print(f'P138 MOVEM specialized: store={ns} load={nl} total={ns+nl}')
