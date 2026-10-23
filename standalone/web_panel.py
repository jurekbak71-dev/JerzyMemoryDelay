#!/usr/bin/env python3
"""Local-network browser controller for Jerzy Memory Delay JACK.
Requires only the Python 3 standard library. Run alongside JerzyMemoryDelayJACK.
"""
from http.server import ThreadingHTTPServer, BaseHTTPRequestHandler
from urllib.parse import parse_qs
import json
import math
import socket
import time

HOST = "0.0.0.0"
PORT = 8088
UDP_ADDR = ("127.0.0.1", 9808)
VALUES = {str(i): v for i, v in enumerate(
    [.4, .5, .35, .15, .18, .55, .6, 0., 0., 0., .35, 1., 0., .3])}
VALUES["-1"] = 120.
LAST_TAPS = []

PAGE = r"""<!doctype html><html lang="pl"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Jerzy Memory Delay</title>
<style>
:root{color-scheme:dark;font-family:system-ui,Arial,sans-serif;background:#14181d;color:#e8e8e8}
body{max-width:940px;margin:auto;padding:18px}
h1{font-size:25px;margin:8px 0} .small{color:#b0b9c3;font-size:13px}
header{display:flex;justify-content:space-between;gap:12px;align-items:center}
select,input,button{accent-color:#f3b65a}select,button{background:#282f38;color:white;border:1px solid #5b6570;border-radius:7px;padding:10px;font-size:15px}
#controls{display:grid;grid-template-columns:repeat(auto-fit,minmax(205px,1fr));gap:13px;margin-top:22px}
.control{padding:14px;background:#222932;border-radius:12px}
label{display:block;font-size:14px;margin-bottom:12px}
input[type=range]{width:100%}
.value{float:right;color:#f3b65a}button{cursor:pointer}
.actions{display:flex;gap:10px;flex-wrap:wrap;margin-top:20px}
button.on{background:#835524}
select{width:100%;margin-top:8px}
</style>
<header><div><h1>JERZY MEMORY DELAY</h1><div class="small">Raspberry Pi 4B · JACK · 6 modeli BBD</div></div><span id="status" class="small">Panel gotowy</span></header>
<label style="margin-top:20px">Model BBD<select id="model"></select></label>
<div id="controls"></div>
<div class="actions"><button id="bypass">Bypass: OFF</button><button id="mod">Modulation: ON</button><button id="sync">Tempo Sync: OFF</button><button id="tap">TAP TEMPO</button></div>
<p class="small">Przy zmianie modelu ogon jest wygaszany zgodnie z oryginalnym DSP. Ustawienia nie są jeszcze zapisywane po restarcie. Panel musi działać w tej samej sieci co Raspberry Pi.</p>
<script>
const names=['EH7500 / SAD','Issue A / SAD','Squelch / SAD','Deluxe / MN','Boy / BL','Toy / BL'];
const specs=[
 [0,'Blend','%',v=>Math.round(v*100)],
 [1,'Level','%',v=>Math.round(v*100)],
 [2,'Feedback','%',v=>Math.round(v*100)],
 [3,'Modulation Rate','Hz',v=>(.15+v*7.85).toFixed(2)],
 [4,'Depth','%',v=>Math.round(v*100)],
 [5,'Delay Time','ms',v=>Math.round(20+530*v)],
 [9,'Boost','%',v=>Math.round(v*100)],
 [10,'Squelch','%',v=>Math.round(v*100)],
 [13,'Note Division','',v=>['1/1','1/1 D','1/1 T','1/2','1/2 D','1/2 T','1/4','1/4 D','1/4 T','1/8','1/8 D','1/8 T','1/16','1/16 D','1/16 T','1/32','1/32 D','1/32 T','1/64','1/64 D','1/64 T'][Math.round(v*20)]],
 [-1,'Tempo','BPM',v=>Math.round(v)]
];
const defaults={'0':.4,'1':.5,'2':.35,'3':.15,'4':.18,'5':.55,'6':.6,'8':0,'9':0,'10':.35,'11':1,'12':0,'13':.3,'-1':120};
let vals={...defaults},pending={},timer=null,taps=[];
const select=document.getElementById('model');
names.forEach((name,i)=>{let o=document.createElement('option');o.value=i;o.textContent=name;select.appendChild(o)});
select.value='3';
function send(id,v){
 vals[id]=v;pending[id]=v;
 if(timer===null)timer=setTimeout(async()=>{
  const batch=pending;pending={};timer=null;
  for(const [key,value] of Object.entries(batch)){
   try{const r=await fetch('/set',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({id:key,value:String(value)})});if(!r.ok)throw Error(r.status);document.getElementById('status').textContent='Połączono';}
   catch(e){document.getElementById('status').textContent='Błąd: '+e.message;}
  }
 },35);
}
select.onchange=()=>send(6,Number(select.value)/5);
const root=document.getElementById('controls');
for(const [id,name,unit,format] of specs){
 const el=document.createElement('div');el.className='control';
 const lab=document.createElement('label');lab.textContent=name;
 const output=document.createElement('span');output.className='value';
 lab.append(output);
 const slider=document.createElement('input');slider.type='range';slider.min=0;slider.max=(id===-1?1000:1);slider.step=(id===-1?1:.001);
 slider.value=vals[id];if(id===-1)slider.min=10;
 const draw=()=>output.textContent=format(Number(slider.value))+' '+unit;
 slider.oninput=()=>{draw();send(id,Number(slider.value))};
 draw();el.append(lab,slider);root.appendChild(el);
}
function toggle(button,id,name){
 const el=document.getElementById(button);
 el.onclick=()=>{const value=vals[id]>=.5?0:1;send(id,value);el.textContent=name+': '+(value?'ON':'OFF');el.classList.toggle('on',!!value)};
}
toggle('bypass',8,'Bypass');toggle('mod',11,'Modulation');toggle('sync',12,'Tempo Sync');
document.getElementById('tap').onclick=()=>{
 const now=performance.now();taps.push(now);taps=taps.filter(t=>now-t<3000).slice(-5);
 if(taps.length>1){const intervals=taps.slice(1).map((t,i)=>t-taps[i]);const bpm=Math.round(60000/(intervals.reduce((a,b)=>a+b,0)/intervals.length));if(bpm>=10&&bpm<=1000){send(-1,bpm);const slider=root.querySelector('.control:last-child input');slider.value=bpm;slider.dispatchEvent(new Event('input'));}}
};
</script></html>"""

