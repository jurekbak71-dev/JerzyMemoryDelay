#!/usr/bin/env python3
"""Build mouse-installable Debian packages; native user-local build on first GUI run."""
import argparse,hashlib,json,pathlib,shutil,subprocess,sys,tarfile,tempfile

ICON='''<svg xmlns="http://www.w3.org/2000/svg" width="128" height="128" viewBox="0 0 128 128"><rect width="128" height="128" rx="24" fill="#152c3e"/><path d="M16 64h16l9-28 16 58 15-64 13 34h27" stroke="#64e6b0" stroke-width="7" fill="none" stroke-linejoin="round"/></svg>'''

def main():
 p=argparse.ArgumentParser();p.add_argument('--output',type=pathlib.Path,required=True);args=p.parse_args()
 root=pathlib.Path(__file__).resolve().parents[1]
 args.output.mkdir(parents=True,exist_ok=True)
 with tempfile.TemporaryDirectory(prefix='jerzy-deb-') as tmp:
  temp=pathlib.Path(tmp)
  subprocess.run([sys.executable,root/'tools/build_installers.py','--output',temp],check=True)
  for kind in ['jack2','alsa']:
   package='jerzy-multifx-next-'+kind;stage=temp/('stage-'+kind)
   opt=stage/'opt'/package;opt.mkdir(parents=True)
   with tarfile.open(temp/(package+'-source.tar.gz')) as archive:
    # Only archives created just above from this repository, never arbitrary uploads.
    archive.extractall(temp/('source-'+kind),filter='data')
   shutil.copytree(temp/('source-'+kind)/package,opt,dirs_exist_ok=True)
   shutil.copy2(root/'installer/click_app.py',opt/'click_app.py')
   shutil.copy2(root/'installer/KLIKNIJ-PRZECZYTAJ.txt',opt/'KLIKNIJ-PRZECZYTAJ.txt')
   checks=json.loads((opt/'SHA256.json').read_text())
   for name in ['click_app.py','KLIKNIJ-PRZECZYTAJ.txt']:checks[name]=hashlib.sha256((opt/name).read_bytes()).hexdigest()
   (opt/'SHA256.json').write_text(json.dumps(checks,indent=2)+'\n')
   icon=stage/'usr/share/icons/hicolor/scalable/apps'/(package+'.svg');icon.parent.mkdir(parents=True,exist_ok=True);icon.write_text(ICON)
   desktop=stage/'usr/share/applications'/(package+'.desktop');desktop.parent.mkdir(parents=True,exist_ok=True)
   desktop.write_text('[Desktop Entry]\nType=Application\nName=Jerzy MultiFX Next '+kind.upper()+'\nComment=Instalacja i uruchamianie audio myszką\nExec=/usr/bin/python3 /opt/'+package+'/click_app.py\nIcon='+package+'\nTerminal=false\nCategories=AudioVideo;Audio;\nStartupNotify=true\n')
   control=stage/'DEBIAN';control.mkdir()
   deps='python3 (>= 3.10), python3-tk, cmake (>= 3.25), g++ (>= 4:11), make, pkg-config, '+('libjack-jackd2-dev' if kind=='jack2' else 'libasound2-dev')
   size=sum(f.stat().st_size for f in stage.rglob('*') if f.is_file())//1024+1
   (control/'control').write_text('Package: '+package+'\nVersion: 0.2.0\nArchitecture: all\nMaintainer: Jerzy MultiFX project <noreply@localhost>\nSection: sound\nPriority: optional\nDepends: '+deps+'\nInstalled-Size: '+str(size)+'\nDescription: Jerzy MultiFX Next '+kind.upper()+' graphical installer and test panel\n Native source installation in a user-local directory. No automatic audio or service changes.\n')
   (control/'md5sums').write_text(''.join(hashlib.md5(f.read_bytes()).hexdigest()+'  '+str(f.relative_to(stage))+'\n' for f in sorted(stage.rglob('*')) if f.is_file() and 'DEBIAN' not in f.relative_to(stage).parts))
   for entry in [stage,*stage.rglob('*')]:
    entry.chmod(0o755 if entry.is_dir() or entry.stat().st_mode & 0o111 else 0o644)
   output=args.output/(package+'_0.2.0_all.deb')
   subprocess.run(['dpkg-deb','--root-owner-group','--build',stage,output],check=True)
   print(output,hashlib.sha256(output.read_bytes()).hexdigest())
if __name__=='__main__':main()
