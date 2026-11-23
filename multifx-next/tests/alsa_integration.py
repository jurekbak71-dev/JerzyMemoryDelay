import subprocess,sys,json,time,select
from pathlib import Path
exe=Path(sys.argv[1])
assert exe.exists(), 'direct ALSA executable missing'
assert subprocess.run([exe,'--help'],capture_output=True).returncode==0
p=subprocess.Popen([exe,'--device','null','--capture-channels','2','--playback-channels','2','--midi','off'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
try:
 assert select.select([p.stdout],[],[],5)[0], 'no READY'
 assert p.stdout.readline().startswith('READY'),p.stderr.read() if p.poll() is not None else 'not ready'
 time.sleep(.05)
 p.stdin.write('STATS\n');p.stdin.flush()
 assert select.select([p.stdout],[],[],5)[0], 'stats timeout'
 s=p.stdout.readline();assert s.startswith('OK '),s
 j=json.loads(s[3:]);assert j['blocks']>0,j
 assert j['backend']=='ALSA' and j['capture_channels']==2,j
 p.stdin.write('QUIT\n');p.stdin.flush()
 assert p.wait(timeout=3)==0,p.stderr.read()
finally:
 if p.poll() is None:p.kill();p.wait()
bad=subprocess.run([exe,'--device','this-device-does-not-exist'],input='QUIT\n',text=True,capture_output=True,timeout=5)
assert bad.returncode!=0 and 'READY' not in bad.stdout
print('ALSA null processing, diagnostics, shutdown, invalid device: PASS')
