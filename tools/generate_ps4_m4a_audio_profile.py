#!/usr/bin/env python3
"""Generate the PSIV native-audio AOT page overlay.

This is deliberately profile-specific. The generic MD2GBA runtime never knows
PSIV's sound-driver PC. For the PSIV profile, the original 68K sound-service
periodic service at 0x0D0000 is replaced with guest-accurate RTS semantics.
The one-time original initializer at 0x0D0004 is deliberately preserved because
it establishes guest-visible sound work-area/bootstrap state before the native
PSC/M4A-style player takes over recurring playback and Sound_Index consumption.

P96 note: generated AOT now carries scheduled-cycle overshoot explicitly. This
overlay detects that ABI and preserves the same accounting at the profile seam.
"""
from __future__ import annotations
import argparse
import re
from pathlib import Path


def make_hot(use_over: bool) -> str:
    if use_over:
        debit = "  { uint32_t cyc=16u; if(left<=cyc){{over+=cyc-left;left=0u;}}else left-=cyc; }"
    else:
        debit = "  { uint32_t cyc=16u; left=(left<=cyc)?0u:(left-cyc); }"
    return """L_0D0000:
  /* PSIV profile seam: recurring UpdateSound is replaced by the native
     PSC/M4A player. Preserve the guest JSR/RTS ABI, but leave 0x0D0004's
     one-time original driver initializer intact for its guest-visible WRAM
     and bus setup side effects. */
  if(left==0u && !st->pre)FAST_RETURN(MD2GBA_AOT_PAGE_EXIT);
  if(st->pre)st->pre=0u;
  { uint32_t ret=md2gba_read32(b,c->a[7]); c->a[7]=(c->a[7]+4u)&0x00FFFFFFu; c->pc=ret&0x00FFFFFFu; }
""" + debit + """
  steps++;
  FAST_RETURN(MD2GBA_AOT_PAGE_EXIT);

"""


def make_exact(use_over: bool) -> str:
    if use_over:
        debit = "if(st->left<=cyc){{st->over+=cyc-st->left;st->left=0u;}}else st->left-=cyc;"
    else:
        debit = "st->left=(st->left<=cyc)?0u:(st->left-cyc);"
    return (
        "    case 0x0u: { uint32_t pre_pc=0x0D0000u; "
        "uint32_t ret=md2gba_read32(b,c->a[7]); "
        "c->a[7]=(c->a[7]+4u)&0x00FFFFFFu; c->pc=ret&0x00FFFFFFu; "
        "uint32_t cyc=16u; (void)pre_pc; " + debit +
        " st->steps++; return MD2GBA_AOT_PAGE_EXIT; }"
    )


def main() -> int:
    ap=argparse.ArgumentParser()
    ap.add_argument('source')
    ap.add_argument('output')
    args=ap.parse_args()
    src=Path(args.source).read_text()
    use_over=('st->over' in src or 'over+=cyc-left' in src)

    h0=src.find('L_0D0000:')
    h1=src.find('L_0D0004:', h0 + 1)
    if h0 < 0 or h1 < 0:
        raise SystemExit('could not locate hot 0x0D0000/0x0D0004 labels')
    old_hot=src[h0:h1]
    if 'c->pc=0x0D0004u' not in old_hot or 'c->pc=0x0D0008u' not in old_hot:
        raise SystemExit('unexpected hot periodic-service body')
    out=src[:h0] + make_hot(use_over) + src[h1:]

    pat=re.compile(r'^\s*case 0x0u: \{ uint32_t pre_pc=0x0D0000u;.*?return MD2GBA_AOT_PAGE_EXIT; \}$', re.M)
    matches=list(pat.finditer(out))
    if len(matches) != 1:
        raise SystemExit(f'exact periodic-service signature count={len(matches)}, expected 1')
    out=out[:matches[0].start()] + make_exact(use_over) + out[matches[0].end():]

    service_span=out[out.index('L_0D0000:'):out.index('L_0D0004:')]
    init_span=out[out.index('L_0D0004:'):out.index('L_0D0008:')]
    if 'ret=md2gba_read32' not in service_span:
        raise SystemExit('native periodic-service RTS seam missing')
    if 'c->pc=0x0D0C1Au' not in init_span:
        raise SystemExit('original 0x0D0004 initializer was not preserved')
    if use_over and 'over+=cyc-left' not in service_span:
        raise SystemExit('P96 overshoot accounting missing from hot profile seam')
    dst=Path(args.output)
    dst.parent.mkdir(parents=True,exist_ok=True)
    dst.write_text(out)
    print(f'generated {dst}: 0x0D0000 periodic service -> native RTS; 0x0D0004 initializer preserved; overshoot={int(use_over)}')
    return 0

if __name__=='__main__':
    raise SystemExit(main())
