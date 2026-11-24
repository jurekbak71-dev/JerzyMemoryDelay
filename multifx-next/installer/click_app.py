#!/usr/bin/env python3
"""Mouse-operated native installer and minimal live test panel. No root build."""
import argparse
import ctypes
import errno
import json
import os
from pathlib import Path
import queue
import select
import signal
import subprocess
import sys
import threading
import time
import uuid
import tkinter as tk
from tkinter import ttk


def jack_connect(inputs, outputs):
    """Connect only our four ports to selected physical channels; no server changes."""
    lib = ctypes.CDLL('libjack.so.0')
    lib.jack_client_open.restype = ctypes.c_void_p
    lib.jack_client_open.argtypes = [ctypes.c_char_p, ctypes.c_uint, ctypes.POINTER(ctypes.c_uint)]
    lib.jack_get_ports.restype = ctypes.POINTER(ctypes.c_char_p)
    lib.jack_get_ports.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_ulong]
    lib.jack_connect.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p]
    lib.jack_client_close.argtypes = [ctypes.c_void_p]
    lib.jack_free.argtypes = [ctypes.c_void_p]
    status = ctypes.c_uint()
    client = lib.jack_client_open(b'JerzyNextPanel', 1, ctypes.byref(status))  # NoStartServer
    if not client:
        raise RuntimeError('Nie można połączyć panelu z JACK.')
    try:
        def ports(flags):
            raw = lib.jack_get_ports(client, None, b'32 bit float mono audio', flags)
            result = []
            if raw:
                try:
                    i = 0
                    while raw[i]:
                        result.append(raw[i]); i += 1
                finally:
                    lib.jack_free(raw)
            return result
        capture, playback = ports(4 | 2), ports(4 | 1)  # Physical + Output/Input
        if max(inputs) > len(capture) or max(outputs) > len(playback):
            raise RuntimeError('Brakuje wybranych kanałów JACK. Sprawdź interfejs w swoim programie JACK.')
        for c in range(2):
            for a,b in [(capture[inputs[c]-1], f'JerzyMultiFXNext:in_{c+1}'.encode()),
                        (f'JerzyMultiFXNext:out_{c+1}'.encode(), playback[outputs[c]-1])]:
                rc = lib.jack_connect(client,a,b)
                if rc not in (0,errno.EEXIST):
                    raise RuntimeError('Nie udało się połączyć kanałów JACK.')
    finally:
        lib.jack_client_close(client)


