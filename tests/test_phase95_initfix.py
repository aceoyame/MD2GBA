#!/usr/bin/env python3
from pathlib import Path
import tempfile, subprocess
root=Path(__file__).resolve().parents[1]
graph=root/'gen_p96_aotfix/ps4_whole_region_page_0340.c'
gen=root/'profiles/ps4/m4a/generated/ps4_whole_region_page_0340.c'
text=gen.read_text()
assert 'L_0D0000:' in text and 'L_0D0004:' in text
service=text[text.index('L_0D0000:'):text.index('L_0D0004:')]
init=text[text.index('L_0D0004:'):text.index('L_0D0008:')]
# Only periodic UpdateSound is replaced by native PSC/M4A playback.
assert 'uint32_t ret=md2gba_read32(b,c->a[7])' in service
assert 'c->pc=0x0D0008u' not in service
assert 'case 0x0u: { uint32_t pre_pc=0x0D0000u; uint32_t ret=md2gba_read32' in text
assert 'over+=cyc-left' in service
assert 'st->over+=cyc-st->left' in text
# One-time original bootstrap remains. It branches into 0x0D0C1A, where the
# source graph clears/initializes the sound work area and performs driver setup.
assert 'c->pc=0x0D0C1Au' in init
assert 'case 0x2u: { uint32_t pre_pc=0x0D0004u; c->pc=0x0D0008u; c->pc=0x0D0C1Au;' in text
# Generic graph is unchanged and still contains both original entries.
generic=graph.read_text()
assert 'c->pc=0x0D0004u; c->pc=0x0D0008u;' in generic
assert 'c->pc=0x0D0008u; c->pc=0x0D0C1Au;' in generic
# Keep P94's proven M4A DMA retirement correction.
transport=(root/'md2gba_psc_m4a.c').read_text()
assert transport.count('DMA1CNT_H=0x8440u;') == 2
assert 'DMA1CNT_H=0x8400u;' not in transport
# Regeneration is deterministic.
with tempfile.TemporaryDirectory() as td:
    out=Path(td)/'p.c'
    subprocess.check_call(['python3',str(root/'tools/generate_ps4_m4a_audio_profile.py'),str(graph),str(out)])
    assert out.read_bytes()==gen.read_bytes()
print('PASS P95 initfix: periodic service native-bypassed; original one-time sound initializer preserved; DMA retirement fixed')
