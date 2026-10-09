#!/usr/bin/env python3
from __future__ import annotations
import concurrent.futures, json, math, os, shutil, sqlite3, subprocess, time
from dataclasses import dataclass
from pathlib import Path

POLICIES=tuple(range(8))

AUTO_SCHEMA='''
CREATE TABLE IF NOT EXISTS auto_campaign_stats(
 id INTEGER PRIMARY KEY AUTOINCREMENT, rom_sha256 TEXT NOT NULL, created_at REAL NOT NULL,
 run_id INTEGER, epoch INTEGER NOT NULL, policy INTEGER NOT NULL, seed INTEGER NOT NULL,
 requested_frames INTEGER NOT NULL, new_pcs INTEGER NOT NULL, new_edges INTEGER NOT NULL,
 new_vdp INTEGER NOT NULL DEFAULT 0, new_z80 INTEGER NOT NULL DEFAULT 0, new_audio INTEGER NOT NULL DEFAULT 0,
 steps INTEGER NOT NULL, frames INTEGER NOT NULL, stop_code INTEGER NOT NULL, elapsed_sec REAL NOT NULL,
 state_id INTEGER, state_depth INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX IF NOT EXISTS idx_auto_stats_rom ON auto_campaign_stats(rom_sha256, policy);
CREATE TABLE IF NOT EXISTS state_corpus(
 id INTEGER PRIMARY KEY AUTOINCREMENT, rom_sha256 TEXT NOT NULL, path TEXT NOT NULL UNIQUE,
 created_at REAL NOT NULL, parent_id INTEGER, source_run_id INTEGER, depth INTEGER NOT NULL DEFAULT 1,
 rel_frame INTEGER NOT NULL DEFAULT 0, absolute_frame INTEGER NOT NULL DEFAULT 0, pc INTEGER NOT NULL DEFAULT 0,
 score REAL NOT NULL DEFAULT 1.0, discoveries REAL NOT NULL DEFAULT 0.0,
 uses INTEGER NOT NULL DEFAULT 0, successes INTEGER NOT NULL DEFAULT 0,
 last_used REAL, active INTEGER NOT NULL DEFAULT 1
);
CREATE INDEX IF NOT EXISTS idx_state_corpus_rom ON state_corpus(rom_sha256,active,score DESC);
'''

def _ensure_col(con,table,name,decl):
    have={r[1] for r in con.execute(f'PRAGMA table_info({table})')}
    if name not in have: con.execute(f'ALTER TABLE {table} ADD COLUMN {name} {decl}')

def ensure_auto_schema(con):
    con.executescript(AUTO_SCHEMA)
    _ensure_col(con,'auto_campaign_stats','state_id','INTEGER')
    _ensure_col(con,'auto_campaign_stats','state_depth','INTEGER NOT NULL DEFAULT 0')
    _ensure_col(con,'auto_campaign_stats','new_vdp','INTEGER NOT NULL DEFAULT 0')
    _ensure_col(con,'auto_campaign_stats','new_z80','INTEGER NOT NULL DEFAULT 0')
    _ensure_col(con,'auto_campaign_stats','new_audio','INTEGER NOT NULL DEFAULT 0')
    con.commit()

def _read_pc_set(prefix:Path):
    p=Path(str(prefix)+'.pcs.tsv'); out=set()
    if not p.exists(): return out
    with p.open() as f:
        next(f,None)
        for line in f:
            z=line.split('\t',1)[0].strip()
            if z: out.add(int(z,16))
    return out

def _read_edge_set(prefix:Path):
    p=Path(str(prefix)+'.edges.tsv'); out=set()
    if not p.exists(): return out
    with p.open() as f:
        next(f,None)
        for line in f:
            q=line.rstrip().split('\t')
            if len(q)>=4: out.add((int(q[0],16),int(q[1],16),int(q[3])))
    return out



