"""Called after verified payload extraction; no root or network changes."""
import hashlib,json,os,pathlib,platform,shutil,subprocess,sys,uuid

def deploy(bundle,home=None):
    bundle=pathlib.Path(bundle);home=pathlib.Path(home or pathlib.Path.home())
    meta=json.loads((bundle/'variant.json').read_text())
    if platform.machine() not in ('aarch64','arm64','x86_64','AMD64'):
        raise RuntimeError('Wymagany system 64-bit ARM64 (lub x86_64 do testów).')
    print('SYSTEM:',platform.platform(),flush=True)
    release=pathlib.Path('/etc/os-release')
    if release.exists():print(release.read_text(),flush=True)
    # GUI must be usable after install; no silent success without its runtime.
    subprocess.run([sys.executable,'-c','import tkinter'],check=True)
    prefix=home/'.local/opt/jerzy-multifx-next'
    target=prefix/(meta['backend']+'-'+meta['source_id'])
    host=target/'bin'/('JerzyMultiFXNext' if meta['backend']=='jack2' else 'JerzyMultiFXNextALSA')
    def complete():
        try:
            if json.loads((target/'variant.json').read_text())!=meta:return False
            if not all(f.is_file() and os.access(f,os.X_OK) for f in [host,target/'start.sh']):return False
            return all((target/'plugins'/name/file).is_file() for name in ['gain','synth','cab','legacy'] for file in ['plugin.so','manifest.json']) and all((target/'plugins/cab/assets/licenses'/name).is_file() for name in ['PFFFT.txt','PFFFT-NOTICES.txt'])
        except (OSError,ValueError):return False
    if target.exists() and not complete():
        target.rename(target.with_name(target.name+'.incomplete-'+uuid.uuid4().hex[:8]))
    if not target.exists():
        subprocess.run([sys.executable,str(bundle/'install.py'),'--install','--prefix',str(prefix)],check=True)
    if not complete():raise RuntimeError('Niekompletna instalacja; brak potwierdzenia sukcesu.')
    subprocess.run([str(host),'--help'],check=True)
    permanent=home/'.local/share/jerzy-multifx-next/installers'/(meta['backend']+'-'+meta['source_id'])
    checks=json.loads((bundle/'SHA256.json').read_text())
    def sources_complete():
        try:return all((permanent/name).is_file() and hashlib.sha256((permanent/name).read_bytes()).hexdigest()==digest for name,digest in checks.items())
        except OSError:return False
    if permanent.exists() and not sources_complete():
        permanent.rename(permanent.with_name(permanent.name+'.incomplete-'+uuid.uuid4().hex[:8]))
    if not permanent.exists():
        permanent.parent.mkdir(parents=True,exist_ok=True)
        shutil.copytree(bundle,permanent)
    if not sources_complete():raise RuntimeError('Niekompletne pliki panelu; przerwano.')
    appdir=home/'.local/share/applications';appdir.mkdir(parents=True,exist_ok=True)
    def quote(s):
        return '"'+str(s).replace('\\','\\\\').replace('"','\\"').replace('`','\\`').replace('$','\\$')+'"'
    launcher=appdir/('jerzy-multifx-next-'+meta['backend']+'.desktop')
    launcher.write_text('[Desktop Entry]\nType=Application\nName=Jerzy MultiFX Next '+meta['backend'].upper()+'\nExec='+quote(sys.executable)+' '+quote(permanent/'click_app.py')+'\nTerminal=false\nCategories=AudioVideo;Audio;\n')
    launcher.chmod(0o644)
    print('JFX_INSTALL_OK '+str(target),flush=True)
    return target
if __name__=='__main__':
    import argparse
    parser=argparse.ArgumentParser();parser.add_argument('--home',type=pathlib.Path)
    args=parser.parse_args()
    deploy(pathlib.Path(__file__).resolve().parent,home=args.home)
