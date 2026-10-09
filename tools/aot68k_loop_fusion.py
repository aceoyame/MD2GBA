"""Address-independent, deadline-guarded 68000 loop recognition.

All fusions are selected from decoded instruction structure.  No title address
or ROM-specific constant is used by the recognizers.
"""


def expand_bits_loop(insns, idx):
    q = insns[idx:idx+8]
    if len(q) != 8:
        return None
    if [i['mnemonic'] for i in q] != ['shift_reg','btst_dn','bcc','or_to_dn','bra','or_to_dn','subq','dbcc']:
        return None
    if any(a['address']+a['size'] != b['address'] for a,b in zip(q,q[1:])):
        return None
    s,t,j,on,br,off,sub,db=q
    if not (s.get('op_size')=='l' and s.get('shift_kind')=='ls' and s.get('direction')=='left'
            and not s.get('count_is_reg') and s.get('count')==4):
        return None
    if t.get('ea',{}).get('kind')!='dn' or sub.get('ea',{}).get('kind')!='dn':return None
    if any(i.get('src',{}).get('kind')!='dn' or i.get('op_size')!='w' for i in [on,off]):return None
    if not (j.get('condition')==7 and j.get('target')==off['address'] and br.get('target')==sub['address']
            and db.get('condition')==1 and db.get('target')==s['address']
            and sub.get('op_size')=='w' and sub.get('quick')==1):return None
    regs=(s['dst_d'],t['src_d'],t['ea']['reg'],db['data_reg'],on['src']['reg'],off['src']['reg'])
    out,bit,src,count,one,zero=regs
    if len(set(regs))!=6 or on['dst_d']!=out or off['dst_d']!=out or sub['ea']['reg']!=bit:return None
    return dict(out=out,bit=bit,src=src,count=count,one=one,zero=zero,next=db['address']+db['size'])


def emit_expand_bits_loop(m):
    out,bit,src,count,one,zero=(m[k] for k in ['out','bit','src','count','one','zero'])
    return f'''  /* P86: guarded register-only bit -> nibble expansion.
       Admission uses the actual path cost (44 cycles/bit + 8 for set bits)
       instead of P85's all-bits-set worst case.  Locals are side-effect free
       until the exact deadline check succeeds. */
  {{ unsigned n=(c->d[{count}]&65535u)+1u, bi=c->d[{bit}]&65535u;
    unsigned on=c->d[{one}]&65535u, off=c->d[{zero}]&65535u;
    if(n<=8u && bi<32u && bi+1u>=n && (on|off)<=15u){{
      unsigned bits=(c->d[{src}]>>(bi+1u-n))&((1u<<n)-1u), pop, cost;
      pop=bits-((bits>>1)&0x55u);
      pop=(pop&0x33u)+((pop>>2)&0x33u);pop=(pop+(pop>>4))&15u;
      cost=n*44u+pop*8u;
      if(left>cost){{
        unsigned spread=bits, rep, mask, row, rem=(bi-n)&65535u;
        spread=(spread|(spread<<12))&0x000f000fu;
        spread=(spread|(spread<<6))&0x03030303u;
        spread=(spread|(spread<<3))&0x11111111u;
        mask=n==8u?0xffffffffu:((1u<<(n*4u))-1u);rep=0x11111111u&mask;
        row=(spread*on)|((rep^spread)*off);
        c->d[{out}]=(n==8u?0u:(c->d[{out}]<<(n*4u)))|row;
        c->d[{bit}]=(c->d[{bit}]&0xffff0000u)|rem;
        c->d[{count}]=(c->d[{count}]&0xffff0000u)|65535u;
        c->sr=(uint16_t)((c->sr&~31u)|(rem==0u?4u:0u)|(rem==65535u?25u:0u));
        c->pc=0x{m['next']:06X}u;steps+=n*6u+pop;left-=cost;
        goto L_{m['next']:06X};
      }}
    }}
  }}
'''


# Keep bus ordering and the existing cycle model; only bookkeeping is fused.
def counted_store_loop(insns,idx):
    q=insns[idx:idx+2]
    if len(q)!=2:return None
    move,db=q
    if move.get('mnemonic')!='move' or db.get('mnemonic')!='dbcc':return None
    if db.get('condition')!=1 or db.get('target')!=move['address']:return None
    if move['address']+move['size']!=db['address']:return None
    src,dst=move.get('src',{}),move.get('dst',{})
    if src.get('kind') not in ('dn','an','imm') or dst.get('kind') not in ('an_ind','an_postinc'):return None
    if src.get('kind')=='dn' and src.get('reg')==db['data_reg']:return None
    # MOVE An,(An)+ would read a changing address-register value each iteration;
    # our fill-style store fusion intentionally hoists invariant sources only.
    if src.get('kind')=='an' and src.get('reg')==dst.get('reg') and dst.get('kind')=='an_postinc':return None
    if src.get('kind')=='an' and move.get('op_size')=='b':return None
    if move.get('op_size') not in ('b','w','l'):return None
    sz={'b':1,'w':2,'l':4}[move['op_size']]
    inc=(2 if sz==1 and dst['reg']==7 else sz) if dst['kind']=='an_postinc' else 0
    # The fastchain's current cycle model charges register-source MOVE as four
    # cycles and immediate-source MOVE as eight, plus ten for DBF.
    iter_cycles=18 if src.get('kind')=='imm' else 14
    # P108: a common hardware-clear shape initializes both an invariant data
    # register and the DBF count immediately before a non-postincrement word
    # store.  Mark the *structure* here; runtime still proves that the address
    # register is actually the VDP data port and that its current state is a
    # batchable sequential VRAM stream.  No title PC or ROM address is used.
    vdp_fill_candidate=False
    if sz==2 and inc==0 and src.get('kind')=='dn' and idx>=2:
        initv,initc=insns[idx-2],insns[idx-1]
        contiguous=(initv['address']+initv['size']==initc['address'] and
                    initc['address']+initc['size']==move['address'])
        vdp_fill_candidate=(contiguous and initv.get('mnemonic')=='moveq' and
            initv.get('dst_d')==src.get('reg') and initc.get('mnemonic')=='move' and
            initc.get('op_size')=='w' and initc.get('src',{}).get('kind')=='imm' and
            initc.get('dst',{}).get('kind')=='dn' and initc.get('dst',{}).get('reg')==db['data_reg'])
    return dict(src_kind=src['kind'],src=src.get('reg',0),imm=src.get('value',0),
                addr=dst['reg'],count=db['data_reg'],sz=sz,inc=inc,iter_cycles=iter_cycles,
                vdp_fill_candidate=vdp_fill_candidate,
                start=move['address'],next=db['address']+db['size'])


