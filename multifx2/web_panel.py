#!/usr/bin/env python3
"""MultiFX v0.2: browser API, installed module discovery, presets and restart recovery.
Only localhost TCP talks to JACK engine; browser binds to the Pi hotspot.
"""
from copy import deepcopy
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlsplit
import json
import math
import os
import re
import socket
import threading
import time

HOST=os.environ.get("JERZY_MULTIFX2_BIND","10.42.0.1")
PORT=int(os.environ.get("JERZY_MULTIFX2_PORT","8090"))
BASE=Path(__file__).resolve().parent
MODULE_DIR=Path(os.environ.get("JERZY_MULTIFX2_MODULE_DIR","/home/jerzy/JerzyMultiFX2/build-multifx2/modules"))
DATA=Path(os.environ.get("JERZY_MULTIFX2_DATA_DIR",str(Path.home()/".config/jerzy-multifx2")))
PRESETS=DATA/"presets.json"
LAST=DATA/"last_state.json"
OLD=Path.home()/".config/jerzy-multifx/presets.json"
CONTROL=("127.0.0.1",9811)
LOCK=threading.RLock()
NAME=re.compile(r"^[a-z0-9_]{1,48}$")
state={"chain":[],"bpm":120.,"master":.7}
presets={}
current_preset=None

def rpc(line):
    with socket.create_connection(CONTROL,timeout=2) as sock:
        sock.settimeout(2)
        sock.sendall((line+"\n").encode("ascii"))
        result=sock.recv(512).decode("utf-8","replace").strip()
    if result!="OK" and not result.startswith("OK "):
        raise RuntimeError(result or "JACK engine unavailable")
    return result[3:] if result.startswith("OK ") else ""

def catalog():
    result={}
    if not MODULE_DIR.is_dir():return result
    for path in sorted(MODULE_DIR.glob("*.json")):
        key=path.stem
        if not NAME.fullmatch(key) or not (MODULE_DIR/(key+".so")).is_file():
            continue
        try:
            meta=json.loads(path.read_text(encoding="utf-8"))
            params=meta["params"]
            if (meta.get("key")!=key or not isinstance(meta.get("name"),str) or
                not isinstance(meta.get("category"),str) or type(params) is not list or
                not 0<=len(params)<=48):continue
            valid=True
            for p in params:
                if (not isinstance(p,dict) or not isinstance(p.get("key"),str) or
                    not isinstance(p.get("label"),str) or
                    type(p.get("def")) not in (float,int) or not math.isfinite(p["def"]) or
                    not 0<=p["def"]<=1 or p.get("kind") not in ("range","switch","choice")):
                    valid=False;break
                if p["kind"]=="choice" and (type(p.get("choices")) is not list or len(p["choices"])<2):valid=False;break
                if p["kind"]=="range" and (type(p.get("min")) not in (float,int) or
                    type(p.get("max")) not in (float,int)):valid=False;break
            if valid:result[key]=meta
        except (OSError,ValueError,TypeError,KeyError):
            pass
    return result

def clean_snapshot(data,installed=None):
    if installed is None:installed=catalog()
    if type(data) is not dict or type(data.get("chain")) is not list or len(data["chain"])>16:
        raise ValueError("Invalid chain")
    chain=[]
    for entry in data["chain"]:
        if not isinstance(entry,dict) or entry.get("key") not in installed:raise ValueError("Module not installed")
        key=entry["key"];params=entry.get("params")
        if type(params) is not list or len(params)!=len(installed[key]["params"]):raise ValueError("Parameter count mismatch")
        if any(type(v) not in (int,float) or not math.isfinite(v) or not 0<=v<=1 for v in params):
            raise ValueError("Invalid parameter values")
        if type(entry.get("enabled")) is not bool:raise ValueError("Invalid bypass")
        chain.append({"key":key,"enabled":entry["enabled"],"params":list(params)})
    bpm=data.get("bpm",120)
    master=data.get("master",.7)
    if type(bpm) not in (int,float) or not math.isfinite(bpm) or not 20<=bpm<=300:raise ValueError("Invalid BPM")
    if type(master) not in (int,float) or not math.isfinite(master) or not 0<=master<=1:raise ValueError("Invalid master")
    return {"chain":chain,"bpm":float(bpm),"master":float(master)}

def save_json(path,value):
    path.parent.mkdir(mode=0o700,parents=True,exist_ok=True)
    tmp=path.with_suffix(path.suffix+".tmp")
    with tmp.open("w",encoding="utf-8") as file:
        json.dump(value,file,ensure_ascii=False,indent=2)
        file.flush();os.fsync(file.fileno())
    tmp.replace(path)

