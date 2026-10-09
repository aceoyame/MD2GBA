# MD2GBA P147 AUTOPROFILE GUI

**Date:** 2026-09-29  
**Base:** P146 Utility / P142 generic runtime

P147 moves MD2GBA toward the intended end-user product: a Windows desktop application that accepts a Genesis/Mega Drive ROM, learns how that ROM actually executes on a fast desktop, creates a persistent measured profile, and generates a GBA build.

## Start it on Windows

Requirements remain Python 3 and LLVM/Clang (`clang`, `ld.lld`, `llvm-objcopy`) on PATH.

Double-click:

    md2gba.cmd

With no command-line arguments, `md2gba.cmd` opens the GUI. You can also drag a ROM onto `md2gba.cmd`, or launch `md2gba_gui.cmd`.

The CLI remains available, for example:

    md2gba.cmd doctor
    md2gba.cmd auto-profile game.bin --hours 6
    md2gba.cmd build game.bin -o game.gba

## GUI Smart Build

**SMART BUILD** performs, without requiring the user to construct a profile manually:

1. Conservative 68000 static analysis.
2. Compile/update the native desktop MD2GBA profiling runner.
3. Launch many independent profiling campaigns in parallel.
4. Merge exact observed PCs, edges and hardware-facing events into the ROM's SQLite profile cache.
5. Adapt the profiling strategy based on which campaigns are still discovering new code.
6. Increase campaign duration automatically when short runs plateau.
7. Stop after measured saturation or the configured time budget.
8. Generate the measured optimization/AOT plan.
9. Compile the GBA ROM with parallel native-page compilation.

Completed profiling evidence is persistent. Stopping a long profiling session does not discard previously imported campaigns.

## Uses the desktop aggressively

P147 defaults profiling workers and native AOT compile workers to `os.cpu_count()` -- all logical processors reported by the desktop. The GUI exposes the worker count so it can be reduced on a machine that needs to remain interactive.

Profiling campaigns are separate native processes, so they execute concurrently across CPU cores instead of being serialized through Python's GIL. GBA AOT page compilation is also parallelized at the selected worker count.

## Automatic profiling intelligence

P146 required the caller to decide how many campaigns to run. P147 adds an adaptive scheduler.

The profiler currently has eight generic input/exploration policies. No policy contains Sonic, PSIV or other title addresses.

The scheduler:

- explores every policy before heavily exploiting one;
- records each campaign's **new PC** and **new control-flow edge** yield;
- uses accumulated novelty as a reward to allocate later CPU time;
- preserves exploration lanes so a previously weak policy can still discover a later state;
- detects low-growth epochs;
- lengthens campaigns automatically after a plateau;
- continues until coverage remains saturated at the maximum campaign depth or the user time budget expires;
- stores campaign intelligence in the same ROM-keyed SQLite cache, so later sessions continue from prior learning.

The profile database still distinguishes cartridge code from dynamically executed WRAM code. Observed RAM execution is evidence for a future dynamic-code lowering path; it is never blindly promoted as ROM AOT.

## New files

- `tools/md2gba_gui.pyw` — native Tk Windows-oriented GUI; no extra Python GUI package required.
- `tools/md2gba_autoprofile.py` — adaptive, parallel campaign scheduler.
- `host/md2gba_profile_runner.c` — expanded from 3 to 8 generic exploration/input policies.
- `tools/md2gba_utility.py` — new `auto-profile` command and all-core defaults.
- `md2gba.cmd` — opens GUI when double-clicked; still serves CLI commands.
- `md2gba_gui.cmd` — explicit GUI launcher.

## P147 scheduler smoke test

A fresh Sonic 1 cache was intentionally profiled with tiny campaign lengths to test decisions quickly. The profiler:

- began with broad policy exploration;
- observed an initial 59 PCs / 65 edges;
- detected a zero-growth plateau and lengthened campaigns from 60 to 105 frames, then 120 frames;
- a later policy discovered **404 new PCs and 441 new edges**;
- retained the newly discovered coverage and continued its saturation logic.

This smoke test is about scheduler behavior, not sufficient game coverage. Normal GUI defaults begin at 3,600 frames and allow growth to 28,800 frames per campaign with a multi-hour profiling budget.

## Still to build

P147 gives the utility the ability to build its own measured profile, but the larger roadmap remains:

- save-state/forked-state coverage exploration so campaigns do not all begin at reset;
- independent/cycle-accurate Genesis oracle import and differential validation;
- host Z80 execution profiling;
- automatic capture/lowering of generated WRAM code;
- automatic IWRAM allocation from measured ARM7 benefit rather than raw 68K hotness alone;
- semantic recognition/lowering of decompression, DMA/copy, renderer and audio-driver kernels;
- replacement generic target audio architecture;
- eventually package Python/LLVM dependencies into a normal Windows installer/application.

P147's profile DB and GUI orchestration are structured so these can be added as new analysis/exploration stages without changing the user's ROM-in -> GBA-out workflow.
