#!/usr/bin/env python3
"""Clean-room behavior/translator inventory for a user-supplied GBA Phantasy Star Collection ROM.

The report intentionally emits no PSC machine-code blobs.  Version-specific ROM
offsets are research anchors only; MD2GBA consumes the derived *behaviors*, never
these addresses.  This lets the project study PSC's static-translation architecture
without making PSC code or data a runtime dependency.
"""
from __future__ import annotations
import argparse, hashlib, json, struct
from pathlib import Path

GBA_BASE=0x08000000
TABLES={'ps3':(0x28702C,110),'ps2':(0x6C9DFC,54)}
HELPERS={
    'read8': {'ps3':0x0836D0,'ps2':0x395A30,'compare':300},
    'write8':{'ps3':0x0839FC,'ps2':0x395D5C,'compare':108},
    'write16':{'ps3':0x083A68,'ps2':0x395DC8,'compare':128},
}


def cstr(b:bytes, off:int)->str:
    if not (0<=off<len(b)): return '?'
    e=b.find(b'\0',off,off+64)
    if e<0:e=min(len(b),off+64)
    return b[off:e].decode('ascii','replace')


def modules(b:bytes, off:int, count:int):
    out=[]
    for i in range(count):
        rec=struct.unpack_from('<5I',b,off+i*20)
        name=cstr(b,rec[0]-GBA_BASE)
        out.append({'index':i,'name':name,'entrypoints':[rec[1],rec[2],rec[3]],'flags':rec[4]})
    return out


def raw_similarity(b:bytes,a:int,c:int,n:int):
    x=b[a:a+n];y=b[c:c+n]
    same=sum(p==q for p,q in zip(x,y))
    return {'bytes':n,'identical_bytes':same,'ratio':same/n if n else 0.0}


def occurrences(b:bytes,word:int):
    q=struct.pack('<I',word);ans=[];p=0
    while True:
        p=b.find(q,p)
        if p<0:break
        ans.append(f'0x{p:06X}');p+=1
    return ans


def _u16(b:bytes,o:int)->int: return struct.unpack_from('<H',b,o)[0]
def _u32(b:bytes,o:int)->int: return struct.unpack_from('<I',b,o)[0]


def _thumb_ldr_literal_value(b:bytes, insn_off:int, insn:int):
    """Resolve Thumb-1 LDR Rd,[PC,#imm] against the GBA ROM image."""
    if (insn&0xF800)!=0x4800:return None
    imm=(insn&0xff)*4
    target=((insn_off+4)&~3)+imm
    if target+4>len(b):return None
    return _u32(b,target),target


