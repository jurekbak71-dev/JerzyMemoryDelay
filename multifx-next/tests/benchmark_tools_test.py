import importlib.util
import pathlib
import tempfile
import unittest
import wave
import struct
import subprocess
import json
import sys
import os
ROOT=pathlib.Path(__file__).resolve().parents[1]
def module(name):
 spec=importlib.util.spec_from_file_location(name,ROOT/'tools'/f'{name}.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
class ToolsTest(unittest.TestCase):
 def test_nonfinite_time_rejected(self):
  for option in ('--duration','--interval'):
   for value in ('nan','inf','-inf'):
    r=subprocess.run([sys.executable,str(ROOT/'tools/benchmark_rpi.py'),'--host','/not/a/host','--backend','alsa',option+'='+value],capture_output=True,text=True)
    self.assertNotEqual(r.returncode,0,(option,value))
 def test_histogram(self):
  b=module('benchmark_rpi');h=[0]*4096;h[3]=95;h[-1]=5
  self.assertEqual(b.quantile_bound(h,.95),{'lower_ns':3000,'upper_ns':4000,'reason':None})
  self.assertIsNone(b.quantile_bound(h,.99)['upper_ns'])
  self.assertIsNone(b.quantile_bound([0]*4096,.95)['lower_ns'])
 def test_streamed_real_null_host(self):
  supplied=os.environ.get("NEXT_BENCHMARK_TEST_HOST")
  if not supplied:self.skipTest("NEXT_BENCHMARK_TEST_HOST not configured")
  host=pathlib.Path(supplied)
  self.assertTrue(host.is_file(),"configured null ALSA host is missing")
  with tempfile.TemporaryDirectory() as d:
   out=pathlib.Path(d)/"report.json"
   subprocess.run([sys.executable,str(ROOT/"tools/benchmark_rpi.py"),"--host",str(host),"--backend","alsa","--device","null","--duration","0.000001","--interval",".02","--output",str(out),"--run"],check=True)
   report=json.loads(out.read_text());self.assertNotIn("samples",report);self.assertGreaterEqual(report["sample_count"],2)
   rows=[json.loads(line) for line in pathlib.Path(str(out)+".samples.jsonl").read_text().splitlines()]
   self.assertEqual(len(rows),report["sample_count"]);self.assertIn("timing_scope",rows[-1]["stats"])
   self.assertIsNone(report["round_trip"])
 def test_loopback(self):
  a=module('analyze_loopback')
  with tempfile.TemporaryDirectory() as d:
   p=pathlib.Path(d)/'test.wav'
   with wave.open(str(p),'wb') as w:
    w.setparams((2,2,1000,0,'NONE','not compressed'));w.writeframes(b''.join(struct.pack('<hh',16000 if n==3 else 0,8000 if n==10 else 0) for n in range(64)))
   r=a.analyze(p,True,20);self.assertEqual(r['correlation_lag_frames'],7);self.assertEqual(r['correlation_latency_ms'],7);self.assertEqual(r['peak_lag_frames'],7)
   with self.assertRaises(ValueError):a.analyze(p,False,20)
if __name__=='__main__':unittest.main()
