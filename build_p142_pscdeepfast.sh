#!/usr/bin/env bash
set -euo pipefail
ROM="${1:-inputs/ps4built.bin}"; ROM="$(realpath "$ROM")"
EXPECT=511f35cc11f88316f8b8940e28ab298bd75a4da193672a80172884d6eb913b6a
[[ "$(sha256sum "$ROM"|awk '{print $1}')" == "$EXPECT" ]] || { echo wrong ROM >&2; exit 3; }
mkdir -p build_p134/base build_p134/cold_pages out_p134
python3 generate_raw_nem_profile_p123.py p119_nem_commands_clone.csv -o profiles/ps4/m4a/generated/rawnem_p123 >/dev/null
# Regenerate the profile-owned page before post-passes so it cannot overwrite CCR DCE later.
python3 tools/generate_ps4_m4a_audio_profile.py gen_p96_aotfix/ps4_whole_region_page_0340.c profiles/ps4/m4a/generated/ps4_whole_region_page_0340.c >/dev/null
python3 tools/patch_generated_lea_arch.py "$ROM" ps4_pcs.json gen_p96_aotfix gen_p116 gen_p126 > out_p134/P134_LEA_GENERATED_REPAIR.txt
python3 tools/patch_generated_lea_stride_arch.py gen_p96_aotfix gen_p116 gen_p126 > out_p134/P134_LEA_STRIDE_REPAIR.txt
# P134Y: Claude-derived conservative CCR dead-code elimination.  Intra-superblock only.
# Cross-block mode is intentionally disabled: its current proof does not establish that
# the next fast block executes before an exact/scheduler seam for every remaining budget.
P134Y_PAGES=(gen_p96_aotfix/ps4_whole_region_page_*.c \
  gen_p116/ps4_whole_region_page_0106.c gen_p116/ps4_whole_region_page_0108.c \
  gen_p116/ps4_whole_region_page_016c.c gen_p116/ps4_whole_region_page_01aa.c gen_p116/ps4_whole_region_page_01ab.c \
  gen_p126/ps4_whole_region_page_0105.c profiles/ps4/m4a/generated/ps4_whole_region_page_0340.c)
python3 tools/p131_ccr_dce.py --no-cross "${P134Y_PAGES[@]}" | tee out_p134/P134Y_CCR_DCE.txt
# P137 / PSC continuation convergence: exact-entry instructions already have a
# statically decoded opcode and continuation. Fold the same cycle rule used by
# the deadline-safe fast chain instead of calling the generic estimator.
python3 tools/p137_fold_static_cycles.py "$ROM" gen_p96_aotfix \
  gen_p116/ps4_whole_region_page_0106.c gen_p116/ps4_whole_region_page_0108.c \
  gen_p116/ps4_whole_region_page_016c.c gen_p116/ps4_whole_region_page_01aa.c gen_p116/ps4_whole_region_page_01ab.c \
  gen_p126/ps4_whole_region_page_0105.c profiles/ps4/m4a/generated/ps4_whole_region_page_0340.c \
  | tee out_p134/P137_STATIC_CYCLE_FOLD.txt
# P137: bit-level CCR liveness. Preserve X when live, but drop dead N/Z/V/C
# work before the next canonical NZVC writer in the same admitted superblock.
python3 tools/p137_partial_ccr.py gen_p96_aotfix \
  gen_p116/ps4_whole_region_page_0106.c gen_p116/ps4_whole_region_page_0108.c \
  gen_p116/ps4_whole_region_page_016c.c gen_p116/ps4_whole_region_page_01aa.c gen_p116/ps4_whole_region_page_01ab.c \
  gen_p126/ps4_whole_region_page_0105.c profiles/ps4/m4a/generated/ps4_whole_region_page_0340.c \
  | tee out_p134/P137_PARTIAL_CCR.txt
# P138: specialize the dominant statically-known MOVEM.L A7 stack forms.
# This runs after all page regeneration/semantic post-passes and before compile.
python3 tools/p138_movem_specialize.py \
  'gen_p96_aotfix/ps4_whole_region_page_*.c' \
  gen_p116/ps4_whole_region_page_0106.c gen_p116/ps4_whole_region_page_0108.c \
  gen_p116/ps4_whole_region_page_016c.c gen_p116/ps4_whole_region_page_01aa.c gen_p116/ps4_whole_region_page_01ab.c \
  gen_p126/ps4_whole_region_page_0105.c profiles/ps4/m4a/generated/ps4_whole_region_page_0340.c \
  | tee out_p134/P138_MOVEM_SPECIALIZE.txt
