"""Real libasound file PCM: multichannel capture -> DSP -> multichannel playback."""
import json,os,pathlib,select,struct,subprocess,sys,tempfile,time
exe=pathlib.Path(sys.argv[1]);plugins=pathlib.Path(sys.argv[2])
with tempfile.TemporaryDirectory() as tmp:
 d=pathlib.Path(tmp);inp=d/'input.raw';out=d/'output.raw'
 inp.write_bytes(struct.pack('<'+'f'*6,*[.1,.2,.3,.4,.5,.6])*200000)
 conf=d/'alsa.conf'
 conf.write_text('pcm.null { type null }\npcm.test_in { type file slave.pcm "null" file "/dev/null" infile "'+str(inp)+'" format "raw" }\npcm.test_out { type file slave.pcm "null" file "'+str(out)+'" format "raw" }\n')
 env=dict(os.environ,ALSA_CONFIG_PATH=str(conf))
 p=subprocess.Popen([exe,'--capture-device','test_in','--playback-device','test_out','--capture-channels','6','--playback-channels','4','--in-left','3','--in-right','5','--out-left','2','--out-right','4','--midi','off'],env=env,text=True,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
 def response(command=None):
  if command:p.stdin.write(command+'\n');p.stdin.flush()
  assert select.select([p.stdout],[],[],5)[0], 'control timeout'
  line=p.stdout.readline();assert line, p.stderr.read();return line
 try:
  assert response().startswith('READY')
  time.sleep(.02)
  assert response('ADD '+json.dumps(str(plugins/'gain/plugin.so'))).startswith('OK')
  time.sleep(.02)
  assert response('SET 1 gain 0.5').startswith('OK')
  time.sleep(.03)
  assert response('STATS').startswith('OK')
  response('QUIT');assert p.wait(timeout=3)==0,p.stderr.read()
 finally:
  if p.poll() is None:p.kill();p.wait()
 data=out.read_bytes();assert len(data)>64*4*4*3
 samples=list(struct.iter_unpack('<ffff',data))
 assert any(abs(s[1]-.3)<1e-6 and abs(s[3]-.5)<1e-6 for s in samples),'passthrough mapping'
 assert any(abs(s[1]-.15)<1e-6 and abs(s[3]-.25)<1e-6 for s in samples),'gain not processed'
 assert all(s[0]==0 and s[2]==0 for s in samples),'unused channels nonzero'
print('real ALSA file PCM, stereo mapping, gain DSP, unused channels: PASS')
