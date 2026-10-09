#!/usr/bin/env python3
from __future__ import annotations
import json, os, queue, subprocess, sys, threading, time
from pathlib import Path
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

ROOT=Path(os.environ.get('MD2GBA_BUNDLE_ROOT', Path(__file__).resolve().parents[1]))
UTILITY=ROOT/'tools'/'md2gba_utility.py'

class App(tk.Tk):
    def __init__(self):
        super().__init__(); self.title('MD2GBA AOT Grinder — Genesis ROM to GBA'); self.geometry('1050x760'); self.minsize(900,650)
        self.proc=None; self.q=queue.Queue(); self.stop_requested=False; self.rom=tk.StringVar(); self.output=tk.StringVar()
        cores=max(1,os.cpu_count() or 4)
        self.workers=tk.IntVar(value=cores); self.hours=tk.DoubleVar(value=0.0); self.frames=tk.IntVar(value=3600); self.max_frames=tk.IntVar(value=115200)
        self.saturation=tk.IntVar(value=4); self.growth=tk.IntVar(value=8); self.auto_build=tk.BooleanVar(value=True); self.keep_build=tk.BooleanVar(value=False)
        self.state_explore=tk.BooleanVar(value=True); self.max_corpus=tk.IntVar(value=512); self.reset_pct=tk.IntVar(value=20); self.audio_mode=tk.StringVar(value='silent')
        self.stage=tk.StringVar(value='Ready'); self.coverage=tk.StringVar(value='No profile loaded'); self.cpu=tk.StringVar(value=f'{cores} logical CPU cores detected')
        self._build_ui(); self.after(100,self._pump)
        if len(sys.argv)>1 and Path(sys.argv[1]).is_file(): self._set_rom(Path(sys.argv[1]))

    def _build_ui(self):
        outer=ttk.Frame(self,padding=12); outer.pack(fill='both',expand=True)
        ttk.Label(outer,text='MD2GBA AOT Grinder',font=('Segoe UI',18,'bold')).pack(anchor='w')
        ttk.Label(outer,text='Desktop-first compiler: profile as long as needed, pretranslate 68K / VDP / Z80 behavior, then emit the GBA ROM.').pack(anchor='w',pady=(0,10))
        files=ttk.LabelFrame(outer,text='ROM and output',padding=10); files.pack(fill='x')
        row=ttk.Frame(files); row.pack(fill='x'); ttk.Label(row,text='Genesis ROM:',width=14).pack(side='left'); ttk.Entry(row,textvariable=self.rom).pack(side='left',fill='x',expand=True,padx=5); ttk.Button(row,text='Browse…',command=self.browse).pack(side='left')
        row=ttk.Frame(files); row.pack(fill='x',pady=(7,0)); ttk.Label(row,text='GBA output:',width=14).pack(side='left'); ttk.Entry(row,textvariable=self.output).pack(side='left',fill='x',expand=True,padx=5); ttk.Button(row,text='Choose…',command=self.choose_output).pack(side='left')

        settings=ttk.LabelFrame(outer,text='Autonomous desktop grind',padding=10); settings.pack(fill='x',pady=10)
        top=ttk.Frame(settings); top.pack(fill='x')
        self._spin(top,'CPU workers',self.workers,1,max(1,(os.cpu_count() or 4)*2),width=7)
        self._spin(top,'Max hours (0 = until saturated)',self.hours,0,720,width=7,inc=0.5)
        self._spin(top,'Initial frames',self.frames,60,200000,width=9,inc=600)
        self._spin(top,'Max frames',self.max_frames,600,1000000,width=9,inc=3600)
        bot=ttk.Frame(settings); bot.pack(fill='x',pady=(8,0))
        self._spin(bot,'Saturation epochs',self.saturation,1,20,width=6)
        self._spin(bot,'Min new PCs / epoch',self.growth,0,10000,width=7)
        ttk.Checkbutton(bot,text='Build automatically when profile saturates / time expires',variable=self.auto_build).pack(side='left',padx=12)
        ttk.Checkbutton(bot,text='Keep compiler build directory',variable=self.keep_build).pack(side='left',padx=8)
        state=ttk.Frame(settings); state.pack(fill='x',pady=(8,0))
        ttk.Checkbutton(state,text='Coverage-guided save-state exploration',variable=self.state_explore).pack(side='left')
        self._spin(state,'State corpus',self.max_corpus,8,2048,width=6,inc=16)
        self._spin(state,'Reset exploration %',self.reset_pct,0,100,width=5,inc=5)
        ttk.Label(settings,textvariable=self.cpu).pack(anchor='w',pady=(8,0))
        ttk.Label(settings,text='Default mode has no wall-clock limit. It uses every selected core, forks productive save states, lengthens campaigns on plateaus, profiles the Z80 at the Genesis clock ratio, records VDP/audio behavior, and stops only when coverage saturates.').pack(anchor='w',pady=(3,0))
        audio=ttk.Frame(settings); audio.pack(fill='x',pady=(8,0)); ttk.Label(audio,text='Target audio:').pack(side='left'); ttk.Radiobutton(audio,text='Silent validation (recommended until generic audio is validated)',variable=self.audio_mode,value='silent').pack(side='left',padx=6); ttk.Radiobutton(audio,text='Compile profiled YM/Z80 audio',variable=self.audio_mode,value='native').pack(side='left',padx=6)

        actions=ttk.Frame(outer); actions.pack(fill='x')
        self.start_btn=ttk.Button(actions,text='AUTONOMOUS AOT BUILD',command=self.start); self.start_btn.pack(side='left')
        ttk.Button(actions,text='Analyze ROM',command=lambda:self.start(mode='analyze')).pack(side='left',padx=6)
        ttk.Button(actions,text='Profile only',command=lambda:self.start(mode='profile')).pack(side='left')
        ttk.Button(actions,text='Precompile current profile',command=lambda:self.start(mode='precompile')).pack(side='left',padx=6)
        self.stop_btn=ttk.Button(actions,text='Stop',command=self.stop,state='disabled'); self.stop_btn.pack(side='left',padx=6)
        ttk.Label(actions,textvariable=self.stage,font=('Segoe UI',10,'bold')).pack(side='right')

        stat=ttk.LabelFrame(outer,text='Profile intelligence',padding=8); stat.pack(fill='x',pady=(10,6))
        ttk.Label(stat,textvariable=self.coverage).pack(anchor='w')
        self.pb=ttk.Progressbar(stat,mode='indeterminate'); self.pb.pack(fill='x',pady=(5,0))
        logs=ttk.LabelFrame(outer,text='Build log',padding=6); logs.pack(fill='both',expand=True)
        self.log=tk.Text(logs,wrap='none',font=('Consolas',9),height=20); self.log.pack(side='left',fill='both',expand=True)
        sy=ttk.Scrollbar(logs,orient='vertical',command=self.log.yview); sy.pack(side='right',fill='y'); self.log.configure(yscrollcommand=sy.set)

    def _spin(self,parent,label,var,lo,hi,width=7,inc=1):
        f=ttk.Frame(parent); f.pack(side='left',padx=(0,14)); ttk.Label(f,text=label).pack(side='left'); ttk.Spinbox(f,textvariable=var,from_=lo,to=hi,width=width,increment=inc).pack(side='left',padx=4)

    def browse(self):
        f=filedialog.askopenfilename(title='Select Genesis / Mega Drive ROM',filetypes=[('Genesis ROM','*.bin *.md *.gen *.rom'),('All files','*.*')])
        if f:self._set_rom(Path(f))
    def _set_rom(self,p):
        self.rom.set(str(p)); self.output.set(str(p.with_suffix('.gba')))
    def choose_output(self):
        f=filedialog.asksaveasfilename(title='GBA output',defaultextension='.gba',filetypes=[('GBA ROM','*.gba')])
        if f:self.output.set(f)
    def _base(self):
        if getattr(sys,'frozen',False): return [sys.executable,'--backend']
        return [sys.executable,str(UTILITY)]

    def start(self,mode='smart'):
        if self.proc is not None:return
        rom=Path(self.rom.get()) if self.rom.get() else None
        if not rom or not rom.is_file(): messagebox.showerror('MD2GBA','Select a Genesis ROM first.'); return
        out=Path(self.output.get()) if self.output.get() else rom.with_suffix('.gba'); self.output.set(str(out))
        self.stop_requested=False; self.start_btn.configure(state='disabled'); self.stop_btn.configure(state='normal'); self.pb.start(12); self.stage.set('Starting…')
        cfg={'hours':float(self.hours.get()),'workers':int(self.workers.get()),'frames':int(self.frames.get()),'max_frames':int(self.max_frames.get()),'saturation':int(self.saturation.get()),'growth':int(self.growth.get()),'auto_build':bool(self.auto_build.get()),'keep_build':bool(self.keep_build.get()),'state_explore':bool(self.state_explore.get()),'max_corpus':int(self.max_corpus.get()),'reset_fraction':max(0.0,min(1.0,float(self.reset_pct.get())/100.0)),'audio_mode':self.audio_mode.get()}
        t=threading.Thread(target=self._workflow,args=(mode,rom,out,cfg),daemon=True); t.start()

    def _run(self,args,stage):
        if self.stop_requested: return 130
        self.q.put(('stage',stage)); cmd=self._base()+args; self.q.put(('log','\n> '+' '.join(f'"{x}"' if ' ' in str(x) else str(x) for x in cmd)+'\n'))
        flags=0
        if os.name=='nt': flags=getattr(subprocess,'CREATE_NO_WINDOW',0)
        self.proc=subprocess.Popen(cmd,cwd=ROOT,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,bufsize=1,creationflags=flags)
        for line in self.proc.stdout:
            self.q.put(('log',line)); self._parse_event(line)
            if self.stop_requested:
                try:self.proc.terminate()
                except Exception:pass
                break
        rc=self.proc.wait(); self.proc=None; return rc

    def _parse_event(self,line):
        z=line.strip()
        if not z.startswith('{') or '"event"' not in z:return
        try:e=json.loads(z)
        except Exception:return
        typ=e.get('event')
        if typ=='epoch_done': self.q.put(('coverage',f"Epoch {e['epoch']} • {e['campaigns_total']} campaigns • {e['observed_pcs']:,} 68K PCs • {e['edges']:,} edges • +{e['new_pcs']} PCs / +{e.get('new_z80',0)} Z80 / +{e.get('new_vdp',0)} VDP semantics / +{e.get('new_audio',0)} audio • semantic {e.get('semantic_growth',e['new_pcs'])} • {e.get('states',0)} states (depth {e.get('max_depth',0)})"))
        elif typ=='campaign_length_increased': self.q.put(('coverage',f"Coverage plateau detected — profiler increased campaign length to {e['frames']:,} frames"))
        elif typ=='saturated': self.q.put(('coverage','Profiler considers 68K + VDP + Z80 + audio behavior saturated at the current exploration depth; moving to offline lowering/build.'))

    def _workflow(self,mode,rom,out,cfg):
        try:
            if self._run(['analyze',str(rom)],'Analyzing ROM')!=0: raise RuntimeError('analysis failed')
            if mode=='analyze': return self.q.put(('done','Analysis complete.'))
            if mode=='precompile':
                if self._run(['precompile',str(rom)],'Generating offline 68K / VDP / Z80 translation package')!=0: raise RuntimeError('precompile failed')
                return self.q.put(('done','Offline translation package updated from current profile.'))
            prof=['auto-profile',str(rom),'--hours',str(cfg['hours']),'--workers',str(cfg['workers']),'--frames',str(cfg['frames']),'--max-frames',str(cfg['max_frames']),'--saturation-epochs',str(cfg['saturation']),'--growth-floor',str(cfg['growth']),'--max-corpus',str(cfg['max_corpus']),'--reset-fraction',str(cfg['reset_fraction'])]
            if not cfg['state_explore']: prof.append('--no-state-exploration')
            if self._run(prof,'Profiling / exploring with desktop CPU')!=0 and not self.stop_requested: raise RuntimeError('profiling failed')
            if self.stop_requested:return self.q.put(('done','Stopped. Profile data completed before stop is preserved.'))
            if self._run(['plan',str(rom)],'Generating optimization plan')!=0: raise RuntimeError('planning failed')
            if self._run(['precompile',str(rom)],'Precompiling 68K / VDP / Z80 translation package')!=0: raise RuntimeError('precompile failed')
            if mode=='profile' or not cfg['auto_build']: return self.q.put(('done','Profiling and optimization plan complete.'))
            b=['build',str(rom),'-o',str(out),'--jobs',str(cfg['workers']),'--audio-mode',cfg['audio_mode']]
            if cfg['keep_build']:b.append('--keep-build')
            if self._run(b,'Compiling native GBA ROM')!=0: raise RuntimeError('build failed')
            self.q.put(('done',f'Build complete: {out}'))
        except Exception as e:self.q.put(('error',str(e)))

    def stop(self):
        self.stop_requested=True; self.stage.set('Stopping after current process…')
        if self.proc:
            try:self.proc.terminate()
            except Exception:pass

    def _pump(self):
        try:
            while True:
                kind,data=self.q.get_nowait()
                if kind=='log': self.log.insert('end',data); self.log.see('end')
                elif kind=='stage': self.stage.set(data)
                elif kind=='coverage': self.coverage.set(data)
                elif kind in ('done','error'):
                    self.pb.stop(); self.start_btn.configure(state='normal'); self.stop_btn.configure(state='disabled'); self.stage.set(data if kind=='done' else 'Failed: '+data)
                    if kind=='error': messagebox.showerror('MD2GBA',data)
        except queue.Empty:pass
        self.after(100,self._pump)

if __name__=='__main__': App().mainloop()
