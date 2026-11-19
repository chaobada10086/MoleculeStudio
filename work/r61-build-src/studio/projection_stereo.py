"""Calculation-only graph adapter for editable projection diagrams.

The document remains standard vector graphics. No invisible chemical atoms or
guessed R groups are inserted. CIP is assigned to the complete molecular graph
by RDKit, using local tetrahedral directions defined by each projection.
"""
import math
import re
import xml.etree.ElementTree as ET
from functools import lru_cache
from pathlib import Path
import json
from rdkit import Chem
from rdkit.Chem import rdCIPLabeler
from rdkit.Geometry import Point3D

NAMES = ('Fischer projection', 'Sawhorse projection', 'Newman projection')


def add(a, b): return (a[0]+b[0], a[1]+b[1])
def sub(a, b): return (a[0]-b[0], a[1]-b[1])
def mul(a, k): return (a[0]*k, a[1]*k)
def dot(a, b): return a[0]*b[0]+a[1]*b[1]
def norm(a): return math.hypot(*a)
def unit(a): return mul(a, 1/max(1e-9, norm(a)))
def xy(text): return tuple(map(float, text.split()[:2]))
def near(a, b, eps=.01): return norm(sub(a, b)) < eps


def primitives(group):
    lines, circles, texts = [], [], []
    for e in group:
        if e.tag == 't':
            label = ''.join(s.text or '' for s in e.iter('s')).strip()
            if re.fullmatch(r'\([RSrsEZ]\)', label) or not label: continue
            if e.get('p'): texts.append((e, label, xy(e.get('p'))))
        elif e.tag == 'graphic' and e.get('GraphicType') == 'Line':
            box = list(map(float, e.get('BoundingBox', '').split()))
            if len(box) != 4: raise ValueError('缺少投影键坐标')
            edge = (tuple(box[:2]), tuple(box[2:]))
            if not any((near(edge[0], a) and near(edge[1], b)) or (near(edge[0], b) and near(edge[1], a)) for a, b in lines): lines.append(edge)
        elif e.tag == 'graphic' and e.get('GraphicType') == 'Oval':
            c = xy(e.get('Center3D')); r = norm(sub(xy(e.get('MajorAxisEnd3D')), c))
            circles.append((c, r))
        elif e.tag == 'arrow' and e.get('ArrowheadHead', 'None') == 'None' and e.get('ArrowheadTail', 'None') == 'None':
            continue  # ChemDraw's redundant companion for the native line.
        else: raise ValueError('投影中含有无法识别的绘图对象')
    return lines, circles, texts


