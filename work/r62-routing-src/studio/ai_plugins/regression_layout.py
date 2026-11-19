import sys,json,copy,math,xml.etree.ElementTree as ET
from pathlib import Path
ROOT=Path(__file__).resolve().parent.parent
sys.path.insert(0,str(ROOT))
import ai_assistant as ai
from ai_plugins import draw_plugin as dp
from chemistry_open import Chem,identity,one_molecule

def engine():
 dp.install_plugin(vars(ai))
 return ai.DrawingTools({'selected':0,'bond_length':14.4,'font':'Times New Roman'})

def check_enolate_step():
    draw = engine()
    row = {'enolate':{}, 'reactants':['smiles:[Na+].CC[O-]', 'smiles:CCOC(=O)CCCCC(=O)OCC']}
    step = draw.call('draw_scheme', {'steps':[row]})['steps'][0]
    scene = {o['ref']:o for o in draw.scene}
    attack, shift, release = (scene[r] for r in step['electron_refs'])
    donor, hydrogen = (scene[attack[key]] for key in ('source','target'))
    assert donor['element']=='O' and donor['charge']==-1 and hydrogen['element']=='H'
    assert math.dist((donor['x'],donor['y']), (hydrogen['x'],hydrogen['y'])) < 3*14.4
    def ends(ref):
        return {scene[ref]['begin'],scene[ref]['end']}
    ch = ends(shift['source'])
    cc = ends(shift['target'])
    co = ends(release['source'])
    assert hydrogen['ref'] in ch and len(ch & cc)==len(cc & co)==1 and not ch & co
    assert scene[release['source']]['order']=='2' and release['target'] in co
    assert scene[release['target']]['element']=='O'
    product, acid = (Chem.MolFromSmiles(p['smiles']) for p in step['products'])
    assert product.HasSubstructMatch(Chem.MolFromSmarts('C=C([O-])O')) and acid.GetNumAtoms()==3
    assert any(a.GetAtomicNum()==11 for a in product.GetAtoms())
    before = [ET.tostring(a) for a in draw.actions],copy.deepcopy(draw.scene)
    for invalid in ({**row,'enolate':{'alpha':2}},
                    {**row,'reactants':[row['reactants'][0],'smiles:CCC(=O)C']}):
        try: draw.call('draw_scheme',{'steps':[row,invalid]})
        except ValueError: pass
        else: raise AssertionError('invalid or ambiguous alpha carbon accepted')
        assert ([ET.tostring(a) for a in draw.actions],draw.scene)==before
    step = draw.call('draw_reaction',{'enolate':{'alpha':2,'carbonyl':3},
                     'reactants':['smiles:CC[O-]','smiles:CCC(=O)C']})
    assert len(step['electron_refs'])==3
    print('PASS enolate endpoints, salt products, compact approach, site selection and rollback')

def check_enol_double_bonds():
    draw = engine()
    # A second, unrelated alkene in the same structure still has its own stereo.
    for smiles, plain, crossed in [('CC=C(C)O',1,0), ('CC=C([O-])CC=CC',1,1),
                                   ('CC=COC',0,1), ('C/C=C(/C)O',0,0), ('C/C=C(\\C)O',0,0)]:
        draw.call('draw_smiles', {'smiles':smiles})
        xml = draw.actions[-1].text
        bonds = ET.fromstring(xml).findall(".//b[@Order='2']")
        assert sum(b.get('MoleculeStudioUnspecifiedEZ')=='yes' and 'Display' not in b.attrib for b in bonds)==plain
        assert sum(b.get('Display')=='Wavy' for b in bonds)==crossed
        assert identity(one_molecule(xml))==identity(Chem.MolFromSmiles(smiles))
    print('PASS parallel enol and enolate C=C, other alkenes and explicit E/Z round trips')

