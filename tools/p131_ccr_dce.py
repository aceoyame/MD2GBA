#!/usr/bin/env python3
"""P131: dead 68K condition-code elimination inside AOT superblocks.

Every translated 68K instruction recomputes N/Z/V/C (and sometimes X) even
though only branches, Scc/DBcc, ADDX/SUBX/ROX and SR reads consume them.  In
a superblock (the straight-line fast path guarded by `if(left>N)`), a flag
computation is dead when a later statement in the same superblock rewrites
the same flags before anything can observe them.

This pass works on the already-generated C (like p130_fix_generated_lea.py)
and removes only the SR sub-statements of provably dead writers:

  c->sr=(uint16_t)(c->sr&~(MD68K_N|MD68K_Z|MD68K_V|MD68K_C[|MD68K_X]));
  if(<cond>)c->sr|=<flags>;   /   c->sr|=(uint16_t)(<flags>);

Soundness rules (conservative):
  * A statement is a *writer* only if it has exactly one canonical SR clear
    and every other SR mention is a canonical conditional/unconditional OR.
  * Anything else that mentions c->sr, any statement containing control flow
    (goto/return/FAST_RETURN/inner braces), and any call receiving the CPU
    context `c` other than the whitelisted memory/EA helpers (which never
    touch SR) is a *barrier* that may read flags.
  * Writer W is dead iff the next writer W' after it in the same superblock
    is reached with no barrier in between, and W' clears every flag W sets
    (X only counts if W' also clears X).
  * The superblock exit always keeps the final flags live.
Idempotent.  Usage: p131_ccr_dce.py FILE.c [FILE.c ...]
"""
import re, sys
from pathlib import Path

CLEAR = re.compile(r'^c->sr=\(uint16_t\)\(c->sr&~\(MD68K_N\|MD68K_Z\|MD68K_V\|MD68K_C(\|MD68K_X)?\)\)$')
SETC = re.compile(r'^if\((.*)\)c->sr\|=(?:\(uint16_t\)\()?MD68K_[NZVCX](?:\|MD68K_[NZVCX])*\)?$')
SETU = re.compile(r'^c->sr\|=(?:\(uint16_t\)\()?MD68K_[NZVCX](?:\|MD68K_[NZVCX])*\)?$')
SAFE_CALLS = {'md68k_rd', 'md68k_wr', 'md68k_movem_load', 'md68k_movem_store', 'md68k_lea_addr', 'md68k_ea_addr',
              'md68k_rd_gen', 'md68k_wr_gen', 'md68k_ea_addr_gen', 'md68k_lea_addr_gen'}
SB = re.compile(r'(  if\(left>(\d+)u\)\{\n    steps\+=\d+u;\n)((?:    \{ .* \}\n)+)(    left-=(.*);\n    c->pc=0x([0-9A-F]+)u;\n    goto L_([0-9A-F]{6});\n)?')
FIRST = re.compile(r'^L_([0-9A-F]{6}):\n  if\(left>\d+u\)\{\n    steps\+=\d+u;\n(    \{ .* \})\n', re.M)
TERN = re.compile(r'\(\(c->pc==0x[0-9A-F]+u\)\?(\d+)u:(\d+)u\)')


def max_sub(expr):
    e = TERN.sub(lambda m: 'max(%s,%s)' % (m.group(1), m.group(2)), expr)
    e = re.sub(r'(\d+)u', r'\1', e)
    if not re.fullmatch(r'[0-9+() ,max]*', e): return None
    try: return eval(e, {'__builtins__': {}}, {'max': max})
    except Exception: return None


def split_top(body):
    """Split on ';' at parenthesis depth 0. Returns list of sub-statements (no ';')."""
    out = []; depth = 0; cur = []
    for ch in body:
        if ch == '(': depth += 1
        elif ch == ')': depth -= 1
        if ch == ';' and depth == 0:
            out.append(''.join(cur).strip()); cur = []
        else:
            cur.append(ch)
    tail = ''.join(cur).strip()
    if tail: out.append(tail)
    return out


