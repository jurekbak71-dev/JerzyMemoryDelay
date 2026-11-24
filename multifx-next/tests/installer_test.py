import subprocess,sys,tempfile,tarfile,pathlib,os
root=pathlib.Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as tmp:
 d=pathlib.Path(tmp)
 tool=root/'tools/build_installers.py'
 assert tool.exists(),'installer builder missing'
 subprocess.run([sys.executable,tool,'--output',str(d)],check=True)
 for kind in ['jack2','alsa']:
  archive=d/f'jerzy-multifx-next-{kind}-source.tar.gz'
  assert archive.is_file(),kind
  with tarfile.open(archive) as t:
   names=set(t.getnames())
   assert any(n.endswith('/source/source/dsp.h') for n in names), 'memory delay DSP missing'
   assert any(n.endswith('/source/source/parameters.h') for n in names), 'memory delay parameters missing'
   assert any(n.endswith('/multifx2/modules/reel_echo.cpp') for n in names), 'complete legacy source catalog missing'
   assert any(n.endswith('/multifx2/vendor/reel/TapeEngine.cpp') for n in names), 'legacy dependency missing'
  out=d/kind;out.mkdir()
  with tarfile.open(archive) as t:t.extractall(out,filter='data')
  bundle=next(out.iterdir())
  r=subprocess.run([sys.executable,bundle/'install.py','--help'],capture_output=True,text=True)
  assert r.returncode==0,r.stderr
  assert not (d/'installed').exists()
  # Checksums must catch modification before cmake or compiler can run.
  (bundle/'source/multifx-next/src/engine.cpp').write_text('tampered')
  r=subprocess.run([sys.executable,bundle/'install.py','--prefix',str(d/'installed')],capture_output=True,text=True)
  assert r.returncode!=0 and 'checksum' in r.stderr.lower(),r.stderr
  assert not (d/'installed').exists()
print('two standalone source archives, help, integrity gate: PASS')

subprocess.run([sys.executable,root/"tests/deploy_integrity_test.py"],check=True)
