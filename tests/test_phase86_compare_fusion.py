#!/usr/bin/env python3
"""Differential test for generic CMPM/DBNE equal-prefix scan fusion."""
import sys,subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1];sys.path.insert(0,str(root/'tools'))
from raw68k_core import decode
from aot68k_loop_fusion import counted_compare_loop,emit_counted_compare_loop
chunks=['''#include <stdint.h>\n#include <stdio.h>\n#include <string.h>\n#include "md2gba_interp68k.h"\n#include "md2gba_cycles.h"\nstatic uint8_t rom[4096];static Md2GbaBus busa,busb;\nstatic unsigned seed=0x86c09a11u;static unsigned rnd(void){seed=seed*1664525u+1013904223u;return seed;}\n''']
entries=[]
for sz,opmode in [(1,4),(2,5),(4,6)]:
    for sr,dr in ((0,1),(0,7),(7,1)):
        op=0xB000|(dr<<9)|(opmode<<6)|(1<<3)|sr
        dbop=0x50C8|(6<<8)|6
        code=op.to_bytes(2,'big')+dbop.to_bytes(2,'big')+(-4&0xffff).to_bytes(2,'big')
        data=bytes(256)+code+bytes(8);ins=[decode(data,256),decode(data,258)]
        m=counted_compare_loop(ins,0);assert m,(sz,sr,dr,ins)
        idx=len(entries);entries.append((idx,code,sr,dr,sz,m['sinc'],m['dinc']))
        chunks.append('static unsigned fused%d(Md2Gba68k*c,Md2GbaBus*b,unsigned *budget){unsigned steps=0,left=*budget;\n'%idx+emit_counted_compare_loop(m)+'''L_000100:\nL_000106:\n*budget=left;return steps;}\n''')
# Same address register must remain unfused because CMPM resolves its two EAs sequentially.
op=0xB000|(4<<9)|(5<<6)|(1<<3)|4;code=op.to_bytes(2,'big')+(0x56ce).to_bytes(2,'big')+(-4&0xffff).to_bytes(2,'big');data=bytes(256)+code+bytes(8)
assert counted_compare_loop([decode(data,256),decode(data,258)],0) is None
chunks.append('static unsigned (*funcs[])(Md2Gba68k*,Md2GbaBus*,unsigned*)={'+','.join('fused%d'%i for i,*_ in entries)+'};\n')
for name,pos in [('sregs',2),('dregs',3),('sizes',4),('sincs',5),('dincs',6)]:chunks.append('static const unsigned '+name+'[]={'+','.join(str(e[pos]) for e in entries)+'};\n')
chunks.append('static const uint8_t codes[][6]={'+','.join('{'+','.join(map(str,e[1]))+'}' for e in entries)+'};\n')
chunks.append(r'''
static void put_guest(Md2GbaBus*b,uint32_t a,unsigned w,uint32_t v){a&=0xffffffu;uint8_t*q;if(a>=0xff0000u)q=b->wram+(a&0xffffu);else q=rom+a;if(w==1u)q[0]=(uint8_t)v;else if(w==2u){q[0]=(uint8_t)(v>>8);q[1]=(uint8_t)v;}else{q[0]=(uint8_t)(v>>24);q[1]=(uint8_t)(v>>16);q[2]=(uint8_t)(v>>8);q[3]=(uint8_t)v;}}
int main(void){
 for(unsigned kind=0;kind<9;kind++)for(unsigned t=0;t<1000;t++){
  for(unsigned i=0;i<sizeof(rom);i++)rom[i]=(uint8_t)(i*19u+3u);memcpy(rom+256,codes[kind],6);md2gba_bus_init(&busa,rom,sizeof(rom));
  for(unsigned i=0;i<sizeof(busa.wram);i++)busa.wram[i]=(uint8_t)rnd();Md2Gba68k a={0},b;for(unsigned i=0;i<8;i++){a.d[i]=rnd();a.a[i]=rnd();}
  unsigned rem=1u+rnd()%33u,scenario=t%2u;uint32_t sa=scenario?0x000300u:0xff1000u,da=scenario?0xff3000u:0xff5000u;unsigned w=sizes[kind];
  a.a[sregs[kind]]=sa;a.a[dregs[kind]]=da;a.d[6]=(a.d[6]&0xffff0000u)|(rem-1u);a.pc=256;a.sr=(uint16_t)rnd();
  unsigned mismatch=(t%5u)==0u?(rnd()%rem):rem;
  for(unsigned i=0;i<rem;i++){uint32_t v=rnd();put_guest(&busa,sa+i*sincs[kind],w,v);put_guest(&busa,da+i*dincs[kind],w,v^(i==mismatch?1u:0u));}
  b=a;busb=busa;unsigned left=1u+rnd()%800u,original=left,bcy=0,steps=funcs[kind](&a,&busa,&left);
  for(unsigned i=0;i<steps;i++){unsigned pc=b.pc;if(md68k_interp_step(&b,&busb)!=1)return 2;bcy+=md68k_estimate_cycles(&busb,pc,b.pc);}
  if(memcmp(&a,&b,sizeof(a))||memcmp(&busa,&busb,sizeof(busa))||bcy!=original-left){printf("FAIL kind=%u case=%u steps=%u cycles=%u/%u pc=%x/%x sr=%x/%x d6=%x/%x\n",kind,t,steps,bcy,original-left,a.pc,b.pc,a.sr,b.sr,a.d[6],b.d[6]);return 1;}
 }
 puts("PASS 9000 CMPM/DBNE scans: byte/word/long, A7, ROM/WRAM, equal/mismatch exits, state/CCR/steps/cycles/deadline; alias rejected");return 0;
}
''')
out=root/'analysis/test_compare_fusion.c';out.write_text(''.join(chunks))
subprocess.run(['cc','-O2','-ffunction-sections','-fdata-sections','-I.','-Iruntime/include','-Iruntime/generated',str(out),'md2gba_bus.c','md2gba_aot68k.c','runtime/src/md2gba_interp68k.c','runtime/src/md2gba_cycles.c','runtime/generated/md2gba_interp_opclass.c','-Wl,--gc-sections','-o','analysis/test_compare_fusion'],cwd=root,check=True)
subprocess.run(['analysis/test_compare_fusion'],cwd=root,check=True)