def parse_generated_dispatcher(b:bytes, entry:int):
    """Recognize PSC's recurring generated selector -> continuation table shell.

    This is behavioral pattern recognition, not a byte signature exported to
    MD2GBA.  The useful contract is: signed selector modulo 1024, dense range
    reduction, then native continuation-table dispatch.
    """
    if not (GBA_BASE <= (entry&~1) < GBA_BASE+len(b)):return None
    off=(entry&~1)-GBA_BASE
    hs=[_u16(b,off+i*2) for i in range(min(56,(len(b)-off)//2))]
    # Common modulo-1024 lowering generated in PSII and PSIII: ASRS r0,#10;
    # LSLS r0,#10; SUBS R?,R?,r0.  Register allocation differs by module.
    mod_i=None
    for i in range(len(hs)-2):
        if hs[i]==0x1280 and hs[i+1]==0x0280 and (hs[i+2]&0xFE00)==0x1A00:
            mod_i=i;break
    if mod_i is None:return None
    # Find SUBS r0,#base followed closely by CMP r0,#max.
    base=limit=None;range_i=None
    for i in range(mod_i+3,min(mod_i+12,len(hs)-1)):
        if (hs[i]&0xFF00)==0x3800 and (hs[i+1]&0xFF00)==0x2800:
            base=hs[i]&0xff;limit=hs[i+1]&0xff;range_i=i;break
    if base is None:return None
    # Dense dispatch tail: LSLS r0,#2; LDR r1,[PC,#imm]; ADDS r0,r0,r1;
    # LDR r0,[r0]; MOV pc,r0.
    jt=None;j_i=None
    for i in range(range_i+1,min(range_i+14,len(hs)-4)):
        if hs[i]==0x0080 and (hs[i+1]&0xFF00)==0x4900 and hs[i+2]==0x1840 and hs[i+3]==0x6800 and hs[i+4]==0x4687:
            q=_thumb_ldr_literal_value(b,off+(i+1)*2,hs[i+1])
            if q:jt=q[0];j_i=i
            break
    if jt is None or not (GBA_BASE<=jt<GBA_BASE+len(b)):return None
    count=limit+1;to=jt-GBA_BASE
    if to+4*count>len(b):return None
    targets=[_u32(b,to+4*i) for i in range(count)]
    good=[x for x in targets if GBA_BASE <= (x&~1) < GBA_BASE+len(b)]
    if len(good)<max(1,count//2):return None
    return {
        'selector_modulus':1024,'selector_base':base,'selector_max_index':limit,
        'continuation_count':count,'jump_table':f'0x{jt:08X}',
        'valid_rom_targets':len(good),'unique_targets':len(set(good)),
        'target_min':f'0x{min(good):08X}' if good else None,
        'target_max':f'0x{max(good):08X}' if good else None,
        'shell_halfwords_through_jump':j_i+5,
    }


def dispatcher_inventory(b:bytes,mods:dict):
    out={};total=matched=0
    for game,mm in mods.items():
        rows=[]
        for m in mm:
            total+=1;d=parse_generated_dispatcher(b,m['entrypoints'][0])
            if d:matched+=1;rows.append({'index':m['index'],'name':m['name'],**d})
        out[game]=rows
    return out,{'first_entrypoints_total':total,'generated_dispatchers':matched,'ratio':matched/total if total else 0.0}


def selected_module_json(mods):
    picks={'ps3':('main','vdp','pack0','window','cg_chg','cgexpand'),
           'ps2':('main','int','io','scroll','window')}
    ans={}
    for g,names in picks.items():
        ans[g]=[]
        for m in mods[g]:
            if m['name'] in names:
                ans[g].append({'index':m['index'],'name':m['name'],
                    'entrypoints':[f'0x{x:08X}' for x in m['entrypoints']], 'flags':m['flags']})
    return ans


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('psc');ap.add_argument('--json-out');ap.add_argument('--md-out')
    ns=ap.parse_args();b=Path(ns.psc).read_bytes()
    if len(b)!=0x800000:raise SystemExit(f'expected 8 MiB PSC image, got {len(b)}')
    title=b[0xA0:0xAC].rstrip(b'\0').decode('ascii','replace');code=b[0xAC:0xB0].decode('ascii','replace')
    mods={k:modules(b,*v) for k,v in TABLES.items()}
    dispatch,dispatch_summary=dispatcher_inventory(b,mods)
    shared={k:raw_similarity(b,v['ps3'],v['ps2'],v['compare']) for k,v in HELPERS.items()}
    literals={
      'VDP_CONTROL_00C00004':occurrences(b,0x00C00004),'GBA_BG0HOFS_04000010':occurrences(b,0x04000010),
      'GBA_BG3HOFS_0400001C':occurrences(b,0x0400001C),'GBA_DMA0SAD_040000B0':occurrences(b,0x040000B0),
      'GBA_DMA1SAD_040000BC':occurrences(b,0x040000BC),'GBA_FIFO_A_040000A0':occurrences(b,0x040000A0),
    }
    findings=[
      {'id':'native_modules','behavior':'PSII/PSIII game logic is split into native Thumb modules rather than a conventional per-instruction 68000 interpreter.','md2gba_use':'translate discovered guest code into native regions and reserve generic dispatch for indirect/dynamic edges.'},
      {'id':'native_continuation_table','behavior':'most recognized module shells reduce a signed selector modulo 1024, subtract a module-local base, range-check it, then jump through a dense native continuation table.','md2gba_use':'compile each guest region to dense local continuation IDs; resolve direct edges natively and use a compact dispatcher only at region/dynamic boundaries.'},
      {'id':'explicit_continuation_state','behavior':'translated cases write continuation/callback pointers into GBA RAM state and resume through native module selectors.','md2gba_use':'make continuation state an explicit compiler IR concept instead of materializing guest PC after every instruction.'},
      {'id':'same_module_native_redispatch','behavior':'observed PSIII module epilogues mask the returned continuation token by its 1024-selector module prefix and, when it still belongs to the current module, call the local dense native selector dispatcher again rather than entering a 68000 interpreter.','md2gba_use':'when a hot superblock cannot continue as one block, resume through a generated exact native continuation in the same region; do not decode/interpret one guest instruction as the normal seam.'},
      {'id':'cross_module_native_return','behavior':'when a continuation token names another module, PSC writes cached native state registers back to shared GBA RAM and returns the token to the outer dispatcher.','md2gba_use':'treat region/module crossings as explicit compiler continuation exits with a compact state sync, while keeping direct intra-region edges native.'},
      {'id':'invalid_selector_native_default','behavior':'unpopulated selector slots observed in native modules route to a generated default epilogue that synchronizes state and returns a neutral token rather than interpreting arbitrary guest bytes.','md2gba_use':'generate explicit cold/default continuation cases for unresolved region-local selectors; reserve the generic 68k interpreter only for code the offline compiler truly did not translate.'},
      {'id':'shared_bus','behavior':'PSII and PSIII contain strongly similar Genesis-address bus helpers rather than title-specific GBA address access.','md2gba_use':'keep one generic 24-bit Genesis bus, but inline/direct ordinary ROM/WRAM paths.'},
      {'id':'address_nibble_dispatch','behavior':'shared byte helpers classify the 24-bit Genesis address space by high address region before handling I/O/RAM.','md2gba_use':'lower dynamic accesses through a shallow region classifier; statically known ROM/WRAM addresses bypass it entirely.'},
      {'id':'reverse_rom','behavior':'wide Genesis ROM values are stored/read through a descending byte layout so native little-endian loads yield big-endian guest values.','md2gba_use':'offline reversed-ROM representation for wide loads; source ROM remains untouched.'},
      {'id':'reverse_wram','behavior':'the shared compatibility core maps $FFxxxx work-RAM bytes in descending guest-address order, enabling native little-endian wide accesses without recurring swaps.','md2gba_use':'optional descending physical WRAM with guest-address accessor helpers; use it in both dynamic bus and constant-address AOT lowering.'},
      {'id':'vdp_word_path','behavior':'the duplicated shared helpers route VDP aperture traffic through the word-write path; byte-write helpers classify Cxxxxx separately and do not implement the VDP transaction path.','md2gba_use':'treat hardware accesses as optimization barriers unless an access-width-specific primitive is proven equivalent.'},
      {'id':'vdp_control_latch','behavior':'a pending two-word VDP command consumes its second word before fresh $8xxx register-write decoding.','md2gba_use':'generic VDP control state machine; regression-tested in P87.'},
      {'id':'vdp_data_reads','behavior':'VDP data-port reads select VRAM/VSRAM/CRAM by decoded command state and then apply register-15 autoincrement.','md2gba_use':'implement generic VDP data reads rather than status-only reads.'},
      {'id':'cram_shadow','behavior':'CRAM writes also update a GBA-side translated palette representation.','md2gba_use':'retain source CRAM plus a write-time GBA palette shadow to remove repeat conversion during presentation.'},
      {'id':'four_bg_scroll','behavior':'the GBA presentation layer mirrors each Genesis plane scroll across its high/low-priority GBA BG pair.','md2gba_use':'BG0/BG2 and BG1/BG3 must receive identical camera offsets.'},
      {'id':'scroll_modes','behavior':'PSC derives H-scroll state from VDP regs 11/13 and stages fixed/cell/raster values for native presentation.','md2gba_use':'decode legal Genesis H-scroll modes once and stage hardware-ready offset streams.'},
      {'id':'hblank_dma','behavior':'PSC stages scroll values in RAM and uses GBA HBlank DMA for rasterized BG offsets.','md2gba_use':'prepare HBlank streams away from the critical scanline path and let DMA feed BG offset registers.'},
      {'id':'pretranslated_video_shadow','behavior':'PSC maintains GBA-facing video state alongside Genesis-visible VDP state instead of re-deriving every presentation value each frame.','md2gba_use':'convert palettes/tile dependencies/scroll streams at write or dirty-span time and keep source VDP state authoritative.'},
      {'id':'fifo_dma','behavior':'PSC uses GBA sound FIFO DMA rather than high-frequency CPU FIFO shoveling.','md2gba_use':'retain DMA audio transport but repair producer/physical-clock synchronization before defaulting it.'},
    ]
    result={'sha256':hashlib.sha256(b).hexdigest(),'size':len(b),'title':title,'game_code':code,
      'module_counts':{k:len(v) for k,v in mods.items()},'selected_modules':selected_module_json(mods),
      'dispatcher_summary':dispatch_summary,'generated_dispatchers':dispatch,
      'shared_helper_raw_similarity':shared,'hardware_literal_offsets':literals,'findings':findings,
      'continuation_research_anchors':{
        'ps3_main':{'epilogue':'0x080849FC','local_dispatch':'0x08083D7A','module_prefix':'0x0800'},
        'ps3_vdp':{'default_epilogue':'0x08085EE6','epilogue':'0x08085F04','local_dispatch':'0x08084A76','module_prefix':'0x0C00'}},
      'note':'Research anchors are PSC-version-specific; MD2GBA runtime/codegen must never key behavior on these PSC offsets.'}
    if ns.json_out:Path(ns.json_out).write_text(json.dumps(result,indent=2)+'\n')
    if ns.md_out:
        lines=['# PSC generic behavior / Thumb architecture extraction','',f'- SHA-256: `{result["sha256"]}`',f'- Header: `{title}` / `{code}`',f'- Native module records: PSIII {len(mods["ps3"])}, PSII {len(mods["ps2"])}',f'- Generated first-entry dispatcher shells recognized: {dispatch_summary["generated_dispatchers"]}/{dispatch_summary["first_entrypoints_total"]} ({dispatch_summary["ratio"]*100:.1f}%)','', '## Shared helper similarity']
        for k,v in shared.items():lines.append(f'- {k}: {v["identical_bytes"]}/{v["bytes"]} raw bytes identical ({100*v["ratio"]:.1f}%) over the compared prefix.')
        lines+=['','## Generated native-dispatch structure']
        for g in ('ps3','ps2'):
            rows=dispatch[g]
            lines.append(f'- {g.upper()}: {len(rows)} recognized module dispatchers.')
            for r in rows[:8]:lines.append(f'  - `{r["name"]}`: selector base {r["selector_base"]}, {r["continuation_count"]} dense continuation slots, {r["unique_targets"]} unique ROM targets.')
            if len(rows)>8:lines.append(f'  - … {len(rows)-8} additional modules recorded in the JSON report.')
        lines+=['','The recurring shell is a compiler/code-generator fingerprint: normalize a signed selector modulo 1024, convert it to a module-local dense index, range-check it, then perform a native Thumb table jump. The cases themselves contain normal native state manipulation and calls into shared compatibility helpers. MD2GBA should reproduce the *IR/control-flow strategy*, not copy the PSC instructions.','', '## Reusable behaviors']
        for f in findings:lines.append(f'- **{f["id"]}** — {f["behavior"]} MD2GBA: {f["md2gba_use"]}')
        lines+=['','No PSC machine-code bytes are emitted by this report. The offsets used by the analyzer are research anchors only and are not part of the MD2GBA runtime contract.']
        Path(ns.md_out).write_text('\n'.join(lines)+'\n')
    print(json.dumps({'module_counts':result['module_counts'],'dispatcher_summary':dispatch_summary,'shared_helper_raw_similarity':shared,'findings':len(findings)},indent=2))
if __name__=='__main__':main()
