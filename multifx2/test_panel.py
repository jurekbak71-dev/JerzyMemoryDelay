import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

spec=importlib.util.spec_from_file_location("multifx_panel",Path(__file__).parent/"web_panel.py")
app=importlib.util.module_from_spec(spec)
spec.loader.exec_module(app)

class MultiFX2Tests(unittest.TestCase):
    def test_catalog_contains_every_delay_parameter(self):
        with tempfile.TemporaryDirectory() as folder:
            modules=Path(folder)
            for name in ("memory_delay","compressor","overdrive","tremolo"):
                raw=(Path(__file__).parent/"manifests"/(name+".json")).read_text(encoding="utf-8")
                (modules/(name+".json")).write_text(raw)
                (modules/(name+".so")).touch()
            old=app.MODULE_DIR
            try:
                app.MODULE_DIR=modules
                cat=app.catalog()
                self.assertEqual(set(cat),{"memory_delay","compressor","overdrive","tremolo"})
                delay=cat["memory_delay"]
                self.assertEqual(len(delay["params"]),14)
                self.assertEqual([p["key"] for p in delay["params"]],
                    ["blend","level","feedback","mod_rate","depth","delay","model",
                     "wave","bypass","boost","squelch","mod_enable","tempo_sync","division"])
                snap={"chain":[{"key":"memory_delay","enabled":True,
                   "params":[p["def"] for p in delay["params"]]}],"bpm":120,"master":.7}
                self.assertEqual(app.clean_snapshot(snap,cat)["chain"][0]["params"],snap["chain"][0]["params"])
                corrupt=json.loads(json.dumps(snap))
                corrupt["chain"][0]["params"].pop()
                with self.assertRaises(ValueError):
                    app.clean_snapshot(corrupt,cat)
            finally:
                app.MODULE_DIR=old

    def test_atomic_preset_write(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/"presets.json"
            app.save_json(path,{"test":{"bpm":120}})
            self.assertEqual(json.loads(path.read_text())["test"]["bpm"],120)
            self.assertFalse(path.with_suffix(".json.tmp").exists())

    def test_invalid_catalog_ignored(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)
            (path/"unexpected.json").write_text('{"key":"bad"}')
            (path/"unexpected.so").touch()
            old=app.MODULE_DIR
            try:
                app.MODULE_DIR=path
                self.assertEqual(app.catalog(),{})
            finally:
                app.MODULE_DIR=old

if __name__=="__main__":unittest.main()
