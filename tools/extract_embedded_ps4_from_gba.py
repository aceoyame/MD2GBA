#!/usr/bin/env python3
from pathlib import Path
import hashlib, sys
EXPECT='511f35cc11f88316f8b8940e28ab298bd75a4da193672a80172884d6eb913b6a'
if len(sys.argv)<2:
    raise SystemExit('usage: extract_embedded_ps4_from_gba.py P126.gba [output.bin]')
p=Path(sys.argv[1]).read_bytes(); marker=p.find(b'SEGA GENESIS')
if marker<0x100: raise SystemExit('Genesis header not found')
start=marker-0x100; rom=p[start:start+0x300000]
if len(rom)!=0x300000: raise SystemExit('embedded ROM truncated')
h=hashlib.sha256(rom).hexdigest()
if h!=EXPECT: raise SystemExit(f'wrong embedded ROM SHA-256: {h}')
out=Path(sys.argv[2] if len(sys.argv)>2 else 'inputs/ps4built.bin'); out.parent.mkdir(parents=True,exist_ok=True); out.write_bytes(rom)
print(f'extracted {len(rom)} bytes @ GBA offset 0x{start:X} -> {out}')
print(f'sha256 {h}')
