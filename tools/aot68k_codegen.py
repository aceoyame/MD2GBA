#!/usr/bin/env python3
"""Title-agnostic decoded 68000 -> C semantic emitter.

No game addresses, hooks, ROM hashes, or title knowledge belong here.
"""
from __future__ import annotations

EA_KIND={
'dn':'MD68K_EA_DN','an':'MD68K_EA_AN','an_ind':'MD68K_EA_AN_IND','an_postinc':'MD68K_EA_AN_POSTINC',
'an_predec':'MD68K_EA_AN_PREDEC','an_disp16':'MD68K_EA_AN_DISP16','an_index8':'MD68K_EA_AN_INDEX8',
'abs_w':'MD68K_EA_ABS_W','abs_l':'MD68K_EA_ABS_L','pc_disp16':'MD68K_EA_PC_DISP16','pc_index8':'MD68K_EA_PC_INDEX8','imm':'MD68K_EA_IMM'}
SZ={'b':'MD68K_SZ_B','w':'MD68K_SZ_W','l':'MD68K_SZ_L'}
SHIFT={'as':'MD68K_SHIFT_AS','ls':'MD68K_SHIFT_LS','rox':'MD68K_SHIFT_ROX','ro':'MD68K_SHIFT_RO'}

def eaargs(e):
    return f"{EA_KIND[e['kind']]},{e.get('reg') or 0},{(e.get('value') or 0)}u,{(e.get('disp') or 0)},{(e.get('index_ext') or 0)}u"

MASK={'b':'0xFFu','w':'0xFFFFu','l':'0xFFFFFFFFu'}
SIGN={'b':'0x80u','w':'0x8000u','l':'0x80000000u'}

def _absw(value:int)->int:
    a=value & 0xFFFF
    if a & 0x8000:
        a |= 0xFF0000
    return a & 0xFFFFFF

def _absw_arch(value:int)->int:
    a=value & 0xFFFF
    return a | 0xFFFF0000 if a & 0x8000 else a

def _mem_read(expr:str,sz:str)->str:
    fn={'b':'md2gba_read8','w':'md2gba_read16','l':'md2gba_read32'}[sz]
    return f"{fn}(b,({expr})&0xFFFFFFu)"

def _mem_write(expr:str,sz:str,v:str)->str:
    fn={'b':'md2gba_write8','w':'md2gba_write16','l':'md2gba_write32'}[sz]
    cast={'b':'(uint8_t)','w':'(uint16_t)','l':''}[sz]
    return f"{fn}(b,({expr})&0xFFFFFFu,{cast}({v}));"

def _direct_wram_read(addr:int,sz:str)->str|None:
    if addr < 0xFF0000 or addr > 0xFFFFFF:
        return None
    o=addr & 0xFFFF
    if sz=='b':
        return f"((uint32_t)md2gba_wram_read8_offset(b,0x{o:04X}u))"
    if sz=='w' and o<=0xFFFE:
        return f"((uint32_t)md2gba_wram_read16_offset(b,0x{o:04X}u))"
    if sz=='l' and o<=0xFFFC:
        return f"md2gba_wram_read32_offset(b,0x{o:04X}u)"
    return None

def _direct_wram_write(addr:int,sz:str,v:str)->str|None:
    if addr < 0xFF0000 or addr > 0xFFFFFF:
        return None
    o=addr & 0xFFFF
    if sz=='b':
        return f"md2gba_wram_write8_offset(b,0x{o:04X}u,(uint8_t)({v}));"
    if sz=='w' and o<=0xFFFE:
        return f"md2gba_wram_write16_offset(b,0x{o:04X}u,(uint16_t)({v}));"
    if sz=='l' and o<=0xFFFC:
        return f"md2gba_wram_write32_offset(b,0x{o:04X}u,(uint32_t)({v}));"
    return None

