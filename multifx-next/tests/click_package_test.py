"""Catch absent menu launchers, package conflicts and invalid dependency metadata."""
import pathlib,subprocess,sys,tempfile,configparser
root=pathlib.Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as tmp:
 d=pathlib.Path(tmp)
 assert (root/'tools/build_click_installers.py').exists(),'mouse installer builder missing'
 subprocess.run([sys.executable,root/'tools/build_click_installers.py','--output',d],check=True)
 paths=[]
 for kind in ['jack2','alsa']:
  package=d/f'jerzy-multifx-next-{kind}_0.2.0_all.deb'
  assert package.exists(),kind
  fields=subprocess.check_output(['dpkg-deb','-f',package,'Package','Architecture','Depends'],text=True)
  assert 'python3-tk' in fields and 'cmake (>= 3.25)' in fields and 'g++ (>= 4:11)' in fields,fields
  assert ('libjack-jackd2-dev' if kind=='jack2' else 'libasound2-dev') in fields
  dest=d/('extract '+kind)
  subprocess.run(['dpkg-deb','-x',package,dest],check=True)
  menu=dest/f'usr/share/applications/jerzy-multifx-next-{kind}.desktop'
  subprocess.run(['desktop-file-validate',menu],check=True)
  c=configparser.ConfigParser(interpolation=None);c.read(menu)
  entry=c['Desktop Entry'];assert entry['Terminal']=='false'
  assert kind in entry['Name'].lower() and entry['Exec'].startswith('/usr/bin/python3 ')
  app=dest/f'opt/jerzy-multifx-next-{kind}/click_app.py'
  assert subprocess.run([sys.executable,app,'--help'],capture_output=True).returncode==0
  paths.append({str(f.relative_to(dest)) for f in dest.rglob('*') if f.is_file()})
 assert not paths[0]&paths[1],paths[0]&paths[1]
print('two .deb packages: dependencies, no conflicts, valid GUI launchers: PASS')
