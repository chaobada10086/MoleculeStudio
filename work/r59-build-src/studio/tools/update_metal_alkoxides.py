"""Authored connected alkoxide drawings and single-attachment abbreviations.

Only these entries are updated; existing template IDs and other coordinates stay.
"""
from pathlib import Path
import json, math, sys, xml.etree.ElementTree as ET
from rdkit import Chem
from rdkit.Chem import rdMolDescriptors

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/"tools"))
FAMILIES = [
    ('Al', 13, 3, 'Me', '甲醇铝'), ('Al', 13, 3, 'Et', '乙醇铝'),
    ('Al', 13, 3, 'i-Pr', '异丙醇铝'), ('Al', 13, 3, 't-Bu', '叔丁醇铝'),
    ('Ti', 22, 4, 'Et', '四乙氧基钛'), ('Ti', 22, 4, 'i-Pr', '四异丙氧基钛'),
    ('Ti', 22, 4, 't-Bu', '四叔丁氧基钛'), ('Zr', 40, 4, 'i-Pr', '四异丙氧基锆'),
    ('Zr', 40, 4, 't-Bu', '四叔丁氧基锆'), ('B', 5, 3, 'i-Pr', '硼酸三异丙酯'),
]
SMILES = {'Me': 'OC', 'Et': 'OCC', 'i-Pr': 'OC(C)C', 't-Bu': 'OC(C)(C)C'}

def key(value):
    return value.replace('-', '').lower()

def drawing(symbol, element, count, alkyl, partial=False):
    fragment = ET.Element('fragment', id='2')
    ids, bonds = [], []
    def atom(z, point, h=0, dummy=False):
        ident = str(10 + len(ids)); ids.append(ident)
        attrs = dict(id=ident, p=f'{point[0]:.9f} {point[1]:.9f}')
        if dummy: attrs['NodeType'] = 'ExternalConnectionPoint'
        else: attrs.update(Element=str(z), NumHydrogens=str(h))
        ET.SubElement(fragment, 'n', attrs)
        return ident
    def bond(a, b):
        bonds.append((a, b))
    def offset(point, angle):
        return (point[0]+22*math.cos(math.radians(angle)), point[1]+22*math.sin(math.radians(angle)))
    root = atom(element, (0, 0))
    for arm in range(count):
        angle = -90 + arm*360/count
        if partial and arm == 0:
            bond(atom(0, offset((0, 0), angle), dummy=True), root)
            continue
        op = offset((0, 0), angle); oxygen = atom(8, op); bond(root, oxygen)
        cp = offset(op, angle+60)
        hydrogen = {'Me': 3, 'Et': 2, 'i-Pr': 1, 't-Bu': 0}[alkyl]
        carbon = atom(6, cp, hydrogen); bond(oxygen, carbon)
        directions = {'Me': [], 'Et': [angle], 'i-Pr': [angle, angle+120],
                      't-Bu': [angle-30, angle+60, angle+150]}[alkyl]
        for direction in directions: bond(carbon, atom(6, offset(cp, direction), 3))
    for index, (a, b) in enumerate(bonds):
        ET.SubElement(fragment, 'b', id=str(1000+index), B=a, E=b, Order='1')
    return fragment

