#!/usr/bin/env python3
"""Execute the generated fused operation against the independent 68K interpreter."""
import sys,subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(root/'tools'))
from raw68k_core import decode
from aot68k_loop_fusion import expand_bits_loop,emit_expand_bits_loop
code=bytes.fromhex('e98d090667048a4060028a41534451cbfff0')
rom=bytes(256)+code+bytes(16)
ins=[];pc=256
while pc<256+len(code):
    q=decode(rom,pc);ins.append(q);pc+=q['size']
match=expand_bits_loop(ins,0)
assert match and match['next']==pc
bad=[dict(i) for i in ins];bad[2]['target']+=2
assert expand_bits_loop(bad,0) is None
generated=emit_expand_bits_loop(match)
out=root/'analysis/test_loop_fusion.c'
out.write_text('''#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "md2gba_interp68k.h"
#include "md2gba_cycles.h"
static uint8_t rom[]={'''+','.join(str(x) for x in rom)+'''};
static Md2GbaBus bus;
static uint32_t rng=93123;
static uint32_t rnd(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static int fused(Md2Gba68k*c,unsigned*step_out,unsigned*cyc_out){
 unsigned steps=0,left=1000;
'''+generated+f'''
 return 0;
 L_{pc:06X}: *step_out=steps;*cyc_out=1000-left;return 1;
}}
int main(void){{md2gba_bus_init(&bus,rom,sizeof(rom));
 for(unsigned k=0;k<50000;k++){{Md2Gba68k a={{0}},b={{0}};unsigned st=0,cy=0,bst=0,bcy=0;
  for(unsigned i=0;i<8;i++){{a.d[i]=rnd();a.a[i]=rnd();}}a.sr=rnd();a.pc=256;a.usp=rnd();
  unsigned n=1+(k%8),bit=n-1+(rnd()%(33-n));
  a.d[3]=(a.d[3]&0xffff0000u)|(n-1);a.d[4]=(a.d[4]&0xffff0000u)|bit;
  a.d[0]=(a.d[0]&0xffff0000u)|(rnd()&15);a.d[1]=(a.d[1]&0xffff0000u)|(rnd()&15);
  b=a;if(!fused(&a,&st,&cy))return 2;
  while(b.pc!={pc}){{unsigned before=b.pc;if(md68k_interp_step(&b,&bus)!=1)return 3;bst++;bcy+=md68k_estimate_cycles(&bus,before,b.pc);if(bst>100)return 4;}}
  if(memcmp(&a,&b,sizeof(a))||st!=bst||cy!=bcy){{printf("FAIL %u sr %x %x steps %u %u cycles %u %u\\n",k,a.sr,b.sr,st,bst,cy,bcy);return 1;}}
 }}
 puts("PASS 50,000 relocated bitmap-loop states, CCR, PC, register, step and cycle equivalence");return 0;
}}
''')
subprocess.run(['cc','-O2','-ffunction-sections','-fdata-sections','-I.','-Iruntime/include','-Iruntime/generated',str(out),
 'md2gba_bus.c','md2gba_aot68k.c','runtime/src/md2gba_interp68k.c','runtime/src/md2gba_cycles.c','runtime/generated/md2gba_interp_opclass.c',
 '-Wl,--gc-sections','-o','analysis/test_loop_fusion'],cwd=root,check=True)
subprocess.run(['analysis/test_loop_fusion'],cwd=root,check=True)
