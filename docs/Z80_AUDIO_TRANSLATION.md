# Z80 / YM2612 / PSG offline translation contract

## Z80 execution

Genesis software commonly uploads a title-specific sound driver into the 8 KiB Z80 RAM window.  A ROM-only static pass cannot assume what code will exist there.  P149 therefore executes the Z80 during desktop profiling at the hardware-derived clock ratio:

- 68000 clock = master clock / 7
- Z80 clock = master clock / 15
- therefore each observed 68000 cycle contributes exactly `7/15` Z80 T-states to the profiler budget.

The profiler records Z80 PCs and preserves Z80 RAM images.  Native generation is byte-guarded: a translated Z80 continuation is used only when the RAM bytes at that PC still match the bytes from which it was compiled.  Uploaded/self-modified variants can therefore coexist safely and unseen variants retain interpreter fallback.

The mature compiler should retain multiple observed Z80 RAM-image variants and generate one guarded dispatch family per unique uploaded program image rather than relying on a single final snapshot.

## YM2612

The profiler records ordered 3-byte YM events (`port, register, value`).  This allows the desktop to reconstruct channel algorithms, operators, envelopes, frequencies and key-on states.

Stable instruments can be pre-rendered into GBA-friendly phase-normalized timbre tables or converted into another native mixer representation.  Runtime work should primarily be key/pitch/envelope scheduling, not a cycle-heavy six-channel FM synthesis implementation when an offline equivalent is possible.

Ordering and timing are authoritative.  The compiler cannot replace two register streams merely because they sound similar in one sample.

## PSG

PSG latch/data writes are cheap enough to reproduce directly, but the utility should still profile them so repeated tone/noise schedules and constant sequences can be precompiled.  The GBA mixer should receive semantic tone/noise state, not emulate the Genesis bus protocol unnecessarily every sample.

## Bus synchronization

68K bus request/reset of the Z80 is an observable cross-CPU boundary.  Generated 68000 blocks must synchronize the sound CPU when a block releases/reacquires the Z80 bus.  Desktop AOT may fuse work between boundaries but may not starve the Z80 by hiding a release inside an oversized host block.

## Current P149 status

P149 adds host-side Z80 execution/profiling, Z80 RAM capture, ordered YM traces, PSG/Z80-upload event attribution and automatic generation of the existing guarded Z80 native module.  Generic audio output remains optional because the earlier Sonic test showed that the old target audio startup path could destabilize boot.  The new evidence pipeline is intended to replace that guess-heavy path with generated audio data.