python3 tools/p141_pc_residency.py gen_p116/ps4_whole_region_page_01ab.c | tee out_p134/P142_PC_RESIDENCY_01AB.txt
python3 tools/p141_pc_residency.py gen_p96_aotfix/ps4_whole_region_page_014d.c | tee out_p134/P142_PC_RESIDENCY_014D.txt
COMMON='-target arm-none-eabi -mcpu=arm7tdmi -fomit-frame-pointer -ffreestanding -fno-builtin -ffunction-sections -fdata-sections -fno-exceptions -fno-unwind-tables -fno-asynchronous-unwind-tables -I. -Iruntime/include -Iruntime/generated'
DEFS_BASE='-DMD2GBA_GBA_IWRAM=1 -DMD2GBA_PSC_REVERSED_ROM=1 -DMD2GBA_PSC_REVERSED_WRAM=1 -DMD2GBA_PSC_M4A_AUDIO=1 -DMD2GBA_MAXMOD_GBA_AUDIO=1 -DMD2GBA_PROJECTED_FIT=1 -DMD2GBA_PROFILE_PROJECTED_UI=1 -DMD2GBA_RELEASE=1'
DEFS_AOT="$DEFS_BASE -DMD2GBA_AOT_GENERATED_FAST=1"
INCS='-Igen_p126 -Igen_p116 -Iprofiles/ps4/m4a/include -Iprofiles/ps4/m4a/generated -Iprofiles/ps4/m4a/generated/rawcache_p116 -Iprofiles/ps4/m4a/generated/fieldfast_p116 -Iprofiles/ps4/m4a/generated/rawnem_p123 -Iprofiles/ps4/m4a/generated/nemnative_p126'
CXX_COMMON='-target arm-none-eabi -mcpu=arm7tdmi -mthumb -O2 -fomit-frame-pointer -ffreestanding -fno-builtin -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-unwind-tables -fno-asynchronous-unwind-tables -std=c++17'
CXX="$CXX_COMMON -Iprofiles/ps4/m4a/include -Iprofiles/ps4/m4a/generated -DMD2GBA_GBA_IWRAM=1 -DMD2GBA_PS4_M4A_NATIVE_ONLY=1 -DMD2GBA_PSC_COLD_SEQ=1 -DMD2GBA_PSC_ARM_MIXER=1"

# Rebuild the cold P96/P99 AOT page cache locally when absent.  This removes
# the last dependency on historical build directories while preserving the
# exact page sources and flags that P126 linked.
# P134Z: always evaluate every page dependency. P134Y put the timestamp check
# inside a "count != 410" guard, so a complete cache could silently remain
# stale after generated C or a shared AOT header changed. Track the exact page
# flags and a bounded shared-header set; invalidate the cold cache when either
# changes, then compile only missing/newer objects.
P134_PAGE_TH="$COMMON -Igen_p96_aotfix -Iprofiles/ps4/m4a/include -Iprofiles/ps4/m4a/generated -mthumb -DMD2GBA_GBA_IWRAM=1 -DMD2GBA_PSC_REVERSED_ROM=1 -DMD2GBA_PSC_REVERSED_WRAM=1 -DMD2GBA_PSC_M4A_AUDIO=1 -DMD2GBA_AOT_GENERATED_FAST=1 -O2"
P134_PAGE_FLAGS_FILE=build_p134/cold_pages/.p134_page_flags
if [[ ! -f "$P134_PAGE_FLAGS_FILE" ]] || [[ "$(cat "$P134_PAGE_FLAGS_FILE")" != "$P134_PAGE_TH" ]]; then
  rm -f build_p134/cold_pages/ps4_whole_region_page_*.o
  printf '%s' "$P134_PAGE_TH" > "$P134_PAGE_FLAGS_FILE"
fi
export P134_PAGE_TH
export P134_PAGE_OUT="$PWD/build_p134/cold_pages"
export P134_PROFILE_PAGE="$PWD/profiles/ps4/m4a/generated/ps4_whole_region_page_0340.c"
export P134_PAGE_DEPS="$PWD/runtime/include/md2gba_aot68k.h $PWD/md2gba_bus.h $PWD/md2gba_timing.h $PWD/runtime/include/md2gba_aot_region.h $PWD/runtime/include/md2gba_cycles.h $PWD/gen_p96_aotfix/ps4_whole_region_pschybrid_internal.h"
find gen_p96_aotfix -name 'ps4_whole_region_page_*.c' -print0 | sort -z | xargs -0 -P "${JOBS:-16}" -I{} bash -c '
  f="$1"; s=$(basename "$f" .c); o="$P134_PAGE_OUT/$s.o";
  if [ "$s" = ps4_whole_region_page_0340 ]; then f="$P134_PROFILE_PAGE"; fi;
  rebuild=0; [ ! -s "$o" ] && rebuild=1; [ "$f" -nt "$o" ] && rebuild=1;
  if [ "$rebuild" -eq 0 ]; then for d in $P134_PAGE_DEPS; do [ "$d" -nt "$o" ] && rebuild=1 && break; done; fi;
  if [ "$rebuild" -eq 1 ]; then clang $P134_PAGE_TH -c "$f" -o "$o"; fi
' _ {}
[[ $(find build_p134/cold_pages -name '*.o' -size +0c | wc -l) -eq 410 ]] || { echo need 410 cold AOT pages; exit 4; }

