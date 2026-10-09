#!/usr/bin/env python3
from pathlib import Path
import runpy, sys
p=Path(__file__).resolve().parent/'tools'/'md2gba_app.py'
sys.argv=[str(p),*sys.argv[1:]]
runpy.run_path(str(p),run_name='__main__')
