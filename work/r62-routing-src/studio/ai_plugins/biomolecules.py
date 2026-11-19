"""Verified editable biomolecule components and carbohydrate projections."""
import json, math
from pathlib import Path
import xml.etree.ElementTree as ET

DATA=json.loads(Path(__file__).with_name('biomolecule_data.json').read_text(encoding='utf-8'))
AA=dict(zip('ACDEFGHIKLMNPQRSTVWY','丙氨酸 半胱氨酸 天冬氨酸 谷氨酸 苯丙氨酸 甘氨酸 组氨酸 异亮氨酸 赖氨酸 亮氨酸 甲硫氨酸 天冬酰胺 脯氨酸 谷氨酰胺 精氨酸 丝氨酸 苏氨酸 缬氨酸 色氨酸 酪氨酸'.split()))
for code,name in AA.items():DATA['aa_'+code]={'name':('' if code=='G' else 'L-')+name,'sequence':code,'source':'RDKit MolFromSequence, natural L amino acids'}
SUGARS=[k for k in DATA if k.startswith(('alpha_','beta_'))]

def category(key):
    if key in SUGARS:return '糖类'
    if key.startswith('aa_'):return '氨基酸'
    if key in ('adenine','guanine','cytosine','thymine','uracil'):return '核酸碱基'
    if key in ('adenosine','thymidine','atp'):return '核苷与核苷酸'
    return '脂质'

def sugar_ring(mol):
    for ring in mol.GetRingInfo().AtomRings():
        oxygen=[i for i in ring if mol.GetAtomWithIdx(i).GetAtomicNum()==8]
        if len(oxygen)!=1 or len(ring) not in (5,6):continue
        o=oxygen[0];members=set(ring)
        starts=[a.GetIdx() for a in mol.GetAtomWithIdx(o).GetNeighbors() if a.GetIdx() in members and any(n.GetIdx() not in members and n.GetAtomicNum()==8 for n in a.GetNeighbors())]
        if len(starts)!=1:continue
        order=[starts[0]];previous=o
        while len(order)<len(ring)-1:
            nxt=[n.GetIdx() for n in mol.GetAtomWithIdx(order[-1]).GetNeighbors() if n.GetIdx() in members and n.GetIdx()!=previous]
            previous=order[-1];order.append(nxt[0])
        return order+[o]
    raise ValueError('未找到具有明确异头碳的单糖环')

def sugar_coordinates(mol,key,form):
    from chemistry_open import Chem
    from rdkit.Geometry import Point3D
    ring=sugar_ring(mol);six=len(ring)==6
    if form.startswith('chair') and not six:raise ValueError('五元糖环不能使用六元环椅式；请选择 haworth 或 auto')
    if six:
        up=[key.startswith('beta_'),False,True,False,True]
        if 'galactose' in key:up[3]=True
        if 'mannose' in key:up[1]=True
    else:up=[True,False,False,True]
    chair=form.startswith('chair')
    if chair:
        points=[(1.2137,.4329),(.2527,.1582),(-.7137,.4329),(-1.2137,-.4326),(-.2527,-.1582),(.7137,-.4326)]
        if form=='chair_1c4':points=[(x,-y) for x,y in points]
        eq=[(1,-.35),(.25,1),(-1,-.12),(-1,.12),(-.25,-1)]
    elif six:points=[(1.2,0),(.6,.55),(-.6,.55),(-1.2,0),(-.6,-.55),(.6,-.55)]
    else:points=[(1,0),(.55,.8),(-.55,.8),(-1,0),(0,-.55)]
    positions={i:(x*1.5,y*1.5) for i,(x,y) in zip(ring,points)};members=set(ring);substituents=[]
    for carbon,i in enumerate(ring[:-1]):
        branches=[n.GetIdx() for n in mol.GetAtomWithIdx(i).GetNeighbors() if n.GetIdx() not in members]
        for branch in branches:
            upward=up[carbon];axial=False
            if chair:
                axial_up=carbon%2==1
                if form=='chair_1c4':axial_up=not axial_up
                axial=upward==axial_up
                dx,dy=(0,-1 if upward else 1) if axial else eq[carbon]
                if not axial:dy=abs(dy)*(-1 if upward else 1)
            else:dx,dy=0,-1 if upward else 1
            # Front C3 OH in Haworth sits inside the projected ring; extending
            # it through the rear C5 vertex produces a misleading attachment.
            reach=.8 if not chair and six and carbon==2 and upward else 1.6
            length=math.hypot(dx,dy);dx*=reach/length;dy*=reach/length
            x,y=positions[i];positions[branch]=(x+dx,y+dy)
            # Hydroxymethyl branch: keep its terminal O clear of the sugar ring.
            for child in mol.GetAtomWithIdx(branch).GetNeighbors():
                if child.GetIdx()!=i:positions[child.GetIdx()]=(x+dx+1.15,y+dy+(-.85 if upward else .85))
            substituents.append({'carbon':carbon+1,'atom':branch,'up':upward,'orientation':('axial' if axial else 'equatorial') if chair else ('up' if upward else 'down')})
    if len(positions)!=mol.GetNumAtoms():raise ValueError('模板含未布置的支链，停止绘制以保留结构')
    conf=Chem.Conformer(mol.GetNumAtoms());conf.Set3D(False)
    for i,(x,y) in positions.items():conf.SetAtomPosition(i,Point3D(x,-y,0))
    mol.RemoveAllConformers();mol.AddConformer(conf)
    return ring,substituents

