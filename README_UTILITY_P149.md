# MD2GBA P149 AOTGRINDER GUI

P149 implements the next step toward the original MD2GBA product goal: a Windows application that can ingest a Genesis/Mega Drive ROM and autonomously grind through analysis, execution profiling, hardware behavior learning and ahead-of-time generation until it has enough evidence to build a GBA target.

## Why P149 exists

P148 proved that coverage-guided state exploration works, but the resulting Sonic 1 GBA remained far too runtime-heavy.  User testing showed roughly 19 FPS plus major tile/sprite/palette corruption.  That result is accepted as a design signal: merely discovering more 68K PCs is not enough.  VDP behavior, Z80 execution and audio behavior also need to be learned and lowered on the desktop.

P149 does not attempt to hide that failure behind game-specific Sonic hacks.  It makes the offline compiler smarter and records enough evidence for later generators to replace the expensive generic target paths.

## GUI / Windows application

`MD2GBA.exe` is a cross-compiled Windows x86-64 PE GUI launcher included in the package.  It launches `MD2GBA_GUI.pyw` with no console window and forwards a ROM dropped onto the EXE.

Source-mode requirements:

- Windows 10/11 recommended;
- Python 3 with Tk;
- LLVM tools on PATH: `clang`, `ld.lld`, `llvm-objcopy`.

For a frozen Python application, `BUILD_WINDOWS_EXE.cmd` invokes the supplied `MD2GBA_WINDOWS.spec`.  The resulting `dist\MD2GBA.exe` embeds the Python GUI/backend/compiler scripts.  LLVM is still external in P149.

## Autonomous build behavior

The GUI's **AUTONOMOUS AOT BUILD** runs the same backend as:

    md2gba grind game.bin -o game.gba

The default `Max hours = 0` is deliberate: the profiler has no clock deadline and stops only when the saturation policy says further exploration is not discovering enough new behavior, or when the user stops it.

All detected logical cores are used by default.  Worker count can be capped in the GUI.

## What the profiler learns

### 68000

The desktop interpreter records exact PCs, control-flow edges, opcode classes, execution counts, VDP/audio/Z80 interactions and dynamic-RAM execution.  Statically proven code and actually observed ROM code are merged for native AOT generation.  Arbitrary pointer-looking ROM data is never promoted to executable code merely by appearance.

### Z80

P149 runs the Genesis Z80 alongside the 68K using the master-clock ratio rather than ignoring the sound CPU.  It records Z80 PCs and retains observed 8 KiB Z80 RAM images.  Generated Z80 continuations are guarded against the uploaded driver bytes they were built from.

### VDP

P149 records both compact VDP event signatures and the actual observed VDP word/value vocabulary: producing 68K PC, access type, VDP target, code, pending-control state, address and value.  This is the raw evidence required to infer static assets, DMA/copy/fill kernels, palette writes, scroll modes and dynamic/raster sites offline.

### Audio

The profiler records YM2612, PSG and Z80-upload event signatures and keeps ordered YM register traces.  Those traces are intended to drive offline reconstruction of patches, envelopes, pitch zones and native GBA mixer events rather than re-running a full Genesis audio stack at runtime.

## Smarter saturation

P148 primarily considered 68K code coverage when deciding whether profiling had plateaued.  P149 scores campaign policies and saturation using four dimensions:

- new 68K PCs/edges;
- new Z80 PCs;
- new VDP behavior signatures;
- new audio behavior signatures.

That matters because a later level can execute the same engine code while loading completely different graphics, raster behavior or music.  The profiler should keep working when hardware semantics are still changing.

## Offline precompile package

`precompile` emits a ROM-hash-bound package containing:

- `68k_aot_graph.json`
- `68k_plan.json`
- `vdp_plan.json` including hot VDP values/sites
- `z80_profile.txt`
- `z80_ram.bin` when observed
- `audio_plan.json`
- `ym_trace.bin` when observed
- `dynamic_code_plan.json`
- `translation_manifest.json`

The final architecture is documented in `docs/OFFLINE_COMPILER_PIPELINE.md`, `docs/68K_AOT_TRANSLATION.md`, `docs/VDP_TRANSLATION.md`, and `docs/Z80_AUDIO_TRANSLATION.md`.

## Current target limitation

P149's target renderer is still inherited from the generic P142/P148 runtime.  Therefore the short P149 validation `.gba` should be treated only as a compiler/link integration test.  It is not evidence that Sonic graphics/performance are solved.

The next major implementation work should consume `vdp_plan.json` and its value vocabulary to generate game-profile-specific native GBA video publication code, then similarly reduce Z80/YM runtime work using the profiled driver/audio artifacts.
