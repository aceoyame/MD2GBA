# 68000 -> ARM7TDMI AOT translation contract

## Architectural state

The generated GBA program preserves the visible Motorola 68000 state: D0-D7, A0-A7, PC, SR/CCR, USP, exception/interrupt state and the 24-bit address space.  Native host registers are an optimization cache, not architectural truth; generated blocks must synchronize state at any boundary where the scheduler, bus, exception logic or fallback can observe it.

## Address-space lowering

The compiler classifies every memory operation before deciding how much work may be removed:

- Cartridge ROM: direct immutable loads where SRAM overlays cannot apply.
- Work RAM `$FF0000-$FFFFFF`: direct EWRAM accesses, optionally using MD2GBA's reversed-byte layout so aligned big-endian guest words become native little-endian ARM loads.
- VDP ports: generated hardware-semantic operations or calls into generated VDP publication kernels.
- Z80/YM/PSG I/O: generated synchronization/audio events.
- SRAM/controller/rare hardware: canonical bus helpers unless a safe specialization is proven.

No optimization may silently turn a potentially hardware-visible access into ordinary memory.

## Control flow

Direct branches/calls are resolved statically.  Indirect JSR/JMP/state-machine callbacks are learned from host execution and stored as exact observed edges.  The desktop may spend hours discovering these targets.  It must not guess them from arbitrary data words, which is the failure mode that made the earlier pointer-scanning experiment red-screen.

Generated dispatch should use exact guest-PC lookup only for code that has been statically proven or actually executed.  Unknown ROM PCs enter the canonical decoder/fallback, are recorded by profiling, and become candidates for the next build.

## Instruction translation

Each decoded instruction is translated into C/IR that preserves:

- operand size and sign-extension rules;
- effective-address side effects, including predecrement/postincrement;
- CCR X/N/Z/V/C behavior;
- stack order and exception frame semantics;
- 24-bit bus masking;
- alignment faults where required;
- instruction-specific cycle accounting sufficient to preserve H/V interrupts and observable VDP timing.

Hot sequential blocks should remain in native continuation form across instructions instead of synchronizing PC/SR after every instruction.  State synchronization is paid only at real boundaries.

## Cycle and scheduler lowering

Desktop profiling records block frequency and hardware interaction.  The generator can then choose among:

- ordinary instruction-by-instruction cycle accounting;
- pre-summed fixed-cycle native blocks;
- scheduler-safe copy/fill/compare fusions;
- H-int-safe semantic spans that cross unobservable scanlines;
- explicit boundaries before VDP status reads, interrupts, Z80 bus-release transitions and other timing-visible events.

The compiler may eliminate host work; it may not speed the guest clock or drop an observable machine event.

## Dynamic code

Execution from WRAM is recorded separately from cartridge code.  The final design is guarded dynamic-code AOT:

1. profiler captures the executed WRAM byte span and a hash/version;
2. desktop translates that exact byte sequence;
3. GBA dispatch tests the bytes/hash before entering the native version;
4. changed/unseen variants fall back and can be added by a later profiling run.

This keeps self-modifying/decompressed code compatible without forcing all ROM execution through an interpreter.

## Host optimization policy

Because the desktop has abundant compute, the optimizer may compile a hot block several ways (Thumb vs ARM, alternate register residency, inline vs helper calls) and inspect resulting target size/instruction count.  IWRAM placement is a measured resource-allocation problem, not a title-specific hand decision.
