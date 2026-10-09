#!/usr/bin/env python3
import csv, ctypes, importlib.util, subprocess, tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
GEN=ROOT/'tools/generate_predecoded_token_profile.py'
spec=importlib.util.spec_from_file_location('p111gen',GEN); mod=importlib.util.module_from_spec(spec); spec.loader.exec_module(mod)
rows=list(csv.DictReader((ROOT/'analysis/p111_tokens2.csv').open()))
from collections import defaultdict
g=defaultdict(list)
for r in rows:g[r['asset'].lower()].append(r)
assets=[]
for src,rs in sorted(g.items(), key=lambda kv:int(kv[0],16)):
    assets.append((int(src,16),mod.merge_invocations(mod.split_invocations(rs))))
with tempfile.TemporaryDirectory() as td:
    so=Path(td)/'p111.so'
    subprocess.run(['cc','-O2','-shared','-fPIC','-I'+str(ROOT/'runtime/include'),
                    str(ROOT/'profiles/ps4/m4a/generated/predecode/md2gba_profile_predecode_tokens.c'),'-o',str(so)],check=True)
    lib=ctypes.CDLL(str(so))
    class CPU(ctypes.Structure):
        _fields_=[('d',ctypes.c_uint32*8),('a',ctypes.c_uint32*8),('pc',ctypes.c_uint32),('usp',ctypes.c_uint32),('sr',ctypes.c_uint16)]
    lib.md2gba_profile_predecode_token.argtypes=[ctypes.POINTER(CPU),ctypes.c_uint32,ctypes.POINTER(ctypes.c_uint32),ctypes.POINTER(ctypes.c_uint32)]
    lib.md2gba_profile_predecode_token.restype=ctypes.c_int
    total=fast=slow=0
    for src,rs in assets:
        lib.md2gba_profile_predecode_reset()
        for r in rs:
            pre,pd5,postd5,packed,typ=mod.pack_record(r)
            c=CPU();
            for i in range(8): c.d[i]=0xA5A50000; c.a[i]=0x5A5A0000
            c.a[0]=0x5A000000|pre; c.d[5]=0xA5A50000|pd5; c.d[6]=0xA5A50000|mod.hx(r['pre_d6']); c.sr=mod.hx(r['pre_sr'])
            cost=ctypes.c_uint32(0); steps=ctypes.c_uint32(0)
            got=lib.md2gba_profile_predecode_token(ctypes.byref(c),1000,ctypes.byref(cost),ctypes.byref(steps))
            total+=1
            if not typ:
                assert got==0; slow+=1; continue
            assert got==1; fast+=1
            pair=(int(r['steps']),int(r['cycles'])); assert pair in mod.NORMAL
            assert cost.value==pair[1] and steps.value==pair[0]
            assert c.a[0]==(0x5A000000|mod.hx(r['post_a0']))
            assert (c.d[0]&0xffff)==mod.hx(r['post_d0'])
            assert (c.d[1]&0xffff)==mod.hx(r['post_d1'])
            assert (c.d[5]&0xffff)==mod.hx(r['post_d5'])
            assert (c.d[6]&0xffff)==mod.hx(r['post_d6'])
            assert (c.d[7]&0xffff)==mod.hx(r['post_d7'])
            assert (c.sr&31)==(mod.hx(r['post_sr'])&31)
            assert (c.d[0]&0xffff0000)==0xA5A50000 and (c.d[7]&0xffff0000)==0xA5A50000
        # Deadline refusal must advance profile exactly once so the next canonical token can resume profile.
        if len(rs)>1:
            lib.md2gba_profile_predecode_reset(); r=rs[0]; pre,pd5,postd5,packed,typ=mod.pack_record(r)
            c=CPU(); c.a[0]=pre; c.d[5]=pd5; c.d[6]=mod.hx(r['pre_d6']); c.sr=mod.hx(r['pre_sr'])
            cy=int(r['cycles']) if typ else 1; cost=ctypes.c_uint32(); steps=ctypes.c_uint32()
            assert lib.md2gba_profile_predecode_token(ctypes.byref(c),cy,ctypes.byref(cost),ctypes.byref(steps))==0
            r=rs[1]; c=CPU(); c.a[0]=mod.hx(r['pre_a0']); c.d[5]=mod.hx(r['pre_d5']); c.d[6]=mod.hx(r['pre_d6']); c.sr=mod.hx(r['pre_sr'])
            pre2,pd52,postd52,packed2,typ2=mod.pack_record(r)
            if typ2:
                assert lib.md2gba_profile_predecode_token(ctypes.byref(c),1000,ctypes.byref(cost),ctypes.byref(steps))==1
    assert total==17024 and fast==17006 and slow==18,(total,fast,slow)
print(f'PASS P111 generated token profile: {fast}/{total} exact shortcuts, {slow} canonical fallbacks, deadline-resume verified')
