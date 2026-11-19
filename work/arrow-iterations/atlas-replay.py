from pathlib import Path
import copy
import runpy
import xml.etree.ElementTree as ET

fixture=runpy.run_path('work/arrow-iterations/compact_cycle_exam.py')
cases=fixture['calls']
ai=fixture['ai']
def render(name,cycles):
    engine=ai.DrawingTools({})
    result=engine.call('draw_cycle_atlas',dict(cycles=cycles))
    assert len(result['cycles'])==len(cycles)
    root=ET.Element('result',ok='true')
    ET.SubElement(root,'actions').extend(engine.actions)
    ET.ElementTree(root).write(Path('work/arrow-iterations')/(name+'.xml'),encoding='utf-8')

render('atlas-four',cases)
render('atlas-dual',cases[3:])
example=copy.deepcopy(cases[2])
example['title']='Negishi  /  溴苯与乙基锌试剂'
example['nodes'][0].pop('incoming')
example['nodes'][0]['incoming_smiles']='Brc1ccccc1'
example['nodes'][1]['incoming']='EtZnCl'
example['nodes'][1]['outgoing']='ZnBrCl'
example['nodes'][2].pop('outgoing')
example['nodes'][2]['outgoing_smiles']='CCc1ccccc1'
for node in example['nodes']:
    node['ligands']=[{'X':'Br','Ar':'Ph','R':'Et'}.get(label,label) for label in node['ligands']]
render('atlas-structural', [example])
