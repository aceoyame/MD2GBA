#!/usr/bin/env python3
"""MD2GBA generic offline compiler front-end.

Takes an arbitrary Mega Drive/Genesis ROM, discovers or consumes a verified
68000 PC graph, builds a verified structural-specialization sidecar, lays out
ROM data in the optional PSC-style descending representation, emits native ARM
AOT continuations, and links the generic GBA runtime.

This tool contains no title checks and no PSC code. PSC-derived techniques are
implemented as generic MD hardware/data-layout/compiler policies.
"""
from __future__ import annotations
import argparse, hashlib, json, os, re, shlex, shutil, subprocess, sys, time
from pathlib import Path

ROOT = Path(os.environ.get('MD2GBA_BUNDLE_ROOT', Path(__file__).resolve().parents[1]))
TOOLS = ROOT / 'tools'

def python_tool(path,*args):
    path=Path(path)
    if getattr(sys,'frozen',False) or os.environ.get('MD2GBA_FROZEN')=='1':
        rel=str(path.resolve().relative_to(ROOT.resolve()))
        return [sys.executable,'--internal-tool',rel,*map(str,args)]
    return [sys.executable,str(path),*map(str,args)]

def run(cmd, *, cwd=ROOT, env=None, quiet=False):
    if not quiet:
        print('+', ' '.join(shlex.quote(str(x)) for x in cmd), flush=True)
    subprocess.run([str(x) for x in cmd], cwd=cwd, env=env, check=True)


def sha256(path: Path) -> str:
    h=hashlib.sha256()
    with path.open('rb') as f:
        for b in iter(lambda:f.read(1<<20), b''): h.update(b)
    return h.hexdigest()


def sanitize_title(raw: bytes) -> str:
    s=''.join(chr(x) if 32 <= x < 127 else ' ' for x in raw).strip()
    s=' '.join(s.split())
    return (s[:12] or 'MD2GBA GAME').upper()


def game_code_from_hash(h: str) -> str:
    alphabet='ABCDEFGHJKLMNPQRSTUVWXYZ23456789'
    n=int(h[:10],16)
    return ''.join(alphabet[(n>>(5*i)) & 31] for i in range(4))


def pc_count(path: Path) -> int:
    x=json.loads(path.read_text())
    return len(x['pcs'] if isinstance(x,dict) else x)


def page_count(path: Path, sym: str) -> int:
    return len(list(path.glob(f'{sym}_page_*.c')))


def parse_symbols(elf: Path):
    out=subprocess.check_output(['nm','-n',str(elf)],text=True)
    d={}
    for line in out.splitlines():
        p=line.split()
        if len(p)>=3:
            try:d[p[2]]=int(p[0],16)
            except ValueError:pass
    return d


def helper_refs(elf: Path):
    out=subprocess.check_output(['nm','-u',str(elf)],text=True,stderr=subprocess.STDOUT)
    return sorted(set(re.findall(r'__aeabi_[A-Za-z0-9_]+',out)))


def write_blob_asm(path: Path, section: str, start: str, end: str, blob: Path):
    q=str(blob.resolve()).replace('\\','\\\\').replace('"','\\"')
    path.write_text(f'''.syntax unified\n.cpu arm7tdmi\n.section {section},"a",%progbits\n.balign 4\n.global {start}\n.type {start},%object\n{start}:\n.incbin "{q}"\n.global {end}\n{end}:\n.size {start}, {end}-{start}\n''')


