#!/usr/bin/env python3
"""JFX v1 package tool. Only trusted native plugins; hashes are integrity, not signatures."""
import argparse, gzip, hashlib, json, os, pathlib, platform, re, shutil, struct, tarfile, tempfile
from contextlib import contextmanager
LIMIT=64*1024*1024

class StrictInfo(tarfile.TarInfo):
 @classmethod
 def frombuf(cls,buf,encoding,errors):
  info=super().frombuf(buf,encoding,errors)
  # Reject hidden extension processing before TarInfo._proc_member reads its body.
  if info.type not in (tarfile.REGTYPE,tarfile.AREGTYPE):raise ValueError('only USTAR regular-file headers are accepted')
  return info

class BoundedReader:
 def __init__(self,source):self.source=source;self.remaining=LIMIT
 def read(self,n):
  if n<0 or n>self.remaining:raise ValueError('decompressed archive budget exceeded')
  data=self.source.read(n);self.remaining-=len(data);return data

@contextmanager
def archive(path):
 with open(path,'rb') as raw:
  if os.fstat(raw.fileno()).st_size>LIMIT+1024*1024:raise ValueError('compressed archive too large')
  compressed=raw.read(2)==b'\x1f\x8b';raw.seek(0)
  source=gzip.GzipFile(fileobj=raw) if compressed else raw
  try:
   with tarfile.open(fileobj=BoundedReader(source),mode='r|',tarinfo=StrictInfo) as tar:yield tar
  finally:
   if compressed:source.close()

def elf(data):
 if len(data)<64 or data[:6]!=b'\x7fELF\x02\x01' or struct.unpack_from('<H',data,16)[0]!=3:
  raise ValueError('expected little-endian ELF64 shared object')
 machine=struct.unpack_from('<H',data,18)[0]
 if machine not in (62,183):raise ValueError('unsupported ELF machine')
 return 'aarch64' if machine==183 else 'x86_64'

def valid_name(name):
 p=pathlib.PurePosixPath(name)
 return bool(name) and not p.is_absolute() and '\\' not in name and all(x not in ('','..','.') for x in name.split('/'))

def validate(meta,payload,native=True):
 if meta.get('format_version')!=1 or meta.get('abi_major')!=3 or type(meta.get('abi_minor')) is not int:raise ValueError('incompatible manifest')
 for field in ['plugin_id','plugin_version']:
  v=meta.get(field,'')
  if not isinstance(v,str) or len(v)>128 or not re.fullmatch('[a-zA-Z0-9_][a-zA-Z0-9_.-]*',v):raise ValueError('invalid identity')
 if meta.get('library')!='plugin.so' or set(meta.get('files',{}))!=set(payload):raise ValueError('invalid file list')
 for name,data in payload.items():
  if not valid_name(name) or (name!='plugin.so' and not name.startswith('assets/')):raise ValueError('invalid payload path')
  if hashlib.sha256(data).hexdigest()!=meta['files'][name]:raise ValueError('hash mismatch')
 for license in meta.get('licenses',[]):
  if not isinstance(license,str) or not license.startswith('assets/licenses/') or not valid_name(license) or license not in payload:raise ValueError('required license file missing')
 arch=elf(payload['plugin.so'])
 if arch!=meta.get('arch') or (native and arch!=platform.machine()):raise ValueError('incompatible architecture')
 ports=meta.get('ports',{})
 if ports.get('inputs') not in (0,2) or ports.get('outputs')!=2 or type(ports.get('midi_input')) is not bool:raise ValueError('unsupported ports')
 params=meta.get('parameters')
 if not isinstance(params,list) or len(params)>48:raise ValueError('invalid parameters')
 seen=set()
 import math
 for p in params:
  if not isinstance(p.get('key'),str) or p['key'] in seen:raise ValueError('invalid parameter key')
  seen.add(p['key'])
  if any(type(p.get(x)) not in (int,float) or not math.isfinite(p[x]) for x in ('min','max','default')) or not p['min']<=p['default']<=p['max']:raise ValueError('invalid parameter range')
 if type(meta.get('state_schema')) is not int or meta['state_schema']<0:raise ValueError('invalid state schema')

def pack(source,destination):
 source=pathlib.Path(source);destination=pathlib.Path(destination)
 meta=json.loads((source/'manifest.json').read_text())
 payload={}
 for file in source.rglob('*'):
  if file.is_symlink():raise ValueError('symlink in source')
  if file.is_file() and file.name!='manifest.json':payload[file.relative_to(source).as_posix()]=file.read_bytes()
 if len(payload)>127 or sum(map(len,payload.values()))>LIMIT:raise ValueError('package too large')
 meta['arch']=elf(payload['plugin.so']);meta['files']={name:hashlib.sha256(data).hexdigest() for name,data in payload.items()}
 validate(meta,payload,native=False)
 import io
 with tarfile.open(destination,'w:gz',format=tarfile.USTAR_FORMAT) as tar:
  files={'manifest.json':json.dumps(meta,indent=2).encode(),**payload}
  if sum(512+((len(data)+511)//512)*512 for data in files.values())+10240>LIMIT:raise ValueError('tar stream too large')
  for name,data in files.items():
   info=tarfile.TarInfo(name);info.size=len(data);info.mode=0o644;tar.addfile(info,io.BytesIO(data))
 print(destination)

def install(package,root):
 files={};size=0;entries=0
 with archive(package) as tar:
  for member in tar:
   entries+=1
   if entries>128 or not valid_name(member.name) or member.name in files or not member.isfile():raise ValueError('unsafe archive entry')
   size+=member.size
   if member.size<0 or size>LIMIT:raise ValueError('package too large')
   files[member.name]=tar.extractfile(member).read()
 if 'manifest.json' not in files or len(files['manifest.json'])>1024*1024:raise ValueError('missing/oversize manifest')
 meta=json.loads(files.pop('manifest.json'));validate(meta,files)
 root=pathlib.Path(root).resolve();root.mkdir(parents=True,exist_ok=True)
 target=root/(meta['plugin_id']+'-'+meta['plugin_version'])
 if target.exists():
  if target.is_symlink() or not target.is_dir():raise ValueError('installation target is not a directory')
  for name,data in files.items():
   p=target/name
   if p.is_symlink() or not p.is_file() or p.read_bytes()!=data:raise ValueError('version already exists with different content')
  print(target/'plugin.so');return
 temp=pathlib.Path(tempfile.mkdtemp(prefix='.jfx-',dir=root))
 try:
  for name,data in files.items():
   path=temp/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data);path.chmod(0o644)
  (temp/'manifest.json').write_text(json.dumps(meta,indent=2))
  os.rename(temp,target)
 finally:
  if temp.exists():shutil.rmtree(temp)
 print(target/'plugin.so')

def main():
 parser=argparse.ArgumentParser();sub=parser.add_subparsers(dest='command',required=True)
 p=sub.add_parser('pack');p.add_argument('source');p.add_argument('destination')
 p=sub.add_parser('install');p.add_argument('package');p.add_argument('root')
 args=parser.parse_args()
 try:
  if args.command=='pack':pack(args.source,args.destination)
  else:install(args.package,args.root)
 except (ValueError,OSError,KeyError,TypeError,tarfile.TarError) as exc:parser.exit(1,f'JFX error: {exc}\n')
if __name__=='__main__':main()
