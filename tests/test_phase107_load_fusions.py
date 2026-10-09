#!/usr/bin/env python3
"""Differential tests for P107 scheduler-safe load/decode superinstructions."""
import sys,subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1];sys.path.insert(0,str(root/'tools'))
from raw68k_core import decode
from aot68k_loop_fusion import (nemesis_table_token_setup,emit_nemesis_table_token_setup,
                                byte_to_nibble_row_loop,emit_byte_to_nibble_row_loop)
srcrom=(root/'inputs/ps4built.bin').read_bytes()

def relocated(start,end,base=0x100):
    code=srcrom[start:end]
    rom=bytes(base)+code+bytes(0x500)
    ins=[];pc=base
    while pc<base+len(code):
        q=decode(rom,pc);ins.append(q);pc+=q['size']
    return rom,ins,pc

# Table token setup: original 041798..0417c2, relocated.
rom,ins,end=relocated(0x41798,0x417c2)
m=nemesis_table_token_setup(ins,0);assert m and m['next']==end,(m,end,ins)
gen=emit_nemesis_table_token_setup(m)
out=root/'analysis/test_p107_nem_token.c';out.parent.mkdir(exist_ok=True)
out.write_text('''#include <stdint.h>\n#include <stdio.h>\n#include <string.h>\n#include "md2gba_interp68k.h"\n#include "md2gba_cycles.h"\n#include "md2gba_aot68k.h"\nstatic uint8_t rom[]={'''+','.join(str(x) for x in rom)+'''};static unsigned seed=0x10741798u;static unsigned rnd(void){seed=seed*1664525u+1013904223u;return seed;}\nstatic unsigned fused(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;\n'''+gen+f'''L_{end:06X}: *budget=left;return steps;\n}}\nint main(void){{for(unsigned t=0;t<30000;t++){{Md2GbaBus ba,bb;md2gba_bus_init(&ba,rom,sizeof(rom));md2gba_bus_init(&bb,rom,sizeof(rom));Md2Gba68k a={{0}},b;for(unsigned i=0;i<8;i++){{a.d[i]=rnd();a.a[i]=rnd();}}a.pc=0x100u;a.sr=(uint16_t)rnd();a.a[1]=0xff1000u;a.a[0]=0xff3000u;unsigned ix=rnd()&255u;unsigned delta=(rnd()%17u)-8u,ent=rnd()&255u,src=rnd()&255u;a.d[1]=(a.d[1]&0xffff0000u)|ix;a.d[6]=(a.d[6]&0xffff0000u)|(rnd()%24u);ba.wram[0x1000u+ix*2u]=(uint8_t)delta;bb.wram[0x1000u+ix*2u]=(uint8_t)delta;ba.wram[0x1001u+ix*2u]=(uint8_t)ent;bb.wram[0x1001u+ix*2u]=(uint8_t)ent;ba.wram[0x3000u]=(uint8_t)src;bb.wram[0x3000u]=(uint8_t)src;b=a;unsigned left=97u+(rnd()%500u),orig=left,bcy=0,steps=fused(&a,&ba,&left);if(!steps){{printf("NOFUSE %u\\n",t);return 2;}}for(unsigned i=0;i<steps;i++){{unsigned before=b.pc;if(md68k_interp_step(&b,&bb)!=1)return 3;bcy+=md68k_estimate_cycles(&bb,before,b.pc);}}if(memcmp(&a,&b,sizeof(a))||memcmp(ba.wram,bb.wram,sizeof(ba.wram))||bcy!=orig-left){{printf("FAIL tok t=%u steps=%u cyc=%u/%u pc=%x/%x sr=%x/%x d0=%x/%x d1=%x/%x d5=%x/%x d6=%x/%x a0=%x/%x\\n",t,steps,bcy,orig-left,a.pc,b.pc,a.sr,b.sr,a.d[0],b.d[0],a.d[1],b.d[1],a.d[5],b.d[5],a.d[6],b.d[6],a.a[0],b.a[0]);return 1;}}}}puts("PASS 30,000 Nemesis table-token states: registers, bus reads, CCR, PC, steps and cycles");return 0;}}\n''')
subprocess.run(['cc','-O2','-ffunction-sections','-fdata-sections','-I.','-Iruntime/include','-Iruntime/generated',str(out),'md2gba_bus.c','md2gba_aot68k.c','runtime/src/md2gba_interp68k.c','runtime/src/md2gba_cycles.c','runtime/generated/md2gba_interp_opclass.c','-Wl,--gc-sections','-o','analysis/test_p107_nem_token'],cwd=root,check=True)
subprocess.run(['analysis/test_p107_nem_token'],cwd=root,check=True)

