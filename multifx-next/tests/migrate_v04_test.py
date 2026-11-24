#!/usr/bin/env python3
"""CLI migration tests: real temporary files; no DSP binaries are executed."""
import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import importlib.util
from unittest.mock import patch
import unittest

TOOL = Path(__file__).resolve().parents[1] / 'tools/migrate_v04.py'

class MigrationTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.modules = self.root / 'modules'
        self.plugins = self.root / 'plugins'
        self.irs = self.root / 'irs'
        for p in (self.modules, self.plugins, self.irs):
            p.mkdir()
        for key in ('legacy', 'cab_dual'):
            p = self.plugins / key
            p.mkdir()
            (p / 'plugin.so').write_bytes(b'filesystem fixture, not executable DSP')
        (self.modules / 'tremolo.so').write_bytes(b'filesystem fixture')
        (self.modules / 'tremolo.json').write_text(json.dumps({
            'key':'tremolo', 'params':[{'key':'rate','def':.33}, {'key':'depth','def':.55}]}))
        self.source = self.root / 'old.json'
        self.output = self.root / 'new.json'
        self.data = {'chain':[{'key':'tremolo','params':[.2,.8],'enabled':False,'route':0}],
                     'bpm':130,'master':.5}

    def run_tool(self, data=None, *extra):
        if data is not None:
            self.source.write_text(json.dumps(data))
        before = self.source.read_bytes()
        result = subprocess.run([sys.executable, str(TOOL), str(self.source),
            '--modules-dir',str(self.modules),'--plugins-dir',str(self.plugins),
            '--ir-dir',str(self.irs),'--output',str(self.output),*extra],
            text=True,capture_output=True)
        self.assertEqual(self.source.read_bytes(),before)
        return result

    def test_full_historical_gui_ir_filename(self):
        name='a'*100+'.wav'
        (self.irs/name).write_bytes(b'path fixture, decoding is tested separately')
        self.data['cab']={'ir0':name}
        result=self.run_tool(self.data)
        self.assertEqual(result.returncode,0,result.stderr)
    def test_cli_preserves_fx_parameters_bypass_and_master(self):
        r = self.run_tool(self.data)
        self.assertEqual(r.returncode,0,r.stderr)
        j = json.loads(self.output.read_text())
        self.assertEqual((j['schema_version'],j['automatic'],j['bpm'],j['master']),(1,True,130,.7))
        self.assertEqual((j['audio_edges'],j['midi_routes']),([],[]))
        s = j['slots'][0]
        self.assertEqual((s['id'],s['plugin_id'],s['version'],s['mode']),(1,'legacy','1.0.0','BYPASS'))
        self.assertEqual(s['parameters'],{f'p{i}':([.2,.8][i] if i<2 else .5) for i in range(48)})
        self.assertEqual(s['ranges'],{f'p{i}':[0,1] for i in range(48)})
        self.assertEqual(s['asset'],str(self.modules/'tremolo.so'))
        self.assertEqual(s['path'],str(self.plugins/'legacy/plugin.so'))

    def test_defaults_come_from_manifest(self):
        del self.data['chain'][0]['params']
        r=self.run_tool(self.data)
        self.assertEqual(r.returncode,0,r.stderr)
        s=json.loads(self.output.read_text())['slots'][0]
        self.assertEqual([s['parameters']['p0'],s['parameters']['p1']],[.33,.55])

    def test_two_ir_assets_and_align_dry(self):
        for name in ('a.wav','b.wav'):(self.irs/name).write_bytes(b'RIFF fixture')
        self.data['cab']={'on':1,'mix':.4,'blend':.3,'level':.6,'lowcut':.1,'highcut':.9,'ir0':'a.wav','ir1':'b.wav'}
        r=self.run_tool(self.data)
        self.assertEqual(r.returncode,0,r.stderr)
        s=json.loads(self.output.read_text())['slots'][-1]
        self.assertEqual((s['plugin_id'],s['latency']),( 'cab_dual',64))
        a=json.loads(Path(s['asset']).read_text())
        self.assertEqual(a,{'ir_a':str(self.irs/'a.wav'),'ir_b':str(self.irs/'b.wav'),'dry_mode':'legacy'})
        self.output.unlink();Path(s['asset']).unlink()
        r=self.run_tool(self.data,'--align-dry')
        self.assertEqual(r.returncode,0,r.stderr)
        s=json.loads(self.output.read_text())['slots'][-1]
        self.assertEqual(json.loads(Path(s['asset']).read_text())['dry_mode'],'aligned')
        self.assertIn('WARNING',r.stderr)

    def test_bass_route_excluded_and_midi_positions_preserved(self):
        self.data['chain'].insert(0,{'key':'tremolo','enabled':True,'params':[.1,.2],'route':1})
        self.data['synth']={'bass':1,'pad':0}
        self.data['midi']=[{'channel':1,'cc':10,'target':{'kind':'fx','position':0,'key':'tremolo','index':1}},
            {'channel':2,'cc':11,'target':{'kind':'fx','position':1,'key':'tremolo','index':0}},
            {'channel':3,'cc':12,'target':{'kind':'master'}},
            {'channel':4,'cc':13,'target':{'kind':'bpm'}},
            {'channel':5,'cc':14,'target':{'kind':'synth.bass'}}]
        r=self.run_tool(self.data)
        self.assertEqual(r.returncode,0,r.stderr)
        j=json.loads(self.output.read_text())
        self.assertEqual(len(j['slots']),1)
        self.assertNotIn('synth',j)
        self.assertEqual(j['midi_bindings'],[
            {'channel':2,'cc':11,'target':1,'param':0,'min':0,'max':1},
            {'channel':3,'cc':12,'target':0,'param':0,'min':0,'max':1.4},
            {'channel':4,'cc':13,'target':-1,'param':0,'min':20,'max':300}])
        self.assertIn('route 1',r.stderr)
        self.assertIn('synth',r.stderr)
        self.assertIn('CC 10',r.stderr)

    def test_invalid_inputs_leave_no_output_family(self):
        mutations=[lambda d:d.update(unknown=1),lambda d:d.update(bpm=True),
            lambda d:d.update(master=2),lambda d:d.update(bpm=10**1000),lambda d:d['chain'][0].update(key='../tremolo'),
            lambda d:d['chain'][0].update(params=[.3]),lambda d:d['chain'][0].update(enabled=1),
            lambda d:d['chain'][0].update(route=2),lambda d:d.update(synth={'invented':0}),
            lambda d:d.update(cab={'ir0':'../outside.wav'}),
            lambda d:d.update(midi=[{'channel':True,'cc':1,'target':{'kind':'master'}}]),
            lambda d:d.update(midi=[{'channel':1,'cc':1,'target':{'kind':'fx','position':0,'key':'wrong','index':0}}])]
        for mutate in mutations:
            with self.subTest(mutate=mutate):
                d=copy.deepcopy(self.data);mutate(d)
                r=self.run_tool(d)
                self.assertEqual(r.returncode,2,r.stderr)
                self.assertIn('ERROR:',r.stderr)
                self.assertFalse(self.output.exists())
                self.assertEqual(list(self.root.glob('new.*')),[])

    def test_existing_output_and_asset_never_overwritten(self):
        self.output.write_text('sentinel')
        r=self.run_tool(self.data)
        self.assertNotEqual(r.returncode,0)
        self.assertEqual(self.output.read_text(),'sentinel')
        self.output.unlink()
        (self.irs/'a.wav').write_bytes(b'RIFF fixture')
        self.data['cab']={'ir0':'a.wav'}
        asset=self.output.with_name(self.output.stem+'.cab.json')
        asset.write_text('existing asset')
        r=self.run_tool(self.data)
        self.assertNotEqual(r.returncode,0)
        self.assertEqual(asset.read_text(),'existing asset')
        self.assertFalse(self.output.exists())

    def test_cab_cc_targets_follow_descriptor_parameter_order(self):
        (self.irs/'a.wav').write_bytes(b'RIFF fixture')
        self.data['cab']={'ir0':'a.wav','on':0}
        fields=('mix','blend','level','lowcut','highcut','on')
        self.data['midi']=[{'channel':1,'cc':i,'target':{'kind':'cab.'+field}}
                           for i,field in enumerate(fields)]
        r=self.run_tool(self.data)
        self.assertEqual(r.returncode,0,r.stderr)
        j=json.loads(self.output.read_text())
        self.assertEqual(j['slots'][-1]['parameters']['enabled'],0)
        self.assertEqual(j['midi_bindings'],[
            {'channel':1,'cc':i,'target':2,'param':i,'min':0,'max':1} for i in range(6)])

    def test_no_ir_keeps_dry_chain_and_reports_dropped_cab_binding(self):
        self.data.update(cab={'on':1},midi=[{'channel':1,'cc':9,'target':{'kind':'cab.mix'}}])
        r=self.run_tool(self.data)
        self.assertEqual(r.returncode,0,r.stderr)
        j=json.loads(self.output.read_text())
        self.assertEqual(len(j['slots']),1)
        self.assertEqual(j['midi_bindings'],[])
        self.assertIn('no IR',r.stderr)
        self.assertIn('CC 9',r.stderr)

    def test_duplicate_json_keys_rejected(self):
        self.source.write_text('{"chain":[],"bpm":120,"master":0.5,"master":0.8}')
        r=self.run_tool()
        self.assertNotEqual(r.returncode,0)
        self.assertIn('duplicate JSON key',r.stderr)
        self.assertFalse(self.output.exists())

    def test_invalid_midi_and_nonfinite_values_rejected(self):
        for rows in ([{'channel':1,'cc':1,'target':{'kind':'unknown'}}],
                     [{'channel':1,'cc':1,'target':{'kind':'master','extra':1}}],
                     [{'channel':1,'cc':1,'target':{'kind':'master'}}]*2):
            with self.subTest(rows=rows):
                d=copy.deepcopy(self.data);d['midi']=rows
                r=self.run_tool(d)
                self.assertNotEqual(r.returncode,0)
                self.assertIn('ERROR:',r.stderr)
                self.assertFalse(self.output.exists())
        d=copy.deepcopy(self.data);d['bpm']=float('nan')
        r=self.run_tool(d)
        self.assertNotEqual(r.returncode,0)
        self.assertIn('non-finite',r.stderr)

    def test_source_output_identity_rejected(self):
        r=self.run_tool(self.data,'--output',str(self.source))
        self.assertNotEqual(r.returncode,0)
        self.assertIn('refusing',r.stderr)

    def test_publish_failure_removes_only_files_created_by_this_invocation(self):
        spec=importlib.util.spec_from_file_location('migrate_v04',TOOL)
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        asset=self.root/'asset.json';output=self.root/'session.json'
        sentinel=self.root/'unrelated';sentinel.write_text('keep')
        real_link=module.os.link
        def link_once(src,dst):
            if dst==output:
                raise OSError('injected second-publication failure')
            return real_link(src,dst)
        with patch.object(module.os,'link',side_effect=link_once):
            with self.assertRaisesRegex(OSError,'second-publication'):
                module.publish_family({asset:{'asset':1},output:{'session':1}})
        self.assertFalse(asset.exists());self.assertFalse(output.exists())
        self.assertEqual(sentinel.read_text(),'keep')
        self.assertEqual(list(self.root.glob('.*.tmp')),[])

    def test_missing_files_and_symlink_escape_rejected(self):
        (self.modules/'tremolo.so').unlink()
        r=self.run_tool(self.data)
        self.assertNotEqual(r.returncode,0)
        outside=self.root/'outside.so';outside.write_bytes(b'outside')
        (self.modules/'tremolo.so').symlink_to(outside)
        r=self.run_tool(self.data)
        self.assertNotEqual(r.returncode,0)
        self.assertFalse(self.output.exists())

if __name__=='__main__':unittest.main()
