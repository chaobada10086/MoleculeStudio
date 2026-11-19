import sys, math, json
from pathlib import Path
import xml.etree.ElementTree as ET
sys.path.insert(0,str(Path('out/Studio').resolve()))
from chemistry_open import one_molecule, identity
from rdkit import Chem, RDLogger
RDLogger.DisableLog('rdApp.warning')
directory=Path(sys.argv[1]);total=0

def parity(v):
    a,b,c=[[v[i][j]-v[3][j] for j in range(3)] for i in range(3)]
    return a[0]*(b[1]*c[2]-b[2]*c[1])-a[1]*(b[0]*c[2]-b[2]*c[0])+a[2]*(b[0]*c[1]-b[1]*c[0])

def appearance_stereo(file):
    root=ET.parse(file).getroot();nodes={n.get('id'):n for n in root.iter('n')};edges=list(root.iter('b'));checked=0
    for ident,node in nodes.items():
        if 'MoleculeStudioProjection' not in node.attrib: continue
        role=int(node.get('MoleculeStudioProjection'));kind,plane=role//10,role%10
        if kind>2:continue
        x,y=map(float,node.get('p').split());old=[];new=[]
        for b in edges:
            key='MoleculeStudioProjectionBegin' if b.get('B')==ident else 'MoleculeStudioProjectionEnd' if b.get('E')==ident else None
            if not key or key not in b.attrib:continue
            other=nodes[b.get('E') if b.get('B')==ident else b.get('B')];ox,oy=map(float,other.get('p').split());dx,dy=ox-x,oy-y;length=math.hypot(dx,dy)
            if length:dx,dy=dx/length,dy/length
            old.append(tuple(map(float,b.get(key).split())))
            if kind==0:new.append((dx,-dy,1 if abs(dx)>.5 else -1))
            elif 'MoleculeStudioProjection' in other.attrib and int(other.get('MoleculeStudioProjection'))//10==kind:new.append((0,0,1 if plane else -1))
            else:new.append((dx-.35*dy if kind==1 else dx,-dy,-.6 if plane else .6))
        if len(old)==4:assert parity(old)*parity(new)>0,(file,ident,old,new);checked+=1
    return checked

for file in directory.glob('converted-*.cdxml'):total+=appearance_stereo(file)
for i,expected in {0:'R',3:'RRR',9:'RSRR',10:'SSRR',13:'RSSR',17:'SRR'}.items():
    for side in ('D','L'):
        file=directory/f'sugar-{i}-{side}.cdxml';m=one_molecule(file.read_text(encoding='utf-8'));root=ET.parse(file).getroot();centres=[int(n.get('id')) for n in root.iter('n') if n.get('MoleculeStudioProjection')=='0'];atoms={a.GetIntProp('_STUDIO_CDX_ID'):a for a in m.GetAtoms() if a.HasProp('_STUDIO_CDX_ID')};got=''.join(atoms[c].GetProp('_CIPCode') for c in centres);want=expected if side=='D' else expected.translate(str.maketrans('RS','SR'));assert got==want,(file,got,want);total+=1
original=identity(one_molecule((directory/'sugar-9-D.cdxml').read_text()))
for f in directory.glob('glucose-converted-*.cdxml'):
    actual=identity(one_molecule(f.read_text()));assert actual==original,(f,actual,original);total+=1;appearance_stereo(f)
for i in range(5):
    base=None
    for j in range(5):
        f=directory/f'converted-{i}-{j}.cdxml';actual=identity(one_molecule(f.read_text()));base=actual if base is None else base;assert actual==base,(f,actual,base);total+=1
result={'checks':total,'sugar_absolute_stereochemistry':True,'conversion_isomeric_smiles':True,'rendered_projection_parity':True}
(directory/'independent-chemistry.json').write_text(json.dumps(result,indent=2));print(json.dumps(result))
