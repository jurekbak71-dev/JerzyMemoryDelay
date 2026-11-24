#!/usr/bin/env python3
"""Analyze an externally captured common-clock reference/return stereo PCM WAV."""
import argparse
import cmath
import json
import math
import pathlib
import wave

def fft(values,inverse=False):
 a=list(map(complex,values));n=len(a);j=0
 for i in range(1,n):
  bit=n>>1
  while j&bit:j^=bit;bit>>=1
  j^=bit
  if i<j:a[i],a[j]=a[j],a[i]
 size=2
 while size<=n:
  root=cmath.exp((2j if inverse else -2j)*math.pi/size)
  for offset in range(0,n,size):
   w=1
   for k in range(size//2):
    u=a[offset+k];v=w*a[offset+k+size//2];a[offset+k]=u+v;a[offset+k+size//2]=u-v;w*=root
  size*=2
 return [x/n for x in a] if inverse else a

def analyze(path,synchronous_reference=False,max_lag_frames=4410,max_frames=262144):
 if not synchronous_reference:raise ValueError('requires --synchronous-reference and documented common-clock reference wiring')
 with wave.open(str(path),'rb') as w:
  rate=w.getframerate();width=w.getsampwidth();n=w.getnframes()
  if w.getnchannels()!=2 or width not in (2,3,4) or w.getcomptype()!='NONE':raise ValueError('requires stereo integer PCM16/24/32 WAV')
  if n>max_frames:raise ValueError('recording exceeds max-frames; select a synchronous event window explicitly')
  raw=w.readframes(n)
 if n<2 or max_lag_frames<0:raise ValueError('invalid frame count or lag range')
 channels=[[],[]]
 for i in range(n):
  for c in range(2):
   start=(i*2+c)*width;channels[c].append(int.from_bytes(raw[start:start+width],'little',signed=True)/(2**(width*8-1)))
 x,y=channels;energy=math.sqrt(sum(v*v for v in x)*sum(v*v for v in y))
 if not energy:raise ValueError('reference or return is silent')
 size=1
 while size<2*n:size*=2
 fx=fft(x+[0]*(size-n));fy=fft(y+[0]*(size-n));corr=fft([b*a.conjugate() for a,b in zip(fx,fy)],True)
 limit=min(max_lag_frames,n-1);lag=max(range(limit+1),key=lambda k:abs(corr[k].real));px=max(range(n),key=lambda i:abs(x[i]));py=max(range(n),key=lambda i:abs(y[i]))
 return {'sample_rate_hz':rate,'frames':n,'reference_channel':1,'returned_channel':2,'peak_reference_frame':px,'peak_return_frame':py,'peak_lag_frames':py-px,'peak_latency_ms':1000*(py-px)/rate,'correlation_lag_frames':lag,'correlation_latency_ms':1000*lag/rate,'correlation_normalized':corr[lag].real/energy,'search_nonnegative_max_lag_frames':limit,'search_boundary':lag==limit,'synchronous_reference_asserted':True,'scope':'Latency between the two captured channels; engine round trip only if documented wiring includes the engine.'}

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('wav');p.add_argument('--synchronous-reference',action='store_true');p.add_argument('--max-lag-frames',type=int,default=4410);p.add_argument('--max-frames',type=int,default=262144);p.add_argument('--output');a=p.parse_args()
 try:r=analyze(a.wav,a.synchronous_reference,a.max_lag_frames,a.max_frames)
 except (ValueError,wave.Error,OSError) as e:p.error(str(e))
 result=json.dumps(r,indent=2)
 if a.output:pathlib.Path(a.output).write_text(result+'\n')
 else:print(result)
if __name__=='__main__':main()
