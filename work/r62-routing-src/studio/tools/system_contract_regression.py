"""System checks across schemas, staging, recognition, and result transport."""
from pathlib import Path
import ast
import copy
from contextlib import contextmanager
import inspect
import io
import json
import math
import re
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import ai_assistant as ai
import ai_dsh
import ai_ocsr_bridge as ocsr
from ai_plugins import draw_plugin as plugin
from chemistry_open import Chem, to_cdxml

@contextmanager
def fake_service(base):
    yield lambda *args, **kwargs: ET.Element('result', {'ok': 'true'})

class SystemContract(unittest.TestCase):
    @classmethod
    def setUpClass(cls): plugin.install_plugin(vars(ai))

    def engine(self): return ai.DrawingTools({'selected': 0, 'bond_length': 14.4, 'font': 'Arial'})

    def molecule(self):
        engine = self.engine(); engine.call('draw_smiles', {'smiles': 'CCO', 'x': 0, 'y': 0}); return engine

    def assert_bonds_consistent(self, engine):
        atoms = {o['ref']: o for o in engine.scene if o['kind'] == 'atom'}
        for bond in (o for o in engine.scene if o['kind'] == 'bond'):
            a, b = atoms[bond['begin']], atoms[bond['end']]
            self.assertEqual([bond[k] for k in ('x','y','x2','y2')],
                             [min(a['x'],b['x']), min(a['y'],b['y']), max(a['x'],b['x']), max(a['y'],b['y'])])

    def test_atom_move_updates_touching_bonds(self):
        engine = self.molecule(); atom = next(o for o in engine.scene if o['kind'] == 'atom')
        engine.call('move_objects', {'target': atom['ref'], 'dx': 50, 'dy': 20})
        self.assert_bonds_consistent(engine)

    def test_bond_transform_moves_endpoints(self):
        engine = self.molecule(); bond = next(o for o in engine.scene if o['kind'] == 'bond')
        atoms = {o['ref']: o for o in engine.scene if o['kind'] == 'atom'}
        a, b = atoms[bond['begin']], atoms[bond['end']]
        before = math.hypot(a['x']-b['x'], a['y']-b['y'])
        engine.call('transform_objects', {'target': bond['ref'], 'scale': 2, 'angle': 30})
        self.assertAlmostEqual(math.hypot(a['x']-b['x'], a['y']-b['y']), before*2)
        self.assert_bonds_consistent(engine)

    def test_text_scale_preserves_point_size_like_native_host(self):
        engine = self.engine(); ref = engine.call('add_text', {'text': 'caption', 'x': 0, 'y': 0, 'size': 10})['ref']
        before = copy.deepcopy(engine.scene[0])
        engine.call('transform_objects', {'target': ref, 'scale': 2})
        after = engine.scene[0]
        self.assertEqual(after['size'], before['size'])
        self.assertAlmostEqual(after['x2']-after['x'], before['x2']-before['x'])
        self.assertAlmostEqual(after['y2']-after['y'], before['y2']-before['y'])

    def test_computed_overflow_rejected_before_commit(self):
        engine = self.engine(); engine.call('add_text', {'text': 'keep', 'x': 0, 'y': 0})
        before = [ET.tostring(n) for n in engine.actions], copy.deepcopy(engine.scene), engine.created_count
        with self.assertRaises(ValueError):
            engine.call('draw_scheme', {'x': 199999, 'steps': [{'reactants': ['smiles:CCO'], 'products': ['smiles:CC=O']}]})
        self.assertEqual(before, ([ET.tostring(n) for n in engine.actions], engine.scene, engine.created_count))

    def test_path_helper_moves_actual_xml_points(self):
        engine = self.engine(); ref = engine.call('draw_path', {'points': [[0,0],[10,10]]})['ref']
        plugin._shift_handle(engine, ref, 30, 40)
        points = [(float(p.get('x')), float(p.get('y'))) for p in engine.actions[0].findall('point')]
        self.assertEqual(points, [(30,40),(40,50)])
        self.assertEqual([engine.scene[0][k] for k in ('x','y','x2','y2')], [30,40,40,50])

    def test_single_recognition_respects_quality_gate(self):
        engine = self.engine()
        attachments = type('Attachments', (), {'sources': [{'kind':'image', 'path':Path('fixture.png')}], 'pdfs':[]})()
        response = ET.fromstring('<result insertable="0"><warning>stereo not verified</warning></result>')
        with patch.object(ocsr, 'service_session', fake_service), patch.object(ocsr, '_recognize', return_value=response), patch.object(ocsr, '_load_cdxml') as load:
            with self.assertRaisesRegex(ValueError, '质量门'):
                ocsr.recognize_and_draw(engine, attachments, {'attachment':1}, Path('.'), ai.number, ai.style)
            load.assert_not_called()
        self.assertEqual(engine.actions, []); self.assertFalse(engine._ocsr_staged)

    def test_candidate_only_recognition_never_claims_staging(self):
        engine = self.engine()
        attachments = type('Attachments', (), {'sources': [{'kind':'image', 'path':Path('fixture.png')}], 'pdfs':[]})()
        response = ET.Element('result', {'insertable':'1'})
        root = ET.fromstring(to_cdxml(Chem.MolFromSmiles('CCO')))
        with patch.object(ocsr, 'service_session', fake_service), patch.object(ocsr, '_recognize', return_value=response), patch.object(ocsr, '_load_cdxml', return_value=(root,list(root.iter('n')))):
            result = ocsr.recognize_regions(engine, attachments, {'attachment':1, 'regions':[[0,0,1,1]], 'stage':False}, Path('.'), ai.number, ai.style)
        self.assertEqual(result['staged'], 0); self.assertEqual(engine.actions, [])
        self.assertIn('未暂存', ''.join(result['warnings']))
        self.assertNotIn('结构已暂存', ''.join(result['warnings']))

    def test_recognition_option_types_are_safe_without_plugin(self):
        engine = self.engine()
        attachments = type('Attachments', (), {'sources': [{'kind':'image', 'path':Path('fixture.png')}], 'pdfs':[]})()
        for bad in ({'stage':'false'}, {'max_regions':1.5}, {'max_regions':0}, {'min_score':float('nan')}, {'min_score':2}, {'min_height':-1}):
            with self.assertRaises(ValueError):
                ocsr.recognize_regions(engine, attachments, {'attachment':1, **bad}, Path('.'), ai.number, ai.style)
        self.assertEqual(engine.actions, [])

    def test_nested_json_cannot_replace_real_plan(self):
        text = '{"plan":[{"tool":"add_text","args":{"text":"actual"}}],"metadata":{"plan":[]}}'
        self.assertEqual(ai_dsh.extract_object(text, ('plan',))['plan'][0]['args']['text'], 'actual')

    def test_last_complete_plan_is_used(self):
        self.assertEqual(ai_dsh.extract_object('log {bad}\n{"plan":[]}\n```json\n{"plan":[1]}\n```')['plan'], [1])

    def test_failed_call_arguments_are_retained_safely(self):
        engine = self.engine(); args = {'text':'bad\ud800', 'x':0, 'y':0}
        runs = ai._advanced_execute(engine, [{'tool':'add_text','args':args}], 120, None)
        result = ai._advanced_result(None, engine, [], '', runs)
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)/'result.xml'
            with patch.object(sys, 'argv', ['worker',str(output)]), patch.object(sys, 'stdin') as stdin, patch.object(ai, 'run', return_value=result):
                stdin.buffer = io.BytesIO(b'<request/>'); ai.main()
            report = json.loads((output.parent/'last-execution-report.json').read_text(encoding='utf-8'))
            self.assertEqual(report['executions'][0]['arguments'], args)
            ET.parse(output)

    def test_all_registered_tools_reject_unknown_fields_atomically(self):
        engine = self.molecule()
        before = [ET.tostring(n) for n in engine.actions], copy.deepcopy(engine.scene), engine.created_count
        names = [entry['function']['name'] for entry in ai.TOOLS]
        self.assertEqual(len(names), len(set(names)))
        for name in names:
            with self.subTest(tool=name):
                with self.assertRaises(ValueError): engine.call(name, {'__unknown_audit_field':1})
                self.assertEqual(before, ([ET.tostring(n) for n in engine.actions], engine.scene, engine.created_count))
                self.assertEqual(engine._plugin_call_depth, 0)

    def test_native_action_allowlist_matches_compiled_bridge_source(self):
        source = Path(ai.__file__).with_name('ai_actions.hpp')
        if not source.exists(): self.skipTest('source-only contract comparison')
        native = set(re.findall(r'p\.kind\s*==\s*"([a-z_]+)"', source.read_text(encoding='utf-8')))
        tree = ast.parse(inspect.getsource(ai.validate_native_actions))
        allowed = next(ast.literal_eval(n.value) for n in ast.walk(tree) if isinstance(n, ast.Assign) and any(isinstance(t,ast.Name) and t.id=='allowed' for t in n.targets))
        self.assertEqual(allowed, native)

if __name__ == '__main__': unittest.main(verbosity=2)
