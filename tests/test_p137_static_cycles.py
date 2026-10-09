#!/usr/bin/env python3
from pathlib import Path
import re, sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from raw68k_core import decode
from emit_aot_sharded import _fastchain_cycle_expr

# Parse current generated opcode class table and enum names.
ct=(ROOT/'runtime/generated/md2gba_interp_opclass.c').read_text()
body=ct.split('{',1)[1].rsplit('}',1)[0]
vals=[int(x) for x in re.findall(r'\b\d+\b',body)]
if len(vals)!=65536:
    raise SystemExit(f'bad opclass table length {len(vals)}')
h=(ROOT/'runtime/generated/md2gba_interp_opclass.h').read_text()
enum={m.group(1):int(m.group(2)) for m in re.finditer(r'\b(MD68KI_[A-Z0-9_]+)\s*=\s*(\d+)',h)}

def rcycle(op, cl, pc, nxt):
    mode=(op>>3)&7
    def E(n): return enum[n]
    if cl in {E('MD68KI_NOP'),E('MD68KI_MOVEQ'),E('MD68KI_EXT'),E('MD68KI_SWAP'),E('MD68KI_EXG')}: return 4
    if cl in {E('MD68KI_BRA'),E('MD68KI_BSR'),E('MD68KI_BCC')}:
        fall=(pc+(2 if (op&0xff) else 4))&0xffffff
        return 8 if cl==E('MD68KI_BCC') and nxt==fall else 10
    if cl==E('MD68KI_DBCC'): return 10
    if cl==E('MD68KI_RTS'): return 16
    if cl==E('MD68KI_RTE'): return 20
    if cl==E('MD68KI_TRAP'): return 34
    if cl==E('MD68KI_JSR'): return 18
    if cl==E('MD68KI_JMP'): return 10
    if cl in {E('MD68KI_MULU'),E('MD68KI_MULS')}: return 54
    if cl in {E('MD68KI_DIVU'),E('MD68KI_DIVS')}: return 140
    if cl in {E('MD68KI_MOVEM_FROM_REGS'),E('MD68KI_MOVEM_TO_REGS')}: return 16
    if cl in {E('MD68KI_MOVE'),E('MD68KI_MOVEA'),E('MD68KI_ADDQ'),E('MD68KI_SUBQ')}: return 4 if mode<=1 else 8
    if cl==E('MD68KI_SHIFT_REG'): return 8
    if cl in {E('MD68KI_ASR_MEM'),E('MD68KI_ASL_MEM'),E('MD68KI_LSR_MEM'),E('MD68KI_LSL_MEM'),E('MD68KI_ROXR_MEM'),E('MD68KI_ROXL_MEM'),E('MD68KI_ROR_MEM'),E('MD68KI_ROL_MEM')}: return 12
    return 6 if mode<=1 else 10

def scycle(ins, fall, nextpc):
    s=_fastchain_cycle_expr(ins,fall)
    m=re.fullmatch(r'(\d+)u',s)
    if m:return int(m.group(1))
    m=re.fullmatch(r'\(\(c->pc==0x([0-9A-F]+)u\)\?8u:10u\)',s)
    if m:return 8 if nextpc==int(m.group(1),16) else 10
    raise RuntimeError(f'unhandled expr {s}')

decoded=checks=0
for op in range(65536):
    # extension bytes zeroed; decode only needs a bounded instruction window.
    blob=bytes([op>>8,op&255])+bytes(14)
    try: ins=decode(blob,0)
    except Exception: continue
    decoded+=1
    fall=int(ins['size'])&0xffffff
    cl=vals[op]
    # Unsupported class should not have decoded successfully in canonical table.
    if cl==enum['MD68KI_UNSUPPORTED']:
        print(f'FAIL decoded unsupported opcode {op:04X}: {ins.get("mnemonic")}')
        raise SystemExit(1)
    for nextpc in (fall,(fall+2)&0xffffff):
        a=scycle(ins,fall,nextpc); b=rcycle(op,cl,0,nextpc);checks+=1
        if a!=b:
            print(f'FAIL op={op:04X} m={ins.get("mnemonic")} class={cl} size={ins["size"]} next={nextpc:06X} static={a} runtime={b} expr={_fastchain_cycle_expr(ins,fall)}')
            raise SystemExit(1)
print(f'PASS P137 static cycle equivalence: decoded={decoded} checks={checks}')
