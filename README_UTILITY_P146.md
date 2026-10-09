# MD2GBA P146 UTILITY — offline ROM -> GBA compiler foundation

Date: 2026-09-29
Base: P142 PSCDEEPFAST runtime/compiler, deliberately forked away from PSIV-specific optimization work.

## New project contract

MD2GBA is now being developed as a desktop utility rather than a PSIV port branch:

    Genesis/Mega Drive ROM
        -> conservative static analysis
        -> persistent desktop execution profiling
        -> measured optimization/AOT plan
        -> generated GBA ROM

Game-specific facts belong in the profile database/generated files. The reusable runtime should not gain hard-coded Sonic/PSIV addresses.

## What P146 implements

- `md2gba.cmd` Windows-oriented launcher and `md2gba.sh` Unix launcher.
- `tools/md2gba_utility.py` top-level CLI:
  - `doctor`
  - `analyze ROM`
  - `profile ROM`
  - `status ROM`
  - `plan ROM`
  - `build ROM -o game.gba`
  - `pipeline ROM -o game.gba`
- Persistent SQLite cache keyed by source-ROM SHA-256.
- Built-in native desktop profiler compiled from MD2GBA's own 68000 interpreter/bus/timing core.
- Exact runtime PC counts and control-flow edges.
- Per-PC VDP data/control, DMA, YM2612, PSG, and Z80-RAM event attribution.
- Multiple deterministic input campaigns whose results merge into the same profile database.
- Static and observed ROM PCs are merged for AOT only after actual execution proves dynamic targets.
- Non-ROM execution is tracked separately as dynamic/RAM code. It is never blindly fed to the ROM code generator.
- Automatic hotness plan: 90% execution set plus top hardware-facing PCs.
- P142 generic compiler build-plumbing repairs retained: projected renderer object, minimal AEABI helpers, generic linker layout with bus body in ROM.
- Generic target audio is temporarily disabled in generated test ROMs (`--silent-audio`) because the earlier Sonic experiments proved its startup path can corrupt generic boot. Audio bus activity is still profiled on the desktop for the future audio compiler.

## Windows use

Requirements for this source checkpoint:

1. Python 3.
2. LLVM/Clang with `clang`, `ld.lld`, and `llvm-objcopy` on PATH.

From Command Prompt:

    md2gba.cmd doctor
    md2gba.cmd analyze game.bin
    md2gba.cmd profile game.bin --campaigns 20 --frames 3600
    md2gba.cmd status game.bin
    md2gba.cmd build game.bin -o game.gba

Or one command:

    md2gba.cmd pipeline game.bin -o game.gba --campaigns 20 --frames 3600

The default profile cache is created next to the ROM as `game.bin.md2gba.sqlite`. It is reusable. Additional profiling runs add evidence instead of starting over.

## Why this is safer than P143/P144/P145

P143 guessed pointer-looking ROM data and promoted it to executable code; Sonic 1 red-screened.
P144/P145 tried exhaustive overlapping translation; they fit under 32 MiB but red/white-screened.

P146 only adds a dynamic ROM target to AOT after the desktop machine has actually executed that PC. This recovered thousands of real indirect/state-machine targets without promoting arbitrary data.

## Current limitations / next engineering work

- The built-in profiler uses MD2GBA's current 68000/bus/timing model, not yet an independent cycle-perfect Genesis oracle.
- Host Z80 execution is not yet part of the campaign profiler; Z80 RAM/YM/PSG traffic is recorded for future lowering.
- Synthetic input campaigns are basic. Coverage-guided save-state branching, scripted exploration, and external emulator trace import should be added.
- Dynamic code executed from WRAM is recorded but not yet statically lowered. S3&K already demonstrated this case at `$FFFFF0/$FFFFF6`.
- Generic GBA audio output remains disabled for bring-up while the audio compiler/player architecture is redesigned.
- The optimizer records hot and hardware-facing PCs but does not yet automatically move measured hot blocks into IWRAM or synthesize semantic kernels from the profile.
- Generated ROMs in this checkpoint are build-validation candidates and still require hardware/emulator runtime testing.

## P146 validation

All three supplied Sonic-family ROMs completed desktop profile campaigns without the interpreter stopping on an unsupported instruction.

### Sonic 1
- static PCs: 2,342
- observed ROM PCs: 8,071
- merged AOT PCs: 8,641
- observed dynamic-only ROM PCs: 6,299
- instructions sampled: 311,309,442
- PCs accounting for 90% of execution: 306
- GBA output: 4,666,672 bytes
- AOT regions: 73
- IWRAM: 25,836 / 28,672
- EWRAM/BSS: 255,848 / 262,144

### Sonic 2 input image
- static PCs: 2,042
- observed ROM PCs: 8,809
- merged AOT PCs: 9,861
- dynamic-only ROM PCs: 7,819
- GBA output: 7,082,796 bytes
- AOT regions: 102

### Sonic 3 & Knuckles input image
- static PCs: 517
- observed ROM PCs: 5,548
- merged AOT PCs: 5,675
- observed dynamic RAM PCs: 2 (`$FFFFF0`, `$FFFFF6`)
- GBA output: 10,128,024 bytes
- AOT regions: 78

All three linked with zero reported build warnings and stayed inside GBA ROM/IWRAM/EWRAM limits.
