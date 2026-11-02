"""Regressions for wide schemes and unspecified alkene stereochemistry."""
from pathlib import Path
import copy
import io
import json
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import ai_assistant as ai
from ai_plugins import draw_plugin as plugin
from chemistry_open import Chem, identity, one_molecule, to_cdxml

PROBLEM = 'CCOC(=O)C=C1CCCC(=O)C1(C)C'

class SchemeFailures(unittest.TestCase):
    @classmethod
    def setUpClass(cls): plugin.install_plugin(vars(ai))

    def engine(self): return ai.DrawingTools({'selected': 0, 'bond_length': 14.4, 'font': 'Arial'})

    def test_unassigned_ez_survives_both_layouts(self):
        for layout in ('fast', 'rdkit'):
            engine = self.engine()
            engine.call('draw_smiles', {'smiles': PROBLEM, 'layout': layout})
            xml = engine.actions[0].text; root = ET.fromstring(xml)
            self.assertEqual(identity(one_molecule(xml, max_atoms=400)), identity(Chem.MolFromSmiles(PROBLEM)))
            # Only the potentially stereogenic double is crossed, not the carbonyls.
            self.assertEqual(len([b for b in root.iter('b') if b.get('Display') == 'Wavy']), 1)

    def test_explicit_ez_and_tetrahedral_stereo_are_retained(self):
        for smiles in ('F/C=C/F', 'F/C=C\\F', 'C[C@H](O)/C=C/C', 'C[C@@H](O)/C=C\\C'):
            mol = Chem.MolFromSmiles(smiles)
            xml = to_cdxml(mol)
            self.assertEqual(identity(one_molecule(xml)), identity(mol))
            self.assertFalse(any(b.get('Display') == 'Wavy' for b in ET.fromstring(xml).iter('b')))

    def test_unspecified_simple_alkene_stays_unspecified(self):
        mol = Chem.MolFromSmiles('CC=CC'); xml = to_cdxml(mol)
        self.assertEqual(identity(one_molecule(xml)), identity(mol))

    def test_fourth_column_and_large_y_are_supported(self):
        engine = self.engine()
        result = engine.call('draw_scheme', {'x': 2560, 'y': 5000, 'steps': [
            {'reactants': ['smiles:CCO'], 'products': ['smiles:'+PROBLEM], 'conditions': 'step 1'},
            {'reactants': ['smiles:'+PROBLEM], 'products': ['smiles:CC=O'], 'conditions': 'step 2'}]})
        self.assertTrue(result['staged']); self.assertGreaterEqual(result['bounds'][0], 2500)
        self.assertGreater(result['bounds'][1], 4900)

    def test_real_host_limit_and_spacing_limits_still_apply(self):
        engine = self.engine()
        step = {'reactants': ['smiles:CCO'], 'products': ['smiles:CC=O']}
        for args in ({'x': 200001, 'steps': [step]}, {'x': 2560, 'gap': 999, 'steps': [step]}):
            with self.assertRaises(ValueError): engine.call('draw_scheme', args)
            self.assertEqual(engine.actions, []); self.assertEqual(engine.scene, [])

    def test_failed_step_diagnostics_reach_dsh_and_disk(self):
        engine = self.engine(); original = ai.DrawingTools.molecule
        def fail_one(self, smiles, args, source=None):
            if smiles == 'C#N':
                raise ai.LayoutError('test layout failure', {'stage': 'stereo_validation', 'attempts': [{'strategy': 'rdkit', 'ok': False, 'error': 'test identity mismatch'}]})
            return original(self, smiles, args, source)
        calls = [{'tool': 'draw_scheme', 'args': {'steps': [
            {'reactants': ['smiles:CCO'], 'products': ['smiles:CC=O']},
            {'reactants': ['smiles:C#N'], 'products': ['smiles:CCO']} ]}}]
        with patch.object(ai.DrawingTools, 'molecule', fail_one):
            runs = ai._advanced_execute(engine, calls, 40, None)
        self.assertFalse(runs[0]['ok']); self.assertEqual(runs[0]['diagnostics']['scheme_step'], 2)
        self.assertEqual(engine.actions, [])
        result = ai._advanced_result(None, engine, [], '', runs)
        self.assertIn('test identity mismatch', result.findtext('execution/diagnostics'))
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)/'result.xml'
            with patch.object(sys, 'argv', ['worker', str(output)]), patch.object(sys, 'stdin') as stdin, patch.object(ai, 'run', return_value=result):
                stdin.buffer = io.BytesIO(b'<request/>'); ai.main()
            report = json.loads((output.parent/'last-execution-report.json').read_text(encoding='utf-8'))
            self.assertEqual(report['executions'][0]['diagnostics']['scheme_step'], 2)

if __name__ == '__main__': unittest.main(verbosity=2)
