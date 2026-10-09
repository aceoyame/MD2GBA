#!/usr/bin/env python3
from pathlib import Path
import subprocess,hashlib
root=Path(__file__).resolve().parents[1]
elf=root/'out_p96/P96_AOT_TIMINGFIX.elf'
rom=root/'out_p96/RUN_THIS_MD2GBA_P96_AOT_TIMINGFIX.gba'
assert elf.exists() and rom.exists(), (elf,rom)
nm=subprocess.check_output(['nm','-n',str(elf)],text=True)
undef=subprocess.check_output(['nm','-u',str(elf)],text=True).strip()
assert not undef,undef
for sym in ['md2gba_z80_run','md2gba_mix_pcm_render','md2gba_gba_pcm_stream_service','pump_refill_one_slice']:
    assert sym not in nm,sym
for sym in ['md2gba_psc_m4a_vsync','md2gba_psc_m4a_service','md2gba_ps4_m4a_mix_mono','md2gba_ps4_m4a_sfx_play']:
    assert sym in nm,sym
body=subprocess.check_output(['llvm-objdump','-d','--disassemble-symbols=_ZN6ps4gba10chip_audio12mix_psc_monoEPai',str(elf)],text=True)
assert '__aeabi_' not in body
b=rom.read_bytes(); assert len(b)<=32*1024*1024
chk=(-((sum(b[0xA0:0xBD])+0x19)&0xff))&0xff
assert b[0xBD]==chk,(b[0xBD],chk)
print('PASS P96 linked audio/scheduler target: native PSC/M4A linked, old live paths absent, no undefined symbols, header checksum OK')
print('ROM sha256',hashlib.sha256(b).hexdigest(),'bytes',len(b),'header',hex(chk))
