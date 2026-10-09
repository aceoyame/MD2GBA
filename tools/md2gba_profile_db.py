#!/usr/bin/env python3
from __future__ import annotations
import hashlib, json, sqlite3, time
from pathlib import Path

SCHEMA = r'''
PRAGMA journal_mode=WAL;
PRAGMA synchronous=NORMAL;
CREATE TABLE IF NOT EXISTS roms(
  sha256 TEXT PRIMARY KEY, size INTEGER NOT NULL, title TEXT, reset_pc INTEGER,
  initial_sp INTEGER, last_path TEXT, created_at REAL NOT NULL, updated_at REAL NOT NULL
);
CREATE TABLE IF NOT EXISTS static_pcs(
  rom_sha256 TEXT NOT NULL, pc INTEGER NOT NULL, source TEXT NOT NULL DEFAULT 'static',
  PRIMARY KEY(rom_sha256,pc)
);
CREATE TABLE IF NOT EXISTS profile_runs(
  id INTEGER PRIMARY KEY AUTOINCREMENT, rom_sha256 TEXT NOT NULL, created_at REAL NOT NULL,
  policy INTEGER NOT NULL, seed INTEGER NOT NULL, requested_frames INTEGER NOT NULL,
  steps INTEGER NOT NULL, frames INTEGER NOT NULL, stop_code INTEGER NOT NULL,
  summary_json TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS observed_pcs(
  rom_sha256 TEXT NOT NULL, pc INTEGER NOT NULL, opcode INTEGER NOT NULL,
  opclass INTEGER NOT NULL, count INTEGER NOT NULL DEFAULT 0, run_hits INTEGER NOT NULL DEFAULT 0,
  vdp_data INTEGER NOT NULL DEFAULT 0, vdp_control INTEGER NOT NULL DEFAULT 0, dma INTEGER NOT NULL DEFAULT 0,
  ym INTEGER NOT NULL DEFAULT 0, psg INTEGER NOT NULL DEFAULT 0, z80 INTEGER NOT NULL DEFAULT 0,
  PRIMARY KEY(rom_sha256,pc)
);
CREATE TABLE IF NOT EXISTS observed_edges(
  rom_sha256 TEXT NOT NULL, src INTEGER NOT NULL, dst INTEGER NOT NULL, kind INTEGER NOT NULL,
  count INTEGER NOT NULL DEFAULT 0, run_hits INTEGER NOT NULL DEFAULT 0,
  PRIMARY KEY(rom_sha256,src,dst,kind)
);
CREATE INDEX IF NOT EXISTS idx_observed_pcs_hot ON observed_pcs(rom_sha256,count DESC);
CREATE INDEX IF NOT EXISTS idx_edges_hot ON observed_edges(rom_sha256,count DESC);

CREATE TABLE IF NOT EXISTS observed_vdp_events(
  rom_sha256 TEXT NOT NULL, pc INTEGER NOT NULL, type INTEGER NOT NULL, target INTEGER NOT NULL,
  code INTEGER NOT NULL, address INTEGER NOT NULL, count INTEGER NOT NULL DEFAULT 0, run_hits INTEGER NOT NULL DEFAULT 0,
  PRIMARY KEY(rom_sha256,pc,type,target,code,address)
);
CREATE TABLE IF NOT EXISTS observed_vdp_values(
  rom_sha256 TEXT NOT NULL, pc INTEGER NOT NULL, type INTEGER NOT NULL, target INTEGER NOT NULL,
  code INTEGER NOT NULL, pending INTEGER NOT NULL, address INTEGER NOT NULL, value INTEGER NOT NULL,
  count INTEGER NOT NULL DEFAULT 0, run_hits INTEGER NOT NULL DEFAULT 0,
  PRIMARY KEY(rom_sha256,pc,type,target,code,pending,address,value)
);
CREATE TABLE IF NOT EXISTS observed_vdp_frames(
  rom_sha256 TEXT NOT NULL, width INTEGER NOT NULL, height INTEGER NOT NULL, display INTEGER NOT NULL,
  a_base INTEGER NOT NULL, b_base INTEGER NOT NULL, sat_base INTEGER NOT NULL, hscroll_base INTEGER NOT NULL,
  reg11 INTEGER NOT NULL, reg12 INTEGER NOT NULL, reg17 INTEGER NOT NULL, reg18 INTEGER NOT NULL,
  max_a INTEGER NOT NULL, max_b INTEGER NOT NULL, count INTEGER NOT NULL DEFAULT 0, run_hits INTEGER NOT NULL DEFAULT 0,
  PRIMARY KEY(rom_sha256,width,height,display,a_base,b_base,sat_base,hscroll_base,reg11,reg12,reg17,reg18,max_a,max_b)
);
CREATE TABLE IF NOT EXISTS observed_vdp_tile_sets(
  rom_sha256 TEXT NOT NULL, width INTEGER NOT NULL, height INTEGER NOT NULL,
  a_base INTEGER NOT NULL, b_base INTEGER NOT NULL, reg11 INTEGER NOT NULL, reg12 INTEGER NOT NULL,
  reg17 INTEGER NOT NULL, reg18 INTEGER NOT NULL, refs BLOB NOT NULL,
  count INTEGER NOT NULL DEFAULT 0, run_hits INTEGER NOT NULL DEFAULT 0,
  PRIMARY KEY(rom_sha256,width,height,a_base,b_base,reg11,reg12,reg17,reg18,refs)
);
CREATE TABLE IF NOT EXISTS observed_audio_events(
  rom_sha256 TEXT NOT NULL, cpu INTEGER NOT NULL, pc INTEGER NOT NULL, type INTEGER NOT NULL,
  key INTEGER NOT NULL, a INTEGER NOT NULL, b INTEGER NOT NULL, count INTEGER NOT NULL DEFAULT 0, run_hits INTEGER NOT NULL DEFAULT 0,
  PRIMARY KEY(rom_sha256,cpu,pc,type,key,a,b)
);
CREATE TABLE IF NOT EXISTS observed_z80_pcs(
  rom_sha256 TEXT NOT NULL, pc INTEGER NOT NULL, count INTEGER NOT NULL DEFAULT 0, run_hits INTEGER NOT NULL DEFAULT 0,
  PRIMARY KEY(rom_sha256,pc)
);
CREATE TABLE IF NOT EXISTS machine_images(
  id INTEGER PRIMARY KEY AUTOINCREMENT, rom_sha256 TEXT NOT NULL, created_at REAL NOT NULL,
  kind TEXT NOT NULL, sha256 TEXT NOT NULL, path TEXT NOT NULL, source_run_id INTEGER,
  UNIQUE(rom_sha256,kind,sha256)
);
CREATE TABLE IF NOT EXISTS translation_artifacts(
  id INTEGER PRIMARY KEY AUTOINCREMENT, rom_sha256 TEXT NOT NULL, created_at REAL NOT NULL,
  kind TEXT NOT NULL, path TEXT NOT NULL, summary_json TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_vdp_events_hot ON observed_vdp_events(rom_sha256,count DESC);
CREATE INDEX IF NOT EXISTS idx_vdp_values_hot ON observed_vdp_values(rom_sha256,count DESC);
CREATE INDEX IF NOT EXISTS idx_vdp_frames_hot ON observed_vdp_frames(rom_sha256,count DESC);
CREATE INDEX IF NOT EXISTS idx_vdp_tile_sets_hot ON observed_vdp_tile_sets(rom_sha256,count DESC);
CREATE INDEX IF NOT EXISTS idx_audio_events_hot ON observed_audio_events(rom_sha256,count DESC);
CREATE INDEX IF NOT EXISTS idx_z80_hot ON observed_z80_pcs(rom_sha256,count DESC);

CREATE TABLE IF NOT EXISTS build_plans(
  id INTEGER PRIMARY KEY AUTOINCREMENT, rom_sha256 TEXT NOT NULL, created_at REAL NOT NULL,
  pc_count INTEGER NOT NULL, static_count INTEGER NOT NULL, observed_count INTEGER NOT NULL,
  dynamic_only_count INTEGER NOT NULL, plan_json TEXT NOT NULL
);
'''

