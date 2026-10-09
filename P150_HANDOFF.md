# MD2GBA P150 PROFILEMAP GUI — handoff

**Date:** 2026-10-05  
**Base:** P149 AOTGRINDER GUI  
**Primary goal:** move routine VDP presentation decisions off the ARM7 and into the desktop compiler.

## User-reported baseline problem

P148 Sonic 1 executed but was approximately 19 FPS and showed severe graphical corruption.  Treat that as evidence that a broad runtime Genesis renderer is the wrong final architecture.  The user explicitly wants the Windows utility to spend as much host compute time as necessary and perform 68K/VDP/Z80 work ahead of time.

## P150 completed work

### 1. Full-plane tile-set profiling

`host/md2gba_profile_runner.c` records a VBlank signature containing the complete active source-pattern set referenced by Plane A + Plane B, plus width/height, plane bases and relevant VDP register state.  Each set uses a 2048-bit source bitmap and is persisted as `.vdptilesets.tsv` / `observed_vdp_tile_sets`.

### 2. Offline mapping synthesis

`tools/md2gba_precompile.py` clusters observed tile sets into fixed mapping profiles.  A profile is accepted only when its source union fits the GBA text-BG budget (<=1023 nonblank slots).  It emits mapping coverage, sources, frame/run weights and rejection reasons in `vdp_renderer_plan.json`.

Sonic 1 accumulated-profile result:

- 877 distinct tile-set states;
- max Genesis source ID 1934;
- union of all observed states only 758 unique source patterns;
- one 758-slot map covers all 877 observed states (coverage 1.0).

This invalidated the initial identity-map idea but produced a much stronger generic solution: source ID does not have to equal GBA tile slot.

### 3. Generated target tables

`tools/md2gba_compile.py` consumes the translation manifest and generates `machine_generated/md2gba_vdp_plan.h` containing source masks, slot counts and sparse 2048-entry source->slot maps.  There are no Sonic title/address tests.

### 4. Profile-mapped GBA renderer

`md2gba_gba_backend.c` adds the guarded compiled VDP path:

- select a generated map from the complete live Plane A/B source set;
- convert/stage each mapped source pattern into its predetermined GBA slot on full rebuild;
- write split high/low priority name maps using generated source->slot lookup;
- on ordinary frames, convert only dirty mapped patterns and patch only dirty name blocks;
- retain palette, hardware scroll/HBlank stream and sprite publication;
- if a dirty map introduces a source outside the current profile, rescan and select another generated mapping;
- if no generated map covers it, return to canonical fallback rather than corrupting output.

### 5. VDP-semantic novelty in autonomous profiling

`tools/md2gba_autoprofile.py` now rewards/retains novelty from VDP event sites, VDP values **and active tile-set states**.  This prevents profiling from declaring saturation merely because new scenes reuse old VDP routines.

## Current validation build

`SONIC1_P150_PROFILEMAP_VALIDATION.gba`

- 4,653,256 bytes;
- 8,474 merged AOT PCs, 71 generated AOT regions;
- VDP map profiles: 1 / 758 slots;
- profiled tile-set coverage: 877/877;
- IWRAM `.iwram`: 0x658c = 25,996 / 28,672;
- EWRAM `.bss`: 0x3e884 = 256,132 / 262,144;
- undefined symbols: zero;
- audio deliberately silent.

## Important limitations / do not overclaim

- P150 is still a 1:1 cropped presenter.  It is not yet the final fitted 320x224->240x160 presentation strategy.
- Pattern pixel conversion for dynamic VRAM tiles still occurs on the GBA.  P150 eliminates dynamic BG slot allocation/search and makes name-table ownership deterministic; it does not yet preconvert every dynamic pattern.
- Sprite translation remains the existing SAT->OAM publisher and needs its own profile-generated lowering later.
- Unsupported/unprofiled VDP modes fall back to the generic renderer, so runtime cost can still spike outside profiled coverage.
- Audio is silent in the validation ROM while the new Z80/YM lowering is developed.
- No target emulator is available in this build container; user-side mGBA/hardware testing is still authoritative for visible correctness/speed.

## Next priorities

1. Test P150 on Sonic 1 and compare against the P148 screenshot/19-FPS baseline.
2. If coherent/faster, add VDP write provenance: infer ROM/WRAM/DMA sources for hot pattern transfers and preconvert stable ROM assets on the desktop.
3. Generate fixed/guarded DMA/copy/fill transfer kernels from profiled VDP sites rather than replaying broad generic bus handling.
4. Profile-generate sprite tile ownership/OAM publication.
5. Design a fitted hardware presentation strategy separately from semantic VDP translation; do not hide correctness problems by scaling/cropping changes.
6. Continue Z80/YM/PSG offline lowering after the video path is stable.
