#!/usr/bin/env python3
"""Opt-in host measurement; no hardware is opened without --run."""
import argparse
import json
import math
import os
import pathlib
import platform
import select
import subprocess
import time

def quantile_bound(hist, q, width=1000):
 if len(hist)!=4096 or any(type(x) is not int or x<0 for x in hist):
  return dict(lower_ns=None,upper_ns=None,reason='invalid histogram')
 total=sum(hist)
 if not total:return dict(lower_ns=None,upper_ns=None,reason='no samples')
 rank=math.ceil(q*total);count=0
 for i,n in enumerate(hist):
  count+=n
  if count>=rank:return dict(lower_ns=i*width,upper_ns=(i+1)*width if i<4095 else None,reason='overflow bin censored' if i==4095 else None)

def ticks(path):
 data=path.read_text();fields=data[data.rfind(')')+2:].split();return int(fields[11])+int(fields[12])

def proc_sample(pid):
 root=pathlib.Path('/proc')/str(pid);r={'monotonic':time.monotonic(),'cpu_ticks':None,'rss_bytes':None,'pss_bytes':None,'threads':{},'unavailable':[]}
 try:r['cpu_ticks']=ticks(root/'stat');r['rss_bytes']=int((root/'statm').read_text().split()[1])*os.sysconf('SC_PAGE_SIZE')
 except (OSError,ValueError,IndexError) as e:r['unavailable'].append(str(e))
 try:
  for line in (root/'smaps_rollup').read_text().splitlines():
   if line.startswith('Pss:'):r['pss_bytes']=int(line.split()[1])*1024
 except (OSError,ValueError) as e:r['unavailable'].append(str(e))
 try:
  for t in (root/'task').iterdir():
   try:r['threads'][t.name]={'ticks':ticks(t/'stat'),'name':(t/'comm').read_text().strip()}
   except (OSError,ValueError):pass
 except OSError as e:r['unavailable'].append(str(e))
 return r

def cpu_delta(a,b):
 elapsed=b['monotonic']-a['monotonic'];hz=os.sysconf('SC_CLK_TCK')
 def percent(x,y):return 100*(y-x)/hz/elapsed if x is not None and y is not None and y>=x and elapsed>0 else None
 return {'one_core_percent':percent(a['cpu_ticks'],b['cpu_ticks']),'thread_one_core_percent':{k:percent(a['threads'].get(k,{}).get('ticks'),v['ticks']) for k,v in b['threads'].items()}}

def hist_delta(a,b,key='callback_histogram'):
 x=a.get(key);y=b.get(key);width=b.get(key+'_width_ns')
 if not isinstance(x,list) or not isinstance(y,list) or len(x)!=4096 or len(y)!=4096 or width!=1000:return {'counts':None,'p95':None,'p99':None,'reason':'histogram unavailable or incompatible'}
 if any(type(i) is not int or type(j) is not int or j<i or i<0 for i,j in zip(x,y)):return {'counts':None,'p95':None,'p99':None,'reason':'invalid or reset histogram'}
 d=[j-i for i,j in zip(x,y)];return {'counts':d,'width_ns':width,'p95':quantile_bound(d,.95,width),'p99':quantile_bound(d,.99,width),'reason':None}

def read_line(proc,timeout=15):
 # Unbuffered binary pipe avoids select versus TextIO read-ahead races.
 result=bytearray();end=time.monotonic()+timeout
 while time.monotonic()<end:
  if select.select([proc.stdout],[],[],max(0,end-time.monotonic()))[0]:
   c=os.read(proc.stdout.fileno(),4096)
   if not c:raise RuntimeError('host exited before response')
   result+=c
   if b'\n' in result:
    line,extra=result.split(b'\n',1)
    if extra:raise RuntimeError('unexpected unsolicited host control output')
    return line.decode()
 raise TimeoutError('host control response timed out')

def command(proc,text):
 proc.stdin.write((text+'\n').encode());proc.stdin.flush();line=read_line(proc)
 if not line.startswith('OK'):raise RuntimeError(line)
 return json.loads(line[3:]) if line.startswith('OK {') else line