def classify(line):
    """Return (kind, subs, clears_x) where kind in 'W' (writer), 'B' (barrier), 'N' (neutral)."""
    inner = line.strip()
    assert inner.startswith('{ ') and inner.endswith(' }')
    body = inner[2:-2]
    if '{' in body or '}' in body or 'goto' in body or 'return' in body or 'FAST_RETURN' in body:
        return 'B', None, False
    subs = split_top(body)
    clears = [k for k, s in enumerate(subs) if CLEAR.match(s)]
    sets = [k for k, s in enumerate(subs) if SETC.match(s) or SETU.match(s)]
    others = [s for k, s in enumerate(subs) if k not in clears and k not in sets]
    for s in others:
        if 'c->sr' in s: return 'B', None, False
        for m in re.finditer(r'\b([A-Za-z_]\w*)\s*\(\s*c\s*[,)]', s):
            if m.group(1) not in SAFE_CALLS: return 'B', None, False
    for k in sets:
        cond = SETC.match(subs[k])
        if cond and ('c->sr' in cond.group(1) or re.search(r'\b[A-Za-z_]\w*\s*\(', cond.group(1))):
            return 'B', None, False
    if len(clears) == 1 and all(k > clears[0] for k in sets):
        return 'W', subs, bool(CLEAR.match(subs[clears[0]]).group(1))
    if not clears and not sets:
        return 'N', subs, False
    return 'B', None, False


def strip_flags(subs):
    keep = [s for s in subs if not (CLEAR.match(s) or SETC.match(s) or SETU.match(s))]
    if not keep: return '    { }'
    return '    { ' + ' '.join(s + ';' for s in keep) + ' }'


def process(text, stats, cross=True):
    # First instruction of every in-page superblock label: does it rewrite all of N/Z/V/C?
    entry = {}
    for m in FIRST.finditer(text):
        k, _, cx = classify(m.group(2))
        entry[m.group(1)] = (k == 'W', cx)

    def fix_block(m):
        head, N, body, tail = m.group(1), int(m.group(2)), m.group(3), m.group(4) or ''
        lines = body.rstrip('\n').split('\n')
        cls = [classify(l) for l in lines]
        out = list(lines)
        for i, (k, subs, cx) in enumerate(cls):
            if k != 'W': continue
            stats['writers'] += 1
            for j in range(i + 1, len(cls)):
                k2, _, cx2 = cls[j]
                if k2 == 'B': break
                if k2 == 'W':
                    if cx and not cx2: break
                    out[i] = strip_flags(subs); stats['removed'] += 1
                    cls[i] = ('N', None, False)
                    break
        # P131b: cross-superblock.  Exit is `c->pc=X; goto L_X;`, the block can
        # subtract at most N cycles (so left>=1 at L_X and the next instruction
        # executes before any scheduler return), and X's first instruction
        # rewrites every flag the final writer here produces.
        if cross and tail and m.group(6) == m.group(7) and m.group(7) in entry:
            ms = max_sub(m.group(5))
            wfirst, xfirst = entry[m.group(7)]
            if ms is not None and ms <= N and wfirst:
                for i in range(len(cls) - 1, -1, -1):
                    k, subs, cx = cls[i]
                    if k == 'B': break
                    if k == 'W':
                        if cx and not xfirst: break
                        out[i] = strip_flags(subs); stats['removed_cross'] += 1
                        break
        return head + '\n'.join(out) + '\n' + tail
    return SB.sub(fix_block, text)


if __name__ == '__main__':
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    cross = '--no-cross' not in sys.argv
    stats = {'writers': 0, 'removed': 0, 'removed_cross': 0}
    for p in args:
        s = Path(p).read_text()
        t = s; first = True
        while True:          # each removal can expose another (X-flag chains)
            w0 = stats['writers']
            u = process(t, stats, cross)
            if not first: stats['writers'] = w0
            first = False
            if u == t: break
            t = u
        if t != s: Path(p).write_text(t)
    print('P131 CCR DCE: superblock flag writers %d, dead removed intra-block %d, cross-block %d' % (stats['writers'], stats['removed'], stats['removed_cross']))