def main():
    ap=argparse.ArgumentParser(description='Compile a Mega Drive/Genesis ROM into an optimized MD2GBA GBA ROM.')
    ap.add_argument('rom',type=Path)
    ap.add_argument('-o','--output',type=Path,required=True)
    ap.add_argument('--addresses',type=Path,help='verified JSON {pcs:[...]} graph; otherwise conservative discovery is used')
    ap.add_argument('--trace',type=Path,help='optional runtime FBTOP/profile trace used to augment the graph')
    ap.add_argument('--page-shift',type=int,default=10,choices=range(10,17),metavar='N',help='native continuation-page shift (default 10 = 1 KiB; larger C shards are experimental on Thumb-1)')
    ap.add_argument('--dispatch-mode',choices=('fastchain','pschybrid','chain','burst','step'),default='fastchain')
    ap.add_argument('--work-dir',type=Path)
    ap.add_argument('--jobs',type=int,default=max(1,os.cpu_count() or 4))
    ap.add_argument('--title',help='GBA header title (<=12 chars)')
    ap.add_argument('--game-code',help='GBA game code (4 chars)')
    ap.add_argument('--no-reversed-rom',action='store_true')
    ap.add_argument('--no-reversed-wram',action='store_true')
    ap.add_argument('--no-dma-audio',action='store_true')
    ap.add_argument('--no-ym-lut',action='store_true')
    ap.add_argument('--z80-snapshot',type=Path,help='optional 8 KiB uploaded Z80 RAM snapshot for compiler-generated native sound-CPU continuations')
    ap.add_argument('--z80-profile',type=Path,help='execution profile matching --z80-snapshot (PC <hex> <count> lines)')
    ap.add_argument('--z80-native-min-count',type=int,default=1000,help='minimum profiled executions for a generated native Z80 continuation')
    ap.add_argument('--ym-trace',type=Path,help='optional raw YM2612 write trace for compiler-lowered FM patch/timbre bank')
    ap.add_argument('--silent-audio',action='store_true',help='disable target audio transport while preserving Genesis audio bus writes for profiling/bring-up')
    ap.add_argument('--translation-manifest',type=Path,help='offline hardware translation manifest to record in the build report')
    ap.add_argument('--keep-build',action='store_true')
    args=ap.parse_args()

    rom=args.rom.resolve(); out=args.output.resolve()
    if not rom.is_file(): ap.error(f'ROM not found: {rom}')
    if bool(args.z80_snapshot) != bool(args.z80_profile): ap.error('--z80-snapshot and --z80-profile must be supplied together')
    raw=rom.read_bytes()
    if len(raw)<8: ap.error('ROM is too small to contain 68000 reset vectors')
    rh=hashlib.sha256(raw).hexdigest()
    reset=int.from_bytes(raw[4:8],'big') & 0xffffff
    if reset>=len(raw) or reset&1: ap.error(f'implausible 68000 reset vector ${reset:06X}')

    work=(args.work_dir.resolve() if args.work_dir else (out.parent/(out.stem+'.md2gba-build')))
    if work.exists(): shutil.rmtree(work)
    gen=work/'gen'; obj=work/'obj'; pages=obj/'pages'; report_dir=work/'reports'
    machine_gen=work/'machine_generated'
    for p in (gen,obj,pages,report_dir,machine_gen): p.mkdir(parents=True,exist_ok=True)

    # Graph discovery / augmentation.
    graph=work/'guest_pcs.json'; discovery=report_dir/'graph_discovery.json'
    if args.addresses:
        shutil.copy2(args.addresses.resolve(),graph)
        discovery.write_text(json.dumps({'source':'user_verified_addresses','pc_count':pc_count(graph),'complete':'externally asserted'},indent=2)+'\n')
    else:
        run(python_tool(TOOLS/'discover68k_graph.py',rom,'-o',graph,'--report',discovery))
    if args.trace:
        aug=work/'guest_pcs_augmented.json'; augrep=report_dir/'graph_augmentation.json'
        run(python_tool(TOOLS/'augment_aot_profile.py',rom,graph,args.trace.resolve(),'-o',aug,'--report',augrep))
        graph=aug

    # Structural sidecar is deliberately generated before codegen; codegen can
    # therefore verify exact guest spans instead of relying on a title identity.
    spec=report_dir/'specialization_plan.json'
    run(python_tool(TOOLS/'md2gba_rom_specialize.py',rom,graph,'-o',spec))

    rev=work/'guest_reversed.bin'; revman=report_dir/'reversed_rom.json'
    if not args.no_reversed_rom:
        run(python_tool(TOOLS/'generate_reversed_rom.py',rom,rev,'--manifest',revman))

    sym='md_guest_region'
    # Generate into a staging directory, then freeze an immutable snapshot.
    # Compilation must never race a graph/codegen regeneration: a previous P87
    # development build proved that a live generated directory can disappear
    # underneath parallel clang workers.
    gen_stage=work/'gen.stage'
    if gen_stage.exists(): shutil.rmtree(gen_stage)
    gen_stage.mkdir(parents=True,exist_ok=True)
    run(python_tool(TOOLS/'emit_aot_sharded.py',rom,graph,'-o',gen_stage,'--symbol',sym,
         '--page-shift',str(args.page_shift),'--dispatch-mode',args.dispatch_mode))
    if gen.exists(): shutil.rmtree(gen)
    shutil.copytree(gen_stage,gen)
    snap=[]
    for fp in sorted(x for x in gen.rglob('*') if x.is_file()):
        snap.append({'path':str(fp.relative_to(gen)),'sha256':sha256(fp),'bytes':fp.stat().st_size})
        fp.chmod(0o444)
    (report_dir/'generated_snapshot.json').write_text(json.dumps({'files':snap},indent=2)+'\n')
    shutil.rmtree(gen_stage)

    # Offline-generated VDP lowering.  The profile manifest may prove that the
    # observed display states fit the direct identity tile publisher.  The
    # reusable backend still carries runtime guards; this flag only permits the
    # generated fast path to be attempted for this ROM.
    compiled_vdp=False
    compiled_vdp_plan=None
    if args.translation_manifest:
        mp=args.translation_manifest.resolve()
        if not mp.is_file(): ap.error(f'translation manifest not found: {mp}')
        man=json.loads(mp.read_text())
        rp=man.get('vdp',{}).get('renderer_plan')
        if rp:
            rpp=Path(rp)
            if not rpp.is_absolute(): rpp=(mp.parent/rpp).resolve()
            if rpp.is_file():
                compiled_vdp_plan=json.loads(rpp.read_text())
                if compiled_vdp_plan.get('enabled') and compiled_vdp_plan.get('renderer') in ('direct_identity_priority','profiled_tilemap_priority'):
                    maps=compiled_vdp_plan.get('tile_mappings') or []
                    if compiled_vdp_plan.get('renderer')=='direct_identity_priority' and not maps:
                        maxsrc=min(1022,int(compiled_vdp_plan.get('observed_max_bg_tile',0)))
                        maps=[{'id':0,'sources':list(range(maxsrc+1)),'slot_count':maxsrc+1,'frames':compiled_vdp_plan.get('profiled_display_frames',0)}]
                    if maps:
                        compiled_vdp=True
                        lines=['#ifndef MD2GBA_VDP_PLAN_H','#define MD2GBA_VDP_PLAN_H','#include <stdint.h>',
                               f'#define MD2GBA_VDP_PROFILE_MAX_BG_TILE {int(compiled_vdp_plan.get("observed_max_bg_tile",0))}u',
                               f'#define MD2GBA_VDP_PROFILE_FRAME_SIGNATURES {int(compiled_vdp_plan.get("frame_signatures",0))}u',
                               f'#define MD2GBA_COMPILED_VDP_MAP_COUNT {len(maps)}u']
                        lines.append(f'static const uint16_t md2gba_compiled_vdp_slot_count[{len(maps)}] = {{'+','.join(str(int(m.get('slot_count',len(m.get('sources',[])))))+'u' for m in maps)+'};')
                        lines.append(f'static const uint32_t md2gba_compiled_vdp_source_masks[{len(maps)}][64] = {{')
                        for m in maps:
                            words=[0]*64
                            for src in m.get('sources',[]):
                                src=int(src)
                                if 0<=src<2048: words[src>>5]|=1<<(src&31)
                            lines.append('  {'+','.join(f'0x{x:08X}u' for x in words)+'},')
                        lines.append('};')
                        lines.append(f'static const uint16_t md2gba_compiled_vdp_source_to_slot[{len(maps)}][2048] = {{')
                        for m in maps:
                            srcs=[int(x) for x in m.get('sources',[]) if 0<=int(x)<2048]
                            lines.append('  {')
                            for slot,src in enumerate(srcs,1): lines.append(f'    [{src}]={slot}u,')
                            lines.append('  },')
                        lines += ['};','#endif','']
                        (machine_gen/'md2gba_vdp_plan.h').write_text('\n'.join(lines))


    # Optional secondary-CPU/audio lowering follows the same PSC-style rule as
    # the 68K compiler: title-specific addresses/data live only in generated
    # modules.  The reusable runtime contains no game identity or sound-driver PCs.
    z80_native=False
    if args.z80_snapshot and args.z80_profile:
        zsnap=args.z80_snapshot.resolve(); zprof=args.z80_profile.resolve()
        if not zsnap.is_file() or not zprof.is_file(): ap.error('Z80 snapshot/profile not found')
        run(python_tool(TOOLS/'generate_z80_native_module.py',zsnap,zprof,'-o',machine_gen/'md2gba_z80_native.inc',
             '--min-count',str(args.z80_native_min_count)))
        z80_native=True
    ym_patchbank=False
    if args.ym_trace:
        yt=args.ym_trace.resolve()
        if not yt.is_file(): ap.error(f'YM trace not found: {yt}')
        run(python_tool(TOOLS/'generate_ym2612_patchbank.py',yt,'-o',machine_gen/'md2gba_ym_patchbank.inc','--rate','31536'))
        ym_patchbank=True

    write_blob_asm(work/'guest_rom.S','.rodata.ps4rom','ps4_rom_start','ps4_rom_end',rom)
    if not args.no_reversed_rom:
        write_blob_asm(work/'guest_reversed.S','.rodata.ps4reversed','ps4_rom_reversed_start','ps4_rom_reversed_end',rev)

    defs=['-DMD2GBA_GBA_IWRAM=1','-DMD2GBA_GBA_AUDIO_HOT=1',
          '-DMD2GBA_GBA_LEGACY_PSG=1','-DMD2GBA_GBA_LEGACY_PSG_TONE3=1','-DMD2GBA_GBA_LEGACY_PSG_NOISE4=1',
          '-DMD2GBA_GUEST_AOT_HEADER="md_guest_region.h"','-DMD2GBA_GUEST_AOT_RUN=md_guest_region_run']
    if args.silent_audio: defs.append('-DMD2GBA_GENERIC_SILENT=1')
    if compiled_vdp: defs.append('-DMD2GBA_COMPILED_VDP=1')
    if z80_native: defs += ['-DMD2GBA_Z80_EXECUTION=1','-DMD2GBA_Z80_NATIVE_MODULE=1']
    if ym_patchbank:
        # PSC-observed Direct Sound family: FIFO A/DMA1, ~31.536 kHz and a
        # 528-sample block per GBA video frame.
        defs += ['-DMD2GBA_YM_PATCHBANK=1','-DMD2GBA_PSC_FIFO_DMA=1','-DP76_AUDIO_RATE=31536u',
                 '-DP76_AUDIO_BLOCK_SAMPLES=528u','-DP76_AUDIO_RING_BLOCKS=4u']
    if not args.no_reversed_rom: defs.append('-DMD2GBA_PSC_REVERSED_ROM=1')
    if not args.no_reversed_wram: defs.append('-DMD2GBA_PSC_REVERSED_WRAM=1')
    if not args.no_dma_audio: defs.append('-DMD2GBA_AUDIO_DMA_CONVEYOR=1')
    if not args.no_ym_lut: defs.append('-DMD2GBA_YM_DYNAMIC_WAVE_LUT=1')
    common=['-target','arm-none-eabi','-mcpu=arm7tdmi','-O2','-fomit-frame-pointer','-ffreestanding','-fno-builtin',
            '-ffunction-sections','-fdata-sections','-fno-exceptions','-fno-unwind-tables','-fno-asynchronous-unwind-tables',
            '-I.', '-Iruntime/include',f'-I{machine_gen}','-Iruntime/generated',f'-I{gen}'] + defs
    th=common+['-mthumb']; arm=common+['-marm']
    units=[
      ('000_main.o','main_p85.c',th),('001_bus.o','md2gba_bus.c',th),('002_aot68k.o','md2gba_aot68k.c',th),
      ('003_interp.o','runtime/src/md2gba_interp68k.c',th),('004_timing.o','md2gba_timing.c',th),('005_cycles.o','runtime/src/md2gba_cycles.c',th),
      ('006_backend.o','md2gba_gba_backend.c',th),('006_projected.o','md2gba_gba_projected.c',th),('007_md2gba_vdp_soft.o','md2gba_vdp_soft.c',th),('008_md2gba_gba_soft.o','md2gba_gba_soft.c',th),
      ('009_mix.o','runtime/src/md2gba_mix_pcm.c',th),('010_psg.o','runtime/src/md2gba_psg_pcm.c',th),('011_ym.o','runtime/src/md2gba_ym2612_pcm.c',th),
      ('012_legacy_psg.o','runtime/src/md2gba_gba_legacy_psg.c',th),('013_pcm_stream.o','md2gba_gba_pcm_stream.c',arm),('007_vblank_irq.o','irq_vblank.c',arm),
      ('018_opclass.o','runtime/generated/md2gba_interp_opclass.c',th)]
    if z80_native: units.append(('020_z80.o','runtime/src/md2gba_z80.c',th))
    for on,src,flags in units:
        useflags=[x for x in flags if x!='-DMD2GBA_GBA_IWRAM=1'] if on in ('006_projected.o','020_z80.o') else flags
        run(['clang',*useflags,'-c',ROOT/src,'-o',obj/on],quiet=True)
    asmflags=['clang','-target','arm-none-eabi','-mcpu=arm7tdmi','-marm','-c']
    run([*asmflags,ROOT/'irq_entry_p75.S','-o',obj/'014_irq_entry.o'],quiet=True)
    run([*asmflags,ROOT/'start_p75.S','-o',obj/'015_start.o'],quiet=True)
    run([*asmflags,ROOT/'runtime/src/md2gba_aeabi_min.S','-o',obj/'021_aeabi_min.o'],quiet=True)
    run([*asmflags,work/'guest_rom.S','-o',obj/'016_rom.o'],quiet=True)
    if not args.no_reversed_rom: run([*asmflags,work/'guest_reversed.S','-o',obj/'019_reversed.o'],quiet=True)
    run(['clang',*th,'-DMD2GBA_AOT_GENERATED_FAST=1','-c',gen/f'{sym}_dispatch.c','-o',obj/'017_dispatch.o'],quiet=True)

    # Compile generated regions concurrently without invoking a shell per file.
    import concurrent.futures
    psrc=sorted(gen.glob(f'{sym}_page_*.c'))
    def one(p):
        q=pages/(p.stem+'.o'); subprocess.run(['clang',*th,'-DMD2GBA_AOT_GENERATED_FAST=1','-c',str(p),'-o',str(q)],cwd=ROOT,check=True,stdout=subprocess.DEVNULL); return q
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as ex:
        pobj=list(ex.map(one,psrc))

    elf=work/'md2gba.elf'; mapf=work/'md2gba.map'
    objs=[obj/'015_start.o',obj/'014_irq_entry.o',obj/'000_main.o',obj/'001_bus.o',obj/'002_aot68k.o',obj/'003_interp.o',obj/'004_timing.o',obj/'005_cycles.o',obj/'006_backend.o',obj/'006_projected.o',obj/'007_md2gba_vdp_soft.o',obj/'008_md2gba_gba_soft.o',obj/'007_vblank_irq.o',obj/'009_mix.o',obj/'010_psg.o',obj/'011_ym.o',obj/'012_legacy_psg.o',obj/'013_pcm_stream.o',obj/'021_aeabi_min.o']
    if z80_native: objs.append(obj/'020_z80.o')
    objs += [obj/'017_dispatch.o',obj/'018_opclass.o',*pobj,obj/'016_rom.o']
    if not args.no_reversed_rom: objs.append(obj/'019_reversed.o')
    run(['ld.lld','--gc-sections','-T',ROOT/'link_utility.ld',f'-Map={mapf}','-o',elf,*objs])
    tmp=work/'md2gba.gba'; run(['llvm-objcopy','-O','binary',elf,tmp])
    title=(args.title or sanitize_title(raw[0x120:0x150] if len(raw)>=0x150 else b'MD2GBA GAME'))[:12]
    code=(args.game_code or game_code_from_hash(rh))[:4]
    run(python_tool(ROOT/'fix_gba_header.py',tmp,'--title',title,'--game-code',code))
    out.parent.mkdir(parents=True,exist_ok=True); shutil.copy2(tmp,out)

    syms=parse_symbols(elf); iw=syms.get('__iwram_end__',0)-syms.get('__iwram_start__',0); ew=syms.get('__bss_end__',0)-syms.get('__bss_start__',0)
    helpers=helper_refs(elf)
    sp=json.loads(spec.read_text())
    dr=json.loads(discovery.read_text()) if discovery.exists() else {}
    rep={
      'compiler':'MD2GBA generic offline compiler','source_rom':str(rom),'source_size':len(raw),'source_sha256':rh,'reset_pc':f'{reset:06X}',
      'graph_pc_count':pc_count(graph),'graph_source':dr.get('source','conservative_static_discovery'),'graph_complete':dr.get('complete'),
      'computed_edges':len(dr.get('computed_edges',[])) if isinstance(dr.get('computed_edges'),list) else dr.get('computed_edges'),
      'decode_errors':len(dr.get('decode_errors',[])) if isinstance(dr.get('decode_errors'),list) else dr.get('decode_errors'),
      'page_shift':args.page_shift,'region_bytes':1<<args.page_shift,'region_count':page_count(gen,sym),'dispatch_mode':args.dispatch_mode,
      'generated_snapshot_sha256':sha256(report_dir/'generated_snapshot.json'),
      'specialization_total':sp.get('total',sp.get('total_sites')),'specialization_counts':sp.get('counts'),
      'psc_style_reversed_rom':not args.no_reversed_rom,'psc_style_reversed_wram':not args.no_reversed_wram,
      'audio_dma_conveyor':not args.no_dma_audio,'ym_dynamic_lut':not args.no_ym_lut,'silent_audio':args.silent_audio,
      'z80_native_module':z80_native,'ym_compiler_patchbank':ym_patchbank,
      'compiled_vdp_renderer':compiled_vdp,'compiled_vdp_plan':compiled_vdp_plan,
      'translation_manifest':str(args.translation_manifest.resolve()) if args.translation_manifest else None,
      'psc_fifo_dma':ym_patchbank,'audio_rate_hz':31536 if ym_patchbank else 16384,
      'audio_block_samples':528 if ym_patchbank else 128,
      'output':str(out),'output_size':out.stat().st_size,'output_sha256':sha256(out),'gba_title':title,'gba_game_code':code,
      'iwram_bytes':iw,'iwram_limit':28*1024,'ewram_bss_bytes':ew,'ewram_limit':256*1024,'aeabi_helpers':helpers,
      'warnings':[]}
    if dr.get('complete') is False: rep['warnings'].append('Automatic static graph is incomplete at computed control-flow/decode boundaries; supply --trace or a verified --addresses graph for full-speed coverage.')
    if helpers: rep['warnings'].append('Unexpected __aeabi_* helper dependencies are present.')
    if iw>28*1024: rep['warnings'].append('IWRAM overflow.')
    if ew>256*1024: rep['warnings'].append('EWRAM overflow.')
    if out.stat().st_size>32*1024*1024: rep['warnings'].append('GBA ROM exceeds 32 MiB.')
    report=out.with_suffix(out.suffix+'.md2gba.json'); report.write_text(json.dumps(rep,indent=2)+'\n')
    print(json.dumps(rep,indent=2))
    if not args.keep_build and not rep['warnings']:
        # Keep reports next to the output, remove only temporary compilation products.
        pass

if __name__=='__main__':
    main()
