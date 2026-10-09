#!/usr/bin/env python3
"""Generate a title-agnostic sharded 68000 AOT region.

Large Genesis programs are split into fixed virtual-PC pages (16 KiB by default).
Phase 11 added a page-resident burst dispatcher. Phase 12 added direct native
instruction chaining inside each generated page. Phase 13 adds an optional exact
scheduled-event countdown so periodic machine events avoid a C callback on every
instruction. Dynamic control flow, event redirects, native misses, stop-PC and
after-step/fallback semantics remain instruction-exact.

Use --dispatch-mode burst for the Phase 11 oracle or step for Phase 5-10.
"""
from __future__ import annotations
from pathlib import Path
import argparse, json, re, sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
from raw68k_core import decode, with_bytes
from aot68k_codegen import emit_sem, cond_expr
from emit_aot_region import collect_pcs
from aot68k_loop_fusion import (
    expand_bits_loop, emit_expand_bits_loop,
    byte_to_nibble_row_loop, emit_byte_to_nibble_row_loop,
    counted_store_loop, emit_counted_store_loop,
    counted_copy_loop, emit_counted_copy_loop,
    counted_compare_loop, emit_counted_compare_loop,
    nibble_accumulate_loop, emit_nibble_accumulate_loop,
    nemesis_table_token_setup, emit_nemesis_table_token_setup,
    nibble_converge_word_loop, emit_nibble_converge_word_loop,
    strided_move_loop, emit_strided_move_loop,
    strided_clear_loop, emit_strided_clear_loop,
    strided_pair_store_loop, emit_strided_pair_store_loop,
)


def ident(s: str) -> str:
    s = re.sub(r'[^A-Za-z0-9_]', '_', s)
    if not s or s[0].isdigit():
        s = 'r_' + s
    return s


def _write_header(out_dir: Path, sym: str) -> None:
    guard = (sym + '_H').upper()
    (out_dir / f'{sym}.h').write_text(
        f'''#ifndef {guard}\n#define {guard}\n#include <stdint.h>\n#include "md2gba_aot_region.h"\nint {sym}_run(Md2Gba68k*,Md2GbaBus*,const Md2GbaAotHooks*,uint32_t,uint32_t,uint32_t);\n#endif\n'''
    )


def _generate_step(out_dir: Path, sym: str, pages: dict[int, list[dict]], page_shift: int):
    """Legacy Phase 5-10 dispatcher, retained as a benchmark/regression oracle."""
    shards = []
    for page, insns in sorted(pages.items()):
        fn = f'{sym}_page_{page:04x}_step'
        shards.append((page, fn, len(insns)))
        cases = '\n'.join(
            f'    case 0x{i["address"]:06X}u: {{ {emit_sem(i)} }} return 1;'
            for i in insns
        )
        (out_dir / f'{sym}_page_{page:04x}.c').write_text(
            f'''/* AUTO-GENERATED title-agnostic AOT page (legacy one-step mode). */\n#include <stdint.h>\n#include "md2gba_aot_region.h"\nint {fn}(Md2Gba68k*c,Md2GbaBus*b){{\n uint32_t t0=0,t1=0; (void)b; (void)t0; (void)t1;\n switch(c->pc){{\n{cases}\n    default:return 0;\n }}\n}}\n'''
        )
    decls = '\n'.join(f'int {fn}(Md2Gba68k*,Md2GbaBus*);' for _, fn, _ in shards)
    dispatch = '\n'.join(f'        case 0x{page:X}u: handled={fn}(c,b); break;' for page, fn, _ in shards)
    (out_dir / f'{sym}_dispatch.c').write_text(
        f'''/* AUTO-GENERATED sharded AOT dispatcher (legacy one-step mode). */\n#include <stdint.h>\n#include "md2gba_aot_region.h"\n#include "{sym}.h"\n{decls}\nint {sym}_run(Md2Gba68k*c,Md2GbaBus*b,const Md2GbaAotHooks*hooks,uint32_t start,uint32_t stop,uint32_t max_steps){{\n uint32_t steps=0; if(start!=MD2GBA_AOT_RESUME_PC)c->pc=start&0xFFFFFFu;\n while(steps<max_steps){{\n   int handled=0,hr=0; uint32_t executed_pc;\n   if(steps && c->pc==stop)return MD2GBA_AOT_OK;\n   if(hooks && hooks->event_step){{hr=hooks->event_step(c,b,steps,hooks->event_user);if(hr>0)return MD2GBA_AOT_YIELD;\n       if(hr<0)return MD2GBA_AOT_EVENT_ERROR;}}\n   executed_pc=c->pc; steps++;\n   switch(c->pc>>{page_shift}){{\n{dispatch}\n     default:handled=0;break;\n   }}\n   if(handled){{if(hooks && hooks->after_step){{hr=hooks->after_step(c,b,executed_pc,steps,hooks->after_user);if(hr>0)return MD2GBA_AOT_YIELD;\n       if(hr<0)return MD2GBA_AOT_EVENT_ERROR;}}continue;}}\n   if(hooks && hooks->handle_pc){{hr=hooks->handle_pc(c,b,c->pc,hooks->user);if(hr>0)continue;if(hr<0)return MD2GBA_AOT_HOOK_ERROR;}}\n   if(hooks && hooks->fallback_pc){{hr=hooks->fallback_pc(c,b,c->pc,hooks->fallback_user);if(hr>0){{if(hooks->after_step){{int ar=hooks->after_step(c,b,executed_pc,steps,hooks->after_user);if(ar>0)return MD2GBA_AOT_YIELD;if(ar<0)return MD2GBA_AOT_EVENT_ERROR;}}continue;}}if(hr<0)return MD2GBA_AOT_FALLBACK_ERROR;}}\n   return MD2GBA_AOT_MISSING_PC;\n }}\n return MD2GBA_AOT_STEP_LIMIT;\n}}\n'''
    )
    return shards


def _generate_burst(out_dir: Path, sym: str, pages: dict[int, list[dict]], page_shift: int):
    """Phase 11 page-resident burst dispatcher with exact per-step hooks.

    ``pre_step_done`` preserves the legacy ordering when an event hook vectors
    execution across a generated-page boundary.  The destination instruction
    must execute in the *same* logical step without a second stop/event check.
    It also lets a hook redirect a previously-missing PC into generated code.
    """
    shards = []
    for page, insns in sorted(pages.items()):
        fn = f'{sym}_page_{page:04x}_burst'
        shards.append((page, fn, len(insns)))
        cases = '\n'.join(
            f'      case 0x{i["address"]:06X}u: {{ {emit_sem(i)} }} handled=1; break;'
            for i in insns
        )
        (out_dir / f'{sym}_page_{page:04x}.c').write_text(
            f'''/* AUTO-GENERATED title-agnostic AOT page (Phase 11 burst mode). */\n#include <stdint.h>\n#include "md2gba_aot_region.h"\nint {fn}(Md2Gba68k*c,Md2GbaBus*b,const Md2GbaAotHooks*hooks,uint32_t*steps,uint32_t stop,uint32_t max_steps,uint8_t*pre_step_done){{\n uint32_t t0=0,t1=0; (void)t0; (void)t1;\n while(*steps<max_steps && (c->pc>>{page_shift})==0x{page:X}u){{\n   int handled=0,hr=0; uint32_t executed_pc,pre_pc;\n   if(!*pre_step_done){{\n     if(*steps && c->pc==stop)return MD2GBA_AOT_OK;\n     if(hooks && hooks->event_step){{\n       pre_pc=c->pc; hr=hooks->event_step(c,b,*steps,hooks->event_user);\n       if(hr>0)return MD2GBA_AOT_YIELD;\n       if(hr<0)return MD2GBA_AOT_EVENT_ERROR;\n       /* If the hook vectors out of this page, the destination must not receive a second pre-step event. */\n       if(c->pc!=pre_pc && (c->pc>>{page_shift})!=0x{page:X}u){{*pre_step_done=1;return MD2GBA_AOT_PAGE_EXIT;}}\n     }}\n   }}\n   *pre_step_done=0;\n   executed_pc=c->pc; (*steps)++;\n   switch(c->pc){{\n{cases}\n      default:handled=0;break;\n   }}\n   if(handled){{\n     if(hooks && hooks->after_step){{hr=hooks->after_step(c,b,executed_pc,*steps,hooks->after_user);if(hr>0)return MD2GBA_AOT_YIELD;\n       if(hr<0)return MD2GBA_AOT_EVENT_ERROR;}}\n     continue;\n   }}\n   if(hooks && hooks->handle_pc){{hr=hooks->handle_pc(c,b,c->pc,hooks->user);if(hr>0)continue;if(hr<0)return MD2GBA_AOT_HOOK_ERROR;}}\n   if(hooks && hooks->fallback_pc){{hr=hooks->fallback_pc(c,b,c->pc,hooks->fallback_user);if(hr>0){{if(hooks->after_step){{int ar=hooks->after_step(c,b,executed_pc,*steps,hooks->after_user);if(ar>0)return MD2GBA_AOT_YIELD;if(ar<0)return MD2GBA_AOT_EVENT_ERROR;}}continue;}}if(hr<0)return MD2GBA_AOT_FALLBACK_ERROR;}}\n   return MD2GBA_AOT_MISSING_PC;\n }}\n if(*steps>=max_steps)return MD2GBA_AOT_STEP_LIMIT;\n return MD2GBA_AOT_PAGE_EXIT;\n}}\n'''
        )

    decls = '\n'.join(
        f'int {fn}(Md2Gba68k*,Md2GbaBus*,const Md2GbaAotHooks*,uint32_t*,uint32_t,uint32_t,uint8_t*);'
        for _, fn, _ in shards
    )
    dispatch = '\n'.join(
        f'      case 0x{page:X}u: rc={fn}(c,b,hooks,&steps,stop,max_steps,&pre_step_done); break;'
        for page, fn, _ in shards
    )
    generated_page_cases = ''.join(f'      case 0x{page:X}u: continue;\n' for page,_,_ in shards)
    (out_dir / f'{sym}_dispatch.c').write_text(
        f'''/* AUTO-GENERATED sharded AOT dispatcher (Phase 11 burst mode). */\n#include <stdint.h>\n#include "md2gba_aot_region.h"\n#include "{sym}.h"\n{decls}\nint {sym}_run(Md2Gba68k*c,Md2GbaBus*b,const Md2GbaAotHooks*hooks,uint32_t start,uint32_t stop,uint32_t max_steps){{\n uint32_t steps=0; uint8_t pre_step_done=0; if(start!=MD2GBA_AOT_RESUME_PC)c->pc=start&0xFFFFFFu;\n while(steps<max_steps){{\n   int rc=MD2GBA_AOT_PAGE_EXIT,hr=0; uint32_t executed_pc,pre_pc;\n   switch(c->pc>>{page_shift}){{\n{dispatch}\n      default:rc=MD2GBA_AOT_PAGE_EXIT;break;\n   }}\n   if(rc!=MD2GBA_AOT_PAGE_EXIT)return rc;\n   /* A generated page may simply have exited to another generated page. */\n   switch(c->pc>>{page_shift}){{\n{generated_page_cases}      default:break;\n   }}\n   /* No generated page owns this PC. Preserve the legacy one-instruction hook/fallback path. */\n   if(!pre_step_done){{\n     if(steps && c->pc==stop)return MD2GBA_AOT_OK;\n     if(hooks && hooks->event_step){{\n       pre_pc=c->pc; hr=hooks->event_step(c,b,steps,hooks->event_user);\n       if(hr>0)return MD2GBA_AOT_YIELD;\n       if(hr<0)return MD2GBA_AOT_EVENT_ERROR;\n       /* Reclassify an event-vector destination without invoking event_step a second time. */\n       if(c->pc!=pre_pc){{pre_step_done=1;continue;}}\n     }}\n   }}\n   pre_step_done=0; executed_pc=c->pc; steps++;\n   if(hooks && hooks->handle_pc){{hr=hooks->handle_pc(c,b,c->pc,hooks->user);if(hr>0)continue;if(hr<0)return MD2GBA_AOT_HOOK_ERROR;}}\n   if(hooks && hooks->fallback_pc){{hr=hooks->fallback_pc(c,b,c->pc,hooks->fallback_user);if(hr>0){{if(hooks->after_step){{int ar=hooks->after_step(c,b,executed_pc,steps,hooks->after_user);if(ar>0)return MD2GBA_AOT_YIELD;if(ar<0)return MD2GBA_AOT_EVENT_ERROR;}}continue;}}if(hr<0)return MD2GBA_AOT_FALLBACK_ERROR;}}\n   return MD2GBA_AOT_MISSING_PC;\n }}\n return MD2GBA_AOT_STEP_LIMIT;\n}}\n'''
    )
    return shards



