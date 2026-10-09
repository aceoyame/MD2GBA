#!/usr/bin/env python3
"""Profile-guided recovery of valid 68000 AOT PCs missed by static CFG discovery.

The static graph cannot always resolve computed JMP/JSR destinations.  A runtime
trace provides *entry PCs only*; this tool then decodes those verified entries
from the original ROM and follows ordinary/direct control flow until it rejoins
the existing graph.  No instruction semantics or game-specific addresses are
hard-coded here.

Accepted seed formats include raw hexadecimal PCs (one per line) and the
``FBTOP ... pc=XXXXXX ...`` lines emitted by the host fallback profiler.
"""
from __future__ import annotations
from collections import deque
from pathlib import Path
import argparse, json, re, sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from raw68k_core import decode
from emit_aot_region import collect_pcs

HEX_RE = re.compile(r"(?:\bpc=)?(?:0x)?([0-9a-fA-F]{6})\b")


def read_seeds(path: Path) -> list[int]:
    out: list[int] = []
    for line in path.read_text(errors="replace").splitlines():
        # Only accept fallback-profiler entries (FBTOP) or a raw one-PC line.
        # Generic status such as ``DONE ... pc=`` is deliberately ignored so
        # an unrelated current PC can never be promoted into the AOT graph.
        stripped = line.strip()
        m = None
        if stripped.startswith("FBTOP"):
            m = re.search(r"\bpc=(?:0x)?([0-9a-fA-F]{6})\b", stripped)
        if not m:
            m = re.fullmatch(r"(?:0x)?([0-9a-fA-F]{6})", stripped)
        if m:
            out.append(int(m.group(1), 16) & 0xFFFFFF)
    return sorted(set(out))


def successors(ins: dict) -> list[int]:
    pc = int(ins["address"])
    nxt = (pc + int(ins["size"])) & 0xFFFFFF
    flow = ins.get("flow", "fallthrough")
    target = ins.get("target")
    computed = bool(ins.get("computed_target"))
    mnem = ins.get("mnemonic", "")

    if flow == "fallthrough":
        return [nxt]
    if flow == "cond":
        out = [nxt]
        if target is not None:
            out.append(int(target) & 0xFFFFFF)
        return out
    if flow == "jump":
        return [] if computed or target is None else [int(target) & 0xFFFFFF]
    if flow == "call":
        # A call can return to the following instruction.  For a direct call,
        # recover the callee as well; a computed call's destination must be
        # independently observed before it is trusted as code.
        out = [nxt]
        if not computed and target is not None:
            out.append(int(target) & 0xFFFFFF)
        return out
    # return/stop/unknown flow: terminate this recovered path.
    return []


def augment(rom: bytes, base_pcs: set[int], seeds: list[int], max_new: int) -> tuple[set[int], dict]:
    recovered: set[int] = set()
    q = deque((s, s) for s in seeds)
    parent_seed: dict[int, int] = {}
    decode_errors: list[dict] = []

    while q:
        pc, seed = q.popleft()
        pc &= 0xFFFFFF
        if pc in base_pcs or pc in recovered:
            continue
        if pc >= len(rom) or pc + 1 >= len(rom):
            decode_errors.append({"pc": f"{pc:06X}", "seed": f"{seed:06X}", "error": "outside ROM"})
            continue
        try:
            ins = decode(rom, pc)
        except Exception as exc:
            decode_errors.append({"pc": f"{pc:06X}", "seed": f"{seed:06X}", "error": str(exc)})
            continue
        recovered.add(pc)
        parent_seed[pc] = seed
        if len(recovered) > max_new:
            raise RuntimeError(f"recovery exceeded --max-new={max_new}; refusing runaway decode")
        for succ in successors(ins):
            if succ not in base_pcs and succ not in recovered:
                q.append((succ, seed))

    merged = set(base_pcs) | recovered
    by_seed: dict[str, int] = {}
    for pc, seed in parent_seed.items():
        key = f"{seed:06X}"
        by_seed[key] = by_seed.get(key, 0) + 1
    report = {
        "base_pc_count": len(base_pcs),
        "seed_count": len(seeds),
        "seeds": [f"{x:06X}" for x in seeds],
        "recovered_pc_count": len(recovered),
        "merged_pc_count": len(merged),
        "recovered_by_seed": by_seed,
        "recovered_pcs": [f"{x:06X}" for x in sorted(recovered)],
        "decode_errors": decode_errors,
    }
    return merged, report


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("rom", type=Path)
    ap.add_argument("base_addresses", type=Path)
    ap.add_argument("trace", type=Path)
    ap.add_argument("-o", "--output", type=Path, required=True)
    ap.add_argument("--report", type=Path)
    ap.add_argument("--max-new", type=int, default=4096)
    args = ap.parse_args()

    rom = args.rom.read_bytes()
    base_obj = json.loads(args.base_addresses.read_text())
    base = set(collect_pcs(base_obj))
    seeds = read_seeds(args.trace)
    if not seeds:
        raise SystemExit("no runtime PC seeds found in trace")
    merged, report = augment(rom, base, seeds, args.max_new)
    args.output.write_text(json.dumps({"pcs": [f"{x:06X}" for x in sorted(merged)]}, indent=2) + "\n")
    if args.report:
        args.report.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
