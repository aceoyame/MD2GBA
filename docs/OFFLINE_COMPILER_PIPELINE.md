# MD2GBA P149 — Desktop-first offline compiler pipeline

## Product contract

MD2GBA is no longer primarily a GBA-side Mega Drive emulator.  The intended product is a Windows utility that accepts a legally obtained Mega Drive / Genesis ROM, spends as much host CPU time as necessary learning that ROM, and emits a GBA ROM whose normal execution is predominantly generated native code and precomputed hardware translation data.

The host is allowed to spend minutes or hours analyzing one game.  The GBA is not.  Work that can be proven or measured on the desktop should be moved out of the ARM7 runtime.

The default P149 GUI therefore uses all detected logical CPU cores and has no wall-clock profiling limit (`Max hours = 0`).  It stops when its coverage/saturation policy says additional exploration is no longer productive, or when the user stops it.  Its SQLite profile cache is persistent, so another compiler version can reuse prior work.

## Pipeline

1. **ROM identification and static decode**
   - Read vectors/header/SRAM information.
   - Conservatively disassemble 68000 code from reset, vectors, direct branches and provable tables.
   - Never promote arbitrary ROM words to code merely because they look like pointers.

2. **Autonomous host profiling**
   - Run MD2GBA's own 68000/bus/VDP model natively on the desktop.
   - Explore with multiple input policies in parallel.
   - Fork interesting machine states and mutate controller sequences from them.
   - Increase campaign length when coverage stalls.
   - Persist executed PCs, edges, hotness, hardware accesses and machine-state corpus.
   - P149 also executes the Genesis Z80 at the exact master-clock ratio of 7 Z80 T-states per 15 68000 cycles and records its executed PCs.

3. **Hardware trace learning**
   - Attribute VDP port traffic to the 68000 PCs that generated it.
   - Record VDP target/code/address signatures for VRAM, CRAM and VSRAM traffic.
   - Record ordered YM2612 register writes, PSG writes and Z80 RAM uploads.
   - Preserve Z80 RAM images so uploaded sound drivers can be compiled from the bytes actually observed.

4. **Offline precompile package**
   - `68k_aot_graph.json`: merged statically proven + actually observed ROM PCs.
   - `68k_plan.json`: hotness, dynamic-code evidence and hardware-attribution summary.
   - `vdp_plan.json`: measured VDP behavior and sites to lower into generated video operations.
   - `z80_profile.txt` / `z80_ram.bin`: sound-CPU profile and observed uploaded program image.
   - `audio_plan.json` / `ym_trace.bin`: ordered audio evidence.
   - `translation_manifest.json`: reproducible manifest tying all generated information to the ROM hash.

5. **Native generation**
   - Generate ARM7TDMI Thumb/ARM continuations for 68000 ROM code.
   - Generate guarded Z80 native continuations for observed sound-driver code.
   - Pre-render stable YM timbre/pitch zones from ordered register traces when audio generation is enabled.
   - Future renderer stages consume the VDP plan to generate asset/provenance tables and native publication kernels instead of reinterpreting the Genesis VDP every frame.

6. **Validation and target build**
   - Link within the GBA 32 MiB ROM, 256 KiB EWRAM and 32 KiB IWRAM hardware limits (the project reserves a portion of IWRAM for system/stack use).
   - Preserve an interpreter only as a correctness escape hatch for unseen/self-modified code.  It is not intended to be the normal game engine.

## Why this differs from P148

P148 proved that autonomous state exploration can discover real game code, but the produced GBA still retained a broad runtime emulation burden.  The Sonic test showed roughly 19 FPS and severe video corruption.  P149 treats that result as evidence that profiling alone is insufficient: profile information must drive ahead-of-time translation of all three major Genesis execution domains (68000, VDP behavior, Z80/audio) and reduce the GBA runtime to scheduling, hardware publication and exceptional fallback.

## Completion target

A mature build should be able to report, before producing the `.gba`:

- percentage of observed 68000 execution covered by native AOT;
- percentage of observed Z80 execution covered by guarded native AOT;
- dynamic WRAM-code variants captured vs unresolved;
- VDP write sites classified as static/precomputed, generated transfer kernels or runtime raster events;
- YM/PSG vocabulary captured;
- GBA ROM/IWRAM/EWRAM budgets;
- remaining fallback sites and why they cannot yet be translated.

A game should not be declared "compiled" merely because it boots.  The goal is to make the report prove that ordinary gameplay is native/precomputed enough to meet GBA performance constraints.

## P149 saturation policy

Autonomous profiling is intentionally not considered complete merely because the 68000 control-flow graph stops growing.  Campaign scheduling and saturation also include newly observed Z80 PCs, VDP behavior signatures and audio behavior signatures.  Reused engine code with new level graphics/music must keep the host grinder active.