def _generate_chain(out_dir: Path, sym: str, pages: dict[int, list[dict]], page_shift: int):
    """Phase 13 direct native chaining + scheduled-event fast path.

    Phase 12 chained statically-known native edges but still had to call an
    ``event_step`` callback at every instruction when a frontend needed periodic
    machine timing. Phase 13 keeps the exact Phase 12 ABI and adds an optional
    countdown scheduler to ``Md2GbaAotHooks``. A scheduled event is invoked only
    at the exact pre-instruction boundary where it is due; between deadlines the
    generated chain performs a cheap local countdown instead of a C callback.

    If ``scheduled_event`` + ``scheduled_countdown`` are populated, they replace
    ``event_step`` for this run. ``event_step`` remains the exact compatibility
    path for non-periodic schedulers. Event redirects, stop-PC semantics,
    after-step hooks, fallback, and page transitions remain instruction-exact.
    """
    shards = []
    for page, insns in sorted(pages.items()):
        fn = f'{sym}_page_{page:04x}_chain'
        shards.append((page, fn, len(insns)))
        owned = {i['address'] for i in insns}
        labels = {pc: f'L_{pc:06X}' for pc in owned}
        entry_cases = '\n'.join(
            f'      case 0x{i["address"]:06X}u: goto {labels[i["address"]]};'
            for i in insns
        )
        blocks = []
        for idx, i in enumerate(insns):
            pc = i['address']
            sem = emit_sem(i)
            nxt = (pc + i['size']) & 0xFFFFFF
            cyc = _fastchain_cycle_expr(i, nxt)
            flow = i.get('flow', 'fallthrough')
            m = i['mnemonic']
            succ = []
            # DIVU/DIVS may vector on divide-by-zero rather than take next_pc.
            if flow == 'fallthrough' and m not in ('divu', 'divs') and nxt in owned:
                succ = [(nxt, labels[nxt])]
            elif m == 'bra' and i.get('target') in owned:
                t = i['target']; succ = [(t, labels[t])]
            elif m in ('bcc', 'dbcc'):
                t = i.get('target')
                if t in owned: succ.append((t, labels[t]))
                if nxt in owned and nxt != t: succ.append((nxt, labels[nxt]))
            elif flow in ('jump', 'call') and i.get('target') in owned and m not in ('trap',):
                t = i['target']; succ = [(t, labels[t])]

            if len(succ) == 1:
                spc, slab = succ[0]
                post = f'''  if(hooks && hooks->after_step){{
    hr=hooks->after_step(c,b,executed_pc,*steps,hooks->after_user);
    if(hr>0)return MD2GBA_AOT_YIELD;
    if(hr<0)return MD2GBA_AOT_EVENT_ERROR;
    if(c->pc!=0x{spc:06X}u)goto redispatch;
  }}
  goto {slab};'''
            elif len(succ) == 2:
                post = f'''  if(hooks && hooks->after_step){{
    hr=hooks->after_step(c,b,executed_pc,*steps,hooks->after_user);
    if(hr>0)return MD2GBA_AOT_YIELD;
    if(hr<0)return MD2GBA_AOT_EVENT_ERROR;
  }}
  if(c->pc==0x{succ[0][0]:06X}u){{goto {succ[0][1]};}}
  if(c->pc==0x{succ[1][0]:06X}u){{goto {succ[1][1]};}}
  goto redispatch;'''
            else:
                post = '''  if(hooks && hooks->after_step){
    hr=hooks->after_step(c,b,executed_pc,*steps,hooks->after_user);
    if(hr>0)return MD2GBA_AOT_YIELD;
    if(hr<0)return MD2GBA_AOT_EVENT_ERROR;
  }
  goto redispatch;'''

            blocks.append(f'''{labels[pc]}:
  if(*steps>=max_steps)return MD2GBA_AOT_STEP_LIMIT;
  if(!*pre_step_done){{
    if(*steps && c->pc==stop)return MD2GBA_AOT_OK;
    if(scheduled_active){{
      if(*scheduled_left==0u){{
        /* Synchronize the cached countdown so the callback sees the exact deadline. */
        *hooks->scheduled_countdown=0u;
        pre_pc=c->pc; hr=hooks->scheduled_event(c,b,*steps,hooks->scheduled_user);
        *scheduled_left=*hooks->scheduled_countdown;
        if(hr>0)return MD2GBA_AOT_YIELD;
        if(hr<0)return MD2GBA_AOT_EVENT_ERROR;
        *scheduled_hold=1u;
        if(c->pc!=pre_pc){{*pre_step_done=1;goto redispatch;}}
      }}
    }} else if(hooks && hooks->event_step){{
      pre_pc=c->pc; hr=hooks->event_step(c,b,*steps,hooks->event_user);
      if(hr>0)return MD2GBA_AOT_YIELD;
      if(hr<0)return MD2GBA_AOT_EVENT_ERROR;
      if(c->pc!=pre_pc){{*pre_step_done=1;goto redispatch;}}
    }}
  }}
  *pre_step_done=0; executed_pc=0x{pc:06X}u; (*steps)++;
  if(scheduled_active){{if(*scheduled_hold)*scheduled_hold=0u;else if(*scheduled_left)(*scheduled_left)--;}}
  {{ {sem} }}
{post}
''')

        body = '\n'.join(blocks)
        (out_dir / f'{sym}_page_{page:04x}.c').write_text(
            f'''/* AUTO-GENERATED title-agnostic AOT page (Phase 13 scheduled direct-chain mode). */
#include <stdint.h>
#include "md2gba_aot_region.h"
int {fn}(Md2Gba68k*c,Md2GbaBus*b,const Md2GbaAotHooks*hooks,uint32_t*steps,uint32_t stop,uint32_t max_steps,uint8_t*pre_step_done,uint32_t*scheduled_left,uint8_t*scheduled_hold,uint8_t scheduled_active){{
 uint32_t t0=0,t1=0,executed_pc=0,pre_pc=0; int hr=0; (void)t0; (void)t1; (void)executed_pc; (void)pre_pc;
redispatch:
 if(*steps>=max_steps)return MD2GBA_AOT_STEP_LIMIT;
 if((c->pc>>{page_shift})!=0x{page:X}u)return MD2GBA_AOT_PAGE_EXIT;
 switch((c->pc & ((1u<<{page_shift})-1u))>>1){{
{entry_cases}
   default:break;
 }}
 /* A native miss inside this page keeps the exact one-instruction fallback ABI. */
 if(!*pre_step_done){{
   if(*steps && c->pc==stop)return MD2GBA_AOT_OK;
   if(scheduled_active){{
     if(*scheduled_left==0u){{
       *hooks->scheduled_countdown=0u;
       pre_pc=c->pc; hr=hooks->scheduled_event(c,b,*steps,hooks->scheduled_user);
       *scheduled_left=*hooks->scheduled_countdown;
       if(hr>0)return MD2GBA_AOT_YIELD;
       if(hr<0)return MD2GBA_AOT_EVENT_ERROR;
       *scheduled_hold=1u;
       if(c->pc!=pre_pc){{*pre_step_done=1;goto redispatch;}}
     }}
   }} else if(hooks && hooks->event_step){{
     pre_pc=c->pc; hr=hooks->event_step(c,b,*steps,hooks->event_user);
     if(hr>0)return MD2GBA_AOT_YIELD;
     if(hr<0)return MD2GBA_AOT_EVENT_ERROR;
     if(c->pc!=pre_pc){{*pre_step_done=1;goto redispatch;}}
   }}
 }}
 *pre_step_done=0; executed_pc=c->pc; (*steps)++;
 if(scheduled_active){{if(*scheduled_hold)*scheduled_hold=0u;else if(*scheduled_left)(*scheduled_left)--;}}
 if(hooks && hooks->handle_pc){{hr=hooks->handle_pc(c,b,c->pc,hooks->user);if(hr>0)goto redispatch;if(hr<0)return MD2GBA_AOT_HOOK_ERROR;}}
 if(hooks && hooks->fallback_pc){{hr=hooks->fallback_pc(c,b,c->pc,hooks->fallback_user);if(hr>0){{if(hooks->after_step){{int ar=hooks->after_step(c,b,executed_pc,*steps,hooks->after_user);if(ar>0)return MD2GBA_AOT_YIELD;if(ar<0)return MD2GBA_AOT_EVENT_ERROR;}}goto redispatch;}}if(hr<0)return MD2GBA_AOT_FALLBACK_ERROR;}}
 return MD2GBA_AOT_MISSING_PC;

{body}
}}
'''
        )

    decls = '\n'.join(
        f'int {fn}(Md2Gba68k*,Md2GbaBus*,const Md2GbaAotHooks*,uint32_t*,uint32_t,uint32_t,uint8_t*,uint32_t*,uint8_t*,uint8_t);'
        for _, fn, _ in shards
    )
    dispatch = '\n'.join(
        f'      case 0x{page:X}u: rc={fn}(c,b,hooks,&steps,stop,max_steps,&pre_step_done,&scheduled_left,&scheduled_hold,scheduled_active); break;'
        for page, fn, _ in shards
    )
    generated_page_cases = ''.join(f'      case 0x{page:X}u: continue;\n' for page,_,_ in shards)
    (out_dir / f'{sym}_dispatch.c').write_text(
        f'''/* AUTO-GENERATED sharded AOT dispatcher (Phase 13 scheduled direct-chain mode). */
#include <stdint.h>
#include "md2gba_aot_region.h"
#include "md2gba_cycles.h"
#include "{sym}.h"
{decls}
#define MD2GBA_P13_RETURN(v) do{{if(scheduled_active){{*hooks->scheduled_countdown=scheduled_left;}}return (v);}}while(0)
int {sym}_run(Md2Gba68k*c,Md2GbaBus*b,const Md2GbaAotHooks*hooks,uint32_t start,uint32_t stop,uint32_t max_steps){{
 uint32_t steps=0,scheduled_left=0; uint8_t pre_step_done=0,scheduled_hold=0;
 uint8_t scheduled_active=(uint8_t)(hooks && hooks->scheduled_event && hooks->scheduled_countdown);
 if(scheduled_active)scheduled_left=*hooks->scheduled_countdown;
 if(start!=MD2GBA_AOT_RESUME_PC)c->pc=start&0xFFFFFFu;
 while(steps<max_steps){{
   int rc=MD2GBA_AOT_PAGE_EXIT,hr=0; uint32_t executed_pc,pre_pc;
   switch(c->pc>>{page_shift}){{
{dispatch}
      default:rc=MD2GBA_AOT_PAGE_EXIT;break;
   }}
   if(rc!=MD2GBA_AOT_PAGE_EXIT)MD2GBA_P13_RETURN(rc);
   switch(c->pc>>{page_shift}){{
{generated_page_cases}      default:break;
   }}
   if(!pre_step_done){{
     if(steps && c->pc==stop)MD2GBA_P13_RETURN(MD2GBA_AOT_OK);
     if(scheduled_active){{
       if(scheduled_left==0u){{
         *hooks->scheduled_countdown=0u;
         pre_pc=c->pc; hr=hooks->scheduled_event(c,b,steps,hooks->scheduled_user);
         scheduled_left=*hooks->scheduled_countdown;
         if(hr>0)MD2GBA_P13_RETURN(MD2GBA_AOT_YIELD);
         if(hr<0)MD2GBA_P13_RETURN(MD2GBA_AOT_EVENT_ERROR);
         scheduled_hold=1u;
         if(c->pc!=pre_pc){{pre_step_done=1;continue;}}
       }}
     }} else if(hooks && hooks->event_step){{
       pre_pc=c->pc; hr=hooks->event_step(c,b,steps,hooks->event_user);
       if(hr>0)MD2GBA_P13_RETURN(MD2GBA_AOT_YIELD);
       if(hr<0)MD2GBA_P13_RETURN(MD2GBA_AOT_EVENT_ERROR);
       if(c->pc!=pre_pc){{pre_step_done=1;continue;}}
     }}
   }}
   pre_step_done=0; executed_pc=c->pc; steps++;
   if(scheduled_active){{if(scheduled_hold)scheduled_hold=0u;else if(scheduled_left)scheduled_left--;}}
   if(hooks && hooks->handle_pc){{hr=hooks->handle_pc(c,b,c->pc,hooks->user);if(hr>0)continue;if(hr<0)MD2GBA_P13_RETURN(MD2GBA_AOT_HOOK_ERROR);}}
   if(hooks && hooks->fallback_pc){{hr=hooks->fallback_pc(c,b,c->pc,hooks->fallback_user);if(hr>0){{if(hooks->after_step){{int ar=hooks->after_step(c,b,executed_pc,steps,hooks->after_user);if(ar>0)MD2GBA_P13_RETURN(MD2GBA_AOT_YIELD);if(ar<0)MD2GBA_P13_RETURN(MD2GBA_AOT_EVENT_ERROR);}}continue;}}if(hr<0)MD2GBA_P13_RETURN(MD2GBA_AOT_FALLBACK_ERROR);}}
   MD2GBA_P13_RETURN(MD2GBA_AOT_MISSING_PC);
 }}
 MD2GBA_P13_RETURN(MD2GBA_AOT_STEP_LIMIT);
}}
#undef MD2GBA_P13_RETURN
'''
    )
    return shards

