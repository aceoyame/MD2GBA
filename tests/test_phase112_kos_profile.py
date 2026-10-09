#!/usr/bin/env python3
import csv,re
from pathlib import Path
R=list(csv.DictReader(open('analysis/p112_kos_records_full_left.csv')))
assert len(R)==8309
assets={r['asset'] for r in R};assert assets=={'2f114e','2f1e6e','2f2a9e','2f3aae'},assets
intr=sum(int(r['irq']) for r in R);term=sum(int(r['terminal']) for r in R)
fast=[r for r in R if not int(r['irq']) and not int(r['terminal']) and 0<int(r['cycles'])<488]
admit=[r for r in fast if int(r['left'])>int(r['cycles'])]
assert intr==14 and term==4 and len(fast)==7868
assert len(admit)==5055
assert sum(int(r['steps']) for r in admit)==94316
assert sum(int(r['cycles']) for r in admit)==706412
C=Path('profiles/ps4/m4a/generated/predecode_p112/md2gba_profile_predecode_kos.c').read_text()
assert C.count('static const KosRec kos_')==4
H=Path('profiles/ps4/m4a/generated/predecode_p112/combined_hooks.json').read_text()
assert '0x041BF6' in H and 'with_bus' in H
print(f'PASS P112 Kos profile: {len(R)} commands, {len(fast)} bounded, {len(admit)} phase-admitted; {sum(int(r["steps"]) for r in admit)} translated instructions elided')
