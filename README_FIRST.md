# MD2GBA P150 PROFILEMAP GUI — START HERE

**Date:** 2026-10-05  
**Base:** P149 AOTGRINDER / P148 STATEFUZZ / P142 generic runtime

P150 is the first target-side step that actually consumes the desktop VDP profile to remove a large piece of per-frame Genesis rendering work from the GBA.

The product goal remains unchanged: **select a Mega Drive / Genesis ROM in the Windows GUI, let the desktop analyze/profile/compile for as long as useful, then emit a GBA ROM whose routine work is generated ahead of time rather than broadly emulated at runtime.**

## Windows GUI

Double-click:

    MD2GBA.exe

or drag a Genesis ROM onto it.  The launcher opens the GUI and forwards the ROM automatically.  Press **AUTONOMOUS AOT BUILD** for the normal workflow.

The small launcher included in the source/portable package expects Python 3.  `BUILD_WINDOWS_EXE.cmd` / `MD2GBA_WINDOWS.spec` can freeze the Python GUI/backend into a larger self-contained Python executable.  LLVM/Clang is still an external build dependency.

## What is new in P150

P149 learned VDP sites/values but still used the expensive generic target renderer.  P150 adds a profile-generated text-plane backend:

- the desktop records the complete active Genesis BG source-pattern set for observed plane states;
- those states are greedily clustered into fixed source-pattern -> GBA tile-slot mappings, each capped at 1023 nonblank GBA BG slots;
- generated source masks and lookup tables are compiled into the target ROM;
- ordinary frames update only dirty mapped patterns and dirty name-table blocks;
- plane priority is preserved using high/low split GBA BG maps;
- palette, whole-plane scroll, line scroll and the existing sprite publisher remain guarded hardware paths;
- if an unprofiled scene is not representable by a generated mapping, the optimized path declines it and the canonical renderer remains the correctness fallback.

This is deliberately **profile-driven, not Sonic-specific**.  No Sonic addresses or title checks select the backend.

## Sonic 1 validation profile

The current accumulated profile contains:

- 7,856 observed 68K PCs; 8,474 merged observed/static AOT PCs;
- 118 observed Z80 PCs;
- 97,392 VDP event signatures;
- 261,936 VDP value signatures;
- 877 distinct observed VDP tile-set/plane states;
- source tile IDs as high as 1934;
- one generated mapping containing only 758 GBA BG slots covers all 877 observed tile-set states.

That last result is important: high Genesis source tile numbers do **not** require equally high GBA tile numbers.  The desktop chooses a compact fixed mapping from the patterns that actually coexist.

The current P150 Sonic test ROM is silent intentionally.  It should be evaluated for **boot/correctness, graphical coherence and speed relative to P148/P149**, not audio quality.

## Current limitation

P150 still presents a guarded **1:1 cropped** Genesis viewport.  It does not yet solve the final 320x224 -> 240x160 fitted presentation.  It also still converts pattern pixels on the GBA when dynamic VRAM patterns change; P150 moves tile ownership/allocation/name mapping offline, while later passes should move VDP DMA/source provenance and static asset conversion offline too.

Read `README_UTILITY_P150.md`, `P150_HANDOFF.md`, and `docs/VDP_TRANSLATION.md` before continuing.