def _ensure_columns(con, table, columns):
    have={r[1] for r in con.execute(f"PRAGMA table_info({table})")}
    for name,decl in columns.items():
        if name not in have:
            con.execute(f"ALTER TABLE {table} ADD COLUMN {name} {decl}")

def open_db(path: Path) -> sqlite3.Connection:
    path.parent.mkdir(parents=True, exist_ok=True)
    con=sqlite3.connect(path)
    con.executescript(SCHEMA)
    # P146 development produced a few cache revisions.  Preserve expensive old
    # profiling and migrate missing additive columns instead of requiring a reset.
    _ensure_columns(con,'observed_pcs',{
      'opcode':'INTEGER NOT NULL DEFAULT 0','opclass':'INTEGER NOT NULL DEFAULT 0',
      'count':'INTEGER NOT NULL DEFAULT 0','run_hits':'INTEGER NOT NULL DEFAULT 0',
      'vdp_data':'INTEGER NOT NULL DEFAULT 0','vdp_control':'INTEGER NOT NULL DEFAULT 0',
      'dma':'INTEGER NOT NULL DEFAULT 0','ym':'INTEGER NOT NULL DEFAULT 0',
      'psg':'INTEGER NOT NULL DEFAULT 0','z80':'INTEGER NOT NULL DEFAULT 0'})
    _ensure_columns(con,'observed_edges',{'kind':'INTEGER NOT NULL DEFAULT 1','count':'INTEGER NOT NULL DEFAULT 0','run_hits':'INTEGER NOT NULL DEFAULT 0'})
    con.commit()
    return con

