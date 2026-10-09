from pathlib import Path
root=Path(__file__).resolve().parents[1]
build=(root/'build_p134r_fullv19.sh').read_text()
chip=(root/'profiles/ps4/m4a/src/chip_audio.cpp').read_text()
fm=(root/'profiles/ps4/m4a/src/ps4_fm_multisample_data_generated.inc').read_text()
seq=(root/'profiles/ps4/m4a/src/ps4_chip_audio_data_generated.inc').read_text()
bridge=(root/'profiles/ps4/m4a/src/ps4_m4a_bridge.cpp').read_text()
main=(root/'main_p85.c').read_text()
assert '-DMD2GBA_PS4_V19_FULL_MIX=1' in build
assert 'v19_prepare_fast_frame();' in chip
assert 'song_master_q8(music_id, fm_backend::multisample)' in chip
assert 'source_eg_headroom_q8(music_id, fm_backend::multisample)' in chip
assert 'v19_frame_fm_gain_q8' in chip and 'v19_frame_dac_gain_q8' in chip
assert 'fast_to_v19_q8[52]' in chip
assert 'const int pan_q8 = pan_bits == 0xC0u ? 256 : (pan_bits ? 128 : 0);' in chip
assert 'if(music_id == 0x88u) q8 = (q8 * 288) >> 8;' in chip
assert '#define PS4GBA_BEHIND_DAC_Q8 80' in chip
assert 'generate_ps4_fm_multisamples_ymref.py' in fm
assert 'v19 six-stage exact-YM PSIV FM bank' in fm
assert 'generated_fm_wave_zone_count = 511u' in fm
# Keep P93/P104's low-cost transport retarget rather than restoring 31.536-kHz phase tables.
assert 'P93 PSC/M4A mixer-rate retarget: phase increments are for 13379u.' in seq
assert 'cmd==0xFBu){md2gba_ps4_m4a_fade();return;' in main
assert 'ps4gba::chip_audio::music_id()==music_id' in bridge
assert 'clear_lane(normal_sfx); clear_lane(special_sfx);' in bridge
assert 'fade_active=true' in bridge and 'fade_level=15u' in bridge
print('P134R recovered v19 audio profile structural gates: PASS')
