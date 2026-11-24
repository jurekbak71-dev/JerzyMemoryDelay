"""Real libasound file PCM catches backend incorrectly overriding session BPM."""
import json,os,pathlib,select,struct,subprocess,sys,tempfile,time
host,fixture=sys.argv[1:3]
with tempfile.TemporaryDirectory() as tmp:
 d=pathlib.Path(tmp);out=d/'tempo.raw';conf=d/'alsa.conf'
 conf.write_text('pcm.null { type null }\npcm.tempo { type file slave.pcm "null" file "'+str(out)+'" format "raw" }\n')
 p=subprocess.Popen([host,'--capture-device','null','--playback-device','tempo','--capture-channels','2','--playback-channels','2','--midi','off'],env=dict(os.environ,ALSA_CONFIG_PATH=str(conf)),text=True,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
 def response(command=None):
  if command:p.stdin.write(command+'\n');p.stdin.flush()
  assert select.select([p.stdout],[],[],5)[0],'tempo control timeout'
  line=p.stdout.readline();assert line, 'host stopped';return line
 try:
  assert response().startswith('READY')
  assert response('ADD '+json.dumps(fixture)).startswith('OK')
  time.sleep(.04)
  assert response('BPM 60').startswith('OK')
  time.sleep(.04)
  assert response('QUIT').startswith('OK');assert p.wait(timeout=3)==0,p.stderr.read()
 finally:
  if p.poll() is None:p.kill();p.wait()
 samples=list(struct.iter_unpack('<ff',out.read_bytes()))
 assert any(abs(a-.4)<1e-6 for a,b in samples),'initial tempo fixture not audible'
 assert any(abs(a-.2)<1e-6 for a,b in samples),'ALSA forced 120 BPM despite BPM 60'
print('PASS actual ALSA transport honors session BPM')