# Rebuild the non-page P99 base objects from current checked-in source.  A P128
# probe verified these are byte-identical to the pinned P99 objects under the
# exact historical flags, eliminating stale-object risk without changing code.
P99_TH="$COMMON -Igen_p96_aotfix -Iprofiles/ps4/m4a/include -Iprofiles/ps4/m4a/generated -mthumb -DMD2GBA_GBA_IWRAM=1 -DMD2GBA_PSC_REVERSED_ROM=1 -DMD2GBA_PSC_REVERSED_WRAM=1 -DMD2GBA_PSC_M4A_AUDIO=1"
clang $P99_TH -O2 -c runtime/src/md2gba_interp68k.c -o build_p134/base/003_interp.o
clang $P99_TH -O2 -c runtime/src/md2gba_cycles.c -o build_p134/base/005_cycles.o
clang $P99_TH -O2 -c md2gba_vdp_soft.c -o build_p134/base/007_vdp_soft.o
clang $P99_TH -O2 -c md2gba_gba_soft.c -o build_p134/base/008_gba_soft.o
clang $P99_TH -O2 -c runtime/generated/md2gba_interp_opclass.c -o build_p134/base/020_opclass.o
clang -target arm-none-eabi -mcpu=arm7tdmi -marm -c irq_entry_p75.S -o build_p134/base/015_irq_entry.o
clang -target arm-none-eabi -mcpu=arm7tdmi -marm -c start_p75.S -o build_p134/base/016_start.o
clang -target arm-none-eabi -mcpu=arm7tdmi -marm -c runtime/src/md2gba_aeabi_min.S -o build_p134/base/023_aeabi_min.o
clang++ $CXX -c profiles/ps4/m4a/src/ps4_chip_audio_data.cpp -o build_p134/base/031_chip_data.o
clang++ $CXX -c profiles/ps4/m4a/src/ps4_fm_multisample_data.cpp -o build_p134/base/032_fm_data.o
clang++ $CXX -c profiles/ps4/m4a/src/ps4_m4a_bridge.cpp -o build_p134/base/033_m4a_bridge_hot.o

# External/copyrighted ROM bytes are never required inside a source handoff.
# Recreate the link objects from the separately supplied verified ROM.
python3 tools/generate_reversed_rom.py "$ROM" build_p134/ps4_reversed.bin --manifest out_p134/P134_REVERSED_ROM_MANIFEST.json >/dev/null
cat > build_p134/ps4_rom.S <<EOS
.syntax unified
.cpu arm7tdmi
.section .rodata.ps4rom,"a",%progbits
.balign 4
.global ps4_rom_start
.type ps4_rom_start,%object
ps4_rom_start:
.incbin "$ROM"
.global ps4_rom_end
ps4_rom_end:
.size ps4_rom_start, ps4_rom_end-ps4_rom_start
EOS
cat > build_p134/ps4_reversed.S <<EOS
.syntax unified
.cpu arm7tdmi
.section .rodata.ps4reversed,"a",%progbits
.balign 4
.global ps4_rom_reversed_start
.type ps4_rom_reversed_start,%object
ps4_rom_reversed_start:
.incbin "$PWD/build_p134/ps4_reversed.bin"
.global ps4_rom_reversed_end
ps4_rom_reversed_end:
.size ps4_rom_reversed_start, ps4_rom_reversed_end-ps4_rom_reversed_start
EOS
cat > build_p134/ps4_m4a_sfx.S <<EOS
.syntax unified
.cpu arm7tdmi
.section .rodata.ps4m4asfx,"a",%progbits
.balign 4
.global ps4_m4a_sfx_bank_start
.type ps4_m4a_sfx_bank_start,%object
ps4_m4a_sfx_bank_start:
.incbin "$PWD/profiles/ps4/m4a/generated/ps4_sfx_bank_13379.pcm"
.global ps4_m4a_sfx_bank_end
ps4_m4a_sfx_bank_end:
.size ps4_m4a_sfx_bank_start, ps4_m4a_sfx_bank_end-ps4_m4a_sfx_bank_start
EOS
clang -target arm-none-eabi -mcpu=arm7tdmi -marm -c build_p134/ps4_rom.S -o build_p134/base/017_rom.o
clang -target arm-none-eabi -mcpu=arm7tdmi -marm -c build_p134/ps4_reversed.S -o build_p134/base/018_reversed.o
clang -target arm-none-eabi -mcpu=arm7tdmi -marm -c build_p134/ps4_m4a_sfx.S -o build_p134/base/022_sfxbank.o

