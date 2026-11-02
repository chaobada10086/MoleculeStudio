import sys,json,copy,xml.etree.ElementTree as ET
from pathlib import Path
ROOT=Path(__file__).resolve().parent.parent
sys.path.insert(0,str(ROOT))
import ai_assistant as ai
from ai_plugins import draw_plugin as dp
from chemistry_open import Chem,identity,one_molecule

def engine():
 dp.install_plugin(vars(ai))
 return ai.DrawingTools({'selected':0,'bond_length':14.4,'font':'Times New Roman'})

def main():
 e=engine()
 record=json.loads((Path(__file__).parent/'fixtures/palytoxin.json').read_text(encoding='utf-8'))['matches'][0]
 e.compounds[record['cid']]=record
 for mode in ('rdkit','fast'):
  result=e.call('draw_compound',{'cid':record['cid'],'layout':mode})
  xml=e.actions[-1].text
  parsed=one_molecule(xml,max_atoms=400)
  assert identity(parsed)==identity(Chem.MolFromSmiles(record['smiles']))
  assert result['diagnostics']['stereocenters']==64
  (Path(__file__).parent/f'palytoxin-{mode}.cdxml').write_text(xml,encoding='utf-8')
 print('PASS palytoxin: both coordinate engines, 186 atoms, 64 stereocenters, exact isomeric identity')
 # Rejected stereochemistry remains rejected; forced serializer error produces useful diagnostics.
 import chemistry_open
 orig=chemistry_open.to_cdxml
 def broken(*a,**k): raise ValueError('forced validation failure')
 chemistry_open.to_cdxml=broken
 count=len(e.actions)
 try:
  try:e.call('draw_smiles',{'smiles':'CCO','layout':'rdkit'});raise AssertionError('must fail')
  except ai.LayoutError as ex:
   assert {'stage','atoms','rings','stereocenters','suggestion'} <= set(ex.diagnostics)
   assert ex.diagnostics['stage']=='stereo_validation'
  assert len(e.actions)==count
 finally: chemistry_open.to_cdxml=orig
 print('PASS failure diagnostics, no partial molecule action')
 # Former backend 200-atom limit must not contradict the bridge 400-atom limit.
 e.call('draw_smiles',{'smiles':'C'*201,'layout':'rdkit'})
 print('PASS 201-atom structure')
 e=engine()
 steps=[
  {'reactants':['smiles:CCO'],'products':['smiles:CC=O'],'conditions':'step one','product_labels':['1']},
  {'reactants':['smiles:c1ccccc1'],'products':['smiles:CC(=O)c1ccccc1'],'conditions':'longer condition text','product_labels':['2']},
  {'reactants':['smiles:CC(=O)O'],'products':['smiles:CC(=O)OC'],'conditions':'第三步条件','product_labels':['3']},
 ]
 scheme=e.call('draw_scheme',{'steps':steps})
 baselines=[]
 for row in scheme['steps']:
  baselines.append(row['arrow_endpoints'][0][1])
  for i,side in ((0,'reactants'),(2,'products')):
   b=row[side][0]['bounds'];assert abs((b[0]+b[2])/2-scheme['column_centers'][i])<.01
  ep=row['arrow_endpoints']; assert abs((ep[0][0]+ep[1][0])/2-scheme['column_centers'][1])<.01
  for item in row['reactants']+row['products']:
   assert max(abs(a-b) for a,b in zip(item['bounds'],dp._group_bounds(e,item['ref'])[1]))<.01
 assert abs((baselines[2]-baselines[1])-(baselines[1]-baselines[0]))<.01
 assert scheme['row_gap']==16
 for a,b in zip(scheme['steps'],scheme['steps'][1:]): assert b['bounds'][1]-a['bounds'][3]>=15.99
 assert not dp._overlaps(dp._top_groups(e)),dp._overlaps(dp._top_groups(e))
 before=copy.deepcopy(e.actions);before_scene=copy.deepcopy(e.scene)
 try:e.call('draw_scheme',{'steps':[steps[0],{'reactants':['smiles:INVALID'],'products':['smiles:O']}]});raise AssertionError('must fail')
 except ValueError:pass
 assert [ET.tostring(n) for n in e.actions]==[ET.tostring(n) for n in before] and e.scene==before_scene
 root=ET.Element('actions');root.extend(e.actions)
 ET.ElementTree(root).write(Path(__file__).parent/'scheme-regression-actions.xml',encoding='utf-8',xml_declaration=True)
 (Path(__file__).parent/'scheme-regression-result.json').write_text(json.dumps(scheme,ensure_ascii=False,indent=2),encoding='utf-8')
 print('PASS three-step columns, equal row spacing, no overlap, fresh bounds and atomic rollback')
 # Wide bilingual labels and multiple compounds, plus auto collision avoidance.
 extra=e.call('draw_reaction',{'reactants':['smiles:CCO','smiles:O'],'products':['smiles:CC=O'],'reactant_labels':['很长的反应物标签','another long label'],'conditions':'x'*100,'x':0,'y':0})
 assert extra['bounds'][1]>scheme['bounds'][3]
 assert not dp._overlaps(dp._top_groups(e)),dp._overlaps(dp._top_groups(e))
 print('PASS long labels, long conditions and existing-content avoidance')
if __name__=='__main__':main()
