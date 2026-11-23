import pathlib,subprocess,sys,tempfile,zipfile,json,hashlib
root=pathlib.Path(__file__).resolve().parents[1]
assert (root/'tools/build_windows_installers.py').exists(),'Windows RPi builder missing'
with tempfile.TemporaryDirectory() as tmp:
 d=pathlib.Path(tmp)
 subprocess.run([sys.executable,root/'tools/build_windows_installers.py','--output',d],check=True)
 for kind in ['jack2','alsa']:
  with zipfile.ZipFile(d/f'Jerzy-RPi-{kind}-Windows.zip') as z:
   assert {'START.cmd','Instaluj.ps1','Transport.ps1','payload.tar.gz','remote_bootstrap.py','bin/plink.exe','bin/pscp.exe','PuTTY-LICENCE.txt'}<=set(z.namelist())
   checks=json.loads(z.read('SHA256.json'))
   for name,h in checks.items():assert hashlib.sha256(z.read(name)).hexdigest()==h,name
   for n in ['bin/plink.exe','bin/pscp.exe']:assert z.read(n)[:2]==b'MZ'
print('Windows ZIPs, launchers, actual PE tools, payload integrity: PASS')
