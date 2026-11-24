"""Rejecting an unsafe entry or accepting a wrong hash/ELF must fail this integration test."""
import hashlib, io, json, pathlib, shutil, subprocess, sys, tarfile, tempfile, unittest
TOOL=pathlib.Path(__file__).parents[1]/'tools/jfx_package.py'
PLUGINS=pathlib.Path(sys.argv[1]);sys.argv=sys.argv[:1]
class PackageTests(unittest.TestCase):
 def run_tool(self,*args):return subprocess.run([sys.executable,str(TOOL),*map(str,args)],text=True,capture_output=True)
 def test_real_pack_install_and_no_overwrite(self):
  with tempfile.TemporaryDirectory() as temp:
   temp=pathlib.Path(temp);package=temp/'gain.jfx';root=temp/'installed'
   r=self.run_tool('pack',PLUGINS/'gain',package);self.assertEqual(r.returncode,0,r.stderr)
   r=self.run_tool('install',package,root);self.assertEqual(r.returncode,0,r.stderr)
   installed=pathlib.Path(r.stdout.strip());self.assertEqual(installed.read_bytes(),(PLUGINS/'gain/plugin.so').read_bytes())
   r=self.run_tool('install',package,root);self.assertEqual(r.returncode,0,r.stderr)
   installed.write_bytes(b'do not overwrite');r=self.run_tool('install',package,root);self.assertNotEqual(r.returncode,0);self.assertEqual(installed.read_bytes(),b'do not overwrite')
 def test_cab_binary_license_retained_and_required(self):
  with tempfile.TemporaryDirectory() as temp:
   temp=pathlib.Path(temp);source=temp/'cab';shutil.copytree(PLUGINS/'cab',source)
   package=temp/'cab.jfx';r=self.run_tool('pack',source,package);self.assertEqual(r.returncode,0,r.stderr)
   with tarfile.open(package) as tar:
    for filename in ['PFFFT.txt','PFFFT-NOTICES.txt']:
     self.assertEqual(tar.extractfile('assets/licenses/'+filename).read(),(source/'assets/licenses'/filename).read_bytes())
   shutil.rmtree(source/'assets/licenses')
   self.assertNotEqual(self.run_tool('pack',source,temp/'missing-license.jfx').returncode,0,'required binary license omitted')
 def test_unsafe_archives_rejected(self):
  for name,kind in [('../escape','file'),('/absolute','file'),('assets/link','symlink'),('assets/hard','hardlink')]:
   with self.subTest(name=name),tempfile.TemporaryDirectory() as temp:
    temp=pathlib.Path(temp);package=temp/'bad.jfx'
    with tarfile.open(package,'w') as tar:
     info=tarfile.TarInfo(name);info.size=1
     if kind!='file':info.type=tarfile.SYMTYPE if kind=='symlink' else tarfile.LNKTYPE;info.linkname='/etc/passwd';info.size=0
     tar.addfile(info,io.BytesIO(b'x'))
    self.assertNotEqual(self.run_tool('install',package,temp/'root').returncode,0)
    self.assertFalse((temp/'escape').exists())
 def test_hidden_extension_budget(self):
  with tempfile.TemporaryDirectory() as temp:
   temp=pathlib.Path(temp);good=temp/'good.jfx'
   r=self.run_tool('pack',PLUGINS/'gain',good);self.assertEqual(r.returncode,0,r.stderr)
   with tarfile.open(good) as source:files={m.name:source.extractfile(m).read() for m in source if m.isfile()}
   for kind in ['pax','gnu']:
    package=temp/(kind+'.jfx')
    with tarfile.open(package,'w:gz',format=tarfile.PAX_FORMAT if kind=='pax' else tarfile.GNU_FORMAT,pax_headers={'comment':'x'*(64*1024*1024+1)} if kind=='pax' else None) as tar:
     if kind=='gnu':
      data=b'plugin.so\0'+b'x'*(64*1024*1024-8);info=tarfile.TarInfo('././@LongLink');info.type=tarfile.GNUTYPE_LONGNAME;info.size=len(data);tar.addfile(info,io.BytesIO(data))
     for name,data in files.items():
      info=tarfile.TarInfo(name);info.size=len(data);tar.addfile(info,io.BytesIO(data))
    self.assertNotEqual(self.run_tool('install',package,temp/kind).returncode,0,kind+' extension bypassed archive budget')
 def test_wrong_hash_and_elf(self):
  with tempfile.TemporaryDirectory() as temp:
   temp=pathlib.Path(temp);source=temp/'source';source.mkdir();(source/'plugin.so').write_bytes(b'not ELF')
   meta={'format_version':1,'plugin_id':'gain','plugin_version':'1.0.0','abi_major':3,'abi_minor':0,'arch':'x86_64','library':'plugin.so','ports':{'inputs':2,'outputs':2,'midi_input':False},'parameters':[],'state_schema':0,'files':{'plugin.so':'0'*64}}
   for digest in ['0'*64,hashlib.sha256(b'not ELF').hexdigest()]:
    meta['files']['plugin.so']=digest;(source/'manifest.json').write_text(json.dumps(meta));package=temp/'bad.jfx'
    with tarfile.open(package,'w') as tar:
     for file in source.iterdir():tar.add(file,arcname=file.name)
    self.assertNotEqual(self.run_tool('install',package,temp/'root').returncode,0)
if __name__=='__main__':unittest.main()