def rd(e,sz,pc):
    k=e['kind']; r=e.get('reg') or 0; v=e.get('value') or 0; d=e.get('disp') or 0
    if k=='dn': return f"(c->d[{r}]&{MASK[sz]})"
    if k=='an': return f"(c->a[{r}]&{MASK[sz]})"
    if k=='imm': return f"(0x{v & 0xFFFFFFFF:X}u&{MASK[sz]})"
    if k=='an_ind': return _mem_read(f"c->a[{r}]",sz)
    if k=='an_disp16': return _mem_read(f"c->a[{r}]+({d})",sz)
    if k=='abs_w':
        a=_absw(v); q=_direct_wram_read(a,sz)
        return q or _mem_read(f"0x{a:06X}u",sz)
    if k=='abs_l':
        a=v & 0xFFFFFF; q=_direct_wram_read(a,sz)
        return q or _mem_read(f"0x{a:06X}u",sz)
    if k=='pc_disp16':
        a=(pc+2+d)&0xFFFFFF; q=_direct_wram_read(a,sz)
        return q or _mem_read(f"0x{a:06X}u",sz)
    # Postincrement/predecrement and indexed modes retain the canonical helper;
    # they have register side effects or extension-word index semantics.
    return f"md68k_rd(c,b,{eaargs(e)},{SZ[sz]},0x{pc:06X}u)"

def wr(e,sz,v,pc):
    k=e['kind']; r=e.get('reg') or 0; value=e.get('value') or 0; d=e.get('disp') or 0
    if k=='dn':
        if sz=='b': return f"c->d[{r}]=(c->d[{r}]&0xFFFFFF00u)|(({v})&0xFFu);"
        if sz=='w': return f"c->d[{r}]=(c->d[{r}]&0xFFFF0000u)|(({v})&0xFFFFu);"
        return f"c->d[{r}]=({v});"
    if k=='an':
        if sz=='w': return f"c->a[{r}]=(uint32_t)(int32_t)(int16_t)(({v})&0xFFFFu);"
        return f"c->a[{r}]=({v});"
    if k=='an_ind': return _mem_write(f"c->a[{r}]",sz,v)
    if k=='an_disp16': return _mem_write(f"c->a[{r}]+({d})",sz,v)
    if k=='abs_w':
        a=_absw(value); q=_direct_wram_write(a,sz,v)
        return q or _mem_write(f"0x{a:06X}u",sz,v)
    if k=='abs_l':
        a=value & 0xFFFFFF; q=_direct_wram_write(a,sz,v)
        return q or _mem_write(f"0x{a:06X}u",sz,v)
    return f"md68k_wr(c,b,{eaargs(e)},{SZ[sz]},({v}),0x{pc:06X}u);"

def addr(e,sz,pc):
    k=e['kind']; r=e.get('reg') or 0; v=e.get('value') or 0; d=e.get('disp') or 0
    if k in ('an_ind','an_postinc'): return f"(c->a[{r}]&0xFFFFFFu)"
    if k=='an_disp16': return f"((c->a[{r}]+({d}))&0xFFFFFFu)"
    if k=='abs_w': return f"0x{_absw(v):06X}u"
    if k=='abs_l': return f"0x{v&0xFFFFFF:06X}u"
    if k=='pc_disp16': return f"0x{(pc+2+d)&0xFFFFFF:06X}u"
    return f"md68k_ea_addr(c,{eaargs(e)},{SZ[sz]},0x{pc:06X}u,0)"

def addr_arch(e,pc):
    """68000 architectural EA value for LEA, before 24-bit bus masking."""
    k=e['kind']; r=e.get('reg') or 0; v=e.get('value') or 0; d=e.get('disp') or 0
    if k in ('an_ind','an_postinc','an_predec'): return f"c->a[{r}]"
    if k=='an_disp16': return f"(c->a[{r}]+(uint32_t)({d}))"
    if k=='abs_w': return f"0x{_absw_arch(v):08X}u"
    if k=='abs_l': return f"0x{v&0xFFFFFFFF:08X}u"
    if k=='pc_disp16': return f"(0x{(pc+2)&0xFFFFFFFF:08X}u+(uint32_t)({d}))"
    return f"md68k_ea_arch_addr(c,{eaargs(e)},0x{pc:06X}u)"

