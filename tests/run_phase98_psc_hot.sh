#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"; cd "$ROOT"
TMP="${TMPDIR:-/tmp}/md2gba-p98-tests-$$"; mkdir -p "$TMP"; trap 'rm -rf "$TMP"' EXIT
python3 tests/test_phase95_initfix.py
python3 tests/test_phase96_aot_timingfix.py
python3 tests/test_phase98_psc_hot.py
cc -O2 -I. tests/test_phase93_psc_m4a_transport.c md2gba_psc_m4a.c -o "$TMP/transport"; "$TMP/transport"
g++ -O2 -std=c++17 -Iprofiles/ps4/m4a/include -Iprofiles/ps4/m4a/generated \
 tests/test_phase98_pcm_equiv.cpp profiles/ps4/m4a/src/chip_audio.cpp \
 profiles/ps4/m4a/src/ps4_chip_audio_data.cpp profiles/ps4/m4a/src/ps4_fm_multisample_data.cpp \
 -o "$TMP/pcm"; "$TMP/pcm"
python3 tests/test_phase93_sfx_profile.py
python3 tests/test_phase93_profile_bypass.py
