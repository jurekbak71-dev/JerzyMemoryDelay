"""Actual deployment bootstrap/native install in isolated user home; no SSH/Windows claim."""
import pathlib,sys,tempfile,zipfile,subprocess,json
archive=pathlib.Path(sys.argv[1]);bootstrap=pathlib.Path(__file__).parents[1]/'installer/windows/remote_bootstrap.py'
with tempfile.TemporaryDirectory(dir='/workspace/work') as tmp:
 d=pathlib.Path(tmp);upload=d/'upload';upload.mkdir();home=d/'user home'
 with zipfile.ZipFile(archive) as z:(upload/'payload.tar.gz').write_bytes(z.read('payload.tar.gz'))
 code='upload_dir='+repr(str(upload))+'\n'+bootstrap.read_text()
 r=subprocess.run([sys.executable,'-c',code,'--home',str(home)],capture_output=True,text=True,timeout=150)
 assert r.returncode==0,r.stdout+r.stderr
 marker=next(s for s in r.stdout.splitlines() if s.startswith('JFX_INSTALL_OK '))
 target=pathlib.Path(marker.split(' ',1)[1]);assert target.is_relative_to(home),target
 meta=json.loads((target/'variant.json').read_text())
 assert len(list((target/'legacy-catalog').glob('*.so')))==13, 'legacy module catalog not installed'
 assert (target/'plugins/cab_dual/plugin.so').is_file(), 'dual CAB missing'
 subprocess.run(['desktop-file-validate',home/'.local/share/applications'/('jerzy-multifx-next-'+meta['backend']+'.desktop')],check=True)
 assert (home/'.local/share/jerzy-multifx-next/installers'/target.name/'click_app.py').exists()
 subprocess.run([target/'start.sh','--help'],check=True,capture_output=True)
 if meta['backend']=='alsa':subprocess.run([sys.executable,pathlib.Path(__file__).with_name('alsa_integration.py'),target/'start.sh'],check=True)
 print('Remote bootstrap, checksums, actual native '+meta['backend']+' install, menu entry and host: PASS')
