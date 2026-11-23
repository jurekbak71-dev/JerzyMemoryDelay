"""Existing broken target must never be advertised ready."""
import importlib.util,json,pathlib,shutil,tempfile,subprocess,contextlib,io
root=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('remote',root/'installer/windows/deploy_remote.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
with tempfile.TemporaryDirectory(dir='/workspace/work') as tmp:
 home=pathlib.Path(tmp);bundle=home/'bundle';bundle.mkdir()
 meta={'backend':'alsa','source_id':'broken-test','format':1}
 (bundle/'variant.json').write_text(json.dumps(meta))
 target=home/'.local/opt/jerzy-multifx-next/alsa-broken-test';(target/'bin').mkdir(parents=True)
 (target/'variant.json').write_text(json.dumps(meta))
 shutil.copy2('/workspace/work/next-build/JerzyMultiFXNextALSA',target/'bin/JerzyMultiFXNextALSA')
 (bundle/'click_app.py').write_text('')
 output=io.StringIO()
 try:
  with contextlib.redirect_stdout(output):m.deploy(bundle,home=home)
 except (OSError,RuntimeError,subprocess.CalledProcessError):pass
 assert 'JFX_INSTALL_OK' not in output.getvalue(),'incomplete target reported successful'
print('partial existing target never yields success marker: PASS')
