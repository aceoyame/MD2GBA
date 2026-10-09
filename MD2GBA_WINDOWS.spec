# PyInstaller spec for the Windows desktop utility.
from pathlib import Path
from PyInstaller.utils.hooks import collect_submodules

ROOT=Path(SPECPATH)
PROJECT=ROOT
skip_parts={'test_inputs','.git','__pycache__','.pytest_cache'}
allowed={'.py','.pyw','.c','.h','.S','.s','.inc','.ld','.json','.csv','.md','.txt','.cmd'}
datas=[]
for p in PROJECT.rglob('*'):
    if not p.is_file(): continue
    rel=p.relative_to(PROJECT)
    if any(x in skip_parts for x in rel.parts): continue
    if rel.parts and rel.parts[0] in {'dist','build'}: continue
    if p.suffix not in allowed and p.name not in {'md2gba.cmd','md2gba.sh'}: continue
    # The entry script is frozen as code; project files are also embedded below
    # because the utility compiles generated C/ASM at runtime.
    dest=str(Path('MD2GBA')/rel.parent)
    datas.append((str(p),dest))

a=Analysis(
    [str(PROJECT/'tools'/'md2gba_app.py')],
    pathex=[str(PROJECT/'tools'),str(PROJECT)],
    binaries=[], datas=datas,
    hiddenimports=collect_submodules('tkinter'),
    hookspath=[], hooksconfig={}, runtime_hooks=[], excludes=[], noarchive=False,
)
pyz=PYZ(a.pure)
exe=EXE(
    pyz,a.scripts,a.binaries,a.datas,[],
    name='MD2GBA',debug=False,bootloader_ignore_signals=False,strip=False,upx=False,
    console=False,disable_windowed_traceback=False,target_arch=None,codesign_identity=None,entitlements_file=None,
)
