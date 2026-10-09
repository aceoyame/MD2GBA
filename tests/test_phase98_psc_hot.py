#!/usr/bin/env python3
from pathlib import Path
root=Path(__file__).resolve().parents[1]
chip=(root/'profiles/ps4/m4a/src/chip_audio.cpp').read_text()
bridge=(root/'profiles/ps4/m4a/src/ps4_m4a_bridge.cpp').read_text()
transport=(root/'md2gba_psc_m4a.c').read_text()
profile=(root/'profiles/ps4/m4a/generated/ps4_whole_region_page_0340.c').read_text()
assert 'MD2GBA_PS4_M4A_SEQ_ONLY' in chip
assert 'render_fm_state' in chip and 'fm_slot[max_lanes]' in chip
assert 'psg_amplitude[max_lanes]' in chip
assert 'section(".iwram.audio")' in chip
assert 'section(".iwram.audio")' in bridge
# P94/P95/P96 correctness work must stay present.
assert transport.count('DMA1CNT_H=0x8440u;') == 2
svc=profile[profile.index('L_0D0000:'):profile.index('L_0D0004:')]
ini=profile[profile.index('L_0D0004:'):profile.index('L_0D0008:')]
assert 'over+=cyc-left' in svc
assert 'c->pc=0x0D0C1Au' in ini
print('PASS P98 PSC-hot source: cached per-frame voices, IWRAM mixer+SFX bridge, seq-only split, P94-P96 fixes retained')
