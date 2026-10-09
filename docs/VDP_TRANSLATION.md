# Genesis VDP -> GBA offline translation contract

## Why P148 video is not acceptable

The P148 Sonic output demonstrated that a generic projected renderer can execute the game while still producing large tile/palette/plane corruption and very low frame rate.  The long-term utility therefore must not ask the ARM7 to rediscover the Genesis display every frame.  The desktop compiler should learn the game's VDP behavior, convert stable assets/layout rules ahead of time, and generate the smallest runtime publication layer that preserves dynamic effects.

## VDP state that must remain authoritative

The compiler models Genesis VRAM (64 KiB), CRAM, VSRAM, VDP registers, auto-increment, command/address state, DMA modes, plane/window bases, sprite attribute table, scroll tables, priorities, tile flips, palette selection and display modes.  Generated GBA state is a derived cache; Genesis-visible reads and timing remain consistent with the logical VDP state.

## Offline classification

P149 records VDP port events with their originating 68000 PC, event type, VDP target/code and destination address.  Across many campaigns this lets the host classify sites into:

- stable asset upload;
- sequential VRAM tile/name-table copy;
- VRAM fill/copy DMA;
- CRAM palette upload/animation;
- VSRAM/scroll update;
- sprite table construction;
- line/column/raster effects;
- status/timing-sensitive behavior.

The compiler should combine this with ROM/WRAM provenance: if a VRAM upload comes from immutable ROM data, convert its tile data on the PC and store the GBA-native result directly in the output ROM.  If it comes from runtime decompression, compile the decompressor and produce a specialized converter/publication kernel.

## GBA representation

The desktop chooses the cheapest exact-enough GBA representation per scene/effect:

- regular tile BGs for stable planes;
- generated tile/name-table caches for dynamically modified cells;
- OAM sprites for Genesis SAT entries, with clipping/priority rules generated from measured behavior;
- palette conversion tables for CRAM;
- BG horizontal/vertical offsets for whole-plane scrolling;
- HBlank DMA or generated scanline tables only when a game actually uses raster/line-scroll effects;
- pre-scaled or fitted assets only when presentation policy explicitly requires 320->240 adaptation.

A generic game must not inherit PSIV-specific viewport assumptions.

## 320x224 versus 240x160

This is a presentation problem, not a CPU-emulation problem.  The utility should make the policy explicit and generate the required assets/mapping offline.  Candidate policies include crop, aspect-preserving fitted tiles, game-aware viewport following, or selective raster reduction.  Collision/game coordinates stay in Genesis space unless the compiler has separately proven a game-specific transformation safe.

The P148 screenshot is evidence that blindly projecting source tiles is not sufficient.  P149 records the data needed for a later renderer compiler to validate tile provenance and plane geometry before emitting GBA mappings.

## Runtime responsibilities after successful VDP compilation

The GBA runtime should ideally perform only:

- publication of changed generated BG/OAM/palette data;
- required HBlank/VBlank scheduling;
- dynamic tile conversion for genuinely runtime-generated graphics;
- rare semantic fallback for an unprofiled VDP mode.

It should not walk/reinterpret an entire Genesis plane or software-render a complete frame unless the utility explicitly reports that the game cannot be represented by the native GBA backends.

## P149 observed-value vocabulary

P149 now records `.vdpvalues.tsv` in addition to site/address signatures.  Each unique observation carries the producing 68K PC, VDP access type, target, code, pending-control state, destination address and 16-bit value.  The profile DB merges these into `observed_vdp_values` with execution/run counts and `vdp_plan.json` exports the hot vocabulary.

This does not by itself prove source provenance, but it closes an important gap exposed by the P148 corrupted Sonic image: a compiler cannot faithfully pretranslate tile/palette/scroll data if it only knows that a site wrote VRAM and not what it actually wrote.  The next generator should connect these observed values back to ROM/WRAM/DMA source ranges and emit preconverted GBA assets or specialized transfer kernels.

## P150 profile-mapped text-plane lowering

P150 is the first implementation that consumes the accumulated VDP profile to replace per-frame BG tile ownership decisions on the target.  The profiler records complete Plane A/B source-pattern sets at VBlank.  The desktop clusters these sets into source->GBA-slot mappings capped at 1023 nonblank text-BG slots and emits them in the ROM-specific translation manifest.

The target selects only a generated mapping whose 2048-bit source mask covers the complete live plane state.  Full scene changes rebuild into an inactive screenblock bank; ordinary frames use VDP dirty-pattern and dirty-name-block bitsets to update predetermined slots/cells.  A source pattern not represented by the active map forces a complete rescan/map selection; absence of any covering generated map returns to the canonical renderer.

The accumulated Sonic 1 profile demonstrates why this is superior to identity mapping: observed source IDs reach 1934, but all 877 profiled tile-set states use only 758 unique source patterns in union.  A single 758-slot generated map therefore covers the measured workload without limiting Genesis source IDs to the GBA tile index range.

This phase does not yet preconvert all pattern pixels.  Dynamic Genesis VRAM remains semantic source truth and dirty pattern bytes are converted to GBA tile layout when necessary.  Future lowering should infer VDP write/DMA provenance and preconvert stable ROM-derived assets and transfer schedules on the host.
