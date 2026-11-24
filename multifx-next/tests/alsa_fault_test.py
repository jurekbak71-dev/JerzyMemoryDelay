import json,os,pathlib,select,subprocess,sys,time
host,shim=sys.argv[1:3]
assert pathlib.Path(shim).is_file(), 'fault-injection library missing'
args=[host,'--device','null','--capture-channels','2','--playback-channels','2','--midi','off']
def start(fatal=False):
 env=dict(os.environ,LD_PRELOAD=shim)
 if fatal:env['JFX_TEST_FATAL']='1'
 p=subprocess.Popen(args,env=env,text=True,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
 assert select.select([p.stdout],[],[],5)[0] and p.stdout.readline().startswith('READY')
 return p
p=start()
try:
 time.sleep(.2)
 p.stdin.write('STATS\n');p.stdin.flush()
 assert select.select([p.stdout],[],[],3)[0]
 j=json.loads(p.stdout.readline()[3:]);assert j['xruns']==1 and j['blocks']>50 and j['error']==0,j
 p.stdin.write('QUIT\n');p.stdin.flush();assert p.wait(timeout=3)==0,p.stderr.read()
finally:
 if p.poll() is None:p.kill();p.wait()
p=start(True)
try:assert p.wait(timeout=3)==1,p.stderr.read()
finally:
 if p.poll() is None:p.kill();p.wait()
print('short capture I/O, XRUN recovery with continuing DSP, fatal disconnect shutdown: PASS')