def rom_info(path: Path):
    raw=path.read_bytes(); h=hashlib.sha256(raw).hexdigest()
    title=''.join(chr(x) if 32<=x<127 else ' ' for x in raw[0x120:0x150]).strip() if len(raw)>=0x150 else ''
    title=' '.join(title.split())
    sp=int.from_bytes(raw[0:4],'big') if len(raw)>=4 else 0
    pc=int.from_bytes(raw[4:8],'big')&0xffffff if len(raw)>=8 else 0
    return {'sha256':h,'size':len(raw),'title':title,'initial_sp':sp,'reset_pc':pc}

def upsert_rom(con, rom:Path):
    i=rom_info(rom); now=time.time()
    con.execute('''INSERT INTO roms(sha256,size,title,reset_pc,initial_sp,last_path,created_at,updated_at)
      VALUES(?,?,?,?,?,?,?,?) ON CONFLICT(sha256) DO UPDATE SET size=excluded.size,title=excluded.title,
      reset_pc=excluded.reset_pc,initial_sp=excluded.initial_sp,last_path=excluded.last_path,updated_at=excluded.updated_at''',
      (i['sha256'],i['size'],i['title'],i['reset_pc'],i['initial_sp'],str(rom.resolve()),now,now))
    con.commit(); return i

def import_static_graph(con, sha, graph_path:Path):
    x=json.loads(graph_path.read_text()); pcs=x.get('pcs',x) if isinstance(x,dict) else x
    rows=[(sha,int(p,16) if isinstance(p,str) else int(p),'static') for p in pcs]
    con.executemany('INSERT OR IGNORE INTO static_pcs(rom_sha256,pc,source) VALUES(?,?,?)',rows); con.commit(); return len(rows)

