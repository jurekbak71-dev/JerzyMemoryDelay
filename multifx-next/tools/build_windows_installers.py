#!/usr/bin/env python3
"""Package Windows -> RPi offline source deployment, official PuTTY x64 tools."""
import argparse,gzip,hashlib,io,json,pathlib,subprocess,sys,tarfile,tempfile,urllib.request,zipfile

def main():
 p=argparse.ArgumentParser();p.add_argument('--output',type=pathlib.Path,required=True);p.add_argument('--cache',type=pathlib.Path,default=pathlib.Path('/workspace/work/putty-0.83'));args=p.parse_args()
 root=pathlib.Path(__file__).resolve().parents[1];win=root/'installer/windows'
 args.cache.mkdir(parents=True,exist_ok=True);args.output.mkdir(parents=True,exist_ok=True)
 base='https://the.earth.li/~sgtatham/putty/0.83/'
 sums=urllib.request.urlopen(base+'sha256sums').read().decode()
 tools={};provenance={}
 for name in ['plink.exe','pscp.exe']:
  relative='w64/'+name;f=args.cache/name
  if not f.exists():urllib.request.urlretrieve(base+relative,f)
  digest=hashlib.sha256(f.read_bytes()).hexdigest()
  expected=next((line.split()[0] for line in sums.splitlines() if line.split()[-1].lstrip('*./')==relative),None)
  if digest!=expected:raise RuntimeError('Official PuTTY checksum mismatch: '+name)
  tools['bin/'+name]=f.read_bytes();provenance[name]={'url':base+relative,'sha256':digest}
 source=args.cache/'putty-0.83.tar.gz'
 if not source.exists():urllib.request.urlretrieve(base+source.name,source)
 source_hash=hashlib.sha256(source.read_bytes()).hexdigest()
 expected=next(line.split()[0] for line in sums.splitlines() if line.split()[-1].lstrip('*./')==source.name)
 if source_hash!=expected:raise RuntimeError('PuTTY source checksum mismatch')
 with tarfile.open(source) as t:license_data=t.extractfile('putty-0.83/LICENCE').read()
 provenance['license_source']={'url':base+source.name,'sha256':source_hash}
 with tempfile.TemporaryDirectory() as tmp:
  temp=pathlib.Path(tmp)
  subprocess.run([sys.executable,root/'tools/build_installers.py','--output',temp],check=True)
  for kind in ['jack2','alsa']:
   with tarfile.open(temp/f'jerzy-multifx-next-{kind}-source.tar.gz') as archive:
    payload={m.name.split('/',1)[1]:archive.extractfile(m).read() for m in archive.getmembers() if m.isfile()}
   payload['click_app.py']=(root/'installer/click_app.py').read_bytes()
   payload['deploy_remote.py']=(win/'deploy_remote.py').read_bytes()
   checks=json.loads(payload['SHA256.json'])
   for n in ['click_app.py','deploy_remote.py']:checks[n]=hashlib.sha256(payload[n]).hexdigest()
   payload['SHA256.json']=(json.dumps(checks,indent=2)+'\n').encode()
   tarbytes=io.BytesIO()
   with gzip.GzipFile(filename='',fileobj=tarbytes,mode='wb',mtime=0) as gz,tarfile.open(fileobj=gz,mode='w') as archive:
    for name,data in sorted(payload.items()):
     info=tarfile.TarInfo('jerzy-multifx-next-'+kind+'/'+name);info.size=len(data);info.mode=0o755 if name=='install.py' else 0o644
     archive.addfile(info,io.BytesIO(data))
   files=dict(tools);files['payload.tar.gz']=tarbytes.getvalue();files['PuTTY-LICENCE.txt']=license_data
   files['PuTTY-PROVENANCE.json']=(json.dumps(provenance,indent=2)+'\n').encode()
   files['variant.json']=payload['variant.json']
   for name in ['START.cmd','Instaluj.ps1','Transport.ps1','CZYTAJ.txt','remote_bootstrap.py']:
    data=(win/name).read_bytes()
    if name.endswith(('.ps1','.cmd','.txt')):data=data.replace(b'\r\n',b'\n').replace(b'\n',b'\r\n')
    # UTF-8 BOM is required for non-ASCII scripts/text in Windows PowerShell 5.1.
    files[name]=(b'\xef\xbb\xbf'+data) if name.endswith('.ps1') else data
   files['SHA256.json']=(json.dumps({k:hashlib.sha256(v).hexdigest() for k,v in sorted(files.items())},indent=2)+'\n').encode()
   output=args.output/f'Jerzy-RPi-{kind}-Windows.zip'
   with zipfile.ZipFile(output,'w',compression=zipfile.ZIP_DEFLATED) as z:
    for name,data in sorted(files.items()):z.writestr(name,data)
   print(output,hashlib.sha256(output.read_bytes()).hexdigest())
if __name__=='__main__':main()