def _store_value_expr(m,mask):
    if m['src_kind']=='dn':return f'c->d[{m["src"]}]&{mask}'
    if m['src_kind']=='an':return f'c->a[{m["src"]}]&{mask}'
    return f'{m["imm"] & 0xffffffff}u&{mask}'


def _safe_batch_expr(cost):
    """Return a division-free lower bound for floor((left-1)/cost).

    For 14 cycles, floor(x/16)+floor(x/128) <= 9x/128 < x/14.
    For 18 cycles, floor(x/32)+floor(x/64)+floor(x/128)
    <= 7x/128 < x/18.  The small-x conditional recovers the first iteration.
    """
    if cost==14:
        return '((budget<16u)?(budget>=14u):((budget>>4)+(budget>>7)))'
    if cost==18:
        return '((budget<32u)?(budget>=18u):((budget>>5)+(budget>>6)+(budget>>7)))'
    raise ValueError(cost)


def emit_counted_store_loop(m):
    addr,count,sz,inc,cost=(m[k] for k in ['addr','count','sz','inc','iter_cycles'])
    write={1:'md2gba_write8_fast',2:'md2gba_write16',4:'md2gba_write32'}[sz]
    mask={1:'255u',2:'65535u',4:'0xffffffffu'}[sz]
    sign={1:'128u',2:'32768u',4:'0x80000000u'}[sz]
    value=_store_value_expr(m,mask)
    batch=_safe_batch_expr(cost)
    hw_guard=f' && (((c->a[{addr}])&0x00ffffe0u)!=0x00c00000u)'
    vdp=''
    if m.get('vdp_fill_candidate'):
        vdp=f'''  /* P108: structural constant-word VDP fill.  The batch contains only
       complete MOVE.W/DBF iterations strictly before the current P96 deadline. */
  if(left>{cost}u && ((c->a[{addr}]&0x00FFFFFFu)==0x00C00000u)){{
    unsigned remaining=(c->d[{count}]&65535u)+1u, budget=left-1u, n={batch};
    uint32_t value={value};
    if(n>remaining)n=remaining;
    if(n && md2gba_vdp_data16_repeat_fast(b,n,(uint16_t)value)){{
      c->d[{count}]=(c->d[{count}]&0xffff0000u)|((remaining-1u-n)&65535u);
      c->sr=(uint16_t)((c->sr&~15u)|(value==0u?4u:0u)|(value&{sign}?8u:0u));
      steps+=n*2u;left-=n*{cost}u;
      if(n==remaining){{c->pc=0x{m['next']:06X}u;goto L_{m['next']:06X};}}
      c->pc=0x{m['start']:06X}u;goto L_{m['start']:06X};
    }}
  }}
'''
    return vdp+f'''  /* P86/P87: same ordered bus writes, amortized loop/CCR bookkeeping.
       VDP-addressed streams remain on the canonical instruction path because
       VDP transaction state can be observable between guest instructions. */
  if(left>{cost}u{hw_guard}){{
    unsigned remaining=(c->d[{count}]&65535u)+1u, budget=left-1u, n={batch};
    uint32_t value={value}, address=c->a[{addr}];
    if(n>remaining)n=remaining;
    if(n<3u || !md2gba_bus_fill_stream_fast(b,address,n,{sz}u,{inc}u,value))
      for(unsigned i=0u;i<n;i++){{{write}(b,address,value);address+={inc}u;}}
    else address+=n*{inc}u;
    c->a[{addr}]=address;
    c->d[{count}]=(c->d[{count}]&0xffff0000u)|((remaining-1u-n)&65535u);
    c->sr=(uint16_t)((c->sr&~15u)|(value==0u?4u:0u)|(value&{sign}?8u:0u));
    steps+=n*2u;left-=n*{cost}u;
    if(n==remaining){{c->pc=0x{m['next']:06X}u;goto L_{m['next']:06X};}}
    c->pc=0x{m['start']:06X}u;goto L_{m['start']:06X};
  }}
'''


def counted_copy_loop(insns,idx):
    """Recognize MOVE.[bwl] (As)+,(Ad)[+] / DBF without assuming addresses.

    Source and destination address registers must differ so postincrement order
    cannot affect the second effective address.  Overlapping memory is allowed:
    reads and writes remain strictly interleaved in original guest order.
    """
    q=insns[idx:idx+2]
    if len(q)!=2:return None
    move,db=q
    if move.get('mnemonic')!='move' or db.get('mnemonic')!='dbcc':return None
    if db.get('condition')!=1 or db.get('target')!=move['address']:return None
    if move['address']+move['size']!=db['address']:return None
    src,dst=move.get('src',{}),move.get('dst',{})
    if src.get('kind')!='an_postinc' or dst.get('kind') not in ('an_ind','an_postinc'):return None
    if src.get('reg')==dst.get('reg'):return None
    if move.get('op_size') not in ('b','w','l'):return None
    sz={'b':1,'w':2,'l':4}[move['op_size']]
    sinc=2 if sz==1 and src['reg']==7 else sz
    dinc=(2 if sz==1 and dst['reg']==7 else sz) if dst['kind']=='an_postinc' else 0
    # Current generated cycle oracle charges postincrement-source MOVE 8 + DBF 10.
    return dict(src=src['reg'],dst=dst['reg'],count=db['data_reg'],sz=sz,sinc=sinc,dinc=dinc,
                iter_cycles=18,start=move['address'],next=db['address']+db['size'])


