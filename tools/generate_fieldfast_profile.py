#!/usr/bin/env python3
"""Generate the PSIV profile-side sustained-field semantic hook.

The generic MD2GBA runtime/emitter knows only how to call validated profile
semantics; title-specific guest PCs and span hashes live here.  The helper is
emitted only when the immutable guest instruction span matches exactly.
"""
from __future__ import annotations
import argparse, hashlib, json
from pathlib import Path

START=0x04209C
END=0x042120
EXPECT='15f08e30e288c1b928f1a4ee708922da7d77c1a6babbacb63a5ad3e8b7bac378'

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('rom', type=Path)
    ap.add_argument('-o','--out', type=Path, required=True)
    a=ap.parse_args()
    rom=a.rom.read_bytes(); span=rom[START:END]
    got=hashlib.sha256(span).hexdigest()
    if got != EXPECT:
        raise SystemExit(f'fieldfast source span mismatch: {got} != {EXPECT}')
    a.out.mkdir(parents=True,exist_ok=True)
    (a.out/'md2gba_profile_fieldfast.h').write_text(r'''#ifndef MD2GBA_PROFILE_FIELDFAST_H
#define MD2GBA_PROFILE_FIELDFAST_H
#include <stdint.h>
#include "md2gba_aot68k.h"
#include "md2gba_bus.h"
int md2gba_profile_fieldfast_4209c(Md2Gba68k*,Md2GbaBus*,uint32_t,uint32_t*,uint32_t*);
#endif
''')
    (a.out/'md2gba_profile_fieldfast.c').write_text(r'''/* AUTO-GENERATED/validated profile semantic for guest $04209C..$04211E.
   Source-span SHA-256: 15f08e30e288c1b928f1a4ee708922da7d77c1a6babbacb63a5ad3e8b7bac378
   Contract: succeeds only with >286 scheduled cycles, executes 41 guest
   instructions through RTS, and reports the exact 286-cycle/41-step charge. */
#include <stdint.h>
#include "md2gba_profile_fieldfast.h"
#if defined(MD2GBA_GBA_IWRAM)
#define FIELD_HOT __attribute__((section(".iwram.fieldfast"),noinline))
#else
#define FIELD_HOT
#endif

static inline uint32_t rol32_2(uint32_t x){return (x<<2)|(x>>30);}

FIELD_HOT int md2gba_profile_fieldfast_4209c(Md2Gba68k*c,Md2GbaBus*b,uint32_t left,uint32_t*cost,uint32_t*steps){
    uint32_t d0,d1,d2,d3,a0,sp,v,t;
    uint16_t ef0a,ef14;
    uint16_t sr;
    if(left<=286u)return 0; /* strict P96 deadline seam */

    d0=c->d[0]; d1=c->d[1]; d2=c->d[2]; d3=c->d[3];
    a0=d0;

    /* Preserve the original guest bus transaction order exactly. */
    md2gba_wram_write8_offset(b,0xEF0Bu,(uint8_t)(md2gba_wram_read8_offset(b,0xEF0Bu)|0x10u));
    ef0a=md2gba_wram_read16_offset(b,0xEF0Au);
    md2gba_write16(b,c->a[6],ef0a);
    md2gba_write16(b,c->a[6],0x8F02u);

    t=(uint32_t)(int32_t)(int16_t)(d2&0xFFFFu);
    t<<=8;
    t=(t&0xFFFF0000u)|((t&0xFFFFu)>>8);
    t|=0x94009300u;
    md2gba_write32(b,c->a[6],t);

    v=d0&0xFFFFu;
    t=0x96009500u|((v&0xFF00u)<<8)|(v&0x00FFu);
    md2gba_write32(b,c->a[6],t);
    md2gba_write16(b,c->a[6],0x977Fu);

    t=((d1<<2)&0xFFFFu)>>2;
    t|=0x4000u;
    md2gba_write16(b,c->a[6],(uint16_t)t);
    ef14=(uint16_t)(0x0080u|((d1>>14)&3u));
    md2gba_wram_write16_offset(b,0xEF14u,ef14);
    md2gba_write16(b,c->a[6],md2gba_wram_read16_offset(b,0xEF14u));

    t=(d3&0xFFFF0000u)|(d1&0xFFFFu);
    t=rol32_2(t);
    t=(t&0xFFFF0000u)|((t&0xFFFFu)>>2);
    t=(t>>16)|(t<<16);
    t=(t&0x7FFF0003u)|0x40000000u;
    md2gba_write32(b,c->a[6],t);

    a0+=a0;
    v=md2gba_read16(b,a0);
    md2gba_write16(b,c->a[5],(uint16_t)v);
    md2gba_wram_write8_offset(b,0xEF0Bu,(uint8_t)(md2gba_wram_read8_offset(b,0xEF0Bu)&0xEFu));
    ef0a=md2gba_wram_read16_offset(b,0xEF0Au);
    md2gba_write16(b,c->a[6],ef0a);
    md2gba_write16(b,0xA11100u,0u);

    /* Final CCR after LSR.W #2,D3 is later reduced by MOVE #0 to Z=1,
       while X survives.  ROL does not alter X; the LSR carry/X source is
       rotated bit 1 == pre-rotate D3 bit 31.  ANDI #$F8FF then clears IPL. */
    sr=(uint16_t)(c->sr & (uint16_t)~(0x0700u|MD68K_X|MD68K_N|MD68K_Z|MD68K_V|MD68K_C));
    sr|=MD68K_Z;
    if(d3&0x80000000u)sr|=MD68K_X;
    c->sr=sr;

    /* MOVEM.L (A7)+,D0-D3/A0 then RTS, preserving exact read order. */
    sp=c->a[7];
    c->d[0]=md2gba_read32(b,sp); sp+=4u;
    c->d[1]=md2gba_read32(b,sp); sp+=4u;
    c->d[2]=md2gba_read32(b,sp); sp+=4u;
    c->d[3]=md2gba_read32(b,sp); sp+=4u;
    c->a[0]=md2gba_read32(b,sp); sp+=4u;
    c->pc=md2gba_read32(b,sp)&0x00FFFFFFu; sp+=4u;
    c->a[7]=sp;

    *cost=286u; *steps=41u;
    return 1;
}
''')
    cfg={
      'source_span':{'start':f'0x{START:06X}','end':f'0x{END:06X}','sha256':EXPECT},
      'hooks':{
        '0x04209C':{
          'header':'md2gba_profile_fieldfast.h',
          'function':'md2gba_profile_fieldfast_4209c',
          'with_bus':True,
          'dynamic_next':True,
          'replace_hot':True
        }
      }
    }
    (a.out/'fieldfast_hooks.json').write_text(json.dumps(cfg,indent=2)+'\n')
    print(f'validated fieldfast span {START:06X}-{END-2:06X}: {got}')

if __name__=='__main__': main()