def topology(group):
    lines, circles, texts = primitives(group)
    kind = group.get('Name', '')
    if circles:
        if len(circles) != 2 or not near(circles[0][0], circles[1][0]): raise ValueError('纽曼投影圆心不一致')
        kind = 'Newman projection'
    if kind not in NAMES: raise ValueError('不是已识别的投影')
    centres, edges, terminals, vectors = [], [], [], {}
    # Terminal: core index, endpoint, local direction towards the ligand.
    if kind == 'Newman projection':
        c, radius = max(circles, key=lambda v: v[1]); centres = [c, c]; edges = [(0, 1, 1)]
        vectors[(0, 1)], vectors[(1, 0)] = (0, 0, -1), (0, 0, 1)
        counts = [0, 0]
        for a, b in lines:
            if norm(sub(a, c)) > norm(sub(b, c)): a, b = b, a
            plane = 0 if norm(sub(a, c)) < radius*.15 else 1
            if plane and abs(norm(sub(a, c))-radius) > radius*.08: raise ValueError('后方键未连接圆周')
            ray = unit(sub(b, c)); counts[plane] += 1
            terminals.append((plane, b, (ray[0], -ray[1], .6 if plane == 0 else -.6)))
        if counts != [3, 3]: raise ValueError('纽曼投影需要六条独立的取代基键')
    elif kind == 'Sawhorse projection':
        points = []
        def vertex(p):
            for i, q in enumerate(points):
                if near(p, q): return i
            points.append(p); return len(points)-1
        links = [(vertex(a), vertex(b)) for a, b in lines]
        degree = [sum(i in edge for edge in links) for i in range(len(points))]
        hubs = [i for i, n in enumerate(degree) if n == 4]
        if len(hubs) != 2 or len(links) != 7: raise ValueError('锯架式连接关系不完整')
        lengths = {i: sum(norm(sub(points[i], points[j])) for a,b in links for j in ([b] if a==i else [a] if b==i else []) if j not in hubs)/3 for i in hubs}
        # Front spokes are deliberately longer in the template; this invariant
        # survives rotation, reflection and ChemDraw's native line rewriting.
        if abs(lengths[hubs[0]]-lengths[hubs[1]]) < .02*max(lengths.values()):
            hubs.sort(key=lambda i: points[i][0]-points[i][1])  # legacy template convention
        else: hubs.sort(key=lambda i: -lengths[i])
        centres = [points[i] for i in hubs]; edges = [(0, 1, 1)]
        axis = unit(sub(centres[1], centres[0])); angle = math.atan2(axis[1], axis[0])+math.atan2(.65, 1.4)
        # Undo the template's 2D slant before interpreting the circumferential
        # rays. Rotation changes neither torsion nor absolute configuration.
        for plane, hub in enumerate(hubs):
            vectors[(plane, 1-plane)] = (0, 0, -1 if plane == 0 else 1)
            for a, b in links:
                j = b if a == hub else a if b == hub else -1
                if j < 0 or j in hubs: continue
                v = unit(sub(points[j], points[hub])); x = v[0]*math.cos(angle)+v[1]*math.sin(angle); y = -v[0]*math.sin(angle)+v[1]*math.cos(angle)
                terminals.append((plane, points[j], (x-.35*y, -y, .6 if plane == 0 else -.6)))
    else:
        if not lines: raise ValueError('空的费歇尔投影')
        stem = max(lines, key=lambda edge: norm(sub(edge[1], edge[0])))
        origin, end = stem; axis = unit(sub(end, origin)); length = norm(sub(end, origin))
        branches = []
        for a, b in lines:
            if (a, b) == stem: continue
            # Orient branches from the main chain to the free endpoint.
            def distance(p): return abs((p[0]-origin[0])*axis[1]-(p[1]-origin[1])*axis[0])
            if distance(b) < distance(a): a, b = b, a
            if distance(a) > length*.003 or abs(dot(unit(sub(b, a)), axis)) > .03: raise ValueError('费歇尔投影支链不垂直')
            branches.append((dot(sub(a, origin), axis), a, b))
        branches.sort()
        clusters = []
        for branch in branches:
            if not clusters or abs(branch[0]-clusters[-1][0][0]) > length*.025: clusters.append([])
            clusters[-1].append(branch)
        if not clusters or any(len(c) != 2 for c in clusters): raise ValueError('费歇尔交叉中心不完整')
        ketones = set()
        for i, cluster in enumerate(clusters):
            at = mul(add(cluster[0][1], cluster[1][1]), .5); centres.append(at)
            a, b = (unit(sub(row[2], row[1])) for row in cluster)
            if dot(a, b) > .95:
                # Two close parallel lines are one C=O, not a stereocentre.
                if norm(sub(cluster[0][1], cluster[1][1])) > length*.03: raise ValueError('无法识别羰基')
                ketones.add(i); tip=mul(add(cluster[0][2], cluster[1][2]),.5)
                terminals.append((i, tip, (a[0], -a[1], 0), 2))
            else:
                for _, start, tip in cluster:
                    ray=unit(sub(tip,start));terminals.append((i,tip,(ray[0],-ray[1],1)))
        for i in range(len(centres)-1):
            edges.append((i,i+1,1));v=unit(sub(centres[i+1],centres[i]));vectors[(i,i+1)]=(v[0],-v[1],-1);vectors[(i+1,i)]=(-v[0],v[1],-1)
        terminals.extend([(0,origin,(-axis[0],axis[1],-1)),(len(centres)-1,end,(axis[0],-axis[1],-1))])
    if len(texts) != len(terminals): raise ValueError('请填写所有投影取代基；空白端点不能用于构型计算')
    # Associate captions with the nearest free endpoint. Bounding boxes account
    # for left/right alignment and labels such as CH2OH with mixed scripts.
    pairs = []
    scale = sorted(norm(sub(b,a)) for a,b in lines)[len(lines)//2]
    for i, terminal in enumerate(terminals):
        p=terminal[1]
        for j,(e,label,anchor) in enumerate(texts):
            box=list(map(float,e.get('BoundingBox','').split()))
            if len(box)==4:
                dx=max(min(box[0],box[2])-p[0],0,p[0]-max(box[0],box[2]));dy=max(min(box[1],box[3])-p[1],0,p[1]-max(box[1],box[3]));distance=math.hypot(dx,dy)
            else: distance=norm(sub(p,anchor))
            pairs.append((distance,i,j))
    assigned={};used=set()
    for distance,i,j in sorted(pairs):
        if i in assigned or j in used:continue
        if distance>max(scale*.95,8):continue
        assigned[i]=j;used.add(j)
    if len(assigned)!=len(terminals):raise ValueError('取代基文字离端点过远或存在歧义')
    return kind, centres, edges, terminals, vectors, [texts[assigned[i]][1] for i in range(len(terminals))]


ALIASES = {'H':'[H]','D':'[2H]','F':'F','Cl':'Cl','Br':'Br','I':'I','OH':'O','HO':'O','NH2':'N','H2N':'N',
           'Me':'C','CH3':'C','Et':'CC','C2H5':'CC','Pr':'CCC','n-Pr':'CCC','i-Pr':'C(C)C','iPr':'C(C)C',
           't-Bu':'C(C)(C)C','tBu':'C(C)(C)C','Ph':'c1ccccc1','Bn':'Cc1ccccc1','OMe':'OC','OEt':'OCC',
           'CHO':'C=O','CH2OH':'CO','COOH':'C(=O)O','CO2H':'C(=O)O','CO2Me':'C(=O)OC','COOMe':'C(=O)OC',
           'CO2Et':'C(=O)OCC','COOEt':'C(=O)OCC','CN':'C#N','NO2':'[N+](=O)[O-]','SH':'S','O':'O'}


@lru_cache(maxsize=1)
def catalog():
    return json.loads((Path(__file__).parent/'nicknames.json').read_text(encoding='utf-8'))


def catalog_ligand(label):
    """Use an existing, explicitly defined single-attachment abbreviation.

    Reject nested/query/stereochemical fragments we cannot faithfully transfer;
    an unknown caption must never silently turn into a carbon atom.
    """
    raw=catalog().get(label)
    if not raw:raise ValueError('尚未定义的投影取代基：'+label)
    root=ET.fromstring(raw);nodes=list(root.iter('n'));bonds=list(root.iter('b'))
    external=[n for n in nodes if n.get('NodeType')=='ExternalConnectionPoint']
    if len(external)!=1:raise ValueError('取代基需要唯一连接点')
    ext=external[0].get('id');links=[b for b in bonds if ext in (b.get('B'),b.get('E'))]
    if len(links)!=1:raise ValueError('取代基连接点不明确')
    anchor=links[0].get('E') if links[0].get('B')==ext else links[0].get('B')
    real=[n for n in nodes if n.get('id')!=ext];real.sort(key=lambda n:n.get('id')!=anchor)
    rw=Chem.RWMol();ids={}
    for n in real:
        if n.get('NodeType','Element')!='Element' or n.find('fragment') is not None:raise ValueError('取代基包含未展开的原子')
        atom=Chem.Atom(int(n.get('Element','6')));atom.SetFormalCharge(int(n.get('Charge','0')))
        if n.get('Isotope'):atom.SetIsotope(int(n.get('Isotope')))
        ids[n.get('id')]=rw.AddAtom(atom)
    for b in bonds:
        if b in links:continue
        if b.get('Display','Solid') not in ('Solid','Normal') or b.get('Order','1') not in ('1','2','3'):raise ValueError('取代基内部构型尚未定义')
        rw.AddBond(ids[b.get('B')],ids[b.get('E')],{1:Chem.BondType.SINGLE,2:Chem.BondType.DOUBLE,3:Chem.BondType.TRIPLE}[int(b.get('Order','1'))])
    return rw.GetMol()


def ligand(label, order=1):
    label=label.translate(str.maketrans('₀₁₂₃₄₅₆₇₈₉','0123456789')).replace('−','-').replace(' ','')
    if label not in ALIASES:return catalog_ligand(label)
    smiles='O' if label=='O' and order==2 else ALIASES[label]
    params=Chem.SmilesParserParams();params.removeHs=False
    result=Chem.MolFromSmiles(smiles,params)
    if result is None:raise ValueError('无法解析取代基：'+label)
    return result


def molecule(group):
    kind, centres, edges, terminals, vectors, labels=topology(group)
    rw=Chem.RWMol();local={i:{} for i in range(len(centres))}
    for _ in centres:rw.AddAtom(Chem.Atom(6))
    for a,b,order in edges:
        rw.AddBond(a,b,Chem.BondType.SINGLE);local[a][b]=vectors[(a,b)];local[b][a]=vectors[(b,a)]
    for terminal,label in zip(terminals,labels):
        core,tip,vector=terminal[:3];order=terminal[3] if len(terminal)>3 else 1;fragment=ligand(label,order);offset=rw.GetNumAtoms()
        for atom in fragment.GetAtoms():rw.AddAtom(Chem.Atom(atom))
        for bond in fragment.GetBonds():rw.AddBond(offset+bond.GetBeginAtomIdx(),offset+bond.GetEndAtomIdx(),bond.GetBondType())
        rw.AddBond(core,offset,Chem.BondType.DOUBLE if order==2 else Chem.BondType.SINGLE);local[core][offset]=vector
    mol=rw.GetMol();Chem.SanitizeMol(mol)
    for index,neighbours in local.items():
        if len(neighbours)!=4:continue
        trial=Chem.Mol(mol);conf=Chem.Conformer(trial.GetNumAtoms());conf.Set3D(True)
        for i in range(trial.GetNumAtoms()):conf.SetAtomPosition(i,Point3D(0,0,0))
        for i,v in neighbours.items():conf.SetAtomPosition(i,Point3D(*v))
        trial.RemoveAllConformers();trial.AddConformer(conf);Chem.AssignAtomChiralTagsFromStructure(trial,replaceExistingTags=True)
        mol.GetAtomWithIdx(index).SetChiralTag(trial.GetAtomWithIdx(index).GetChiralTag())
    Chem.AssignStereochemistry(mol,cleanIt=True,force=True);rdCIPLabeler.AssignCIPLabels(mol)
    return mol,centres,kind


def labels(root):
    records=[];skipped=0
    em=float(root.get('LabelSize','10'));unit_length=float(root.get('BondLength','14.4'))
    for group in root.iter('group'):
        if group.get('Name') not in NAMES:continue
        try:mol,centres,kind=molecule(group)
        except (ValueError,TypeError,RuntimeError):skipped+=1;continue
        for i,centre in enumerate(centres):
            atom=mol.GetAtomWithIdx(i)
            if not atom.HasProp('_CIPCode'):continue
            code=atom.GetProp('_CIPCode')
            if code not in ('R','S','r','s'):skipped+=1;continue
            # Fischer labels sit in the open diagonal quadrant beside each
            # centre. Newman front/back labels occupy distinct circle sectors.
            if kind=='Fischer projection':p=(centre[0]+em*1.6,centre[1]-em*.65)
            elif kind=='Newman projection':p=(centre[0]+(-1 if i==0 else 1)*unit_length*.9,centre[1]-unit_length*.55)
            else:p=(centre[0]-em*.6,centre[1]-em*.9)
            records.append('projection\t%s:%d\t%s\t%.5f\t%.5f\t%.5f\t%.5f'%(group.get('id','0'),i,code,*p,*centre))
    return records,skipped
