"""JACK2 dummy server + native host + separate native probe; no hardware latency claim."""
import json, os, pathlib, select, subprocess, sys, tempfile, time
host,probe,jackd,plugins=sys.argv[1:5]
env=os.environ.copy();server='jfx_next_'+str(os.getpid());env['JACK_DEFAULT_SERVER']=server
with tempfile.TemporaryDirectory() as temp:
 log=open(pathlib.Path(temp)/'jack.log','w+')
 daemon=subprocess.Popen([jackd,'--no-realtime','--name',server,'-d','dummy','-r','44100','-p','64'],env=env,stdout=log,stderr=log)
 client=None
 try:
  deadline=time.monotonic()+12
  while time.monotonic()<deadline:
   client=subprocess.Popen([host],env=env,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=log,text=True,bufsize=1)
   ready,_,_=select.select([client.stdout],[],[],1)
   line=client.stdout.readline() if ready else ''
   if line.startswith('READY '):break
   if client.poll() is None:client.terminate();client.wait(timeout=3)
   client=None;time.sleep(.1)
  if client is None:raise RuntimeError('host did not connect to dummy JACK')
  def command(text):
   for _ in range(200):
    client.stdin.write(text+'\n');client.stdin.flush()
    ready,_,_=select.select([client.stdout],[],[],3)
    if not ready:raise RuntimeError('control response timeout')
    response=client.stdout.readline().strip()
    if response.startswith('ERR busy:'):time.sleep(.005);continue
    if not response.startswith('OK'):raise RuntimeError(text+': '+response)
    return response
   raise RuntimeError('acknowledgement timeout')
  def capture(overflow=False):
   p=subprocess.run([probe]+(['--overflow'] if overflow else []),env=env,capture_output=True,text=True,timeout=12)
   if p.returncode:raise RuntimeError('JACK probe failed: '+p.stderr)
   return json.loads(p.stdout)
  a=capture();assert a['finite'] and abs(a['left']-.2)<1e-5 and abs(a['right']+.3)<1e-5,a
  tool=pathlib.Path(__file__).parents[1]/'tools/jfx_package.py'
  package=pathlib.Path(temp)/'gain.jfx'
  subprocess.run([sys.executable,str(tool),'pack',plugins+'/gain',str(package)],check=True,capture_output=True,text=True)
  installed=subprocess.run([sys.executable,str(tool),'install',str(package),str(pathlib.Path(temp)/'installed')],check=True,capture_output=True,text=True).stdout.strip()
  assert command('ADD '+installed)=='OK 1';command('SET 1 gain 0.5')
  a=capture();assert abs(a['left']-.1)<1e-5 and abs(a['right']+.15)<1e-5,a
  command('ADD '+plugins+'/synth/plugin.so')
  a=capture();assert a['finite'] and a['note_energy']>.01 and abs(a['left']-.1)<1e-5,a
  a=capture(overflow=True);assert a['tail_variation']<1e-6,a
  assert json.loads(command('STATS')[3:])['midi_overflows']>0,'JACK lost-event fixture did not report mixdown overflow'
  for _ in range(100):command('MODE 1 BYPASS');command('MODE 1 ACTIVE')
  # ACTIVE creates a fresh instance. Graph acknowledgement is published at block start,
  # before its first process call finishes; wait for measured work instead of a fixed sleep.
  deadline=time.monotonic()+3
  while True:
   stats=json.loads(command('STATS')[3:])
   if not stats['pending'] and stats['blocks']>500 and stats['slot_calls'][stats['slot_ids'].index(1)]>0:break
   if time.monotonic()>=deadline:raise AssertionError(stats)
   time.sleep(.002)
  command('MODE 2 UNLOADED');a=capture();assert a['note_energy']<1e-5,a
  print('PASS real JACK stereo passthrough/gain/MIDI audio, Note Off, 200 mode edits; stats='+json.dumps(stats))
  command('QUIT');client.wait(timeout=5);assert client.returncode==0
 except Exception:
  log.flush();log.seek(0);print(log.read()[-6000:],file=sys.stderr);raise
 finally:
  if client and client.poll() is None:client.terminate();client.wait(timeout=5)
  daemon.terminate()
  try:daemon.wait(timeout=5)
  except subprocess.TimeoutExpired:daemon.kill();daemon.wait()
  log.close()
