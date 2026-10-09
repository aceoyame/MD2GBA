#!/usr/bin/env python3
"""Generate the offline hardware translation package for one Genesis ROM.

P149 deliberately moves title knowledge off the GBA.  This step consumes the
persistent desktop profile database and emits the exact artifacts later stages
need: a 68K AOT graph/hotness map, Z80 profile/image choice, VDP behavior plan,
audio trace choice, dynamic-code inventory, and one reproducible manifest.
"""
from __future__ import annotations
import argparse, hashlib, json, shutil, sqlite3, time
from pathlib import Path
from md2gba_profile_db import open_db, upsert_rom, make_plan


def sha256(p:Path)->str:
    h=hashlib.sha256()
    with p.open('rb') as f:
        for b in iter(lambda:f.read(1<<20),b''):h.update(b)
    return h.hexdigest()

def existing_images(con,sha,kind):
    out=[]
    for rid,created,h,path,runid in con.execute('SELECT id,created_at,sha256,path,source_run_id FROM machine_images WHERE rom_sha256=? AND kind=? ORDER BY created_at DESC',(sha,kind)):
        p=Path(path)
        if p.is_file():out.append({'id':rid,'created_at':created,'sha256':h,'path':p,'source_run_id':runid,'bytes':p.stat().st_size})
    return out

def export_z80(con,sha,out:Path):
    rows=list(con.execute('SELECT pc,count,run_hits FROM observed_z80_pcs WHERE rom_sha256=? ORDER BY count DESC,pc',(sha,)))
    with out.open('w') as f:
        for pc,c,_ in rows:f.write(f'PC {pc:04X} {c}\n')
    return rows

def vdp_plan(con,sha):
    rows=list(con.execute('SELECT pc,type,target,code,address,count,run_hits FROM observed_vdp_events WHERE rom_sha256=? ORDER BY count DESC',(sha,)))
    values=list(con.execute('SELECT pc,type,target,code,pending,address,value,count,run_hits FROM observed_vdp_values WHERE rom_sha256=? ORDER BY count DESC',(sha,)))
    by_pc={}
    targets={1:'VRAM',2:'CRAM',3:'VSRAM',0:'NONE'}
    for pc,t,target,code,addr,count,hits in rows:
        q=by_pc.setdefault(pc,{'pc':f'{pc:06X}','events':0,'runs':0,'data':0,'control':0,'VRAM':0,'CRAM':0,'VSRAM':0,'NONE':0,'addresses':set(),'codes':set()})
        q['events']+=count;q['runs']=max(q['runs'],hits);q['data']+=count if t==2 else 0;q['control']+=count if t==1 else 0;q[targets.get(target,'NONE')]+=count;q['addresses'].add(addr);q['codes'].add(code)
    pcs=[]
    for q in by_pc.values():
        q['addresses']=[f'{x:04X}' for x in sorted(q['addresses'])[:64]];q['codes']=sorted(q['codes']);pcs.append(q)
    pcs.sort(key=lambda q:(-q['events'],q['pc']))
    total=sum(r[5] for r in rows)
    return {
      'schema':2,'event_signatures':len(rows),'value_signatures':len(values),'events':total,
      'hot_values':[{'pc':f'{pc:06X}','type':t,'target':target,'code':code,'pending':pending,'address':f'{addr:04X}','value':f'{value:04X}','count':count,'run_hits':hits} for pc,t,target,code,pending,addr,value,count,hits in values[:2000]],
      'target_counts':{name:sum(r[5] for r in rows if targets.get(r[2],'NONE')==name) for name in ('VRAM','CRAM','VSRAM','NONE')},
      'hot_sites':pcs[:500],
      'translation_policy':{
        'VRAM':'precompute tile/source provenance and specialize sequential copy/fill/DMA sites; GBA runtime publishes only changed native tile/OAM/BG state',
        'CRAM':'precompute Genesis->GBA color conversion tables and palette ownership; preserve mid-frame writes as raster events',
        'VSRAM':'classify whole-plane vs column/line scroll and lower static modes to GBA BG offsets; keep raster schedule only when observable',
        'DMA':'lower measured ROM/WRAM copy/fill modes into generated native transfer plans; retain semantic fallback for unobserved modes',
        'raster':'profile register/write timing by scanline before declaring a frame-static renderer specialization'
      }
    }


