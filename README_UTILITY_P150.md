# MD2GBA P150 PROFILEMAP GUI

P150 consumes the hardware evidence gathered by P149 and emits the first profile-generated GBA renderer path.  The design target is not a game-specific port; it is a generic offline compiler that can spend hours on the host discovering safe representations and then bake those representations into the GBA ROM.

## Desktop-generated VDP tile mappings

A Genesis text-plane name entry can refer to any of 2048 pattern IDs, while a GBA text BG can address only 1024 slots.  P148/P149 therefore could not safely use source tile ID as GBA tile ID for arbitrary scenes.

P150 profiles the complete source-pattern set referenced by Plane A and Plane B at VBlank.  The SQLite database stores those tile-set signatures together with plane geometry/base/register state.  During precompile, the host greedily merges compatible observed sets into one or more mapping profiles, each containing at most 1023 source patterns.  Slot zero remains reserved as the blank tile used by the high/low-priority split.

The compiler emits, per mapping profile:

- a 2048-bit source coverage mask;
- a 2048-entry sparse source -> GBA-slot table;
- the contiguous GBA slot count;
- coverage metadata bound to the exact ROM/profile manifest.

The target no longer searches/allocates BG cache ownership each ordinary frame.  It selects a prebuilt mapping when a scene/map family changes, stages its patterns into their predetermined GBA slots, and then handles normal changes with the VDP dirty bitsets.

## Runtime guards

The generated renderer is used only for profiled-compatible text-plane states.  Current guards reject:

- unsupported plane geometry outside 32/64 x 32;
- column V-scroll;
- shadow/highlight mode;
- active VDP window mode;
- a live source-pattern set not covered by any generated mapping.

A guard failure leaves the VDP dirtiness available and returns to the canonical renderer.  The optimized path therefore does not guess that an unseen scene resembles a profiled one.

## Autonomous profiling improvement

P150 also treats three VDP novelty classes as profiling reward/saturation input:

1. VDP event/site signatures;
2. actual VDP value signatures;
3. complete active tile-set/plane-state signatures.

This matters because a new level can execute the same 68K write routines with familiar register values while referencing a new family of graphics.  Such a state must remain interesting to the autonomous profiler because it may require another offline tile mapping.

## What is still target-side

P150 is not the end state.  The GBA still owns:

- dynamic Genesis VRAM as semantic source truth;
- 4bpp Genesis->GBA nibble conversion for dirty dynamic patterns;
- guarded map patch publication;
- the existing sprite packer;
- scroll publication/HBlank DMA where needed;
- generic fallback for unseen/unsupported video modes.

The next VDP compiler stages should infer write/DMA source provenance and preconvert stable ROM-derived assets/transfers, so routine pattern conversion and broad VDP mutation handling migrate to the desktop too.

## Validation candidate

`SONIC1_P150_PROFILEMAP_VALIDATION.gba` is built from the accumulated Sonic 1 profile with silent audio.  Build-side validation:

- output: 4,653,256 bytes;
- generated AOT graph: 8,474 68K PCs / 71 AOT regions;
- generated VDP mapping profiles: 1;
- mapping slots: 758;
- observed tile-set signatures covered: 877 / 877;
- observed maximum Genesis BG source ID: 1934;
- IWRAM: 25,996 / 28,672 bytes;
- EWRAM `.bss`: 256,132 / 262,144 bytes;
- undefined ELF symbols: none.

Target behavioral validation still requires a GBA emulator/hardware test; no mGBA executable is available in the build container.
