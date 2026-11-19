from pathlib import Path
import sys,unittest,xml.etree.ElementTree as ET
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import ai_assistant as ai
from ai_plugins import draw_plugin as p
p.install_plugin(vars(ai))
def engine():return ai.DrawingTools({'selected':0,'bond_length':14.4,'font':'Arial'})
def args():return {'reactants':['smiles:CCO'],'products':['smiles:CC=O'],'reagents':['NaBH4 (1.2 equiv)','CeCl3 (0.5 equiv)'],'conditions_below':'CH3OH\n0 °C, 2 h, N2'}
class ReactionStyle(unittest.TestCase):
 def test_formula(self):
  self.assertEqual(p._condition_formula('NaBH4 (1.2 equiv), CH2Cl2, 25 °C, 2 h, N2'), 'NaBH₄ (1.2 equiv), CH₂Cl₂, 25 °C, 2 h, N₂')
  self.assertEqual(p._condition_formula('SO4^2-, ^13CH3OH'),'SO₄²⁻, ¹³CH₃OH')
  self.assertEqual(p._condition_formula('Fe3+, NH4+, Cl-'),'Fe³⁺, NH₄⁺, Cl⁻')
 def test_above_below_compact(self):
  e=engine();r=e.call('draw_reaction',args());nodes=[n for n in e.actions if n.tag=='add_text'];self.assertEqual(len(nodes),4)
  y=r['arrow_endpoints'][0][1];self.assertTrue(all(float(n.get('y'))<y for n in nodes[:2]));self.assertTrue(all(float(n.get('y'))>y for n in nodes[2:]))
  self.assertLessEqual(r['arrow_endpoints'][1][0]-r['arrow_endpoints'][0][0],72)
  self.assertEqual(len(r['condition_refs']),4);ai.validate_native_actions(e.actions)
 def test_crowded_reagents_and_overrides(self):
  a={'reagents':['A','B','C','D','E']};self.assertEqual(p._condition_lines(a)[0],['A, B','C, D','E'])
  a['reagents_per_line']=1;self.assertEqual(len(p._condition_lines(a)[0]),5)
  self.assertEqual(p._condition_lines({'conditions':'CH2Cl2\nN2','formula_layout':False})[0],['CH2Cl2','N2'])
 def test_scheme_moves_all_conditions(self):
  e=engine();r=e.call('draw_scheme',{'steps':[args(),args()]});rows=r['steps'];self.assertEqual(len(rows[0]['condition_refs']),4)
  for row in rows:
   y=row['arrow_endpoints'][0][1];objects=[o for o in e.scene if o['owner'] in row['condition_refs']];self.assertTrue(any(o['y']>y for o in objects));self.assertTrue(any(o['y']<y for o in objects))
  self.assertLess(rows[0]['bounds'][3],rows[1]['bounds'][1])
 def test_legacy_and_explicit_length(self):
  e=engine();r=e.call('draw_reaction',{'reactants':['smiles:C'],'products':['smiles:CO'],'conditions':'heat','arrow_length':120});self.assertAlmostEqual(r['arrow_endpoints'][1][0]-r['arrow_endpoints'][0][0],120,places=2)
 def test_invalid_atomic(self):
  for bad in ({'reagents_per_line':True},{'reagents':['A\nB']},{'formula_layout':'yes'},{'conditions_below':12}):
   e=engine()
   with self.assertRaises(ValueError):e.call('draw_reaction',{**args(),**bad})
   self.assertEqual(e.actions,[])
if __name__=='__main__':unittest.main(verbosity=2)