# P134 reproducibility cleanup: rebuild every active source object changed since
# the P99 base instead of trusting P109/P116 cached binaries.  The 410 cold AOT
# pages remain the intentionally pinned P99 cache; all overridden/hot pages are
# compiled below from source.
clang++ $CXX -DMD2GBA_PS4_MAXMOD_MODE_A=1 -DMD2GBA_PS4_MAXMOD_CHANNEL_MAJOR=1 -c profiles/ps4/m4a/src/chip_audio.cpp -o build_p134/chip_maxmod_mode_a.o
clang $COMMON $INCS -marm $DEFS_BASE -O2 -c md2gba_maxmod_gba.c -o build_p134/maxmod_transport.o
clang $COMMON $INCS -marm $DEFS_BASE -O2 -c main_p85.c -o build_p134/main.o
clang $COMMON $INCS -marm $DEFS_BASE -O2 -c irq_vblank.c -o build_p134/vblank.o
clang $COMMON $INCS -mthumb $DEFS_AOT -O2 -c gen_p116/ps4_whole_region_dispatch.c -o build_p134/dispatch.o
clang $COMMON $INCS -mthumb $DEFS_AOT -O2 -c gen_p116/ps4_whole_region_page_0106.c -o build_p134/page_0106.o
clang $COMMON $INCS -mthumb $DEFS_AOT -O2 -c gen_p116/ps4_whole_region_page_0108.c -o build_p134/page_0108.o
for pg in 016c 01aa 01ab; do clang $COMMON $INCS -mthumb $DEFS_AOT -O2 -c gen_p116/ps4_whole_region_page_${pg}.c -o build_p134/page_${pg}.o; done
clang $COMMON $INCS -mthumb $DEFS_AOT -O2 -c profiles/ps4/m4a/generated/rawcache_p116/md2gba_profile_predecode_kos.c -o build_p134/rawcache_kos.o
clang $COMMON $INCS -mthumb $DEFS_AOT -O2 -c profiles/ps4/m4a/generated/fieldfast_p116/md2gba_profile_fieldfast.c -o build_p134/fieldfast.o

# Unsupported audio ownership combinations must fail structurally rather than
# producing a partially composable binary.
if clang $COMMON $INCS -marm $DEFS_BASE -DMD2GBA_Z80_EXECUTION=1 -E main_p85.c >/dev/null 2>&1; then
  echo 'conflicting PSC-native/Z80 audio flags unexpectedly accepted' >&2; exit 6
fi
clang $COMMON $INCS -marm $DEFS_BASE -O2 -c md2gba_bus.c -o build_p134/bus.o
clang $COMMON $INCS -mthumb $DEFS_BASE -O2 -c md2gba_aot68k.c -o build_p134/aot68k.o
clang $COMMON $INCS -mthumb $DEFS_AOT -Oz -c md2gba_timing.c -o build_p134/timing.o
clang $COMMON $INCS -marm $DEFS_BASE -O2 -c md2gba_gba_backend.c -o build_p134/backend.o
clang $COMMON $INCS -mthumb $DEFS_BASE -O2 -c md2gba_gba_projected.c -o build_p134/projected.o
clang $COMMON $INCS -mthumb $DEFS_AOT -Oz -c gen_p126/ps4_whole_region_page_0105.c -o build_p134/page_0105.o
clang $COMMON $INCS -mthumb $DEFS_AOT -O2 -c profiles/ps4/m4a/generated/rawnem_p123/md2gba_profile_raw_nem.c -o build_p134/rawnem.o
clang $COMMON $INCS -mthumb $DEFS_AOT -O2 -c profiles/ps4/m4a/generated/nemnative_p126/md2gba_profile_nem_native.c -o build_p134/nemnative.o

