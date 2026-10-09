#!/usr/bin/env python3
"""MD2GBA utility front-end: ROM -> analyze -> profile -> plan -> GBA.

This is the new utility lineage. Game-specific facts live in a persistent profile
DB and generated files; the reusable runtime remains title-agnostic.
"""
from __future__ import annotations
import argparse, json, os, shutil, subprocess, sys, tempfile
from pathlib import Path
from md2gba_profile_db import open_db, upsert_rom, import_static_graph, import_run, make_plan
from md2gba_autoprofile import AutoProfiler, AutoConfig

ROOT=Path(os.environ.get('MD2GBA_BUNDLE_ROOT', Path(__file__).resolve().parents[1]))
TOOLS=ROOT/'tools'; HOST=ROOT/'host'

def python_tool(path,*args):
    path=Path(path)
    if getattr(sys,'frozen',False) or os.environ.get('MD2GBA_FROZEN')=='1':
        rel=str(path.resolve().relative_to(ROOT.resolve()))
        return [sys.executable,'--internal-tool',rel,*map(str,args)]
    return [sys.executable,str(path),*map(str,args)]

def run(cmd,cwd=ROOT):
    print('+',' '.join(str(x) for x in cmd),flush=True); subprocess.run([str(x) for x in cmd],cwd=cwd,check=True)

def build_host_runner(force=False):
    exe=HOST/('md2gba_profile_runner.exe' if os.name=='nt' else 'md2gba_profile_runner')
    srcs=[HOST/'md2gba_profile_runner.c',ROOT/'md2gba_bus.c',ROOT/'md2gba_aot68k.c',ROOT/'runtime/src/md2gba_interp68k.c',ROOT/'runtime/src/md2gba_cycles.c',ROOT/'md2gba_timing.c',ROOT/'runtime/generated/md2gba_interp_opclass.c',ROOT/'runtime/src/md2gba_z80.c']
    if not force and exe.exists() and exe.stat().st_mtime>=max(x.stat().st_mtime for x in srcs): return exe
    cc=shutil.which('clang') or shutil.which('gcc') or shutil.which('cc')
    if not cc: raise SystemExit('A C compiler (Clang/GCC) is required for the built-in profiler.')
    run([cc,'-O3','-std=c11','-DMD2GBA_Z80_PROFILE=1','-I.','-Iruntime/include','-Iruntime/generated',*srcs,'-o',exe])
    return exe

def paths_for(rom:Path,dbarg:Path|None):
    db=(dbarg.resolve() if dbarg else rom.with_suffix(rom.suffix+'.md2gba.sqlite'))
    work=db.parent/(db.stem+'.work'); work.mkdir(parents=True,exist_ok=True)
    return db,work

def ensure_analysis(rom,db,work):
    con=open_db(db); info=upsert_rom(con,rom); graph=work/'static_graph.json'; rep=work/'static_graph_report.json'
    if not graph.exists(): run(python_tool(TOOLS/'discover68k_graph.py',rom,'-o',graph,'--report',rep))
    con.execute('DELETE FROM static_pcs WHERE rom_sha256=?',(info['sha256'],)); con.commit(); import_static_graph(con,info['sha256'],graph)
    return con,info,graph,rep

def cmd_analyze(a):
    rom=a.rom.resolve();db,work=paths_for(rom,a.db);con,info,graph,rep=ensure_analysis(rom,db,work)
    n=con.execute('SELECT count(*) FROM static_pcs WHERE rom_sha256=?',(info['sha256'],)).fetchone()[0]
    print(json.dumps({'rom':info,'db':str(db),'static_pc_count':n,'report':str(rep)},indent=2))

