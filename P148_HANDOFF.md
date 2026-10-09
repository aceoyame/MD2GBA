# MD2GBA P148 STATEFUZZ GUI — implementation handoff

Date: 2026-09-29
Base: P147 AUTOPROFILE GUI

## Direction

Continue the ROM-in -> desktop analysis/profile -> generated GBA utility. Do not return to hand-tuning PSIV or Sonic runtime addresses as the primary architecture.

## Implemented

### Host save/restore
- `host/md2gba_profile_runner.c` supports optional input state + snapshot output.
- `M2GSTATE` v1 stores CPU, bus and timing state with host pointers stripped.
- State loader validates runner structure sizes, ROM size and ROM FNV64 before loading.
- Resumed campaigns run `requested_frames` relative to the snapshot frame instead of interpreting it as an absolute VBlank target.
- Snapshot TSV records state path, relative frame, absolute frame and current PC.

### Coverage-guided corpus
- `tools/md2gba_autoprofile.py` adds persistent `state_corpus` SQLite schema.
- Productive runs promote their furthest clean state; high-novelty runs may also retain a midpoint state.
- Child states track parent ID and depth.
- Parent scores incorporate discovered PCs/edges and successful descendants.
- State scheduling balances score, depth, recency and use count.
- Reset lanes remain active through `reset_fraction` (default 0.25).
- Active corpus is capped (default 128) and low-value states are pruned.
- Failed/incompatible state launches are deactivated rather than crashing the entire profiling session.

### GUI / utility
- GUI exposes state exploration on/off, corpus cap and reset exploration percentage.
- Live coverage text reports active saved states and deepest corpus generation.
- `auto-profile` CLI has `--no-state-exploration`, `--reset-fraction`, `--max-corpus`, `--snapshot-slices`, `--promote-per-success`.
- `status` reports corpus summary when available.
- P147 all-core profiling/AOT defaults remain unchanged.

## Validation

1. Exact deterministic state resume: continuous Sonic 1 frame 120 snapshot matched reset->60->reload->60 snapshot byte-for-byte.
2. Fresh adaptive corpus test with 4 workers / 120-frame campaigns:
   - reset baseline: 463 PCs / 506 edges;
   - first state-fork epoch: +436 PCs / +499 edges;
   - merged: 899 PCs / 1,005 edges;
   - final short-session corpus: 32 active states, max depth 6;
   - 78 state-started campaigns; 35 produced new coverage.
3. Normal target compiler accepted the profile and generated `SONIC1_P148_STATEFUZZ_VALIDATION.gba`, 4,344,300 bytes, 7,466 AOT entries / 64 pages.

No SHA-256 validation was performed for this checkpoint.

## Important constraints

- P148 states are host-profiler cache artifacts, not stable cross-version emulator save states.
- The current state reward is code/edge novelty. It does not yet know semantic game progress or visual uniqueness.
- Controller mutation still uses the eight generic P147 policies; it does not yet synthesize fine-grained input sequences from feedback.
- Host profiling uses MD2GBA semantics. Independent-emulator differential validation is still needed for stronger correctness assurance.
- Generic GBA target audio remains disabled in generated experimental builds.
- Do not restore the rejected P143 pointer scanner or P144/P145 overlapping FULLAOT strategy.
