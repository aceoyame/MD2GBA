#!/usr/bin/env python3
"""Growing 68000 binary decoder used by md2gba.

Phase 3 focuses on code discovery rather than pretending to be a complete ISA
implementation.  The decoder is deliberately strict: it knows enough EA forms
and instruction families to walk the verified PSII reset/bootstrap and the
standard Sega startup templates. Unknown opcodes are
reported instead of guessed.
"""
from __future__ import annotations
from dataclasses import dataclass, asdict
from typing import Any


def s8(x:int)->int: return x-0x100 if x & 0x80 else x
def s16(x:int)->int: return x-0x10000 if x & 0x8000 else x
def w(b:bytes,o:int)->int:
    if o+2>len(b): raise ValueError(f"read past ROM at 0x{o:X}")
    return int.from_bytes(b[o:o+2], 'big')
def l(b:bytes,o:int)->int:
    if o+4>len(b): raise ValueError(f"read past ROM at 0x{o:X}")
    return int.from_bytes(b[o:o+4], 'big')

SIZE_BYTES={'b':1,'w':2,'l':4}

@dataclass
class EA:
    mode:int
    reg:int
    kind:str
    ext_bytes:int=0
    value:int|None=None
    disp:int|None=None
    index_ext:int|None=None
    def dict(self): return asdict(self)


def decode_ea(b:bytes, ext_pc:int, mode:int, reg:int, size:str)->EA:
    if mode==0: return EA(mode,reg,'dn')
    if mode==1: return EA(mode,reg,'an')
    if mode==2: return EA(mode,reg,'an_ind')
    if mode==3: return EA(mode,reg,'an_postinc')
    if mode==4: return EA(mode,reg,'an_predec')
    if mode==5: return EA(mode,reg,'an_disp16',2,disp=s16(w(b,ext_pc)))
    if mode==6: return EA(mode,reg,'an_index8',2,index_ext=w(b,ext_pc),disp=s8(w(b,ext_pc)&0xff))
    if mode==7:
        if reg==0: return EA(mode,reg,'abs_w',2,value=w(b,ext_pc))
        if reg==1: return EA(mode,reg,'abs_l',4,value=l(b,ext_pc))
        if reg==2: return EA(mode,reg,'pc_disp16',2,disp=s16(w(b,ext_pc)))
        if reg==3: return EA(mode,reg,'pc_index8',2,index_ext=w(b,ext_pc),disp=s8(w(b,ext_pc)&0xff))
        if reg==4:
            n=4 if size=='l' else 2
            val=l(b,ext_pc) if n==4 else w(b,ext_pc)
            if size=='b': val &= 0xff
            return EA(mode,reg,'imm',n,value=val)
    raise ValueError(f'unsupported EA mode={mode} reg={reg}')


def _move_size(op:int)->str|None:
    # MOVE occupies only top nibbles 1/2/3; masking with &3 would falsely
    # classify arithmetic opcodes such as Dxxx/Bxxx/Exxx as MOVE.
    return {1:'b',2:'l',3:'w'}.get((op>>12)&0xF)