class App:
    def __init__(self, root, bundle=None, prefix=None):
        self.root = root
        self.bundle = Path(bundle or Path(__file__).resolve().parent)
        self.meta = json.loads((self.bundle/'variant.json').read_text())
        self.kind = self.meta['backend']
        self.prefix = Path(prefix or Path.home()/'.local/opt/jerzy-multifx-next')
        self.target = self.prefix/(self.kind+'-'+self.meta['source_id'])
        self.cmake = 'cmake'
        self.events = queue.Queue()
        self.process_lock = threading.Lock()
        self.io_lock = threading.Lock()
        self.host = self.build = None
        self.closing = False
        self.state = 'new'
        self.generation=0
        self.last_error = None
        self.last_stats = {}
        self.effect_active = False
        self.audio_cancel = threading.Event()
        self.effect_id = None
        self.status = tk.StringVar(value='Sprawdzanie instalacji…')
        self.diagnostics = tk.StringVar(value='Audio jest zatrzymane.')
        self.device = tk.StringVar(value='HeadRush Core')
        self.gain = tk.DoubleVar(value=.5)
        self.channel_vars = [tk.IntVar(value=n) for n in (1,1,1,2)]
        root.title('Jerzy MultiFX Next — '+self.kind.upper())
        root.geometry('680x700'); root.minsize(640,660)
        frame = ttk.Frame(root,padding=24); frame.pack(fill='both',expand=True)
        ttk.Label(frame,text='Jerzy MultiFX Next',font=('',22,'bold')).pack(anchor='w')
        ttk.Label(frame,text=self.kind.upper()+' • panel testowy',font=('',13)).pack(anchor='w',pady=(3,14))
        ttk.Label(frame,textvariable=self.status,wraplength=570).pack(anchor='w',pady=(0,12))
        self.install_button = ttk.Button(frame,text='Zainstaluj',command=self.install)
        self.install_button.pack(fill='x',pady=3)
        if self.kind=='alsa':
            row=ttk.Frame(frame);row.pack(fill='x',pady=8)
            ttk.Label(row,text='Urządzenie:').pack(side='left',padx=(0,8))
            ttk.Combobox(row,textvariable=self.device,state='readonly',values=['HeadRush Core','Test bez karty — bez dźwięku'],width=36).pack(side='left')
        else:
            ttk.Label(frame,text='Korzysta z działającego JACK. Start połączy tylko porty tego programu\nz fizycznymi kanałami wybranymi poniżej.',wraplength=570).pack(anchor='w',pady=8)
        channels=ttk.LabelFrame(frame,text='Kanały interfejsu',padding=8);channels.pack(fill='x',pady=6)
        for i,name in enumerate(['Wejście L','Wejście P','Wyjście L','Wyjście P']):
            ttk.Label(channels,text=name).grid(row=0,column=i,padx=5)
            ttk.Spinbox(channels,from_=1,to=6 if i<2 else 4,width=7,textvariable=self.channel_vars[i]).grid(row=1,column=i,padx=5)
        buttons=ttk.Frame(frame);buttons.pack(fill='x',pady=10)
        self.start_button=ttk.Button(buttons,text='Uruchom audio',command=self.start)
        self.start_button.pack(side='left',fill='x',expand=True,padx=(0,5))
        self.stop_button=ttk.Button(buttons,text='Zatrzymaj',command=self.stop)
        self.stop_button.pack(side='left',fill='x',expand=True)
        fx=ttk.LabelFrame(frame,text='Test efektu — ustaw głośność przed włączeniem',padding=8);fx.pack(fill='x',pady=5)
        ttk.Scale(fx,from_=0,to=1,variable=self.gain).pack(side='left',fill='x',expand=True,padx=(0,12))
        self.effect_button=ttk.Button(fx,text='Włącz efekt',command=self.toggle_effect)
        self.effect_button.pack(side='right')
        ttk.Label(frame,textvariable=self.diagnostics,wraplength=570).pack(anchor='w',pady=12)
        self.progress=ttk.Progressbar(frame,mode='indeterminate');self.progress.pack(fill='x',pady=5)
        self.details=tk.Text(frame,height=5,wrap='word',state='disabled')
        ttk.Button(frame,text='Pokaż / ukryj szczegóły',command=self.toggle_details).pack(anchor='w')
        ttk.Label(frame,text='Dwa warianty testuj osobno. Przed ALSA zatrzymaj JACK używający HeadRusha.',wraplength=570).pack(anchor='w',pady=(10,0))
        root.protocol('WM_DELETE_WINDOW',self.close)
        self.refresh_installation()
        root.after(50,self.poll)

    def valid_install(self):
        try:
            meta=json.loads((self.target/'variant.json').read_text())
            host=self.target/'bin'/('JerzyMultiFXNext' if self.kind=='jack2' else 'JerzyMultiFXNextALSA')
            return (meta==self.meta and host.is_file() and os.access(host,os.X_OK) and (self.target/'start.sh').is_file() and os.access(self.target/'start.sh',os.X_OK) and
                    (self.target/'plugins/gain/plugin.so').is_file())
        except (OSError,ValueError):return False

    def refresh_installation(self):
        self.set_state('ready' if self.valid_install() else 'new',
                       'Zainstalowano. Możesz uruchomić audio.' if self.valid_install() else 'Kliknij „Zainstaluj”. Przygotowanie może potrwać kilka minut.')

    def set_state(self,state,text=None):
        self.state=state
        if text:self.status.set(text)
        if state in ('installing','starting','stopping'):
            self.progress.configure(mode='indeterminate');self.progress.start(15)
        else:
            self.progress.stop();self.progress.configure(mode='determinate',value=0)
        idle=state in ('new','ready','error')
        self.install_button.configure(state='normal' if idle and not self.valid_install() else 'disabled')
        self.start_button.configure(state='normal' if idle and self.valid_install() else 'disabled')
        self.stop_button.configure(state='normal' if state=='running' else 'disabled')
        self.effect_button.configure(state='normal' if state=='running' else 'disabled',text='Wyłącz efekt' if self.effect_active else 'Włącz efekt')

    def toggle_details(self):
        if self.details.winfo_manager():self.details.pack_forget()
        else:self.details.pack(fill="both",expand=True)

    def log(self,text):
        self.details.configure(state='normal');self.details.insert('end',text+'\n')
        if int(self.details.index('end-1c').split('.')[0])>150:self.details.delete('1.0','50.0')
        self.details.see('end');self.details.configure(state='disabled')

    def audio_event(self,kind,value,generation):
        self.events.put((kind,value,generation))

    def background(self,fn,generation=None):
        def work():
            try:fn()
            except Exception as exc:
                if self.closing or (generation is not None and generation!=self.generation):return
                if generation is not None:
                    with self.process_lock:
                        if generation!=self.generation:return
                        p=self.host;self.audio_cancel.set()
                    self.terminate(p)
                    with self.process_lock:
                        if self.host is p:self.host=None
                    self.audio_event('error',str(exc),generation)
                else:self.events.put(('error',str(exc)))
        threading.Thread(target=work,daemon=True).start()

    def install(self):
        self.set_state('installing','Instalowanie… Poczekaj, nie zamykaj okna.')
        def work():
            if self.target.exists() and not self.valid_install():
                backup=self.target.with_name(self.target.name+'.incomplete-'+uuid.uuid4().hex[:8])
                self.target.rename(backup)
                self.events.put(('log','Niekompletna instalacja zachowana w: '+str(backup)))
            with self.process_lock:
                if self.closing:return
                self.build=subprocess.Popen([sys.executable,'-u',str(self.bundle/'install.py'),'--install','--prefix',str(self.prefix),'--cmake',self.cmake],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,start_new_session=True)
                p=self.build
            for line in p.stdout:self.events.put(('log',line.rstrip()))
            rc=p.wait();p.stdout.close()
            with self.process_lock:self.build=None
            if rc or not self.valid_install():raise RuntimeError('Nie udało się zainstalować programu. Szczegóły są poniżej. Sprawdź dostępne miejsce i zależności w systemowym instalatorze pakietów.')
            self.events.put(('ready','Zainstalowano. Kliknij „Uruchom audio”.'))
        self.background(work)

    def response(self,p,command=None,timeout=5):
        with self.io_lock:
            deadline=time.monotonic()+timeout
            while True:
                if command:p.stdin.write(command+'\n');p.stdin.flush()
                if not select.select([p.stdout],[],[],max(0,deadline-time.monotonic()))[0]:raise RuntimeError('Program audio nie odpowiada.')
                line=p.stdout.readline().strip()
                if line.startswith('ERR busy:') and time.monotonic()<deadline:time.sleep(.02);continue
                if not line:raise RuntimeError('Program audio zakończył pracę. Sprawdź urządzenie i JACK. Szczegóły poniżej.')
                if command and not line.startswith('OK'):raise RuntimeError(line)
                return line

    def start(self):
        try:
            channels=[v.get() for v in self.channel_vars]
            if any(n<1 or n>(6 if i<2 else 4) for i,n in enumerate(channels)) or channels[2]==channels[3]:raise ValueError()
        except (ValueError,tk.TclError):
            self.status.set('Wybierz poprawne kanały; wyjścia L i P muszą być różne.');return
        args=[str(self.target/'start.sh')]
        if self.kind=='alsa':
            device='null' if self.device.get().startswith('Test bez karty') else 'hw:CARD=Core,DEV=0'
            args+=['--device',device]
            for key,n in zip(['--in-left','--in-right','--out-left','--out-right'],channels):args += [key,str(n)]
        self.set_state('starting','Uruchamianie audio…')
        self.last_error=None;self.last_stats={};self.effect_active=False;self.effect_id=None
        self.audio_cancel=threading.Event()
        cancel=self.audio_cancel
        with self.process_lock:
            self.generation+=1;generation=self.generation
        def work():
            logdir=Path.home()/'.local/state/jerzy-multifx-next';logdir.mkdir(parents=True,exist_ok=True)
            with (logdir/(self.kind+'.log')).open('w') as err:
                with self.process_lock:
                    if self.closing:return
                    self.host=subprocess.Popen(args,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=err,text=True,bufsize=1,start_new_session=True)
                    p=self.host
                try:
                    if not self.response(p).startswith('READY'):raise RuntimeError('Program nie zgłosił gotowości.')
                    if self.kind=='jack2':jack_connect(channels[:2],channels[2:])
                    self.audio_event('running','Audio działa. Możesz włączyć test efektu.',generation)
                    while not self.closing and not cancel.is_set() and p.poll() is None:
                        stats=json.loads(self.response(p,'STATS')[3:]);self.audio_event('stats',stats,generation)
                        for _ in range(10):
                            if self.closing or cancel.is_set() or p.poll() is not None:break
                            time.sleep(.1)
                    if not cancel.is_set() and not self.closing:raise RuntimeError('Program audio zatrzymał się niespodziewanie.')
                except Exception:
                    if p.poll() is None:self.terminate(p)
                    with self.process_lock:
                        if self.host is p:self.host=None
                    self.events.put(('log',(logdir/(self.kind+'.log')).read_text()[-4000:]))
                    if not cancel.is_set() and not self.closing:raise
                finally:
                    if p.poll() is not None:
                        p.stdin.close();p.stdout.close()
        self.background(work,generation)

    @staticmethod
    def terminate(p):
        if p is None:return
        if p.poll() is None:
            try:os.killpg(p.pid,signal.SIGTERM)
            except ProcessLookupError:pass
            try:p.wait(timeout=2)
            except subprocess.TimeoutExpired:
                try:os.killpg(p.pid,signal.SIGKILL)
                except ProcessLookupError:pass
                p.wait(timeout=2)

    def stop(self):
        with self.process_lock:
            self.audio_cancel.set();self.generation+=1
        self.set_state('stopping','Zatrzymywanie audio…')
        p=self.host
        def work():
            self.terminate(p)
            with self.process_lock:
                if self.host is p:self.host=None
            self.effect_active=False;self.effect_id=None
            self.events.put(('ready','Audio zatrzymane.'))
        self.background(work)

    def toggle_effect(self):
        p=self.host
        value=float(self.gain.get())
        generation=self.generation;active=self.effect_active;ident=self.effect_id
        self.effect_button.configure(state='disabled')
        def work():
            if active:
                self.response(p,f'REMOVE {ident}')
                self.audio_event('effect',(False,None),generation)
            else:
                path=str(self.target/'plugins/gain/plugin.so')
                new_ident=int(self.response(p,'ADD '+json.dumps(path,ensure_ascii=False))[3:])
                self.response(p,f'SET {new_ident} gain {value}')
                self.audio_event('effect',(True,new_ident),generation)
        self.background(work,generation)

    def poll(self):
        if self.closing:return
        while True:
            try:event=self.events.get_nowait()
            except queue.Empty:break
            if len(event)==3 and event[2]!=self.generation:continue
            kind,value=event[:2]
            if kind=='log':self.log(value)
            elif kind=='error':
                self.effect_active=False;self.effect_id=None
                self.last_error=value;self.set_state('error','Nie udało się wykonać operacji. '+value)
            elif kind=='stats':
                self.last_stats=value
                self.diagnostics.set(f"Bloki audio: {value['blocks']}   XRUN: {value['xruns']}   Błędy próbek: {value['bad_samples']}")
            elif kind=='effect':
                self.effect_active,self.effect_id=value
                self.set_state('running','Efekt włączony.' if self.effect_active else 'Efekt wyłączony — czysty sygnał.')
            else:self.set_state(kind,value)
        self.root.after(50,self.poll)

    def close(self):
        with self.process_lock:
            self.closing=True;self.generation+=1;self.audio_cancel.set();children=[self.build,self.host]
        for p in children:self.terminate(p)
        self.root.destroy()


def main():
    parser=argparse.ArgumentParser(description='Jerzy MultiFX Next — instalator i panel okienkowy')
    parser.add_argument('--bundle',type=Path)
    parser.add_argument('--prefix',type=Path)
    args=parser.parse_args()
    root=tk.Tk();App(root,bundle=args.bundle,prefix=args.prefix);root.mainloop()
if __name__=='__main__':main()
