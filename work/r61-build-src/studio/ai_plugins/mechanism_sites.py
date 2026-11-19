"""Resolve atom-map labels before inserting reactants into an editable drawing."""
import re

MAP_SUFFIX = re.compile(r':\d+(?=\])')


def resolve_move_maps(specs, moves):
    if not any(MAP_SUFFIX.search(s) for s in specs) and not any(
            '@' in m[k] for m in moves for k in ('source','target')):
        return specs, moves
    from chemistry_open import Chem
    parser = Chem.SmilesParserParams()
    parser.removeHs = False
    clean, maps = [], []
    for spec in specs:
        labels = {}
        if spec.lower().startswith('smiles:') and MAP_SUFFIX.search(spec):
            mol = Chem.MolFromSmiles(spec[7:], parser)
            if mol is None:
                raise ValueError('带位点编号的 SMILES 无法解析')
            for atom in mol.GetAtoms():
                label = atom.GetAtomMapNum()
                if label:
                    if label in labels:
                        raise ValueError(f'同一反应物中位点 @{label} 重复')
                    labels[label] = atom.GetIdx()+1
            spec = MAP_SUFFIX.sub('',spec)
        clean.append(spec)
        maps.append(labels)

    def endpoint(address):
        if '@' not in address:
            return address
        match = re.fullmatch(r'([1-9]\d*):@([1-9]\d*)(?:-@([1-9]\d*))?',address)
        if not match:
            raise ValueError('标记端点请写为 1:@7 或 1:@7-@8')
        side, a, b = match.groups()
        if int(side)>len(maps):
            raise ValueError(f'端点 {address} 指向本步反应物以外')
        labels = maps[int(side)-1]
        try:
            return side+':'+str(labels[int(a)])+('-'+str(labels[int(b)]) if b else '')
        except KeyError as exc:
            raise ValueError(f'反应物 {side} 缺少端点标记 @{exc.args[0]}') from None
    return clean, [{**m,'source':endpoint(m['source']),'target':endpoint(m['target'])} for m in moves]