def set_nz(sz:str,v:str)->str:
    mask=MASK[sz]; sign=SIGN[sz]
    return (f"t1=({v})&{mask}; c->sr=(uint16_t)(c->sr&~(MD68K_N|MD68K_Z|MD68K_V|MD68K_C)); "
            f"if(!t1)c->sr|=MD68K_Z; if(t1&{sign})c->sr|=MD68K_N;")

def cond_expr(cc:int)->str:
    C='(c->sr&MD68K_C)'; V='(c->sr&MD68K_V)'; Z='(c->sr&MD68K_Z)'; N='(c->sr&MD68K_N)'
    return {
      0:'1',1:'0',2:f'(!{C}&&!{Z})',3:f'({C}||{Z})',4:f'(!{C})',5:C,
      6:f'(!{Z})',7:Z,8:f'(!{V})',9:V,10:f'(!{N})',11:N,
      12:f'(!!{N}==!!{V})',13:f'(!!{N}!=!!{V})',14:f'(!{Z}&&(!!{N}==!!{V}))',15:f'({Z}||(!!{N}!=!!{V}))'
    }[cc & 15]

def sx_expr(v:str,bits:int)->str:
    if bits==8:return f"((int32_t)(int8_t)(({v})&0xFFu))"
    if bits==16:return f"((int32_t)(int16_t)(({v})&0xFFFFu))"
    return f"((int32_t)({v}))"



def arith_inline(sz:str,dst:str,src:str,sub:bool,setx:bool,result:str="t1")->str:
    """Emit width-specialized ADD/SUB/CMP CCR semantics with no size-dispatch call.

    ``dst`` and ``src`` must remain valid until the returned flag statements have
    executed.  The result is left in ``result``.  This is title-agnostic 68000
    lowering; it deliberately mirrors md68k_add/md68k_sub bit-for-bit.
    """
    mask=MASK[sz]; sign=SIGN[sz]
    d=f"(({dst})&{mask})"; q=f"(({src})&{mask})"
    op='-' if sub else '+'
    clear='(MD68K_N|MD68K_Z|MD68K_V|MD68K_C'+('|MD68K_X)' if setx else ')')
    parts=[f"{result}=({d}{op}{q})&{mask};",f"c->sr=(uint16_t)(c->sr&~{clear});",
           f"if(!{result})c->sr|=MD68K_Z;",f"if({result}&{sign})c->sr|=MD68K_N;"]
    if sub:
        parts += [f"if((({d}^{q})&({d}^{result})&{sign})!=0)c->sr|=MD68K_V;",
                  f"if({q}>{d})c->sr|=(uint16_t)(MD68K_C" + ("|MD68K_X" if setx else "") + ");"]
    else:
        parts += [f"if((~({d}^{q})&({d}^{result})&{sign})!=0)c->sr|=MD68K_V;",
                  f"if({q}>({mask}-{d}))c->sr|=(uint16_t)(MD68K_C" + ("|MD68K_X" if setx else "") + ");"]
    return ' '.join(parts)