def import_run(con, sha, prefix:Path, policy:int, seed:int, requested_frames:int):
    summ=json.loads(Path(str(prefix)+'.summary.json').read_text())
    cur=con.execute('''INSERT INTO profile_runs(rom_sha256,created_at,policy,seed,requested_frames,steps,frames,stop_code,summary_json)
      VALUES(?,?,?,?,?,?,?,?,?)''',(sha,time.time(),policy,seed,requested_frames,int(summ['steps']),int(summ['frames']),int(summ['stop_code']),json.dumps(summ,sort_keys=True)))
    runid=cur.lastrowid
    with Path(str(prefix)+'.pcs.tsv').open() as f:
        next(f)
        rows=[]
        for line in f:
            pc,c,op,cl,vd,vc,dm,ym,psg,z80=line.rstrip().split('\t'); rows.append((sha,int(pc,16),int(op,16),int(cl),int(c),int(vd),int(vc),int(dm),int(ym),int(psg),int(z80)))
        con.executemany('''INSERT INTO observed_pcs(rom_sha256,pc,opcode,opclass,count,vdp_data,vdp_control,dma,ym,psg,z80,run_hits) VALUES(?,?,?,?,?,?,?,?,?,?,?,1)
          ON CONFLICT(rom_sha256,pc) DO UPDATE SET count=count+excluded.count,run_hits=run_hits+1,
          vdp_data=vdp_data+excluded.vdp_data,vdp_control=vdp_control+excluded.vdp_control,dma=dma+excluded.dma,
          ym=ym+excluded.ym,psg=psg+excluded.psg,z80=z80+excluded.z80,opcode=excluded.opcode,opclass=excluded.opclass''',rows)
    with Path(str(prefix)+'.edges.tsv').open() as f:
        next(f); rows=[]
        for line in f:
            s,d,c,k=line.rstrip().split('\t'); rows.append((sha,int(s,16),int(d,16),int(k),int(c)))
        con.executemany('''INSERT INTO observed_edges(rom_sha256,src,dst,kind,count,run_hits) VALUES(?,?,?,?,?,1)
          ON CONFLICT(rom_sha256,src,dst,kind) DO UPDATE SET count=count+excluded.count,run_hits=run_hits+1''',rows)
    vdp_path=Path(str(prefix)+'.vdp.tsv')
    if vdp_path.exists():
        with vdp_path.open() as f:
            next(f,None); rows=[]
            for line in f:
                if not line.strip(): continue
                pc,t,target,code,addr,c=line.rstrip().split('\t'); rows.append((sha,int(pc,16),int(t),int(target),int(code),int(addr,16),int(c)))
            con.executemany("""INSERT INTO observed_vdp_events(rom_sha256,pc,type,target,code,address,count,run_hits) VALUES(?,?,?,?,?,?,?,1)
              ON CONFLICT(rom_sha256,pc,type,target,code,address) DO UPDATE SET count=count+excluded.count,run_hits=run_hits+1""",rows)
    vdpv_path=Path(str(prefix)+'.vdpvalues.tsv')
    if vdpv_path.exists():
        with vdpv_path.open() as f:
            next(f,None); rows=[]
            for line in f:
                if not line.strip(): continue
                pc,t,target,code,pending,addr,value,c=line.rstrip().split('\t'); rows.append((sha,int(pc,16),int(t),int(target),int(code),int(pending),int(addr,16),int(value,16),int(c)))
            con.executemany("""INSERT INTO observed_vdp_values(rom_sha256,pc,type,target,code,pending,address,value,count,run_hits) VALUES(?,?,?,?,?,?,?,?,?,1)
              ON CONFLICT(rom_sha256,pc,type,target,code,pending,address,value) DO UPDATE SET count=count+excluded.count,run_hits=run_hits+1""",rows)
    vdpf_path=Path(str(prefix)+'.vdpframes.tsv')
    if vdpf_path.exists():
        with vdpf_path.open() as f:
            next(f,None); rows=[]
            for line in f:
                if not line.strip(): continue
                w,h,display,a_base,b_base,sat_base,hscroll_base,r11,r12,r17,r18,ma,mb,c=line.rstrip().split('\t')
                rows.append((sha,int(w),int(h),int(display),int(a_base,16),int(b_base,16),int(sat_base,16),int(hscroll_base,16),int(r11,16),int(r12,16),int(r17,16),int(r18,16),int(ma),int(mb),int(c)))
            con.executemany("""INSERT INTO observed_vdp_frames(rom_sha256,width,height,display,a_base,b_base,sat_base,hscroll_base,reg11,reg12,reg17,reg18,max_a,max_b,count,run_hits) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,1)
              ON CONFLICT(rom_sha256,width,height,display,a_base,b_base,sat_base,hscroll_base,reg11,reg12,reg17,reg18,max_a,max_b) DO UPDATE SET count=count+excluded.count,run_hits=run_hits+1""",rows)
    vdpts_path=Path(str(prefix)+'.vdptilesets.tsv')
    if vdpts_path.exists():
        with vdpts_path.open() as f:
            next(f,None); rows=[]
            for line in f:
                if not line.strip(): continue
                w,h,a_base,b_base,r11,r12,r17,r18,refs_hex,c=line.rstrip().split('\t')
                raw=bytes.fromhex(refs_hex)
                rows.append((sha,int(w),int(h),int(a_base,16),int(b_base,16),int(r11,16),int(r12,16),int(r17,16),int(r18,16),raw,int(c)))
            con.executemany("""INSERT INTO observed_vdp_tile_sets(rom_sha256,width,height,a_base,b_base,reg11,reg12,reg17,reg18,refs,count,run_hits) VALUES(?,?,?,?,?,?,?,?,?,?,?,1)
              ON CONFLICT(rom_sha256,width,height,a_base,b_base,reg11,reg12,reg17,reg18,refs) DO UPDATE SET count=count+excluded.count,run_hits=run_hits+1""",rows)
    audio_path=Path(str(prefix)+'.audio.tsv')
    if audio_path.exists():
        with audio_path.open() as f:
            next(f,None); rows=[]
            for line in f:
                if not line.strip(): continue
                cpu,pc,t,key,a,b,c=line.rstrip().split('\t'); rows.append((sha,int(cpu),int(pc,16),int(t),int(key,16),int(a,16),int(b,16),int(c)))
            con.executemany("""INSERT INTO observed_audio_events(rom_sha256,cpu,pc,type,key,a,b,count,run_hits) VALUES(?,?,?,?,?,?,?,?,1)
              ON CONFLICT(rom_sha256,cpu,pc,type,key,a,b) DO UPDATE SET count=count+excluded.count,run_hits=run_hits+1""",rows)
    z80_path=Path(str(prefix)+'.z80.tsv')
    if z80_path.exists():
        with z80_path.open() as f:
            next(f,None); rows=[]
            for line in f:
                if not line.strip(): continue
                pc,c=line.rstrip().split('\t'); rows.append((sha,int(pc,16),int(c)))
            con.executemany("""INSERT INTO observed_z80_pcs(rom_sha256,pc,count,run_hits) VALUES(?,?,?,1)
              ON CONFLICT(rom_sha256,pc) DO UPDATE SET count=count+excluded.count,run_hits=run_hits+1""",rows)
    zram=Path(str(prefix)+'.z80ram.bin')
    if zram.exists():
        zh=hashlib.sha256(zram.read_bytes()).hexdigest()
        con.execute('INSERT OR IGNORE INTO machine_images(rom_sha256,created_at,kind,sha256,path,source_run_id) VALUES(?,?,?,?,?,?)',(sha,time.time(),'z80_ram',zh,str(zram.resolve()),runid))
    ymraw=Path(str(prefix)+'.ymraw.bin')
    if ymraw.exists() and ymraw.stat().st_size:
        yh=hashlib.sha256(ymraw.read_bytes()).hexdigest()
        con.execute('INSERT OR IGNORE INTO machine_images(rom_sha256,created_at,kind,sha256,path,source_run_id) VALUES(?,?,?,?,?,?)',(sha,time.time(),'ym_trace',yh,str(ymraw.resolve()),runid))
    con.commit(); return runid,summ

