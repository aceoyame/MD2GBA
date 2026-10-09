from pathlib import Path
import hashlib
root=Path(__file__).resolve().parents[1]
b=(root/'build_p134t_psc104.sh').read_text()
chip=(root/'profiles/ps4/m4a/src/chip_audio.cpp').read_text()
transport=(root/'md2gba_maxmod_gba.c').read_text()
bridge=(root/'profiles/ps4/m4a/src/ps4_m4a_bridge.cpp').read_text()
assert '-DMD2GBA_PS4_MAXMOD_MODE_A=1' in b and '-DMD2GBA_PS4_MAXMOD_CHANNEL_MAJOR=1' in b
assert 'MD2GBA_PS4_V18_FULL_MIX=1' not in b and 'MD2GBA_PS4_V19_FULL_MIX=1' not in b
assert 'MD2GBA_PS4_MAXMOD_CHANNEL_MAJOR' in chip
assert 'g_audio_silence' in transport and 'restart_dma_from(s,g_audio_silence)' in transport
assert 'SOUNDCNT_H=0x0B0Eu;' in transport
assert 'set_volume(224);' in bridge and 'play(music_id,224,true)' in bridge
fm=(root/'profiles/ps4/m4a/src/ps4_fm_multisample_data_generated.inc').read_bytes()
assert hashlib.sha256(fm).hexdigest()=='fc356493e82546f7d818389ad5a58cb3299f17f6263f680633021106c439231d'
print('P134T PSC104 structural profile: PASS')