def main(argv=None):
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--run',action='store_true');p.add_argument('--host',required=True);p.add_argument('--backend',choices=['jack','alsa'],required=True)
 p.add_argument('--rate',type=int,default=44100);p.add_argument('--period',type=int,default=64);p.add_argument('--periods',type=int,default=3);p.add_argument('--device',default='hw:CARD=Core,DEV=0');p.add_argument('--duration',type=float,default=60);p.add_argument('--interval',type=float,default=1);p.add_argument('--preset');p.add_argument('--commands',help='JSON list of startup control command strings');p.add_argument('--output',default='benchmark-rpi.json');a=p.parse_args(argv)
 if not math.isfinite(a.duration) or not math.isfinite(a.interval) or a.duration<=0 or a.interval<=0:p.error('duration and interval must be positive')
 cmd=[a.host]
 if a.backend=='alsa':cmd+=['--device',a.device,'--rate',str(a.rate),'--period',str(a.period),'--periods',str(a.periods),'--capture-channels','6','--playback-channels','4']
 cpuinfo=pathlib.Path('/proc/cpuinfo').read_text() if pathlib.Path('/proc/cpuinfo').exists() else ''
 model_path=pathlib.Path('/proc/device-tree/model');model=model_path.read_bytes().decode(errors='replace').strip('\0') if model_path.exists() else None
 report={'schema':1,'command':cmd,'requested':vars(a),'machine':platform.machine(),'model':model,'cpuinfo':cpuinfo,'raspberry_pi':bool(model and 'Raspberry Pi' in model),'core_interface_verified':None,'round_trip':None,'round_trip_reason':'No synchronous physical loopback captured by this tool','sample_count':0,'sample_file':a.output+'.samples.jsonl','errors':[]}
 if not a.run:print(json.dumps({'dry_run':True,'command':cmd,'note':'Pass --run to open audio hardware. JACK server rate/buffer must be configured externally.'},indent=2));return 0
 startup=[]
 if a.preset:startup.append('LOAD '+json.dumps(str(pathlib.Path(a.preset).resolve())))
 if a.commands:
  loaded=json.loads(pathlib.Path(a.commands).read_text())
  if not isinstance(loaded,list):p.error('commands must be a JSON list')
  startup+=loaded
  if any(not isinstance(c,str) or '\n' in c or '\r' in c for c in startup):p.error('commands must be a JSON list of single-line strings')
 proc=None;stderr=open(a.output+'.stderr','wb');sample_file=open(report['sample_file'],'w');first_sample=None;previous=None
 try:
  proc=subprocess.Popen(cmd,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=stderr,bufsize=0);report['ready']=read_line(proc)
  if not report['ready'].startswith('READY '):raise RuntimeError(report['ready'])
  report['startup_responses']=[]
  for c in startup:
   response=command(proc,c);report['startup_responses'].append({'command':c,'response':response})
   settled_by=time.monotonic()+15
   while command(proc,'STATS').get('pending',False):
    if time.monotonic()>settled_by:raise TimeoutError('startup graph did not settle')
    time.sleep(.01)
  end=time.monotonic()+a.duration;previous=None
  while True:
   item={'proc':proc_sample(proc.pid),'stats':command(proc,'STATS')}
   if previous:
    item['cpu_delta']=cpu_delta(previous['proc'],item['proc']);item['histogram_delta']=hist_delta(previous['stats'],item['stats']);item['dsp_histogram_delta']=hist_delta(previous['stats'],item['stats'],'dsp_histogram')
   sample_file.write(json.dumps(item,separators=(',',':'))+'\n');sample_file.flush()
   report['sample_count']+=1
   if first_sample is None:first_sample=item
   previous=item
   if time.monotonic()>=end and report['sample_count']>=2:break
   time.sleep(min(a.interval,max(0,end-time.monotonic())))
  first,last=first_sample['stats'],previous['stats']
  def metadata(item):return {'proc':item['proc'],'stats':{k:v for k,v in item['stats'].items() if not k.endswith('_histogram')}}
  report['first_sample']=metadata(first_sample);report['last_sample']=metadata(previous)
  report['timing_scope']=last.get('timing_scope');report['timing_scope_reason']=None if 'timing_scope' in last else 'backend did not provide timing_scope'
  report['aggregate_dsp_histogram']=hist_delta(first,last,'dsp_histogram')
  report['aggregate_histogram']=hist_delta(first,last)
  report['counter_deltas']={k:last[k]-first[k] if type(first.get(k)) is int and type(last.get(k)) is int and last[k]>=first[k] else None for k in ('xruns','blocks','deadline_misses','oversize_blocks','midi_overflows','bad_samples')}
  report['counter_delta_reason']='null means unavailable or reset; raw STATS retained' 
 except Exception as e:report['errors'].append(str(e))
 finally:
  if proc:
   try:command(proc,'QUIT');proc.wait(timeout=5)
   except Exception:
    proc.terminate()
    try:proc.wait(timeout=5)
    except subprocess.TimeoutExpired:proc.kill();proc.wait()
   report['host_exit_code']=proc.returncode
  sample_file.close();stderr.close();pathlib.Path(a.output).write_text(json.dumps(report,indent=2))
 return 1 if report['errors'] else 0
if __name__=='__main__':raise SystemExit(main())