def emit_counted_copy_loop(m):
    src,dst,count,sz,sinc,dinc,cost=(m[k] for k in ['src','dst','count','sz','sinc','dinc','iter_cycles'])
    read={1:'md2gba_read8_fast',2:'md2gba_read16',4:'md2gba_read32'}[sz]
    write={1:'md2gba_write8_fast',2:'md2gba_write16',4:'md2gba_write32'}[sz]
    mask={1:'255u',2:'65535u',4:'0xffffffffu'}[sz]
    sign={1:'128u',2:'32768u',4:'0x80000000u'}[sz]
    batch=_safe_batch_expr(cost)
    # Hardware apertures can have transaction-level state that is observable
    # between iterations. Keep VDP-addressed streams on the canonical decoded
    # instruction path for every access width.
    hw_guard = f' && (((c->a[{src}])&0x00ffffe0u)!=0x00c00000u) && (((c->a[{dst}])&0x00ffffe0u)!=0x00c00000u)'
    return f'''  /* P86/P87: ordered postincrement copy/stream loop fusion.
     * Byte accesses in the VDP aperture deliberately stay on the canonical
     * instruction path.  PSC routes VDP traffic through word helpers, but
     * MD2GBA must preserve its own byte-bus semantics until hardware behavior
     * is independently established.  This guard is address-class based, not
     * title based. */
  if(left>{cost}u{hw_guard}){{
    unsigned remaining=(c->d[{count}]&65535u)+1u, budget=left-1u, n={batch};
    uint32_t saddr=c->a[{src}], daddr=c->a[{dst}], value=0u;
    if(n>remaining)n=remaining;
    if(n<3u || !md2gba_bus_copy_stream_fast(b,saddr,daddr,n,{sz}u,{sinc}u,{dinc}u,&value))
      for(unsigned i=0u;i<n;i++){{value={read}(b,saddr);saddr+={sinc}u;{write}(b,daddr,value);daddr+={dinc}u;}}
    else{{saddr+=n*{sinc}u;daddr+=n*{dinc}u;}}
    c->a[{src}]=saddr;c->a[{dst}]=daddr;
    c->d[{count}]=(c->d[{count}]&0xffff0000u)|((remaining-1u-n)&65535u);
    value&={mask};
    c->sr=(uint16_t)((c->sr&~15u)|(value==0u?4u:0u)|(value&{sign}?8u:0u));
    steps+=n*2u;left-=n*{cost}u;
    if(n==remaining){{c->pc=0x{m['next']:06X}u;goto L_{m['next']:06X};}}
    c->pc=0x{m['start']:06X}u;goto L_{m['start']:06X};
  }}
'''


def counted_compare_loop(insns,idx):
    """Recognize CMPM.[bwl] (As)+,(Ad)+ / DBNE.

    DBNE continues only while CMPM reports equality.  This is the canonical
    bounded memory-scan shape; matching decoded structure keeps it title-free.
    Equal source/destination A registers are rejected because CMPM updates the
    source register before resolving the destination effective address.
    """
    q=insns[idx:idx+2]
    if len(q)!=2:return None
    cmpm,db=q
    if cmpm.get('mnemonic')!='cmpm' or db.get('mnemonic')!='dbcc':return None
    if db.get('condition')!=6 or db.get('target')!=cmpm['address']:return None
    if cmpm['address']+cmpm['size']!=db['address']:return None
    if cmpm.get('src_a')==cmpm.get('dst_a'):return None
    if cmpm.get('op_size') not in ('b','w','l'):return None
    sz={'b':1,'w':2,'l':4}[cmpm['op_size']]
    sinc=2 if sz==1 and cmpm['src_a']==7 else sz
    dinc=2 if sz==1 and cmpm['dst_a']==7 else sz
    # Existing live timing model: CMPM falls through the default mode<=1 path
    # at six cycles; DBcc is ten cycles.
    return dict(src=cmpm['src_a'],dst=cmpm['dst_a'],count=db['data_reg'],sz=sz,
                sinc=sinc,dinc=dinc,iter_cycles=16,start=cmpm['address'],next=db['address']+db['size'])


def emit_counted_compare_loop(m):
    src,dst,count,sz,sinc,dinc=(m[k] for k in ['src','dst','count','sz','sinc','dinc'])
    read={1:'md2gba_read8_fast',2:'md2gba_read16',4:'md2gba_read32'}[sz]
    mask={1:'255u',2:'65535u',4:'0xffffffffu'}[sz]
    return f'''  /* P86: bounded CMPM/DBNE equal-prefix scan fusion. */
  if(left>16u){{
    unsigned remaining=(c->d[{count}]&65535u)+1u, n=(left-1u)>>4, k=0u;
    uint32_t saddr=c->a[{src}],daddr=c->a[{dst}],sv=0u,dv=0u;int equal=1;
    if(n>remaining)n=remaining;
    if(n>=3u && md2gba_bus_compare_stream_fast(b,saddr,daddr,n,{sz}u,{sinc}u,{dinc}u,&sv,&dv,&k)){{
      saddr+=k*{sinc}u;daddr+=k*{dinc}u;equal=((sv^dv)&{mask})==0u;
    }}else{{
      for(;k<n;k++){{sv={read}(b,saddr);saddr+={sinc}u;dv={read}(b,daddr);daddr+={dinc}u;if(((sv^dv)&{mask})!=0u){{equal=0;k++;break;}}}}
    }}
    c->a[{src}]=saddr;c->a[{dst}]=daddr;(void)md68k_sub(c,{sz}u,dv,sv,0);
    steps+=k*2u;left-=k*16u;
    if(!equal){{c->d[{count}]=(c->d[{count}]&0xffff0000u)|((remaining-k)&65535u);c->pc=0x{m['next']:06X}u;goto L_{m['next']:06X};}}
    c->d[{count}]=(c->d[{count}]&0xffff0000u)|((remaining-1u-k)&65535u);
    if(k==remaining){{c->pc=0x{m['next']:06X}u;goto L_{m['next']:06X};}}
    c->pc=0x{m['start']:06X}u;goto L_{m['start']:06X};
  }}
'''


def nibble_accumulate_loop(insns,idx):
    """Recognize the fast arm of a nibble-accumulate/DBF loop.

    Shape: LSL.L #4,Dout; OR.B Dsrc,Dout; SUBQ.W #1,Dchunk;
    BNE <dbf>; ...slow path...; DBF Douter,<lsl>.
    Only iterations for which Dchunk remains non-zero are fused, so the slow
    path (often an output/callback seam in decompressors) is never crossed.
    """
    q=insns[idx:idx+4]
    if len(q)!=4:return None
    sh,orr,sub,br=q
    if [x.get('mnemonic') for x in q]!=['shift_reg','or_to_dn','subq','bcc']:return None
    if any(a['address']+a['size']!=b['address'] for a,b in zip(q,q[1:])):return None
    if not (sh.get('op_size')=='l' and sh.get('shift_kind')=='ls' and sh.get('direction')=='left'
            and not sh.get('count_is_reg') and sh.get('count')==4):return None
    if not (orr.get('op_size')=='b' and orr.get('src',{}).get('kind')=='dn' and orr.get('dst_d')==sh.get('dst_d')):return None
    if not (sub.get('op_size')=='w' and sub.get('quick')==1 and sub.get('ea',{}).get('kind')=='dn'):return None
    if br.get('condition')!=6:return None
    dbidx=None
    for j in range(idx+4,min(len(insns),idx+16)):
        if insns[j]['address']==br.get('target'):
            dbidx=j;break
        if insns[j]['address']>br.get('target',0):break
    if dbidx is None:return None
    db=insns[dbidx]
    if db.get('mnemonic')!='dbcc' or db.get('condition')!=1 or db.get('target')!=sh['address']:return None
    out=sh['dst_d'];src=orr['src']['reg'];chunk=sub['ea']['reg'];outer=db['data_reg']
    if len({out,src,chunk,outer})!=4:return None
    return dict(out=out,src=src,chunk=chunk,outer=outer,iter_cycles=38,
                start=sh['address'],next=db['address']+db['size'])


