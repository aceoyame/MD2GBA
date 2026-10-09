#!/usr/bin/env python3
"""Retarget precomputed PSIV phase increments to a new PCM mixer rate.

The source tables were generated for 31,536 Hz.  The FM multisample waveforms
are phase-normalized; only phase increments and rate metadata need retargeting
for PSC's normal 13,379 Hz MusicPlayer2000 mode.
"""
from pathlib import Path
import argparse,re

def scale_array(text,name,src,dst):
    pat=re.compile(r'(const unsigned int '+re.escape(name)+r'\[128\] = \{\n)(.*?)(\n\};)',re.S)
    m=pat.search(text)
    if not m: raise SystemExit(f'missing {name}')
    vals=[int(x) for x in re.findall(r'(\d+)u',m.group(2))]
    if len(vals)!=128: raise SystemExit(f'{name}: {len(vals)} entries')
    out=[]
    for v in vals:
        # Preserve frequency: phase_increment/output_rate is constant.
        q=(v*src + dst//2)//dst
        if q>0xffffffff: q=0xffffffff
        out.append(q)
    lines=[]
    for i in range(0,128,8): lines.append('    '+', '.join(f'{v}u' for v in out[i:i+8])+',')
    return text[:m.start()]+m.group(1)+'\n'.join(lines)+m.group(3)+text[m.end():]

def main():
    ap=argparse.ArgumentParser(); ap.add_argument('--src-rate',type=int,default=31536); ap.add_argument('--rate',type=int,default=13379)
    ap.add_argument('--chip',default='profiles/ps4/m4a/src/ps4_chip_audio_data_generated.inc')
    ap.add_argument('--fm',default='profiles/ps4/m4a/src/ps4_fm_multisample_data_generated.inc')
    a=ap.parse_args(); p=Path(a.chip); text=p.read_text()
    text=scale_array(text,'generated_fm_phase_inc',a.src_rate,a.rate)
    text=scale_array(text,'generated_psg_phase_inc',a.src_rate,a.rate)
    marker='// P93 PSC/M4A mixer-rate retarget: phase increments are for %du.\n'%a.rate
    if 'P93 PSC/M4A mixer-rate retarget' not in text: text=marker+text
    p.write_text(text)
    f=Path(a.fm); ft=f.read_text(); ft=re.sub(r'constexpr unsigned int generated_fm_wave_sample_rate = \d+u;',f'constexpr unsigned int generated_fm_wave_sample_rate = {a.rate}u;',ft)
    if 'P93 PSC/M4A mixer-rate retarget' not in ft: ft=marker+ft
    f.write_text(ft)
    print(f'retargeted PSIV native audio tables {a.src_rate} -> {a.rate} Hz')
if __name__=='__main__': main()