def _abs_w_24(v: int) -> int:
    a = v & 0xFFFF
    if a & 0x8000:
        a |= 0xFF0000
    return a & 0xFFFFFF


def _ea_is_direct_wram(e: dict | None) -> bool:
    if not e:
        return False
    k = e.get('kind')
    if k == 'abs_w':
        a = _abs_w_24(int(e.get('value') or 0))
    elif k == 'abs_l':
        a = int(e.get('value') or 0) & 0xFFFFFF
    else:
        return False
    return 0xFF0000 <= a <= 0xFFFFFF


def _fastchain_poll_branch(insns: list[dict], idx: int) -> bool:
    """Return True for a tiny side-effect-free polling loop.

    The optimization is deliberately conservative: a backward Bcc must branch
    to the immediately preceding instruction, and that instruction may only
    read a fixed 68K WRAM address while updating condition codes.  Since no CPU
    state capable of changing the result is modified inside the loop, its only
    possible progress comes from an external scheduled machine event (typically
    H/VBlank interrupt work).  The fast-chain can therefore skip the redundant
    iterations and force the next scheduler deadline without changing the state
    observed after the event.
    """
    # P96 correctness: the loop body is side-effect-free, but interrupt phase is not.
    # Forcing an early scheduler seam changes the stacked return PC/timing.
    return False
    if idx <= 0:
        return False
    br = insns[idx]
    if br.get('mnemonic') != 'bcc':
        return False
    prev = insns[idx - 1]
    if br.get('target') != prev.get('address'):
        return False
    if (prev.get('address', 0) + prev.get('size', 0)) != br.get('address'):
        return False
    m = prev.get('mnemonic')
    if m == 'tst':
        return _ea_is_direct_wram(prev.get('ea'))
    if m == 'btst_imm':
        return _ea_is_direct_wram(prev.get('ea'))
    if m == 'cmpi':
        return _ea_is_direct_wram(prev.get('ea'))
    if m == 'cmp':
        return _ea_is_direct_wram(prev.get('src'))
    return False


def _deadline_poll_pair(insns: list[dict], idx: int) -> dict | None:
    """Recognize a side-effect-free fixed-WRAM poll + backward Bcc pair.

    Unlike the retired P87 idle shortcut, this optimization never advances a
    hardware deadline.  It may only collapse redundant loop iterations that fit
    wholly inside the *current* scheduled countdown.  The final iteration at the
    scheduler seam remains on the exact P96 path, preserving interrupt phase,
    stacked PC and scheduled overshoot.
    """
    if idx < 0 or idx + 1 >= len(insns):
        return None
    poll = insns[idx]
    br = insns[idx + 1]
    if br.get('mnemonic') != 'bcc':
        return None
    if br.get('target') != poll.get('address'):
        return None
    if (poll.get('address', 0) + poll.get('size', 0)) != br.get('address'):
        return None
    m = poll.get('mnemonic')
    safe = False
    if m in ('tst', 'btst_imm', 'cmpi'):
        safe = _ea_is_direct_wram(poll.get('ea'))
    elif m == 'cmp':
        safe = _ea_is_direct_wram(poll.get('src'))
    if not safe:
        return None
    # A taken Bcc is 10 cycles on 68000.  Keep the poll estimate identical to
    # the existing generated fast path so folded and ordinary execution consume
    # the same virtual cycle budget.
    poll_cycles = _fastchain_cycle_max(poll)
    branch_cycles = 10
    return {
        'poll': poll,
        'branch': br,
        'loop_cycles': poll_cycles + branch_cycles,
        'loop_steps': 2,
    }


def _emit_deadline_poll_fold(insns: list[dict], idx: int) -> str:
    """Emit a bounded poll-loop fold that cannot cross a scheduler deadline.

    One host-side probe performs the same side-effect-free WRAM read/CCR update
    as the source poll.  If the backward branch is still taken, collapse at most
    sixteen *complete* source iterations whose cycles fit before the deadline.
    Returning to the outer dispatcher after each batch preserves max_steps
    checks and prevents a hot page from hiding an arbitrarily large logical
    instruction count.  No division helper is needed on ARM7TDMI.
    """
    info = _deadline_poll_pair(insns, idx)
    if not info:
        return ''
    poll = info['poll']; br = info['branch']
    cyc = int(info['loop_cycles']); stp = int(info['loop_steps'])
    poll_cycles = int(_fastchain_cycle_max(poll)); branch_cycles = int(cyc-poll_cycles)
    cond = cond_expr(int(br.get('condition') or 0))
    # Choose the largest power-of-two batch <=16 that leaves at least one cycle
    # in the current countdown.  Any remaining iterations are handled by later
    # hot-page entries, and the final scheduler-crossing instruction is exact.
    choices = []
    for n in (16, 8, 4, 2, 1):
        choices.append((n, n * cyc, n * stp))
    select = []
    for j,(n,cc,ss) in enumerate(choices):
        kw = 'if' if j == 0 else 'else if'
        select.append(f'      {kw}(left>{cc}u){{left-={cc}u;steps+={ss}u;FAST_RETURN(MD2GBA_AOT_PAGE_EXIT);}}')
    return '\n'.join([
        f'  /* P106 deadline-local fixed-WRAM poll folding: never crosses the P96 scheduler seam. */',
        f'  if(left>{cyc}u){{',
        f'    {{ {emit_sem(poll, set_pc=False)} }}',
        f'    if({cond}){{',
        f'      st->idle=1u;st->idle_poll_pc=0x{poll["address"]:06X}u;st->idle_branch_pc=0x{br["address"]:06X}u;st->idle_poll_cycles={int(poll_cycles)}u;st->idle_branch_cycles={int(branch_cycles)}u;',
        *select,
        f'    }}',
        f'  }}',
    ]) + '\n'


def _pschybrid_inline_singleton(ins: dict) -> bool:
    """Keep cheap bus-free one-instruction continuations in the hot module.

    This is deliberately structural and title-agnostic.  PSC's translated
    modules keep ordinary ALU/register/address-control work native and reserve
    shared compatibility helpers for memory/hardware behavior.  Do the same
    here without profiling PCs or recognizing a particular game.
    """
    m = ins.get('mnemonic')
    if m in ('bcc','dbcc','bra','nop','moveq','ext','swap','shift_reg','lea','jmp','exg'):
        return True
    ea = ins.get('ea')
    if m in ('andi','ori','eori','addi','subi','cmpi','tst','clr','neg','not','addq','subq'):
        return isinstance(ea, dict) and ea.get('kind') in ('dn','an')
    src = ins.get('src')
    dst = ins.get('dst')
    if m == 'move':
        return (isinstance(src,dict) and src.get('kind') in ('dn','an','imm') and
                isinstance(dst,dict) and dst.get('kind') in ('dn','an'))
    if m == 'movea':
        return isinstance(src,dict) and src.get('kind') in ('dn','an','imm')
    if m in ('cmp','add_to_dn','sub_from_dn','or_to_dn','and_to_dn','eor_to_ea'):
        # eor_to_ea is bus-free only for a Dn destination.
        if m == 'eor_to_ea':
            return isinstance(ea,dict) and ea.get('kind') == 'dn'
        return isinstance(src,dict) and src.get('kind') in ('dn','an','imm')
    return False


def _fastchain_cycle_expr(ins: dict, nxt: int) -> str:
    """Approximate 68000 core cycles for scheduled live timing.

    Mirrors runtime md68k_estimate_cycles(), but folds the common case into
    generated code so cycle-weighted scheduling does not add a helper call on
    every translated instruction. Conditional branches select 8/10 cycles
    from the post-instruction PC.
    """
    m = ins.get('mnemonic')
    op = int(ins.get('opcode') or 0)
    mode = (op >> 3) & 7
    if m in ('nop','moveq','ext','swap','exg'):
        return '4u'
    if m in ('bra','bsr'):
        return '10u'
    if m == 'bcc':
        return f'((c->pc==0x{nxt:06X}u)?8u:10u)'
    if m == 'dbcc':
        return '10u'
    if m == 'rts':
        return '16u'
    if m == 'rte':
        return '20u'
    if m == 'trap':
        return '34u'
    if m == 'jsr':
        return '18u'
    if m == 'jmp':
        return '10u'
    if m in ('mulu','muls'):
        return '54u'
    if m in ('divu','divs'):
        return '140u'
    if m in ('movem_from_regs','movem_to_regs'):
        return '16u'
    if m in ('move','movea','addq','subq'):
        return '4u' if mode <= 1 else '8u'
    if m == 'shift_reg':
        return '8u'
    if m in ('asr_mem','asl_mem','lsr_mem','lsl_mem','roxr_mem','roxl_mem','ror_mem','rol_mem'):
        return '12u'
    return '6u' if mode <= 1 else '10u'


