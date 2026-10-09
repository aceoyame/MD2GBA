# MD2GBA P147 AUTOPROFILE GUI — handoff

Date: 2026-09-29
Base: P146 Utility / P142 generic runtime and compiler

## Direction

MD2GBA is now a ROM-in -> profile/analyze on desktop -> GBA-out utility. P147 deliberately invests in host-side intelligence and parallelism rather than game-specific runtime patches.

## Implemented in P147

### Native GUI
- `tools/md2gba_gui.pyw`: Tkinter GUI requiring no third-party Python GUI package.
- `md2gba.cmd`: double-click launches GUI; dropping an existing ROM path onto it launches GUI with that ROM selected; named CLI commands still route to `md2gba_utility.py`.
- `md2gba_gui.cmd`: explicit GUI launcher.
- GUI exposes ROM/output, worker count, profile time budget, initial/max campaign length, saturation depth and coverage-growth threshold.
- SMART BUILD drives analyze -> adaptive profile -> plan -> parallel GBA compile automatically.
- Live coverage/log panel parses adaptive-profiler events.
- Stop preserves campaigns already imported into the SQLite cache.

### Full desktop parallelism
- Profiling default worker count is every logical CPU reported by `os.cpu_count()`.
- Each campaign is a separate native `md2gba_profile_runner` process, so profiling is not serialized by Python's GIL.
- Generic AOT compilation default jobs changed from an 8-worker cap to all logical CPUs.
- GUI lets the user cap workers manually.

### Profile intelligence
- New `tools/md2gba_autoprofile.py` adaptive campaign scheduler.
- Eight generic input/exploration policies in the native host profiler; none use title-specific addresses.
- Scheduler stores per-policy novelty: newly observed PCs and control-flow edges.
- Broad exploration precedes exploitation.
- A UCB-like novelty score shifts later CPU time toward productive policies while retaining exploration lanes.
- Parallel campaigns are all scored against the same epoch baseline so completion/import order cannot steal novelty credit.
- Coverage plateaus automatically increase campaign duration up to the configured maximum.
- Profiling ends after sustained low growth at maximum depth or the requested time budget.
- Adaptive campaign stats persist in the ROM-keyed SQLite DB and can guide subsequent sessions.

### Cache migration
- `open_db()` now performs additive schema migration for older P146 observed-PC/edge caches. Existing expensive profiling no longer needs to be deleted just because hardware-event columns were added later.

## Validation

### Adaptive scheduler smoke test
Fresh Sonic 1 database, deliberately tiny runs for fast decision validation:
- workers: 2
- start: 60 frames
- observed 59 PCs / 65 edges initially
- plateau detected; grew to 105 frames, then 120
- later generic policy discovered +404 PCs / +441 edges
- merged coverage reached 463 PCs / 506 edges
- saturation logic then completed normally
- profiler runner returned no unsupported-instruction stop

See `validation/p147_scheduler_smoke.log` in complete checkpoint.

### Existing-cache migration + end-to-end target build
Used the prior rich Sonic 1 P146 cache after migration:
- merged graph PCs: 8,641
- native regions: 73
- output bytes: 4,666,672
- IWRAM: 25,836 / 28,672
- EWRAM/BSS: 255,848 / 262,144
- warnings: none

This validates the P147 GUI backend path through planning and target compilation. The test target remains silent-audio by design because generic target audio is still a separate unresolved architecture item.

## Important limitations / next work

P147 can make and refine profiles autonomously from reset, but the largest coverage improvement still available is host exploration from saved/forked states instead of every campaign starting at reset. Recommended next steps:
1. Add profiler snapshots containing 68K registers, WRAM, VDP, timing and relevant sound state.
2. Keep a coverage corpus of interesting states and fork new campaigns from states that discovered new PCs/edges.
3. Add mutation/selection over state corpus similar to coverage-guided fuzzing, but reward valid hardware/game progress rather than crashes.
4. Add independent Genesis-emulator trace import/differential validation.
5. Add host Z80 execution and audio-driver profiling.
6. Convert measured hotness + hardware-event attribution into automatic semantic kernels/IWRAM placement after ARM7 cost validation.
7. Replace the generic target audio transport.
8. Package Python/LLVM into a conventional Windows installer when the compiler architecture stabilizes.

Do not reintroduce P143 pointer-looking-data promotion or P144/P145 exhaustive overlapping FULLAOT. Runtime-observed cartridge execution remains the safe authority for dynamic targets.
