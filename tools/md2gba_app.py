#!/usr/bin/env python3
"""Single entry point used by the Windows MD2GBA.exe build.

The frozen EXE can launch the GUI, act as the CLI backend, or execute one of the
bundled compiler helper scripts.  This avoids depending on a separately
installed Python interpreter after PyInstaller packages the application.
LLVM/Clang is still an external toolchain dependency in P150.
"""
from __future__ import annotations
import os, runpy, sys
from pathlib import Path


def bundle_root()->Path:
    if getattr(sys,'frozen',False):
        base=Path(getattr(sys,'_MEIPASS',Path(sys.executable).parent))/'MD2GBA'
        os.environ['MD2GBA_FROZEN']='1'
    else:
        base=Path(__file__).resolve().parents[1]
    os.environ['MD2GBA_BUNDLE_ROOT']=str(base)
    t=str(base/'tools')
    if t not in sys.path:sys.path.insert(0,t)
    return base

def run_backend(args):
    root=bundle_root();sys.argv=['md2gba',*args]
    runpy.run_path(str(root/'tools'/'md2gba_utility.py'),run_name='__main__')

def run_internal(rel,args):
    root=bundle_root();p=(root/rel).resolve()
    if root.resolve() not in p.parents and p!=root.resolve(): raise SystemExit('invalid internal tool path')
    sys.argv=[str(p),*args];runpy.run_path(str(p),run_name='__main__')

def main():
    root=bundle_root();args=sys.argv[1:]
    if args and args[0]=='--internal-tool':
        if len(args)<2:raise SystemExit('--internal-tool requires a relative path')
        return run_internal(args[1],args[2:])
    if args and args[0]=='--backend':return run_backend(args[1:])
    commands={'doctor','analyze','status','profile','auto-profile','plan','precompile','build','pipeline','grind'}
    if args and args[0] in commands:return run_backend(args)
    # A dragged ROM is the only GUI argument we preserve.
    gui=root/'tools'/'md2gba_gui.pyw';sys.argv=[str(gui),*args];runpy.run_path(str(gui),run_name='__main__')
if __name__=='__main__':main()