def emit_nibble_accumulate_loop(m):
    out,src,chunk,outer=(m[k] for k in ['out','src','chunk','outer'])
    return f'''  /* P86: fast-arm nibble accumulator; never crosses its slow seam. */
  if(left>38u){{
    unsigned chunk=(c->d[{chunk}]&65535u),remaining=(c->d[{outer}]&65535u)+1u,budget=left-1u;
    unsigned n=(budget<64u)?(budget>=38u):((budget>>6)+(budget>>7));
    if(chunk>1u && n){{
      uint32_t acc=c->d[{out}],part=c->d[{src}]&255u;unsigned k=chunk-1u;
      if(n>k)n=k;if(n>remaining)n=remaining;
      for(k=0u;k<n;k++)acc=(acc<<4)|part;
      c->d[{out}]=acc;chunk-=n;c->d[{chunk}]=(c->d[{chunk}]&0xffff0000u)|chunk;
      (void)md68k_sub(c,2u,chunk+1u,1u,1);
      c->d[{outer}]=(c->d[{outer}]&0xffff0000u)|((remaining-1u-n)&65535u);
      steps+=n*5u;left-=n*38u;
      if(n==remaining){{c->pc=0x{m['next']:06X}u;goto L_{m['next']:06X};}}
      c->pc=0x{m['start']:06X}u;goto L_{m['start']:06X};
    }}
  }}
'''




def nibble_converge_word_loop(insns,idx):
    """Recognize a 4-nibble word convergence loop.

    The shape compares corresponding nibbles in two words, increments each
    differing source nibble once, rotates both words by four bits, then stores
    the adjusted source word. Recognition is decoded-structure based.
    """
    q=insns[idx:idx+18]
    if len(q)!=18:return None
    want=['move','move','moveq','move','move','andi','andi','cmp','bcc','scc','andi','addq','or_to_dn','shift_reg','shift_reg','dbcc','move','dbcc']
    if [x.get('mnemonic') for x in q]!=want:return None
    if any(a['address']+a['size']!=b['address'] for a,b in zip(q,q[1:])):return None
    ld0,ld1,mi,c0,c1,m0,m1,cmp,beq,st,clr,inc,orr,r0,r1,dbi,store,dbo=q
    s0,d0=ld0.get('src',{}),ld0.get('dst',{});s1,d1=ld1.get('src',{}),ld1.get('dst',{})
    if not (ld0.get('op_size')=='w' and s0.get('kind')=='an_ind' and d0.get('kind')=='dn'):return None
    if not (ld1.get('op_size')=='w' and s1.get('kind')=='an_postinc' and d1.get('kind')=='dn'):return None
    cur,tgt=d0.get('reg'),d1.get('reg');a0,a1=s0.get('reg'),s1.get('reg')
    inner=mi.get('dst_d')
    if mi.get('imm')!=3:return None
    if not (c0.get('op_size')=='w' and c0.get('src',{}).get('kind')=='dn' and c0['src'].get('reg')==cur and c0.get('dst',{}).get('kind')=='dn'):return None
    if not (c1.get('op_size')=='w' and c1.get('src',{}).get('kind')=='dn' and c1['src'].get('reg')==tgt and c1.get('dst',{}).get('kind')=='dn'):return None
    tc,tt=c0['dst'].get('reg'),c1['dst'].get('reg')
    if not (m0.get('op_size')=='w' and m0.get('imm')==0xf and m0.get('ea',{}).get('kind')=='dn' and m0['ea'].get('reg')==tc):return None
    if not (m1.get('op_size')=='w' and m1.get('imm')==0xf and m1.get('ea',{}).get('kind')=='dn' and m1['ea'].get('reg')==tt):return None
    if not (cmp.get('op_size')=='w' and cmp.get('dst_d')==tt and cmp.get('src',{}).get('kind')=='dn' and cmp['src'].get('reg')==tc):return None
    if not (beq.get('condition')==7 and beq.get('target')==r0['address']):return None
    flag=st.get('ea',{}).get('reg')
    if not (st.get('condition')==0 and st.get('ea',{}).get('kind')=='dn'):return None
    if not (clr.get('op_size')=='w' and clr.get('imm')==0xfff0 and clr.get('ea',{}).get('kind')=='dn' and clr['ea'].get('reg')==cur):return None
    if not (inc.get('op_size')=='w' and inc.get('quick')==1 and inc.get('ea',{}).get('kind')=='dn' and inc['ea'].get('reg')==tc):return None
    if not (orr.get('op_size')=='w' and orr.get('dst_d')==cur and orr.get('src',{}).get('kind')=='dn' and orr['src'].get('reg')==tc):return None
    for rr,reg in ((r0,cur),(r1,tgt)):
        if not (rr.get('op_size')=='w' and rr.get('shift_kind')=='ro' and rr.get('direction')=='right' and not rr.get('count_is_reg') and rr.get('count')==4 and rr.get('dst_d')==reg):return None
    if not (dbi.get('condition')==1 and dbi.get('data_reg')==inner and dbi.get('target')==c0['address']):return None
    ss,sd=store.get('src',{}),store.get('dst',{})
    if not (store.get('op_size')=='w' and ss.get('kind')=='dn' and ss.get('reg')==cur and sd.get('kind')=='an_postinc' and sd.get('reg')==a0):return None
    outer=dbo.get('data_reg')
    if not (dbo.get('condition')==1 and dbo.get('target')==ld0['address']):return None
    if len({cur,tgt,tc,tt,inner,flag,outer})!=7 or a0==a1:return None
    return dict(cur=cur,tgt=tgt,tmpcur=tc,tmptgt=tt,inner=inner,flag=flag,outer=outer,a0=a0,a1=a1,start=ld0['address'],next=dbo['address']+dbo['size'])


