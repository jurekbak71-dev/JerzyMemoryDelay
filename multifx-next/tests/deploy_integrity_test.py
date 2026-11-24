"""Metadata-only completeness regression; fake files never claimed to be DSP."""
import importlib.util,pathlib,tempfile,os
root=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('deploy',root/'installer/windows/deploy_remote.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
with tempfile.TemporaryDirectory() as tmp:
 t=pathlib.Path(tmp);meta={'backend':'alsa','source_id':'test','format':1}
 import json
 (t/'variant.json').write_text(json.dumps(meta))
 for n in ['bin/JerzyMultiFXNextALSA','start.sh']:
  p=t/n;p.parent.mkdir(parents=True,exist_ok=True);p.write_text('fixture');p.chmod(0o755)
 for plugin in ['gain','synth','generator','cab','cab_dual','legacy']:
  for name in ['plugin.so','manifest.json']:
   p=t/'plugins'/plugin/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text('fixture')
 for plugin in ['cab','cab_dual']:
  for name in ['PFFFT.txt','PFFFT-NOTICES.txt']:
   p=t/'plugins'/plugin/'assets/licenses'/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text('fixture')
 catalog=['compressor','overdrive','tremolo','memory_delay','small_clone','big_muff','attack_decay','tape_drive','mx_analog_delay','atomic_cluster','reel_echo','oil_drum_echo','lofi_crush']
 (t/'legacy-catalog').mkdir()
 for module in catalog:
  for suffix in ['.so','.json']:(t/'legacy-catalog'/(module+suffix)).write_text('fixture')
 assert m.installation_complete(t,meta)
 damaged=[t/'legacy-catalog/tremolo.so',t/'legacy-catalog/tremolo.json',t/'plugins/cab_dual/assets/licenses/PFFFT.txt']
 (t/'legacy-catalog/unrelated.so').write_text('fixture')
 for p in damaged:
  p.unlink();assert not m.installation_complete(t,meta),str(p)
  p.write_text('fixture');assert m.installation_complete(t,meta)
 print('expected module identity, manifests, both CAB license inventories: PASS')
