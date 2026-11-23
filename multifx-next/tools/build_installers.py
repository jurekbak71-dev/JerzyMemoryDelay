#!/usr/bin/env python3
"""Build separate reproducible, self-contained source installers."""
import argparse,gzip,hashlib,io,json,pathlib,tarfile

def main():
 p=argparse.ArgumentParser();p.add_argument('--output',type=pathlib.Path,required=True);args=p.parse_args()
 root=pathlib.Path(__file__).resolve().parents[1];repo=root.parent
 files={}
 for folder in ['src','include','plugins','vendor','cmake','docs','tools','tests','installer']:
  for f in sorted((root/folder).rglob('*')):
   if f.is_file() and '__pycache__' not in f.parts and f.suffix!='.pyc':
    files['source/multifx-next/'+str(f.relative_to(root))]=f.read_bytes()
 for name in ['CMakeLists.txt','README.md']:
  files['source/multifx-next/'+name]=(root/name).read_bytes()
 for name in ['multifx2/api.h','multifx2/modules/tremolo.cpp','multifx4/ir_cab.h']:
  files['source/'+name]=(repo/name).read_bytes()
 source_id=hashlib.sha256(b''.join(k.encode()+hashlib.sha256(v).digest() for k,v in sorted(files.items()))+(root/'installer/install.py').read_bytes()+(root/'installer/README-PL.txt').read_bytes()).hexdigest()[:16]
 args.output.mkdir(parents=True,exist_ok=True)
 for variant in ['jack2','alsa']:
  payload=dict(files)
  payload['install.py']=(root/'installer/install.py').read_bytes()
  payload['README-PL.txt']=(root/'installer/README-PL.txt').read_bytes()
  payload['variant.json']=(json.dumps({'backend':variant,'source_id':source_id,'format':1},indent=2)+'\n').encode()
  payload['SHA256.json']=(json.dumps({k:hashlib.sha256(v).hexdigest() for k,v in sorted(payload.items())},indent=2)+'\n').encode()
  archive=args.output/f'jerzy-multifx-next-{variant}-source.tar.gz'
  with archive.open('wb') as raw,gzip.GzipFile(filename='',mode='wb',fileobj=raw,mtime=0) as gz,tarfile.open(fileobj=gz,mode='w') as t:
   for name,data in sorted(payload.items()):
    info=tarfile.TarInfo('jerzy-multifx-next-'+variant+'/'+name);info.size=len(data);info.mode=0o755 if name=='install.py' else 0o644
    t.addfile(info,io.BytesIO(data))
  print(archive,hashlib.sha256(archive.read_bytes()).hexdigest())
if __name__=='__main__':main()