# Complete byte->nibble row iteration: original 06ac72..06ac92, relocated.
rom2,ins2,end2=relocated(0x6ac72,0x6ac92)
m2=byte_to_nibble_row_loop(ins2,0);assert m2 and m2['next']==end2,(m2,end2)
gen2=emit_byte_to_nibble_row_loop(m2)
out2=root/'analysis/test_p107_bitmap_row.c'
out2.write_text('''#include <stdint.h>\n#include <stdio.h>\n#include <string.h>\n#include "md2gba_interp68k.h"\n#include "md2gba_cycles.h"\n#include "md2gba_aot68k.h"\nstatic uint8_t rom[]={'''+','.join(str(x) for x in rom2)+'''};static unsigned seed=0x1076ac72u;static unsigned rnd(void){seed=seed*1103515245u+12345u;return seed;}\nstatic unsigned fused(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;\n'''+gen2+f'''L_000100: *budget=left;return steps;\nL_{end2:06X}: *budget=left;return steps;\n}}\nint main(void){{for(unsigned t=0;t<30000;t++){{Md2GbaBus ba,bb;md2gba_bus_init(&ba,rom,sizeof(rom));md2gba_bus_init(&bb,rom,sizeof(rom));Md2Gba68k a={{0}},b;for(unsigned i=0;i<8;i++){{a.d[i]=rnd();a.a[i]=rnd();}}a.pc=0x100u;a.sr=(uint16_t)rnd();a.a[0]=0xff2000u;a.a[1]=0xff3000u;unsigned byte=rnd()&255u;ba.wram[0x2000]=byte;bb.wram[0x2000]=byte;a.d[0]=(a.d[0]&0xffff0000u)|(rnd()&15u);a.d[1]=(a.d[1]&0xffff0000u)|(rnd()&15u);a.d[7]=(a.d[7]&0xffff0000u)|(rnd()%8u);b=a;unsigned left=451u+(rnd()%300u),orig=left,bcy=0,steps=fused(&a,&ba,&left);if(!steps){{printf("NOFUSE row %u\\n",t);return 2;}}for(unsigned i=0;i<steps;i++){{unsigned before=b.pc;if(md68k_interp_step(&b,&bb)!=1)return 3;bcy+=md68k_estimate_cycles(&bb,before,b.pc);}}if(memcmp(&a,&b,sizeof(a))||memcmp(ba.wram,bb.wram,sizeof(ba.wram))||bcy!=orig-left){{printf("FAIL row t=%u steps=%u cyc=%u/%u pc=%x/%x sr=%x/%x d3=%x/%x d4=%x/%x d5=%x/%x d7=%x/%x a0=%x/%x a1=%x/%x\\n",t,steps,bcy,orig-left,a.pc,b.pc,a.sr,b.sr,a.d[3],b.d[3],a.d[4],b.d[4],a.d[5],b.d[5],a.d[7],b.d[7],a.a[0],b.a[0],a.a[1],b.a[1]);return 1;}}}}puts("PASS 30,000 bitmap-row states: output bytes, registers, CCR, PC, steps and cycles");return 0;}}\n''')
subprocess.run(['cc','-O2','-ffunction-sections','-fdata-sections','-I.','-Iruntime/include','-Iruntime/generated',str(out2),'md2gba_bus.c','md2gba_aot68k.c','runtime/src/md2gba_interp68k.c','runtime/src/md2gba_cycles.c','runtime/generated/md2gba_interp_opclass.c','-Wl,--gc-sections','-o','analysis/test_p107_bitmap_row'],cwd=root,check=True)
subprocess.run(['analysis/test_p107_bitmap_row'],cwd=root,check=True)