def _fastchain_cycle_max(ins: dict) -> int:
    m = ins.get('mnemonic')
    op = int(ins.get('opcode') or 0)
    mode = (op >> 3) & 7
    if m in ('nop','moveq','ext','swap','exg'): return 4
    if m in ('bra','bsr'): return 10
    if m == 'bcc': return 10
    if m == 'dbcc': return 10
    if m == 'rts': return 16
    if m == 'rte': return 20
    if m == 'trap': return 34
    if m == 'jsr': return 18
    if m == 'jmp': return 10
    if m in ('mulu','muls'): return 54
    if m in ('divu','divs'): return 140
    if m in ('movem_from_regs','movem_to_regs'): return 16
    if m in ('move','movea','addq','subq'): return 4 if mode <= 1 else 8
    if m == 'shift_reg': return 8
    if m in ('asr_mem','asl_mem','lsr_mem','lsl_mem','roxr_mem','roxl_mem','ror_mem','rol_mem'): return 12
    return 6 if mode <= 1 else 10


FASTCHAIN_MAX_INSNS = 64

def _generate_fastchain(out_dir: Path, sym: str, pages: dict[int, list[dict]], page_shift: int):
    '''P72/P75 scheduled live fast-chain.

    In addition to the exact per-instruction path, conventional basic-block
    leaders get a deadline-safe superblock path.  If the next machine event is
    farther away than the block's worst-case cycle cost, the generated ARM code
    can execute the whole straight-line block without re-testing the scheduler
    or doing saturating countdown arithmetic at every 68K instruction.  When a
    deadline is close (or execution resumes in the middle of a block after an
    interrupt), the original exact instruction-granular path remains available.
    '''
    state_name = f'{sym}_fast_state'
    internal = out_dir / f'{sym}_fast_internal.h'
    guard = (sym + '_FAST_INTERNAL_H').upper()
    internal.write_text(
        f'#ifndef {guard}\n#define {guard}\n#include <stdint.h>\n#include "md2gba_aot_region.h"\n'
        f'typedef struct {state_name} {{ uint32_t steps,left; uint8_t pre; }} {state_name};\n#endif\n'
    )
    shards = []
    for page, insns in sorted(pages.items()):
        fn = f'{sym}_page_{page:04x}_fastchain'
        shards.append((page, fn, len(insns)))
        owned = {i['address'] for i in insns}
        labels = {pc: f'L_{pc:06X}' for pc in owned}
        addr_to_idx = {i['address']: n for n, i in enumerate(insns)}
        entry_cases = '\n'.join(
            f'      case 0x{i["address"]:06X}u: goto {labels[i["address"]]};'
            for i in insns
        )

        # Static CFG leaders.  Interrupt/RTE can resume at any instruction, so
        # every label still has the exact slow path; only natural block entries
        # receive duplicated superblock code.
        leaders = {insns[0]['address']} if insns else set()
        for i in insns:
            pc=i['address']; nxt=(pc+i['size'])&0xFFFFFF; m=i['mnemonic']; flow=i.get('flow','fallthrough')
            t=i.get('target')
            if t in owned: leaders.add(t)
            if flow != 'fallthrough' or m in ('bra','bsr','bcc','dbcc','rts','rte','trap','jsr','jmp'):
                if nxt in owned: leaders.add(nxt)

        def successors(idx, i):
            pc=i['address']; nxt=(pc+i['size'])&0xFFFFFF; flow=i.get('flow','fallthrough'); m=i['mnemonic']; succ=[]
            if flow == 'fallthrough' and m not in ('divu','divs') and nxt in owned:
                succ=[(nxt,labels[nxt])]
            elif m == 'bra' and i.get('target') in owned:
                t=i['target']; succ=[(t,labels[t])]
            elif m in ('bcc','dbcc'):
                t=i.get('target')
                if t in owned: succ.append((t,labels[t]))
                if nxt in owned and nxt != t: succ.append((nxt,labels[nxt]))
            elif flow in ('jump','call') and i.get('target') in owned and m not in ('trap',):
                t=i['target']; succ=[(t,labels[t])]
            return succ

        def post_for(idx, i):
            succ=successors(idx,i)
            m=i['mnemonic']
            # Bcc/DBcc can have one successor in this shard and the other in a
            # different shard.  Never turn that into an unconditional local
            # goto: the generated semantic has already selected c->pc.
            if m in ('bcc','dbcc'):
                poll_wait=_fastchain_poll_branch(insns,idx)
                lines=[]
                for spc,slab in succ:
                    pp=('left=0u;' if poll_wait and spc==i.get('target') else '')
                    lines.append(f'  if(c->pc==0x{spc:06X}u){{{pp}goto {slab};}}')
                lines.append('  goto redispatch;')
                return '\n'.join(lines)
            if len(succ)==1:
                return f'  goto {succ[0][1]};'
            if len(succ)==2:
                return (f'  if(c->pc==0x{succ[0][0]:06X}u)goto {succ[0][1]};\n'
                        f'  if(c->pc==0x{succ[1][0]:06X}u)goto {succ[1][1]};\n'
                        '  goto redispatch;')
            return '  goto redispatch;'

        fast_runs={}
        for leader in leaders:
            idx=addr_to_idx.get(leader)
            if idx is None: continue
            seq=[]; j=idx
            while j < len(insns) and len(seq) < FASTCHAIN_MAX_INSNS:
                cur=insns[j]; seq.append((j,cur))
                pc=cur['address']; nxt=(pc+cur['size'])&0xFFFFFF
                flow=cur.get('flow','fallthrough'); m=cur['mnemonic']
                if flow != 'fallthrough' or m in ('bra','bsr','bcc','dbcc','rts','rte','trap','jsr','jmp','divu','divs'):
                    break
                if j+1>=len(insns) or insns[j+1]['address']!=nxt: break
                if nxt in leaders: break
                j+=1
            if len(seq)>=2:
                fast_runs[leader]=seq

        blocks=[]
        for idx,i in enumerate(insns):
            pc=i['address']; sem=emit_sem(i); nxt=(pc+i['size'])&0xFFFFFF; cyc=_fastchain_cycle_expr(i,nxt)
            post=post_for(idx,i)
            brow=byte_to_nibble_row_loop(insns,idx)
            fused=emit_byte_to_nibble_row_loop(brow) if brow and brow['next'] in owned else ''
            fusion=expand_bits_loop(insns,idx)
            fused=emit_expand_bits_loop(fusion) if fusion and fusion['next'] in owned else ''
            stores=counted_store_loop(insns,idx)
            if stores and stores['next'] in owned:fused+=emit_counted_store_loop(stores)
            copies=counted_copy_loop(insns,idx)
            if copies and copies['next'] in owned:fused+=emit_counted_copy_loop(copies)
            compares=counted_compare_loop(insns,idx)
            if compares and compares['next'] in owned:fused+=emit_counted_compare_loop(compares)
            ntok=nemesis_table_token_setup(insns,idx)
            if ntok and ntok['next'] in owned:fused+=emit_nemesis_table_token_setup(ntok)
            nconv=nibble_converge_word_loop(insns,idx)
            if nconv and nconv['next'] in owned:fused+=emit_nibble_converge_word_loop(nconv)
            nibbles=nibble_accumulate_loop(insns,idx)
            if nibbles and nibbles['next'] in owned:fused+=emit_nibble_accumulate_loop(nibbles)
            sm=strided_move_loop(insns,idx)
            if sm and sm['next'] in owned:fused+=emit_strided_move_loop(sm)
            sc=strided_clear_loop(insns,idx)
            if sc and sc['next'] in owned:fused+=emit_strided_clear_loop(sc)
            sp=strided_pair_store_loop(insns,idx)
            if sp and sp['next'] in owned:fused+=emit_strided_pair_store_loop(sp)
            fast=''
            seq=fast_runs.get(pc)
            if seq:
                maxcyc=sum(_fastchain_cycle_max(q) for _,q in seq)
                lines=[f'  if(left>{maxcyc}u){{',f'    steps+={len(seq)}u;']
                cyc_terms=[]
                last_pc_materialized=True
                for qidx,q in seq:
                    qn=(q['address']+q['size'])&0xFFFFFF
                    qcyc=_fastchain_cycle_expr(q,qn)
                    # PSC-style continuation optimization: within an admitted
                    # straight-line superblock the scheduler cannot observe the
                    # intermediate virtual PC. Keep it implicit for fallthrough
                    # instructions and materialize once at the block boundary.
                    # Control-flow/divide instructions retain the canonical
                    # emitter because their semantics can select another PC.
                    q_fall=(q.get('flow','fallthrough')=='fallthrough' and q['mnemonic'] not in ('divu','divs'))
                    lines.append(f'    {{ {emit_sem(q, set_pc=not q_fall)} }}')
                    last_pc_materialized=not q_fall
                    cyc_terms.append(f'({qcyc})')
                # The max-cycle admission check proves no event can occur inside
                # this straight-line block, so charge its exact cycles once.
                lines.append('    left-=('+ '+'.join(cyc_terms) +');')
                final_idx,final_i=seq[-1]
                if not last_pc_materialized:
                    final_next=(final_i['address']+final_i['size'])&0xFFFFFF
                    lines.append(f'    c->pc=0x{final_next:06X}u;')
                fp=post_for(final_idx,final_i).replace('\n','\n    ')
                lines.append('    '+fp)
                lines.append('  }')
                fast='\n'.join(lines)+'\n'

            blocks.append(f'''{labels[pc]}:
  if(!pre){{
    if(left==0u){{
      *hooks->scheduled_countdown=0u;
      pre_pc=c->pc; hr=hooks->scheduled_event(c,b,steps,hooks->scheduled_user);
      left=*hooks->scheduled_countdown;
      if(hr>0)FAST_RETURN(MD2GBA_AOT_YIELD);
      if(hr<0)FAST_RETURN(MD2GBA_AOT_EVENT_ERROR);
      if(c->pc!=pre_pc){{pre=1u;goto redispatch;}}
    }}
  }}
  pre=0u;
{fused}{fast}  steps++;
  {{ {sem} }}
  {{ uint32_t cyc={cyc}; left=(left<=cyc)?0u:(left-cyc); }}
{post}
''')
        body='\n'.join(blocks)
        (out_dir / f'{sym}_page_{page:04x}.c').write_text(f'''/* AUTO-GENERATED P72 deadline-safe superblock fast-chain page. */
#include <stdint.h>
#include "md2gba_aot_region.h"
#include "md2gba_cycles.h"
#include "{sym}_fast_internal.h"
int {fn}(Md2Gba68k*c,Md2GbaBus*b,const Md2GbaAotHooks*hooks,{state_name}*st){{
 uint32_t t0=0,t1=0,steps=st->steps,left=st->left,pre_pc=0; int hr=0; uint8_t pre=st->pre;
 #define FAST_RETURN(v) do{{st->steps=steps;st->left=left;st->pre=pre;return (v);}}while(0)
redispatch:
 if((c->pc>>{page_shift})!=0x{page:X}u)FAST_RETURN(MD2GBA_AOT_PAGE_EXIT);
 switch(c->pc){{
{entry_cases}
   default:break;
 }}
 if(!pre){{
   if(left==0u){{
     *hooks->scheduled_countdown=0u;
     pre_pc=c->pc; hr=hooks->scheduled_event(c,b,steps,hooks->scheduled_user);
     left=*hooks->scheduled_countdown;
     if(hr>0)FAST_RETURN(MD2GBA_AOT_YIELD);
     if(hr<0)FAST_RETURN(MD2GBA_AOT_EVENT_ERROR);
     if(c->pc!=pre_pc){{pre=1u;goto redispatch;}}
   }}
 }}
 pre=0u; steps++;
 if(hooks->fallback_pc){{pre_pc=c->pc;hr=hooks->fallback_pc(c,b,c->pc,hooks->fallback_user);if(hr>0){{uint32_t cyc=md68k_estimate_cycles(b,pre_pc,c->pc);left=(left<=cyc)?0u:(left-cyc);goto redispatch;}}if(hr<0)FAST_RETURN(MD2GBA_AOT_FALLBACK_ERROR);}}
 FAST_RETURN(MD2GBA_AOT_MISSING_PC);

{body}
#undef FAST_RETURN
}}
''')

    decls='\n'.join(f'int {fn}(Md2Gba68k*,Md2GbaBus*,const Md2GbaAotHooks*,{state_name}*);' for _,fn,_ in shards)
    maxpage=max((p for p,_,_ in shards),default=0)
    inits='\n'.join(f'    [0x{p:X}u] = {fn},' for p,fn,_ in shards)
    (out_dir / f'{sym}_dispatch.c').write_text(f'''/* AUTO-GENERATED P72 compact fast-chain dispatcher. */
#include <stdint.h>
#include "md2gba_aot_region.h"
#include "md2gba_cycles.h"
#include "{sym}.h"
#include "{sym}_fast_internal.h"
#if defined(MD2GBA_GBA_IWRAM)
#define MD2GBA_DISPATCH_HOT __attribute__((section(".iwram.dispatch")))
#else
#define MD2GBA_DISPATCH_HOT
#endif
{decls}
typedef int (*{sym}_page_fn)(Md2Gba68k*,Md2GbaBus*,const Md2GbaAotHooks*,{state_name}*);
#define {sym.upper()}_MAX_FAST_PAGE 0x{maxpage:X}u
static const {sym}_page_fn {sym}_page_table[{sym.upper()}_MAX_FAST_PAGE+1u]={{
{inits}
}};
MD2GBA_DISPATCH_HOT int {sym}_run(Md2Gba68k*c,Md2GbaBus*b,const Md2GbaAotHooks*hooks,uint32_t start,uint32_t stop,uint32_t max_steps){{
 {state_name} st={{0,0,0}}; (void)stop; (void)max_steps;
 if(!hooks || !hooks->scheduled_event || !hooks->scheduled_countdown || hooks->event_step || hooks->after_step || hooks->handle_pc) return MD2GBA_AOT_EVENT_ERROR;
 st.left=*hooks->scheduled_countdown;
 if(start!=MD2GBA_AOT_RESUME_PC)c->pc=start&0xFFFFFFu;
 for(;;){{
   int rc=MD2GBA_AOT_PAGE_EXIT,hr=0; uint32_t pre_pc,page; {sym}_page_fn fn=0;
   page=c->pc>>{page_shift}; if(page<={sym.upper()}_MAX_FAST_PAGE)fn={sym}_page_table[page];
   if(fn)rc=fn(c,b,hooks,&st);
   if(rc!=MD2GBA_AOT_PAGE_EXIT){{*hooks->scheduled_countdown=st.left;return rc;}}
   page=c->pc>>{page_shift}; fn=0; if(page<={sym.upper()}_MAX_FAST_PAGE)fn={sym}_page_table[page]; if(fn)continue;
   if(st.left==0u){{
     *hooks->scheduled_countdown=0u;pre_pc=c->pc;hr=hooks->scheduled_event(c,b,st.steps,hooks->scheduled_user);st.left=*hooks->scheduled_countdown;
     if(hr>0){{*hooks->scheduled_countdown=st.left;return MD2GBA_AOT_YIELD;}}
     if(hr<0){{*hooks->scheduled_countdown=st.left;return MD2GBA_AOT_EVENT_ERROR;}}
     if(c->pc!=pre_pc)continue;
   }}st.steps++;
   if(hooks->fallback_pc){{pre_pc=c->pc;hr=hooks->fallback_pc(c,b,c->pc,hooks->fallback_user);if(hr>0){{uint32_t cyc=md68k_estimate_cycles(b,pre_pc,c->pc);st.left=(st.left<=cyc)?0u:(st.left-cyc);continue;}}if(hr<0){{*hooks->scheduled_countdown=st.left;return MD2GBA_AOT_FALLBACK_ERROR;}}}}
   *hooks->scheduled_countdown=st.left;return MD2GBA_AOT_MISSING_PC;
 }}
}}
''')
    return shards