def emit_nibble_converge_word_loop(m):
    cur,tgt,tc,tt,inner,flag,outer,a0,a1=(m[k] for k in ['cur','tgt','tmpcur','tmptgt','inner','flag','outer','a0','a1'])
    return f"""  /* P109: one complete four-nibble convergence word.
       WRAM reads are side-effect free, so compute the exact path cost before
       committing state; admit only when the whole unit stays before P96's
       current scheduler deadline. */
  if(((c->a[{a0}]&0x00ff0000u)==0x00ff0000u) && ((c->a[{a1}]&0x00ff0000u)==0x00ff0000u)){{
    uint16_t p109_w=md2gba_read16(b,c->a[{a0}]&0xffffffu),p109_t=md2gba_read16(b,c->a[{a1}]&0xffffffu);
    uint16_t p109_x=p109_w,p109_y=p109_t,p109_tc=0u,p109_tt=0u;unsigned p109_m=0u;
    for(unsigned p109_i=0u;p109_i<4u;p109_i++){{
      p109_tc=(uint16_t)(p109_x&15u);p109_tt=(uint16_t)(p109_y&15u);
      if(p109_tc!=p109_tt){{p109_m++;p109_tc=(uint16_t)(p109_tc+1u);p109_x=(uint16_t)((p109_x&0xfff0u)|p109_tc);}}
      p109_x=(uint16_t)((p109_x>>4)|(p109_x<<12));p109_y=(uint16_t)((p109_y>>4)|(p109_y<<12));
    }}
    unsigned p109_cost=282u+p109_m*20u;
    if(left>p109_cost){{
      uint16_t p109_oc=(uint16_t)((c->d[{outer}]&0xffffu)-1u);uint16_t p109_sr=c->sr;
      c->a[{a1}]+=2u;c->d[{cur}]=(c->d[{cur}]&0xffff0000u)|p109_x;c->d[{tgt}]=(c->d[{tgt}]&0xffff0000u)|p109_y;
      c->d[{tc}]=(c->d[{tc}]&0xffff0000u)|p109_tc;c->d[{tt}]=(c->d[{tt}]&0xffff0000u)|p109_tt;c->d[{inner}]=0x0000ffffu;
      if(p109_m)c->d[{flag}]=(c->d[{flag}]&0xffffff00u)|0xffu;
      md2gba_write16(b,c->a[{a0}]&0xffffffu,p109_x);c->a[{a0}]+=2u;c->d[{outer}]=(c->d[{outer}]&0xffff0000u)|p109_oc;
      c->sr=(uint16_t)((p109_sr&~31u)|((p109_m?0u:(p109_sr&16u)))|(p109_x==0u?4u:0u)|(p109_x&0x8000u?8u:0u));
      steps+=41u+p109_m*4u;left-=p109_cost;
      if(p109_oc!=0xffffu){{c->pc=0x{m['start']:06X}u;goto L_{m['start']:06X};}}
      c->pc=0x{m['next']:06X}u;goto L_{m['next']:06X};
    }}
  }}
"""

def byte_to_nibble_row_loop(insns, idx):
    """Recognize one complete byte -> eight-nibble row + ordered store iteration."""
    q=insns[idx:idx+14]
    if len(q)!=14:return None
    want=['move','moveq','moveq','moveq','shift_reg','btst_dn','bcc','or_to_dn','bra','or_to_dn','subq','dbcc','move','dbcc']
    if [x.get('mnemonic') for x in q]!=want:return None
    if any(a['address']+a['size']!=b['address'] for a,b in zip(q,q[1:])):return None
    rd,mi,mb,mo,sh,bt,br,on,bypass,off,sub,inner,wr,outer=q
    rs=rd.get('src',{}); dd=rd.get('dst',{})
    if not (rd.get('op_size')=='b' and rs.get('kind')=='an_postinc' and dd.get('kind')=='dn'):return None
    byte=dd['reg']; srca=rs['reg']
    if not (mi.get('imm')==7 and mb.get('imm')==7 and mo.get('imm')==0):return None
    innerc,bit,out=mi.get('dst_d'),mb.get('dst_d'),mo.get('dst_d')
    if not (sh.get('op_size')=='l' and sh.get('shift_kind')=='ls' and sh.get('direction')=='left' and not sh.get('count_is_reg') and sh.get('count')==4 and sh.get('dst_d')==out):return None
    if not (bt.get('src_d')==bit and bt.get('ea',{}).get('kind')=='dn' and bt['ea'].get('reg')==byte):return None
    if not (br.get('condition')==7 and br.get('target')==off['address'] and bypass.get('target')==sub['address']):return None
    if not (on.get('op_size')=='w' and off.get('op_size')=='w' and on.get('dst_d')==out and off.get('dst_d')==out and on.get('src',{}).get('kind')=='dn' and off.get('src',{}).get('kind')=='dn'):return None
    one,zero=on['src']['reg'],off['src']['reg']
    if not (sub.get('op_size')=='w' and sub.get('quick')==1 and sub.get('ea',{}).get('kind')=='dn' and sub['ea'].get('reg')==bit):return None
    if not (inner.get('condition')==1 and inner.get('data_reg')==innerc and inner.get('target')==sh['address']):return None
    ws,wd=wr.get('src',{}),wr.get('dst',{})
    if not (wr.get('op_size')=='l' and ws.get('kind')=='dn' and ws.get('reg')==out and wd.get('kind')=='an_postinc'):return None
    if not (outer.get('condition')==1 and outer.get('target')==rd['address']):return None
    outerc=outer.get('data_reg'); dsta=wd.get('reg')
    if len({byte,innerc,bit,out,one,zero,outerc})!=7:return None
    return dict(byte=byte,inner=innerc,bit=bit,out=out,one=one,zero=zero,outer=outerc,srca=srca,dsta=dsta,start=rd['address'],next=outer['address']+outer['size'])


