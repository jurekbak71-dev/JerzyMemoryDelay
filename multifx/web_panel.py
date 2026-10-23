#!/usr/bin/env python3
"""Jerzy MultiFX mobile controller. Python 3 stdlib only; no cloud or router.
JACK callback never handles HTTP, sockets or disk access.
"""
import json
import math
import os
from pathlib import Path
import socket
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlsplit

BIND = os.environ.get("JERZY_MULTIFX_BIND", "10.42.0.1")
PORT = 8089
CONTROL = ("127.0.0.1", 9810)
FILE = Path(os.environ.get("JERZY_MULTIFX_DATA_DIR", str(Path.home() / ".config/jerzy-multifx"))) / "presets.json"
DEFAULT_PARAMS = [.50,.40,.50,.35,.65,.42,.33,.55,.50,.35,(350.-20.)/530.,.60,.70]
LOCK = threading.RLock()
state = {"params": DEFAULT_PARAMS[:], "enabled": [True]*4, "order": list(range(4)), "bpm":120.0}
presets = {}
active_preset = None

def valid_state(s):
    if not isinstance(s, dict):
        raise ValueError("Invalid state")
    params = s["params"]
    enabled = s["enabled"]
    order = s["order"]
    bpm = s["bpm"]
    if not isinstance(params, list) or len(params) != 13 or any(
            not isinstance(v,(int,float)) or isinstance(v,bool) or not math.isfinite(v) or not 0 <= v <= 1 for v in params):
        raise ValueError("Invalid parameter array")
    if not isinstance(enabled,list) or len(enabled)!=4 or any(type(v) is not bool for v in enabled):
        raise ValueError("Invalid enable array")
    if not isinstance(order,list) or sorted(order)!=list(range(4)) or any(type(v) is not int for v in order):
        raise ValueError("Invalid order")
    if not isinstance(bpm,(int,float)) or not math.isfinite(bpm) or not 20<=bpm<=300:
        raise ValueError("Invalid tempo")
    return {"params":list(params),"enabled":list(enabled),"order":list(order),"bpm":float(bpm)}

def load_file():
    global presets
    if not FILE.exists():
        return
    try:
        raw=json.loads(FILE.read_text(encoding="utf-8"))
        if not isinstance(raw,dict):
            raise ValueError("Expected object")
        safe={}
        for name,value in raw.items():
            if isinstance(name,str) and 0<len(name)<=64:
                safe[name]=valid_state(value)
        presets=safe
    except (OSError,ValueError,KeyError,TypeError) as err:
        print("Preset read error (leaving file unchanged):",err,flush=True)

def save_file():
    FILE.parent.mkdir(mode=0o700,parents=True,exist_ok=True)
    tmp=FILE.with_suffix(".json.tmp")
    with tmp.open("w",encoding="utf-8") as f:
        json.dump(presets,f,ensure_ascii=False,indent=2)
        f.flush()
        os.fsync(f.fileno())
    tmp.replace(FILE)

def send(message):
    with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as sock:
        sock.sendto(message.encode("ascii"),CONTROL)

def apply_to_engine(st):
    for i,v in enumerate(st["params"]):
        send("P %d %.9g" % (i,v))
    for i,v in enumerate(st["enabled"]):
        send("E %d %d" % (i,int(v)))
    send("O " + " ".join(str(i) for i in st["order"]))
    send("B %.9g" % st["bpm"])