def shift_imm_inline(i:dict)->str|None:
    """Closed-form immediate AS/LS/RO register shift.

    ROX is intentionally retained in the canonical helper because X participates
    in the rotate ring. Register-counted shifts also stay canonical. Immediate
    counts on a 68000 are 1..8, so all cases here are bounded by operand width.
    """
    if i.get('count_is_reg') or i.get('shift_kind')=='rox':
        return None
    sz=i['op_size']; kind=i['shift_kind']; left=i['direction']=='left'; n=int(i['count']); dr=i['dst_d']
    mask=int(MASK[sz][2:-1],16); sign=int(SIGN[sz][2:-1],16); bits={'b':8,'w':16,'l':32}[sz]
    assert 1 <= n <= 8 and n <= bits
    M=f"0x{mask:X}u"; S=f"0x{sign:X}u"
    parts=[f"t0=c->d[{dr}]&{M};"]
    if kind=='ls':
        if left:
            parts.append(f"t1=(t0<<{n}u)&{M};")
            carry=f"((t0>>{bits-n}u)&1u)"
        else:
            parts.append(f"t1=t0>>{n}u;")
            carry=f"((t0>>{n-1}u)&1u)"
        ov=None; clearx=True
    elif kind=='as':
        if left:
            parts.append(f"t1=(t0<<{n}u)&{M};")
            carry=f"((t0>>{bits-n}u)&1u)"
            if n==bits:
                ov="t0!=0u"
            else:
                topmask=((1<<(n+1))-1) << (bits-(n+1))
                ov=f"((t0&0x{topmask:X}u)!=0u && (t0&0x{topmask:X}u)!=0x{topmask:X}u)"
        else:
            fill=mask ^ (mask>>n)
            parts.append(f"t1=(t0>>{n}u)|((t0&{S})?0x{fill:X}u:0u);")
            carry=f"((t0>>{n-1}u)&1u)"; ov=None
        clearx=True
    elif kind=='ro':
        if n==bits:
            parts.append("t1=t0;")
        elif left:
            parts.append(f"t1=((t0<<{n}u)|(t0>>{bits-n}u))&{M};")
        else:
            parts.append(f"t1=((t0>>{n}u)|(t0<<{bits-n}u))&{M};")
        carry=(f"((t0>>{bits-n}u)&1u)" if left else f"((t0>>{n-1}u)&1u)")
        ov=None; clearx=False
    else:
        return None
    clr='(MD68K_N|MD68K_Z|MD68K_V|MD68K_C'+('|MD68K_X)' if clearx else ')')
    parts += [f"c->sr=(uint16_t)(c->sr&~{clr});",
              "if(!t1)c->sr|=MD68K_Z;",f"if(t1&{S})c->sr|=MD68K_N;"]
    if ov: parts.append(f"if({ov})c->sr|=MD68K_V;")
    parts.append(f"if({carry})c->sr|=MD68K_C;")
    if clearx: parts.append(f"if({carry})c->sr|=MD68K_X;")
    keep={'b':'0xFFFFFF00u','w':'0xFFFF0000u','l':'0u'}[sz]
    if sz=='l': parts.append(f"c->d[{dr}]=t1;")
    else: parts.append(f"c->d[{dr}]=(c->d[{dr}]&{keep})|t1;")
    return ' '.join(parts)