def _generate_pscfast(out_dir: Path, sym: str, pages: dict[int, list[dict]], page_shift: int):
    '''PSC-inspired hot-continuation generator.

    Keep native superblocks/fusions in compact hot shards and route uncommon
    exact boundaries through the shared interpreter seam. This reproduces the
    architecture of a native continuation compiler without copying PSC code.
    '''
    NEED_EXACT=8
    state_name=f'{sym}_pscfast_state'
    internal=out_dir/f'{sym}_pscfast_internal.h'
    guard=(sym+'_PSCFAST_INTERNAL_H').upper()
    internal.write_text(f'#ifndef {guard}\n#define {guard}\n#include <stdint.h>\n#include "md2gba_aot_region.h"\n'
                        f'typedef struct {state_name} {{ uint32_t steps,left; }} {state_name};\n'
                        f'#define MD2GBA_AOT_NEED_EXACT {NEED_EXACT}\n#endif\n')
    shards=[]
    for page,insns in sorted(pages.items()):
        fn=f'{sym}_page_{page:04x}_pscfast'; shards.append((page,fn,len(insns)))
        owned={i['address'] for i in insns}; labels={pc:f'L_{pc:06X}' for pc in owned}; idxmap={i['address']:n for n,i in enumerate(insns)}
        entry_cases='\n'.join(f'      case 0x{((i["address"] & ((1 << page_shift)-1)) >> 1):X}u: goto {labels[i["address"]]};' for i in insns)
        leaders={insns[0]['address']} if insns else set()
        for i in insns:
            pc=i['address']; nxt=(pc+i['size'])&0xffffff; m=i['mnemonic']; flow=i.get('flow','fallthrough'); t=i.get('target')
            if t in owned: leaders.add(t)
            if flow!='fallthrough' or m in ('bra','bsr','bcc','dbcc','rts','rte','trap','jsr','jmp'):
                if nxt in owned: leaders.add(nxt)

        def successors(idx,i):
            pc=i['address'];nxt=(pc+i['size'])&0xffffff;flow=i.get('flow','fallthrough');m=i['mnemonic'];succ=[]
            if flow=='fallthrough' and m not in ('divu','divs') and nxt in owned:succ=[(nxt,labels[nxt])]
            elif m=='bra' and i.get('target') in owned:
                t=i['target'];succ=[(t,labels[t])]
            elif m in ('bcc','dbcc'):
                t=i.get('target')
                if t in owned:succ.append((t,labels[t]))
                if nxt in owned and nxt!=t:succ.append((nxt,labels[nxt]))
            elif flow in ('jump','call') and i.get('target') in owned and m not in ('trap',):
                t=i['target'];succ=[(t,labels[t])]
            return succ

        def post_for(idx,i):
            succ=successors(idx,i);m=i['mnemonic']
            if m in ('bcc','dbcc'):
                lines=[f'    if(c->pc==0x{p:06X}u)goto {lab};' for p,lab in succ]
                lines.append('    goto redispatch;');return '\n'.join(lines)
            if len(succ)==1:return f'    goto {succ[0][1]};'
            if len(succ)==2:return (f'    if(c->pc==0x{succ[0][0]:06X}u)goto {succ[0][1]};\n'
                                    f'    if(c->pc==0x{succ[1][0]:06X}u)goto {succ[1][1]};\n    goto redispatch;')
            return '    goto redispatch;'

        fast_runs={}
        for leader in leaders:
            j=idxmap.get(leader)
            if j is None:continue
            seq=[]
            while j<len(insns) and len(seq)<FASTCHAIN_MAX_INSNS:
                cur=insns[j];seq.append((j,cur));pc=cur['address'];nxt=(pc+cur['size'])&0xffffff;flow=cur.get('flow','fallthrough');m=cur['mnemonic']
                if flow!='fallthrough' or m in ('bra','bsr','bcc','dbcc','rts','rte','trap','jsr','jmp','divu','divs'):break
                if j+1>=len(insns) or insns[j+1]['address']!=nxt:break
                if nxt in leaders:break
                j+=1
            if len(seq)>=2:fast_runs[leader]=seq

        blocks=[]
        for idx,i in enumerate(insns):
            pc=i['address']; fused=''
            deadline_poll=_emit_deadline_poll_fold(insns,idx)
            brow=byte_to_nibble_row_loop(insns,idx)
            if brow and brow['next'] in owned:fused+=emit_byte_to_nibble_row_loop(brow)
            fusion=expand_bits_loop(insns,idx)
            if fusion and fusion['next'] in owned:fused+=emit_expand_bits_loop(fusion)
            stores=counted_store_loop(insns,idx)
            if stores and stores['next'] in owned:fused+=emit_counted_store_loop(stores)
            copies=counted_copy_loop(insns,idx)
            if copies and copies['next'] in owned:fused+=emit_counted_copy_loop(copies)
            compares=counted_compare_loop(insns,idx)
            if compares and compares['next'] in owned:fused+=emit_counted_compare_loop(compares)
            ntok=nemesis_table_token_setup(insns,idx)
            if ntok and ntok['next'] in owned:fused+=emit_nemesis_table_token_setup(ntok)
            nconv=nibble_converge_word_loop(insns,idx)
            if nconv and nconv['next'] in owned:fused+=emit_nibble_converge_word_loop(nconv)
            nibbles=nibble_accumulate_loop(insns,idx)
            if nibbles and nibbles['next'] in owned:fused+=emit_nibble_accumulate_loop(nibbles)
            sm=strided_move_loop(insns,idx)
            if sm and sm['next'] in owned:fused+=emit_strided_move_loop(sm)
            sc=strided_clear_loop(insns,idx)
            if sc and sc['next'] in owned:fused+=emit_strided_clear_loop(sc)
            sp=strided_pair_store_loop(insns,idx)
            if sp and sp['next'] in owned:fused+=emit_strided_pair_store_loop(sp)
            fast='';seq=fast_runs.get(pc)
            if seq:
                maxcyc=sum(_fastchain_cycle_max(q) for _,q in seq)
                lines=[f'  if(left>{maxcyc}u){{',f'    steps+={len(seq)}u;'];cyc_terms=[];last_pc_materialized=True
                for qidx,q in seq:
                    qn=(q['address']+q['size'])&0xffffff;qcyc=_fastchain_cycle_expr(q,qn)
                    qfall=(q.get('flow','fallthrough')=='fallthrough' and q['mnemonic'] not in ('divu','divs'))
                    lines.append(f'    {{ {emit_sem(q,set_pc=not qfall)} }}');last_pc_materialized=not qfall;cyc_terms.append(f'({qcyc})')
                lines.append('    left-=('+ '+'.join(cyc_terms)+');')
                fi,final=seq[-1]
                if not last_pc_materialized:lines.append(f'    c->pc=0x{((final["address"]+final["size"])&0xffffff):06X}u;')
                lines.append(post_for(fi,final));lines.append('  }');fast='\n'.join(lines)+'\n'
            blocks.append(f'''{labels[pc]}:
{fused}{fast}  FAST_RETURN(MD2GBA_AOT_NEED_EXACT);
''')
        body='\n'.join(blocks)
        (out_dir/f'{sym}_page_{page:04x}.c').write_text(f'''/* AUTO-GENERATED PSC-inspired hot-continuation page. */
#include <stdint.h>
#include "md2gba_aot_region.h"
#include "md2gba_cycles.h"
#include "{sym}_pscfast_internal.h"
int {fn}(Md2Gba68k*c,Md2GbaBus*b,const Md2GbaAotHooks*hooks,{state_name}*st){{
 uint32_t t0=0,t1=0,steps=st->steps,left=st->left; (void)hooks;
 #define FAST_RETURN(v) do{{st->steps=steps;st->left=left;return (v);}}while(0)
redispatch:
 if((c->pc>>{page_shift})!=0x{page:X}u)FAST_RETURN(MD2GBA_AOT_PAGE_EXIT);
 switch(c->pc){{
{entry_cases}
   default:FAST_RETURN(MD2GBA_AOT_NEED_EXACT);
 }}
{body}
#undef FAST_RETURN
}}
''')

    decls='\n'.join(f'int {fn}(Md2Gba68k*,Md2GbaBus*,const Md2GbaAotHooks*,{state_name}*);' for _,fn,_ in shards)
    maxpage=max((p for p,_,_ in shards),default=0)
    inits='\n'.join(f'    [0x{p:X}u] = {fn},' for p,fn,_ in shards)
    (out_dir/f'{sym}_dispatch.c').write_text(f'''/* AUTO-GENERATED PSC-inspired hot continuation dispatcher. */
#include <stdint.h>
#include "md2gba_aot_region.h"
#include "md2gba_cycles.h"
#include "{sym}.h"
#include "{sym}_pscfast_internal.h"
#if defined(MD2GBA_GBA_IWRAM)
#define MD2GBA_DISPATCH_HOT __attribute__((section(".iwram.dispatch")))
#else
#define MD2GBA_DISPATCH_HOT
#endif
{decls}
typedef int (*{sym}_page_fn)(Md2Gba68k*,Md2GbaBus*,const Md2GbaAotHooks*,{state_name}*);
#define {sym.upper()}_MAX_FAST_PAGE 0x{maxpage:X}u
static const {sym}_page_fn {sym}_page_table[{sym.upper()}_MAX_FAST_PAGE+1u]={{
{inits}
}};
MD2GBA_DISPATCH_HOT int {sym}_run(Md2Gba68k*c,Md2GbaBus*b,const Md2GbaAotHooks*hooks,uint32_t start,uint32_t stop,uint32_t max_steps){{
 {state_name} st={{0,0}}; if(!hooks||!hooks->scheduled_event||!hooks->scheduled_countdown||hooks->event_step||hooks->after_step||hooks->handle_pc)return MD2GBA_AOT_EVENT_ERROR;
 st.left=*hooks->scheduled_countdown;if(start!=MD2GBA_AOT_RESUME_PC)c->pc=start&0xffffffu;
 for(;;){{
   int rc=MD2GBA_AOT_NEED_EXACT,hr=0;uint32_t pre_pc,page;{sym}_page_fn fn=0;
   if(st.steps>=max_steps){{*hooks->scheduled_countdown=st.left;return MD2GBA_AOT_STEP_LIMIT;}}
   if(st.steps&&c->pc==stop){{*hooks->scheduled_countdown=st.left;return MD2GBA_AOT_OK;}}
   if(st.left==0u){{
     *hooks->scheduled_countdown=0u;pre_pc=c->pc;hr=hooks->scheduled_event(c,b,st.steps,hooks->scheduled_user);st.left=*hooks->scheduled_countdown;
     if(hr>0){{*hooks->scheduled_countdown=st.left;return MD2GBA_AOT_YIELD;}}
     if(hr<0){{*hooks->scheduled_countdown=st.left;return MD2GBA_AOT_EVENT_ERROR;}}
     if(c->pc!=pre_pc)continue;
   }}
   page=c->pc>>{page_shift};if(page<={sym.upper()}_MAX_FAST_PAGE)fn={sym}_page_table[page];if(fn)rc=fn(c,b,hooks,&st);
   if(rc==MD2GBA_AOT_PAGE_EXIT)continue;
   if(rc!=MD2GBA_AOT_NEED_EXACT){{*hooks->scheduled_countdown=st.left;return rc;}}
   /* Shared cold oracle: execute exactly one guest instruction, then re-enter native continuation dispatch. */
   st.steps++;
   if(hooks->fallback_pc){{pre_pc=c->pc;hr=hooks->fallback_pc(c,b,c->pc,hooks->fallback_user);if(hr>0){{uint32_t cyc=md68k_estimate_cycles(b,pre_pc,c->pc);st.left=(st.left<=cyc)?0u:(st.left-cyc);continue;}}if(hr<0){{*hooks->scheduled_countdown=st.left;return MD2GBA_AOT_FALLBACK_ERROR;}}}}
   *hooks->scheduled_countdown=st.left;return MD2GBA_AOT_MISSING_PC;
 }}
}}
''')
    return shards

