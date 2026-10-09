# MD2GBA P148 STATEFUZZ GUI

**Date:** 2026-09-29  
**Base:** P147 AUTOPROFILE GUI

P148 implements the next utility milestone: **coverage-guided save-state exploration**. The desktop profiler can now preserve exact Genesis machine states, fork new campaigns from useful states, mutate controller input, reward branches that discover new code/control flow, and prune stale states automatically.

## Windows GUI

Double-click `md2gba.cmd` or `md2gba_gui.cmd`.

SMART BUILD remains:

1. analyze ROM;
2. compile/update the native host profiler;
3. profile across all selected CPU workers;
4. build/refine a persistent state corpus;
5. merge observed PCs/edges/hardware events into SQLite;
6. generate the optimization/AOT plan;
7. compile the GBA with the selected worker count.

New GUI controls:

- **Coverage-guided save-state exploration** — enabled by default;
- **State corpus** — default 128 active states;
- **Reset exploration %** — default 25%, preserving boot/title exploration while the rest of the machine forks deeper states.

## Save-state model

`host/md2gba_profile_runner.c` now has a host-only profiling state format (`M2GSTATE`, version 1). A state stores:

- full 68000 register/context state;
- MD2GBA scheduler/timing and interrupt state;
- 68K WRAM;
- Z80 RAM and bus/reset state;
- cartridge SRAM state;
- VDP registers, VRAM, CRAM, VSRAM and DMA state;
- controller/hardware state;
- YM/PSG/Z80-facing bus state and counters.

Host pointers, callbacks and the ROM pointer are never persisted. On load, the state is bound to the current ROM after size + FNV64 identity checks and structure-size/version checks.

These are **profiling corpus states**, not a public emulator save-state format. They are intentionally tied to the profile-runner ABI; incompatible future runner versions reject them instead of silently loading bad state.

## Coverage-guided corpus scheduler

`tools/md2gba_autoprofile.py` now manages a persistent `state_corpus` table plus state files under the ROM work directory.

The scheduler:

- begins from reset when no corpus exists;
- snapshots long campaigns at bounded intervals plus their furthest clean state;
- promotes states only from campaigns that add new PCs/edges;
- scores parents by measured novelty and successful descendants;
- forks later campaigns from productive states with new policy/seed mutations;
- rewards deeper successful descendants without abandoning broad exploration;
- reserves a configurable percentage of reset-from-boot workers;
- caps the active corpus and prunes low-value/overused shallow states;
- persists corpus depth, usage, success and discovery history in SQLite;
- automatically deactivates missing/incompatible states rather than invalidating the whole profile cache.

No ROM address, Sonic behavior or PSIV behavior is encoded in the state scheduler.

## Validation performed

### Exact state resume

Sonic 1, neutral input:

- continuous run: reset -> frame 120;
- forked run: reset -> frame 60 -> save -> reload -> another 60 frames;
- resulting frame-120 serialized machine states were **byte-for-byte identical**.

This validates the profiler snapshot/restore path for the current runner ABI.

### Autonomous state exploration

A fresh Sonic 1 database was intentionally run with very short 120-frame campaigns to make scheduler behavior visible quickly.

Epoch 0 (all reset starts):

- 463 observed PCs;
- 506 edges.

Epoch 1:

- 3 of 4 workers started from promoted states;
- those descendants discovered +436 PCs and +499 edges;
- merged coverage reached 899 PCs / 1,005 edges;
- corpus reached depth 2 immediately.

During the short validation session the bounded corpus reached:

- 32 active states (cap reached/pruned);
- maximum lineage depth 6;
- 78 campaigns started from saved states;
- 35 saved-state campaigns produced new PC and/or edge coverage.

The validation is intentionally a scheduler/architecture test, not a claim of complete Sonic coverage.

### Generated target

The validation profile fed the normal generic AOT compiler successfully:

- merged generated instruction entries: 7,466;
- AOT page regions: 64;
- output: `SONIC1_P148_STATEFUZZ_VALIDATION.gba`;
- output size: 4,344,300 bytes;
- target audio remains silent by design while generic target audio is unresolved.

## CLI

State exploration is on by default:

    md2gba.cmd auto-profile game.bin --hours 6

Useful controls:

    --no-state-exploration
    --reset-fraction 0.25
    --max-corpus 128
    --snapshot-slices 4
    --promote-per-success 2

`md2gba.cmd status game.bin` reports active corpus size, maximum depth, state uses and successful descendants when a P148 corpus exists.

## Next high-value work

P148 solves reset-only exploration. The next major utility work should build on this corpus:

- novelty-aware state mutation beyond the current eight controller policies;
- screen/VRAM/game-progress fingerprints to avoid spending CPU on semantically duplicate states;
- automatic detection of stuck/dead states and state-specific policy selection;
- import/differential validation against an independent Genesis emulator;
- host Z80 execution profiling and target audio redesign;
- generated-WRAM-code capture/lowering;
- automatic ARM7/IWRAM placement based on measured generated-code cost rather than only 68K hotness;
- semantic lowering for repeatedly measured VDP/DMA/decompression/audio kernels;
- eventually package Python/LLVM into a conventional Windows installer.
