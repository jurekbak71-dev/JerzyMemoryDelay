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
