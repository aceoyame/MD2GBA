#!/usr/bin/env bash
set -euo pipefail
ROOT=/mnt/data/p137_work
BASE=/mnt/data/p136a_cmp
ROM=$ROOT/inputs/ps4built.bin
OUT=$ROOT/build_p137_diff
mkdir -p "$OUT"
COMMON_SRC="$ROOT/md2gba_bus.c $ROOT/md2gba_aot68k.c $ROOT/md2gba_timing.c $ROOT/runtime/src/md2gba_cycles.c $ROOT/runtime/generated/md2gba_interp_opclass.c"
for spec in "$@"; do
  pg=${spec%%:*}; dir=${spec#*:}
  if [[ $dir == p116 ]]; then B=$BASE/gen_p116/ps4_whole_region_page_${pg}.c; N=$ROOT/gen_p116/ps4_whole_region_page_${pg}.c
  elif [[ $dir == p126 ]]; then B=$BASE/gen_p126/ps4_whole_region_page_${pg}.c; N=$ROOT/gen_p126/ps4_whole_region_page_${pg}.c
  else B=$BASE/gen_p96_aotfix/ps4_whole_region_page_${pg}.c; N=$ROOT/gen_p96_aotfix/ps4_whole_region_page_${pg}.c; fi
  fn=ps4_whole_region_page_${pg}_pschybrid
  pcs=$OUT/${pg}.pcs
  grep -oE '^L_[0-9A-F]{6}:' "$N" | sed -E 's/^L_([0-9A-F]{6}):/\1/' > "$pcs"
  cc -O2 -DPAGEFN=$fn -I$ROOT -I$ROOT/runtime/include -I$ROOT/runtime/generated -I$ROOT/gen_p96_aotfix -I$ROOT/gen_p116 -I$ROOT/profiles/ps4/m4a/generated/rawcache_p116 -I$ROOT/profiles/ps4/m4a/generated/rawnem_p123 -I$ROOT/profiles/ps4/m4a/generated/nemnative_p126 \
    $ROOT/tests/test_p131_ccr_diff.c "$B" $COMMON_SRC -o $OUT/base_${pg}
  cc -O2 -DPAGEFN=$fn -I$ROOT -I$ROOT/runtime/include -I$ROOT/runtime/generated -I$ROOT/gen_p96_aotfix -I$ROOT/gen_p116 -I$ROOT/profiles/ps4/m4a/generated/rawcache_p116 -I$ROOT/profiles/ps4/m4a/generated/rawnem_p123 -I$ROOT/profiles/ps4/m4a/generated/nemnative_p126 \
    $ROOT/tests/test_p131_ccr_diff.c "$N" $COMMON_SRC -o $OUT/new_${pg}
  $OUT/base_${pg} "$ROM" "$pcs" 1000 $((16#$pg + 137)) > $OUT/base_${pg}.txt
  $OUT/new_${pg} "$ROM" "$pcs" 1000 $((16#$pg + 137)) > $OUT/new_${pg}.txt
  diff -q $OUT/base_${pg}.txt $OUT/new_${pg}.txt >/dev/null
  echo "PASS page $pg trials=1000 pcs=$(wc -l < "$pcs")"
done
