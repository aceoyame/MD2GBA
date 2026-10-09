# Cleanup report — MD2GBA

**Input:** `MD2GBA_P150_PROFILEMAP_GUI_SOURCE_2026-10-05.zip` (preserved unmodified outside this repository)

- Files retained directly from source: **261** (3,498,832 bytes uncompressed)
- Files removed from Git import: **29** (8,895,978 bytes uncompressed)
- Added curated README, Git ignore/attributes, handoff and restoration notes.

## Cleanup rationale
- 15 file(s): ROM-derived VDP test capture
- 8 file(s): obsolete source backup/snapshot
- 4 file(s): compiled extensionless executable
- 1 file(s): ROM-derived profiling/capture dataset
- 1 file(s): log/temporary/backup file

This is a **source publication cleanup**, not a guarantee that a binary-identical build works without assets. Generated data and third-party ROM contents are omitted for licensing and repository hygiene. No test execution is implied by this report. Original inputs remain in the separately preserved checkpoint.

## Removed inputs (audit trail)
- `p119_nem_commands_clone.csv` (5,473,228 bytes): ROM-derived profiling/capture dataset
- `MD2GBA.exe` (1,783,296 bytes): compiled extensionless executable
- `host/md2gba_profile_runner` (166,456 bytes): compiled extensionless executable
- `tests/test_p134_projected_runtime.c.p134f` (6,693 bytes): obsolete source backup/snapshot
- `tests/test_p134_projected_runtime.c.p134k` (19,827 bytes): obsolete source backup/snapshot
- `tests/test_p134_projected_runtime.c.p134l` (23,570 bytes): obsolete source backup/snapshot
- `tests/fixtures/source-0120.vdp` (66,672 bytes): ROM-derived VDP test capture
- `tests/fixtures/source-0360.vdp` (66,672 bytes): ROM-derived VDP test capture
- `tests/fixtures/source-1200.vdp` (66,672 bytes): ROM-derived VDP test capture
- `tests/fixtures/source-1440.vdp` (66,672 bytes): ROM-derived VDP test capture
- `tests/fixtures/source-0840.vdp` (66,672 bytes): ROM-derived VDP test capture
- `tests/fixtures/source-0600.vdp` (66,672 bytes): ROM-derived VDP test capture
- `tests/fixtures/source-0240.vdp` (66,672 bytes): ROM-derived VDP test capture
- `tests/fixtures/source-0720.vdp` (66,672 bytes): ROM-derived VDP test capture
- `tests/fixtures/source-1320.vdp` (66,672 bytes): ROM-derived VDP test capture
- `tests/fixtures/source-0960.vdp` (66,672 bytes): ROM-derived VDP test capture
- `tests/fixtures/source-1680.vdp` (66,672 bytes): ROM-derived VDP test capture
- `tests/fixtures/source-1560.vdp` (66,672 bytes): ROM-derived VDP test capture
- `tests/fixtures/source-1800.vdp` (66,672 bytes): ROM-derived VDP test capture
- `tests/fixtures/source-0480.vdp` (66,672 bytes): ROM-derived VDP test capture
- `tests/fixtures/source-1080.vdp` (66,672 bytes): ROM-derived VDP test capture
- `tools/generate_predecoded_token_profile.py.p111_inline` (6,916 bytes): obsolete source backup/snapshot
- `tools/emit_aot_sharded.py.p111_before_p112` (74,908 bytes): obsolete source backup/snapshot
- `tools/emit_aot_sharded.py.p111pre` (73,262 bytes): obsolete source backup/snapshot
- `tools/generate_ps4_m4a_audio_profile.py.p94` (4,803 bytes): obsolete source backup/snapshot
- `tools/emit_aot_sharded.py.p110pre` (72,491 bytes): obsolete source backup/snapshot
- `analysis/test_p108_vdp_bulk` (30,168 bytes): compiled extensionless executable
- `analysis/test_phase87_strided_fusions` (160,280 bytes): compiled extensionless executable
- `runtime/src/md2gba_psg_pcm.c.tmp` (0 bytes): log/temporary/backup file
