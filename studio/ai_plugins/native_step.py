"""Ask the desktop mechanism engine for the products of one staged arrow set."""
from collections import Counter
from pathlib import Path
import json
import os
import subprocess
import tempfile
import xml.etree.ElementTree as ET


def native_products(engine, specs, moves):
    from chemistry_open import Chem, one_molecule
    from ai_plugins.draw_plugin import _row_electrons

    if not moves or not all(s.lower().startswith('smiles:') for s in specs):
        raise ValueError('按箭头推演需要 SMILES 反应物和至少一支电子箭头。')
    runtime = Path(__file__).resolve().parents[1]
    manifest = runtime/'manifest.json'
    if manifest.is_file():
        name = Path(json.loads(manifest.read_text(encoding='utf-8'))['exe']).name
        host = runtime/name
    else:
        host = runtime/'MoleculeStudio.exe'
    if not host.is_file() and (runtime/'build.py').is_file():
        host = Path(os.environ.get('MOLECULE_STUDIO_TEST_EXE',''))
    if not host.is_file() or host.suffix.lower() != '.exe':
        raise ValueError('未找到配套桌面引擎；源码测试可设置 MOLECULE_STUDIO_TEST_EXE。')
    preview = type(engine)({**engine.context,'selected':0})
    items = []
    x = 0.
    for spec in specs:
        item = preview.call('draw_smiles', {'smiles':spec[7:], 'keep_explicit_hydrogens':'[H]' in spec, 'x':x,'y':0})
        items.append({'ref':item['ref'],'label_ref':None})
        x = item['bounds'][2] + (item['bounds'][2]-item['bounds'][0])*.5 + 60
    arrow_refs = _row_electrons(preview, items, moves)
    root = ET.Element('result',ok='true')
    actions = ET.SubElement(root,'actions')
    actions.extend(preview.actions)
    step = ET.SubElement(actions,'mechanism_step',mode='arrows',keep_loss_structures='true')
    for ref in arrow_refs:
        ET.SubElement(step,'ref',id=ref)
    # This document exists only in the worker. Export the inferred structures
    # without inferring their identity from spatial position or native IDs.
    remove = ET.SubElement(actions,'delete_objects')
    for ref in [item['ref'] for item in items]+arrow_refs:
        ET.SubElement(remove,'ref',id=ref)
    with tempfile.TemporaryDirectory(prefix='MoleculeStudio-step-') as folder:
        plan, output = (Path(folder)/name for name in ('plan.xml','products.cdxml'))
        ET.ElementTree(root).write(plan,encoding='utf-8')
        try:
            proc = subprocess.run([str(host),'--ai-apply','-',str(plan),str(output)],
                                  capture_output=True,timeout=45,creationflags=subprocess.CREATE_NO_WINDOW)
        except (OSError,subprocess.TimeoutExpired) as exc:
            raise ValueError('桌面推演未完成，请检查本步骤或稍后重试。') from exc
        if proc.returncode or not output.is_file():
            detail = proc.stderr.decode('utf-8','replace').strip()[:300]
            raise ValueError('电子移动未通过桌面推演：'+(detail or '请核对电子来源、受体和离去键。'))
        doc = ET.parse(output).getroot()
    # The editor can serialize ungrouped product atoms directly under page.
    # Normalize the molecular container for RDKit, keeping all chemical attributes.
    page = doc.find('page')
    if page is None or not list(page.iter('n')):
        raise ValueError('推演未返回可引用的产物。')
    flat = [e for e in page if e.tag in ('n','b')]
    if flat:
        group = ET.SubElement(page,'fragment')
        for e in flat:
            page.remove(e)
            group.append(e)
    mol = one_molecule(ET.tostring(doc,encoding='unicode'),allow_disconnected=True,max_atoms=400)
    # Native generation copies connected components touched by an arrow. Retain
    # unselected counterions when turning its output into the next row's inputs.
    reactants, spectators = [], []
    touched = set()
    for move in moves:
        for key in ('source', 'target'):
            group, site = move[key].split(':')
            touched.update((int(group), int(atom)) for atom in site.split('-'))
    parser = Chem.SmilesParserParams()
    parser.removeHs = False
    for index, spec in enumerate(specs, 1):
        reactant = Chem.MolFromSmiles(spec[7:], parser)
        reactants.append(reactant)
        mappings = []
        components = Chem.GetMolFrags(reactant, asMols=True, fragsMolAtomMapping=mappings)
        for component, indices in zip(components, mappings):
            if not any((index, atom + 1) in touched for atom in indices):
                spectators.append(Chem.RemoveHs(component))

    def inventory(molecules):
        elements = Counter()
        charge = 0
        for molecule in molecules:
            charge += Chem.GetFormalCharge(molecule)
            elements.update((a.GetAtomicNum(), a.GetIsotope()) for a in Chem.AddHs(molecule).GetAtoms())
        return elements, charge

    products = list(Chem.GetMolFrags(mol, asMols=True))
    expected = inventory(reactants)
    if inventory(products) != expected:
        products.extend(spectators)
        if inventory(products) != expected:
            raise ValueError('推演前后的原子、同位素或总电荷不守恒。请检查本步是否补全反应伙伴及成键、断键的箭头。')

    def order(fragment):
        return (-fragment.GetNumHeavyAtoms(),Chem.MolToSmiles(fragment))
    return ['smiles:'+Chem.MolToSmiles(m) for m in sorted(products,key=order)]