def check_mechanism_sites():
    from ai_plugins.mitsunobu import scheme
    draw = engine()
    args = {'alcohol':'CC(O)C','acid':'CC(=O)O'}
    rows = draw.call('draw_mitsunobu', args)['steps']
    objects = {o['ref']:o for o in draw.scene}
    moves = [[objects[r] for r in row['electron_refs']] for row in rows]
    assert [len(group) for group in moves]==[2,2,4,2]
    assert all(m['electrons']==2 for group in moves for m in group)
    add, pi = moves[0]
    assert objects[add['source']]['element']=='P' and objects[add['target']]['element']=='N'
    nn = objects[pi['source']]
    assert nn['order']=='2' and {nn['begin'],nn['end']}=={add['target'],pi['target']}
    attack, leaving = moves[-1]
    assert objects[attack['source']]['charge']==-1 and objects[attack['source']]['element']=='O'
    co, op = (objects[leaving[k]] for k in ('source','target'))
    assert attack['target'] in (co['begin'],co['end']) and objects[attack['target']]['element']=='C'
    assert {objects[op[k]]['element'] for k in ('begin','end')}=={'O','P'}
    assert rows[-1]['products'][0]['smiles']==Chem.MolToSmiles(Chem.MolFromSmiles('CC(=O)OC(C)C'))
    for alcohol, cip in [('C[C@H](O)CC','R'),('O[C@@H](C)CC','R'),('C[C@@H](O)CC','S')]:
        spec = scheme({**args,'alcohol':alcohol})['steps'][-1]['products'][0]
        assert [s for i,s in Chem.FindMolChiralCenters(Chem.MolFromSmiles(spec[7:]))]==[cip]
    checkpoint = [ET.tostring(a) for a in draw.actions],copy.deepcopy(draw.scene)
    for name,params in [
        ('draw_mitsunobu',{**args,'alcohol':'OCCO'}),
        ('draw_mitsunobu',{**args,'alcohol':'CC(C)(C)O'}),
        ('draw_reaction',{'reactants':['smiles:[OH:5][CH3:5]'],'products':['smiles:C']}),
        ('draw_reaction',{'reactants':['smiles:C[O-:5]'],'products':['smiles:CO'],
                          'electron_moves':[{'source':'1:@5','target':'1:@9'}]})]:
        try: draw.call(name,params)
        except ValueError: pass
        else: raise AssertionError('invalid site accepted')
        assert ([ET.tostring(a) for a in draw.actions],draw.scene)==checkpoint
    print('PASS mapped sites, Mitsunobu double-electron flow, SN2 inversion and atomic rollback')

def check_robinson_arrows():
    draw = engine()
    rows = draw.call('draw_scheme', {'steps':[
        {'reactants':['smiles:[OH-]','smiles:O=C1CCCCC1'],'enolate':{}},
        {'reactants':['smiles:[O-]C1=CCCCC1','smiles:C=CC(C)=O'],'michael':{}}
    ]})['steps']
    sites = {o['ref']:o for o in draw.scene}
    moves = [[(sites[a['source']],sites[a['target']]) for a in (sites[r] for r in row['electron_refs'])] for row in rows]
    first, second = moves
    assert len(first)==3 and len(second)==4
    assert first[0][0]['charge']==-1 and first[0][1]['element']=='H' and first[0][1]['ref'] in (first[1][0]['begin'],first[1][0]['end'])
    assert second[0][0]['charge']==-1 and second[0][0]['ref'] in (second[0][1]['begin'],second[0][1]['end'])
    assert second[1][0]['order']=='2' and second[1][0]['owner']!=second[1][1]['owner']
    assert second[1][1]['ref'] in (second[2][0]['begin'],second[2][0]['end']) and second[3][1]['element']=='O'
    expected = Chem.MolFromSmiles('CC([O-])=CCC1CCCCC1=O')
    assert Chem.MolToSmiles(Chem.MolFromSmiles(rows[1]['products'][0]['smiles']))==Chem.MolToSmiles(expected)
    assert all(a.get('route')=='true' for a in draw.actions if a.get('style')=='mechanism')
    for route in (True, False):
        move = {'source':'1:1-2','target':'1:2','bend':.25}
        if not route: move['route']=False
        draw.call('draw_reaction', {'reactants':['smiles:C=O'],'products':['smiles:C=O'],'electron_moves':[move]})
        assert [a for a in draw.actions if a.get('style')=='mechanism'][-1].get('route')==str(route).lower()
    print('PASS hydroxide and Michael electron pairs, charge, product graph and explicit route override')