def _refs_blob_mask(raw):
    """Profiler writes 64 uint32 words as fixed-width hex; DB stores those bytes verbatim."""
    if isinstance(raw,memoryview): raw=raw.tobytes()
    mask=0
    for wi in range(min(64,len(raw)//4)):
        w=int.from_bytes(raw[wi*4:wi*4+4],'big')
        mask |= w << (wi*32)
    return mask

def _mask_sources(mask):
    out=[]
    while mask:
        lsb=mask & -mask
        out.append(lsb.bit_length()-1)
        mask ^= lsb
    return out

def _profiled_tile_mappings(con,sha,max_maps=48):
    rows=list(con.execute("SELECT width,height,a_base,b_base,reg11,reg12,reg17,reg18,refs,count,run_hits FROM observed_vdp_tile_sets WHERE rom_sha256=? ORDER BY count DESC",(sha,)))
    usable=[]; rejected=[]
    for w,h,a,b,r11,r12,r17,r18,raw,count,hits in rows:
        hc=40 if (r12&0x81)==0x81 else 32
        vr=28
        hp=min((r17&0x1f)*2,hc);vp=min(r18&0x1f,vr)
        window=((hp<hc) if (r17&0x80) else (hp>0)) or ((vp<vr) if (r18&0x80) else (vp>0))
        mask=_refs_blob_mask(raw); n=mask.bit_count()
        reason=None
        if w not in (32,64) or h!=32: reason='unsupported plane geometry'
        elif r11&4: reason='column V-scroll'
        elif r12&8: reason='shadow/highlight'
        elif window: reason='VDP window'
        elif n>1023: reason=f'active BG source set has {n} patterns (>1023)'
        if reason:
            rejected.append({'reason':reason,'frames':count,'sources':n});continue
        usable.append({'mask':mask,'count':count,'hits':hits,'width':w,'height':h,'a_base':a,'b_base':b,'reg11':r11,'reg12':r12,'reg17':r17,'reg18':r18})
    # Weighted greedy coalescing.  Combining observed tile sets is safe whenever
    # their union still fits the 1023 nonblank GBA text-BG slots.  This converts
    # profile evidence into a handful of fixed source->slot maps instead of a
    # per-frame target-side cache allocator.
    maps=[]
    for u in usable:
        best=None;best_score=None
        for i,m in enumerate(maps):
            union=m['mask']|u['mask']; n=union.bit_count()
            if n>1023: continue
            overlap=(m['mask']&u['mask']).bit_count()
            added=(union^m['mask']).bit_count()
            score=(overlap,-added,m['frames'])
            if best_score is None or score>best_score:best=i;best_score=score
        if best is None:
            if len(maps)<max_maps: maps.append({'mask':u['mask'],'frames':u['count'],'sets':1,'run_hits':u['hits']})
            else: rejected.append({'reason':'mapping candidate cap reached','frames':u['count'],'sources':u['mask'].bit_count()})
        else:
            m=maps[best];m['mask']|=u['mask'];m['frames']+=u['count'];m['sets']+=1;m['run_hits']=max(m['run_hits'],u['hits'])
    maps.sort(key=lambda m:(-m['frames'],m['mask'].bit_count()))
    total=sum(u['count'] for u in usable)+sum(r['frames'] for r in rejected)
    covered=sum(m['frames'] for m in maps)
    out=[]
    for i,m in enumerate(maps):
        sources=_mask_sources(m['mask'])
        out.append({'id':i,'slot_count':len(sources),'frames':m['frames'],'set_count':m['sets'],'run_hits':m['run_hits'],'sources':sources})
    return {'signature_count':len(rows),'usable_signature_count':len(usable),'mappings':out,'rejected':rejected,
            'profiled_frames':total,'covered_frames':covered,'coverage_ratio':(covered/total if total else 0.0)}

def vdp_renderer_plan(con,sha):
    rows=list(con.execute("SELECT width,height,display,a_base,b_base,sat_base,hscroll_base,reg11,reg12,reg17,reg18,max_a,max_b,count,run_hits FROM observed_vdp_frames WHERE rom_sha256=? ORDER BY count DESC",(sha,)))
    shown=[r for r in rows if r[2]]
    mappings=_profiled_tile_mappings(con,sha)
    fatal=[]
    if not shown: fatal.append('no profiled display-on VDP frames')
    if not mappings['mappings']: fatal.append('no profile-derived BG tile mapping fits GBA text-BG capacity')
    total=sum(r[13] for r in shown) if shown else 0
    max_tile=max([max(r[11],r[12]) for r in shown],default=0)
    widths=sorted({r[0] for r in shown}); hscroll_modes=sorted({r[7]&3 for r in shown})
    modes=[]
    for r in rows[:128]:
        modes.append({'width':r[0],'height':r[1],'display':bool(r[2]),'a_base':f'{r[3]:04X}','b_base':f'{r[4]:04X}','sat_base':f'{r[5]:04X}','hscroll_base':f'{r[6]:04X}','reg11':f'{r[7]:02X}','reg12':f'{r[8]:02X}','reg17':f'{r[9]:02X}','reg18':f'{r[10]:02X}','max_a':r[11],'max_b':r[12],'frames':r[13],'run_hits':r[14]})
    enabled=not fatal
    return {
      'schema':2,'frame_signatures':len(rows),'display_frame_signatures':len(shown),'profiled_display_frames':total,
      'renderer':'profiled_tilemap_priority' if enabled else 'generic_guarded_fallback','enabled':enabled,
      'observed_max_bg_tile':max_tile,'mapping_profile_count':len(mappings['mappings']),
      'tile_set_signature_count':mappings['signature_count'],'tile_set_coverage_ratio':mappings['coverage_ratio'],
      'tile_mappings':mappings['mappings'],'mapping_rejections':mappings['rejected'][:128],
      'observed_widths':widths,'observed_hscroll_modes':hscroll_modes,'rejection_reasons':fatal,
      'runtime_guards':['plane width 32/64','plane height 32','active BG source set covered by one generated mapping','no column V-scroll','no VDP window','no shadow/highlight'],
      'lowering':{
        'patterns':'desktop clusters observed co-resident Genesis patterns into fixed <=1023-slot GBA mappings; runtime converts/uploads only dirty or newly referenced patterns',
        'name_tables':'generated source->slot lookup translates only dirty name-table blocks; full-map scan only on scene/geometry/mapping changes',
        'palette':'use CRAM->GBA shadow and publish only dirty entries',
        'scroll':'whole-plane offsets map directly; line/cell H-scroll keeps generated HBlank table; column V-scroll falls back',
        'sprites':'retain generated SAT->OAM path and separate OBJ pattern cache',
        'fallback':'unprofiled active tile sets or unsupported VDP modes fall back without mutating Genesis VDP state'
      },
      'observed_modes':modes
    }

def audio_plan(con,sha):
    rows=list(con.execute('SELECT cpu,pc,type,key,a,b,count,run_hits FROM observed_audio_events WHERE rom_sha256=? ORDER BY count DESC',(sha,)))
    return {
      'schema':1,'event_signatures':len(rows),'events':sum(r[6] for r in rows),
      'ym_events':sum(r[6] for r in rows if r[2]==1),'psg_events':sum(r[6] for r in rows if r[2]==2),'z80_upload_events':sum(r[6] for r in rows if r[2]==3),
      'hot_events':[{'cpu':'z80' if cpu else '68k','pc':f'{pc:04X}' if cpu else f'{pc:06X}','type':t,'key':f'{key:04X}','a':a,'b':b,'count':count,'run_hits':hits} for cpu,pc,t,key,a,b,count,hits in rows[:500]],
      'translation_policy':{
        'Z80':'AOT compile profiled uploaded sound-driver images with byte guards; select variant by uploaded RAM image/hash; interpreter is recovery only',
        'YM2612':'reconstruct register streams offline, compile stable patches/envelopes/pitch zones, and schedule only dynamic key/register changes on GBA',
        'PSG':'compile latch/data streams and noise/tone schedules to native mixer events',
        'ordering':'preserve observed Z80/YM/PSG ordering and Genesis master-clock timing; never infer audio commands from title identity'
      }
    }

def main():
    ap=argparse.ArgumentParser(description='Create complete offline translation package from a profiled ROM')
    ap.add_argument('rom',type=Path);ap.add_argument('--db',type=Path,required=True);ap.add_argument('--work',type=Path,required=True);ap.add_argument('--z80-min-count',type=int,default=1)
    a=ap.parse_args();rom=a.rom.resolve();out=a.work.resolve();out.mkdir(parents=True,exist_ok=True)
    con=open_db(a.db.resolve());info=upsert_rom(con,rom);sha=info['sha256']
    graph=out/'68k_aot_graph.json';baseplan=out/'68k_plan.json';plan=make_plan(con,sha,graph,baseplan)
    zprof=out/'z80_profile.txt';zrows=export_z80(con,sha,zprof)
    zimgs=existing_images(con,sha,'z80_ram');ymimgs=existing_images(con,sha,'ym_trace')
    chosen_z=None
    if zimgs:
        # newest observed image is conservative; byte guards in generated Z80 AOT
        # prevent stale uploaded code from executing native semantics.
        chosen_z=out/'z80_ram.bin';shutil.copy2(zimgs[0]['path'],chosen_z)
    chosen_ym=None
    if ymimgs:
        # Prefer the longest ordered trace: it generally contains the largest
        # patch/key-on vocabulary and is more useful than an arbitrary newest run.
        best=max(ymimgs,key=lambda x:(x['bytes'],x['created_at']))
        chosen_ym=out/'ym_trace.bin';shutil.copy2(best['path'],chosen_ym)
    vp=vdp_plan(con,sha);(out/'vdp_plan.json').write_text(json.dumps(vp,indent=2)+'\n')
    vrp=vdp_renderer_plan(con,sha);(out/'vdp_renderer_plan.json').write_text(json.dumps(vrp,indent=2)+'\n')
    apn=audio_plan(con,sha);(out/'audio_plan.json').write_text(json.dumps(apn,indent=2)+'\n')
    dyn=plan.get('dynamic_code_sample',[]);(out/'dynamic_code_plan.json').write_text(json.dumps({'schema':1,'count':plan.get('dynamic_code_pc_count',0),'sample':dyn,'policy':'capture WRAM code bytes/state during profiling; emit guarded native variants; interpreter only for unseen/self-modified variants'},indent=2)+'\n')
    manifest={
      'schema':1,'created_at':time.time(),'rom':info,'profile_db':str(a.db.resolve()),
      '68k':{'graph':str(graph),'pc_count':plan['merged_pc_count'],'observed_pc_count':plan['observed_pc_count'],'hot90_pc_count':plan['hot90_pc_count'],'dynamic_code_pc_count':plan['dynamic_code_pc_count']},
      'vdp':{'plan':str(out/'vdp_plan.json'),'renderer_plan':str(out/'vdp_renderer_plan.json'),'renderer':vrp['renderer'],'renderer_enabled':vrp['enabled'],'observed_max_bg_tile':vrp['observed_max_bg_tile'],'event_signatures':vp['event_signatures'],'value_signatures':vp.get('value_signatures',0),'frame_signatures':vrp['frame_signatures'],'tile_mapping_profiles':vrp.get('mapping_profile_count',0),'events':vp['events']},
      'z80':{'profile':str(zprof),'pc_count':len(zrows),'instructions':sum(r[1] for r in zrows),'image_count':len(zimgs),'selected_image':str(chosen_z) if chosen_z else None},
      'audio':{'plan':str(out/'audio_plan.json'),'event_signatures':apn['event_signatures'],'ordered_ym_trace_count':len(ymimgs),'selected_ym_trace':str(chosen_ym) if chosen_ym else None},
      'build_policy':{
        'desktop':'use all requested host cores; profile until semantic/code saturation or explicit user stop; caches are reusable',
        '68k':'compile observed + statically proven ROM code AOT; measured hotness drives IWRAM/ARM selection',
        'vdp':'translate measured VDP behavior/assets offline; do not rely on title-specific renderer hacks',
        'z80':'compile profiled sound-driver code from uploaded RAM images; guarded fallback for variants',
        'gba_runtime':'scheduler, hardware publication and exceptional/unseen fallback only; routine game execution should not be interpreted'
      }
    }
    mp=out/'translation_manifest.json';mp.write_text(json.dumps(manifest,indent=2)+'\n')
    con.execute('INSERT INTO translation_artifacts(rom_sha256,created_at,kind,path,summary_json) VALUES(?,?,?,?,?)',(sha,time.time(),'precompile_manifest',str(mp),json.dumps(manifest,sort_keys=True)));con.commit()
    print(json.dumps(manifest,indent=2))
if __name__=='__main__':main()
