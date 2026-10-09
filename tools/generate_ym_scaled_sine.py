#!/usr/bin/env python3
"""Generate exact integer gain waveforms. No floating point or game assets."""
from pathlib import Path
import re,sys
root=Path(__file__).resolve().parents[1]
s=(root/'runtime/src/md2gba_ym2612_pcm.c').read_text()
sine=[int(n) for n in re.findall(r'-?\d+',re.search(r'ym_sine\[256\][^=]*=\{(.*?)\};',s,re.S).group(1))];assert len(sine)==256
text='/* Build-time sine * gain / 256. Every 8-bit gain is represented exactly. */\nstatic const int8_t ym_scaled_sine[256][256] = {\n'+''.join('{'+','.join(str((n*g)>>8)for n in sine)+'},\n'for g in range(256))+'};\n'
out=root/'runtime/generated/md2gba_ym_scaled_sine.h'
if '--check' in sys.argv:
 if out.read_text()!=text:raise SystemExit('YM table is stale')
 print('PASS all 65536 precomputed gain/sine values match the integer equation')
else:out.write_text(text)