if [[ "${P134_SKIP_TESTS:-0}" != "1" ]]; then
  # Core P122/P124 virtual-VDP + scheduler differentials.
  cc -O2 -I. -Iruntime/include tests/test_phase122_vdp_stream.c md2gba_bus.c -o build_p134/test_vdp122
  ./build_p134/test_vdp122
  cc -O2 -I. -Iruntime/include tests/test_phase123_vdp_stream.c md2gba_bus.c -o build_p134/test_vdp123
  ./build_p134/test_vdp123
  cc -O2 -I. -Iruntime/include tests/test_phase124_scroll_shadow.c md2gba_bus.c -o build_p134/test_scroll124
  ./build_p134/test_scroll124
  cc -O2 -I. -Iruntime/include -Iruntime/generated tests/test_phase121_unobservable_span.c md2gba_timing.c md2gba_bus.c md2gba_aot68k.c runtime/src/md2gba_cycles.c runtime/generated/md2gba_interp_opclass.c -o build_p134/test_span121
  ./build_p134/test_span121
  cc -O2 -I. -Iruntime/include -Iruntime/generated tests/test_phase123_span_arithmetic.c md2gba_timing.c md2gba_bus.c md2gba_aot68k.c runtime/src/md2gba_cycles.c runtime/generated/md2gba_interp_opclass.c -o build_p134/test_span123
  ./build_p134/test_span123
  # Opening VBlank-batch oracle.
  cc -O2 -I. -Iruntime/include -Iruntime/generated -Iprofiles/ps4/m4a/generated/rawnem_p123 \
   tests/test_phase121_rawnem_batch.c profiles/ps4/m4a/generated/rawnem_p123/md2gba_profile_raw_nem.c md2gba_bus.c md2gba_timing.c md2gba_aot68k.c runtime/src/md2gba_cycles.c runtime/generated/md2gba_interp_opclass.c -o build_p134/test_batch
  ./build_p134/test_batch
  # All-game native token decoder vs canonical interpreter.
  cc -O2 -I. -Iruntime/include -Iruntime/generated -Iprofiles/ps4/m4a/generated/nemnative_p126 \
   tests/test_phase125_nem_native.c profiles/ps4/m4a/generated/nemnative_p126/md2gba_profile_nem_native.c md2gba_bus.c md2gba_aot68k.c runtime/src/md2gba_interp68k.c runtime/src/md2gba_cycles.c runtime/generated/md2gba_interp_opclass.c -o build_p134/test_nemnative
  ./build_p134/test_nemnative "$ROM"
  # Combined real page: VBlank batch / per-command / native token ordering must remain canonical at all opening boundaries.
  cc -O2 -I. -Iruntime/include -Iruntime/generated -Igen_p126 -Igen_p116 -Iprofiles/ps4/m4a/generated/rawnem_p123 -Iprofiles/ps4/m4a/generated/nemnative_p126 \
   tests/test_phase125_integrated_page.c gen_p126/ps4_whole_region_page_0105.c profiles/ps4/m4a/generated/rawnem_p123/md2gba_profile_raw_nem.c profiles/ps4/m4a/generated/nemnative_p126/md2gba_profile_nem_native.c \
   md2gba_bus.c md2gba_aot68k.c runtime/src/md2gba_interp68k.c md2gba_timing.c runtime/src/md2gba_cycles.c runtime/generated/md2gba_interp_opclass.c -o build_p134/test_integrated
  ./build_p134/test_integrated "$ROM"
  python3 tests/test_phase96_aot_timingfix.py
  python3 tests/test_phase108_vdp_bulk.py
  python3 tests/test_phase109_generator_delta.py
  cc -O2 -I. -Iruntime/include -Iruntime/generated -Igen_p116 -Iprofiles/ps4/m4a/generated/rawcache_p116 \
   tests/test_phase112_integrated_kos_page.c gen_p116/ps4_whole_region_page_0106.c profiles/ps4/m4a/generated/rawcache_p116/md2gba_profile_predecode_kos.c \
   md2gba_bus.c md2gba_aot68k.c runtime/src/md2gba_interp68k.c md2gba_timing.c runtime/src/md2gba_cycles.c runtime/generated/md2gba_interp_opclass.c \
   -o build_p134/test_integrated_rawcache
  ./build_p134/test_integrated_rawcache "$ROM"

  # P134 OPTAGG: preserve the original OPT-A exact UI output oracle.
  cc -O2 -ffunction-sections -fdata-sections -Wl,--gc-sections -I. -Iruntime/include -Iruntime/generated tests/test_opta_ui.c md2gba_bus.c -o build_p134/test_opta_ui
  ./build_p134/test_opta_ui
  # P134 OPTAGG: rerun the recovered OPT-B differentials on the rebased P126 sources.
  HOSTOPT='-O2 -DMD2GBA_PSC_REVERSED_ROM=1 -DMD2GBA_PSC_REVERSED_WRAM=1 -I. -Iruntime/include'
  cc $HOSTOPT tests/optb_reference/test_optb_streams.c md2gba_bus.c -o build_p134/test_optb_streams
  ./build_p134/test_optb_streams
  cc $HOSTOPT tests/optb_reference/test_optb_dma.c -o build_p134/test_optb_dma
  ./build_p134/test_optb_dma
  cc $HOSTOPT tests/optb_reference/test_optb_renderer.c md2gba_bus.c -o build_p134/test_optb_renderer
  ./build_p134/test_optb_renderer
  cc $HOSTOPT tests/optb_reference/test_optb_sprite_refs.c md2gba_bus.c -o build_p134/test_optb_sprite_refs
  ./build_p134/test_optb_sprite_refs
  cc -O2 -ffunction-sections -fdata-sections -Wl,--gc-sections -I. -Iruntime/include tests/test_p128_shift_div.c md2gba_aot68k.c md2gba_bus.c -o build_p134/test_p128_shift_div
  ./build_p134/test_p128_shift_div
  cc -O2 -ffunction-sections -fdata-sections -Wl,--gc-sections -I. -Iruntime/include tests/test_p128_scroll_planner.c md2gba_gba_backend.c md2gba_bus.c -o build_p134/test_p128_scroll_planner
  ./build_p134/test_p128_scroll_planner
  cc -O2 tests/test_p129_fast_div.c -o build_p134/test_p129_fast_div
  ./build_p134/test_p129_fast_div
  cc -O2 -I. -Iruntime/include tests/test_p129_dma_prefix.c -o build_p134/test_p129_dma_prefix
  ./build_p134/test_p129_dma_prefix

  # P134W architectural LEA stride fusions: preserve the complete An value
  # while memory accesses remain 24-bit bus-domain.
  python3 tests/test_phase87_strided_fusions.py

  # P134W immutable Kosinski/Nemesis trace fast paths store 24-bit bus snapshots;
  # restoring them must preserve the architectural top byte already in An.
  cc -O2 -I. -Iruntime/include -Iruntime/generated -Iprofiles/ps4/m4a/generated/rawcache_p116 -Iprofiles/ps4/m4a/generated/rawnem_p123 \
   tests/test_p134w_fast_profile_arch.c md2gba_bus.c md2gba_timing.c md2gba_aot68k.c runtime/src/md2gba_cycles.c runtime/generated/md2gba_interp_opclass.c \
   -o build_p134/test_p134w_fast_profile_arch
  ./build_p134/test_p134w_fast_profile_arch

  # P134 architectural address-register correction: abs.w LEA sign extension
  # must survive into architectural A registers while bus accesses remain 24-bit.
  cc -O2 -std=c11 -I. -Iruntime/include -Iruntime/generated tests/test_p134_addrreg_semantics.c runtime/src/md2gba_interp68k.c md2gba_bus.c md2gba_aot68k.c runtime/src/md2gba_cycles.c runtime/generated/md2gba_interp_opclass.c -o build_p134/test_p134_addrreg
  ./build_p134/test_p134_addrreg

  # P134 screen-fit/runtime oracle: exact declared 320x224 -> 240x160 sampling,
  # bounded resident references across scrolling, Mode-0/SBB layout and gates.
  cc -O2 -std=c11 -I. -Iruntime/include -Iprofiles/ps4/m4a/include tests/test_p134_projected_runtime.c -o build_p134/test_p134_projected_runtime
  ./build_p134/test_p134_projected_runtime
  cc -O2 -std=c11 -I. -Iruntime/include -Iprofiles/ps4/m4a/include -Itests tests/test_p134y_projected_costs.c -o build_p134/test_p134y_projected_costs
  ./build_p134/test_p134y_projected_costs
  cc -O2 -std=c11 -I. -Iruntime/include -Iprofiles/ps4/m4a/include tests/test_p134z_blank_pair.c -o build_p134/test_p134z_blank_pair
  ./build_p134/test_p134z_blank_pair

  # P135 exact transactional rebuild carry-over: renderer output must match
  # reuse-disabled behavior, and slot ownership/refcounts must remain exact.
  cc -O2 -std=c11 -I. -Iruntime/include -Iprofiles/ps4/m4a/include -DMD2GBA_PROJECTED_REBUILD_REUSE=1 tests/test_p135_rebuild_reuse.c -o build_p134/test_p135_reuse_on
  cc -O2 -std=c11 -I. -Iruntime/include -Iprofiles/ps4/m4a/include -DMD2GBA_PROJECTED_REBUILD_REUSE=0 tests/test_p135_rebuild_reuse.c -o build_p134/test_p135_reuse_off
  ./build_p134/test_p135_reuse_on 60 80 > out_p134/P135_REUSE_ON.txt 2> out_p134/P135_REUSE_ON_STATS.txt
  ./build_p134/test_p135_reuse_off 60 80 > out_p134/P135_REUSE_OFF.txt 2> out_p134/P135_REUSE_OFF_STATS.txt
  cmp out_p134/P135_REUSE_ON.txt out_p134/P135_REUSE_OFF.txt
  cc -O2 -std=c11 -I. -Iruntime/include -Iprofiles/ps4/m4a/include tests/test_p135_targeted.c -o build_p134/test_p135_targeted
  ./build_p134/test_p135_targeted | tee out_p134/P135_TARGETED.txt
  cc -O2 -std=c11 -I. -Iruntime/include -Iprofiles/ps4/m4a/include -Itests tests/test_p135_ref_integrity.c -o build_p134/test_p135_ref_integrity
  ./build_p134/test_p135_ref_integrity

  # P136/P136A: real-game presenter replay.  The in-place recovery and bounded
  # recent-capacity hint must remove transition blackouts without changing any
  # settled displayed frame relative to the reuse/in-place-disabled baseline.
  cc -O2 -std=gnu11 -I. -Iruntime/include -Iruntime/generated -Iprofiles/ps4/m4a/include -Itests \
   tests/replay_p136_presenter.c md2gba_bus.c md2gba_aot68k.c runtime/src/md2gba_interp68k.c md2gba_timing.c \
   runtime/src/md2gba_cycles.c runtime/generated/md2gba_interp_opclass.c md2gba_vdp_soft.c md2gba_gba_backend.c \
   -o build_p134/replay_p136_on
  cc -O2 -std=gnu11 -DMD2GBA_PROJECTED_INPLACE_RECOVERY=0 -DMD2GBA_PROJECTED_REBUILD_REUSE=0 \
   -I. -Iruntime/include -Iruntime/generated -Iprofiles/ps4/m4a/include -Itests \
   tests/replay_p136_presenter.c md2gba_bus.c md2gba_aot68k.c runtime/src/md2gba_interp68k.c md2gba_timing.c \
   runtime/src/md2gba_cycles.c runtime/generated/md2gba_interp_opclass.c md2gba_vdp_soft.c md2gba_gba_backend.c \
   -o build_p134/replay_p136_off
  ./build_p134/replay_p136_on "$ROM" 1200 h out_p134/P136A_REPLAY_ON.hash | tee out_p134/P136A_REPLAY_ON.txt
  ./build_p134/replay_p136_off "$ROM" 1200 h out_p134/P136A_REPLAY_OFF.hash | tee out_p134/P136A_REPLAY_OFF.txt
  cmp out_p134/P136A_REPLAY_ON.hash out_p134/P136A_REPLAY_OFF.hash
  grep -q 'frames_with_dark_rebuild=1 .*pair_builds=9329' out_p134/P136A_REPLAY_ON.txt
  grep -q 'frames_with_dark_rebuild=8 .*pair_builds=20461' out_p134/P136A_REPLAY_OFF.txt

  # P136A: transaction-local preparation dedup.  The exact audit reproducer's
  # native-panel close must stay below the old 120 pair-synthesis baseline.
  cc -O2 -Itests -I. -Iruntime/include -Iprofiles/ps4/m4a/include tests/test_p136a_work_amplification.c -o build_p134/test_p136a_work
  ./build_p134/test_p136a_work | tee out_p134/P136A_WORK_AMPLIFICATION.txt
  grep -q 'close_panel rc=0 builds=60 ' out_p134/P136A_WORK_AMPLIFICATION.txt

  # P137: every statically folded exact-entry cycle charge must remain bit-for-bit
  # equivalent to the canonical runtime estimator for every decoded opcode and
  # both branch continuations.
  python3 tests/test_p137_static_cycles.py

  python3 tests/test_p137_partial_ccr.py

  # P137: PSC-style exact name-table dirty geometry replaces the old lossy
  # per-cell bloom. Randomized wrapped/negative geometries must match the exact
  # source-footprint oracle with no false positives or negatives.
  cc -O2 -Itests -I. -Iruntime/include -Iprofiles/ps4/m4a/include tests/test_p137_name_dirty_geometry.c -o build_p134/test_p137_name_dirty
  ./build_p134/test_p137_name_dirty

  # P140: PSC-style lazy exact pattern summary. A dirty pattern invalidates its
  # cached blank/nonblank state and the next consumer must refresh exactly.
  cc -O2 -std=c11 -DMD2GBA_PROJECTED_TEST_PATTERN_CACHE=1 -Itests -I. -Iruntime/include -Iprofiles/ps4/m4a/include \
   tests/test_p140_pattern_summary.c -o build_p134/test_p140_pattern_summary
  ./build_p134/test_p140_pattern_summary

  # P138: exact output-equivalence preflight must collapse dirty-but-visually-
  # identical updates to no synthesis/publication, while leaving real work intact.
  grep -q 'pattern_net_revert rc=0 builds=0 .*map_writes=0 bank_switch=0 pending=0' out_p134/P136A_WORK_AMPLIFICATION.txt
  grep -q 'same_pixels_different_pattern_id rc=0 builds=0 .*map_writes=0 bank_switch=0' out_p134/P136A_WORK_AMPLIFICATION.txt
  grep -q 'empty_plane_priority_change rc=0 builds=0 .*map_writes=0 bank_switch=0' out_p134/P136A_WORK_AMPLIFICATION.txt
  grep -q 'diagonal_one_cell rc=0 builds=52 .*map_writes=51' out_p134/P136A_WORK_AMPLIFICATION.txt

  # P138: the dominant MOVEM.L -(A7)/(A7)+ helpers must be complete-state
  # equivalent to the canonical generic helper, including MC68000 A7-in-mask semantics.
  cc -O2 -std=c11 -DMD2GBA_PSC_REVERSED_WRAM=1 -I. -Iruntime/include \
   tests/test_p138_movem_stack.c md2gba_aot68k.c md2gba_bus.c -o build_p134/test_p138_movem_stack
  ./build_p134/test_p138_movem_stack

  # P139A: PSC-style profile-authoritative negative UI scan + predictive edge prebuild.
  # Same canonical 4,300-VBlank route used for the P138/P139 A/B. Visible hashes
  # are compared externally to P138 in the validation record; this local gate fixes
  # the candidate work/read counts so regressions are immediately visible.
  cc -O2 -std=gnu11 -I. -Iruntime/include -Iruntime/generated -Iprofiles/ps4/m4a/include -Itests \
   tests/replay_p139_route.c md2gba_bus.c md2gba_aot68k.c runtime/src/md2gba_interp68k.c md2gba_timing.c \
   runtime/src/md2gba_cycles.c runtime/generated/md2gba_interp_opclass.c md2gba_vdp_soft.c md2gba_gba_backend.c \
   -o build_p134/replay_p139_route
  P137_INPUT=tests/p139_route_input.txt ./build_p134/replay_p139_route "$ROM" 4300 h out_p134/P139A_ROUTE.hash | tee out_p134/P139A_ROUTE.txt
  grep -q 'projected=4110' out_p134/P139A_ROUTE.txt
  grep -q 'frames_with_dark_rebuild=1' out_p134/P139A_ROUTE.txt
  grep -q 'pair_builds=36471' out_p134/P139A_ROUTE.txt
  grep -q 'UI_NAME_READS=35005' out_p134/P139A_ROUTE.txt

  # P134U: restore the proven P104 PSC/Maxmod Mode-A channel-major audio path.
  g++ -O2 -std=c++17 -Iprofiles/ps4/m4a/include -Iprofiles/ps4/m4a/generated \
   -DMD2GBA_PS4_MAXMOD_MODE_A=1 -DMD2GBA_PS4_MAXMOD_CHANNEL_MAJOR=1 \
   tests/test_p134t_psc104_catalog.cpp profiles/ps4/m4a/src/chip_audio.cpp profiles/ps4/m4a/src/ps4_chip_audio_data.cpp profiles/ps4/m4a/src/ps4_fm_multisample_data.cpp \
   -o build_p134/test_p134t_psc104_catalog
  ./build_p134/test_p134t_psc104_catalog
  python3 tests/test_p134t_psc104_profile.py
  python3 tests/test_p134y_ccr_dce.py

