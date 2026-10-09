#!/usr/bin/env python3
from pathlib import Path
import argparse

LOGO = bytes.fromhex('''
24 FF AE 51 69 9A A2 21 3D 84 82 0A 84 E4 09 AD
11 24 8B 98 C0 81 7F 21 A3 52 BE 19 93 09 CE 20
10 46 4A 4A F8 27 31 EC 58 C7 E8 33 82 E3 CE BF
85 F4 DF 94 CE 4B 09 C1 94 56 8A C0 13 72 A7 FC
9F 84 4D 73 A3 CA 9A 61 58 97 A3 27 FC 03 98 76
23 1D C7 61 03 04 AE 56 BF 38 84 00 40 A7 0E FD
FF 52 FE 03 6F 95 30 F1 97 FB C0 85 60 D6 80 25
A9 63 BE 03 01 4E 38 E2 F9 A2 34 FF BB 3E 03 44
78 00 90 CB 88 11 3A 94 65 C0 7C 63 87 F0 3C AF
D6 25 E4 8B 38 0A AC 72 21 D4 F8 07
''')
assert len(LOGO) == 156

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('rom', type=Path)
    ap.add_argument('--title', default='MD2GBA TEST')
    ap.add_argument('--game-code', default='M2G0')
    ap.add_argument('--maker', default='00')
    a=ap.parse_args()
    b=bytearray(a.rom.read_bytes())
    if len(b) < 0xC0: b.extend(b'\0'*(0xC0-len(b)))
    b[0x04:0xA0]=LOGO
    title=a.title.encode('ascii','replace')[:12].ljust(12,b' ')
    code=a.game_code.encode('ascii','replace')[:4].ljust(4,b' ')
    maker=a.maker.encode('ascii','replace')[:2].ljust(2,b' ')
    b[0xA0:0xAC]=title
    b[0xAC:0xB0]=code
    b[0xB0:0xB2]=maker
    b[0xB2]=0x96
    b[0xB3]=0x00
    b[0xB4]=0x00
    b[0xB5:0xBC]=b'\0'*7
    b[0xBC]=0x00
    b[0xBD]=(-0x19-sum(b[0xA0:0xBD])) & 0xFF
    b[0xBE:0xC0]=b'\0\0'
    # Pad to a harmless power-of-two-ish minimum so tools recognize a cartridge image cleanly.
    target=max(0x400, (len(b)+3)&~3)
    b.extend(b'\xFF'*(target-len(b)))
    a.rom.write_bytes(b)
    print(f'{a.rom}: {len(b)} bytes, header checksum=0x{b[0xBD]:02X}')

if __name__=='__main__': main()
