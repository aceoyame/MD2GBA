#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"; cd "$ROOT"
TMP="${TMPDIR:-/tmp}/md2gba-p93-tests-$$"; mkdir -p "$TMP"; trap 'rm -rf "$TMP"' EXIT
CFLAGS='-O2 -ffunction-sections -fdata-sections -I. -Iruntime/include -Iruntime/generated'
cc $CFLAGS tests/test_phase83_ui_fit.c md2gba_bus.c -Wl,--gc-sections -o "$TMP/ui_fit"; "$TMP/ui_fit"
cc $CFLAGS tests/test_phase89_ui_present.c md2gba_bus.c -Wl,--gc-sections -o "$TMP/ui_present"; "$TMP/ui_present"
cc $CFLAGS tests/test_phase87_psc_vdp_control.c md2gba_bus.c -Wl,--gc-sections -o "$TMP/vdp_control"; "$TMP/vdp_control"
cc $CFLAGS tests/test_phase87_psc_vdp_read.c md2gba_bus.c -Wl,--gc-sections -o "$TMP/vdp_read"; "$TMP/vdp_read"
cc $CFLAGS -DMD2GBA_PSC_REVERSED_WRAM=1 tests/test_phase87_psc_reversed_wram.c md2gba_bus.c -Wl,--gc-sections -o "$TMP/rev_wram"; "$TMP/rev_wram"
cc $CFLAGS tests/test_phase88_psc_scroll_shadow.c md2gba_bus.c md2gba_gba_backend.c -Wl,--gc-sections -o "$TMP/scroll_shadow"; "$TMP/scroll_shadow"
cc $CFLAGS tests/test_phase87_live_scroll_pending.c md2gba_gba_backend.c md2gba_bus.c -Wl,--gc-sections -o "$TMP/live_scroll"; "$TMP/live_scroll"
cc $CFLAGS -I. tests/test_phase93_psc_m4a_transport.c md2gba_psc_m4a.c -Wl,--gc-sections -o "$TMP/m4a_slot"; "$TMP/m4a_slot"
g++ -O2 -std=c++17 -Iprofiles/ps4/m4a/include -Iprofiles/ps4/m4a/generated \
 tests/test_phase93_psc_m4a_catalog.cpp profiles/ps4/m4a/src/chip_audio.cpp \
 profiles/ps4/m4a/src/ps4_chip_audio_data.cpp profiles/ps4/m4a/src/ps4_fm_multisample_data.cpp \
 -o "$TMP/catalog"; "$TMP/catalog"
python3 tests/test_phase93_sfx_profile.py
python3 tests/test_phase93_profile_bypass.py
python3 tests/test_phase93_linked_audio.py
