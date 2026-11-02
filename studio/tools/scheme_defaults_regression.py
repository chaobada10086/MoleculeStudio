"""Regression for draw_scheme group defaults and per-step overrides."""
from pathlib import Path
import copy
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import ai_assistant as ai
from ai_plugins import draw_plugin as plugin

class SchemeDefaults(unittest.TestCase):
    @classmethod
    def setUpClass(cls): plugin.install_plugin(vars(ai))

    def engine(self): return ai.DrawingTools({'selected': 0, 'bond_length': 14.4, 'font': 'Arial'})

    def test_group_label_size_and_per_step_override(self):
        engine = self.engine()
        steps = [
            {'reactants': ['smiles:CCO'], 'products': ['smiles:CC=O'], 'reactant_labels': ['R1'], 'product_labels': ['P1']},
            {'reactants': ['smiles:CC=O'], 'products': ['smiles:CC(=O)O'], 'reactant_labels': ['R2'], 'product_labels': ['P2'], 'label_size': 12},
        ]
        original = copy.deepcopy(steps)
        result = engine.call('draw_scheme', {'label_size': 18, 'steps': steps})
        self.assertTrue(result['staged'])
        texts = {node.text: float(node.get('size')) for node in engine.actions if node.tag == 'add_text'}
        self.assertEqual(texts, {'R1': 18, 'P1': 18, 'R2': 12, 'P2': 12})
        self.assertEqual(steps, original)

    def test_other_group_defaults_reach_rows(self):
        engine = self.engine()
        result = engine.call('draw_scheme', {'labels': True, 'color': '#0000FF', 'width': .8, 'arrow': 'both', 'steps': [
            {'reactants': ['smiles:CCO'], 'products': ['smiles:CC=O']},
            {'reactants': ['smiles:CC=O'], 'products': ['smiles:CCO'], 'color': '#FF0000', 'width': .5, 'arrow': 'forward'},
        ]})
        molecules = [node for node in engine.actions if node.tag == 'molecule']
        self.assertEqual([n.get('color') for n in molecules], ['#0000FF']*2+['#FF0000']*2)
        self.assertEqual([float(n.get('width')) for n in molecules], [.8]*2+[.5]*2)
        self.assertEqual([n.get('style') for n in engine.actions if n.tag == 'draw_arrow'], ['both', 'forward'])
        self.assertTrue(all(item['label_ref'] for row in result['steps'] for item in row['reactants']+row['products']))

    def test_group_gap_is_default_for_each_reaction(self):
        engine = self.engine()
        result = engine.call('draw_scheme', {'gap': 48, 'steps': [{'reactants': ['smiles:CCO'], 'products': ['smiles:CC=O']}]})
        row = result['steps'][0]
        self.assertAlmostEqual(row['arrow_endpoints'][0][0]-row['reactants'][0]['bounds'][2], 48, places=2)

    def test_invalid_defaults_are_rejected_atomically(self):
        for bad in ({'label_size': 37}, {'label_size': True}, {'labels': 'yes'}, {'color': 'invalid'}, {'width': 99}, {'arrow': 'invalid'}, {'gap': 999}):
            engine = self.engine()
            step = {'reactants': ['smiles:CCO'], 'products': ['smiles:CC=O'], 'label_size': 12, 'color': '#000000', 'width': 1}
            with self.assertRaises(ValueError): engine.call('draw_scheme', {**bad, 'steps': [step]})
            self.assertEqual(engine.actions, []); self.assertEqual(engine.scene, [])

if __name__ == '__main__': unittest.main(verbosity=2)
