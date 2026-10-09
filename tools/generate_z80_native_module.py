#!/usr/bin/env python3
"""Generate guarded native Z80 continuation cases from an observed RAM image/profile.

This is intentionally title-agnostic.  The input RAM snapshot and execution profile
are compiler inputs; the reusable runtime contains no title-specific Z80 PCs.
Unsupported opcodes are simply omitted and fall back to the canonical interpreter.
"""
from __future__ import annotations
import argparse, re
from pathlib import Path

PC_RE = re.compile(r"^PC\s+([0-9A-Fa-f]{1,4})\s+(\d+)\s*$")

# (length, cycles, emitted semantics).  Semantics may use p0/p1/p2 bytes and
# helper functions in md2gba_z80.c. Conditional instructions are emitted below.
SIMPLE = {
    0x00:(1,4, "z->pc=(uint16_t)(pc+1u);"),
    0x3E:(2,7, "z->a={p1};z->pc=(uint16_t)(pc+2u);"),
    0x32:(3,13,"zwrite(z,b,(uint16_t)({p1}|((uint16_t){p2}<<8)),z->a);z->pc=(uint16_t)(pc+3u);"),
    0x21:(3,10,"set_hl(z,(uint16_t)({p1}|((uint16_t){p2}<<8)));z->pc=(uint16_t)(pc+3u);"),
    0x1A:(1,7, "z->a=zread(z,b,rp_de(z));z->pc=(uint16_t)(pc+1u);"),
    0x36:(2,10,"zwrite(z,b,rp_hl(z),{p1});z->pc=(uint16_t)(pc+2u);"),
    0x23:(1,6, "set_hl(z,(uint16_t)(rp_hl(z)+1u));z->pc=(uint16_t)(pc+1u);"),
    0x77:(1,7, "zwrite(z,b,rp_hl(z),z->a);z->pc=(uint16_t)(pc+1u);"),
    0xAF:(1,4, "z->a=0u;z->f=szp(0u);z->pc=(uint16_t)(pc+1u);"),
    0x3A:(3,13,"z->a=zread(z,b,(uint16_t)({p1}|((uint16_t){p2}<<8)));z->pc=(uint16_t)(pc+3u);"),
    0xB7:(1,4, "z->f=szp(z->a);z->pc=(uint16_t)(pc+1u);"),
    0x13:(1,6, "set_de(z,(uint16_t)(rp_de(z)+1u));z->pc=(uint16_t)(pc+1u);"),
    0x0B:(1,6, "set_bc(z,(uint16_t)(rp_bc(z)-1u));z->pc=(uint16_t)(pc+1u);"),
    0x78:(1,4, "z->a=z->b;z->pc=(uint16_t)(pc+1u);"),
    0xB1:(1,4, "z->a=(uint8_t)(z->a|z->c);z->f=szp(z->a);z->pc=(uint16_t)(pc+1u);"),
    0xC3:(3,10,"z->pc=(uint16_t)({p1}|((uint16_t){p2}<<8));"),
    0x06:(2,7, "z->b={p1};z->pc=(uint16_t)(pc+2u);"),
    0x4F:(1,4, "z->c=z->a;z->pc=(uint16_t)(pc+1u);"),
    0x09:(1,11,"set_hl(z,add16_hl(z,rp_hl(z),rp_bc(z)));z->pc=(uint16_t)(pc+1u);"),
    0x7E:(1,7, "z->a=zread(z,b,rp_hl(z));z->pc=(uint16_t)(pc+1u);"),
    0x66:(1,7, "z->h=zread(z,b,rp_hl(z));z->pc=(uint16_t)(pc+1u);"),
    0x6F:(1,4, "z->l=z->a;z->pc=(uint16_t)(pc+1u);"),
    0xC1:(1,10,"set_bc(z,pop16(z,b));z->pc=(uint16_t)(pc+1u);"),
    0xE9:(1,4, "z->pc=rp_hl(z);"),
    0xC5:(1,11,"push16(z,b,rp_bc(z));z->pc=(uint16_t)(pc+1u);"),
    0xD6:(2,7, "z->a=sub8(z,z->a,{p1},0u);z->pc=(uint16_t)(pc+2u);"),
}

# unconditional byte guard expression
def guard(bs: bytes) -> str:
    terms=[f"b->z80_ram[pc+{i}u]==0x{x:02x}u" if i else f"b->z80_ram[pc]==0x{x:02x}u" for i,x in enumerate(bs)]
    return "&&".join(terms)