def emit_byte_to_nibble_row_loop(m):
    byte,inner,bit,out,one,zero,outer=(m[k] for k in ['byte','inner','bit','out','one','zero','outer'])
    srca,dsta=(m[k] for k in ['srca','dsta'])
    return f"""  /* P107: one complete byte->nibble-row iteration inside one scheduler interval. */
  if(left>450u && ((c->d[{one}]|c->d[{zero}])&0xffffu)<=15u){{
    uint8_t p107_b=md2gba_read8_fast(b,c->a[{srca}]&0xffffffu);c->a[{srca}]+=1u;
    unsigned p107_pop=p107_b-((p107_b>>1)&0x55u);
    p107_pop=(p107_pop&0x33u)+((p107_pop>>2)&0x33u);p107_pop=(p107_pop+(p107_pop>>4))&15u;
    unsigned p107_cost=386u+p107_pop*8u;
    uint32_t p107_sp=p107_b,p107_rep,row;
    p107_sp=(p107_sp|(p107_sp<<12))&0x000f000fu;
    p107_sp=(p107_sp|(p107_sp<<6))&0x03030303u;
    p107_sp=(p107_sp|(p107_sp<<3))&0x11111111u;p107_rep=0x11111111u;
    row=(p107_sp*(c->d[{one}]&15u))|((p107_rep^p107_sp)*(c->d[{zero}]&15u));
    c->d[{byte}]=(c->d[{byte}]&0xffffff00u)|p107_b;
    c->d[{inner}]=0x0000ffffu;
    c->d[{bit}]=0x0000ffffu;
    c->d[{out}]=row;
    md2gba_write32(b,c->a[{dsta}]&0xffffffu,row);c->a[{dsta}]+=4u;
    uint16_t p107_oc=(uint16_t)((c->d[{outer}]&0xffffu)-1u);
    c->d[{outer}]=(c->d[{outer}]&0xffff0000u)|p107_oc;
    c->sr=(uint16_t)((c->sr&~31u)|16u|(row==0u?4u:0u)|(row&0x80000000u?8u:0u));
    steps+=54u+p107_pop;left-=p107_cost;
    if(p107_oc!=0xffffu){{c->pc=0x{m['start']:06X}u;goto L_{m['start']:06X};}}
    c->pc=0x{m['next']:06X}u;goto L_{m['next']:06X};
  }}
"""

def nemesis_table_token_setup(insns, idx):
    """Recognize a table-driven variable-bit token setup used by Nemesis-like decoders."""
    q=insns[idx:idx+15]
    if len(q)!=15:return None
    want=['andi','add_to_dn','move','ext','sub_from_dn','cmpi','bcc','addq','shift_reg','move','move','move','andi','andi','shift_reg']
    if [x.get('mnemonic') for x in q]!=want:return None
    if any(a['address']+a['size']!=b['address'] for a,b in zip(q,q[1:])):return None
    ai,add,look0,ext,sub,cmp,br,refadd,refsh,refread,look1,mv,maskpix,maskrep,rshift=q
    idxr=ai.get('ea',{}).get('reg')
    if not (ai.get('op_size')=='w' and ai.get('imm')==0xff and ai.get('ea',{}).get('kind')=='dn'):return None
    if not (add.get('op_size')=='w' and add.get('dst_d')==idxr and add.get('src',{}).get('kind')=='dn' and add['src'].get('reg')==idxr):return None
    s0=look0.get('src',{}); d0=look0.get('dst',{})
    if not (look0.get('op_size')=='b' and s0.get('kind')=='an_index8' and d0.get('kind')=='dn'):return None
    tmp=d0.get('reg')
    if not (ext.get('from_size')=='b' and ext.get('op_size')=='w' and ext.get('data_reg')==tmp):return None
    bits=sub.get('dst_d')
    if not (sub.get('op_size')=='w' and sub.get('src',{}).get('kind')=='dn' and sub['src'].get('reg')==tmp):return None
    if not (cmp.get('op_size')=='w' and cmp.get('imm')==9 and cmp.get('ea',{}).get('kind')=='dn' and cmp['ea'].get('reg')==bits):return None
    if not (br.get('condition')==4 and br.get('target')==look1['address']):return None
    if not (refadd.get('op_size')=='w' and refadd.get('quick')==8 and refadd.get('ea',{}).get('kind')=='dn' and refadd['ea'].get('reg')==bits):return None
    buf=refsh.get('dst_d')
    if not (refsh.get('op_size')=='w' and refsh.get('shift_kind')=='as' and refsh.get('direction')=='left' and not refsh.get('count_is_reg') and refsh.get('count')==8):return None
    sr=refread.get('src',{}); dr=refread.get('dst',{})
    if not (refread.get('op_size')=='b' and sr.get('kind')=='an_postinc' and dr.get('kind')=='dn' and dr.get('reg')==buf):return None
    s1=look1.get('src',{}); d1=look1.get('dst',{})
    if not (look1.get('op_size')=='b' and s1.get('kind')=='an_index8' and d1.get('kind')=='dn' and d1.get('reg')==idxr):return None
    if not (s1.get('reg')==s0.get('reg') and (s1.get('index_ext')&0xff00)==(s0.get('index_ext')&0xff00) and s1.get('disp')==s0.get('disp')+1):return None
    rep=mv.get('dst',{}).get('reg')
    if not (mv.get('op_size')=='w' and mv.get('src',{}).get('kind')=='dn' and mv['src'].get('reg')==idxr and mv.get('dst',{}).get('kind')=='dn'):return None
    if not (maskpix.get('op_size')=='w' and maskpix.get('imm')==0xf and maskpix.get('ea',{}).get('kind')=='dn' and maskpix['ea'].get('reg')==idxr):return None
    if not (maskrep.get('op_size')=='w' and maskrep.get('imm')==0xf0 and maskrep.get('ea',{}).get('kind')=='dn' and maskrep['ea'].get('reg')==rep):return None
    if not (rshift.get('op_size')=='w' and rshift.get('shift_kind')=='ls' and rshift.get('direction')=='right' and not rshift.get('count_is_reg') and rshift.get('count')==4 and rshift.get('dst_d')==rep):return None
    if tmp!=rep or len({idxr,bits,buf,rep})!=4:return None
    return dict(idx=idxr,tmp=tmp,bits=bits,buf=buf,rep=rep,table_an=s0['reg'],table_disp=s0['disp'],table_ext=s0['index_ext'],stream_an=sr['reg'],next=rshift['address']+rshift['size'])


