#!/usr/bin/env python3
from pathlib import Path
import os,subprocess
root=Path(__file__).resolve().parents[1];os.chdir(root);Path('analysis').mkdir(exist_ok=True)
base=['cc','-O2','-ffunction-sections','-fdata-sections','-I.','-Iruntime/include','-Iruntime/generated']
checks=[('test_phase75_planner',['md2gba_gba_backend.c']),('test_phase76_dma',['md2gba_bus.c']),('test_phase76_audio_transport',['runtime/src/md2gba_psg_pcm.c']),('test_phase81_window_partial',['md2gba_vdp_soft.c','md2gba_gba_backend.c','md2gba_bus.c']),('test_phase82_bottom_ui',['md2gba_gba_backend.c','md2gba_vdp_soft.c']),('test_phase82_viewport',['md2gba_gba_backend.c']),('test_phase83_ui_fit',[]),('test_phase83_ui_panel',['md2gba_gba_backend.c']),('test_phase84_ym_shared_cache',['runtime/src/md2gba_ym2612_pcm.c']),('test_phase85_ym',['tests/phase85_ym_oracle.c','runtime/src/md2gba_ym2612_pcm.c']),('test_phase85_scroll',['md2gba_gba_backend.c','md2gba_bus.c'])]
def run(args,**kw):subprocess.run(args,check=True,**kw)
run(['python3','tools/generate_ym_scaled_sine.py','--check'])
for name,src in checks:
 exe='analysis/'+name;run(base+['tests/'+name+'.c']+src+['-Wl,--gc-sections','-o',exe]);run([exe])
for name in ['test_phase84_codegen_semantics','test_phase85_loop_fusion','test_phase85_store_fusion']:run(['python3','tests/'+name+'.py'])
run(base+['-c','tests/reference/md2gba_vdp_soft_p84.c','-o','analysis/soft84.o'])
symbols=subprocess.check_output(['nm','--defined-only','--extern-only','analysis/soft84.o'],text=True)
Path('analysis/soft84.syms').write_text(''.join(f'{s[2]} ref_{s[2]}\n'for l in symbols.splitlines()if len(s:=l.split())==3 and s[1]=='T'))
run(['objcopy','--redefine-syms=analysis/soft84.syms','analysis/soft84.o'])
run(base+['tests/test_phase85_projected.c','md2gba_vdp_soft.c','md2gba_gba_backend.c','md2gba_bus.c','analysis/soft84.o','-Wl,--gc-sections','-o','analysis/test_projected'])
run(['analysis/test_projected']+[str(p)for p in sorted(Path('tests/fixtures').glob('source-*.vdp'))])