def emit_sem(i, set_pc=True):
    """Emit one decoded 68000 instruction.

    ``set_pc=False`` is used only by the deadline-admitted fast-superblock
    emitter.  In that context control cannot be observed between straight-line
    instructions, so the virtual PC can stay in an ARM local and be materialized
    once at the block boundary.  All ordinary/oracle emitters keep the historical
    per-instruction PC store by default.
    """
    m=i['mnemonic']; pc=i['address']; nxt=(pc+i['size'])&0xffffff; L=[]
    if set_pc: L.append(f"c->pc=0x{nxt:06X}u;")
    if m=='nop': pass
    elif m=='moveq':
        r=i['dst_d']; L += [f"c->d[{r}]=0x{i['imm']&0xffffffff:08X}u;",set_nz('l',f"c->d[{r}]")]
    elif m=='move':
        s=i['op_size']; L += [f"t0={rd(i['src'],s,pc)};",wr(i['dst'],s,'t0',pc),set_nz(s,'t0')]
    elif m=='movea':
        s=i['op_size']; r=i['dst_a']; L += [f"t0={rd(i['src'],s,pc)};",f"c->a[{r}]={(f'(uint32_t)(int32_t)(int16_t)(t0&0xFFFFu)' if s=='w' else 't0')};"]
    elif m=='lea': L.append(f"c->a[{i['dst_a']}]={addr_arch(i['ea'],pc)};")
    elif m in ('andi','ori','eori'):
        s=i['op_size']; op={'andi':'&','ori':'|','eori':'^'}[m]; L += [f"t0={rd(i['ea'],s,pc)};",f"t1=t0 {op} 0x{i['imm']&0xffffffff:X}u;",wr(i['ea'],s,'t1',pc),set_nz(s,'t1')]
    elif m in ('addi','subi'):
        s=i['op_size']; L += [f"t0={rd(i['ea'],s,pc)};",arith_inline(s,'t0',f"0x{i['imm']&0xffffffff:X}u",m=='subi',True),wr(i['ea'],s,'t1',pc)]
    elif m=='cmpi':
        s=i['op_size'];L += [f"t0={rd(i['ea'],s,pc)};",arith_inline(s,'t0',f"0x{i['imm']&0xffffffff:X}u",True,False)]
    elif m in ('btst_imm','btst_dn'):
        e=i['ea']; isreg=e['kind']=='dn'; s='l' if isreg else 'b'; bitexpr=(str(i['bit'])+'u') if m=='btst_imm' else f"c->d[{i['src_d']}]"; mod=32 if isreg else 8
        L += [f"t0={rd(e,s,pc)};",f"t1=({bitexpr})&{mod-1}u;","c->sr=(uint16_t)((c->sr&~MD68K_Z)|(((t0>>t1)&1u)?0u:MD68K_Z));"]
    elif m in ('bset_imm','bclr_imm','bchg_imm','bset_dn','bclr_dn','bchg_dn'):
        e=i['ea']; isreg=e['kind']=='dn'; s='l' if isreg else 'b'; mod=32 if isreg else 8
        bitexpr=(str(i['bit'])+'u') if m.endswith('_imm') else f"c->d[{i['src_d']}]"
        L += [f"t0={rd(e,s,pc)};",f"t1=({bitexpr})&{mod-1}u;","c->sr=(uint16_t)((c->sr&~MD68K_Z)|(((t0>>t1)&1u)?0u:MD68K_Z));"]
        if m.startswith('bset'): L += ["t0|=(1u<<t1);"]
        elif m.startswith('bclr'): L += ["t0&=~(1u<<t1);"]
        else: L += ["t0^=(1u<<t1);"]
        L.append(wr(e,s,'t0',pc))
    elif m=='bcc': L.append(f"if({cond_expr(i['condition'])}) c->pc=0x{i['target']:06X}u;")
    elif m=='bra': L.append(f"c->pc=0x{i['target']:06X}u;")
    elif m in ('bsr','jsr'):
        L += ["c->a[7]-=4u;",f"md2gba_write32(b,c->a[7],0x{nxt:06X}u);"]
        if i.get('target') is not None: L.append(f"c->pc=0x{i['target']:06X}u;")
        else: L.append(f"c->pc={addr(i['ea'],'l',pc)};")
    elif m=='trap':
        L += ["c->a[7]-=4u;",f"md2gba_write32(b,c->a[7],0x{nxt:06X}u);","c->a[7]-=2u; md2gba_write16(b,c->a[7],c->sr);","c->sr|=0x2000u;",f"c->pc=0x{i['target']:06X}u;"]
    elif m=='jmp':
        if i.get('target') is not None:L.append(f"c->pc=0x{i['target']:06X}u;")
        else:L.append(f"c->pc={addr(i['ea'],'l',pc)};")
    elif m=='rts': L += ["t0=md2gba_read32(b,c->a[7]);","c->a[7]+=4u; c->pc=t0;"]
    elif m=='rte': L += ["t0=md2gba_read16(b,c->a[7]); c->a[7]+=2u; c->sr=(uint16_t)t0;","t0=md2gba_read32(b,c->a[7]); c->a[7]+=4u; c->pc=t0;"]
    elif m=='dbcc':
        r=i['data_reg']; L.append(f"if(!({cond_expr(i['condition'])})){{ t0=(c->d[{r}]-1u)&0xFFFFu; c->d[{r}]=(c->d[{r}]&0xFFFF0000u)|t0; if(t0!=0xFFFFu)c->pc=0x{i['target']:06X}u; }}")
    elif m in ('addq','subq'):
        e=i['ea'];s=i['op_size'];q=i['quick'];sg='+' if m=='addq' else '-'
        if e['kind']=='an':L.append(f"c->a[{e['reg']}]=c->a[{e['reg']}] {sg} {q}u;")
        else:
            L += [f"t0={rd(e,s,pc)};",arith_inline(s,'t0',f"{q}u",m=='subq',True),wr(e,s,'t1',pc)]
    elif m=='scc':L.append(wr(i['ea'],'b',f"(({cond_expr(i['condition'])})?0xFFu:0u)",pc))
    elif m=='swap':
        r=i['data_reg']; L += [f"t0=c->d[{r}];",f"c->d[{r}]=(t0<<16)|(t0>>16);",set_nz('l',f"c->d[{r}]")]
    elif m=='ext':
        r=i['data_reg'];
        if i['op_size']=='w':L += [f"t0=(uint32_t){sx_expr(f'c->d[{r}]',8)}&0xFFFFu;",f"c->d[{r}]=(c->d[{r}]&0xFFFF0000u)|t0;",set_nz('w','t0')]
        else:L += [f"c->d[{r}]=(uint32_t){sx_expr(f'c->d[{r}]',16)};",set_nz('l',f"c->d[{r}]")]
    elif m=='shift_reg':
        q=shift_imm_inline(i); L.append(q if q is not None else f"md68k_shift(c,{SZ[i['op_size']]},{i['dst_d']},{SHIFT[i['shift_kind']]},{1 if i['direction']=='left' else 0},{1 if i['count_is_reg'] else 0},{i['count_reg'] if i['count_is_reg'] else i['count']});")
    elif m in ('or_to_dn','and_to_dn'):
        s=i['op_size'];r=i['dst_d'];op='|' if m=='or_to_dn' else '&'; L += [f"t0={rd(i['src'],s,pc)};",f"t1=(c->d[{r}]&{MASK[s]}){op}t0;",wr({'kind':'dn','reg':r},s,'t1',pc),set_nz(s,'t1')]
    elif m=='eor_dn_to_ea':
        s=i['op_size'];r=i['src_d'];L += [f"t0={rd(i['dst'],s,pc)};",f"t1=t0^(c->d[{r}]&{MASK[s]});",wr(i['dst'],s,'t1',pc),set_nz(s,'t1')]
    elif m in ('mulu','muls'):
        r=i['dst_d'];L.append(f"t0={rd(i['src'],'w',pc)}&0xFFFFu;")
        if m=='mulu':L.append(f"t1=(c->d[{r}]&0xFFFFu)*t0;")
        else:L.append(f"t1=(uint32_t)({sx_expr(f'c->d[{r}]',16)}*{sx_expr('t0',16)});")
        L += [f"c->d[{r}]=t1;",set_nz('l','t1')]
    elif m in ('divu','divs'):
        r=i['dst_d'];L.append(f"t0={rd(i['src'],'w',pc)}&0xFFFFu;")
        L.append(f"c->d[{r}]={'md68k_divu' if m=='divu' else 'md68k_divs'}(c,b,c->d[{r}],t0,0x{pc:06X}u);")
    elif m in ('add_to_dn','sub_from_dn'):
        s=i['op_size'];r=i['dst_d'];L += [f"t0={rd(i['src'],s,pc)};",arith_inline(s,f"c->d[{r}]",'t0',m=='sub_from_dn',True),wr({'kind':'dn','reg':r},s,'t1',pc)]
    elif m in ('add_dn_to_ea','sub_dn_to_ea'):
        s=i['op_size'];r=i['src_d'];L += [f"t0={rd(i['dst'],s,pc)};",arith_inline(s,'t0',f"c->d[{r}]",m=='sub_dn_to_ea',True),wr(i['dst'],s,'t1',pc)]
    elif m in ('or_dn_to_ea','and_dn_to_ea'):
        s=i['op_size'];r=i['src_d'];op='|' if m=='or_dn_to_ea' else '&';L += [f"t0={rd(i['dst'],s,pc)};",f"t1=t0 {op} (c->d[{r}]&{MASK[s]});",wr(i['dst'],s,'t1',pc),set_nz(s,'t1')]
    elif m in ('adda','suba'):
        s=i['op_size'];r=i['dst_a'];L += [f"t0={rd(i['src'],s,pc)};",f"t1={(f'(uint32_t)(int32_t)(int16_t)(t0&0xFFFFu)' if s=='w' else 't0')};",f"c->a[{r}]=c->a[{r}] {'+' if m=='adda' else '-'} t1;"]
    elif m=='cmp':
        s=i['op_size'];L += [f"t0={rd(i['src'],s,pc)};",arith_inline(s,f"c->d[{i['dst_d']}]",'t0',True,False)]
    elif m=='cmpa':
        s=i['op_size'];L += [f"t0={rd(i['src'],s,pc)};",f"t0={(f'(uint32_t)(int32_t)(int16_t)(t0&0xFFFFu)' if s=='w' else 't0')};",arith_inline('l',f"c->a[{i['dst_a']}]",'t0',True,False)]
    elif m=='clr':
        s=i['op_size'];L += [wr(i['ea'],s,'0u',pc),"c->sr=(uint16_t)((c->sr&~(MD68K_N|MD68K_V|MD68K_C))|MD68K_Z);"]
    elif m=='tst':
        s=i['op_size'];L += [f"t0={rd(i['ea'],s,pc)};",set_nz(s,'t0')]
    elif m=='neg':
        s=i['op_size'];L += [f"t0={rd(i['ea'],s,pc)};",arith_inline(s,'0u','t0',True,True),wr(i['ea'],s,'t1',pc)]
    elif m=='not':
        s=i['op_size']; L += [f"t0={rd(i['ea'],s,pc)};",f"t1=(~t0)&{MASK[s]};",wr(i['ea'],s,'t1',pc),set_nz(s,'t1')]
    elif m=='exg':
        x=i['reg_x'];y=i['reg_y'];k=i['kind']
        if k=='d_d': L += [f"t0=c->d[{x}];",f"c->d[{x}]=c->d[{y}];",f"c->d[{y}]=t0;"]
        elif k=='a_a': L += [f"t0=c->a[{x}];",f"c->a[{x}]=c->a[{y}];",f"c->a[{y}]=t0;"]
        else: L += [f"t0=c->d[{x}];",f"c->d[{x}]=c->a[{y}];",f"c->a[{y}]=t0;"]
    elif m=='cmpm':
        s=i['op_size'];sa=i['src_a'];da=i['dst_a'];
        L += [f"t0=md68k_rd(c,b,MD68K_EA_AN_POSTINC,{sa},0u,0,0u,{SZ[s]},0x{pc:06X}u);",f"t1=md68k_rd(c,b,MD68K_EA_AN_POSTINC,{da},0u,0,0u,{SZ[s]},0x{pc:06X}u);",f"(void)md68k_sub(c,{SZ[s]},t1,t0,0);"]
    elif m=='movep':
        a=i['addr_reg'];d=i['data_reg'];disp=i['disp'];base=f"((c->a[{a}]+({disp}))&0xFFFFFFu)"
        if i['direction']=='mem_to_reg':
            if i['op_size']=='w': L += [f"t0=((uint32_t)md2gba_read8(b,{base})<<8)|md2gba_read8(b,({base}+2u)&0xFFFFFFu);",f"c->d[{d}]=(c->d[{d}]&0xFFFF0000u)|(t0&0xFFFFu);"]
            else: L += [f"t0=((uint32_t)md2gba_read8(b,{base})<<24)|((uint32_t)md2gba_read8(b,({base}+2u)&0xFFFFFFu)<<16)|((uint32_t)md2gba_read8(b,({base}+4u)&0xFFFFFFu)<<8)|md2gba_read8(b,({base}+6u)&0xFFFFFFu);",f"c->d[{d}]=t0;"]
        else:
            if i['op_size']=='w': L += [f"md2gba_write8(b,{base},(uint8_t)(c->d[{d}]>>8));",f"md2gba_write8(b,({base}+2u)&0xFFFFFFu,(uint8_t)c->d[{d}]);"]
            else: L += [f"md2gba_write8(b,{base},(uint8_t)(c->d[{d}]>>24));",f"md2gba_write8(b,({base}+2u)&0xFFFFFFu,(uint8_t)(c->d[{d}]>>16));",f"md2gba_write8(b,({base}+4u)&0xFFFFFFu,(uint8_t)(c->d[{d}]>>8));",f"md2gba_write8(b,({base}+6u)&0xFFFFFFu,(uint8_t)c->d[{d}]);"]
    elif m.endswith('_mem') and m in ('asr_mem','asl_mem','lsr_mem','lsl_mem','roxr_mem','roxl_mem','ror_mem','rol_mem'):
        kinds={'asr_mem':('MD68K_SHIFT_AS',0),'asl_mem':('MD68K_SHIFT_AS',1),'lsr_mem':('MD68K_SHIFT_LS',0),'lsl_mem':('MD68K_SHIFT_LS',1),'roxr_mem':('MD68K_SHIFT_ROX',0),'roxl_mem':('MD68K_SHIFT_ROX',1),'ror_mem':('MD68K_SHIFT_RO',0),'rol_mem':('MD68K_SHIFT_RO',1)}
        kind,left=kinds[m];L += [f"t0={rd(i['ea'],'w',pc)};",f"t1=md68k_shift_value(c,MD68K_SZ_W,t0,{kind},{left},1u);",wr(i['ea'],'w','t1',pc)]
    elif m=='movem_from_regs':
        s=i['op_size'];e=i['ea'];L.append(f"md68k_movem_store(c,b,0x{i['mask']:04X}u,{SZ[s]},{eaargs(e)},0x{pc:06X}u);")
    elif m=='movem_to_regs':
        s=i['op_size'];e=i['ea'];L.append(f"md68k_movem_load(c,b,0x{i['mask']:04X}u,{SZ[s]},{eaargs(e)},0x{pc:06X}u);")
    elif m=='move_to_sr': L += [f"t0={rd(i['src'],'w',pc)};","c->sr=(uint16_t)t0;"]
    elif m=='move_to_sr_imm': L.append(f"c->sr=0x{i['imm']&0xffff:04X}u;")
    elif m in ('ori_to_ccr','andi_to_ccr','eori_to_ccr'):
        op={'ori_to_ccr':'|','andi_to_ccr':'&','eori_to_ccr':'^'}[m];L.append(f"c->sr=(uint16_t)((c->sr&0xFF00u)|(((c->sr&0xFFu){op}0x{i['imm']&0xff:02X}u)&0xFFu));")
    elif m in ('ori_to_sr','andi_to_sr','eori_to_sr'):
        op={'ori_to_sr':'|=','andi_to_sr':'&=','eori_to_sr':'^='}[m];L.append(f"c->sr {op} 0x{i['imm']&0xffff:04X}u;")
    elif m=='move_from_sr':L.append(wr(i['dst'],'w','c->sr',pc))
    elif m=='move_to_ccr':L += [f"t0={rd(i['src'],'w',pc)};","c->sr=(uint16_t)((c->sr&0xFF00u)|(t0&0xFFu));"]
    elif m=='move_an_to_usp':L.append(f"c->usp=c->a[{i['src_a']}];")
    elif m=='move_usp_to_an':L.append(f"c->a[{i['dst_a']}]=c->usp;")
    else: raise RuntimeError(f"AOT emitter missing {m} at {pc:06X}: {i}")
    return ' '.join(L)