fi

for o in build_p134/projected.o build_p134/timing.o build_p134/dispatch.o build_p134/page_0105.o build_p134/page_0106.o build_p134/page_0108.o build_p134/page_016c.o build_p134/page_01aa.o build_p134/page_01ab.o; do
  if nm -u "$o" | grep -E '__aeabi_(u?idiv|uidivmod|l?div|uldivmod)' >/dev/null; then
    echo "unexpected division helper in $o" >&2; nm -u "$o"; exit 7
  fi
done

mapfile -t PAGES < <(find build_p134/cold_pages -name '*.o' \
 ! -name 'ps4_whole_region_page_0105.o' ! -name 'ps4_whole_region_page_0106.o' ! -name 'ps4_whole_region_page_0108.o' \
 ! -name 'ps4_whole_region_page_016c.o' ! -name 'ps4_whole_region_page_01aa.o' ! -name 'ps4_whole_region_page_01ab.o' -print | sort)
ELF=out_p134/RUN_THIS_MD2GBA_P142_PSCDEEPFAST.elf
MAP=out_p134/RUN_THIS_MD2GBA_P142_PSCDEEPFAST.map
OUT=out_p134/RUN_THIS_MD2GBA_P142_PSCDEEPFAST.gba
ld.lld --gc-sections -T link_p85.ld -Map="$MAP" -o "$ELF" \
 build_p134/base/016_start.o build_p134/base/015_irq_entry.o build_p134/main.o build_p134/bus.o build_p134/aot68k.o build_p134/base/003_interp.o build_p134/timing.o build_p134/base/005_cycles.o \
 build_p134/backend.o build_p134/projected.o build_p134/base/007_vdp_soft.o build_p134/base/008_gba_soft.o build_p134/vblank.o build_p134/maxmod_transport.o build_p134/dispatch.o build_p134/base/020_opclass.o \
 build_p134/chip_maxmod_mode_a.o build_p134/base/031_chip_data.o build_p134/base/032_fm_data.o build_p134/base/033_m4a_bridge_hot.o build_p134/base/023_aeabi_min.o \
 "${PAGES[@]}" build_p134/page_0105.o build_p134/page_0106.o build_p134/page_0108.o build_p134/page_016c.o build_p134/page_01aa.o build_p134/page_01ab.o \
 build_p134/rawcache_kos.o build_p134/fieldfast.o build_p134/rawnem.o build_p134/nemnative.o build_p134/base/022_sfxbank.o build_p134/base/017_rom.o build_p134/base/018_reversed.o