def main():
    source_path = ROOT/'reagents_extra.json'
    extras = json.loads(source_path.read_text(encoding='utf-8'))
    catalogue_path = ROOT/'reagents.json'
    catalogue = json.loads(catalogue_path.read_text(encoding='utf-8'))
    records = catalogue['reagents']
    additions, partials, partial_sources = [], {}, {}
    for symbol, element, count, alkyl, title in FAMILIES:
        name = f'{symbol}(O{alkyl}){count}'
        plain = name.replace('-', '')
        smiles = f'[{symbol}]' + ''.join(f'({SMILES[alkyl]})' for _ in range(count-1)) + SMILES[alkyl]
        mol = Chem.MolFromSmiles(smiles)
        if mol is None: raise ValueError(name)
        formula = rdMolDescriptors.CalcMolFormula(mol)
        old = next((r for r in records if any(key(a)==key(name) for a in r['alias'].split())), None)
        aliases = list(dict.fromkeys([name, plain, title] + (old['alias'].split() if old else [])))
        source = dict(title=title, alias=' '.join(aliases), category='金属醇盐',
                      smiles=smiles, formula=formula, inorganic=False,
                      source='Project-authored connected alkoxide monomer depiction')
        match = next((r for r in extras if any(key(a)==key(name) for a in r['alias'].split())), None)
        if match: match.update(source)
        else: additions.append(source)
        if old:
            entry = old; entry.update({k: source[k] for k in ('title', 'alias', 'category', 'formula', 'inorganic')})
        else:
            entry = {k: source[k] for k in ('title', 'alias', 'category', 'formula', 'inorganic')}
            entry['file'] = f'reagent-{len(records):03d}.cdxml'; records.append(entry)
        for key_name in ('full_file', 'compact'):
            entry.pop(key_name, None)
        document = ET.Element('CDXML', BondLength='22', LabelSize='10', LineWidth='0.6')
        page = ET.SubElement(document, 'page', id='1'); page.append(drawing(symbol, element, count, alkyl))
        ET.ElementTree(document).write(ROOT/'reagent_templates'/entry['file'], encoding='utf-8', xml_declaration=True)
        # Every family also supports the metal with one existing external ligand.
        group = f'{symbol}(O{alkyl}){count-1}'
        fragment = drawing(symbol, element, count, alkyl, partial=True)
        partial_smiles=f'[*][{symbol}]'+''.join(f'({SMILES[alkyl]})' for _ in range(count-2))+SMILES[alkyl]
        formula_mol = Chem.MolFromSmiles(partial_smiles)
        partial_sources[group]=dict(smiles=partial_smiles,confidence='high',note='Authored metal-centred alkoxide attachment group')
        group_formula = rdMolDescriptors.CalcMolFormula(formula_mol).replace('*', '')
        partials[group] = (ET.tostring(fragment, encoding='unicode'), group_formula)
    source_path.write_text(json.dumps(extras, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    (ROOT/'metal_alkoxides.json').write_text(json.dumps(additions, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    catalogue_path.write_text(json.dumps(catalogue, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    lines = ['#pragma once', 'struct ReagentEntry {const char* title;const char* alias;const char* category;const char* file;bool inorganic;};',
             'static const std::vector<ReagentEntry>& reagentEntries(){static const std::vector<ReagentEntry> entries={']
    for r in records:
        lines.append('    {'+','.join(json.dumps(r[k], ensure_ascii=False) for k in ('title','alias','category','file'))+','+str(r['inorganic']).lower()+'},')
    (ROOT/'reagent_catalog.hpp').write_text('\n'.join(lines+['};return entries;}'])+'\n', encoding='utf-8')
    lines = ['// Generated by tools/update_metal_alkoxides.py; metal-centred attachment groups.']
    backend = json.loads((ROOT/'nicknames.json').read_text(encoding='utf-8'))
    for name, (fragment, formula) in sorted(partials.items()):
        lines.append('{'+json.dumps(name)+',R"NICK('+fragment+')NICK",'+json.dumps(formula)+'},')
        backend[name] = fragment
    (ROOT/'metal_alkoxide_groups.json').write_text(json.dumps({name:dict(fragment=value[0],formula=value[1],**partial_sources[name]) for name,value in partials.items()},ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    (ROOT/'metal_alkoxide_groups.inc').write_text('\n'.join(lines)+'\n', encoding='utf-8')
    (ROOT/'nicknames.json').write_text(json.dumps(backend, ensure_ascii=False, indent=0, sort_keys=True)+'\n', encoding='utf-8')
    # Keep both direct regeneration paths aware of the authored additions.
    generator = ROOT/'tools/generate_reagents.py'
    text = generator.read_text(encoding='utf-8')
    if "metal_alkoxides.json" not in text:
        text = text.replace('    return result\n\n\ndef prepare_molecule', "    alkoxides=ROOT/'metal_alkoxides.json'\n    if alkoxides.exists():result.extend(json.loads(alkoxides.read_text(encoding='utf-8')))\n    return result\n\n\ndef prepare_molecule", 1)
    generator.write_text(text, encoding='utf-8')
    for path in (ROOT/'nickname_catalog.hpp', ROOT/'tools/build_nickname_catalog.py'):
        text = path.read_text(encoding='utf-8')
        if path.suffix == '.hpp':
            if '#include "metal_alkoxide_groups.inc"' not in text:
                text = text.replace('static const Entry nicknames[]={', 'static const Entry nicknames[]={\n#include "metal_alkoxide_groups.inc"', 1)
            import re
            text = re.sub(r'static const unsigned count=\d+;', 'static const unsigned count=sizeof(nicknames)/sizeof(nicknames[0]);', text)
            text = text.replace('if(n==v.name)return &v;', 'if(n==v.name)return &v;', 1)
        else:
            text = text.replace("'static const Entry nicknames[]={']", "'static const Entry nicknames[]={', '#include \\\"metal_alkoxide_groups.inc\\\"']")
            text = text.replace("'static const unsigned count=%d;' % len(entries)", "'static const unsigned count=sizeof(nicknames)/sizeof(nicknames[0]);'")
        path.write_text(text, encoding='utf-8')
    from build_compound_catalog import build
    build()
    print(f'Updated {len(FAMILIES)} alkoxide templates and {len(partials)} attachment groups')

if __name__ == '__main__':
    main()