def local_source_hint(engine, specs, moves, declared):
    """Propose a lone-pair source only after the native graph matches the prediction."""
    from chemistry_open import Chem
    parser = Chem.SmilesParserParams()
    parser.removeHs = False
    import re
    checked = 0
    for index,move in enumerate(moves):
        if move.get('electrons',2)!=2:continue
        if any(not re.fullmatch(r'[1-9][0-9]*:[1-9][0-9]*-[1-9][0-9]*',move.get(k,'')) for k in ('source','target')):continue
        group, source = move['source'].split(':')
        side, target = move['target'].split(':')
        if group!=side or int(side)>len(specs) or not specs[int(side)-1].lower().startswith('smiles:'):continue
        giver, receiver = set(source.split('-')), set(target.split('-'))
        if len(receiver-giver)!=1 or len(giver & receiver)!=1:continue
        other = next(iter(receiver-giver))
        mol = Chem.MolFromSmiles(specs[int(side)-1][7:],parser)
        if mol is None or int(other)>mol.GetNumAtoms():continue
        atom = mol.GetAtomWithIdx(int(other)-1)
        if atom.GetAtomicNum() not in (7,8,15,16) or atom.GetFormalCharge()>0:continue
        trial = [dict(m) for m in moves]
        trial[index]['source'] = side+':'+other
        checked += 1
        try:
            inferred = native_products(engine,specs,trial)
            verify_products(inferred,declared)
        except ValueError:
            if checked>=2:break
            continue
        return f"桌面已核验：第 {index+1} 支箭头的 source 改为 {side}:{other}（该原子的孤对）后，推演与声明的产物相符。请核对并重新提交。"
    return ''

def fragment_counts(specs):
    from chemistry_open import Chem
    parts = Counter()
    for spec in specs:
        if not spec.lower().startswith('smiles:'):
            raise ValueError('验证产物时请使用 SMILES。')
        mol = Chem.MolFromSmiles(spec[7:])
        if mol is None:
            raise ValueError('产物 SMILES 无法解析。')
        for atom in mol.GetAtoms():
            atom.SetAtomMapNum(0)
        parts.update(Chem.MolToSmiles(mol).split('.'))
    return parts


def verify_products(inferred, declared):
    actual, expected = fragment_counts(inferred), fragment_counts(declared)
    if expected-actual:
        raise ValueError('声明的产物与箭头推演不符。实际得到 '+', '.join(inferred)+'；请核对实际成断键、立体信息和电荷并修正箭头或声明的结构。')
    return ["smiles:"+s for s in (actual-expected).elements()]


def product_sites(spec, full=False):
    """Indices follow the returned SMILES, including its explicit hydrogen atoms."""
    from chemistry_open import Chem
    parser = Chem.SmilesParserParams()
    parser.removeHs = False
    mol = Chem.MolFromSmiles(spec[7:],parser)
    if mol is None:
        raise ValueError('无法读取反应位点，请检查 SMILES：'+spec[:160])
    if not full and mol.GetNumAtoms()>80:
        return {'atom_count':mol.GetNumAtoms(), 'site_list_truncated':True}
    graph = {'atoms':[(a.GetIdx()+1,a.GetSymbol(),a.GetFormalCharge()) for a in mol.GetAtoms()],
            'bonds':[(b.GetBeginAtomIdx()+1,b.GetEndAtomIdx()+1,b.GetBondTypeAsDouble()) for b in mol.GetBonds()]}
    if full:
        graph['hydrogens'] = {a.GetIdx()+1:a.GetTotalNumHs(includeNeighbors=True) for a in mol.GetAtoms()
                              if a.GetAtomicNum()!=1 and a.GetTotalNumHs(includeNeighbors=True)}
        graph['radical_electrons'] = {a.GetIdx()+1:a.GetNumRadicalElectrons() for a in mol.GetAtoms() if a.GetNumRadicalElectrons()}
        graph['rings'] = [[i+1 for i in ring] for ring in mol.GetRingInfo().AtomRings()]
    return graph