def _read_vdp_set(prefix:Path):
    """Return all VDP novelty signatures produced by one campaign.

    Raw write sites alone are not enough for autonomous AOT rendering: later
    scenes can reuse the same VDP routines while presenting new value streams
    or completely different active tile layouts.  Keep all three domains in
    one tagged set so the existing reward/saturation machinery treats any of
    them as useful VDP novelty.
    """
    out=set()
    p=Path(str(prefix)+'.vdp.tsv')
    if p.exists():
        with p.open() as f:
            next(f,None)
            for line in f:
                q=line.rstrip().split('\t')
                if len(q)>=5:
                    out.add(('event',int(q[0],16),int(q[1]),int(q[2]),int(q[3]),int(q[4],16)))
    p=Path(str(prefix)+'.vdpvalues.tsv')
    if p.exists():
        with p.open() as f:
            next(f,None)
            for line in f:
                q=line.rstrip().split('\t')
                if len(q)>=7:
                    out.add(('value',int(q[0],16),int(q[1]),int(q[2]),int(q[3]),int(q[4]),int(q[5],16),int(q[6],16)))
    p=Path(str(prefix)+'.vdptilesets.tsv')
    if p.exists():
        with p.open() as f:
            next(f,None)
            for line in f:
                q=line.rstrip().split('\t')
                if len(q)>=10:
                    out.add(('tileset',int(q[0]),int(q[1]),int(q[2],16),int(q[3],16),
                             int(q[4],16),int(q[5],16),int(q[6],16),int(q[7],16),q[8]))
    return out

def _read_z80_set(prefix:Path):
    p=Path(str(prefix)+'.z80.tsv'); out=set()
    if not p.exists(): return out
    with p.open() as f:
        next(f,None)
        for line in f:
            q=line.rstrip().split('\t')
            if q and q[0]: out.add(int(q[0],16))
    return out

def _read_audio_set(prefix:Path):
    p=Path(str(prefix)+'.audio.tsv'); out=set()
    if not p.exists(): return out
    with p.open() as f:
        next(f,None)
        for line in f:
            q=line.rstrip().split('\t')
            if len(q)>=6:
                out.add((int(q[0]),int(q[1],16),int(q[2]),int(q[3],16),int(q[4],16),int(q[5],16)))
    return out

def _read_states(prefix:Path):
    p=Path(str(prefix)+'.states.tsv'); out=[]
    if not p.exists(): return out
    with p.open() as f:
        next(f,None)
        for line in f:
            q=line.rstrip().split('\t')
            if len(q)>=4:
                out.append({'path':Path(q[0]),'rel_frame':int(q[1]),'absolute_frame':int(q[2]),'pc':int(q[3],16)})
    return out

@dataclass
class AutoConfig:
    workers:int=max(1,os.cpu_count() or 4)
    max_hours:float=0.0
    base_frames:int=3600
    max_frames:int=115200
    max_steps:int=250_000_000
    saturation_epochs:int=4
    min_campaigns:int=16
    growth_floor:int=8
    seed:int=1
    policies:tuple=POLICIES
    state_exploration:bool=True
    reset_fraction:float=0.20
    max_corpus:int=512
    snapshot_slices:int=4
    promote_per_success:int=2

