from pathlib import Path
root=Path(__file__).resolve().parents[1]
build=(root/'build_p134q_fullv18.sh').read_text()
chip=(root/'profiles/ps4/m4a/src/chip_audio.cpp').read_text()
bridge=(root/'profiles/ps4/m4a/src/ps4_m4a_bridge.cpp').read_text()
main=(root/'main_p85.c').read_text()
assert '-DMD2GBA_PS4_V18_FULL_MIX=1' in build
assert 'v18_prepare_fast_frame();' in chip
assert 'fast_to_v18_q8[52]' in chip
assert 'v18_frame_fm_gain_q8' in chip and 'v18_frame_dac_gain_q8' in chip
assert 'cmd==0xFBu){md2gba_ps4_m4a_fade();return;' in main
assert 'ps4gba::chip_audio::music_id()==music_id' in bridge
assert 'clear_lane(normal_sfx); clear_lane(special_sfx);' in bridge
assert 'fade_active=true' in bridge and 'fade_level=15u' in bridge
print('P134Q audio profile structural gates: PASS')