def cmd_profile(a):
    rom=a.rom.resolve();db,work=paths_for(rom,a.db);con,info,graph,rep=ensure_analysis(rom,db,work);exe=build_host_runner()
    policies=[int(x) for x in a.policies.split(',') if x.strip()]
    runroot=work/'runs';runroot.mkdir(exist_ok=True)
    summaries=[]
    for i in range(a.campaigns):
        policy=policies[i%len(policies)];seed=a.seed+i*0x9e3779b1
        prefix=runroot/f'run_{i:04d}_p{policy}_s{seed&0xffffffff:08x}'
        p=subprocess.run([str(exe),str(rom),str(prefix),str(a.frames),str(a.max_steps),str(policy),str(seed&0xffffffff)],cwd=ROOT)
        rid,s=import_run(con,info['sha256'],prefix,policy,seed&0xffffffff,a.frames);s['run_id']=rid;s['policy']=policy;s['seed']=seed&0xffffffff;summaries.append(s)
        if p.returncode and a.stop_on_fault: break
    obs=con.execute('SELECT count(*),coalesce(sum(count),0) FROM observed_pcs WHERE rom_sha256=?',(info['sha256'],)).fetchone()
    ed=con.execute('SELECT count(*) FROM observed_edges WHERE rom_sha256=?',(info['sha256'],)).fetchone()[0]
    print(json.dumps({'db':str(db),'campaigns_completed':len(summaries),'unique_observed_pcs':obs[0],'observed_instructions':obs[1],'unique_edges':ed,'runs':summaries},indent=2))


def cmd_auto_profile(a):
    rom=a.rom.resolve(); db,work=paths_for(rom,a.db); con,info,graph,rep=ensure_analysis(rom,db,work); exe=build_host_runner()
    policies=tuple(int(x) for x in a.policies.split(',') if x.strip())
    cfg=AutoConfig(workers=max(1,a.workers),max_hours=a.hours,base_frames=a.frames,max_frames=a.max_frames,
                   max_steps=a.max_steps,saturation_epochs=a.saturation_epochs,min_campaigns=a.min_campaigns,
                   growth_floor=a.growth_floor,seed=a.seed,policies=policies,state_exploration=not a.no_state_exploration,reset_fraction=a.reset_fraction,max_corpus=a.max_corpus,snapshot_slices=a.snapshot_slices,promote_per_success=a.promote_per_success)
    ap=AutoProfiler(rom=rom,con=con,sha=info['sha256'],runner=exe,runroot=work/'auto_runs',import_run_fn=import_run,cfg=cfg)
    result=ap.run(); graph=work/'profiled_graph.json'; planp=work/'optimization_plan.json'; plan=make_plan(con,info['sha256'],graph,planp)
    print(json.dumps({'db':str(db),'result':result,'plan':plan},indent=2))

def cmd_plan(a):
    rom=a.rom.resolve();db,work=paths_for(rom,a.db);con,info,_,_=ensure_analysis(rom,db,work)
    graph=work/'profiled_graph.json';planp=work/'optimization_plan.json';plan=make_plan(con,info['sha256'],graph,planp)
    print(json.dumps({'graph':str(graph),'plan':str(planp),**plan},indent=2))

def cmd_precompile(a):
    rom=a.rom.resolve();db,work=paths_for(rom,a.db);ensure_analysis(rom,db,work)
    out=work/'precompile';out.mkdir(parents=True,exist_ok=True)
    run(python_tool(TOOLS/'md2gba_precompile.py',rom,'--db',db,'--work',out))
    manifest=out/'translation_manifest.json'
    print(json.dumps({'translation_manifest':str(manifest),'work':str(out)},indent=2))

def cmd_build(a):
    rom=a.rom.resolve();db,work=paths_for(rom,a.db);con,info,_,_=ensure_analysis(rom,db,work)
    graph=work/'profiled_graph.json';planp=work/'optimization_plan.json';plan=make_plan(con,info['sha256'],graph,planp)
    pre=work/'precompile';pre.mkdir(parents=True,exist_ok=True)
    run(python_tool(TOOLS/'md2gba_precompile.py',rom,'--db',db,'--work',pre))
    out=a.output.resolve() if a.output else rom.with_suffix('.md2gba.gba')
    cmd=python_tool(TOOLS/'md2gba_compile.py',rom,'-o',out,'--addresses',graph,'--jobs',str(a.jobs),'--dispatch-mode',a.dispatch_mode,'--translation-manifest',pre/'translation_manifest.json')
    zimg=pre/'z80_ram.bin';zprof=pre/'z80_profile.txt';ym=pre/'ym_trace.bin'
    if zimg.exists() and zprof.exists() and zprof.stat().st_size:
        cmd += ['--z80-snapshot',str(zimg),'--z80-profile',str(zprof),'--z80-native-min-count','1']
    if ym.exists() and ym.stat().st_size and getattr(a,'audio_mode','silent')=='native': cmd += ['--ym-trace',str(ym)]
    else: cmd.append('--silent-audio')
    if a.keep_build: cmd.append('--keep-build')
    run(cmd); print(json.dumps({'output':str(out),'profile_db':str(db),'translation_manifest':str(pre/'translation_manifest.json'),'plan':plan},indent=2))

