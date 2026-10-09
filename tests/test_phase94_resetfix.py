#!/usr/bin/env python3
"""P94 DMA-retirement regression retained by later phases.

The P94 attempt also bypassed 0x0D0004, but P95 superseded that part after
runtime evidence showed the original initializer must be preserved.
"""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
transport=(root/'md2gba_psc_m4a.c').read_text()
assert transport.count('DMA1CNT_H=0x8440u;') == 2
assert 'DMA1CNT_H=0x8400u;' not in transport
generic=(root/'gen_p96_aotfix/ps4_whole_region_page_0340.c').read_text()
assert 'c->pc=0x0D0004u; c->pc=0x0D0008u;' in generic
assert 'c->pc=0x0D0008u; c->pc=0x0D0C1Au;' in generic
print('PASS retained P94 fix: M4A retirement DMA is destination-fixed; generic sound entries unchanged')