def decode(b:bytes, pc:int)->dict[str,Any]:
    op=w(b,pc)
    x={'address':pc,'opcode':op}

    # Fixed/control instructions.
    if op==0x4E71: return x|{'mnemonic':'nop','size':2,'flow':'fallthrough'}
    if op==0x4E75: return x|{'mnemonic':'rts','size':2,'flow':'return'}
    if op==0x4E73: return x|{'mnemonic':'rte','size':2,'flow':'return'}
    if op==0x46FC: return x|{'mnemonic':'move_to_sr_imm','imm':w(b,pc+2),'size':4,'flow':'fallthrough'}
    # Immediate logic to SR/CCR (privileged variants are common in startup code).
    if op in (0x007C,0x027C,0x0A7C):
        return x|{'mnemonic':{0x007C:'ori_to_sr',0x027C:'andi_to_sr',0x0A7C:'eori_to_sr'}[op],
                  'imm':w(b,pc+2),'size':4,'flow':'fallthrough'}
    if op in (0x003C,0x023C,0x0A3C):
        return x|{'mnemonic':{0x003C:'ori_to_ccr',0x023C:'andi_to_ccr',0x0A3C:'eori_to_ccr'}[op],
                  'imm':w(b,pc+2)&0xff,'size':4,'flow':'fallthrough'}
    # MOVE SR,<ea>.
    if (op & 0xFFC0)==0x40C0:
        mode=(op>>3)&7; reg=op&7; ea=decode_ea(b,pc+2,mode,reg,'w')
        return x|{'mnemonic':'move_from_sr','op_size':'w','dst':ea.dict(),'size':2+ea.ext_bytes,'flow':'fallthrough'}
    # MOVE <ea>,SR. 0x46FC (immediate) was handled above.
    if (op & 0xFFC0)==0x46C0:
        mode=(op>>3)&7; reg=op&7; ea=decode_ea(b,pc+2,mode,reg,'w')
        return x|{'mnemonic':'move_to_sr','op_size':'w','src':ea.dict(),'size':2+ea.ext_bytes,'flow':'fallthrough'}
    # MOVE <ea>,CCR.
    if (op & 0xFFC0)==0x44C0:
        mode=(op>>3)&7; reg=op&7; ea=decode_ea(b,pc+2,mode,reg,'w')
        return x|{'mnemonic':'move_to_ccr','op_size':'w','src':ea.dict(),'size':2+ea.ext_bytes,'flow':'fallthrough'}
    if 0x4E60 <= op <= 0x4E67:
        return x|{'mnemonic':'move_an_to_usp','src_a':op&7,'size':2,'flow':'fallthrough'}
    if 0x4E68 <= op <= 0x4E6F:
        return x|{'mnemonic':'move_usp_to_an','dst_a':op&7,'size':2,'flow':'fallthrough'}

    # BRA/BSR/Bcc.
    if (op & 0xF000)==0x6000:
        cond=(op>>8)&0xf; d8=op&0xff
        if d8==0:
            disp=s16(w(b,pc+2)); sz=4
        elif d8==0xff: # 68000 treats FF as -1 byte displacement, not 68020 long form.
            disp=-1; sz=2
        else:
            disp=s8(d8); sz=2
        target=(pc+2+disp)&0xffffff
        mn={0:'bra',1:'bsr'}.get(cond,'bcc')
        flow='jump' if cond==0 else ('call' if cond==1 else 'cond')
        return x|{'mnemonic':mn,'condition':cond,'displacement':disp,'target':target,'size':sz,'flow':flow}

    # DBcc.
    if (op & 0xF0F8)==0x50C8:
        disp=s16(w(b,pc+2)); cond=(op>>8)&0xf
        return x|{'mnemonic':'dbcc','condition':cond,'data_reg':op&7,'displacement':disp,
                  'target':(pc+2+disp)&0xffffff,'size':4,'flow':'cond'}

    # MOVEQ.
    if (op & 0xF100)==0x7000:
        return x|{'mnemonic':'moveq','dst_d':(op>>9)&7,'imm':s8(op&0xff),'size':2,'flow':'fallthrough'}

    # JSR/JMP generic EA.
    if (op & 0xFFC0) in (0x4E80,0x4EC0):
        mode=(op>>3)&7; reg=op&7
        ea=decode_ea(b,pc+2,mode,reg,'l')
        sz=2+ea.ext_bytes
        target=None
        if ea.kind=='abs_l': target=ea.value&0xffffff
        elif ea.kind=='abs_w': target=((ea.value|0xff0000) if ea.value & 0x8000 else ea.value)&0xffffff
        elif ea.kind=='pc_disp16': target=(pc+2+ea.disp)&0xffffff
        mn='jsr' if (op&0xFFC0)==0x4E80 else 'jmp'
        flow='call' if mn=='jsr' else 'jump'
        return x|{'mnemonic':mn,'ea':ea.dict(),'target':target,'size':sz,'flow':flow,
                  'computed_target':target is None}

    # LEA.
    if (op & 0xF1C0)==0x41C0:
        mode=(op>>3)&7; reg=op&7; ea=decode_ea(b,pc+2,mode,reg,'l')
        return x|{'mnemonic':'lea','dst_a':(op>>9)&7,'ea':ea.dict(),'size':2+ea.ext_bytes,'flow':'fallthrough'}

    # EXT.W/EXT.L Dn. These overlap the broad MOVEM opcode family and must
    # be recognized first (0x4880..0x4887 / 0x48C0..0x48C7).
    if (op & 0xFFF8)==0x4880:
        return x|{'mnemonic':'ext','from_size':'b','op_size':'w','data_reg':op&7,'size':2,'flow':'fallthrough'}
    if (op & 0xFFF8)==0x48C0:
        return x|{'mnemonic':'ext','from_size':'w','op_size':'l','data_reg':op&7,'size':2,'flow':'fallthrough'}

    # MOVEM memory -> registers.  Enough for Sega startup templates.
    if (op & 0xFF80)==0x4C80:
        sizech='l' if (op&0x40) else 'w'
        mask=w(b,pc+2); mode=(op>>3)&7; reg=op&7
        ea=decode_ea(b,pc+4,mode,reg,sizech)
        return x|{'mnemonic':'movem_to_regs','op_size':sizech,'mask':mask,'ea':ea.dict(),
                  'size':4+ea.ext_bytes,'flow':'fallthrough'}
    # MOVEM registers -> memory.
    if (op & 0xFF80)==0x4880:
        sizech='l' if (op&0x40) else 'w'
        mask=w(b,pc+2); mode=(op>>3)&7; reg=op&7
        ea=decode_ea(b,pc+4,mode,reg,sizech)
        return x|{'mnemonic':'movem_from_regs','op_size':sizech,'mask':mask,'ea':ea.dict(),
                  'size':4+ea.ext_bytes,'flow':'fallthrough'}

    # Immediate logic/arithmetic to EA. CCR/SR special encodings are intentionally excluded.
    imm_family={0x0000:'ori',0x0200:'andi',0x0400:'subi',0x0600:'addi',0x0A00:'eori',0x0C00:'cmpi'}
    fam=op & 0xFF00
    if fam in imm_family and op not in (0x003C,0x007C,0x023C,0x027C,0x0A3C,0x0A7C):
        szcode=(op>>6)&3
        if szcode in (0,1,2):
            sizech={0:'b',1:'w',2:'l'}[szcode]
            imm_bytes=4 if sizech=='l' else 2
            imm=l(b,pc+2) if sizech=='l' else w(b,pc+2)
            if sizech=='b': imm &= 0xff
            mode=(op>>3)&7; reg=op&7
            ea=decode_ea(b,pc+2+imm_bytes,mode,reg,sizech)
            return x|{'mnemonic':imm_family[fam],'op_size':sizech,'imm':imm,'ea':ea.dict(),
                      'size':2+imm_bytes+ea.ext_bytes,'flow':'fallthrough'}

    # Static/dynamic bit test/modify.
    static_bit={0x0800:'btst_imm',0x0840:'bchg_imm',0x0880:'bclr_imm',0x08C0:'bset_imm'}
    sb=op&0xFFC0
    if sb in static_bit:
        bit=w(b,pc+2)&0xff; mode=(op>>3)&7; reg=op&7
        ea=decode_ea(b,pc+4,mode,reg,'b')
        return x|{'mnemonic':static_bit[sb],'bit':bit,'ea':ea.dict(),'size':4+ea.ext_bytes,'flow':'fallthrough'}
    # MOVEP is encoded inside the dynamic-bit opcode space when EA mode == 001.
    # It transfers bytes at two-byte spacing and is used by SRAM/peripheral code.
    if (op & 0xF138)==0x0108:
        dn=(op>>9)&7; an=op&7; opmode=(op>>6)&3; disp=s16(w(b,pc+2))
        return x|{'mnemonic':'movep','direction':'mem_to_reg' if opmode in (0,1) else 'reg_to_mem',
                  'op_size':'l' if opmode in (1,3) else 'w','data_reg':dn,'addr_reg':an,'disp':disp,
                  'size':4,'flow':'fallthrough'}
    dyn_bit={0x0100:'btst_dn',0x0140:'bchg_dn',0x0180:'bclr_dn',0x01C0:'bset_dn'}
    db=op & 0xF1C0
    if db in dyn_bit:
        mode=(op>>3)&7; reg=op&7; ea=decode_ea(b,pc+2,mode,reg,'b')
        return x|{'mnemonic':dyn_bit[db],'src_d':(op>>9)&7,'ea':ea.dict(),'size':2+ea.ext_bytes,'flow':'fallthrough'}

    # TRAP #n: direct vector target is available from the ROM vector table.
    if (op & 0xFFF0)==0x4E40:
        n=op&0xF; vec=(32+n)*4; target=l(b,vec)&0xFFFFFF if vec+4<=len(b) else None
        return x|{'mnemonic':'trap','trap':n,'target':target,'size':2,'flow':'call','computed_target':target is None}

    # ADDQ/SUBQ. Exclude size-code 3, which is the Scc/DBcc family handled above.
    if (op & 0xF000)==0x5000 and ((op>>6)&3)!=3:
        sizech={0:'b',1:'w',2:'l'}[(op>>6)&3]; mode=(op>>3)&7; reg=op&7
        ea=decode_ea(b,pc+2,mode,reg,sizech); q=(op>>9)&7; q=8 if q==0 else q
        return x|{'mnemonic':'subq' if (op&0x0100) else 'addq','op_size':sizech,'quick':q,'ea':ea.dict(),
                  'size':2+ea.ext_bytes,'flow':'fallthrough'}

    # Scc <ea>. DBcc (mode=1) was handled earlier.
    if (op & 0xF0C0)==0x50C0 and ((op>>3)&7)!=1:
        mode=(op>>3)&7; reg=op&7; ea=decode_ea(b,pc+2,mode,reg,'b')
        return x|{'mnemonic':'scc','condition':(op>>8)&0xF,'ea':ea.dict(),'size':2+ea.ext_bytes,'flow':'fallthrough'}

    # SWAP Dn.
    if (op & 0xFFF8)==0x4840:
        return x|{'mnemonic':'swap','data_reg':op&7,'size':2,'flow':'fallthrough'}

    # Register shifts/rotates. Decode the exact operation because static
    # code generation needs to distinguish AS/LS/ROX/RO and immediate vs Dn counts.
    # Memory shifts use size-code 3 and are handled below.
    if (op & 0xF000)==0xE000 and ((op>>6)&3)!=3:
        sizech={0:'b',1:'w',2:'l'}[(op>>6)&3]
        direction='left' if (op&0x0100) else 'right'
        shift_kind=('as','ls','rox','ro')[(op>>3)&3]
        count_is_reg=bool(op&0x0020)
        cr=(op>>9)&7
        return x|{'mnemonic':'shift_reg','op_size':sizech,'shift_kind':shift_kind,'direction':direction,
                  'count_is_reg':count_is_reg,'count_reg':cr if count_is_reg else None,
                  'count':None if count_is_reg else (8 if cr==0 else cr),'dst_d':op&7,
                  'size':2,'flow':'fallthrough'}

    # Memory shifts/rotates (always one word in memory, one-bit count).
    if (op & 0xF000)==0xE000 and ((op>>6)&3)==3:
        mode=(op>>3)&7; reg=op&7; ea=decode_ea(b,pc+2,mode,reg,'w')
        subop=(op>>9)&7
        return x|{'mnemonic':('asr_mem','asl_mem','lsr_mem','lsl_mem','roxr_mem','roxl_mem','ror_mem','rol_mem')[subop],
                  'op_size':'w','ea':ea.dict(),'size':2+ea.ext_bytes,'flow':'fallthrough'}

    # EXG register pairs. Check before the broader Cxxx AND/MUL family.
    exgpat=op & 0xF1F8
    if exgpat in (0xC140,0xC148,0xC188):
        kind={0xC140:'d_d',0xC148:'a_a',0xC188:'d_a'}[exgpat]
        return x|{'mnemonic':'exg','kind':kind,'reg_x':(op>>9)&7,'reg_y':op&7,'size':2,'flow':'fallthrough'}

    # OR/AND source-EA -> Dn plus 68000 DIV/MUL word forms.
    if (op & 0xF000) in (0x8000,0xC000):
        opmode=(op>>6)&7; dn=(op>>9)&7; top=op&0xF000
        if opmode in (0,1,2):
            sizech={0:'b',1:'w',2:'l'}[opmode]; mode=(op>>3)&7; reg=op&7
            ea=decode_ea(b,pc+2,mode,reg,sizech)
            return x|{'mnemonic':'or_to_dn' if top==0x8000 else 'and_to_dn','op_size':sizech,'dst_d':dn,'src':ea.dict(),
                      'size':2+ea.ext_bytes,'flow':'fallthrough'}
        if opmode in (3,7):
            mode=(op>>3)&7; reg=op&7; ea=decode_ea(b,pc+2,mode,reg,'w')
            if top==0xC000: mn='mulu' if opmode==3 else 'muls'
            else: mn='divu' if opmode==3 else 'divs'
            return x|{'mnemonic':mn,'op_size':'w','dst_d':dn,'src':ea.dict(),'size':2+ea.ext_bytes,'flow':'fallthrough'}
        if opmode in (4,5,6):
            sizech={4:'b',5:'w',6:'l'}[opmode]; mode=(op>>3)&7; reg=op&7
            ea=decode_ea(b,pc+2,mode,reg,sizech)
            return x|{'mnemonic':'or_dn_to_ea' if top==0x8000 else 'and_dn_to_ea','op_size':sizech,'src_d':dn,'dst':ea.dict(),
                      'size':2+ea.ext_bytes,'flow':'fallthrough'}

    # NOT.
    if (op & 0xFF00)==0x4600 and op!=0x46FC:
        szcode=(op>>6)&3
        if szcode in (0,1,2):
            sizech={0:'b',1:'w',2:'l'}[szcode]; mode=(op>>3)&7; reg=op&7
            ea=decode_ea(b,pc+2,mode,reg,sizech)
            return x|{'mnemonic':'not','op_size':sizech,'ea':ea.dict(),'size':2+ea.ext_bytes,'flow':'fallthrough'}

    # NEG.
    if (op & 0xFF00)==0x4400:
        szcode=(op>>6)&3
        if szcode in (0,1,2):
            sizech={0:'b',1:'w',2:'l'}[szcode]; mode=(op>>3)&7; reg=op&7
            ea=decode_ea(b,pc+2,mode,reg,sizech)
            return x|{'mnemonic':'neg','op_size':sizech,'ea':ea.dict(),'size':2+ea.ext_bytes,'flow':'fallthrough'}

    # TST.
    if (op & 0xFF00)==0x4A00:
        szcode=(op>>6)&3
        if szcode in (0,1,2):
            sizech={0:'b',1:'w',2:'l'}[szcode]
            mode=(op>>3)&7; reg=op&7; ea=decode_ea(b,pc+2,mode,reg,sizech)
            return x|{'mnemonic':'tst','op_size':sizech,'ea':ea.dict(),'size':2+ea.ext_bytes,'flow':'fallthrough'}

    # CLR.
    if (op & 0xFF00)==0x4200:
        szcode=(op>>6)&3
        if szcode in (0,1,2):
            sizech={0:'b',1:'w',2:'l'}[szcode]
            mode=(op>>3)&7; reg=op&7; ea=decode_ea(b,pc+2,mode,reg,sizech)
            return x|{'mnemonic':'clr','op_size':sizech,'ea':ea.dict(),'size':2+ea.ext_bytes,'flow':'fallthrough'}

    # MOVE / MOVEA. This covers a large fraction of startup code and gives us EA sizes generically.
    msz=_move_size(op)
    if msz is not None:
        sm=(op>>3)&7; sr=op&7; dm=(op>>6)&7; dr=(op>>9)&7
        src=decode_ea(b,pc+2,sm,sr,msz)
        dst_ext_pc=pc+2+src.ext_bytes
        dst=decode_ea(b,dst_ext_pc,dm,dr,msz)
        # Destination address-register direct is MOVEA for word/long.
        if dm==1 and msz in ('w','l'):
            return x|{'mnemonic':'movea','op_size':msz,'src':src.dict(),'dst_a':dr,
                      'size':2+src.ext_bytes,'flow':'fallthrough'}
        return x|{'mnemonic':'move','op_size':msz,'src':src.dict(),'dst':dst.dict(),
                  'size':2+src.ext_bytes+dst.ext_bytes,'flow':'fallthrough'}

    # ADD <ea>,Dn (byte/word/long) and ADDA.
    if (op & 0xF000)==0xD000:
        opmode=(op>>6)&7; dn=(op>>9)&7
        if opmode in (0,1,2):
            sizech={0:'b',1:'w',2:'l'}[opmode]; mode=(op>>3)&7; reg=op&7
            ea=decode_ea(b,pc+2,mode,reg,sizech)
            return x|{'mnemonic':'add_to_dn','op_size':sizech,'dst_d':dn,'src':ea.dict(),
                      'size':2+ea.ext_bytes,'flow':'fallthrough'}
        if opmode in (3,7):
            sizech='w' if opmode==3 else 'l'; mode=(op>>3)&7; reg=op&7
            ea=decode_ea(b,pc+2,mode,reg,sizech)
            return x|{'mnemonic':'adda','op_size':sizech,'dst_a':dn,'src':ea.dict(),
                      'size':2+ea.ext_bytes,'flow':'fallthrough'}
        # Dn -> EA forms used by common startup/engine code (e.g. ADD.W d7,d5).
        if opmode in (4,5,6):
            sizech={4:'b',5:'w',6:'l'}[opmode]; mode=(op>>3)&7; reg=op&7
            ea=decode_ea(b,pc+2,mode,reg,sizech)
            return x|{'mnemonic':'add_dn_to_ea','op_size':sizech,'src_d':dn,'dst':ea.dict(),
                      'size':2+ea.ext_bytes,'flow':'fallthrough'}

    # SUB <ea>,Dn / SUBA, source-EA forms.
    if (op & 0xF000)==0x9000:
        opmode=(op>>6)&7; dn=(op>>9)&7
        if opmode in (0,1,2):
            sizech={0:'b',1:'w',2:'l'}[opmode]; mode=(op>>3)&7; reg=op&7
            ea=decode_ea(b,pc+2,mode,reg,sizech)
            return x|{'mnemonic':'sub_from_dn','op_size':sizech,'dst_d':dn,'src':ea.dict(),'size':2+ea.ext_bytes,'flow':'fallthrough'}
        if opmode in (3,7):
            sizech='w' if opmode==3 else 'l'; mode=(op>>3)&7; reg=op&7
            ea=decode_ea(b,pc+2,mode,reg,sizech)
            return x|{'mnemonic':'suba','op_size':sizech,'dst_a':dn,'src':ea.dict(),'size':2+ea.ext_bytes,'flow':'fallthrough'}
        if opmode in (4,5,6):
            sizech={4:'b',5:'w',6:'l'}[opmode]; mode=(op>>3)&7; reg=op&7
            ea=decode_ea(b,pc+2,mode,reg,sizech)
            return x|{'mnemonic':'sub_dn_to_ea','op_size':sizech,'src_d':dn,'dst':ea.dict(),
                      'size':2+ea.ext_bytes,'flow':'fallthrough'}

    # CMP <ea>,Dn / CMPA / CMPM.
    if (op & 0xF000)==0xB000:
        opmode=(op>>6)&7; dn=(op>>9)&7
        if opmode in (0,1,2):
            sizech={0:'b',1:'w',2:'l'}[opmode]; mode=(op>>3)&7; reg=op&7
            ea=decode_ea(b,pc+2,mode,reg,sizech)
            return x|{'mnemonic':'cmp','op_size':sizech,'dst_d':dn,'src':ea.dict(),
                      'size':2+ea.ext_bytes,'flow':'fallthrough'}
        if opmode in (3,7):
            sizech='w' if opmode==3 else 'l'; mode=(op>>3)&7; reg=op&7
            ea=decode_ea(b,pc+2,mode,reg,sizech)
            return x|{'mnemonic':'cmpa','op_size':sizech,'dst_a':dn,'src':ea.dict(),
                      'size':2+ea.ext_bytes,'flow':'fallthrough'}
        if opmode in (4,5,6) and ((op>>3)&7)==1:
            return x|{'mnemonic':'cmpm','op_size':{4:'b',5:'w',6:'l'}[opmode],
                      'src_a':op&7,'dst_a':dn,'size':2,'flow':'fallthrough'}
        if opmode in (4,5,6):
            sizech={4:'b',5:'w',6:'l'}[opmode]; mode=(op>>3)&7; reg=op&7
            ea=decode_ea(b,pc+2,mode,reg,sizech)
            return x|{'mnemonic':'eor_dn_to_ea','op_size':sizech,'src_d':dn,'dst':ea.dict(),
                      'size':2+ea.ext_bytes,'flow':'fallthrough'}

    # LSL/LSR register immediate form (PSII main-loop starts with LSL.W #1,D0).
    if (op & 0xF018)==0xE008:
        direction=(op>>8)&1; sizecode=(op>>6)&3; count=(op>>9)&7; count=8 if count==0 else count
        if sizecode in (0,1,2):
            return x|{'mnemonic':'lsl_imm' if direction else 'lsr_imm','op_size':{0:'b',1:'w',2:'l'}[sizecode],
                      'count':count,'dst_d':op&7,'size':2,'flow':'fallthrough'}

    raise ValueError(f"unsupported 68000 opcode {op:04X} at 0x{pc:06X}")


def with_bytes(b:bytes, ins:dict[str,Any])->dict[str,Any]:
    out=dict(ins); pc=out['address']; sz=out['size']; out['bytes']=b[pc:pc+sz].hex().upper(); return out