def main():
 check_robinson_arrows()
 check_mechanism_sites()
 check_enol_double_bonds()
 check_enolate_step()
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
 # Row-local electron addresses resolve to atoms or actual bonds and survive layout.
 mechanism=engine()
 row={'reactants':['smiles:[OH-]','smiles:CBr'],'products':['smiles:CO','smiles:[Br-]'],
      'electron_moves':[{'source':'1:1','target':'2:1'},{'source':'2:1-2','target':'2:2'}]}
 scheme=mechanism.call('draw_scheme',{'steps':[row,{**row,'title':'第二步','reactant_labels':['Nu','底物']}]})
 first,second=scheme['steps']
 assert len(first['electron_refs'])==2 and second['bounds'][1]-first['bounds'][3]>=15.99
 assert all(o['source'].split('/')[0] in [r['ref'] for r in step['reactants']]
            for step in scheme['steps'] for o in mechanism.scene if o['ref'] in step['electron_refs'])
 a=next(o for o in mechanism.scene if o.get('owner')==first['reactants'][0]['ref'] and o['kind']=='atom')
 b=next(o for o in mechanism.scene if o.get('owner')==first['products'][0]['ref'] and o['kind']=='atom')
 count=len(mechanism.actions)
 for name,args in [('draw_curved_arrow',{'source':a['ref'],'target':b['ref']}),
                   ('draw_arrow',{'style':'mechanism','x':a['x'],'y':a['y'],'x2':b['x'],'y2':b['y']}),
                   ('draw_scheme',{'steps':[row,{**row,'electron_moves':[{'source':'1:1','target':'2:1-9'}]}]})]:
  try:mechanism.call(name,args)
  except ValueError:pass
  else:raise AssertionError('invalid mechanism accepted')
  assert len(mechanism.actions)==count
 print('PASS row-local electron flow, compact spacing, cross-product rejection and atomic rollback')
 e=engine()
 michael={'title':'Michael 加成','reactants':['smiles:COC(=O)[CH-]C(=O)C','smiles:C=CC(=O)C'],
         'products':['smiles:COC(=O)C(C/C=C(/[O-])C)C(=O)C'],
         'electron_moves':[{'source':'1:5','target':'2:1'},{'source':'2:1-2','target':'2:2-3'},{'source':'2:3-4','target':'2:4'}]}
 row=e.call('draw_reaction',michael)
 by_ref={o['ref']:o for o in e.scene}
 attack=by_ref[row['electron_refs'][0]]
 donor,acceptor=(by_ref[attack[k]] for k in ('source','target'))
 assert abs(donor['y']-acceptor['y'])<1e-6
 assert max(donor['x'],acceptor['x'])<row['arrow_endpoints'][0][0]
 payload={'style':'mechanism','x':donor['x']+.3,'y':donor['y'],'x2':acceptor['x'],'y2':acceptor['y']-.2}
 e.call('draw_arrow',payload)
 assert e.actions[-1].get('source_ref')==donor['ref'] and e.actions[-1].get('target_ref')==acceptor['ref']
 product=next(o for o in e.scene if o.get('owner')==row['products'][0]['ref'] and o['kind']=='atom')
 for o in e.scene:
  o.pop('reaction_row',None)
  o.pop('reaction_side',None)
 checkpoint=len(e.actions)
 for name,args in [('draw_arrow',{**payload,'x2':acceptor['x'],'y2':acceptor['y']+100}),
                   ('draw_mechanism_step',{'moves':[{'source':donor['ref'],'target':product['ref']}],'generate':False}),
                   ('draw_curved_arrow',{'source':donor['ref'],'target':product['ref']})]:
  try:e.call(name,args)
  except ValueError:pass
  else:raise AssertionError('unbound or cross-divider arrow was accepted')
  assert len(e.actions)==checkpoint
 new=engine()
 a=new.call('draw_smiles',{'smiles':'[OH-]','x':0,'y':0})['atom_refs'][0]['ref']
 b=new.call('draw_smiles',{'smiles':'C','x':85,'y':0})['atom_refs'][0]['ref']
 new.call('draw_arrow',{'style':'mechanism','source':a,'target':b})
 old=len(new.actions)
 try:new.call('draw_arrow',{'style':'forward','x':25,'x2':60,'y':0,'y2':0})
 except ValueError:pass
 else:raise AssertionError('late divider through an electron arrow')
 assert len(new.actions)==old
 plain=engine()
 for coords,ax,ax2 in [((0,0,60,2),'y','y2'),((12,40,11,0),'x','x2')]:
  plain.call('draw_arrow',{'style':'forward',**dict(zip(('x','y','x2','y2'),coords))})
  assert plain.actions[-1].get(ax)==plain.actions[-1].get(ax2)
 plain.call('draw_arrow',{'style':'forward','x':0,'y':0,'x2':30,'y2':25})
 assert float(plain.actions[-1].get('y2'))==25
 print('PASS Michael addition: aligned contact sites, snapped chemical endpoints and cross-divider checks for existing scenes')
if __name__=='__main__':main()
