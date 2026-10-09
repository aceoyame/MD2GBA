#!/usr/bin/env python3
"""Generate a title-agnostic YM2612 patch/pitch wavetable bank from raw YM writes.

Input trace is a sequence of 3-byte records: port, register, value.  The tool
reconstructs the six YM channels, records every keyed patch/pitch state, and
renders six phase-normalized 128-sample timbre snapshots offline.  Runtime code
matches the live YM register state at key-on and uses the nearest precompiled
pitch zone.  Unmatched/dynamic patches retain the live YM fallback.
"""
from __future__ import annotations
import argparse, collections, math
from dataclasses import dataclass
from pathlib import Path

WAVE_POINTS=128
TIMBRE_TIMES=(0.010,0.045,0.100,0.200,0.400,0.800)
CLOCK=7670454
DEFAULT_RATE=31536
CARRIER_MASKS=(0x08,0x08,0x08,0x08,0x0A,0x0E,0x0E,0x0F)
FKEY=(0,0,0,0,0,0,0,1,2,3,3,3,3,3,3,3)
DT=(
(0,)*32,
(0,0,0,0,1,1,1,1,1,1,1,1,2,2,2,2,2,3,3,3,4,4,4,5,5,6,6,7,8,8,8,8),
(1,1,1,1,2,2,2,2,2,3,3,3,4,4,4,5,5,6,6,7,8,8,9,10,11,12,13,14,16,16,16,16),
(2,2,2,2,2,3,3,3,4,4,4,5,5,6,6,7,8,8,9,10,11,12,13,14,16,17,19,20,22,22,22,22),
)
SINE=tuple(round(math.sin(i/256*math.tau)*127) for i in range(256))

@dataclass
class Op:
    phase:int=0; env:int=0; prev:int=0; inc:int=1
    dtmul:int=0; tl:int=127; rsar:int=0; d1r:int=0; d2r:int=0; slrr:int=0; ssg:int=0; stage:int=1

