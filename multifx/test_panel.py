import importlib.util
import tempfile
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location("web_panel", Path(__file__).with_name("web_panel.py"))
panel = importlib.util.module_from_spec(spec)
spec.loader.exec_module(panel)

class PanelTests(unittest.TestCase):
    def test_default_matches_dsp(self):
        self.assertEqual(len(panel.DEFAULT_PARAMS), 13)
        self.assertEqual(panel.state["enabled"], [False,False,False,True])
        self.assertEqual(panel.state["order"], [0,1,2,3])
        self.assertEqual(panel.valid_state(panel.state), panel.state)

    def test_validate_rejects_invalid_values(self):
        original = panel.valid_state(panel.state)
        for bad in [-1, 2, float("nan"), float("inf"), True, "0.3"]:
            value = panel.valid_state(original)
            value["params"][0] = bad
            with self.assertRaises((TypeError, ValueError)):
                panel.valid_state(value)
        bad = panel.valid_state(original)
        bad["order"] = [0,0,2,3]
        with self.assertRaises(ValueError):
            panel.valid_state(bad)
        bad = panel.valid_state(original)
        bad["bpm"] = 10000
        with self.assertRaises(ValueError):
            panel.valid_state(bad)

    def test_presets_survive_restart(self):
        with tempfile.TemporaryDirectory() as directory:
            previous_file, previous_presets = panel.FILE, panel.presets
            try:
                panel.FILE = Path(directory) / "presets.json"
                panel.presets = {"Guitar clean": panel.valid_state(panel.state)}
                panel.save_file()
                panel.presets = {}
                panel.load_file()
                self.assertIn("Guitar clean", panel.presets)
                self.assertEqual(panel.presets["Guitar clean"]["order"], [0,1,2,3])
            finally:
                panel.FILE, panel.presets = previous_file, previous_presets

if __name__ == "__main__":
    unittest.main()
