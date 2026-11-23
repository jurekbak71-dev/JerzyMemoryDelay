"""Actual Tk widgets and actual installed ALSA host; requires DISPLAY."""
import importlib.util,json,pathlib,sys,tempfile,time,tkinter as tk
app_path=pathlib.Path(sys.argv[1]);cmake=sys.argv[2]
spec=importlib.util.spec_from_file_location('click_app',app_path)
assert spec and spec.loader
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
with tempfile.TemporaryDirectory() as tmp:
 meta=json.loads((app_path.parent/'variant.json').read_text())
 damaged=pathlib.Path(tmp)/(meta['backend']+'-'+meta['source_id'])
 (damaged/'plugins/gain').mkdir(parents=True)
 (damaged/'variant.json').write_text(json.dumps(meta))
 (damaged/'start.sh').write_text('#!/bin/sh\n')
 (damaged/'plugins/gain/plugin.so').write_bytes(b'incomplete')
 root=tk.Tk()
 app=module.App(root,bundle=app_path.parent,prefix=pathlib.Path(tmp))
 assert app.state=='new','missing executable advertised installed' 
 def until(check,timeout=8):
  end=time.monotonic()+timeout
  while time.monotonic()<end:
   root.update()
   if check():return
   time.sleep(.01)
  raise AssertionError(app.status.get())
 try:
  # An invalid source install must yield visible error, never enable Start.
  app.cmake='/definitely-missing-cmake'
  app.install_button.invoke()
  until(lambda:app.state=='error')
  assert 'Nie udało' in app.status.get() and str(app.start_button['state'])=='disabled'
  # Reuse only a complete, installed variant, then control real host through buttons.
  app.cmake=cmake
  app.install_button.invoke()
  until(lambda:app.state in ('ready','error'),timeout=120)
  assert app.state=='ready',app.status.get()
  assert list(pathlib.Path(tmp).glob('*.incomplete-*')),'incomplete install not preserved'
  app.refresh_installation()
  assert app.state=='ready'
  if app.kind=='alsa':app.device.set('Test bez karty — bez dźwięku')
  app.start_button.invoke();until(lambda:app.state=='running')
  until(lambda:app.last_stats.get('blocks',0)>0)
  app.gain.set(.5);app.effect_button.invoke()
  until(lambda:app.effect_active)
  assert app.last_error is None,app.last_error
  app.effect_button.invoke();until(lambda:not app.effect_active)
  app.effect_button.invoke()
  app.stop_button.invoke();until(lambda:app.state=='ready')
  for _ in range(30):root.update();time.sleep(.01)
  assert app.state=='ready','stale effect result revived a stopped session'
  assert app.host is None
  app.start_button.invoke();until(lambda:app.state=='running')
  plugin=app.target/'plugins/gain/plugin.so'
  plugin.rename(plugin.with_suffix('.hidden'))
  failed_child=app.host
  app.effect_button.invoke();until(lambda:app.state=='error')
  assert app.host is None and failed_child.poll() is not None,'effect error orphaned audio'
  plugin.with_suffix('.hidden').rename(plugin)
  app.refresh_installation()
  app.start_button.invoke();until(lambda:app.state=='running')
  child=app.host
  app.close();assert child.poll() is not None,'window close left host running'
 finally:
  try:
   if root.winfo_exists():app.close()
  except tk.TclError:pass
print('actual Tk buttons: failed build, install, reuse, audio start, gain, stats, stop, close: PASS')
