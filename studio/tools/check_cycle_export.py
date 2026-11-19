from collections import Counter
from collections import defaultdict
from itertools import combinations
from math import hypot
from pathlib import Path
import sys
import xml.etree.ElementTree as ET


def check(base):
    plan=ET.parse(base.with_suffix('.xml'))
    drawing=ET.parse(base.with_suffix('.cdxml'))
    cycles=plan.findall('./actions/catalytic_cycle')
    assert cycles
    expected=Counter(cycle=sum(len(cycle.findall('ref')) for cycle in cycles))
    for cycle in cycles:
        expected.update(branch.get('role') for branch in cycle.findall('branch'))
        if cycle.get('couple_edge'):
            expected['out']+=1
    actual=Counter(obj.get('MoleculeStudioCatalytic') for obj in drawing.iter() if obj.get('MoleculeStudioCatalytic'))
    assert expected==actual,(expected,actual)
    for obj in drawing.iter():
        if obj.get('MoleculeStudioCatalytic')=='in':
            params=dict(field.split('=',1) for field in obj.attrib['MoleculeStudioArrow'].split(';') if '=' in field)
            assert obj.tag=='curve' and params['hend']==params['hbegin']=='0',(obj.attrib['id'],'incoming must be an unheaded curve')
            assert obj.get('MoleculeStudioElectronTarget','').startswith('arrow:'),'incoming lost its cycle anchor'
    ports=defaultdict(list)
    for branch in drawing.iter():
        if branch.get('MoleculeStudioCatalytic') not in ('in','out'):
            continue
        for anchor,coordinate in [('MoleculeStudioElectronSource','Tail3D'),('MoleculeStudioElectronTarget','Head3D')]:
            ref=branch.get(anchor,'')
            if ref.startswith('arrow:'):
                ports[ref].append(tuple(map(float,branch.attrib[coordinate].split()[:2])))
    for ref,points in ports.items():
        for first,second in combinations(points,2):
            assert hypot(first[0]-second[0],first[1]-second[1])>1,(ref,'coincident junction',first,second)
    assert len(drawing.findall('.//n[@MoleculeStudioCatalyticMetal]'))==len(plan.findall('.//metal'))
    page=list(map(float,drawing.find('.//page').get('BoundingBox').split()))
    for element in drawing.iter():
        if element.tag in ('n','t') and element.get('p'):
            pos=list(map(float,element.get('p').split()))
            assert page[0]<=pos[0]<=page[2] and page[1]<=pos[1]<=page[3],(element.tag,pos,page)
    print(base.name,actual,'anchors inside page')


if __name__=='__main__':
    for item in sys.argv[1:]:check(Path(item))