class Renderer:
    def __init__(self, patch:bytes, fnum:int, block:int, rate:int):
        self.alg=patch[0]&7; self.fb=(patch[0]>>3)&7; self.rate=rate; self.elapsed=0; self.ops=[]
        hz=fnum*(CLOCK/144.0)*(2.0**(block-21)); base=max(1,min(0xffffffff,round(hz*(2.0**32)/rate)))
        keycode=(block<<2)|FKEY[(fnum>>7)&15]; unit=round((CLOCK*4096.0)/(144.0*rate))
        for i in range(4):
            q=1+i*7; dtmul,tl,rsar,d1r,d2r,slrr,ssg=patch[q:q+7]
            mul=dtmul&15; dt=(dtmul>>4)&7; raw=DT[dt&3][keycode]; delta=raw*unit
            if dt&4: delta=-delta
            adj=max(1,base+delta); inc=max(1,min(0xffffffff,adj//2 if mul==0 else adj*mul))
            self.ops.append(Op(inc=inc,dtmul=dtmul,tl=tl,rsar=rsar,d1r=d1r,d2r=d2r,slrr=slrr,ssg=ssg))
    @staticmethod
    def tlq15(tl): return max(0,min(32767,round(32767*(10.0**(-(tl&127)*0.75/20.0)))))
    def step_env(self,o:Op):
        ar=o.rsar&31; dr=o.d1r&31; sr=o.d2r&31; sl=(o.slrr>>4)&15
        if o.stage==1:
            if not ar:return
            o.env+=max(1,round((1+ar*ar)*18157/self.rate))
            if o.env>=65535:o.env=65535;o.stage=2
        elif o.stage==2:
            target=0 if sl>=15 else 65535-sl*4096; st=max(1,round((1+(dr*dr>>2))*18157/self.rate))
            if o.env>target+st:o.env-=st
            else:o.env=target;o.stage=3
        elif o.stage==3 and sr and o.env:
            st=max(1,round((1+(sr*sr>>4))*18157/self.rate));o.env=max(0,o.env-st)
    def op(self,o:Op,mod:int):
        self.step_env(o);o.phase=(o.phase+o.inc)&0xffffffff; idx=((o.phase+(mod&0xffffffff))>>24)&255
        return (SINE[idx]*((o.env*self.tlq15(o.tl))//65535))>>15
    def sample(self):
        o=self.ops;s1=self.op(o[0],(o[0].prev*self.fb)<<20);o[0].prev=s1;sh=23
        if self.alg==0:s2=self.op(o[1],s1<<sh);s3=self.op(o[2],s2<<sh);out=self.op(o[3],s3<<sh)
        elif self.alg==1:s2=self.op(o[1],0);s3=self.op(o[2],(s1+s2)<<sh);out=self.op(o[3],s3<<sh)
        elif self.alg==2:s2=self.op(o[1],0);s3=self.op(o[2],s2<<sh);out=self.op(o[3],(s1+s3)<<sh)
        elif self.alg==3:s2=self.op(o[1],s1<<sh);s3=self.op(o[2],0);out=self.op(o[3],(s2+s3)<<sh)
        elif self.alg==4:s2=self.op(o[1],s1<<sh);s3=self.op(o[2],0);out=s2+self.op(o[3],s3<<sh)
        elif self.alg==5:s2=self.op(o[1],s1<<sh);s3=self.op(o[2],s1<<sh);out=s2+s3+self.op(o[3],s1<<sh)
        elif self.alg==6:s2=self.op(o[1],s1<<sh);out=s2+self.op(o[2],0)+self.op(o[3],0)
        else: out=s1+self.op(o[1],0)+self.op(o[2],0)+self.op(o[3],0)
        self.elapsed+=1;return out>>2
    def advance(self,t):
        n=round(t*self.rate)
        while self.elapsed<n:self.sample()
    def wave(self,fnum,block):
        hz=max(1.0,fnum*(CLOCK/144.0)*(2.0**(block-21)));period=self.rate/hz;n=max(4,math.ceil(period)+3)
        raw=[self.sample() for _ in range(n)];vals=[]
        for p in range(WAVE_POINTS):
            x=(p/WAVE_POINTS)*period;i=int(x);fr=x-i;a=raw[min(i,n-1)];bb=raw[min(i+1,n-1)];vals.append(a+(bb-a)*fr)
        mean=sum(vals)/len(vals);vals=[x-mean for x in vals];peak=max(1.0,max(abs(x) for x in vals));scale=112.0/peak
        # Preserve the old exact-YM multisample calibration idea: the runtime
        # applies the live carrier envelope/TL, while this factor restores the
        # amplitude removed when the compiler normalized the phase waveform.
        cm=CARRIER_MASKS[self.alg&7];amps=[]
        for i,o in enumerate(self.ops):
            if cm&(1<<i):
                amp=(max(0,min(127,o.env>>9))*self.tlq15(o.tl))//32767
                amps.append(amp)
        carrier=max(1,round(sum(amps)/len(amps))) if amps else 1
        gain=max(1,min(255,round((peak*128*64)/(112*carrier))))
        return bytes((max(-127,min(127,round(x*scale)))&255 for x in vals)),gain

def key_channel(v):
    c=v&3
    if c==3:return None
    return c+(3 if v&4 else 0)

def parse_trace(raw:bytes):
    ch=[{'ops':[[0,127,0,0,0,0,0] for _ in range(4)],'alg':7,'fb':0,'fnum':0,'block':0} for _ in range(6)]
    hits=collections.Counter(); pitch=collections.defaultdict(collections.Counter)
    for i in range(0,len(raw)-2,3):
        port,reg,val=raw[i],raw[i+1],raw[i+2];port&=1
        if port==0 and reg==0x28:
            c=key_channel(val)
            if c is not None and val&0xf0:
                q=ch[c];patch=bytes([q['alg']|(q['fb']<<3)]+[x for op in q['ops'] for x in op]);hits[patch]+=1;pitch[patch][(q['fnum'],q['block'])]+=1
            continue
        if 0x30<=reg<=0x9f:
            rc=reg&3
            if rc>=3:continue
            c=rc+(3 if port else 0);oi=(reg>>2)&3;idx={0x30:0,0x40:1,0x50:2,0x60:3,0x70:4,0x80:5,0x90:6}.get(reg&0xf0)
            if idx is not None:ch[c]['ops'][oi][idx]=val&(0x7f if idx==1 else 0x1f if idx==4 else 0x0f if idx==6 else 0xff)
            continue
        rc=reg&3
        if rc>=3:continue
        c=rc+(3 if port else 0)
        if (reg&0xfc)==0xa0:ch[c]['fnum']=(ch[c]['fnum']&0x700)|val
        elif (reg&0xfc)==0xa4:ch[c]['fnum']=(ch[c]['fnum']&0xff)|((val&7)<<8);ch[c]['block']=(val>>3)&7
        elif (reg&0xfc)==0xb0:ch[c]['alg']=val&7;ch[c]['fb']=(val>>3)&7
    return hits,pitch

def emit(trace:Path,out:Path,rate:int):
    hits,pitches=parse_trace(trace.read_bytes());zones=[];blob=bytearray()
    for patch,_ in hits.most_common():
        for (fnum,block),count in sorted(pitches[patch].items(),key=lambda kv:(kv[0][1],kv[0][0])):
            r=Renderer(patch,fnum,block,rate);offs=[];gains=[]
            for t in TIMBRE_TIMES:
                r.advance(t);w,g=r.wave(fnum,block);offs.append(len(blob));blob+=w;gains.append(g)
            zones.append((patch,fnum,block,count,offs,gains))
    lines=['/* generated by generate_ym2612_patchbank.py; do not edit */',f'#define MD2GBA_YM_PATCHBANK_RATE {rate}u',f'#define MD2GBA_YM_PATCHBANK_WAVE_POINTS {WAVE_POINTS}u',f'#define MD2GBA_YM_PATCHBANK_STAGES {len(TIMBRE_TIMES)}u']
    lines.append(f'static const int8_t md2gba_ym_patch_wave_blob[{len(blob)}]={{')
    for i in range(0,len(blob),24): lines.append('  '+','.join(str(x if x<128 else x-256) for x in blob[i:i+24])+',')
    lines.append('};')
    lines.append(f'static const Md2GbaYmPatchZone md2gba_ym_patch_zones[{len(zones)}]={{')
    for patch,fnum,block,count,offs,gains in zones:
        ps=','.join(str(x) for x in patch);os=','.join(str(x)+'u' for x in offs);gs=','.join(str(x) for x in gains)
        lines.append(f'  {{{{{ps}}},{fnum}u,{block}u,{count}u,{{{os}}},{{{gs}}}}},')
    lines.append('};')
    lines.append(f'static const unsigned md2gba_ym_patch_zone_count={len(zones)}u;')
    out.write_text('\n'.join(lines)+'\n')
    print(f'patches={len(hits)} zones={len(zones)} wave_bytes={len(blob)} keyons={sum(hits.values())}')

def main():
    ap=argparse.ArgumentParser();ap.add_argument('trace',type=Path);ap.add_argument('-o','--output',type=Path,required=True);ap.add_argument('--rate',type=int,default=DEFAULT_RATE);a=ap.parse_args();emit(a.trace,a.output,a.rate)
if __name__=='__main__':main()
