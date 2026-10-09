# MD2GBA — P150 ProfileMap Windows GUI

Experimental desktop converter for compiling Sega Genesis/Mega Drive ROM behavior and observed VDP patterns into Game Boy Advance ROMs. This is a **development prototype**, not a reliable arbitrary-game converter.

Snapshot: **P150 PROFILEMAP GUI**, 2026-10-05. See [P150_HANDOFF.md](P150_HANDOFF.md), [README_FIRST.md](README_FIRST.md) and [docs/](docs/) for architecture and current restrictions.

## Windows
- Python GUI entry: `MD2GBA_GUI.pyw` / `RUN_MD2GBA_GUI.cmd` (requires Python 3 and project dependencies).
- Standalone launcher source in `windows/launcher.go`; `BUILD_WINDOWS_EXE.cmd` builds an optional PyInstaller package.
- The original bundled `MD2GBA.exe` is intentionally **not committed**. Build locally. LLVM/Clang is needed for native GBA outputs.

## Pipeline
`tools/md2gba_autoprofile.py`, `tools/md2gba_precompile.py` and `tools/md2gba_compile.py` contain profiling/precompilation components. `md2gba_gba_backend.c` contains the target runtime, including guarded generated VDP map support.

## Current caveats
P150 validation is a cropped 1:1 presenter; dynamic VRAM graphics still require some runtime conversion, sprite lowering is not complete, fallback paths remain expensive, and the Sonic 1 validation ROM had no audio. Visual correctness/performance should not be assumed without new tests.

This import removes the bundled executable, game-specific ROM/capture data and generated binaries. Developers must supply their own lawfully obtained test ROMs. No Sega/Nintendo/third-party source, artwork, or license is granted. This is a public experimental source archive; code provenance and redistribution rights for incorporated third-party material remain the responsibility of contributors.

See [CLEANUP_REPORT.md](CLEANUP_REPORT.md) for cleanup details.