def emit_nemesis_table_token_setup(m):
    idx,tmp,bits,buf,rep=(m[k] for k in ['idx','tmp','bits','buf','rep'])
    ta,disp,ext,sa=(m[k] for k in ['table_an','table_disp','table_ext','stream_an'])
    return f"""  /* P107: scheduler-safe table-token superinstruction. */
  {{
    if(left>96u){{
      uint16_t p107_idx0=(uint16_t)(c->d[{idx}]&0xffffu);
      uint16_t p107_idx=(uint16_t)((p107_idx0&0x00ffu)*2u);
      uint32_t p107_keep_idx=c->d[{idx}]&0xffff0000u;
      uint32_t p107_keep_bits=c->d[{bits}]&0xffff0000u;
      uint32_t p107_keep_buf=c->d[{buf}]&0xffff0000u;
      uint32_t p107_keep_rep=c->d[{rep}]&0xffff0000u;
      c->d[{idx}]=p107_keep_idx|p107_idx;
      uint32_t p107_a0=md68k_ea_addr(c,MD68K_EA_AN_INDEX8,{ta},0u,{disp},{ext}u,1u,0u,1);
      int16_t p107_delta=(int16_t)(int8_t)md2gba_read8_fast(b,p107_a0);
      uint16_t p107_bits=(uint16_t)((c->d[{bits}]&0xffffu)-(uint16_t)p107_delta);
      unsigned p107_refill=(p107_bits<9u),p107_cost=p107_refill?96u:78u;
      if(p107_refill){{
        p107_bits=(uint16_t)(p107_bits+8u);
        uint16_t bv=(uint16_t)(c->d[{buf}]&0xffffu);
        uint8_t q=md2gba_read8_fast(b,c->a[{sa}]&0xffffffu);c->a[{sa}]+=1u;
        c->d[{buf}]=p107_keep_buf|(uint16_t)(((uint16_t)(bv<<8)&0xff00u)|q);
      }}
      c->d[{bits}]=p107_keep_bits|p107_bits;
      uint32_t p107_a1=md68k_ea_addr(c,MD68K_EA_AN_INDEX8,{ta},0u,{disp+1},{ext}u,1u,0u,1);
      uint8_t p107_ent=md2gba_read8_fast(b,p107_a1);
      uint16_t p107_r=(uint16_t)((p107_ent&0xf0u)>>4);
      c->d[{idx}]=p107_keep_idx|(uint16_t)(p107_ent&0x0fu);
      c->d[{rep}]=p107_keep_rep|p107_r;
      c->sr=(uint16_t)((c->sr&~31u)|(p107_r==0u?4u:0u));
      c->pc=0x{m['next']:06X}u;steps+=p107_refill?15u:12u;left-=p107_cost;
      goto L_{m['next']:06X};
    }}
  }}
"""

def _strided_batch_expr(cost):
    """Division-free safe lower bounds for floor((left-1)/cost)."""
    if cost==24:return '((budget<32u)?(budget>=24u):((budget>>5)+(budget>>7)))'
    if cost==28:return '((budget<32u)?(budget>=28u):((budget>>5)+(budget>>8)))'
    if cost==30:return '((budget<32u)?(budget>=30u):(budget>>5))'
    if cost==32:return '(budget>>5)'
    if cost==36:return '((budget<64u)?(budget>=36u):((budget>>6)+(budget>>7)))'
    raise ValueError(cost)


def strided_move_loop(insns,idx):
    """Recognize MOVE / LEA stride / DBF table walks.

    The recognizer describes an access shape, never a title/address. Sources are
    restricted to invariant Dn/immediate values or an independent (An)+ stream.
    """
    q=insns[idx:idx+3]
    if len(q)!=3:return None
    move,lea,db=q
    if [x.get('mnemonic') for x in q]!=['move','lea','dbcc']:return None
    if any(a['address']+a['size']!=b['address'] for a,b in zip(q,q[1:])):return None
    if db.get('condition')!=1 or db.get('target')!=move['address']:return None
    if lea.get('ea',{}).get('kind')!='an_disp16' or lea.get('dst_a')!=lea['ea'].get('reg'):return None
    stride=lea['ea'].get('disp')
    if not stride:return None
    src,dst=move.get('src',{}),move.get('dst',{})
    if src.get('kind') not in ('dn','imm','an_postinc'):return None
    if dst.get('kind') not in ('an_ind','an_disp16'):return None
    dr=dst.get('reg'); sr=src.get('reg',0); count=db['data_reg']
    if dr!=lea.get('dst_a'):return None
    if src.get('kind')=='dn' and sr==count:return None
    if src.get('kind')=='an_postinc' and (sr==dr):return None
    if move.get('op_size') not in ('b','w','l'):return None
    sz={'b':1,'w':2,'l':4}[move['op_size']]
    sinc=0
    if src.get('kind')=='an_postinc':sinc=2 if sz==1 and sr==7 else sz
    iter_cycles=(28 if src.get('kind') in ('imm','an_postinc') else 24)
    return dict(src_kind=src['kind'],src=sr,imm=src.get('value',0),dst=dr,
                dst_disp=dst.get('disp') or 0,stride=stride,count=count,sz=sz,sinc=sinc,
                iter_cycles=iter_cycles,start=move['address'],next=db['address']+db['size'])


def emit_strided_move_loop(m):
    src,dst,count,sz,sinc,stride,disp,cost=(m[k] for k in ['src','dst','count','sz','sinc','stride','dst_disp','iter_cycles'])
    read={1:'md2gba_read8_fast',2:'md2gba_read16',4:'md2gba_read32'}[sz]
    write={1:'md2gba_write8_fast',2:'md2gba_write16',4:'md2gba_write32'}[sz]
    mask={1:'255u',2:'65535u',4:'0xffffffffu'}[sz]
    sign={1:'128u',2:'32768u',4:'0x80000000u'}[sz]
    batch=_strided_batch_expr(cost)
    if m['src_kind']=='dn': srcinit=f'c->d[{src}]&{mask}'
    elif m['src_kind']=='imm': srcinit=f'{m["imm"]&0xffffffff}u&{mask}'
    else: srcinit='0u'
    srcdecl=f'uint32_t saddr=c->a[{src}];' if m['src_kind']=='an_postinc' else ''
    srcstep=(f'value={read}(b,saddr);saddr+={sinc}u;' if m['src_kind']=='an_postinc' else '')
    srccommit=f'c->a[{src}]=saddr;' if m['src_kind']=='an_postinc' else ''
    return f'''  /* P86/P87: generic fixed-stride MOVE/LEA/DBF fusion. */
  if(left>{cost}u){{
    unsigned remaining=(c->d[{count}]&65535u)+1u,budget=left-1u,n={batch};
    uint32_t dbase=c->a[{dst}],value={srcinit};{srcdecl}
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){{{srcstep}{write}(b,dbase+({disp}),value);dbase=dbase+(uint32_t)({stride});}}
    c->a[{dst}]=dbase;{srccommit}
    c->d[{count}]=(c->d[{count}]&0xffff0000u)|((remaining-1u-n)&65535u);
    value&={mask};c->sr=(uint16_t)((c->sr&~15u)|(value==0u?4u:0u)|(value&{sign}?8u:0u));
    steps+=n*3u;left-=n*{cost}u;
    if(n==remaining){{c->pc=0x{m['next']:06X}u;goto L_{m['next']:06X};}}
    c->pc=0x{m['start']:06X}u;goto L_{m['start']:06X};
  }}
'''