def case_for(pc:int, ram:bytes) -> tuple[str,int]|None:
    op=ram[pc]
    if op in SIMPLE:
        ln, cyc, sem=SIMPLE[op]
        bs=ram[pc:pc+ln]
        p=[ram[pc+i] if pc+i<len(ram) else 0 for i in range(3)]
        sem=sem.format(p1=f'0x{p[1]:02x}u',p2=f'0x{p[2]:02x}u')
        code=(f"case 0x{pc:04x}u: if(budget<{cyc}u||!({guard(bs)}))return 0u;"
              f"{sem} NATIVE_DONE(pc,0x{op:02x}u,{ln}u,{cyc}u);")
        return code, ln
    if op==0x20: # JR NZ,e 7/12
        rel=ram[pc+1]; target=(pc+2+((rel+128)%256-128))&0xffff
        bs=ram[pc:pc+2]
        code=(f"case 0x{pc:04x}u: if(!({guard(bs)}))return 0u;"
              f"if(z->f&F_Z){{if(budget<7u)return 0u;z->pc=0x{pc+2:04x}u;NATIVE_DONE(pc,0x20u,2u,7u);}}"
              f"else{{if(budget<12u)return 0u;z->pc=0x{target:04x}u;NATIVE_DONE(pc,0x20u,2u,12u);}}")
        return code,2
    if op==0xF2: # JP P,nn, fixed 10 cycles
        target=ram[pc+1]|(ram[pc+2]<<8); bs=ram[pc:pc+3]
        code=(f"case 0x{pc:04x}u: if(budget<10u||!({guard(bs)}))return 0u;"
              f"z->pc=(z->f&F_S)?0x{pc+3:04x}u:0x{target:04x}u;NATIVE_DONE(pc,0xf2u,3u,10u);")
        return code,3
    if op==0xFA: # JP M,nn
        target=ram[pc+1]|(ram[pc+2]<<8); bs=ram[pc:pc+3]
        code=(f"case 0x{pc:04x}u: if(budget<10u||!({guard(bs)}))return 0u;"
              f"z->pc=(z->f&F_S)?0x{target:04x}u:0x{pc+3:04x}u;NATIVE_DONE(pc,0xfau,3u,10u);")
        return code,3
    return None

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('snapshot', type=Path)
    ap.add_argument('profile', type=Path)
    ap.add_argument('-o','--output',type=Path,required=True)
    ap.add_argument('--min-count',type=int,default=1000)
    args=ap.parse_args()
    ram=args.snapshot.read_bytes()
    if len(ram)<0x2000: raise SystemExit('snapshot must contain at least 8192 bytes')
    counts={}
    for line in args.profile.read_text(errors='replace').splitlines():
        m=PC_RE.match(line)
        if m: counts[int(m.group(1),16)]=int(m.group(2))
    selected=[(pc,c) for pc,c in counts.items() if c>=args.min_count and pc<0x2000]
    selected.sort(key=lambda x:(-x[1],x[0]))
    cases=[]; skipped=[]
    for pc,c in selected:
        q=case_for(pc,ram)
        if q: cases.append((pc,c,q[0]))
        else: skipped.append((pc,c,ram[pc]))
    out=[]
    out.append('/* Auto-generated by tools/generate_z80_native_module.py. */')
    out.append('/* Input-specific PCs belong here, never in the reusable Z80 runtime. */')
    out.append(f'/* selected={len(selected)} emitted={len(cases)} skipped={len(skipped)} min_count={args.min_count} */')
    out.append('#define NATIVE_DONE(_pc,_op,_bytes,_cyc) do{z->last_pc=(uint16_t)(_pc);z->last_opcode=(uint8_t)(_op);z->last_prefix=0u;z80_r_advance(z,(_bytes));*guest=1u;return (_cyc);}while(0)')
    out.append('static __attribute__((noinline)) unsigned md2gba_z80_native_step_generated(Md2GbaZ80*z,Md2GbaBus*b,unsigned budget,unsigned*guest){')
    out.append('    unsigned pc=z->pc; if(pc>=0x2000u)return 0u; switch(pc){')
    for pc,c,code in cases:
        out.append(f'        /* profile {c} */ {code}')
    out.append('        default:return 0u;')
    out.append('    }')
    out.append('}')
    out.append('#undef NATIVE_DONE')
    if skipped:
        out.append('/* Skipped hot PCs/opcodes; canonical interpreter remains fallback:')
        for pc,c,op in skipped: out.append(f'   {pc:04x} count={c} opcode={op:02x}')
        out.append('*/')
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text('\n'.join(out)+'\n')
    print(f'emitted {len(cases)}/{len(selected)} hot PCs -> {args.output}')
    if skipped: print('skipped:', ' '.join(f'{pc:04x}:{op:02x}' for pc,_,op in skipped))

if __name__=='__main__': main()