def store():
    save_json(LAST,clean_snapshot(state))

def restore(data):
    global state
    snapshot=clean_snapshot(data)
    rpc("CLEAR")
    chain=[]
    for item in snapshot["chain"]:
        slot_id=int(rpc("ADD "+item["key"]))
        for index,value in enumerate(item["params"]):
            rpc("SET %d %d %.9g"%(slot_id,index,value))
        rpc("ENABLE %d %d"%(slot_id,int(item["enabled"])))
        chain.append({**deepcopy(item),"id":slot_id})
    rpc("BPM %.9g"%snapshot["bpm"])
    rpc("MASTER %.9g"%snapshot["master"])
    state={"chain":chain,"bpm":snapshot["bpm"],"master":snapshot["master"]}

def import_old():
    if not OLD.is_file():return
    try:
        raw=json.loads(OLD.read_text(encoding="utf-8"))
        installed=catalog()
        keys=["compressor","overdrive","tremolo","memory_delay"]
        for name,saved in raw.items():
            if type(name) is not str or not name or len(name)>64 or name in presets:continue
            try:
                values,enabled,order=saved["params"],saved["enabled"],saved["order"]
                if len(values)!=13 or len(enabled)!=4 or sorted(order)!=[0,1,2,3]:continue
                chain=[]
                for index in order:
                    key=keys[index]
                    values_new=[p["def"] for p in installed[key]["params"]]
                    if index==0:values_new[:3]=values[:3]
                    elif index==1:values_new[:3]=values[3:6]
                    elif index==2:values_new[:2]=values[6:8]
                    elif index==3:
                        for dst,src in ((0,8),(2,9),(5,10),(6,11)):values_new[dst]=values[src]
                    chain.append({"key":key,"params":values_new,"enabled":bool(enabled[index])})
                presets[name]=clean_snapshot({"chain":chain,"bpm":saved["bpm"],"master":values[12]})
            except (KeyError,TypeError,IndexError,ValueError):
                continue
        if presets:save_json(PRESETS,presets)
    except (OSError,ValueError,TypeError) as error:
        print("Legacy preset import unavailable:",error,flush=True)

def startup():
    global presets
    for _ in range(40):
        try:rpc("PING");break
        except (OSError,RuntimeError):time.sleep(.25)
    else:raise RuntimeError("JACK MultiFX v0.2 not responding")
    if PRESETS.exists():
        try:
            raw=json.loads(PRESETS.read_text(encoding="utf-8"))
            if isinstance(raw,dict):
                presets={name:clean_snapshot(value) for name,value in raw.items()
                         if type(name) is str and 0<len(name)<=64}
        except (OSError,ValueError,TypeError):
            print("Presets file invalid; left unchanged",flush=True)
    else:import_old()
    installed=catalog()
    initial={"chain":[{"key":"memory_delay","enabled":True,
                       "params":[p["def"] for p in installed["memory_delay"]["params"]]}],
             "bpm":120.,"master":.7}
    if LAST.is_file():
        try:initial=clean_snapshot(json.loads(LAST.read_text(encoding="utf-8")))
        except (OSError,ValueError,TypeError) as err:print("Restore default delay:",err,flush=True)
    restore(initial)

