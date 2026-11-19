"""Independent chemical and geometric regression fixtures for projection CIP."""
import sys, math, copy, json
from pathlib import Path
import xml.etree.ElementTree as ET
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from projection_stereo import molecule, labels
from chemistry_open import stereo_labels, to_cdxml
from rdkit import Chem

def fischer(pattern, ketose=False):
    root=ET.Element('CDXML',BondLength='20',LabelSize='10');page=ET.SubElement(root,'page');g=ET.SubElement(page,'group',Name='Fischer projection',id='2')
    def line(a,b):ET.SubElement(g,'graphic',GraphicType='Line',BoundingBox=' '.join(map(str,(*a,*b))))
    def text(p,s):e=ET.SubElement(g,'t',p=' '.join(map(str,p)));ET.SubElement(e,'s').text=s
    top=-50 if ketose else -20;bottom=(len(pattern)-1)*30+20;line((0,top),(0,bottom));text((0,top-4),'CH2OH' if ketose else 'CHO');text((0,bottom+8),'CH2OH')
    if ketose:
        line((0,-31.2),(17,-31.2));line((0,-28.8),(17,-28.8));text((22,-27),'O')
    for i,side in enumerate(pattern):
        y=i*30;line((0,y),(-20,y));line((0,y),(20,y));text((-26,y+3),'H' if side=='R' else 'HO');text((26,y+3),'OH' if side=='R' else 'H')
    return root,g

def codes(g):
    m,c,k=molecule(g);return ''.join(m.GetAtomWithIdx(i).GetProp('_CIPCode') for i in range(len(c)) if m.GetAtomWithIdx(i).HasProp('_CIPCode'))

def transform(g,ang=0,reflect=False):
    def point(x,y):
        if reflect:x=-x
        return x*math.cos(ang)-y*math.sin(ang),x*math.sin(ang)+y*math.cos(ang)
    for e in g.iter():
        for key in ('p','BoundingBox','Center3D','MajorAxisEnd3D','MinorAxisEnd3D'):
            if key not in e.attrib:continue
            a=list(map(float,e.get(key).split()));r=[]
            if key.endswith('3D'):r=[*point(a[0],a[1]),a[2]]
            else:
                for i in range(0,len(a),2):r.extend(point(a[i],a[i+1]))
            e.set(key,' '.join(map(str,r)))

count=0
for pattern,ketose,expected in [('R',False,'R'),('RLRR',False,'RSRR'),('RLLR',False,'RSSR'),('LLRR',False,'SSRR'),('RRR',False,'RRR'),('LRR',True,'SRR')]:
    root,g=fischer(pattern,ketose);assert codes(g)==expected,(pattern,codes(g),expected)
    for angle in (0,.7,math.pi):
        for mirror in (False,True):
            t=copy.deepcopy(g);transform(t,angle,mirror);want=expected.translate(str.maketrans('RS','SR')) if mirror else expected
            assert codes(t)==want,(pattern,angle,mirror,codes(t),want);count+=1
    blank=copy.deepcopy(root)
    for t in list(blank.find('page/group').findall('t')):blank.find('page/group').remove(t)
    assert not labels(blank)[0]
# Real ordinary molecule stereochemistry remains available, including E/Z.
for smiles,want in [('N[C@@H](C)C(=O)O','S'),('C/C=C/C','E'),('C/C=C\\C','Z')]:
    out=stereo_labels(to_cdxml(Chem.MolFromSmiles(smiles)));assert '\t'+want+'\t' in out,(smiles,out);count+=1
# Two fully specified centres; exchanging two ligands in one plane must invert
# only that centre. Neither torsion choice nor in-plane rotation changes CIP.
def two_centres(kind, eclipsed):
    root=ET.Element('CDXML',BondLength='20',LabelSize='10');page=ET.SubElement(root,'page');g=ET.SubElement(page,'group',Name=kind,id='2')
    def line(a,b):ET.SubElement(g,'graphic',GraphicType='Line',BoundingBox=' '.join(map(str,(*a,*b))))
    def text(p,s):e=ET.SubElement(g,'t',p=' '.join(map(str,p)));ET.SubElement(e,'s').text=s
    if kind=='Newman projection':
        for r in (8.8,.6):ET.SubElement(g,'graphic',GraphicType='Oval',Center3D='0 0 0',MajorAxisEnd3D=f'{r} 0 0')
    else:line((-28,13),(28,-13))
    for plane in range(2):
        for i,label in enumerate(('H','F','Cl') if plane==0 else ('H','Br','I')):
            if kind=='Newman projection':
                angle=math.radians(-90+i*120+((8 if plane else -8) if eclipsed else (60 if plane else 0)))
                u=(math.cos(angle),math.sin(angle));a=tuple(x*(8.8 if plane else 0) for x in u);b=tuple(x*22.4 for x in u);p=tuple(x*28 for x in u)
            else:
                angle=math.radians(i*120+(0 if eclipsed else 60 if plane else 0));u=(math.cos(angle)+.35*math.sin(angle),math.sin(angle));n=math.hypot(*u);u=tuple(x/n for x in u);a=(28,-13) if plane else (-28,13);b=tuple(a[k]+u[k]*(16.4 if plane else 18) for k in range(2));p=tuple(b[k]+u[k]*4 for k in range(2))
            line(a,b);text(p,label)
    return root,g
for kind in ('Newman projection','Sawhorse projection'):
    previous=None
    for eclipsed in (False,True):
        root,g=two_centres(kind,eclipsed);base=codes(g);assert len(base)==2,(kind,base)
        if previous is not None:assert base==previous,(kind,base,previous)
        previous=base
        for angle in (0,.7,math.pi,4.1):
            for mirror in (False,True):
                t=copy.deepcopy(g);transform(t,angle,mirror);got=codes(t);want=base.translate(str.maketrans('RS','SR')) if mirror else base
                assert got==want,(kind,eclipsed,angle,mirror,got,want);count+=1
        t=copy.deepcopy(g);ts=t.findall('t');ts[0].find('s').text,ts[1].find('s').text=ts[1].find('s').text,ts[0].find('s').text
        assert codes(t)==base[0].translate(str.maketrans('RS','SR'))+base[1]
if len(sys.argv)>1:
    source=Path(sys.argv[1])
    for f in source.glob('sugar-*.cdxml'):
        root=ET.parse(f).getroot();out=stereo_labels(ET.tostring(root,encoding='unicode'));assert '\nprojection\t' in out,(f,out);count+=1
print(json.dumps({'passed':count,'independent_sugar_CIP':True,'rotation_mirror':True,'blank_rejected':True,'ordinary_RS_EZ':True}))
