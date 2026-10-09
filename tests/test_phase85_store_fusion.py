#!/usr/bin/env python3
"""Compare generated counted-store loops to the independent 68K interpreter."""
import sys,subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1];sys.path.insert(0,str(root/'tools'))
from raw68k_core import decode
from aot68k_loop_fusion import counted_store_loop,emit_counted_store_loop
chunks=['''#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "md2gba_interp68k.h"
#include "md2gba_cycles.h"
static uint8_t rom[1024];static Md2GbaBus busa,busb;
static unsigned seed=0x1457;static unsigned rnd(void){seed=seed*1664525u+1013904223u;return seed;}
'''];entries=[]
for sz,top in [(1,1),(2,3),(4,2)]:
 for post in (0,1):
  for ar in (0,7):
   op=(top<<12)|(ar<<9)|((3 if post else 2)<<6)|2
   code=op.to_bytes(2,'big')+bytes.fromhex('51cffffc');data=bytes(256)+code+bytes(10)
   m=counted_store_loop([decode(data,256),decode(data,258)],0);assert m
   idx=len(entries);entries.append((idx,code,ar))
   chunks.append('static unsigned fused%d(Md2Gba68k*c,Md2GbaBus*b,unsigned *budget){unsigned steps=0,left=*budget;\n'%idx+emit_counted_store_loop(m)+'''L_000100:
L_000106:
*budget=left;return steps;}
''')
chunks.append('static unsigned (*funcs[])(Md2Gba68k*,Md2GbaBus*,unsigned*)={'+','.join('fused%d'%i for i,_,_ in entries)+'};\n')
chunks.append('static const uint8_t codes[][6]={'+','.join('{'+','.join(map(str,c))+'}' for _,c,_ in entries)+'};\n')
chunks.append('static const unsigned aregs[]={'+','.join(str(a)for _,_,a in entries)+'};\n')
chunks.append('''int main(void){for(unsigned kind=0;kind<12;kind++)for(unsigned t=0;t<600;t++){
 Md2Gba68k a={0},b;unsigned left=1+rnd()%1200,bcy=0;memcpy(rom+256,codes[kind],6);md2gba_bus_init(&busa,rom,sizeof(rom));
 for(unsigned i=0;i<8;i++){a.d[i]=rnd();a.a[i]=rnd();}unsigned addresses[]={0xff1000,0xfffffc,0xff0001,0xc00000,0xc00004,0xff7ffa};
 a.a[aregs[kind]]=addresses[t%6];a.d[7]=(a.d[7]&0xffff0000u)|(rnd()%65);a.pc=256;a.sr=rnd();busa.vdp.regs[15]=2;b=a;busb=busa;unsigned original=left,steps=funcs[kind](&a,&busa,&left);
 for(unsigned i=0;i<steps;i++){unsigned pc=b.pc;if(md68k_interp_step(&b,&busb)!=1)return 2;bcy+=md68k_estimate_cycles(&busb,pc,b.pc);}
 if(memcmp(&a,&b,sizeof(a))||memcmp(&busa,&busb,sizeof(busa))||bcy!=original-left){printf("FAIL kind=%u case=%u steps=%u cycles=%u/%u pc=%x/%x sr=%x/%x\\n",kind,t,steps,bcy,original-left,a.pc,b.pc,a.sr,b.sr);return 1;}}
 puts("PASS 7200 store loops: byte/word/long, A7, address wrap, WRAM/VDP effects, state/steps/cycles/deadline");return 0;}
''')
out=root/'analysis/test_store_fusion.c';out.write_text(''.join(chunks))
subprocess.run(['cc','-O2','-ffunction-sections','-fdata-sections','-I.','-Iruntime/include','-Iruntime/generated',str(out),'md2gba_bus.c','md2gba_aot68k.c','runtime/src/md2gba_interp68k.c','runtime/src/md2gba_cycles.c','runtime/generated/md2gba_interp_opclass.c','-Wl,--gc-sections','-o','analysis/test_store_fusion'],cwd=root,check=True)
subprocess.run(['analysis/test_store_fusion'],cwd=root,check=True)