llvm-objcopy -O binary "$ELF" "$OUT"
python3 fix_gba_header.py "$OUT" --title 'M2G142 PSCF' --game-code Q2PF
if nm -u "$ELF" | grep -q .; then nm -u "$ELF"; exit 9; fi
sha256sum "$OUT" | tee out_p134/P142_SHA256.txt
wc -c "$OUT" | tee out_p134/P142_SIZE.txt
size -A "$ELF" > out_p134/P142_SECTION_SIZES.txt
grep -E '\.iwram( |\.|$)|nem_rawcmd|nem_token|fieldfast|kos_rawcache|movem|vdp_data16_direct|raise_autovector' "$MAP" > out_p134/P142_IWRAM_MAP.txt || true
python3 - <<'PY'
from pathlib import Path
s=Path('out_p134/P142_SECTION_SIZES.txt').read_text(errors='ignore'); total=0
for line in s.splitlines():
 p=line.split()
 if len(p)>=2 and p[0].startswith('.iwram'):
  try: total+=int(p[1],0)
  except: pass
limit=28672
reserve=1024
print('P142 IWRAM bytes:',total,'free:',limit-total)
if total>limit: raise SystemExit('IWRAM overflow')
if limit-total<reserve: raise SystemExit(f'IWRAM safety reserve violated: {limit-total} < {reserve}')
PY
[[ $(wc -c < "$OUT") -le 33554432 ]] || { echo ROM overflow; exit 10; }
