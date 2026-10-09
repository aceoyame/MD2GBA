# MD2GBA P149 AOTGRINDER GUI — handoff

**Date:** 2026-10-05  
**User product goal:** a Windows utility that accepts a Mega Drive / Genesis ROM, autonomously spends substantial desktop compute profiling/analyzing it, and emits a GBA ROM after doing as much 68K/VDP/Z80/audio translation as possible ahead of time.

## User validation that triggered this branch

The P148 Sonic 1 validation ROM executed, but user testing showed approximately 19 FPS and severe graphical corruption (large wrong tiles/sprites/palette regions).  The user also correctly noted that the delivered checkpoint did not present an obvious compiled Windows GUI executable.

Do **not** respond by micro-fixing Sonic inside the generic runtime.  P149 is the architectural pivot toward the final utility.

## Completed in P149

### Windows GUI/application delivery

- `MD2GBA.exe`: actual Windows x86-64 PE GUI launcher, cross-compiled with Go.
- Double-click opens the P149 GUI; drag-and-drop ROM is forwarded.
- `RUN_MD2GBA_GUI.cmd` / `MD2GBA_GUI.pyw` remain source-mode alternatives.
- `BUILD_WINDOWS_EXE.cmd`, `.ps1`, and `MD2GBA_WINDOWS.spec` build a PyInstaller-frozen GUI/backend on Windows.
- The small included launcher still requires Python 3; frozen build removes that Python dependency.  LLVM remains external in this checkpoint.

### Autonomous grind defaults

- all logical CPU cores by default;
- zero-hour wall-clock limit by default: profile until saturation/user stop;
- state corpus default 512;
- reset lane default 20%;
- maximum adaptive campaign length 115,200 frames;
- SQLite cache reused/migrated in place.

### Host Z80 profiling

`host/md2gba_profile_runner.c` now runs `md2gba_z80.c` alongside the 68K using the Genesis master-clock ratio (68K MCLK/7, Z80 MCLK/15).  Z80 state is included in profiler save states.  VBlank raises Z80 IRQ.  The runner emits Z80 PC counts and the observed 8 KiB uploaded Z80 RAM image.

### VDP evidence

The profiler attributes VDP activity to its producing 68K PC and emits two levels of evidence:

- `.vdp.tsv`: compact PC/type/target/code/address event signatures;
- `.vdpvalues.tsv`: actual PC/type/target/code/pending/address/value signatures.

The second table is critical for future offline asset/transfer reconstruction.  The SQLite DB persists it in `observed_vdp_values`.

### Audio evidence

The profiler records YM2612 writes, PSG writes and Z80-RAM uploads, plus an ordered raw YM trace.  The precompiler selects/preserves useful observed audio and Z80-driver images.

### Semantic/hardware-guided autonomous scheduling

Campaign reward and saturation are no longer based on 68K PCs alone.  P149 includes new VDP signatures, Z80 PCs and audio signatures in policy reward and plateau detection.  A scene with familiar 68K engine code but new graphics/music therefore remains interesting.

### Offline translation package

`tools/md2gba_precompile.py` creates a deterministic, ROM-hash-bound package with 68K graph/hotness, VDP plans and actual value vocabulary, Z80 profile/image, audio trace/plan and dynamic-code evidence.

### Target plumbing

The compiler accepts the translation manifest.  Z80 generated/native support is integrated; Z80 generic runtime code was moved out of IWRAM when required to keep the target inside hardware limits.  This is temporary: later hotness-based placement should automatically choose ARM/Thumb/IWRAM/ROM tradeoffs.

## Validation

Short fresh Sonic 1 host profile after the P149 instrumentation:

- 889,416 68K instructions / 60 frames in one minimal campaign;
- 59 new 68K PCs / 65 edges;
- 25 new Z80 PCs;
- 112 new compact VDP signatures;
- 136 observed VDP value signatures in the later value-capture validation;
- 42 new audio signatures.

A deliberately short compiler-plumbing validation build also linked successfully (prior P149 run):

- 2,743 merged 68K PCs;
- 25 generated regions in the very short-profile build;
- output 3,143,152 bytes;
- IWRAM 25,996 / 28,672;
- EWRAM 255,848 / 262,144;
- no undefined helpers.

**Do not treat that ROM as a fixed Sonic build.** The P148 target renderer remains the known visual/performance problem.

## Next technical priorities

1. **Generate the renderer from the VDP plan.** Infer stable ROM/WRAM source provenance, DMA/copy/fill kernels, tile/palette conversion and raster schedule.  The GBA should publish pretranslated BG/OAM/palette state rather than emulate the Genesis VDP stream broadly.
2. **Capture dynamic source provenance.** VDP values alone are useful evidence but source-memory ranges and transformation lineage are needed to preconvert assets safely.
3. **Generate multiple guarded Z80 driver variants.** Current precompile selects one observed image; retain all materially distinct uploaded driver images and dispatch by hash/byte guard.
4. **Offline YM/PSG lowering.** Build native patch/instrument/sample/mixer tables from ordered traces and Z80 semantics.
5. **Dynamic WRAM 68K capture.** Save generated/copied RAM-code bytes with execution contexts and build guarded native variants.
6. **Host-side differential validation.** Before linking, compare generated 68K/VDP/Z80 transformations against the host semantic model across saved-state corpus states.
7. **Toolchain packaging.** Produce a distributable Windows bundle with a known-compatible LLVM subset if redistribution/licensing/size are acceptable.

## Architectural invariant

The desktop may take hours.  The GBA may not.  Any work that can be proven, profiled, converted, compressed, scheduled or specialized on the host should be done before target execution.  The target runtime should converge toward scheduling, GBA hardware publication and exceptional fallback—not a full Genesis emulator.
