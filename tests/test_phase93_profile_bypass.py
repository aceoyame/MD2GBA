#!/usr/bin/env python3
"""Current PSIV native-audio profile seam regression.

Phase 93 originally bypassed 0x0D0004. Runtime testing later showed that the
one-time initializer must remain. The current invariant is therefore that only
0x0D0000 recurring UpdateSound is replaced while generation stays deterministic.
"""
from pathlib import Path
import tempfile, subprocess
root=Path(__file__).resolve().parents[1]
src=root/'gen_p96_aotfix/ps4_whole_region_page_0340.c'
gen=root/'profiles/ps4/m4a/generated/ps4_whole_region_page_0340.c'
text=gen.read_text()
assert 'L_0D0000:' in text and 'L_0D0004:' in text
service=text[text.index('L_0D0000:'):text.index('L_0D0004:')]
init=text[text.index('L_0D0004:'):text.index('L_0D0008:')]
assert 'uint32_t ret=md2gba_read32(b,c->a[7])' in service
assert 'c->pc=0x0D0C1Au' in init
assert 'case 0x0u: { uint32_t pre_pc=0x0D0000u; uint32_t ret=md2gba_read32' in text
assert 'over+=cyc-left' in service
assert 'st->over+=cyc-st->left' in text
assert 'case 0x2u: { uint32_t pre_pc=0x0D0004u; c->pc=0x0D0008u; c->pc=0x0D0C1Au;' in text
orig=src.read_bytes()
with tempfile.TemporaryDirectory() as td:
    out=Path(td)/'p.c'
    subprocess.check_call(['python3',str(root/'tools/generate_ps4_m4a_audio_profile.py'),str(src),str(out)])
    assert out.read_bytes()==gen.read_bytes()
assert src.read_bytes()==orig
print('PASS current native-audio profile: 0D0000 recurring service bypassed; 0D0004 initializer preserved; generic graph unchanged')