class AutoProfiler:
    def __init__(self, *, rom:Path, con:sqlite3.Connection, sha:str, runner:Path, runroot:Path,
                 import_run_fn, cfg:AutoConfig, emit=None):
        self.rom=Path(rom); self.con=con; self.sha=sha; self.runner=Path(runner); self.runroot=Path(runroot)
        self.runroot.mkdir(parents=True,exist_ok=True); self.corpus_dir=self.runroot.parent/'state_corpus'; self.corpus_dir.mkdir(parents=True,exist_ok=True)
        self.import_run_fn=import_run_fn; self.cfg=cfg
        self.emit=emit or (lambda kind,data: print(json.dumps({'event':kind,**data}),flush=True))
        ensure_auto_schema(con); self._repair_corpus()

    def _repair_corpus(self):
        for sid,path in self.con.execute('SELECT id,path FROM state_corpus WHERE rom_sha256=? AND active=1',(self.sha,)):
            if not Path(path).is_file(): self.con.execute('UPDATE state_corpus SET active=0 WHERE id=?',(sid,))
        self.con.commit()

    def _policy_stats(self):
        d={p:{'n':0,'reward':0.0,'new_pcs':0,'new_edges':0,'new_vdp':0,'new_z80':0,'new_audio':0} for p in self.cfg.policies}
        sql='''SELECT policy,count(*),coalesce(sum(new_pcs),0),coalesce(sum(new_edges),0),
                      coalesce(sum(new_vdp),0),coalesce(sum(new_z80),0),coalesce(sum(new_audio),0)
               FROM auto_campaign_stats WHERE rom_sha256=? GROUP BY policy'''
        for p,n,pcs,ed,vdp,z80,aud in self.con.execute(sql,(self.sha,)):
            if p in d:
                reward=pcs+0.20*ed+0.50*vdp+1.00*z80+0.10*aud
                d[p]={'n':n,'new_pcs':pcs,'new_edges':ed,'new_vdp':vdp,'new_z80':z80,'new_audio':aud,'reward':reward}
        return d

    def _choose_policies(self,n,epoch):
        st=self._policy_stats(); total=1+sum(v['n'] for v in st.values()); choices=[]
        for slot in range(n):
            unseen=[p for p,v in st.items() if v['n']<2]
            if unseen: p=unseen[(epoch*n+slot)%len(unseen)]
            else:
                scored=[]
                for p,v in st.items():
                    mean=v['reward']/max(1,v['n']); bonus=20.0*math.sqrt(math.log(total+1)/v['n']); scored.append((mean+bonus,p))
                scored.sort(reverse=True); p=scored[slot%max(1,min(4,len(scored)))][1]
            choices.append(p); st[p]['n']+=1; total+=1
        return choices

    def _active_states(self):
        rows=[]
        for r in self.con.execute('SELECT id,path,depth,score,discoveries,uses,successes,coalesce(last_used,0),absolute_frame,pc FROM state_corpus WHERE rom_sha256=? AND active=1',(self.sha,)):
            if Path(r[1]).is_file():
                rows.append({'id':r[0],'path':Path(r[1]),'depth':r[2],'score':r[3],'discoveries':r[4],'uses':r[5],'successes':r[6],'last_used':r[7],'absolute_frame':r[8],'pc':r[9]})
        return rows

    def _choose_states(self,n,epoch):
        if not self.cfg.state_exploration: return [None]*n
        states=self._active_states()
        if not states: return [None]*n
        choices=[]; now=time.time(); reset_period=max(1,round(1.0/max(0.01,min(1.0,self.cfg.reset_fraction))))
        # Frontier score balances novelty, depth and under-used states.  A fixed
        # reset lane keeps boot/title exploration alive even with a rich corpus.
        def priority(s):
            freshness=min(4.0,max(0.0,(now-s['last_used'])/3600.0)) if s['last_used'] else 4.0
            return (s['score']+2.0*s['successes']+0.35*s['depth']+0.15*freshness)/math.sqrt(1.0+s['uses'])
        states.sort(key=lambda s:(-priority(s),s['uses'],-s['depth'],s['id']))
        idx=0
        for slot in range(n):
            if ((epoch*n+slot)%reset_period)==0: choices.append(None); continue
            s=states[idx%len(states)].copy(); idx+=1; choices.append(s)
            self.con.execute('UPDATE state_corpus SET uses=uses+1,last_used=? WHERE id=?',(now,s['id']))
            s['uses']+=1
        self.con.commit(); return choices

    def _run_one(self, prefix, frames, policy, seed, state):
        t=time.time(); statedir=Path(str(prefix)+'.states'); statedir.mkdir(parents=True,exist_ok=True)
        snap=max(60,frames//max(1,self.cfg.snapshot_slices)) if self.cfg.state_exploration else 0
        state_path=str(state['path']) if state else '-'
        args=[str(self.runner),str(self.rom),str(prefix),str(frames),str(self.cfg.max_steps),str(policy),str(seed),state_path,str(statedir) if snap else '-',str(snap)]
        p=subprocess.run(args,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
        return prefix,policy,seed,frames,state,p.returncode,time.time()-t,p.stdout[-4000:]

    def _reward_parent(self,state,reward):
        if not state:return
        if reward>0:
            self.con.execute('UPDATE state_corpus SET successes=successes+1,discoveries=discoveries+?,score=score*0.85+? WHERE id=?',(reward,max(1.0,reward),state['id']))
        else:
            self.con.execute('UPDATE state_corpus SET score=max(0.25,score*0.97) WHERE id=?',(state['id'],))

    def _promote_states(self, states, parent, run_id, reward, epoch, policy, seed):
        if not self.cfg.state_exploration or reward<=0 or not states:return 0
        states=sorted((s for s in states if s['path'].is_file()),key=lambda s:s['rel_frame'])
        if not states:return 0
        pick=[states[-1]]
        if self.cfg.promote_per_success>1 and len(states)>=3 and reward>=max(4.0,self.cfg.growth_floor*2.0): pick.insert(0,states[len(states)//2])
        promoted=0; parent_id=parent['id'] if parent else None; depth=(parent['depth']+1) if parent else 1
        for j,s in enumerate(pick[:self.cfg.promote_per_success]):
            dst=self.corpus_dir/f'e{epoch:05d}_r{run_id:07d}_p{policy}_s{seed:08x}_{j}_f{s["absolute_frame"]:010d}_pc{s["pc"]:06X}.m2gs'
            try: shutil.copy2(s['path'],dst)
            except OSError: continue
            score=max(1.0,reward)+(0.35*depth)+(0.10*(s['rel_frame']/max(1,self.cfg.base_frames)))
            self.con.execute('''INSERT OR IGNORE INTO state_corpus(rom_sha256,path,created_at,parent_id,source_run_id,depth,rel_frame,absolute_frame,pc,score,discoveries,uses,successes,active)
                 VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,1)''',(self.sha,str(dst),time.time(),parent_id,run_id,depth,s['rel_frame'],s['absolute_frame'],s['pc'],score,reward,0,1))
            promoted+=1
        self.con.commit(); return promoted

    def _prune_corpus(self):
        active=self._active_states(); excess=len(active)-self.cfg.max_corpus
        if excess<=0:return 0
        # Retain productive, deep and under-used frontier states.  Prune old
        # shallow low-reward states first; descendants remain self-contained.
        active.sort(key=lambda s:((s['score']+1.5*s['successes']+0.25*s['depth'])/math.sqrt(1+s['uses']),s['last_used'],s['id']))
        for s in active[:excess]:
            self.con.execute('UPDATE state_corpus SET active=0 WHERE id=?',(s['id'],))
            try:s['path'].unlink()
            except OSError:pass
        self.con.commit(); return excess

    def _corpus_stats(self):
        row=self.con.execute('SELECT count(*),coalesce(max(depth),0),coalesce(sum(uses),0),coalesce(sum(successes),0) FROM state_corpus WHERE rom_sha256=? AND active=1',(self.sha,)).fetchone()
        return {'states':row[0],'max_depth':row[1],'state_uses':row[2],'state_successes':row[3]}

    def run(self):
        t0=time.time(); deadline=t0+self.cfg.max_hours*3600 if self.cfg.max_hours>0 else float('inf')
        epoch=0; stagnant=0; completed=0; frames=self.cfg.base_frames
        self.emit('auto_start',{'workers':self.cfg.workers,'max_hours':self.cfg.max_hours,'base_frames':frames,'policies':list(self.cfg.policies),'state_exploration':self.cfg.state_exploration,'max_corpus':self.cfg.max_corpus})
        while time.time()<deadline:
            batch=self.cfg.workers; policies=self._choose_policies(batch,epoch); starts=self._choose_states(batch,epoch); jobs=[]
            for i,(p,state) in enumerate(zip(policies,starts)):
                seed=(self.cfg.seed + (epoch+1)*0x9e3779b1 + i*0x85ebca6b + p*0xc2b2ae35 + (state['id']*0x27d4eb2d if state else 0))&0xffffffff
                tag=f'_st{state["id"]}_d{state["depth"]}' if state else '_reset'
                prefix=self.runroot/f'auto_e{epoch:04d}_{i:03d}_p{p}_s{seed:08x}{tag}'
                jobs.append((prefix,frames,p,seed,state))
            self.emit('epoch_start',{'epoch':epoch,'campaigns':len(jobs),'frames':frames,'policies':policies,'state_starts':sum(s is not None for s in starts),**self._corpus_stats()})
            baseline_pcs={r[0] for r in self.con.execute('SELECT pc FROM observed_pcs WHERE rom_sha256=?',(self.sha,))}
            baseline_edges={(r[0],r[1],r[2]) for r in self.con.execute('SELECT src,dst,kind FROM observed_edges WHERE rom_sha256=?',(self.sha,))}
            baseline_vdp={('event',r[0],r[1],r[2],r[3],r[4]) for r in self.con.execute('SELECT pc,type,target,code,address FROM observed_vdp_events WHERE rom_sha256=?',(self.sha,))}
            baseline_vdp.update(('value',r[0],r[1],r[2],r[3],r[4],r[5],r[6]) for r in self.con.execute('SELECT pc,type,target,code,pending,address,value FROM observed_vdp_values WHERE rom_sha256=?',(self.sha,)))
            baseline_vdp.update(('tileset',r[0],r[1],r[2],r[3],r[4],r[5],r[6],r[7],bytes(r[8]).hex().upper()) for r in self.con.execute('SELECT width,height,a_base,b_base,reg11,reg12,reg17,reg18,refs FROM observed_vdp_tile_sets WHERE rom_sha256=?',(self.sha,)))
            baseline_z80={r[0] for r in self.con.execute('SELECT pc FROM observed_z80_pcs WHERE rom_sha256=?',(self.sha,))}
            baseline_audio={(r[0],r[1],r[2],r[3],r[4],r[5]) for r in self.con.execute('SELECT cpu,pc,type,key,a,b FROM observed_audio_events WHERE rom_sha256=?',(self.sha,))}
            results=[]
            with concurrent.futures.ThreadPoolExecutor(max_workers=batch) as ex:
                fut=[ex.submit(self._run_one,*j) for j in jobs]
                for f in concurrent.futures.as_completed(fut): results.append(f.result())
            epoch_pc_union=set(); epoch_edge_union=set(); epoch_vdp_union=set(); epoch_z80_union=set(); epoch_audio_union=set(); epoch_promoted=0
            for prefix,policy,seed,req_frames,state,rc,elapsed,tail in results:
                summary_path=Path(str(prefix)+'.summary.json')
                if not summary_path.exists():
                    if state:
                        self.con.execute('UPDATE state_corpus SET active=0 WHERE id=?',(state['id'],));self.con.commit()
                    completed+=1
                    self.emit('campaign_failed',{'epoch':epoch,'policy':policy,'seed':seed,'from_state':state['id'] if state else None,'returncode':rc,'runner_tail':tail})
                    shutil.rmtree(Path(str(prefix)+'.states'),ignore_errors=True)
                    continue
                pcs=_read_pc_set(prefix); edges=_read_edge_set(prefix); vdp=_read_vdp_set(prefix); z80=_read_z80_set(prefix); audio=_read_audio_set(prefix)
                new_pc_set=pcs-baseline_pcs; new_edge_set=edges-baseline_edges; new_vdp_set=vdp-baseline_vdp; new_z80_set=z80-baseline_z80; new_audio_set=audio-baseline_audio
                new_pcs=len(new_pc_set); new_edges=len(new_edge_set); new_vdp=len(new_vdp_set); new_z80=len(new_z80_set); new_audio=len(new_audio_set)
                reward=float(new_pcs)+0.20*float(new_edges)+0.50*float(new_vdp)+1.00*float(new_z80)+0.10*float(new_audio)
                epoch_pc_union.update(new_pc_set); epoch_edge_union.update(new_edge_set); epoch_vdp_union.update(new_vdp_set); epoch_z80_union.update(new_z80_set); epoch_audio_union.update(new_audio_set)
                rid,s=self.import_run_fn(self.con,self.sha,prefix,policy,seed,req_frames);depth=state['depth'] if state else 0
                self.con.execute('''INSERT INTO auto_campaign_stats(rom_sha256,created_at,run_id,epoch,policy,seed,requested_frames,new_pcs,new_edges,new_vdp,new_z80,new_audio,steps,frames,stop_code,elapsed_sec,state_id,state_depth)
                    VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)''',(self.sha,time.time(),rid,epoch,policy,seed,req_frames,new_pcs,new_edges,new_vdp,new_z80,new_audio,int(s['steps']),int(s['frames']),int(s['stop_code']),elapsed,state['id'] if state else None,depth))
                self._reward_parent(state,reward)
                if int(s['stop_code'])==0: epoch_promoted+=self._promote_states(_read_states(prefix),state,rid,reward,epoch,policy,seed)
                self.con.commit();completed+=1
                self.emit('campaign_done',{'epoch':epoch,'policy':policy,'seed':seed,'new_pcs':new_pcs,'new_edges':new_edges,'new_vdp':new_vdp,'new_z80':new_z80,'new_audio':new_audio,'steps':s['steps'],'frames':s['frames'],'stop_code':s['stop_code'],'seconds':round(elapsed,2),'from_state':state['id'] if state else None,'state_depth':depth,'runner_tail':tail})
                # Temporary campaign snapshots are disposable after promotion.
                shutil.rmtree(Path(str(prefix)+'.states'),ignore_errors=True)
            pruned=self._prune_corpus(); epoch_new_pcs=len(epoch_pc_union); epoch_new_edges=len(epoch_edge_union); epoch_new_vdp=len(epoch_vdp_union); epoch_new_z80=len(epoch_z80_union); epoch_new_audio=len(epoch_audio_union)
            obs=self.con.execute('SELECT count(*),coalesce(sum(count),0) FROM observed_pcs WHERE rom_sha256=?',(self.sha,)).fetchone();edges=self.con.execute('SELECT count(*) FROM observed_edges WHERE rom_sha256=?',(self.sha,)).fetchone()[0]
            # Do not declare saturation just because 68K PCs stopped changing. A later
            # scene can reuse the same engine code while exercising new VDP assets/raster,
            # Z80 driver paths or audio register behavior. Weight hardware novelty into the
            # same low-growth decision, using divisors to keep large signature sets bounded.
            semantic_growth=epoch_new_pcs + epoch_new_z80 + (epoch_new_vdp//8) + (epoch_new_audio//16)
            if semantic_growth<self.cfg.growth_floor:stagnant+=1
            else:stagnant=0
            cstats=self._corpus_stats()
            self.emit('epoch_done',{'epoch':epoch,'new_pcs':epoch_new_pcs,'new_edges':epoch_new_edges,'new_vdp':epoch_new_vdp,'new_z80':epoch_new_z80,'new_audio':epoch_new_audio,'semantic_growth':semantic_growth,'observed_pcs':obs[0],'instructions':obs[1],'edges':edges,'stagnant_epochs':stagnant,'campaigns_total':completed,'states_promoted':epoch_promoted,'states_pruned':pruned,**cstats})
            if completed>=self.cfg.min_campaigns and stagnant>=self.cfg.saturation_epochs:
                if frames<self.cfg.max_frames:
                    frames=min(self.cfg.max_frames,max(frames+1,int(frames*1.75)));stagnant=0;self.emit('campaign_length_increased',{'frames':frames})
                else:self.emit('saturated',{'reason':'semantic_hardware_growth_below_threshold','epochs':stagnant,'frames':frames,**cstats});break
            epoch+=1
            if time.time()>=deadline:break
        elapsed=time.time()-t0;out={'campaigns_completed':completed,'epochs':epoch+1,'elapsed_sec':elapsed,'final_frames':frames,'stopped_by_time':time.time()>=deadline,**self._corpus_stats()}
        self.emit('auto_done',out);return out