PAGE=r"""<!doctype html>
<html lang="pl"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="theme-color" content="#161c26"><title>Jerzy MultiFX</title>
<style>
:root{color-scheme:dark;font:15px system-ui,-apple-system,Arial,sans-serif;background:#121721;color:#ecf0f6}
*{box-sizing:border-box}body{margin:0;padding:16px max(16px,env(safe-area-inset-right)) 30px;max-width:1100px;margin:auto}
header{display:flex;justify-content:space-between;align-items:center;gap:10px;margin:0 0 16px}
h1{margin:0;font-size:24px;letter-spacing:.5px}.muted{color:#aebaca;font-size:13px}.status{font-size:12px;color:#ffc77c}
section{background:#202b39;padding:14px;border-radius:14px;margin:12px 0}
.toolbar{display:flex;gap:8px;align-items:center;flex-wrap:wrap}
button,input,select{font:inherit;color:#fff}button,select,input[type=text]{background:#344354;border:1px solid #5e7189;border-radius:9px;padding:10px;min-height:42px}
button{cursor:pointer}button:disabled{opacity:.35}button.primary{background:#925f27;border-color:#c68b4d}
input[type=text]{flex:1;min-width:140px;width:100%}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(240px,1fr));gap:12px}
.effect{border:1px solid #425468;background:#202b39;padding:16px;border-radius:15px}
.effect.off{opacity:.68}.head{display:flex;align-items:center;justify-content:space-between;gap:8px}
.head strong{font-size:19px}.row{display:flex;gap:6px;align-items:center}
.slide{margin:17px 0}.slide label{display:flex;justify-content:space-between;gap:8px;font-size:13px}
.slide output{color:#ffca83;font-variant-numeric:tabular-nums}
input[type=range]{width:100%;margin:12px 0 0;accent-color:#f5b463;touch-action:pan-y}
select{max-width:100%}.minor{background:#2b3644;padding:7px;min-height:36px}
footer{color:#aebaca;font-size:12px;margin-top:16px}
</style></head><body>
<header><div><h1>JERZY MULTIFX</h1><div class="muted">Raspberry Pi 4B · HeadRush Core · JACK</div></div><span class="status" id="status">Łączenie...</span></header>
<section><div class="toolbar"><input type="text" id="presetName" maxlength="64" placeholder="Nazwa presetu">
<button class="primary" id="save">Zapisz preset</button></div>
<div class="toolbar" style="margin-top:8px"><select id="presets" aria-label="Presety"><option value="">Wybierz preset</option></select>
<button id="load">Wczytaj</button><button id="remove">Usuń</button></div></section>
<section class="toolbar">
<label>Tempo <strong id="bpmText">120</strong> BPM</label>
<button id="tap">TAP</button>
<input type="range" id="bpm" min="20" max="300" step="1" value="120" style="flex:1;min-width:120px">
</section>
<div id="effects" class="grid"></div>
<section><div class="slide"><label><span>MASTER LEVEL</span><output id="masterVal"></output></label>
<input type="range" id="master" min="0" max="1" step=".001"></div></section>
<footer>Kolejność efektów można zmieniać strzałkami ↑ ↓. Presety zapisują się w pamięci Raspberry Pi, nie na iPhonie.
Sieć Jerzy-MultiFX nie potrzebuje internetu. Zmiana kolejności podczas gry może powodować krótkie artefakty.</footer>
<script>
const names=['Compressor','Overdrive','Tremolo','Memory Delay'];
const specs=[
 [{i:0,n:'Threshold',f:v=>Math.round(-6-42*v)+' dB'},{i:1,n:'Ratio',f:v=>(1+9*v).toFixed(1)+':1'},{i:2,n:'Makeup',f:v=>Math.round(-6+12*v)+' dB'}],
 [{i:3,n:'Gain',f:v=>(1+35*v).toFixed(1)+'×'},{i:4,n:'Tone',f:v=>Math.round(350+9000*v)+' Hz'},{i:5,n:'Output',f:v=>Math.round(100*(.08+1.55*v))+'%'}],
 [{i:6,n:'Rate',f:v=>(.3+11.7*v).toFixed(2)+' Hz'},{i:7,n:'Depth',f:v=>Math.round(100*v)+'%'}],
 [{i:8,n:'Blend',f:v=>Math.round(100*v)+'%'},{i:9,n:'Feedback',f:v=>Math.round(100*v)+'%'},{i:10,n:'Time',f:v=>Math.round(20+530*v)+' ms'},
 {i:11,n:'BBD Model',f:v=>['EH7500 / SAD','Issue A / SAD','Squelch / SAD','Deluxe / MN','Boy / BL','Toy / BL'][Math.round(5*v)]}]
];
let S=null,taps=[],pending=new Map();
const $=s=>document.querySelector(s), effectRoot=$('#effects');
async function api(path,data){
 try{
  const r=await fetch('/api/'+path,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(data)});
  if(!r.ok)throw Error(await r.text());
  $('#status').textContent='Połączono';
  return await r.json();
 }catch(e){$('#status').textContent='Błąd';alert('Błąd komunikacji: '+e.message);throw e}
}
function queueParam(i,v){
 S.params[i]=v;
 pending.set(i,v);
 clearTimeout(queueParam.timer);
 queueParam.timer=setTimeout(async()=>{
  const batch=[...pending.entries()];pending.clear();
  for(const [param,value] of batch){try{await api('param',{param,value})}catch(e){break}}
 },65);
}
function draw(){
 effectRoot.replaceChildren();
 for(let position=0;position<4;position++){
  const id=S.order[position],box=document.createElement('article');
  box.className='effect'+(S.enabled[id]?'':' off');
  const head=document.createElement('div');head.className='head';
  const title=document.createElement('strong');title.textContent=(position+1)+'. '+names[id];
  const actions=document.createElement('div');actions.className='row';
  for(const [symbol,delta] of [['↑',-1],['↓',1]]){
   const b=document.createElement('button');b.className='minor';b.textContent=symbol;b.disabled=position+delta<0||position+delta>=4;
   b.onclick=async()=>{const copy=S.order.slice();[copy[position],copy[position+delta]]=[copy[position+delta],copy[position]];await api('order',{order:copy});S.order=copy;draw()};
   actions.append(b);
  }
  const enable=document.createElement('button');enable.className='minor';enable.textContent=S.enabled[id]?'ON':'BYPASS';
  enable.onclick=async()=>{const value=!S.enabled[id];await api('enabled',{effect:id,value});S.enabled[id]=value;draw()};
  actions.append(enable);head.append(title,actions);box.append(head);
  for(const param of specs[id]){
   const div=document.createElement('div');div.className='slide';
   const label=document.createElement('label');const out=document.createElement('output');out.textContent=param.f(S.params[param.i]);
   const name=document.createElement('span');name.textContent=param.n;label.append(name,out);
   const range=document.createElement('input');range.type='range';range.min='0';range.max='1';range.step=param.i===11?'.2':'.001';range.value=S.params[param.i];
   range.oninput=()=>{const v=Number(range.value);out.textContent=param.f(v);queueParam(param.i,v)};
   div.append(label,range);box.append(div);
  }
  effectRoot.append(box);
 }
 $('#master').value=S.params[12];$('#masterVal').textContent=Math.round(140*S.params[12])+'%';
 $('#bpm').value=S.bpm;$('#bpmText').textContent=Math.round(S.bpm);
}
function refreshPresets(){
 const select=$('#presets');const old=select.value;select.replaceChildren();
 const first=document.createElement('option');first.value='';first.textContent='Wybierz preset';select.append(first);
 for(const n of S.presets){const option=document.createElement('option');option.value=n;option.textContent=n;select.append(option)}
 select.value=S.presets.includes(old)?old:(S.active_preset||'');
}
$('#master').oninput=e=>{const v=Number(e.target.value);$('#masterVal').textContent=Math.round(v*140)+'%';queueParam(12,v)};
$('#bpm').oninput=e=>{$('#bpmText').textContent=e.target.value};
$('#bpm').onchange=async e=>{const value=Number(e.target.value);await api('bpm',{value});S.bpm=value};
$('#tap').onclick=async()=>{const now=performance.now();taps.push(now);taps=taps.filter(t=>now-t<3000).slice(-5);if(taps.length>1){const intervals=taps.slice(1).map((t,i)=>t-taps[i]);const value=Math.min(300,Math.max(20,Math.round(60000/(intervals.reduce((a,b)=>a+b,0)/intervals.length))));await api('bpm',{value});S.bpm=value;$('#bpm').value=value;$('#bpmText').textContent=value}};
$('#save').onclick=async()=>{const name=$('#presetName').value.trim();if(!name)return alert('Podaj nazwę presetu');const v=await api('presets/save',{name});S.presets=v.presets;S.active_preset=name;refreshPresets()};
$('#load').onclick=async()=>{const name=$('#presets').value;if(!name)return;S=await api('presets/load',{name});$('#presetName').value=name;draw();refreshPresets()};
$('#remove').onclick=async()=>{const name=$('#presets').value;if(!name||!confirm('Usunąć preset '+name+'?'))return;const v=await api('presets/delete',{name});S.presets=v.presets;S.active_preset=null;refreshPresets()};
(async()=>{try{const r=await fetch('/api/state',{cache:'no-store'});if(!r.ok)throw Error(r.status);S=await r.json();draw();refreshPresets();$('#status').textContent='Połączono'}catch(e){$('#status').textContent='Brak połączenia'}})();
</script></body></html>
"""