def molecule(key,form='auto',sequence=None):
    from chemistry_open import Chem
    from rdkit.Chem import rdDepictor
    if key=='peptide':
        if not sequence or not 1<=len(sequence)<=30 or any(c not in AA for c in sequence):raise ValueError('肽序列需要 1–30 个标准氨基酸单字母大写代码')
        mol=Chem.MolFromSequence(sequence)
    else:
        item=DATA[key];mol=Chem.MolFromSequence(item['sequence']) if 'sequence' in item else Chem.MolFromSmiles(item['smiles'])
    if mol is None or mol.GetNumAtoms()>400:raise ValueError('组件无法解析或超过 400 个原子，请缩短肽序列')
    ring=[];subs=[]
    if key in SUGARS:
        if form=='auto':form='chair_4c1' if len(sugar_ring(mol))==6 else 'haworth'
        if form=='flat':rdDepictor.Compute2DCoords(mol,forceRDKit=True)
        else:ring,subs=sugar_coordinates(mol,key,form)
    else:
        if form not in ('auto','flat'):raise ValueError('此组件不适用糖环投影，请选择 auto 或 flat')
        form='flat';rdDepictor.Compute2DCoords(mol,forceRDKit=True,useRingTemplates=True)
    return mol,form,ring,subs

def cdxml(key,form='auto',sequence=None):
    from chemistry_open import to_cdxml
    mol,form,ring,subs=molecule(key,form,sequence)
    xml=to_cdxml(mol,preserve_coordinates=True)
    return mol,xml,form,ring,subs

def register(p):
    p.PLUGIN_TOOLS.extend([
        p._tool('list_biomolecules','列出已验证的生物分子组件及支持的构象；无需联网。',{'category':{'type':'string','enum':['all','糖类','氨基酸','核酸碱基','核苷与核苷酸','脂质']}}),
        p._tool('draw_biomolecule','绘制可编辑生物分子组件。六元糖 auto 默认 4C1 椅式，支持环翻转 1C4、Haworth 投影；区分 α/β 和 D 型。可画天然 L 氨基酸、短肽、碱基、核苷、ATP 与脂质。',
            {'component':{'type':'string','enum':list(DATA)+['peptide']},'conformation':{'type':'string','enum':['auto','chair_4c1','chair_1c4','haworth','flat']},'sequence':{'type':'string'},'x':p.NUM,'y':p.NUM,'label':p.STR,'color':p.STR,'width':p.NUM},['component'])])
    p.PLUGIN_NAMES.update(('list_biomolecules','draw_biomolecule'))
    p._HANDLERS.update({'list_biomolecules':lambda e,a:listing(a),'draw_biomolecule':lambda e,a:draw(p,e,a)})
    p.PLUGIN_SYSTEM+='\n【生物分子组件】糖分子优先 list_biomolecules / draw_biomolecule，六元糖默认椅式 chair_4c1，不要用普通 cyclohexane 或 draw_path 代替真实糖结构。指定 α/β 和 D/L，不要凭名称自行改变立体中心；组件库目前只含列出的 D 糖异头体，L 糖或其他糖先检索确认。chair_1c4 是环翻转，保持分子绝对构型不变；五元糖只用 haworth/flat，不能声称椅式。常用氨基酸、碱基、核苷、ATP、脂质和短肽也优先组件工具。复杂多糖或保护糖尚无专用模板，按完整已验证结构处理，不要以单糖冒充。'

def listing(a):
    selected=a.get('category','all')
    return {'components':[{'component':k,'name':v['name'],'category':category(k),'conformations':(['chair_4c1','chair_1c4','haworth','flat'] if len(k)>0 and k in SUGARS and 'ribose' not in k else ['haworth','flat'] if k in SUGARS else ['flat']),'source':v['source']} for k,v in DATA.items() if selected=='all' or category(k)==selected],
            'peptide':'component=peptide, sequence=标准单字母大写序列；游离 N/C 末端，标准 L 型，非两性离子表示。'}

def draw(p,e,a):
    from chemistry_open import identity,rdMolDescriptors
    key=a['component']
    if key!='peptide' and 'sequence' in a:raise ValueError('sequence 参数仅用于 peptide')
    mol,xml,form,ring,subs=cdxml(key,a.get('conformation','auto'),a.get('sequence'))
    root=ET.fromstring(xml)
    for font in root.iter('font'):font.set('name',e.context.get('font','Times New Roman'))
    old=p._document_bounds(e);x=float(a.get('x',0));y=float(a.get('y',0))
    # Place by the actual local bounds, including projecting substituents.
    coords=[tuple(map(float,n.get('p').split())) for n in root.iter('n')];scale=float(e.context.get('bond_length',14.4))/30
    minx=min(c[0] for c in coords)*scale;miny=min(c[1] for c in coords)*scale
    if old and 'y' not in a:y=old[3]+28-miny
    if 'x' not in a:x=-minx+12
    attrs={'x':str(x),'y':str(y),**p._NS['style']({k:a[k] for k in ('color','width') if k in a})}
    node=ET.Element('molecule',attrs);node.text=ET.tostring(root,encoding='unicode')
    made=e.record_creation(node);e.actions.append(node);e.molecules+=1
    label=a.get('label',DATA[key]['name'] if key!='peptide' else a['sequence'])
    if len(label)>200:raise ValueError('标签最长 200 字')
    label_ref=None
    if label:label_ref=p._add_caption(e,{'target':made['ref'],'text':label,'position':'below','size':10})['ref']
    return {'staged':True,**made,'component':key,'conformation':form,'ring_atoms':ring,'substituents':subs,'label_ref':label_ref,'smiles':identity(mol),
            'formula':rdMolDescriptors.CalcMolFormula(mol),'source':DATA[key]['source'] if key!='peptide' else 'RDKit MolFromSequence','diagnostics':{'stage':'complete','atoms':mol.GetNumAtoms(),'rings':mol.GetRingInfo().NumRings(),'stereocenters':len(__import__('rdkit').Chem.FindMolChiralCenters(mol)),'suggestion':''}}