def make_plan(con, sha, out_graph:Path, out_plan:Path):
    rom_size=con.execute('SELECT size FROM roms WHERE sha256=?',(sha,)).fetchone()[0]
    static={r[0] for r in con.execute('SELECT pc FROM static_pcs WHERE rom_sha256=?',(sha,)) if 0 <= r[0] < rom_size}
    observed_all={r[0]:r[1] for r in con.execute('SELECT pc,count FROM observed_pcs WHERE rom_sha256=?',(sha,))}
    observed={p:c for p,c in observed_all.items() if 0 <= p < rom_size}
    dynamic_code={p:c for p,c in observed_all.items() if not (0 <= p < rom_size)}
    pcs=sorted(static|set(observed))
    dynamic=sorted(set(observed)-static)
    hot=sorted(observed.items(),key=lambda x:(-x[1],x[0]))
    total=sum(observed.values()) or 1
    z80_rows=list(con.execute('SELECT pc,count FROM observed_z80_pcs WHERE rom_sha256=? ORDER BY count DESC',(sha,)))
    vdp_rows=list(con.execute('SELECT pc,type,target,code,address,count FROM observed_vdp_events WHERE rom_sha256=? ORDER BY count DESC LIMIT 1000',(sha,)))
    audio_rows=list(con.execute('SELECT cpu,pc,type,key,a,b,count FROM observed_audio_events WHERE rom_sha256=? ORDER BY count DESC LIMIT 1000',(sha,)))
    accum=0; hot90=[]
    for pc,c in hot:
        hot90.append(pc); accum+=c
        if accum*100>=total*90: break
    graph={'pcs':pcs,'source':'md2gba_profile_db','rom_sha256':sha}
    out_graph.parent.mkdir(parents=True,exist_ok=True);out_graph.write_text(json.dumps(graph,indent=2)+'\n')
    plan={
      'rom_sha256':sha,'rom_size':rom_size,'static_pc_count':len(static),'observed_pc_count':len(observed),
      'observed_all_pc_count':len(observed_all),'merged_pc_count':len(pcs),'dynamic_only_pc_count':len(dynamic),
      'dynamic_code_pc_count':len(dynamic_code),'dynamic_code_instruction_count':sum(dynamic_code.values()),'hot90_pc_count':len(hot90),
      'total_observed_instructions':sum(observed_all.values()),'rom_observed_instructions':sum(observed.values()),
      'z80_observed_pc_count':len(z80_rows),'z80_observed_instructions':sum(r[1] for r in z80_rows),
      'vdp_event_signature_count':con.execute('SELECT count(*) FROM observed_vdp_events WHERE rom_sha256=?',(sha,)).fetchone()[0],
      'vdp_value_signature_count':con.execute('SELECT count(*) FROM observed_vdp_values WHERE rom_sha256=?',(sha,)).fetchone()[0],
      'vdp_frame_signature_count':con.execute('SELECT count(*) FROM observed_vdp_frames WHERE rom_sha256=?',(sha,)).fetchone()[0],
      'vdp_tile_set_signature_count':con.execute('SELECT count(*) FROM observed_vdp_tile_sets WHERE rom_sha256=?',(sha,)).fetchone()[0],
      'audio_event_signature_count':con.execute('SELECT count(*) FROM observed_audio_events WHERE rom_sha256=?',(sha,)).fetchone()[0],
      'top_pcs':[{'pc':f'{p:06X}','count':c} for p,c in hot[:100]],
      'top_hardware_pcs':[{'pc':f'{r[0]:06X}','count':r[1],'vdp_data':r[2],'vdp_control':r[3],'dma':r[4],'ym':r[5],'psg':r[6],'z80':r[7]} for r in con.execute('SELECT pc,count,vdp_data,vdp_control,dma,ym,psg,z80 FROM observed_pcs WHERE rom_sha256=? AND (vdp_data+vdp_control+dma+ym+psg+z80)>0 ORDER BY (vdp_data+vdp_control+dma+ym+psg+z80) DESC LIMIT 100',(sha,))],
      'dynamic_only_sample':[f'{p:06X}' for p in dynamic[:200]],
      'dynamic_code_sample':[{'pc':f'{p:06X}','count':c} for p,c in sorted(dynamic_code.items(),key=lambda x:(-x[1],x[0]))[:200]],
      'top_z80_pcs':[{'pc':f'{p:04X}','count':c} for p,c in z80_rows[:200]],
      'top_vdp_events':[{'pc':f'{pc:06X}','type':t,'target':target,'code':code,'address':f'{addr:04X}','count':c} for pc,t,target,code,addr,c in vdp_rows[:200]],
      'top_audio_events':[{'cpu':'z80' if cpu else '68k','pc':f'{pc:06X}' if not cpu else f'{pc:04X}','type':t,'key':f'{key:04X}','a':a,'b':b,'count':c} for cpu,pc,t,key,a,b,c in audio_rows[:200]],
      'strategy':{
        'tier0_hot':'IWRAM/native candidates selected from measured counts, subject to size/correctness gates',
        'tier1_observed':'AOT compile every actually observed ROM PC',
        'tier2_static':'retain conservative statically proven AOT graph',
        'tier3_unknown':'interpreter/dynamic-code fallback only'
      }
    }
    out_plan.write_text(json.dumps(plan,indent=2)+'\n')
    con.execute('INSERT INTO build_plans(rom_sha256,created_at,pc_count,static_count,observed_count,dynamic_only_count,plan_json) VALUES(?,?,?,?,?,?,?)',
                (sha,time.time(),len(pcs),len(static),len(observed),len(dynamic),json.dumps(plan,sort_keys=True)))
    con.commit(); return plan
