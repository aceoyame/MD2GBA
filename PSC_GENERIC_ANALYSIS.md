# PSC generic behavior / Thumb architecture extraction

- SHA-256: `6747a17af7ccb082bab1f2a8ed7d88952ab93879d93173b12331b1e5e8ec1c4e`
- Header: `PHANTASYSTAR` / `AYCE`
- Native module records: PSIII 110, PSII 54
- Generated first-entry dispatcher shells recognized: 84/164 (51.2%)

## Shared helper similarity
- read8: 224/300 raw bytes identical (74.7%) over the compared prefix.
- write8: 102/108 raw bytes identical (94.4%) over the compared prefix.
- write16: 97/128 raw bytes identical (75.8%) over the compared prefix.

## Generated native-dispatch structure
- PS3: 62 recognized module dispatchers.
  - `main`: selector base 128, 55 dense continuation slots, 27 unique ROM targets.
  - `vdp`: selector base 128, 77 dense continuation slots, 55 unique ROM targets.
  - `pack0`: selector base 128, 39 dense continuation slots, 26 unique ROM targets.
  - `sound`: selector base 128, 39 dense continuation slots, 27 unique ROM targets.
  - `put`: selector base 128, 29 dense continuation slots, 11 unique ROM targets.
  - `gmctrl`: selector base 128, 223 dense continuation slots, 154 unique ROM targets.
  - `start`: selector base 128, 189 dense continuation slots, 127 unique ROM targets.
  - `logo`: selector base 128, 58 dense continuation slots, 15 unique ROM targets.
  - … 54 additional modules recorded in the JSON report.
- PS2: 22 recognized module dispatchers.
  - `act`: selector base 1, 32 dense continuation slots, 13 unique ROM targets.
  - `adva`: selector base 1, 27 dense continuation slots, 27 unique ROM targets.
  - `battle`: selector base 1, 52 dense continuation slots, 49 unique ROM targets.
  - `coli`: selector base 1, 20 dense continuation slots, 14 unique ROM targets.
  - `color`: selector base 1, 46 dense continuation slots, 28 unique ROM targets.
  - `colordt`: selector base 1, 67 dense continuation slots, 56 unique ROM targets.
  - `datae`: selector base 1, 32 dense continuation slots, 13 unique ROM targets.
  - `demo`: selector base 1, 67 dense continuation slots, 56 unique ROM targets.
  - … 14 additional modules recorded in the JSON report.

The recurring shell is a compiler/code-generator fingerprint: normalize a signed selector modulo 1024, convert it to a module-local dense index, range-check it, then perform a native Thumb table jump. The cases themselves contain normal native state manipulation and calls into shared compatibility helpers. MD2GBA should reproduce the *IR/control-flow strategy*, not copy the PSC instructions.

## Reusable behaviors
- **native_modules** — PSII/PSIII game logic is split into native Thumb modules rather than a conventional per-instruction 68000 interpreter. MD2GBA: translate discovered guest code into native regions and reserve generic dispatch for indirect/dynamic edges.
- **native_continuation_table** — most recognized module shells reduce a signed selector modulo 1024, subtract a module-local base, range-check it, then jump through a dense native continuation table. MD2GBA: compile each guest region to dense local continuation IDs; resolve direct edges natively and use a compact dispatcher only at region/dynamic boundaries.
- **explicit_continuation_state** — translated cases write continuation/callback pointers into GBA RAM state and resume through native module selectors. MD2GBA: make continuation state an explicit compiler IR concept instead of materializing guest PC after every instruction.
- **same_module_native_redispatch** — observed PSIII module epilogues mask the returned continuation token by its 1024-selector module prefix and, when it still belongs to the current module, call the local dense native selector dispatcher again rather than entering a 68000 interpreter. MD2GBA: when a hot superblock cannot continue as one block, resume through a generated exact native continuation in the same region; do not decode/interpret one guest instruction as the normal seam.
- **cross_module_native_return** — when a continuation token names another module, PSC writes cached native state registers back to shared GBA RAM and returns the token to the outer dispatcher. MD2GBA: treat region/module crossings as explicit compiler continuation exits with a compact state sync, while keeping direct intra-region edges native.
- **invalid_selector_native_default** — unpopulated selector slots observed in native modules route to a generated default epilogue that synchronizes state and returns a neutral token rather than interpreting arbitrary guest bytes. MD2GBA: generate explicit cold/default continuation cases for unresolved region-local selectors; reserve the generic 68k interpreter only for code the offline compiler truly did not translate.
- **shared_bus** — PSII and PSIII contain strongly similar Genesis-address bus helpers rather than title-specific GBA address access. MD2GBA: keep one generic 24-bit Genesis bus, but inline/direct ordinary ROM/WRAM paths.
- **address_nibble_dispatch** — shared byte helpers classify the 24-bit Genesis address space by high address region before handling I/O/RAM. MD2GBA: lower dynamic accesses through a shallow region classifier; statically known ROM/WRAM addresses bypass it entirely.
- **reverse_rom** — wide Genesis ROM values are stored/read through a descending byte layout so native little-endian loads yield big-endian guest values. MD2GBA: offline reversed-ROM representation for wide loads; source ROM remains untouched.
- **reverse_wram** — the shared compatibility core maps $FFxxxx work-RAM bytes in descending guest-address order, enabling native little-endian wide accesses without recurring swaps. MD2GBA: optional descending physical WRAM with guest-address accessor helpers; use it in both dynamic bus and constant-address AOT lowering.
- **vdp_word_path** — the duplicated shared helpers route VDP aperture traffic through the word-write path; byte-write helpers classify Cxxxxx separately and do not implement the VDP transaction path. MD2GBA: treat hardware accesses as optimization barriers unless an access-width-specific primitive is proven equivalent.
- **vdp_control_latch** — a pending two-word VDP command consumes its second word before fresh $8xxx register-write decoding. MD2GBA: generic VDP control state machine; regression-tested in P87.
- **vdp_data_reads** — VDP data-port reads select VRAM/VSRAM/CRAM by decoded command state and then apply register-15 autoincrement. MD2GBA: implement generic VDP data reads rather than status-only reads.
- **cram_shadow** — CRAM writes also update a GBA-side translated palette representation. MD2GBA: retain source CRAM plus a write-time GBA palette shadow to remove repeat conversion during presentation.
- **four_bg_scroll** — the GBA presentation layer mirrors each Genesis plane scroll across its high/low-priority GBA BG pair. MD2GBA: BG0/BG2 and BG1/BG3 must receive identical camera offsets.
- **scroll_modes** — PSC derives H-scroll state from VDP regs 11/13 and stages fixed/cell/raster values for native presentation. MD2GBA: decode legal Genesis H-scroll modes once and stage hardware-ready offset streams.
- **hblank_dma** — PSC stages scroll values in RAM and uses GBA HBlank DMA for rasterized BG offsets. MD2GBA: prepare HBlank streams away from the critical scanline path and let DMA feed BG offset registers.
- **pretranslated_video_shadow** — PSC maintains GBA-facing video state alongside Genesis-visible VDP state instead of re-deriving every presentation value each frame. MD2GBA: convert palettes/tile dependencies/scroll streams at write or dirty-span time and keep source VDP state authoritative.
- **fifo_dma** — PSC uses GBA sound FIFO DMA rather than high-frequency CPU FIFO shoveling. MD2GBA: retain DMA audio transport but repair producer/physical-clock synchronization before defaulting it.

No PSC machine-code bytes are emitted by this report. The offsets used by the analyzer are research anchors only and are not part of the MD2GBA runtime contract.