def strided_clear_loop(insns,idx):
    q=insns[idx:idx+3]
    if len(q)!=3:return None
    clr,lea,db=q
    if [x.get('mnemonic') for x in q]!=['clr','lea','dbcc']:return None
    if any(a['address']+a['size']!=b['address'] for a,b in zip(q,q[1:])):return None
    if db.get('condition')!=1 or db.get('target')!=clr['address']:return None
    if lea.get('ea',{}).get('kind')!='an_disp16' or lea.get('dst_a')!=lea['ea'].get('reg'):return None
    dst=clr.get('ea',{});dr=dst.get('reg');stride=lea['ea'].get('disp')
    if dst.get('kind') not in ('an_ind','an_disp16') or dr!=lea.get('dst_a') or not stride:return None
    if clr.get('op_size') not in ('b','w','l'):return None
    sz={'b':1,'w':2,'l':4}[clr['op_size']]
    return dict(dst=dr,dst_disp=dst.get('disp') or 0,stride=stride,count=db['data_reg'],sz=sz,
                iter_cycles=30,start=clr['address'],next=db['address']+db['size'])


def emit_strided_clear_loop(m):
    dst,count,sz,stride,disp,cost=(m[k] for k in ['dst','count','sz','stride','dst_disp','iter_cycles'])
    write={1:'md2gba_write8_fast',2:'md2gba_write16',4:'md2gba_write32'}[sz]
    batch=_strided_batch_expr(cost)
    return f'''  /* P86/P87: generic fixed-stride CLR/LEA/DBF fusion. */
  if(left>{cost}u){{
    unsigned remaining=(c->d[{count}]&65535u)+1u,budget=left-1u,n={batch};uint32_t dbase=c->a[{dst}];
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){{{write}(b,dbase+({disp}),0u);dbase=dbase+(uint32_t)({stride});}}
    c->a[{dst}]=dbase;c->d[{count}]=(c->d[{count}]&0xffff0000u)|((remaining-1u-n)&65535u);
    c->sr=(uint16_t)((c->sr&~15u)|4u);steps+=n*3u;left-=n*{cost}u;
    if(n==remaining){{c->pc=0x{m['next']:06X}u;goto L_{m['next']:06X};}}
    c->pc=0x{m['start']:06X}u;goto L_{m['start']:06X};
  }}
'''


def _pair_src(q,count):
    s=q.get('src',{})
    if s.get('kind') not in ('dn','imm'):return None
    if s.get('kind')=='dn' and s.get('reg')==count:return None
    return dict(src_kind=s['kind'],src=s.get('reg',0),imm=s.get('value',0),
                disp=q.get('dst',{}).get('disp') or 0,sz={'b':1,'w':2,'l':4}[q['op_size']])


def strided_pair_store_loop(insns,idx):
    q=insns[idx:idx+4]
    if len(q)!=4:return None
    a,b,lea,db=q
    if [x.get('mnemonic') for x in q]!=['move','move','lea','dbcc']:return None
    if any(x['address']+x['size']!=y['address'] for x,y in zip(q,q[1:])):return None
    if db.get('condition')!=1 or db.get('target')!=a['address']:return None
    if lea.get('ea',{}).get('kind')!='an_disp16' or lea.get('dst_a')!=lea['ea'].get('reg'):return None
    dr=lea.get('dst_a');stride=lea['ea'].get('disp')
    if not stride:return None
    for z in (a,b):
        if z.get('op_size') not in ('b','w','l') or z.get('dst',{}).get('kind') not in ('an_ind','an_disp16') or z['dst'].get('reg')!=dr:return None
    first=_pair_src(a,db['data_reg']);second=_pair_src(b,db['data_reg'])
    if not first or not second:return None
    iter_cycles=20+(8 if first['src_kind']=='imm' else 4)+(8 if second['src_kind']=='imm' else 4)
    return dict(dst=dr,stride=stride,count=db['data_reg'],first=first,second=second,
                iter_cycles=iter_cycles,start=a['address'],next=db['address']+db['size'])


def _pair_value(x):
    mask={1:'255u',2:'65535u',4:'0xffffffffu'}[x['sz']]
    return (f'c->d[{x["src"]}]&{mask}' if x['src_kind']=='dn' else f'{x["imm"]&0xffffffff}u&{mask}')


def emit_strided_pair_store_loop(m):
    dst,count,stride,cost=(m[k] for k in ['dst','count','stride','iter_cycles'])
    a,b=m['first'],m['second']; wa={1:'md2gba_write8_fast',2:'md2gba_write16',4:'md2gba_write32'}[a['sz']]; wb={1:'md2gba_write8_fast',2:'md2gba_write16',4:'md2gba_write32'}[b['sz']]
    va,vb=_pair_value(a),_pair_value(b); mask={1:'255u',2:'65535u',4:'0xffffffffu'}[b['sz']];sign={1:'128u',2:'32768u',4:'0x80000000u'}[b['sz']]
    batch=_strided_batch_expr(cost)
    return f'''  /* P86/P87: ordered two-field record update fusion. */
  if(left>{cost}u){{
    unsigned remaining=(c->d[{count}]&65535u)+1u,budget=left-1u,n={batch};uint32_t dbase=c->a[{dst}],v0={va},v1={vb};
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){{{wa}(b,dbase+({a['disp']}),v0);{wb}(b,dbase+({b['disp']}),v1);dbase=dbase+(uint32_t)({stride});}}
    c->a[{dst}]=dbase;c->d[{count}]=(c->d[{count}]&0xffff0000u)|((remaining-1u-n)&65535u);
    v1&={mask};c->sr=(uint16_t)((c->sr&~15u)|(v1==0u?4u:0u)|(v1&{sign}?8u:0u));steps+=n*4u;left-=n*{cost}u;
    if(n==remaining){{c->pc=0x{m['next']:06X}u;goto L_{m['next']:06X};}}
    c->pc=0x{m['start']:06X}u;goto L_{m['start']:06X};
  }}
'''