def cmd_pipeline(a):
    # Smart CLI equivalent of the GUI: analyze + adaptive/state-guided profile + plan + build.
    class X: pass
    x=X(); x.rom=a.rom; x.db=a.db; x.hours=a.hours; x.workers=a.workers; x.frames=a.frames; x.max_frames=a.max_frames; x.max_steps=a.max_steps
    x.policies=a.policies; x.seed=a.seed; x.saturation_epochs=a.saturation_epochs; x.min_campaigns=a.min_campaigns; x.growth_floor=a.growth_floor
    x.no_state_exploration=a.no_state_exploration; x.reset_fraction=a.reset_fraction; x.max_corpus=a.max_corpus; x.snapshot_slices=a.snapshot_slices; x.promote_per_success=a.promote_per_success
    cmd_auto_profile(x)
    q=X(); q.rom=a.rom; q.db=a.db; cmd_precompile(q)
    if not a.no_build:
        y=X(); y.rom=a.rom; y.db=a.db; y.output=a.output; y.jobs=a.jobs; y.dispatch_mode=a.dispatch_mode; y.keep_build=a.keep_build; y.audio_mode=a.audio_mode; cmd_build(y)

def cmd_doctor(a):
    import platform
    tools={n:shutil.which(n) for n in ['clang','gcc','ld.lld','llvm-objcopy']}
    print(json.dumps({'python':sys.version.split()[0],'platform':platform.platform(),'tools':tools,
      'host_profiler_compiler_ok':bool(tools['clang'] or tools['gcc']),
      'gba_toolchain_ok':bool(tools['clang'] and tools['ld.lld'] and tools['llvm-objcopy'])},indent=2))

def cmd_status(a):
    rom=a.rom.resolve();db,work=paths_for(rom,a.db);con,info,_,_=ensure_analysis(rom,db,work)
    sha=info['sha256']; runs=con.execute('SELECT count(*) FROM profile_runs WHERE rom_sha256=?',(sha,)).fetchone()[0]
    obs=con.execute('SELECT count(*),coalesce(sum(count),0) FROM observed_pcs WHERE rom_sha256=?',(sha,)).fetchone()
    static=con.execute('SELECT count(*) FROM static_pcs WHERE rom_sha256=?',(sha,)).fetchone()[0]
    graph=work/'profiled_graph.json';planp=work/'optimization_plan.json';plan=make_plan(con,sha,graph,planp)
    states=con.execute('SELECT count(*),coalesce(max(depth),0),coalesce(sum(uses),0),coalesce(sum(successes),0) FROM state_corpus WHERE rom_sha256=? AND active=1',(sha,)).fetchone() if con.execute("SELECT name FROM sqlite_master WHERE type='table' AND name='state_corpus'").fetchone() else (0,0,0,0)
    print(json.dumps({'rom':info,'db':str(db),'runs':runs,'static_pcs':static,'observed_pcs':obs[0],
      'observed_instructions':obs[1],'state_corpus':{'active':states[0],'max_depth':states[1],'uses':states[2],'successes':states[3]},'plan':plan},indent=2))

