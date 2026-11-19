from pathlib import Path as P
from xml.etree import ElementTree as X
import subprocess as sp, itertools, hashlib
from rdkit import Chem
r=P('work/r60-extra');r.mkdir(exist_ok=True)
exe=next(P('out/Studio-r60').glob('*r60.exe')).resolve()
def call(*args):
    p=sp.run([exe,*args],capture_output=True,timeout=50)
    if p.returncode: raise RuntimeError(p.stderr.decode('utf8','replace'))
def plan(refs, additions=()):
    root=X.Element('result',ok='true');acts=X.SubElement(root,'actions'); acts.extend(additions)
    step=X.SubElement(acts,'mechanism_step',mode='arrows')
    for ref in refs:X.SubElement(step,'ref',id=ref)
    X.ElementTree(root).write(r/'plan.xml',encoding='utf-8')
def signature(elements,edges,charges={}):
    m=Chem.RWMol()
    for i,s in enumerate(elements):
        a=Chem.Atom(s);a.SetFormalCharge(charges.get(i,0));m.AddAtom(a)
    for i,j,order in edges:m.AddBond(i,j,{1:Chem.BondType.SINGLE,2:Chem.BondType.DOUBLE,3:Chem.BondType.TRIPLE}[order])
    mol=m.GetMol();Chem.SanitizeMol(mol);return Chem.MolToSmiles(mol)
def last_component(filename,n):
    doc=X.parse(filename);ns=list(doc.iter('n'))[-n:];index={a.get('id'):i for i,a in enumerate(ns)}
    elements=[Chem.GetPeriodicTable().GetElementSymbol(int(a.get('Element','6'))) for a in ns]
    bonds=[(index[b.get('B')],index[b.get('E')],int(b.get('Order','1'))) for b in doc.iter('b') if b.get('B') in index and b.get('E') in index]
    return signature(elements,bonds,{i:int(a.get('Charge','0')) for i,a in enumerate(ns)})
source=P('work/r60-original.r62doc');digest=hashlib.sha256(source.read_bytes()).hexdigest()
atoms=['C']*14
for i in (6,7,11):atoms[i]='O'
atoms[13]='H'
edges=[(0,1,1),(0,5,1),(1,2,1),(2,3,1),(2,6,2),(3,4,1),(3,8,1),(4,5,1),(4,7,1),(8,9,1),(9,10,1),(10,11,2),(10,12,1),(9,13,1),(4,12,1)]
expected=signature(atoms,edges,{7:-1})
for perm in itertools.permutations((17,19,20)):
    plan([f'arrow:{i}' for i in perm]);product=r/'aldol-trial.cdxml'
    call('--ai-apply',source,r/'plan.xml',product)
    assert last_component(product,14)==expected,(perm,last_component(product,14),expected)
assert hashlib.sha256(source.read_bytes()).hexdigest()==digest
print('aldol original: 6 permutations, validated entire product',expected,flush=True)
for variant in ('butadiene','cyclopentadiene','maleic','substituted'):
    # A=B-C=D and E=F. For cyclopentadiene an additional CH2 bridges A and D.
    pts=[(0,0),(-26,15),(-26,45),(0,60),(60,45),(60,15)]
    elements=['C']*6;edges=[(0,1,2),(1,2,1),(2,3,2),(4,5,2)]
    def add(x,y,s='C'):
        pts.append((x,y));elements.append(s);return len(pts)-1
    if variant in ('cyclopentadiene','maleic'):
        g=add(20,30);edges.extend([(0,g,1),(3,g,1)])
    if variant=='maleic':
        c=add(87,55);o=add(101,30,'O');z=add(87,5)
        edges.extend([(4,c,1),(c,o,1),(o,z,1),(z,5,1),(c,add(89,85,'O'),2),(z,add(89,-25,'O'),2)])
    if variant=='substituted':
        edges.extend([(0,add(0,-30),1),(4,add(86,60,'Cl'),1)])
    doc=X.Element('CDXML',BondLength='30');frag=X.SubElement(X.SubElement(doc,'page',id='1'),'fragment',id='2')
    for i,(x,y) in enumerate(pts):X.SubElement(frag,'n',id=str(i+10),p=f'{x} {y}',Element=str(Chem.GetPeriodicTable().GetAtomicNumber(elements[i])))
    for i,(a,b,k) in enumerate(edges):X.SubElement(frag,'b',id=str(100+i),B=str(10+a),E=str(10+b),Order=str(k))
    src=r/(variant+'.cdxml');X.ElementTree(doc).write(src,encoding='utf-8')
    call('--ai-scene',src,r/'scene.xml');items=list(X.parse(r/'scene.xml').iter('object'))
    atoms=sorted((x for x in items if x.get('kind')=='atom'),key=lambda x:int(x.get('ref').split(':')[1]))
    bondmap={frozenset((b.get('begin'),b.get('end'))):b for b in items if b.get('kind')=='bond'}
    bs=[bondmap[frozenset((atoms[a].get('ref'),atoms[b].get('ref')))] for a,b,k in edges]
    adds=[]
    for i,(a,b) in enumerate(((bs[0],bs[1]),(bs[2],atoms[4]),(bs[3],atoms[0]))):
        def mid(x,k):return str((float(x.get(k))+float(x.get(k+'2')))/2)
        adds.append(X.Element('draw_arrow',{'style':'mechanism','source_ref':a.get('ref'),'target_ref':b.get('ref'),'x':mid(a,'x'),'y':mid(a,'y'),'x2':mid(b,'x'),'y2':mid(b,'y'),'handle':f'new:{i+1}'}))
    plan([f'new:{i}' for i in (1,2,3)],adds);dest=r/(variant+'-product.cdxml')
    call('--ai-apply',src,r/'plan.xml',dest)
    gold=signature(elements,[(0,1,1),(1,2,2),(2,3,1),(4,5,1),(3,4,1),(0,5,1)]+edges[4:])
    assert last_component(dest,len(atoms))==gold,(variant,last_component(dest,len(atoms)),gold)
    print(variant,gold,flush=True)