class Handler(BaseHTTPRequestHandler):
    def respond(self,status,body,mime="application/json; charset=utf-8"):
        if not isinstance(body,bytes):body=json.dumps(body,ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type",mime)
        self.send_header("Content-Length",str(len(body)))
        self.send_header("Cache-Control","no-store")
        self.send_header("X-Content-Type-Options","nosniff")
        self.end_headers()
        self.wfile.write(body)
    def snapshot(self):
        return {**deepcopy(state),"catalog":catalog(),
                "presets":sorted(presets),"active_preset":current_preset}
    def do_GET(self):
        path=urlsplit(self.path).path
        if path=="/":
            self.respond(200,(BASE/"panel.html").read_bytes(),"text/html; charset=utf-8")
        elif path=="/api/state":
            with LOCK:self.respond(200,self.snapshot())
        else:self.send_error(404)
    def do_POST(self):
        global current_preset
        path=urlsplit(self.path).path
        if not path.startswith("/api/"):self.send_error(404);return
        origin=self.headers.get("Origin")
        if origin and urlsplit(origin).netloc!=self.headers.get("Host",""):
            self.send_error(403);return
        if self.headers.get("Content-Type","").split(";")[0].strip().lower()!="application/json":
            self.send_error(415);return
        try:
            count=int(self.headers.get("Content-Length","0"))
            if not 0<count<=4096:raise ValueError("Bad request length")
            data=json.loads(self.rfile.read(count))
            if type(data) is not dict:raise ValueError("Bad request")
            with LOCK:
                if path=="/api/add":
                    key=data.get("key")
                    if type(key) is not str or key not in catalog() or len(state["chain"])>=16:raise ValueError("Unknown module or full chain")
                    ident=int(rpc("ADD "+key))
                    state["chain"].append({"id":ident,"key":key,"enabled":True,
                        "params":[float(p["def"]) for p in catalog()[key]["params"]]})
                    current_preset=None
                elif path=="/api/remove":
                    ident=data.get("id")
                    slot=next((s for s in state["chain"] if type(ident) is int and s["id"]==ident),None)
                    if slot is None:raise ValueError("Unknown instance")
                    rpc("REMOVE %d"%ident)
                    state["chain"].remove(slot);current_preset=None
                elif path=="/api/move":
                    ident,pos=data.get("id"),data.get("position")
                    if type(ident) is not int or type(pos) is not int or not 0<=pos<len(state["chain"]):raise ValueError("Bad move")
                    slot=next((s for s in state["chain"] if s["id"]==ident),None)
                    if slot is None:raise ValueError("Unknown instance")
                    rpc("MOVE %d %d"%(ident,pos))
                    state["chain"].remove(slot);state["chain"].insert(pos,slot);current_preset=None
                elif path=="/api/enable":
                    ident,value=data.get("id"),data.get("value")
                    if type(ident) is not int or type(value) is not bool:raise ValueError("Invalid enable")
                    slot=next((s for s in state["chain"] if s["id"]==ident),None)
                    if slot is None:raise ValueError("Unknown instance")
                    rpc("ENABLE %d %d"%(ident,int(value)))
                    slot["enabled"]=value;current_preset=None
                elif path=="/api/param":
                    ident,index,value=data.get("id"),data.get("index"),data.get("value")
                    slot=next((s for s in state["chain"] if type(ident) is int and s["id"]==ident),None)
                    if (slot is None or type(index) is not int or not 0<=index<len(slot["params"]) or
                        type(value) not in (float,int) or not math.isfinite(value) or not 0<=value<=1):
                        raise ValueError("Invalid parameter")
                    rpc("SET %d %d %.9g"%(ident,index,value))
                    slot["params"][index]=float(value);current_preset=None
                elif path=="/api/bpm":
                    value=data.get("value")
                    if type(value) not in (int,float) or not math.isfinite(value) or not 20<=value<=300:raise ValueError("Bad tempo")
                    rpc("BPM %.9g"%value);state["bpm"]=float(value);current_preset=None
                elif path=="/api/master":
                    value=data.get("value")
                    if type(value) not in (int,float) or not math.isfinite(value) or not 0<=value<=1:raise ValueError("Bad master")
                    rpc("MASTER %.9g"%value);state["master"]=float(value);current_preset=None
                elif path=="/api/preset/save":
                    name=data.get("name")
                    if type(name) is not str or not 0<len(name.strip())<=64:raise ValueError("Invalid preset name")
                    name=name.strip();presets[name]=clean_snapshot(state)
                    save_json(PRESETS,presets);current_preset=name
                elif path=="/api/preset/load":
                    name=data.get("name")
                    if name not in presets:raise ValueError("Preset unavailable")
                    restore(presets[name]);current_preset=name
                elif path=="/api/preset/delete":
                    name=data.get("name")
                    if name not in presets:raise ValueError("Preset unavailable")
                    del presets[name];save_json(PRESETS,presets)
                    if current_preset==name:current_preset=None
                else:self.send_error(404);return
                if path!="/api/preset/delete":store()
                self.respond(200,self.snapshot())
        except (ValueError,KeyError,TypeError,IndexError) as err:
            self.respond(400,{"error":str(err)})
        except (OSError,RuntimeError) as err:
            self.respond(503,{"error":str(err)})
    def log_message(self,*args):pass

if __name__=="__main__":
    startup()
    print("Jerzy MultiFX v0.2 panel: http://%s:%s/"%(HOST,PORT),flush=True)
    ThreadingHTTPServer((HOST,PORT),Handler).serve_forever()