def _generate_pschybrid(out_dir: Path, sym: str, pages: dict[int, list[dict]], page_shift: int,
                        iwram_hot_pages: set[int] | None = None,
                        profile_token_hooks: dict[int, dict] | None = None):
    '''PSC-inspired hot continuations with generated exact cold seams.

    Phase 87 PSCFAST proved that splitting compact native superblocks away from
    the instruction-exact CFG reduces hot-function spill pressure. Its first
    version regressed runtime by routing every non-admitted/single-instruction
    seam through the shared interpreter. PSCHYBRID keeps the compact hot
    function, but emits a second per-page cold function that executes exactly
    one already-decoded instruction in generated ARM. The interpreter remains
    only for truly unowned PCs.
    '''
    NEED_EXACT=8
    iwram_hot_pages=set(iwram_hot_pages or ())
    profile_token_hooks=dict(profile_token_hooks or {})
    state_name=f'{sym}_pschybrid_state'
    internal=out_dir/f'{sym}_pschybrid_internal.h'
    guard=(sym+'_PSCHYBRID_INTERNAL_H').upper()
    internal.write_text(f'#ifndef {guard}\n#define {guard}\n#include <stdint.h>\n#include "md2gba_aot_region.h"\n'
                        f'typedef struct {state_name} {{ uint32_t steps,left,pre,idle,over,idle_poll_pc,idle_branch_pc,idle_poll_cycles,idle_branch_cycles; }} {state_name};\n'
                        f'#define MD2GBA_AOT_NEED_EXACT {NEED_EXACT}\n#endif\n')
    shards=[]
    for page,insns in sorted(pages.items()):
        hotfn=f'{sym}_page_{page:04x}_pschybrid'
        exactfn=f'{sym}_page_{page:04x}_exact'
        shards.append((page,hotfn,len(insns)))
        owned={i['address'] for i in insns}; labels={pc:f'L_{pc:06X}' for pc in owned}; idxmap={i['address']:n for n,i in enumerate(insns)}
        # PSC-style native continuation dispatch.  Do not leave this as a C
        # switch: LLVM's Thumb-1 switch lowering can synthesize integer divide
        # helpers for some sparse case patterns.  PSC itself uses a range check
        # plus native continuation pointer table, so emit that shape explicitly.
        dispatch_indices={((i["address"] & ((1 << page_shift)-1)) >> 1): labels[i["address"]] for i in insns}
        dispatch_min=min(dispatch_indices) if dispatch_indices else 0
        dispatch_max=max(dispatch_indices) if dispatch_indices else -1
        dispatch_entries=',\n'.join('    &&'+dispatch_indices.get(di, 'L_unowned') for di in range(dispatch_min, dispatch_max+1))
        leaders={insns[0]['address']} if insns else set()
        for i in insns:
            pc=i['address']; nxt=(pc+i['size'])&0xffffff; m=i['mnemonic']; flow=i.get('flow','fallthrough'); t=i.get('target')
            if t in owned: leaders.add(t)
            if flow!='fallthrough' or m in ('bra','bsr','bcc','dbcc','rts','rte','trap','jsr','jmp'):
                if nxt in owned: leaders.add(nxt)

        def successors(idx,i):
            pc=i['address'];nxt=(pc+i['size'])&0xffffff;flow=i.get('flow','fallthrough');m=i['mnemonic'];succ=[]
            if flow=='fallthrough' and m not in ('divu','divs') and nxt in owned:succ=[(nxt,labels[nxt])]
            elif m=='bra' and i.get('target') in owned:
                t=i['target'];succ=[(t,labels[t])]
            elif m in ('bcc','dbcc'):
                t=i.get('target')
                if t in owned:succ.append((t,labels[t]))
                if nxt in owned and nxt!=t:succ.append((nxt,labels[nxt]))
            elif flow in ('jump','call') and i.get('target') in owned and m not in ('trap',):
                t=i['target'];succ=[(t,labels[t])]
            return succ

        def post_for(idx,i):
            succ=successors(idx,i);m=i['mnemonic']
            if m in ('bcc','dbcc'):
                poll_wait=_fastchain_poll_branch(insns,idx)
                lines=[]
                for p,lab in succ:
                    pp=('st->idle=1u;left=0u;' if poll_wait and p==i.get('target') else '')
                    lines.append(f'    if(c->pc==0x{p:06X}u){{{pp}goto {lab};}}')
                lines.append('    goto redispatch;');return '\n'.join(lines)
            if len(succ)==1:return f'    goto {succ[0][1]};'
            if len(succ)==2:return (f'    if(c->pc==0x{succ[0][0]:06X}u)goto {succ[0][1]};\n'
                                    f'    if(c->pc==0x{succ[1][0]:06X}u)goto {succ[1][1]};\n    goto redispatch;')
            return '    goto redispatch;'

        fast_runs={}
        for leader in leaders:
            j=idxmap.get(leader)
            if j is None:continue
            seq=[]
            while j<len(insns) and len(seq)<FASTCHAIN_MAX_INSNS:
                cur=insns[j];seq.append((j,cur));pc=cur['address'];nxt=(pc+cur['size'])&0xffffff;flow=cur.get('flow','fallthrough');m=cur['mnemonic']
                if flow!='fallthrough' or m in ('bra','bsr','bcc','dbcc','rts','rte','trap','jsr','jmp','divu','divs'):break
                if j+1>=len(insns) or insns[j+1]['address']!=nxt:break
                if nxt in leaders:break
                j+=1
            if len(seq)>=2:fast_runs[leader]=seq

        blocks=[]
        for idx,i in enumerate(insns):
            pc=i['address']; fused=''
            profile_hook=''
            ph=profile_token_hooks.get(pc)
            if ph is not None:
                fn=ident(str(ph['function']))
                args=('c,b,left,&pcost,&psteps' if ph.get('with_bus') else 'c,left,&pcost,&psteps')
                if ph.get('dynamic_next'):
                    # Profile semantic owns c->pc on success (e.g. a native
                    # superblock ending in RTS).  The generic emitter merely
                    # charges its exact scheduler cost and redispatches.
                    profile_hook=('  if(left){ uint32_t pcost=0u,psteps=0u; if(%s(%s)){left-=pcost;steps+=psteps;goto redispatch;} }\n'
                                  % (fn,args))
                else:
                    hn=int(ph['next']) & 0xFFFFFF
                    if hn not in owned:
                        raise ValueError(f'profile token hook next PC {hn:06X} from {pc:06X} is outside generated page')
                    profile_hook=('  if(left){ uint32_t pcost=0u,psteps=0u; if(%s(%s)){left-=pcost;steps+=psteps;c->pc=0x%06Xu;goto %s;} }\n'
                                  % (fn,args,hn,labels[hn]))
            deadline_poll=_emit_deadline_poll_fold(insns,idx)
            brow=byte_to_nibble_row_loop(insns,idx)
            if brow and brow['next'] in owned:fused+=emit_byte_to_nibble_row_loop(brow)
            fusion=expand_bits_loop(insns,idx)
            if fusion and fusion['next'] in owned:fused+=emit_expand_bits_loop(fusion)
            stores=counted_store_loop(insns,idx)
            if stores and stores['next'] in owned:fused+=emit_counted_store_loop(stores)
            copies=counted_copy_loop(insns,idx)
            if copies and copies['next'] in owned:fused+=emit_counted_copy_loop(copies)
            compares=counted_compare_loop(insns,idx)
            if compares and compares['next'] in owned:fused+=emit_counted_compare_loop(compares)
            ntok=nemesis_table_token_setup(insns,idx)
            if ntok and ntok['next'] in owned:fused+=emit_nemesis_table_token_setup(ntok)
            nconv=nibble_converge_word_loop(insns,idx)
            if nconv and nconv['next'] in owned:fused+=emit_nibble_converge_word_loop(nconv)
            nibbles=nibble_accumulate_loop(insns,idx)
            if nibbles and nibbles['next'] in owned:fused+=emit_nibble_accumulate_loop(nibbles)
            sm=strided_move_loop(insns,idx)
            if sm and sm['next'] in owned:fused+=emit_strided_move_loop(sm)
            sc=strided_clear_loop(insns,idx)
            if sc and sc['next'] in owned:fused+=emit_strided_clear_loop(sc)
            sp=strided_pair_store_loop(insns,idx)
            if sp and sp['next'] in owned:fused+=emit_strided_pair_store_loop(sp)
            fast='';seq=fast_runs.get(pc)
            if seq:
                maxcyc=sum(_fastchain_cycle_max(q) for _,q in seq)
                lines=[f'  if(left>{maxcyc}u){{',f'    steps+={len(seq)}u;'];cyc_terms=[];last_pc_materialized=True
                for qidx,q in seq:
                    qn=(q['address']+q['size'])&0xffffff;qcyc=_fastchain_cycle_expr(q,qn)
                    qfall=(q.get('flow','fallthrough')=='fallthrough' and q['mnemonic'] not in ('divu','divs'))
                    lines.append(f'    {{ {emit_sem(q,set_pc=not qfall)} }}');last_pc_materialized=not qfall;cyc_terms.append(f'({qcyc})')
                lines.append('    left-=('+ '+'.join(cyc_terms)+');')
                fi,final=seq[-1]
                if not last_pc_materialized:lines.append(f'    c->pc=0x{((final["address"]+final["size"])&0xffffff):06X}u;')
                lines.append(post_for(fi,final));lines.append('  }');fast='\n'.join(lines)+'\n'
            # A validated semantic replacement keeps the generated exact
            # one-instruction seam as fallback but intentionally omits the
            # duplicate hot-chain body. This is how profile-native modules can
            # shrink ROM fetch pressure without changing canonical semantics.
            if ph is not None and ph.get('replace_hot'):
                deadline_poll=''
                fused=''
                fast=''
            inline_single = ''
            if not fused and not fast and not (ph is not None and ph.get('replace_hot')) and _pschybrid_inline_singleton(i):
                nxt=(pc+i['size'])&0xffffff
                cyc=_fastchain_cycle_expr(i,nxt)
                poll_wait=(i['mnemonic']=='bcc' and _fastchain_poll_branch(insns,idx) and i.get('target') is not None)
                # Keep this one-instruction continuation in the hot module.  It
                # may run at a scheduler redirect destination (st->pre), but a
                # normal zero countdown is still a hard event boundary.
                ls=['  if(left==0u && !st->pre)FAST_RETURN(MD2GBA_AOT_PAGE_EXIT);',
                    '  if(st->pre)st->pre=0u;',
                    f'  {{ {emit_sem(i)} }}',
                    f'  {{ uint32_t cyc=({cyc}); if(left<=cyc){{{{over+=cyc-left;left=0u;}}}}else left-=cyc; }}']
                if poll_wait:
                    ls.append(f'  if(c->pc==0x{i["target"]:06X}u){{st->idle=1u;left=0u;}}')
                ls += ['  steps++;','  if(left==0u)FAST_RETURN(MD2GBA_AOT_PAGE_EXIT);']
                target=i.get('target')
                if target in owned and nxt in owned:
                    ls.append(f'  if(c->pc==0x{target:06X}u)goto {labels[target]};')
                    if nxt!=target: ls.append(f'  if(c->pc==0x{nxt:06X}u)goto {labels[nxt]};')
                    ls.append('  goto redispatch;')
                else:
                    ls.append('  goto redispatch;')
                inline_single='\n'.join(ls)+'\n'
            blocks.append(f'''{labels[pc]}:
{profile_hook}{deadline_poll}{fused}{fast}{inline_single}  goto local_exact;
''')
        body='\n'.join(blocks)

        exact_cases=[]
        for idx,i in enumerate(insns):
            pc=i['address']
            # Match the shared exact cycle estimator, while retaining the proven
            # fastchain polling-loop scheduler shortcut. The latter is a timing
            # optimization, not guest-visible instruction semantics.
            poll=''
            if i['mnemonic'] in ('bcc','dbcc') and _fastchain_poll_branch(insns,idx) and i.get('target') is not None:
                poll=f' if(c->pc==0x{i["target"]:06X}u){{st->idle=1u;st->left=0u;}}'
            # The decoded instruction and its fall-through PC are static here, so
            # use the same cycle rule as the deadline-safe fast chain instead of
            # calling md68k_estimate_cycles() at runtime. Dynamic fallback paths
            # retain the shared estimator.
            cyc = _fastchain_cycle_expr(i, (pc + i['size']) & 0xFFFFFF)
            exact_cases.append(f'    case 0x{((pc & ((1 << page_shift)-1)) >> 1):X}u: {{ uint32_t pre_pc=0x{pc:06X}u; {emit_sem(i)} uint32_t cyc={cyc}; if(st->left<=cyc){{{{st->over+=cyc-st->left;st->left=0u;}}}}else st->left-=cyc;{poll} st->steps++; return MD2GBA_AOT_PAGE_EXIT; }}')
        exact_switch='\n'.join(exact_cases)
        hot_attr = ('__attribute__((section(\".iwram.aot_hotpage\"),noinline)) ' if page in iwram_hot_pages else '')
        page_hook_headers=sorted({str(h['header']) for pc0,h in profile_token_hooks.items() if (pc0 >> page_shift)==page})
        profile_includes=''.join(f'#include \"{h}\"\n' for h in page_hook_headers)
        (out_dir/f'{sym}_page_{page:04x}.c').write_text(f'''/* AUTO-GENERATED PSC-inspired split hot/cold continuation page. */
#include <stdint.h>
#include "md2gba_aot_region.h"
#include "md2gba_cycles.h"
#include "{sym}_pschybrid_internal.h"
{profile_includes}int {exactfn}(Md2Gba68k*,Md2GbaBus*,{state_name}*);
{hot_attr}int {hotfn}(Md2Gba68k*c,Md2GbaBus*b,const Md2GbaAotHooks*hooks,{state_name}*st){{
 uint32_t t0=0,t1=0,steps=st->steps,left=st->left,over=st->over; (void)hooks;
 #define FAST_RETURN(v) do{{st->steps=steps;st->left=left;st->over=over;return (v);}}while(0)
 static const void * const continuation_table[]={{
{dispatch_entries}
 }};
redispatch:
 if((c->pc>>{page_shift})!=0x{page:X}u)FAST_RETURN(MD2GBA_AOT_PAGE_EXIT);
 {{ uint32_t di=(c->pc & ((1u<<{page_shift})-1u))>>1;
    if(di<0x{dispatch_min:X}u || di>0x{dispatch_max:X}u)FAST_RETURN(MD2GBA_AOT_NEED_EXACT);
    goto *continuation_table[di-0x{dispatch_min:X}u]; }}
L_unowned:
 FAST_RETURN(MD2GBA_AOT_NEED_EXACT);
{body}
local_exact:
 /* PSC-style same-module continuation: keep ordinary one-instruction seams
    inside this native page. A zero countdown is a hard scheduler boundary: do
    not execute the cold continuation until the outer dispatcher services it. */
 if(left==0u && !st->pre)FAST_RETURN(MD2GBA_AOT_PAGE_EXIT);
 {{
   uint32_t pre_once=st->pre;
   if(pre_once)st->pre=0u;
   st->steps=steps;st->left=left;st->over=over;
   int er={exactfn}(c,b,st);
   steps=st->steps;left=st->left;over=st->over;
   if(er==MD2GBA_AOT_PAGE_EXIT){{if(left==0u)FAST_RETURN(MD2GBA_AOT_PAGE_EXIT);goto redispatch;}}
   FAST_RETURN(er);
 }}
#undef FAST_RETURN
}}
/* Cold generated one-instruction oracle for exact scheduler seams.
   Keep it native, but do not spend compile time/ROM size aggressively optimizing
   a path that should only run at scheduler seams or rare block boundaries. */
#if defined(__clang__)
#define MD2GBA_AOT_COLD_EXACT __attribute__((noinline,cold,minsize,section(".rom.cold.aot")))
#elif defined(__GNUC__)
#define MD2GBA_AOT_COLD_EXACT __attribute__((noinline,cold,optimize("Os"),section(".rom.cold.aot")))
#else
#define MD2GBA_AOT_COLD_EXACT
#endif
MD2GBA_AOT_COLD_EXACT int {exactfn}(Md2Gba68k*c,Md2GbaBus*b,{state_name}*st){{
 uint32_t t0=0,t1=0;(void)t0;(void)t1;
 switch((c->pc & ((1u<<{page_shift})-1u))>>1){{
{exact_switch}
   default:return MD2GBA_AOT_NEED_EXACT;
 }}
}}
''')

    hotdecl='\n'.join(f'int {fn}(Md2Gba68k*,Md2GbaBus*,const Md2GbaAotHooks*,{state_name}*);' for _,fn,_ in shards)
    exactdecl='\n'.join(f'int {sym}_page_{p:04x}_exact(Md2Gba68k*,Md2GbaBus*,{state_name}*);' for p,_,_ in shards)
    maxpage=max((p for p,_,_ in shards),default=0)
    hotinits='\n'.join(f'    [0x{p:X}u] = {fn},' for p,fn,_ in shards)
    exactinits='\n'.join(f'    [0x{p:X}u] = {sym}_page_{p:04x}_exact,' for p,_,_ in shards)
    (out_dir/f'{sym}_dispatch.c').write_text(f'''/* AUTO-GENERATED PSC-inspired split hot/cold dispatcher. */
#include <stdint.h>
#include "md2gba_aot_region.h"
#include "md2gba_cycles.h"
#include "{sym}.h"
#include "{sym}_pschybrid_internal.h"
#if defined(MD2GBA_GBA_IWRAM)
#define MD2GBA_DISPATCH_HOT __attribute__((section(".iwram.dispatch")))
#else
#define MD2GBA_DISPATCH_HOT
#endif
{hotdecl}
{exactdecl}
typedef int (*{sym}_hot_fn)(Md2Gba68k*,Md2GbaBus*,const Md2GbaAotHooks*,{state_name}*);
typedef int (*{sym}_exact_fn)(Md2Gba68k*,Md2GbaBus*,{state_name}*);
#define {sym.upper()}_MAX_FAST_PAGE 0x{maxpage:X}u
static const {sym}_hot_fn {sym}_hot_table[{sym.upper()}_MAX_FAST_PAGE+1u]={{
{hotinits}
}};
static const {sym}_exact_fn {sym}_exact_table[{sym.upper()}_MAX_FAST_PAGE+1u]={{
{exactinits}
}};
#if defined(__GNUC__) || defined(__clang__)
#define MD2GBA_DISPATCH_COLD __attribute__((noinline,cold))
#else
#define MD2GBA_DISPATCH_COLD
#endif
/* PSC-style seam: leave the hot continuation dispatcher, execute one generated
   native exact continuation, then immediately resume hot dispatch. Keep this
   table lookup in prefetched ROM instead of spending scarce IWRAM on a rare path. */
MD2GBA_DISPATCH_COLD static int {sym}_run_seam(Md2Gba68k*c,Md2GbaBus*b,const Md2GbaAotHooks*hooks,{state_name}*st){{
 uint32_t page=c->pc>>{page_shift},pre_pc,cyc;int rc=MD2GBA_AOT_NEED_EXACT,hr;{sym}_exact_fn exact=0;
 if(page<={sym.upper()}_MAX_FAST_PAGE)exact={sym}_exact_table[page];
 if(exact){{rc=exact(c,b,st);if(rc!=MD2GBA_AOT_NEED_EXACT)return rc;}}
 if(hooks->fallback_pc){{pre_pc=c->pc;hr=hooks->fallback_pc(c,b,c->pc,hooks->fallback_user);if(hr>0){{cyc=md68k_estimate_cycles(b,pre_pc,c->pc);if(st->left<=cyc){{st->over+=cyc-st->left;st->left=0u;}}else st->left-=cyc;st->steps++;return MD2GBA_AOT_PAGE_EXIT;}}if(hr<0)return MD2GBA_AOT_FALLBACK_ERROR;}}
 return MD2GBA_AOT_MISSING_PC;
}}
MD2GBA_DISPATCH_HOT int {sym}_run(Md2Gba68k*c,Md2GbaBus*b,const Md2GbaAotHooks*hooks,uint32_t start,uint32_t stop,uint32_t max_steps){{
 {state_name} st; st.steps=0u;st.left=0u;st.pre=0u;st.idle=0u;st.over=0u;st.idle_poll_pc=0u;st.idle_branch_pc=0u;st.idle_poll_cycles=0u;st.idle_branch_cycles=0u; if(!hooks||!hooks->scheduled_event||!hooks->scheduled_countdown||!hooks->scheduled_overshoot||hooks->event_step||hooks->after_step||hooks->handle_pc)return MD2GBA_AOT_EVENT_ERROR;
 st.left=*hooks->scheduled_countdown;st.over=*hooks->scheduled_overshoot;if(start!=MD2GBA_AOT_RESUME_PC)c->pc=start&0xffffffu;
 for(;;){{
   int rc=MD2GBA_AOT_NEED_EXACT,hr=0;uint32_t pre_pc,page;{sym}_hot_fn hot=0;
   if(st.steps>=max_steps){{*hooks->scheduled_countdown=st.left;*hooks->scheduled_overshoot=st.over;return MD2GBA_AOT_STEP_LIMIT;}}
   if(st.steps&&c->pc==stop){{*hooks->scheduled_countdown=st.left;*hooks->scheduled_overshoot=st.over;return MD2GBA_AOT_OK;}}
   if(st.left==0u && !st.pre){{
     if(st.idle){{
       if(hooks->scheduled_idle_hint)*hooks->scheduled_idle_hint=1u;
       if(hooks->scheduled_idle_descriptor){{hooks->scheduled_idle_descriptor[0]=st.idle_poll_pc;hooks->scheduled_idle_descriptor[1]=st.idle_branch_pc;hooks->scheduled_idle_descriptor[2]=st.idle_poll_cycles;hooks->scheduled_idle_descriptor[3]=st.idle_branch_cycles;}}
       st.idle=0u;
     }}
     if(hooks->scheduled_idle_steps)*hooks->scheduled_idle_steps=0u;
     *hooks->scheduled_countdown=0u;*hooks->scheduled_overshoot=st.over;pre_pc=c->pc;hr=hooks->scheduled_event(c,b,st.steps,hooks->scheduled_user);st.left=*hooks->scheduled_countdown;st.over=*hooks->scheduled_overshoot;
     if(hooks->scheduled_idle_steps){{st.steps+=*hooks->scheduled_idle_steps;*hooks->scheduled_idle_steps=0u;}}
     if(hr>0){{*hooks->scheduled_countdown=st.left;*hooks->scheduled_overshoot=st.over;return MD2GBA_AOT_YIELD;}}
     if(hr<0){{*hooks->scheduled_countdown=st.left;*hooks->scheduled_overshoot=st.over;return MD2GBA_AOT_EVENT_ERROR;}}
     if(c->pc!=pre_pc){{st.pre=1u;continue;}}
   }}
   page=c->pc>>{page_shift};if(page<={sym.upper()}_MAX_FAST_PAGE)hot={sym}_hot_table[page];if(hot)rc=hot(c,b,hooks,&st);
   if(rc==MD2GBA_AOT_PAGE_EXIT){{st.pre=0u;continue;}}
   if(rc!=MD2GBA_AOT_NEED_EXACT){{*hooks->scheduled_countdown=st.left;*hooks->scheduled_overshoot=st.over;return rc;}}
   /* A hot block can deliberately force a scheduler seam. Do not execute a
      cold instruction past that boundary.  The sole exception is an event
      redirect destination marked pre=1: that instruction must run once before
      the zero-countdown scheduler is eligible again. */
   if(st.left==0u && !st.pre)continue;
   rc={sym}_run_seam(c,b,hooks,&st);
   if(rc==MD2GBA_AOT_PAGE_EXIT){{st.pre=0u;continue;}}
   *hooks->scheduled_countdown=st.left;*hooks->scheduled_overshoot=st.over;return rc;
 }}
}}
''')
    return shards

