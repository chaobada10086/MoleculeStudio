"""Offline naming and 1H shift backend using local open-source chemistry libraries."""
from pathlib import Path
import ast
import collections
import math
import json
import tempfile
import re
import subprocess
import sys
import xml.etree.ElementTree as ET

BASE = Path(__file__).resolve().parent
RUNTIME = BASE / 'chemistry_runtime'
sys.path.insert(0, str(RUNTIME / 'python-site'))
from rdkit import Chem
from rdkit.Chem import rdDepictor, rdMolDescriptors


def java(jar, args, text=None):
    result = subprocess.run(
        [str(RUNTIME / 'java/bin/java.exe'), '-Xmx512m', '-Dfile.encoding=UTF-8',
         '-jar', str(RUNTIME / jar), *args], input=text, text=True, encoding='utf-8',
        errors='replace', stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        cwd=RUNTIME, timeout=24, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    if result.returncode:
        raise ValueError('开源化学引擎执行失败：' + result.stderr.strip()[-800:])
    return result.stdout


def name_molecule(name):
    if not name.strip() or len(name) > 6000 or '\n' in name.strip() or '\r' in name.strip():
        raise ValueError('请输入单个英文系统名称或 OPSIN 支持的常用名称')
    output = java('opsin.jar', ['-osmi'], name.strip() + '\n')
    lines = [line.strip() for line in output.splitlines() if line.strip()]
    mol = Chem.MolFromSmiles(lines[0]) if len(lines) == 1 else None
    if mol is None:
        raise ValueError('OPSIN 无法解析该名称，请检查英文命名、括号、位次和立体描述符')
    return mol


def formula_nicknames():
    # RDKit's public abbreviation patterns supplement the project nickname catalog.
    # Exact spelling is significant (for example CN and NC differ chemically).
    from rdkit.Chem import rdAbbreviations
    patterns={a.label:Chem.MolToSmiles(a.mol) for a in rdAbbreviations.GetDefaultAbbreviations()}
    patterns.update({'Me2N':'*N(C)C','NMe2':'*N(C)C','O2N':'*[N+](=O)[O-]',
                     'CO2Me':'*C(=O)OC','COOMe':'*C(=O)OC','MeO2C':'*C(=O)OC',
                     'NHMe':'*NC','MeNH':'*NC','Et2N':'*N(CC)CC','NEt2':'*N(CC)CC'})
    result={}
    for label,smiles in patterns.items():
        mol=Chem.MolFromSmiles(smiles);rdDepictor.Compute2DCoords(mol);Chem.Kekulize(mol,clearAromaticFlags=True)
        fragment=ET.Element('fragment',{'id':'1'})
        for a in mol.GetAtoms():
            pos=mol.GetConformer().GetAtomPosition(a.GetIdx());attrs={'id':str(a.GetIdx()+2),'p':f'{pos.x*20} {-pos.y*20}'}
            if not a.GetAtomicNum():attrs['NodeType']='ExternalConnectionPoint'
            else:
                attrs.update({'Element':str(a.GetAtomicNum()),'NumHydrogens':str(a.GetTotalNumHs())})
                if a.GetFormalCharge():attrs['Charge']=str(a.GetFormalCharge())
            ET.SubElement(fragment,'n',attrs)
        for b in mol.GetBonds():ET.SubElement(fragment,'b',{'id':str(mol.GetNumAtoms()+2+b.GetIdx()),'B':str(b.GetBeginAtomIdx()+2),'E':str(b.GetEndAtomIdx()+2),'Order':str(int(b.GetBondTypeAsDouble()))})
        result[label]=ET.tostring(fragment,encoding='unicode')
    return result


def expand_known_nicknames(root):
    """Fill omitted nickname definitions in a calculation-only XML copy."""
    catalog_path=BASE/'nicknames.json'
    if not catalog_path.exists(): raise ValueError('缺少本地缩写结构库，请修复程序安装')
    catalog=json.loads(catalog_path.read_text(encoding='utf-8'))
    formulas=formula_nicknames()
    for label,fragment in formulas.items():catalog.setdefault(label,fragment)
    used={int(e.get('id')) for e in root.iter() if e.get('id') and e.get('id').isdigit()}
    next_id=1
    def allocate():
        nonlocal next_id
        while next_id in used: next_id+=1
        used.add(next_id);return next_id
    for depth in range(20):
        changed=False
        for node in list(root.iter('n')):
            if node.find('fragment') is not None: continue
            label=''.join(s.text or '' for t in node.findall('t') for s in t.iter('s')).strip()
            if label not in catalog: continue
            if label not in formulas and node.get('NodeType','Element')=='Element' and int(node.get('Element','6')) not in (0,6): continue
            fragment=ET.fromstring(catalog[label]); ids={int(e.get('id')):allocate() for e in fragment.iter() if e.get('id')}
            for element in fragment.iter():
                for key in ('id','B','E','ConnectionOrder','BondOrdering','BondCircularOrdering','CrossingBonds','object'):
                    if key in element.attrib:
                        element.set(key,' '.join(str(ids.get(int(v),int(v))) for v in element.get(key).split()))
            definitions=list(fragment) if fragment.tag=='nickname' else [fragment]
            external=[e for e in fragment.iter('n') if e.get('NodeType')=='ExternalConnectionPoint']
            if len(external)==1 and node.get('p'):
                # The real attachment atom must occupy the original label's
                # position, or RDKit can invert a neighbouring stereocentre
                # when it perceives wedges after fusing the abbreviation.
                endpoint=external[0].get('id');connection=next((b for b in fragment.iter('b')if endpoint in (b.get('B'),b.get('E'))),None)
                if connection is not None:
                    anchor_id=connection.get('E') if connection.get('B')==endpoint else connection.get('B')
                    anchor=next((a for a in fragment.iter('n')if a.get('id')==anchor_id),None)
                    if anchor is not None and anchor.get('p'):
                        px,py=map(float,node.get('p').split());ax,ay=map(float,anchor.get('p').split())
                        scale=float(root.get('BondLength','30'))/30.
                        for a in fragment.iter('n'):
                            if a.get('p'):
                                x,y=map(float,a.get('p').split());a.set('p',f'{px+(x-ax)*scale:.10f} {py+(y-ay)*scale:.10f}');a.attrib.pop('xyz',None)
            if not external:
                # Complete reagents (S8, carbonate salts) have no attachment point.
                # Inline all of their components rather than retaining a dummy atom.
                incident=[b for b in root.iter('b') if node.get('id') in (b.get('B'),b.get('E'))]
                if incident: raise ValueError('缩写 '+label+' 表示完整试剂，没有可连接的取代基位点')
                parent=next(p for p in root.iter() if node in list(p));parent.remove(node)
                for definition in definitions:
                    for child in list(definition):parent.append(child)
            else:
                node.set('NodeType','Nickname');node.attrib.pop('NumHydrogens',None)
                for definition in definitions:node.append(definition)
            changed=True
        if not changed:return
    raise ValueError('缩写定义循环或嵌套过深，无法展开')


def expand_display(source):
    """Inline explicit nickname fragments without rebuilding the surrounding graph."""
    root=ET.fromstring(source)
    expand_known_nicknames(root)
    identity=Chem.MolToSmiles(one_molecule(source,allow_disconnected=True,max_atoms=10000),isomericSmiles=True)
    next_id=max([int(e.get('id')) for e in root.iter() if e.get('id','').isdigit()] or [0])+1
    for depth in range(1024):
        pairs=[(parent,n) for parent in root.iter('fragment') for n in parent.findall('n') if n.find('fragment') is not None]
        if not pairs: break
        # Innermost first so nested nicknames become ordinary nodes too.
        parent,node=pairs[-1]; fragment=node.find('fragment')
        external=[a for a in fragment.findall('n') if a.get('NodeType')=='ExternalConnectionPoint']
        if len(external)!=1: raise ValueError('该缩写需要恰好一个外部连接点，已保留原结构')
        ext=external[0]; connections=[b for b in fragment.findall('b') if ext.get('id') in (b.get('B'),b.get('E'))]
        if len(connections)!=1: raise ValueError('缩写连接点定义不完整')
        link=connections[0]; anchor_id=link.get('E') if link.get('B')==ext.get('id') else link.get('B')
        anchor=next((a for a in fragment.findall('n') if a.get('id')==anchor_id),None)
        if anchor is None: raise ValueError('缩写缺少连接原子')
        incident=[b for b in parent.findall('b') if node.get('id') in (b.get('B'),b.get('E'))]
        if len(incident)>1: raise ValueError('单连接点官能团不能连接多个外部化学键')
        origin=list(map(float,node.get('p','0 0').split())); ap=list(map(float,anchor.get('p','0 0').split()))
        ep=list(map(float,ext.get('p','0 0').split()))
        scale=1.;angle=0.
        lengths=[]
        nodes={a.get('id'):a for a in fragment.findall('n')}
        for b in fragment.findall('b'):
            if b.get('B') in nodes and b.get('E') in nodes:
                aa=list(map(float,nodes[b.get('B')].get('p','0 0').split()));bb=list(map(float,nodes[b.get('E')].get('p','0 0').split()))
                length=math.hypot(aa[0]-bb[0],aa[1]-bb[1])
                if length>1e-6:lengths.append(length)
        if lengths:scale=float(root.get('BondLength','14.4'))/sorted(lengths)[len(lengths)//2]
        if incident:
            b=incident[0];other=b.get('E') if b.get('B')==node.get('id') else b.get('B')
            neighbor=next((a for a in parent.findall('n') if a.get('id')==other),None)
            if neighbor is not None:
                np=list(map(float,neighbor.get('p','0 0').split()))
                angle=math.atan2(origin[1]-np[1],origin[0]-np[0])-math.atan2(ap[1]-ep[1],ap[0]-ep[0])
        mapping={anchor_id:node.get('id')}
        for a in fragment.iter():
            ident=a.get('id')
            if ident and ident not in mapping:
                mapping[ident]=str(next_id);next_id+=1
        if incident:mapping[link.get('id')]=incident[0].get('id')
        fragment.remove(ext);fragment.remove(link)
        for a in fragment.iter():
            for key in ('id','B','E','ConnectionOrder','BondOrdering','BondCircularOrdering','CrossingBonds','object'):
                if key in a.attrib:a.set(key,' '.join(mapping.get(v,v) for v in a.get(key).split()))
            if a.tag=='n' and a.get('p'):
                x,y=map(float,a.get('p').split());x=(x-ap[0])*scale;y=(y-ap[1])*scale
                a.set('p',f'{origin[0]+x*math.cos(angle)-y*math.sin(angle):.10f} {origin[1]+x*math.sin(angle)+y*math.cos(angle):.10f}')
                a.attrib.pop('xyz',None)
        parent.remove(node)
        for child in list(fragment):parent.append(child)
    else:raise ValueError('缩写嵌套过深')
    if any(n.get('NodeType') in ('Nickname','GenericNickname','ExternalConnectionPoint') for n in root.iter('n')):
        raise ValueError('选中结构包含未知缩写或未定义连接点')
    # Verify valence and stereochemistry are readable before returning anything.
    actual=Chem.MolToSmiles(one_molecule(ET.tostring(root,encoding='unicode'),allow_disconnected=True,max_atoms=10000),isomericSmiles=True)
    if identity!=actual:raise ValueError('展开会改变结构或立体化学，已保留原结构')
    return ET.tostring(root,encoding='unicode')


def one_molecule(source, mime='text/xml', allow_disconnected=False, allow_queries=False, max_atoms=200):
    if mime == 'chemical/x-daylight-smiles':
        mols = [Chem.MolFromSmiles(source.strip())]
    elif mime == 'chemical/x-helm':
        mols = [Chem.MolFromHELM(source.strip())]
    elif mime.startswith('chemical/x-mdl-molfile'):
        mols = [Chem.MolFromMolBlock(source, removeHs=False)]
    else:
        root = ET.fromstring(source)
        expand_known_nicknames(root)
        # A labelled R group must never be silently treated as a carbon atom.
        for node in root.iter('n'):
            kind = node.get('NodeType', '')
            if not allow_queries and kind in ('GenericNickname', 'Unspecified', 'VariableAttachment'):
                raise ValueError('结构包含未定义的 R 基团或连接点，请先补全结构')
        # RDKit clears its internal CDX atom IDs. Its supported reaction atom-map
        # parser retains an exact ID correspondence, even with coincident/rounded
        # coordinates and nested abbreviations. Add maps to this temporary XML
        # only; erase them from the molecule before any chemical calculations.
        for parent in root.iter():
            for child in list(parent):
                if child.tag == 'scheme': parent.remove(child)
        node_ids=[int(n.get('id')) for n in root.iter('n') if n.get('id')]
        if len(set(node_ids))!=len(node_ids): raise ValueError('结构包含重复原子编号，无法安全建立对应关系')
        page=root.find('page')
        if page is None: raise ValueError('结构文件缺少页面')
        object_ids=[int(e.get('id')) for e in root.iter() if e.get('id') and e.get('id').isdigit()]
        free_id=max(object_ids+[0])+1
        if free_id>2147483645: raise ValueError('结构对象编号超出可处理范围')
        scheme=ET.SubElement(page,'scheme',{'id':str(free_id)})
        ET.SubElement(scheme,'step',{'id':str(free_id+1),'ReactionStepAtomMap':' '.join(str(i) for node_id in node_ids for i in (node_id,node_id))})
        # The open RDKit reader resolves endpoints as it visits a fragment.
        # Our editor permits forward references and interleaves atom/bond rows.
        # Normalize only this temporary adapter input, retaining atom order/IDs.
        for fragment in root.iter('fragment'):
            for bond in list(fragment.findall('b')):
                fragment.remove(bond)
                fragment.append(bond)
        mols=list(Chem.MolsFromCDXML(ET.tostring(root,encoding='unicode')))
        for parsed in mols:
            if parsed is None: continue
            for atom in parsed.GetAtoms():
                index=atom.GetAtomMapNum()-1
                if 0<=index<len(node_ids): atom.SetIntProp('_STUDIO_CDX_ID',node_ids[index])
                atom.SetAtomMapNum(0)
    if not mols or any(m is None for m in mols):
        raise ValueError('无法读取完整的分子结构')
    if not allow_disconnected and (len(mols) != 1 or len(Chem.GetMolFrags(mols[0])) != 1):
        raise ValueError('请仅选中一个完整分子后重试；当前选区包含多个组分')
    combined = mols[0]
    for other in mols[1:]: combined = Chem.CombineMols(combined, other)
    mol = Chem.RemoveHs(combined)
    if not mol.GetNumAtoms() or mol.GetNumAtoms() > max_atoms:
        raise ValueError(f'此功能支持 1–{max_atoms} 个重原子的完整分子')
    if not allow_queries and any(a.GetAtomicNum() == 0 for a in mol.GetAtoms()):
        raise ValueError('结构含未定义的原子或缩写，请先展开并补全结构')
    Chem.SanitizeMol(mol)
    Chem.AssignStereochemistry(mol, cleanIt=True, force=True)
    return mol


def identity(mol):
    return Chem.MolToSmiles(Chem.RemoveHs(mol), canonical=True, isomericSmiles=True)


def systematic_name(mol):
    from nispo import mol_to_iupac
    name = mol_to_iupac(mol)
    if not name:
        raise ValueError('NISPO 暂不能为此结构生成系统名称')
    recovered = name_molecule(name)
    if identity(recovered) != identity(mol):
        raise ValueError('生成的名称未通过 OPSIN 往返校验，已拒绝插入不一致的名称')
    return name


def to_cdxml(mol, explicit_hydrogens=False, preserve_coordinates=False):
    # WedgeMolBonds replaces SMILES bond directions used by RemoveHs.
    # Compare the payload to the untouched input, never to the wedged copy.
    expected_identity = identity(mol)
    # A stereogenic alkene with unspecified E/Z must remain unspecified after
    # coordinate-based readers inspect its drawing. CDXML represents this as a
    # crossed double bond (Display=Wavy), not as a definite geometric isomer.
    unspecified_double_bonds = {info.centeredOn for info in Chem.FindPotentialStereo(mol)
        if info.type == Chem.StereoType.Bond_Double and info.specified != Chem.StereoSpecified.Specified}
    work = Chem.Mol(mol)
    if not preserve_coordinates or not work.GetNumConformers():
        rdDepictor.Compute2DCoords(work)
    Chem.WedgeMolBonds(work, work.GetConformer())
    kekule = Chem.Mol(work)
    Chem.Kekulize(kekule, clearAromaticFlags=True)
    conf = work.GetConformer()
    # RDKit normally uses 1.5-unit bonds, CoordGen uses 1.0 and database
    # coordinates may use another scale. Match the declared 30 pt bond length
    # while preserving all relative geometry and stereochemistry.
    lengths = sorted((conf.GetAtomPosition(b.GetBeginAtomIdx()) -
                      conf.GetAtomPosition(b.GetEndAtomIdx())).Length() for b in work.GetBonds())
    lengths = [v for v in lengths if math.isfinite(v) and v > 1e-8]
    median = (lengths[len(lengths)//2] + lengths[(len(lengths)-1)//2]) / 2 if lengths else 1.5
    coordinate_scale = 30.0 / median

    def build(y_sign, swap_wedges):
        root = ET.Element('CDXML', {'BondLength': '30', 'LabelFont': '3', 'LabelSize': '10',
                                   'LineWidth': '0.6', 'BondSpacing': '18'})
        fonts = ET.SubElement(root, 'fonttable')
        ET.SubElement(fonts, 'font', {'id': '3', 'charset': 'iso-8859-1', 'name': 'Arial'})
        page = ET.SubElement(root, 'page', {'id': '1'})
        frag = ET.SubElement(page, 'fragment', {'id': '2'})
        for atom in work.GetAtoms():
            point = conf.GetAtomPosition(atom.GetIdx())
            attrs = {'id': str(atom.GetIdx()+10), 'Element': str(atom.GetAtomicNum()),
                     'p': f'{point.x*coordinate_scale:.8f} {y_sign*point.y*coordinate_scale:.8f}'}
            if atom.GetFormalCharge(): attrs['Charge'] = str(atom.GetFormalCharge())
            if atom.GetIsotope(): attrs['Isotope'] = str(atom.GetIsotope())
            if explicit_hydrogens or atom.GetAtomicNum() not in (1, 6) or atom.GetNumExplicitHs():
                attrs['NumHydrogens'] = str(atom.GetTotalNumHs())
            node = ET.SubElement(frag, 'n', attrs)
            if atom.GetAtomicNum() not in (1, 6):
                text = ET.SubElement(node, 't')
                hydrogens = atom.GetTotalNumHs()
                label = atom.GetSymbol() + ('H' + (str(hydrogens) if hydrogens > 1 else '') if hydrogens else '')
                ET.SubElement(text, 's', {'font': '3', 'size': '10', 'face': '96'}).text = label
        for bond in work.GetBonds():
            direction = bond.GetBondDir()
            attrs = {'id': str(1000+bond.GetIdx()), 'B': str(10+bond.GetBeginAtomIdx()),
                     'E': str(10+bond.GetEndAtomIdx()), 'Order': format(kekule.GetBondWithIdx(bond.GetIdx()).GetBondTypeAsDouble(), 'g')}
            if direction == Chem.BondDir.BEGINWEDGE:
                attrs['Display'] = 'WedgedHashBegin' if swap_wedges else 'WedgeBegin'
            if direction == Chem.BondDir.BEGINDASH:
                attrs['Display'] = 'WedgeBegin' if swap_wedges else 'WedgedHashBegin'
            if bond.GetIdx() in unspecified_double_bonds:
                attrs['Display'] = 'Wavy'
            ET.SubElement(frag, 'b', attrs)
        return ET.tostring(root, encoding='unicode')

    # A few readers disagree on whether CDXML's screen-down Y and wedge direction
    # already account for the reflection.  Try the two wedge polarities and the
    # two Y conventions, but accept ONLY a payload that round-trips to the input
    # stereochemistry.  This fixes false "stereo validation failed" errors on
    # crowded dienes/ring systems without ever accepting a different isomer.
    for y_sign, swap_wedges in ((-1, False), (-1, True), (1, False), (1, True)):
        output = build(y_sign, swap_wedges)
        try:
            got = identity(one_molecule(output, allow_disconnected=True, max_atoms=400))
        except Exception:
            continue
        if got == expected_identity:
            return '<?xml version="1.0" encoding="UTF-8"?>\n' + output
    raise ValueError('结构绘图序列化未通过立体化学校验（已尝试 4 种楔形/坐标编码），已保留原文档')


def nmr_structure(mol):
    """An expanded display copy with the exact source IDs; never edits input XML."""
    root=ET.fromstring(to_cdxml(mol, explicit_hydrogens=True))
    ids={10+a.GetIdx():a.GetIntProp('_STUDIO_CDX_ID') for a in mol.GetAtoms()}
    used=set(ids.values()); next_id=1
    for element in root.iter():
        if element.tag=='n': element.set('id',str(ids[int(element.get('id'))]))
        elif element.tag in ('page','fragment','b'):
            while next_id in used: next_id+=1
            element.set('id',str(next_id));used.add(next_id);next_id+=1
        if element.tag=='b':
            element.set('B',str(ids[int(element.get('B'))]));element.set('E',str(ids[int(element.get('E'))]))
    return ET.tostring(root,encoding='unicode')


def simple_proton_splitting(mol, entries, per_parent, symmetry):
    """Topology-based n+1 approximation; all vicinal protons share assumed J."""
    parents = sorted({r['parent'] for r in entries})
    hints = []
    for parent in parents:
        atom = mol.GetAtomWithIdx(parent)
        if atom.GetAtomicNum() != 6:
            return None
        # H-F and other heteronuclear couplings are outside this simple model.
        if any(n.GetAtomicNum() in (9, 15) for n in atom.GetNeighbors()):
            return None
        coupled = []
        for neighbor in atom.GetNeighbors():
            j = neighbor.GetIdx()
            if neighbor.GetAtomicNum() != 6 or j not in per_parent or symmetry[j] == symmetry[parent]:
                continue
            bond = mol.GetBondBetweenAtoms(parent, j)
            if atom.GetIsAromatic():
                if not neighbor.GetIsAromatic() or not bond.GetIsAromatic():
                    return None
            elif (atom.GetHybridization() != Chem.HybridizationType.SP3 or
                  neighbor.GetHybridization() != Chem.HybridizationType.SP3 or
                  bond.GetBondType() != Chem.BondType.SINGLE):
                return None
            if any(n.GetAtomicNum() in (9, 15) for n in neighbor.GetNeighbors()):
                return None
            neighbor_rows = per_parent[j]
            coupled.extend(float(r['shift_ppm']) for r in neighbor_rows)
        if not coupled:
            # Long-range aromatic and other couplings cannot be inferred here.
            if atom.GetIsAromatic() or atom.GetHybridization() != Chem.HybridizationType.SP3:
                return None
            hints.append((0, 0.0))
        else:
            count = len(coupled)
            if count > 12:
                return None
            center = sum(float(r['shift_ppm']) for r in entries)/len(entries)
            hints.append((count, min(coupled, key=lambda shift: abs(shift-center))))
    if not hints or any(h[0] != hints[0][0] for h in hints):
        return None
    center = sum(float(r['shift_ppm']) for r in entries)/len(entries)
    return hints[0][0], min((h[1] for h in hints), key=lambda shift: abs(shift-center))


def predict_ml_nmr(mol, nucleus, solvent=None):
    if solvent in (None, "", "unknown"):
        solvent = "cdcl3"
    # Isolated, CPU-only Python runtime. Bundled checkpoints are verified by CSP5;
    # refuse missing checkpoints instead of letting the package fetch at runtime.
    import os
    target_number={'1H':1,'13C':6,'19F':9}[nucleus]
    target_count=sum(a.GetTotalNumHs() for a in mol.GetAtoms()) if nucleus=='1H' else sum(a.GetAtomicNum()==target_number for a in mol.GetAtoms())
    if not target_count: raise ValueError({'1H':'当前结构不含可预测的氢原子','13C':'当前结构不含碳原子','19F':'当前结构不含氟原子，无需预测氟谱'}[nucleus])
    os.environ.setdefault('OMP_NUM_THREADS', '2')
    os.environ.setdefault('MKL_NUM_THREADS', '2')
    sys.path.insert(0, str(RUNTIME / 'nmr-ml-site'))
    import torch
    torch.set_num_threads(2)
    from csp5 import predict_mols
    from csp5.model_registry import get_model_spec
    from rdkit.Chem import AllChem
    atom_number = {'1H':1, '13C':6, '19F':9}[nucleus]
    explicit = Chem.AddHs(Chem.Mol(mol))
    target_count = sum(a.GetAtomicNum()==atom_number for a in explicit.GetAtoms())
    if not target_count: raise ValueError({'1H':'当前结构不含可预测的氢原子', '13C':'当前结构不含碳原子', '19F':'当前结构不含氟原子，无需预测氟谱'}[nucleus])
    if any(a.GetIsotope() for a in mol.GetAtoms()): raise ValueError('当前模型不支持同位素标记结构')
    if mol.GetNumAtoms()>120: raise ValueError('CSP5 本地预测支持最多 120 个重原子，请缩小选区')
    model = 'CSP5q-'+nucleus
    spec = get_model_spec(nucleus, model_name=model)
    if solvent is not None and solvent not in spec.solvent_vocab:
        raise ValueError('当前模型不支持所选溶剂：'+str(solvent))
    if not spec.weights_path.exists(): raise ValueError('缺少本地模型：'+model+'，请修复核磁运行时')
    explicit.RemoveAllConformers()
    params = AllChem.ETKDGv3(); params.randomSeed=20261020; params.numThreads=2
    if AllChem.EmbedMolecule(explicit, params) < 0: raise ValueError('无法为该分子生成有效的三维构象')
    if AllChem.MMFFHasAllMoleculeParams(explicit): AllChem.MMFFOptimizeMolecule(explicit,maxIters=200)
    else: AllChem.UFFOptimizeMolecule(explicit,maxIters=200)
    result = predict_mols([explicit], nucleus=nucleus, model_name=model, device='cpu', solvent=solvent)
    if solvent is not None and result.solvent_key != solvent:
        raise ValueError('预测模型未使用所选溶剂，已拒绝返回不匹配的结果')
    if result.predictions.empty or result.failures: raise ValueError('CSP5 无法预测该结构：'+'; '.join(map(str,result.failures)))
    rows = result.predictions.to_dict('records')
    if len(rows)!=target_count: raise ValueError('模型输出原子数不完整，已拒绝错标')
    symmetry = list(Chem.CanonicalRankAtoms(mol, breakTies=False))
    per_parent = collections.defaultdict(list)
    seen = set()
    for row in rows:
        index = int(row['atom_index'])
        if index in seen or not 0<=index<explicit.GetNumAtoms(): raise ValueError('模型返回无效原子映射')
        seen.add(index); atom=explicit.GetAtomWithIdx(index)
        if atom.GetAtomicNum()!=atom_number: raise ValueError('模型返回的核类型与原子不匹配')
        parent = atom.GetNeighbors()[0].GetIdx() if nucleus=='1H' else index
        if parent>=mol.GetNumAtoms(): raise ValueError('氢原子所属原子映射失败')
        row['parent']=parent
        for key in ['shift_ppm','shift_std_ppm','shift_q05_ppm','shift_q95_ppm']:
            if not math.isfinite(float(row[key])): raise ValueError('模型返回了非有限预测值')
        per_parent[parent].append(row)
    groups=collections.defaultdict(list)
    for parent, entries in per_parent.items():
        entries.sort(key=lambda r:r['shift_ppm'])
        # Methyl/heteroatom H are rotation-averaged; resolved methylene H remain
        # separate when the model predicts different environments.
        split = nucleus=='1H' and len(entries)==2 and mol.GetAtomWithIdx(parent).GetAtomicNum()==6 and abs(entries[0]['shift_ppm']-entries[1]['shift_ppm'])>.12
        for i,row in enumerate(entries): groups[(symmetry[parent], i if split else 0)].append(row)
    low,high={'1H':(.25,.6),'13C':(3.,8.),'19F':(5.,15.)}[nucleus]
    records=['NMR'+nucleus+'\t1', 'formula\t'+rdMolDescriptors.CalcMolFormula(mol), f'coverage\t{len(rows)}\t{target_count}', 'engine\t'+model+' 0.3.1 / CPU',
             'mapping\t'+str(int(all(a.HasProp('_STUDIO_CDX_ID') for a in mol.GetAtoms()))),
             'solvent\t'+(result.solvent_key or 'unknown'),
             'warning\t单个三维构象；'+('溶剂未指定' if solvent is None else '溶剂条件：'+solvent)+'；'+('一阶裂分示意：相邻氢统一假定 J=7 Hz，未预测真实耦合；不含偕偶合、长程耦合和强耦合。' if nucleus=='1H' else '仅显示化学位移，不模拟 J 耦合。'),
             'warning\tσ 来源于模型分位区间，不代表实验准确率；罕见或训练集之外的环境仍可能失准。']
    if solvent in ('d2o','cd3od'):
        records.append('warning\t未模拟 H/D 交换：OH/NH 等可交换氢的预测峰可能在该溶剂的实验谱中消失。')
    records.extend('warning\t'+str(w).replace('\t',' ').replace('\n',' ') for w in result.warnings)
    peaks=[]
    for entries in groups.values():
        ppm=sum(float(r['shift_ppm']) for r in entries)/len(entries)
        std=max(float(r['shift_std_ppm']) for r in entries)
        q05=min(float(r['shift_q05_ppm']) for r in entries); q95=max(float(r['shift_q95_ppm']) for r in entries)
        quality='good' if std<=low else 'medium' if std<=high else 'rough'
        ids=sorted({mol.GetAtomWithIdx(r['parent']).GetIntProp('_STUDIO_CDX_ID') if mol.GetAtomWithIdx(r['parent']).HasProp('_STUDIO_CDX_ID') else r['parent']+1 for r in entries})
        reason=f'CSP5q; σ={std:.3f} ppm; q05–q95={q05:.3f}–{q95:.3f} ppm'
        hint=simple_proton_splitting(mol,entries,per_parent,symmetry) if nucleus=='1H' else None
        mult=(('s','d','t','q','quint','sext','sept')[hint[0]] if hint[0]<=6 else 'm')+'（示意）' if hint else '—'
        extra=f"\t{hint[0]}\t{hint[1]:.6f}" if hint else ''
        peaks.append((ppm,f"peak\t{ppm:.4f}\t{len(entries):.4f}\t{','.join(map(str,ids))}\t{mult}\t{reason}\t{quality}{extra}"))
    records.extend(line for _,line in sorted(peaks,reverse=True))
    if all(a.HasProp('_STUDIO_CDX_ID') for a in mol.GetAtoms()):
        try: records.append('structure\t'+nmr_structure(mol))
        except ValueError: records.append('warning\t重新布局未通过立体化学校验，保留原绘图用于标注。')
    return '\n'.join(records)+'\n'


def predict_hnmr(mol): return predict_ml_nmr(mol,'1H')
def predict_cnmr(mol): return predict_ml_nmr(mol,'13C')
def predict_fnmr(mol): return predict_ml_nmr(mol,'19F')


def clean_structure(source):
    """RDKit 2D depiction, keeping the user's atom IDs and collapsed labels."""
    root=ET.fromstring(source);expand_known_nicknames(root)
    mol=one_molecule(ET.tostring(root,encoding='unicode'),allow_disconnected=True,allow_queries=True)
    before=identity(mol)
    id_to_index={a.GetIntProp('_STUDIO_CDX_ID'):a.GetIdx() for a in mol.GetAtoms() if a.HasProp('_STUDIO_CDX_ID')}
    if len(id_to_index)!=mol.GetNumAtoms():raise ValueError('开源整理无法建立完整原子映射，保留原结构')
    nodes={int(n.get('id')):n for n in root.iter('n') if n.get('id')}
    aliases={};ports={}
    for node_id,node in nodes.items():
        fragment=node.find('fragment')
        if fragment is None:continue
        endpoints=[n for n in fragment.findall('n') if n.get('NodeType')=='ExternalConnectionPoint']
        connection_order=fragment.get('ConnectionOrder','').split()
        if connection_order:endpoints.sort(key=lambda n:connection_order.index(n.get('id')) if n.get('id') in connection_order else len(connection_order))
        anchors=[]
        for endpoint in endpoints:
            ext=endpoint.get('id')
            bond=next((b for b in fragment.findall('b')if ext in (b.get('B'),b.get('E'))),None)
            if bond is not None:anchors.append(int(bond.get('E') if bond.get('B')==ext else bond.get('B')))
        aliases[node_id]=[int(n.get('id')) for n in fragment.iter('n') if int(n.get('id')) in id_to_index]
        outer=node.get('BondOrdering','').split() or [b.get('id')for b in root.iter('b')if str(node_id) in (b.get('B'),b.get('E'))]
        for bond_id,anchor in zip(outer,anchors):ports[(node_id,int(bond_id))]=anchor
    rdDepictor.Compute2DCoords(mol,clearConfs=True,forceRDKit=True,useRingTemplates=True)
    for bond in mol.GetBonds():bond.SetBondDir(Chem.BondDir.NONE)
    Chem.WedgeMolBonds(mol,mol.GetConformer())
    scale=float(root.get('BondLength','30'))/1.5;conf=mol.GetConformer()
    positions={node_id:(conf.GetAtomPosition(i).x*scale,-conf.GetAtomPosition(i).y*scale)for node_id,i in id_to_index.items()}
    for node_id,children in aliases.items():
        anchors=[anchor for (parent,_),anchor in ports.items()if parent==node_id and anchor in positions]
        members=anchors or children
        if members:positions[node_id]=tuple(sum(positions[i][axis]for i in members)/len(members)for axis in (0,1))
    for node_id,point in positions.items():
        if node_id in nodes:nodes[node_id].set('p',f'{point[0]:.10f} {point[1]:.10f}');nodes[node_id].attrib.pop('xyz',None)
    directions={frozenset((a.GetIntProp('_STUDIO_CDX_ID'),b.GetIntProp('_STUDIO_CDX_ID'))):(a.GetIntProp('_STUDIO_CDX_ID'),bond.GetBondDir())for bond in mol.GetBonds()for a,b in [(bond.GetBeginAtom(),bond.GetEndAtom())]}
    for bond in root.iter('b'):
        start,end=int(bond.get('B')),int(bond.get('E'));bond_id=int(bond.get('id'))
        a=ports.get((start,bond_id),start);b=ports.get((end,bond_id),end)
        direction=directions.get(frozenset((a,b)))
        if direction and direction[1] in (Chem.BondDir.BEGINWEDGE,Chem.BondDir.BEGINDASH):
            if direction[0]!=a:bond.set('B',str(end));bond.set('E',str(start))
            bond.set('Display','WedgeBegin' if direction[1]==Chem.BondDir.BEGINWEDGE else 'WedgedHashBegin')
        elif bond.get('Display','') in ('WedgeBegin','WedgeEnd','WedgedHashBegin','WedgedHashEnd','HashBegin','HashEnd'):bond.attrib.pop('Display',None)
    output=ET.tostring(root,encoding='unicode')
    if identity(one_molecule(output,allow_disconnected=True,allow_queries=True))!=before:raise ValueError('RDKit 整理未通过连接关系和立体化学校验，已保留原结构')
    return output


def _segment_distance(point, start, end):
    ax, ay = start; bx, by = end; px, py = point
    dx, dy = bx - ax, by - ay
    length = dx * dx + dy * dy
    if length <= 1e-12: return math.hypot(px - ax, py - ay)
    t = max(0., min(1., ((px - ax) * dx + (py - ay) * dy) / length))
    return math.hypot(px - (ax + t * dx), py - (ay + t * dy))


# Label boxes are conservative: the drawn "(R)" is narrower than this.
LABEL_HALF_WIDTH = 9.0
LABEL_HALF_HEIGHT = 5.4
ATOM_CLEARANCE = 6.5
BOND_CLEARANCE = 5.5


def _label_clearance(point, atoms, segments, boxes, own_atom=None):
    """Smallest gap between the label box and any atom, bond or earlier label."""
    score = 1e9
    for node_id, xy in atoms.items():
        if node_id == own_atom: continue
        score = min(score, math.hypot(point[0] - xy[0], point[1] - xy[1]) - ATOM_CLEARANCE)
    for segment in segments:
        score = min(score, _segment_distance(point, segment[0], segment[1]) - BOND_CLEARANCE)
    for other in boxes:
        score = min(score, max(abs(point[0] - other[0]) - 2 * LABEL_HALF_WIDTH,
                               abs(point[1] - other[1]) - 2 * LABEL_HALF_HEIGHT))
    return score


def _place_label(direction, centre, reach, atoms, segments, boxes, own_atom=None, outward_only=False):
    """Pick the clearest direction around a centre, preferring the ring-outside one.

    Ring atoms and ring double bonds pass outward_only so the label can never
    slide back over the ring interior just because that spot is emptier."""
    if abs(direction[0]) < 1e-9 and abs(direction[1]) < 1e-9: direction = (1.0, 0.0)
    base = math.atan2(direction[1], direction[0])
    best = None
    for step in range(12):
        swing = step if step <= 6 else step - 12
        if outward_only and abs(swing) > 3: continue
        angle = base + step * math.pi / 6
        point = (centre[0] + reach * math.cos(angle), centre[1] + reach * math.sin(angle))
        score = _label_clearance(point, atoms, segments, boxes, own_atom) + 4.0 * math.cos(step * math.pi / 6)
        if best is None or score > best[0]: best = (score, point)
    return best[1]


def stereo_labels(source):
    """CIP labels computed by RDKit and placed beside each centre in document units."""
    root = ET.fromstring(source)
    mol = one_molecule(source)
    Chem.AssignStereochemistry(mol, cleanIt=True, force=True)
    points = {}
    for node in root.iter('n'):
        if node.get('id') and node.get('p'):
            x, y = node.get('p').split()[:2]
            points[int(node.get('id'))] = (float(x), float(y))
    by_index = {}
    for atom in mol.GetAtoms():
        if not atom.HasProp('_STUDIO_CDX_ID'): raise ValueError('开源立体化学服务无法建立完整原子映射')
        by_index[atom.GetIdx()] = atom.GetIntProp('_STUDIO_CDX_ID')
    if len(by_index) != mol.GetNumAtoms() or any(node_id not in points for node_id in by_index.values()):
        raise ValueError('开源立体化学服务无法建立完整原子映射')
    bond_ids = {}
    for bond in root.iter('b'):
        if bond.get('B') and bond.get('E') and bond.get('id'):
            bond_ids[frozenset((int(bond.get('B')), int(bond.get('E'))))] = int(bond.get('id'))
    rings = [tuple(by_index[i] for i in ring) for ring in mol.GetRingInfo().AtomRings()]
    ring_centroids = [(sum(points[i][0] for i in ring) / len(ring), sum(points[i][1] for i in ring) / len(ring)) for ring in rings]
    atoms = {by_index[i]: points[by_index[i]] for i in range(mol.GetNumAtoms())}
    segments = [(atoms[by_index[b.GetBeginAtomIdx()]], atoms[by_index[b.GetEndAtomIdx()]]) for b in mol.GetBonds()]
    lengths = sorted(math.hypot(s[1][0] - s[0][0], s[1][1] - s[0][1]) for s in segments)
    bond_length = lengths[len(lengths) // 2] if lengths else float(root.get('BondLength', '30'))
    boxes = []
    records = ['stereolabels\t1']
    skipped = 0
    for index, label in Chem.FindMolChiralCenters(mol, includeUnassigned=True, useLegacyImplementation=False):
        if label not in ('R', 'S'):
            skipped += 1; continue
        node_id = by_index[index]
        own_rings = [c for c, ring in zip(ring_centroids, rings) if node_id in ring]
        origin = points[node_id]
        # Outward from the ring for ring atoms; away from the substituents otherwise.
        if own_rings:
            middle = (sum(c[0] for c in own_rings) / len(own_rings), sum(c[1] for c in own_rings) / len(own_rings))
            direction = (origin[0] - middle[0], origin[1] - middle[1])
        else:
            neighbours = [points[by_index[n.GetIdx()]] for n in mol.GetAtomWithIdx(index).GetNeighbors()]
            if neighbours:
                direction = (origin[0] - sum(p[0] for p in neighbours) / len(neighbours), origin[1] - sum(p[1] for p in neighbours) / len(neighbours))
            else: direction = (1.0, 0.0)
        point = _place_label(direction, points[node_id], bond_length * .9, atoms, segments, boxes, node_id, outward_only=bool(own_rings))
        boxes.append(point)
        records.append('atom\t%d\t%s\t%.4f\t%.4f' % (node_id, label, point[0], point[1]))
    for info in Chem.FindPotentialStereo(mol):
        if info.type != Chem.StereoType.Bond_Double: continue
        if not info.specified:
            skipped += 1; continue
        bond = mol.GetBondWithIdx(info.centeredOn)
        # StereoDescriptor (Bond_Trans / Bond_Cis) is not the BondStereo enum.
        label = 'E' if str(info.descriptor).endswith('Bond_Trans') else 'Z'
        start, end = by_index[bond.GetBeginAtomIdx()], by_index[bond.GetEndAtomIdx()]
        bond_id = bond_ids.get(frozenset((start, end)))
        if bond_id is None: continue
        (ax, ay), (bx, by) = points[start], points[end]
        middle = ((ax + bx) / 2., (ay + by) / 2.)
        own_rings = [c for c, ring in zip(ring_centroids, rings) if start in ring and end in ring]
        if own_rings:
            direction = (middle[0] - sum(c[0] for c in own_rings) / len(own_rings), middle[1] - sum(c[1] for c in own_rings) / len(own_rings))
        else:
            direction = (by - ay, ax - bx)
        point = _place_label(direction, middle, bond_length * .5, atoms, segments, boxes, outward_only=bool(own_rings))
        boxes.append(point)
        records.append('bond\t%d\t%s\t%.4f\t%.4f' % (bond_id, label, point[0], point[1]))
    records.append('skipped\t%d' % skipped)
    return '\n'.join(records) + '\n'

def convert_molecule(mol, out_mime):
    if out_mime == 'chemical/x-daylight-smiles':
        return Chem.MolToSmiles(mol, isomericSmiles=True)
    if out_mime == 'chemical/x-helm':
        text=Chem.MolToHELM(mol)
        if not text: raise ValueError('该结构无法导出为 HELM')
        return text
    if out_mime.startswith('chemical/x-mdl-molfile'):
        return Chem.MolToMolBlock(mol, forceV3000=out_mime.endswith('v3000'))
    if out_mime == 'text/xml':
        return to_cdxml(mol)
    raise ValueError('不支持的本地化学格式转换')


def run(source, in_mime, out_mime, action):
    # A chemical name is not CDXML: resolve it before every XML path. This used
    # to be checked after the generic "convert" branch, so a name was parsed as
    # CDXML and returned "syntax error" instead of a structure.
    if in_mime == 'chemical/x-name': return convert_molecule(name_molecule(source), out_mime)
    if action=='clean':return clean_structure(source)
    # Stereo annotation reads the drawn wedges/coordinates and returns R/S/E/Z
    # labels with the positions the app draws them at (document units).
    if action=='stereolabels':return stereo_labels(source)
    if action=='expand':return expand_display(source)
    if action=='convert':
        # Structure -> systematic name is a convert request too. It used to be
        # checked after this branch, which made "structureToName" fail with
        # "unsupported local chemical format conversion".
        if out_mime == 'chemical/x-name': return systematic_name(one_molecule(source, in_mime))
        mol = one_molecule(source, in_mime, allow_disconnected=True, allow_queries=True)
        return convert_molecule(mol, out_mime)
    if action.split(';',1)[0] in ('nmr1h','nmr13c','nmr19f'):
        parts=action.split(';'); solvent='cdcl3'
        if len(parts)>2 or (len(parts)==2 and not parts[1].startswith('solvent=')):
            raise ValueError('无效的核磁预测条件')
        if len(parts)==2:
            solvent=parts[1][len('solvent='):]
            if solvent in ('', 'unknown'): solvent='cdcl3'
        mol=one_molecule(source,in_mime)
        return predict_ml_nmr(mol,{'nmr1h':'1H','nmr13c':'13C','nmr19f':'19F'}[parts[0]],solvent)
    mol = one_molecule(source, in_mime, allow_disconnected=out_mime == 'chemical/x-name')
    if action == 'nmr1h': return predict_hnmr(mol)
    if action == 'nmr13c': return predict_cnmr(mol)
    if action == 'nmr19f': return predict_fnmr(mol)
    raise ValueError('不支持的本地化学操作')


if __name__ == '__main__':
    try:
        text = run(Path(sys.argv[1]).read_text(encoding='utf-8-sig'), *sys.argv[3:6])
        code = 0
    except subprocess.TimeoutExpired:
        text, code = '开源化学计算超时，请缩小选区或简化结构', 2
    except Exception as exc:
        text, code = str(exc), 2
    Path(sys.argv[2]).write_text(text, encoding='utf-8')
    sys.exit(code)
