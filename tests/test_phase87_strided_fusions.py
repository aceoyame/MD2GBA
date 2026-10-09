#!/usr/bin/env python3
"""Differentially validate restored generic strided loop fusions."""
import json,sys,subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1];sys.path.insert(0,str(root/'tools'))
from raw68k_core import decode,with_bytes
from emit_aot_region import collect_pcs
from aot68k_loop_fusion import (strided_move_loop,emit_strided_move_loop,strided_clear_loop,emit_strided_clear_loop,strided_pair_store_loop,emit_strided_pair_store_loop)
rom=(root/'inputs/ps4built.bin').read_bytes(); pcs=collect_pcs(json.loads((root/'ps4_pcs.json').read_text())); ins=[with_bytes(rom,decode(rom,pc)) for pc in sorted(set(pcs))]
classes=[('move',strided_move_loop,emit_strided_move_loop,3),('clear',strided_clear_loop,emit_strided_clear_loop,3),('pair',strided_pair_store_loop,emit_strided_pair_store_loop,4)]
entries=[]; chunks=['''#include <stdint.h>\n#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n#include "md2gba_interp68k.h"\n#include "md2gba_cycles.h"\nstatic Md2GbaBus busa,busb; static uint8_t *rom; static unsigned seed=0x87a55a31u; static unsigned rnd(void){seed=seed*1664525u+1013904223u;return seed;}\n''']
for kind,rec,emit,per in classes:
    for i in range(len(ins)):
        m=rec(ins,i)
        if not m: continue
        idx=len(entries);entries.append((kind,m,per))
        chunks.append(f'static unsigned fused{idx}(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){{unsigned steps=0,left=*budget;\n'+emit(m)+f'L_{m["start"]:06X}:; L_{m["next"]:06X}:; *budget=left;return steps;}}\n')
chunks.append('static unsigned (*funcs[])(Md2Gba68k*,Md2GbaBus*,unsigned*)={'+','.join(f'fused{i}' for i in range(len(entries)))+'};\n')
chunks.append('static const uint32_t starts[]={'+','.join(f'0x{m["start"]:x}u' for _,m,_ in entries)+'};\n')
chunks.append('static const uint8_t lens[]={'+','.join(str(per) for _,_,per in entries)+'};\n')
# Metadata for assigning address regs safely.
def gv(m,k,default=255):return m.get(k,default)
chunks.append('static const uint8_t dstreg[]={'+','.join(str(gv(m,'dst')) for _,m,_ in entries)+'};\n')
chunks.append('static const uint8_t srcreg[]={'+','.join(str(gv(m,'src')) if m.get('src_kind')=='an_postinc' else '255' for _,m,_ in entries)+'};\n')
chunks.append('static const uint8_t countreg[]={'+','.join(str(m['count']) for _,m,_ in entries)+'};\n')
chunks.append(r'''
static void init_bus(Md2GbaBus*b){md2gba_bus_init(b,rom,0x300000u);for(unsigned i=0;i<65536;i++)b->wram[i]=(uint8_t)rnd();for(unsigned i=0;i<65536;i++)b->vdp.vram[i]=(uint8_t)rnd();}
int main(void){
 rom=(uint8_t*)malloc(0x300000u);FILE*f=fopen("inputs/ps4built.bin","rb");if(!rom||!f||fread(rom,1,0x300000u,f)!=0x300000u)return 3;fclose(f);
 unsigned cases=0;
 for(unsigned k=0;k<sizeof(starts)/sizeof(starts[0]);k++)for(unsigned t=0;t<40;t++){
   Md2Gba68k a={0},r;init_bus(&busa);for(unsigned j=0;j<8;j++){a.d[j]=rnd();a.a[j]=rnd();}
   unsigned dr=dstreg[k],sr=srcreg[k],cr=countreg[k];
   /* Exercise ordinary WRAM, wrap-near-WRAM, and VDP-ish addresses while keeping
      the structure itself title-independent. */
   static const uint32_t da[]={0xff1000u,0xff7f20u,0xffffc0u,0xc00000u};
   if(dr<8)a.a[dr]=(rnd()&0xFF000000u)|da[t&3u];
   if(sr<8)a.a[sr]=(rnd()&0xFF000000u)|((t&1u)?0xff4000u:0x000400u);
   a.d[cr]=(a.d[cr]&0xffff0000u)|(rnd()%33u);a.pc=starts[k];a.sr=(uint16_t)rnd();
   r=a;busb=busa;unsigned left=1u+rnd()%1600u,orig=left,cy=0u,steps=funcs[k](&a,&busa,&left);
   for(unsigned j=0;j<steps;j++){uint32_t pc=r.pc;if(md68k_interp_step(&r,&busb)!=1){printf("interp fail k=%u pc=%x\n",k,pc);return 4;}cy+=md68k_estimate_cycles(&busb,pc,r.pc);}
   if(memcmp(&a,&r,sizeof(a))||memcmp(&busa,&busb,sizeof(busa))||cy!=orig-left){
     printf("FAIL k=%u t=%u steps=%u cyc=%u/%u pc=%x/%x sr=%x/%x cpu=%d bus=%d\n",k,t,steps,cy,orig-left,a.pc,r.pc,a.sr,r.sr,memcmp(&a,&r,sizeof(a)),memcmp(&busa,&busb,sizeof(busa)));
     for(unsigned z=0;z<8;z++)if(a.d[z]!=r.d[z]||a.a[z]!=r.a[z])printf(" r%u d=%x/%x a=%x/%x\n",z,a.d[z],r.d[z],a.a[z],r.a[z]);
     for(unsigned z=0;z<65536;z++)if(busa.wram[z]!=busb.wram[z]){printf(" wram diff %x %x/%x\n",z,busa.wram[z],busb.wram[z]);break;}
     return 1;}cases++;
 }
 printf("PASS P87 restored strided fusions: %u differential cases across %u structural sites\n",cases,(unsigned)(sizeof(starts)/sizeof(starts[0])));return 0;
}
''')
out=root/'analysis/test_phase87_strided_fusions.c';out.parent.mkdir(exist_ok=True);out.write_text(''.join(chunks))
assert len(entries)==76,len(entries)
subprocess.run(['cc','-O2','-ffunction-sections','-fdata-sections','-I.','-Iruntime/include','-Iruntime/generated',str(out),'md2gba_bus.c','md2gba_aot68k.c','runtime/src/md2gba_interp68k.c','runtime/src/md2gba_cycles.c','runtime/generated/md2gba_interp_opclass.c','-Wl,--gc-sections','-o','analysis/test_phase87_strided_fusions'],cwd=root,check=True)
subprocess.run(['analysis/test_phase87_strided_fusions'],cwd=root,check=True)
