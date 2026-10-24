#!/usr/bin/env python3
"""Produce two clearly labeled synthetic test IRs (not sampled commercial cabinets).
Default CAB bypass is OFF; these demos are only offered in the local IR library.
"""
from pathlib import Path
import math
import struct
import wave
import sys

RATE=44100
LENGTH=2048

def generate(low_hz, high_hz):
    low=high=0.0
    shelves=[0.0]*4
    lo_coeff=1-math.exp(-2*math.pi*low_hz/RATE)
    hi_coeff=1-math.exp(-2*math.pi*high_hz/RATE)
    impulses=[]
    for i in range(LENGTH):
        x=1.0 if i==0 else 0.0
        high+=lo_coeff*(x-high)
        z=x-high
        # Slight cabinet-like body resonance. The response is synthetic.
        z+=.18*math.exp(-i/(RATE*.012))*math.sin(2*math.pi*low_hz*i/RATE)
        for n in range(len(shelves)):
            shelves[n]+=hi_coeff*(z-shelves[n])
            z=shelves[n]
        impulses.append(z)
    energy=sum(abs(x) for x in impulses)
    return [int(max(-32767,min(32767,round(24000*x/max(.01,energy))))) for x in impulses]

def main():
    target=Path(sys.argv[1]) if len(sys.argv)>1 else Path("demo-irs")
    target.mkdir(parents=True,exist_ok=True)
    for name,low,high in [
        ("DEMO_Guitar_4x12_Synthetic.wav",85,5800),
        ("DEMO_Bass_1x15_Synthetic.wav",40,3600)]:
        path=target/name
        if path.exists():continue
        values=generate(low,high)
        with wave.open(str(path),"wb") as f:
            f.setnchannels(1);f.setsampwidth(2);f.setframerate(RATE)
            f.writeframes(struct.pack("<"+"h"*len(values),*values))
        print("Synthetic reference IR:",path)

if __name__=="__main__":main()