def main():
    if len(sys.argv)>1 and sys.argv[1]=='grind': sys.argv[1]='pipeline'
    ap=argparse.ArgumentParser(prog='md2gba',description='Offline Mega Drive/Genesis -> GBA compiler utility')
    sub=ap.add_subparsers(dest='cmd',required=True)
    def common(p): p.add_argument('rom',type=Path);p.add_argument('--db',type=Path,help='persistent profile cache (default: ROM.md2gba.sqlite)')
    p=sub.add_parser('doctor');p.set_defaults(fn=cmd_doctor)
    p=sub.add_parser('analyze');common(p);p.set_defaults(fn=cmd_analyze)
    p=sub.add_parser('status');common(p);p.set_defaults(fn=cmd_status)
    p=sub.add_parser('profile');common(p);p.add_argument('--campaigns',type=int,default=8);p.add_argument('--frames',type=int,default=3600);p.add_argument('--max-steps',type=int,default=100_000_000);p.add_argument('--policies',default='0,1,2');p.add_argument('--seed',type=int,default=1);p.add_argument('--stop-on-fault',action='store_true');p.set_defaults(fn=cmd_profile)
    p=sub.add_parser('auto-profile');common(p);p.add_argument('--hours',type=float,default=0.0,help='0 = grind until coverage saturates');p.add_argument('--workers',type=int,default=max(1,os.cpu_count() or 4));p.add_argument('--frames',type=int,default=3600);p.add_argument('--max-frames',type=int,default=28800);p.add_argument('--max-steps',type=int,default=250_000_000);p.add_argument('--policies',default='0,1,2,3,4,5,6,7');p.add_argument('--seed',type=int,default=1);p.add_argument('--saturation-epochs',type=int,default=4);p.add_argument('--min-campaigns',type=int,default=16);p.add_argument('--growth-floor',type=int,default=8);p.add_argument('--no-state-exploration',action='store_true');p.add_argument('--reset-fraction',type=float,default=0.25);p.add_argument('--max-corpus',type=int,default=128);p.add_argument('--snapshot-slices',type=int,default=4);p.add_argument('--promote-per-success',type=int,default=2);p.set_defaults(fn=cmd_auto_profile)
    p=sub.add_parser('plan');common(p);p.set_defaults(fn=cmd_plan)
    p=sub.add_parser('precompile');common(p);p.set_defaults(fn=cmd_precompile)
    p=sub.add_parser('build');common(p);p.add_argument('-o','--output',type=Path);p.add_argument('--jobs',type=int,default=max(1,os.cpu_count() or 4));p.add_argument('--dispatch-mode',default='fastchain',choices=['fastchain','pschybrid','chain','burst','step']);p.add_argument('--audio-mode',choices=['silent','native'],default='silent');p.add_argument('--keep-build',action='store_true');p.set_defaults(fn=cmd_build)
    p=sub.add_parser('pipeline');common(p);p.add_argument('-o','--output',type=Path);p.add_argument('--hours',type=float,default=0.0,help='0 = keep profiling until saturation');p.add_argument('--workers',type=int,default=max(1,os.cpu_count() or 4));p.add_argument('--frames',type=int,default=3600);p.add_argument('--max-frames',type=int,default=28800);p.add_argument('--max-steps',type=int,default=250_000_000);p.add_argument('--policies',default='0,1,2,3,4,5,6,7');p.add_argument('--seed',type=int,default=1);p.add_argument('--saturation-epochs',type=int,default=4);p.add_argument('--min-campaigns',type=int,default=16);p.add_argument('--growth-floor',type=int,default=8);p.add_argument('--no-state-exploration',action='store_true');p.add_argument('--reset-fraction',type=float,default=0.25);p.add_argument('--max-corpus',type=int,default=128);p.add_argument('--snapshot-slices',type=int,default=4);p.add_argument('--promote-per-success',type=int,default=2);p.add_argument('--jobs',type=int,default=max(1,os.cpu_count() or 4));p.add_argument('--dispatch-mode',default='fastchain',choices=['fastchain','pschybrid','chain','burst','step']);p.add_argument('--audio-mode',choices=['silent','native'],default='silent');p.add_argument('--keep-build',action='store_true');p.add_argument('--no-build',action='store_true');p.set_defaults(fn=cmd_pipeline)
    a=ap.parse_args();a.fn(a)
if __name__=='__main__':main()
