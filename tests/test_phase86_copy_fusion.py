#!/usr/bin/env python3
"""Differential test for generic MOVE (An)+,(Am)[+] / DBF fusion."""
import sys, subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(root/'tools'))
from raw68k_core import decode
from aot68k_loop_fusion import counted_copy_loop,emit_counted_copy_loop

chunks=['''#include <stdint.h>\n#include <stdio.h>\n#include <string.h>\n#include "md2gba_interp68k.h"\n#include "md2gba_cycles.h"\nstatic uint8_t rom[4096];static Md2GbaBus busa,busb;\nstatic unsigned seed=0x86c0ffeeu;static unsigned rnd(void){seed=seed*1664525u+1013904223u;return seed;}\n''']
entries=[]
# Exercise ordinary registers plus A7 byte-postincrement semantics on either side.
for sz,top in [(1,1),(2,3),(4,2)]:
    for post in (0,1):
        for sr,dr in ((0,1),(0,7),(7,1),(7,0)):
            op=(top<<12)|(dr<<9)|((3 if post else 2)<<6)|(3<<3)|sr
            dbop=0x51c8|6
            code=op.to_bytes(2,'big')+dbop.to_bytes(2,'big')+(-4 & 0xffff).to_bytes(2,'big')
            data=bytes(256)+code+bytes(10)
            ins=[decode(data,256),decode(data,258)]
            m=counted_copy_loop(ins,0);assert m,(sz,post,sr,dr,ins)
            idx=len(entries);entries.append((idx,code,sr,dr,post))
            chunks.append('static unsigned fused%d(Md2Gba68k*c,Md2GbaBus*b,unsigned *budget){unsigned steps=0,left=*budget;\n'%idx+emit_counted_copy_loop(m)+'''L_000100:\nL_000106:\n*budget=left;return steps;}\n''')
chunks.append('static unsigned (*funcs[])(Md2Gba68k*,Md2GbaBus*,unsigned*)={'+','.join('fused%d'%i for i,_,_,_,_ in entries)+'};\n')
chunks.append('static const uint8_t codes[][6]={'+','.join('{'+','.join(map(str,c))+'}' for _,c,_,_,_ in entries)+'};\n')
chunks.append('static const unsigned sregs[]={'+','.join(str(s) for _,_,s,_,_ in entries)+'};\n')
chunks.append('static const unsigned dregs[]={'+','.join(str(d) for _,_,_,d,_ in entries)+'};\n')
chunks.append('static const unsigned dposts[]={'+','.join(str(p) for _,_,_,_,p in entries)+'};\n')
chunks.append(r'''
static void fill_bus(Md2GbaBus*b){
 md2gba_bus_init(b,rom,sizeof(rom));b->vdp.regs[15]=2;
 for(unsigned i=0;i<sizeof(b->wram);i++)b->wram[i]=(uint8_t)rnd();
 for(unsigned i=0;i<sizeof(b->vdp.vram);i++)b->vdp.vram[i]=(uint8_t)rnd();
}
int main(void){
 for(unsigned i=0;i<sizeof(rom);i++)rom[i]=(uint8_t)(i*37u+11u);
 for(unsigned kind=0;kind<24;kind++)for(unsigned t=0;t<400;t++){
  memcpy(rom+256,codes[kind],6);Md2Gba68k a={0},b;fill_bus(&busa);
  for(unsigned i=0;i<8;i++){a.d[i]=rnd();a.a[i]=rnd();}
  unsigned srcs[]={0xff1000u,0xff1000u,0xff1002u,0x000300u,0xfffffcu,0xff2200u,0xc00000u};
  unsigned dsts[]={0xff2200u,0xff1002u,0xff1000u,0xff3300u,0xff4400u,0xc00000u,0xff5500u};
  unsigned c=t%7u,sr=sregs[kind],dr=dregs[kind];
  a.a[sr]=srcs[c];a.a[dr]=dsts[c];
  /* Postincrement through the VDP aperture is legal to emulate but not a useful
     stream shape, so keep the VDP destination fixed for those vectors. */
  if(dposts[kind] && dsts[c]==0xc00000u)a.a[dr]=0xff6600u;
  a.d[6]=(a.d[6]&0xffff0000u)|(rnd()%65u);a.pc=256;a.sr=(uint16_t)rnd();
  b=a;busb=busa;unsigned left=1u+rnd()%1200u,original=left,bcy=0;
  unsigned steps=funcs[kind](&a,&busa,&left);
  for(unsigned i=0;i<steps;i++){unsigned pc=b.pc;if(md68k_interp_step(&b,&busb)!=1)return 2;bcy+=md68k_estimate_cycles(&busb,pc,b.pc);}
  if(memcmp(&a,&b,sizeof(a))||memcmp(&busa,&busb,sizeof(busa))||bcy!=original-left){
   printf("FAIL kind=%u case=%u steps=%u cycles=%u/%u pc=%x/%x sr=%x/%x sa=%x/%x da=%x/%x\n",kind,t,steps,bcy,original-left,a.pc,b.pc,a.sr,b.sr,a.a[sr],b.a[sr],a.a[dr],b.a[dr]);return 1;
  }
 }
 puts("PASS 9600 copy loops: byte/word/long, A7, overlap, wrap, ROM/WRAM/VDP effects, state/steps/cycles/deadline");return 0;
}
''')
out=root/'analysis/test_copy_fusion.c';out.parent.mkdir(exist_ok=True);out.write_text(''.join(chunks))
subprocess.run(['cc','-O2','-ffunction-sections','-fdata-sections','-I.','-Iruntime/include','-Iruntime/generated',str(out),'md2gba_bus.c','md2gba_aot68k.c','runtime/src/md2gba_interp68k.c','runtime/src/md2gba_cycles.c','runtime/generated/md2gba_interp_opclass.c','-Wl,--gc-sections','-o','analysis/test_copy_fusion'],cwd=root,check=True)
subprocess.run(['analysis/test_copy_fusion'],cwd=root,check=True)