class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path != "/":
            self.send_error(404)
            return
        body=PAGE.encode()
        self.send_response(200)
        self.send_header("Content-Type","text/html; charset=utf-8")
        self.send_header("Cache-Control","no-store")
        self.send_header("Content-Length",str(len(body)))
        self.end_headers()
        self.wfile.write(body)
    def do_POST(self):
        if self.path!="/set":
            self.send_error(404)
            return
        try:
            size=int(self.headers.get("Content-Length","0"))
            if not 0<size<512:
                raise ValueError("Invalid request length")
            data=parse_qs(self.rfile.read(size).decode())
            ident=int(data["id"][0])
            value=float(data["value"][0])
            if not math.isfinite(value) or (ident!=-1 and not (0<=ident<=13 and 0<=value<=1)) or (ident==-1 and not 10<=value<=1000):
                raise ValueError("Invalid control value")
            with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as udp:
                udp.sendto(f"{ident} {value:.9g}".encode(),UDP_ADDR)
            VALUES[str(ident)]=value
            self.send_response(204)
            self.end_headers()
        except (ValueError,KeyError,IndexError,UnicodeError):
            self.send_error(400)
    def log_message(self,*args):
        pass

if __name__=="__main__":
    print(f"Jerzy Memory Delay panel: http://bassambient.local:{PORT}",flush=True)
    ThreadingHTTPServer((HOST,PORT),Handler).serve_forever()
