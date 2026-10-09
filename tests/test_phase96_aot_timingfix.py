#!/usr/bin/env python3
from pathlib import Path
import re, tempfile, subprocess
root=Path(__file__).resolve().parents[1]
gen=(root/'gen_p96_aotfix/ps4_whole_region_dispatch.c').read_text()
internal=(root/'gen_p96_aotfix/ps4_whole_region_pschybrid_internal.h').read_text()
emit=(root/'tools/emit_aot_sharded.py').read_text()
profile=(root/'profiles/ps4/m4a/generated/ps4_whole_region_page_0340.c').read_text()
main=(root/'main_p85.c').read_text()
timing=(root/'md2gba_timing.c').read_text()
# P96 scheduler ABI must carry instruction-crossing overshoot end to end.
assert 'uint32_t steps,left,pre,idle,over;' in internal
assert 'scheduled_overshoot' in gen
assert '*hooks->scheduled_overshoot=st.over' in gen
assert 'st.over=*hooks->scheduled_overshoot' in gen
assert 'hooks.scheduled_overshoot = &timing.scheduled_overshoot;' in main
assert 'uint32_t over=t->scheduled_overshoot' in timing
assert 't->scheduled_countdown=lim-over' in timing
# Unsafe fixed-WRAM idle shortcut is disabled: it could force an interrupt seam
# before the source instruction stream reached the real deadline.
needle='def _fastchain_poll_branch'
pos=emit.index(needle)
body=emit[pos:pos+1200]
assert re.search(r'return False\s+if idx <= 0:', body)
# Profile-only sound seam must preserve the same accounting while leaving the
# original one-time 0D0004 bootstrap intact.
svc=profile[profile.index('L_0D0000:'):profile.index('L_0D0004:')]
ini=profile[profile.index('L_0D0004:'):profile.index('L_0D0008:')]
assert 'over+=cyc-left' in svc
assert 'c->pc=0x0D0C1Au' in ini
# Deterministic regeneration from the corrected P96 graph.
with tempfile.TemporaryDirectory() as td:
    out=Path(td)/'p.c'
    subprocess.check_call(['python3',str(root/'tools/generate_ps4_m4a_audio_profile.py'),
                           str(root/'gen_p96_aotfix/ps4_whole_region_page_0340.c'),str(out)])
    assert out.read_bytes()==(root/'profiles/ps4/m4a/generated/ps4_whole_region_page_0340.c').read_bytes()
print('PASS P96 AOT timingfix: overshoot preserved; unsafe idle seam disabled; PSIV profile seam timing-compatible')
