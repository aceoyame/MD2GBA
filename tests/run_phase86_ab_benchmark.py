#!/usr/bin/env python3
"""Run the Phase 86 2x2 ROM set through the same fixed-input mGBA benchmark.

Requires the mGBA development library (0.10.x-compatible API) and pkg-config.
No game identity is consulted by the benchmark: ELF symbols locate the generic
scheduler/audio state for each independently-linked A/B binary.
"""
from __future__ import annotations
import argparse,csv,json,os,re,shlex,struct,subprocess,tempfile
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
VARIANTS=['base','dma','lut','dma_lut']
GBA_HZ=16777216
GBA_FRAME_CYCLES=280896
SAMPLE_RATE=16384
BLOCK_SAMPLES=128

def run(cmd,**kw):
    print('+',' '.join(shlex.quote(str(x)) for x in cmd),flush=True)
    return subprocess.run([str(x) for x in cmd],check=True,**kw)

def pkg_flags():
    for name in ('mgba','libmgba'):
        p=subprocess.run(['pkg-config','--cflags','--libs',name],text=True,capture_output=True)
        if p.returncode==0:return shlex.split(p.stdout)
    raise SystemExit('mGBA development library not found via pkg-config (tried mgba, libmgba)')

def target_offsets():
    src='''#include <stddef.h>\n#include "md2gba_gba_pcm_stream.h"\n#include "md2gba_timing.h"\nconst unsigned OFF_vblanks=offsetof(Md2GbaTiming,vblanks);\nconst unsigned OFF_irq_blocks=offsetof(Md2GbaGbaPcmStream,irq_blocks);\nconst unsigned OFF_refills=offsetof(Md2GbaGbaPcmStream,refills);\nconst unsigned OFF_underruns=offsetof(Md2GbaGbaPcmStream,underruns);\nconst unsigned OFF_wrap_retargets=offsetof(Md2GbaGbaPcmStream,wrap_retargets);\nconst unsigned OFF_pump_bursts=offsetof(Md2GbaGbaPcmStream,pump_bursts);\nconst unsigned OFF_pump_late_quanta=offsetof(Md2GbaGbaPcmStream,pump_late_quanta);\n'''
    with tempfile.TemporaryDirectory() as td:
        c=Path(td)/'o.c'; ll=Path(td)/'o.ll'; c.write_text(src)
        run(['clang','-target','arm-none-eabi','-mcpu=arm7tdmi','-S','-emit-llvm','-I'+str(ROOT),'-I'+str(ROOT/'runtime/include'),c,'-o',ll],stdout=subprocess.DEVNULL)
        text=ll.read_text()
    out={}
    for k,v in re.findall(r'@OFF_([A-Za-z0-9_]+)\s*=.*?i32\s+(\d+)',text):out[k]=int(v)
    need={'vblanks','irq_blocks','refills','underruns','wrap_retargets','pump_bursts','pump_late_quanta'}
    if set(out)<need:raise RuntimeError(f'could not derive target offsets: {out}')
    return out

def symbols(elf:Path):
    text=subprocess.check_output(['nm',str(elf)],text=True)
    out={}
    for line in text.splitlines():
        p=line.split()
        if len(p)==3 and p[2] in {'timing','audio_stream'}:out[p[2]]=int(p[0],16)
    if set(out)!={'timing','audio_stream'}:raise RuntimeError(f'missing benchmark symbols in {elf}: {out}')
    return out

def ram_u32(blob:bytes,addr:int)->int:
    if 0x02000000<=addr<0x02040000:o=addr-0x02000000
    elif 0x03000000<=addr<0x03008000:o=0x40000+(addr-0x03000000)
    else:raise ValueError(hex(addr))
    return struct.unpack_from('<I',blob,o)[0]

def compile_probe(out:Path):
    flags=pkg_flags()
    run(['cc','-O2',str(ROOT/'tests/probe_phase85_mgba.c'),'-o',out]+flags)

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--frames',type=int,default=12000)
    ap.add_argument('--interval',type=int,default=300,help='probe currently records every 300 frames; retained for output metadata')
    ap.add_argument('--out',type=Path,default=ROOT/'analysis/p86_ab_bench')
    ap.add_argument('--keep-dumps',action='store_true')
    a=ap.parse_args(); a.out.mkdir(parents=True,exist_ok=True)
    probe=a.out/'probe_phase86_mgba'; compile_probe(probe)
    offs=target_offsets(); rows=[]
    for tag in VARIANTS:
        rom=ROOT/'out_p86'/f'RUN_THIS_MD2GBA_P86_{tag.upper()}.gba'
        elf=ROOT/'out_p86'/f'p86_{tag}.elf'
        if not rom.exists() or not elf.exists():raise SystemExit(f'missing {rom} / {elf}')
        sy=symbols(elf); prefix=a.out/tag
        run([probe,rom,str(a.frames),prefix])
        for ram in sorted(a.out.glob(f'{tag}-*.ram')):
            f=int(ram.stem.split('-')[-1]); blob=ram.read_bytes()
            vb=ram_u32(blob,sy['timing']+offs['vblanks']); ab=sy['audio_stream']
            irq=ram_u32(blob,ab+offs['irq_blocks']); ref=ram_u32(blob,ab+offs['refills'])
            und=ram_u32(blob,ab+offs['underruns']); wr=ram_u32(blob,ab+offs['wrap_retargets'])
            pb=ram_u32(blob,ab+offs['pump_bursts']); late=ram_u32(blob,ab+offs['pump_late_quanta'])
            seconds=f*GBA_FRAME_CYCLES/GBA_HZ
            pump=tag in {'base','lut'}
            unready=max(0,und-late) if pump else und
            consumer_irqs=pb if pump else irq
            rows.append(dict(variant=tag,physical_frames=f,seconds=seconds,guest_vblanks=vb,
                guest_fps=vb*(GBA_HZ/GBA_FRAME_CYCLES)/f,audio_blocks=irq,audio_refills=ref,
                underrun_counter=und,late_fifo_quanta=late,unready_blocks=unready,
                unready_block_percent=(100.0*unready/irq if irq else 0.0),
                consumer_irq_events=consumer_irqs,wrap_retargets=wr))
            if not a.keep_dumps:ram.unlink()
        if not a.keep_dumps:
            for ppm in a.out.glob(f'{tag}-*.ppm'):ppm.unlink()
    keys=list(rows[0]);
    with (a.out/'PHASE86_AB_BENCHMARK.csv').open('w',newline='') as f:
        w=csv.DictWriter(f,fieldnames=keys);w.writeheader();w.writerows(rows)
    final={r['variant']:r for r in rows if r['physical_frames']==a.frames}
    meta={'method':{'emulator':'mGBA library','physical_frames':a.frames,'input':'Start frames 420-479; A frames 660-719; cold boot','sample_rate':SAMPLE_RATE,'block_samples':BLOCK_SAMPLES},'target_offsets':offs,'final':final}
    (a.out/'PHASE86_AB_BENCHMARK.json').write_text(json.dumps(meta,indent=2)+'\n')
    print(json.dumps(final,indent=2))
if __name__=='__main__':main()