class Handler(BaseHTTPRequestHandler):
    protocol_version="HTTP/1.1"
    def response(self,code,body,btype="application/json; charset=utf-8"):
        if not isinstance(body,bytes):
            body=json.dumps(body,ensure_ascii=False).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type",btype)
        self.send_header("Content-Length",str(len(body)))
        self.send_header("Cache-Control","no-store")
        self.send_header("X-Content-Type-Options","nosniff")
        self.end_headers()
        self.wfile.write(body)
    def snapshot(self):
        return {**valid_state(state),"presets":sorted(presets),"active_preset":active_preset}
    def do_GET(self):
        if urlsplit(self.path).path == "/":
            self.response(200,PAGE.encode("utf-8"),"text/html; charset=utf-8")
        elif urlsplit(self.path).path == "/api/state":
            with LOCK:self.response(200,self.snapshot())
        else:self.send_error(404)
    def do_POST(self):
        global active_preset, state
        route=urlsplit(self.path).path
        if not route.startswith("/api/"):
            self.send_error(404);return
        origin=self.headers.get("Origin")
        if origin and urlsplit(origin).netloc != self.headers.get("Host",""):
            self.send_error(403);return
        if self.headers.get("Content-Type","").split(";",1)[0].strip().lower()!="application/json":
            self.send_error(415);return
        try:
            count=int(self.headers.get("Content-Length","0"))
            if not 0<count<=4096:
                raise ValueError("Invalid request size")
            data=json.loads(self.rfile.read(count))
            if not isinstance(data,dict):
                raise ValueError("Expected JSON object")
            with LOCK:
                if route=="/api/param":
                    i=data["param"];v=data["value"]
                    if type(i) is not int or not 0<=i<13 or type(v) not in (int,float) or not math.isfinite(v) or not 0<=v<=1:
                        raise ValueError("Invalid parameter")
                    send("P %d %.9g"%(i,v));state["params"][i]=float(v);active_preset=None
                elif route=="/api/enabled":
                    i=data["effect"];v=data["value"]
                    if type(i) is not int or not 0<=i<4 or type(v) is not bool:raise ValueError("Invalid toggle")
                    send("E %d %d"%(i,int(v)));state["enabled"][i]=v;active_preset=None
                elif route=="/api/order":
                    order=data["order"]
                    if type(order) is not list or len(order)!=4 or any(type(v) is not int for v in order) or sorted(order)!=[0,1,2,3]:
                        raise ValueError("Invalid order")
                    send("O "+" ".join(str(i) for i in order));state["order"]=order[:];active_preset=None
                elif route=="/api/bpm":
                    v=data["value"]
                    if type(v) not in (float,int) or not math.isfinite(v) or not 20<=v<=300:raise ValueError("Invalid tempo")
                    send("B %.9g"%v);state["bpm"]=float(v);active_preset=None
                elif route=="/api/presets/save":
                    name=data["name"]
                    if type(name) is not str or not 0<len(name.strip())<=64:raise ValueError("Invalid name")
                    name=name.strip();presets[name]=valid_state(state);save_file();active_preset=name
                elif route=="/api/presets/load":
                    name=data["name"]
                    if type(name) is not str or name not in presets:raise ValueError("Unknown preset")
                    copy=valid_state(presets[name]);apply_to_engine(copy);state=copy;active_preset=name
                elif route=="/api/presets/delete":
                    name=data["name"]
                    if type(name) is not str or name not in presets:raise ValueError("Unknown preset")
                    del presets[name];save_file()
                    if active_preset==name:active_preset=None
                else:
                    self.send_error(404);return
                self.response(200,self.snapshot())
        except (TypeError,ValueError,KeyError,IndexError,UnicodeError,json.JSONDecodeError) as error:
            self.response(400,{"error":str(error)})
        except OSError as error:
            self.response(503,{"error":"Control unavailable: "+str(error)})
    def log_message(self,*args):
        pass

if __name__=="__main__":
    load_file()
    print("Jerzy MultiFX mobile panel http://%s:%s"%(BIND,PORT),flush=True)
    ThreadingHTTPServer((BIND,PORT),Handler).serve_forever()
