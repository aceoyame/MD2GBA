#!/usr/bin/env python3
"""Differential test for the generic fast-arm nibble accumulator fusion."""
import sys,subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1];sys.path.insert(0,str(root/'tools'))
from raw68k_core import decode
from aot68k_loop_fusion import nibble_accumulate_loop,emit_nibble_accumulate_loop
code=bytes.fromhex('e98c8801534366064ed37800760851c8fff0')
rom=bytes(256)+code+bytes(16);ins=[];pc=256
while pc<256+len(code):q=decode(rom,pc);ins.append(q);pc+=q['size']
m=nibble_accumulate_loop(ins,0);assert m and m['next']==274,(m,pc,ins)
gen=emit_nibble_accumulate_loop(m)
out=root/'analysis/test_nibble_fusion.c';out.parent.mkdir(exist_ok=True)
out.write_text('''#include <stdint.h>\n#include <stdio.h>\n#include <string.h>\n#include "md2gba_interp68k.h"\n#include "md2gba_cycles.h"\nstatic uint8_t rom[]={'''+','.join(str(x) for x in rom)+'''};static Md2GbaBus bus;static unsigned seed=0x86b17u;static unsigned rnd(void){seed=seed*1664525u+1013904223u;return seed;}\nstatic unsigned fused(Md2Gba68k*c,unsigned*budget){unsigned steps=0,left=*budget;\n'''+gen+'''L_000100:\nL_000112:\n*budget=left;return steps;}\nint main(void){md2gba_bus_init(&bus,rom,sizeof(rom));for(unsigned t=0;t<50000;t++){Md2Gba68k a={0},b;for(unsigned i=0;i<8;i++){a.d[i]=rnd();a.a[i]=rnd();}a.pc=256;a.sr=(uint16_t)rnd();unsigned chunk=rnd()%10u,rem=1u+rnd()%24u;a.d[3]=(a.d[3]&0xffff0000u)|chunk;a.d[0]=(a.d[0]&0xffff0000u)|(rem-1u);b=a;unsigned left=1u+rnd()%700u,orig=left,bcy=0,steps=fused(&a,&left);for(unsigned i=0;i<steps;i++){unsigned before=b.pc;if(md68k_interp_step(&b,&bus)!=1)return 2;bcy+=md68k_estimate_cycles(&bus,before,b.pc);}if(memcmp(&a,&b,sizeof(a))||bcy!=orig-left){printf("FAIL t=%u chunk=%u rem=%u steps=%u cycles=%u/%u pc=%x/%x sr=%x/%x d0=%x/%x d3=%x/%x d4=%x/%x\\n",t,chunk,rem,steps,bcy,orig-left,a.pc,b.pc,a.sr,b.sr,a.d[0],b.d[0],a.d[3],b.d[3],a.d[4],b.d[4]);return 1;}}puts("PASS 50,000 nibble fast-arm states: seam avoidance, registers, CCR/X, PC, steps, cycles and deadlines");return 0;}\n''')
subprocess.run(['cc','-O2','-ffunction-sections','-fdata-sections','-I.','-Iruntime/include','-Iruntime/generated',str(out),'md2gba_bus.c','md2gba_aot68k.c','runtime/src/md2gba_interp68k.c','runtime/src/md2gba_cycles.c','runtime/generated/md2gba_interp_opclass.c','-Wl,--gc-sections','-o','analysis/test_nibble_fusion'],cwd=root,check=True)
subprocess.run(['analysis/test_nibble_fusion'],cwd=root,check=True)