def generate(rom: bytes, pcs: list[int], out_dir: Path, symbol: str,
             page_shift: int = 14, dispatch_mode: str = 'chain',
             iwram_hot_pages: set[int] | None = None,
             profile_token_hooks: dict[int, dict] | None = None) -> dict:
    if page_shift < 10 or page_shift > 20:
        raise ValueError('page_shift must be 10..20')
    if dispatch_mode not in ('chain', 'fastchain', 'pscfast', 'pschybrid', 'burst', 'step'):
        raise ValueError('dispatch_mode must be chain, fastchain, pscfast, pschybrid, burst or step')
    sym = ident(symbol)
    out_dir.mkdir(parents=True, exist_ok=True)
    # Avoid stale shards when regenerating the same symbol into an existing directory.
    for old in out_dir.glob(f'{sym}_page_*.c'):
        old.unlink()
    for old in (out_dir / f'{sym}_dispatch.c', out_dir / f'{sym}.h', out_dir / f'{sym}_manifest.json'):
        if old.exists():
            old.unlink()

    # 68000 instruction fetches are word-aligned.  Odd PCs are address-error
    # conditions, never native continuation entries.  Keep them out of the
    # static graph so halfword-indexed page dispatch cannot alias an even PC.
    all_pcs = sorted(set(int(pc) & 0xFFFFFF for pc in pcs))
    rejected_odd_pcs = [pc for pc in all_pcs if pc & 1]
    native_pcs = [pc for pc in all_pcs if not (pc & 1)]
    decoded = [with_bytes(rom, decode(rom, pc)) for pc in native_pcs]
    pages: dict[int, list[dict]] = {}
    for ins in decoded:
        pages.setdefault(ins['address'] >> page_shift, []).append(ins)
    _write_header(out_dir, sym)
    if dispatch_mode == 'step':
        shards = _generate_step(out_dir, sym, pages, page_shift)
    elif dispatch_mode == 'pscfast':
        shards = _generate_pscfast(out_dir, sym, pages, page_shift)
    elif dispatch_mode == 'pschybrid':
        shards = _generate_pschybrid(out_dir, sym, pages, page_shift, iwram_hot_pages, profile_token_hooks)
    elif dispatch_mode == 'burst':
        shards = _generate_burst(out_dir, sym, pages, page_shift)
    elif dispatch_mode == 'fastchain':
        shards = _generate_fastchain(out_dir, sym, pages, page_shift)
    else:
        shards = _generate_chain(out_dir, sym, pages, page_shift)

    rep = {
        'symbol': sym,
        'instruction_count': len(decoded),
        'rejected_odd_pc_count': len(rejected_odd_pcs),
        'rejected_odd_pcs': [f'{pc:06X}' for pc in rejected_odd_pcs],
        'page_shift': page_shift,
        'page_size': 1 << page_shift,
        'page_count': len(pages),
        'dispatch_mode': dispatch_mode,
        'iwram_hot_pages': [f'{p:X}' for p in sorted(iwram_hot_pages or ())],
        'profile_token_hook_pcs': [f'{p:06X}' for p in sorted((profile_token_hooks or {}).keys())],
        'max_instructions_per_page': max((len(v) for v in pages.values()), default=0),
        'pages': [
            {'page': f'{p:X}', 'base': f'{p << page_shift:06X}', 'instruction_count': n,
             'source': f'{sym}_page_{p:04x}.c'}
            for p, _, n in shards
        ],
    }
    (out_dir / f'{sym}_manifest.json').write_text(json.dumps(rep, indent=2))
    return rep


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('rom', type=Path)
    ap.add_argument('addresses', type=Path)
    ap.add_argument('-o', '--out-dir', type=Path, required=True)
    ap.add_argument('--symbol', default='md2gba_region')
    ap.add_argument('--page-shift', type=int, default=14)
    ap.add_argument('--dispatch-mode', choices=('chain', 'fastchain', 'pscfast', 'pschybrid', 'burst', 'step'), default='chain',
                    help='pschybrid (PSC hot + generated cold seams), pscfast (interpreter-seam experiment), fastchain (scheduled live specialization), chain (Phase 13 generic), burst (Phase 11), or legacy step')
    ap.add_argument('--iwram-hot-pages', default='',
                    help='comma-separated generated page numbers (hex or decimal) whose hot pschybrid functions are placed in IWRAM; profile-guided compiler policy, not runtime title logic')
    ap.add_argument('--profile-token-hooks', type=Path,
                    help='generated-profile JSON for exact token-boundary shortcuts; canonical AOT remains fallback on mismatch/deadline')
    a = ap.parse_args()
    pcs = collect_pcs(json.loads(a.addresses.read_text()))
    hot_pages=set()
    for tok in (x.strip() for x in a.iwram_hot_pages.split(',')):
        if tok: hot_pages.add(int(tok,0))
    profile_hooks={}
    if a.profile_token_hooks:
        cfg=json.loads(a.profile_token_hooks.read_text())
        default_header=cfg.get('header'); default_function=cfg.get('function')
        for k,v in cfg.get('hooks',{}).items():
            if isinstance(v,dict):
                header=str(v.get('header',default_header)); function=ident(str(v.get('function',default_function)))
                dynamic_next=bool(v.get('dynamic_next',False)); replace_hot=bool(v.get('replace_hot',False))
                nxt=v.get('next'); with_bus=bool(v.get('with_bus',False))
            else:
                header=str(default_header); function=ident(str(default_function)); nxt=v; with_bus=False
                dynamic_next=False; replace_hot=False
            if not header or not function: raise ValueError(f'missing profile hook header/function for {k}')
            if not dynamic_next and nxt is None: raise ValueError(f'missing profile hook next PC for {k}')
            profile_hooks[int(k,0)]={'next': (int(nxt,0) if nxt is not None else None), 'header': header, 'function': function,
                                     'with_bus': with_bus, 'dynamic_next': dynamic_next, 'replace_hot': replace_hot}
    print(json.dumps(generate(a.rom.read_bytes(), pcs, a.out_dir, a.symbol,
                              a.page_shift, a.dispatch_mode, hot_pages, profile_hooks), indent=2))


if __name__ == '__main__':
    main()
