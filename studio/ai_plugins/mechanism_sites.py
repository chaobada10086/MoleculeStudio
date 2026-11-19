"""Resolve atom-map labels before inserting reactants into an editable drawing."""
import re

MAP_SUFFIX = re.compile(r':\d+(?=\])')



def mapped_step(reaction):
    """Compile an elementary mapped transformation into bound electron-pair moves."""
    from collections import Counter
    from chemistry_open import Chem
    if reaction.count(">>") != 1:
        raise ValueError("transformation 应写为反应物>>产物，反应伙伴用点分开。")
    parser = Chem.SmilesParserParams()
    parser.removeHs = False

    def read(side):
        specs = side.split(".")
        if not 1 <= len(specs) <= 8:
            raise ValueError("每侧支持 1–8 个结构。")
        atoms, bonds = {}, {}
        for group, text in enumerate(specs, 1):
            mol = Chem.MolFromSmiles(text, parser)
            if mol is None or not mol.GetNumAtoms():
                raise ValueError("映射反应式中存在无效结构。")
            for atom in mol.GetAtoms():
                label = atom.GetAtomMapNum()
                if not label:
                    continue
                if label in atoms or atom.GetNumRadicalElectrons():
                    raise ValueError("映射编号必须唯一；单电子步骤请提供 electron_moves。")
                atoms[label] = (group, atom)
            for bond in mol.GetBonds():
                ends = tuple(sorted((bond.GetBeginAtom().GetAtomMapNum(), bond.GetEndAtom().GetAtomMapNum())))
                if ends[0]:
                    bonds[ends] = bond.GetBondTypeAsDouble()
        return ["smiles:"+s for s in specs], atoms, bonds

    left, before, old = read(reaction.split(">>")[0])
    right, after, new = read(reaction.split(">>")[1])
    if not before or before.keys() != after.keys():
        raise ValueError("每个反应中心及离去基团的映射编号须在两侧各出现一次。")
    # Materialize one unambiguous H transfer so the native arrow lands on a
    # real hydrogen rather than its heavy-atom label.
    hydrogen_changes = {key:after[key][1].GetTotalNumHs()-a.GetTotalNumHs()
                        for key, (_, a) in before.items()}
    donors = [k for k,v in hydrogen_changes.items() if v < 0]
    acceptors = [k for k,v in hydrogen_changes.items() if v > 0]
    if len(donors)==len(acceptors)==1 and hydrogen_changes[donors[0]]==-1 and hydrogen_changes[acceptors[0]]==1:
        hydrogen_map = max(before)+1
        def materialize(specs, side, key):
            group, atom = side[key]
            mol = Chem.RWMol(Chem.MolFromSmiles(specs[group-1][7:], parser))
            donor = mol.GetAtomWithIdx(atom.GetIdx())
            donor.SetNumExplicitHs(donor.GetTotalNumHs()-1)
            donor.SetNoImplicit(True)
            hydrogen = Chem.Atom(1)
            hydrogen.SetAtomMapNum(hydrogen_map)
            mol.AddBond(donor.GetIdx(), mol.AddAtom(hydrogen), Chem.BondType.SINGLE)
            mol.UpdatePropertyCache()
            specs[group-1] = "smiles:"+Chem.MolToSmiles(mol, canonical=False)
            return read(".".join(spec[7:] for spec in specs))
        left,before,old = materialize(left,before,donors[0])
        right,after,new = materialize(right,after,acceptors[0])
    delta = Counter()
    for edge in old.keys() | new.keys():
        change = new.get(edge, 0) - old.get(edge, 0)
        if int(change) != change:
            raise ValueError("芳香键去芳构化请用明确的基元步骤与 electron_moves。")
        delta[edge] = int(change)
    for label, (_, a) in before.items():
        b = after[label][1]
        if (a.GetAtomicNum(), a.GetIsotope()) != (b.GetAtomicNum(), b.GetIsotope()):
            raise ValueError("同一编号指向了不同元素或同位素。")
        if a.GetTotalNumHs() != b.GetTotalNumHs():
            raise ValueError("转移的氢请在两侧写成同号的独立 [H:n]，供体和受体上相应少写一个 H。")
        diff = a.GetFormalCharge() + a.GetTotalValence() - b.GetFormalCharge() - b.GetTotalValence()
        if diff % 2:
            raise ValueError("电子变化不能分解为电子对，请检查映射或提供 electron_moves。")
        delta[(label,)] = diff // 2
    sources = list((-delta).elements())
    sinks = list((+delta).elements())
    if not sources or len(sources) != len(sinks) or len(sources) > 8:
        raise ValueError("键级与电子对变化不平衡，请标全发生变化的原子和副产物，或直接填写 electron_moves。")
    # An electron pair can move only between sites sharing an atom. Reject
    # alternate pairings, since identical net products can have different mechanisms.
    solutions = set()
    def assign(i, remaining, pairs):
        if len(solutions) > 1:
            return
        if i == len(sources):
            solutions.add(tuple(sorted(pairs)))
            return
        src = sources[i]
        for dst in sorted(set(remaining)):
            if not set(src).intersection(dst):
                continue
            rest = remaining.copy()
            rest.remove(dst)
            assign(i+1, rest, pairs+[(src, dst)])
    assign(0, sinks, [])
    if len(solutions) != 1:
        raise ValueError("映射结构不能唯一确定电子路径，请为这一步填写 electron_moves。")
    def endpoint(site):
        return str(before[site[0]][0])+":"+"-".join("@"+str(i) for i in site)
    moves = []
    for src, dst in solutions.pop():
        target = dst
        if len(dst) == 2 and (len(src) == 1 or dst not in old):
            target = tuple(k for k in dst if k not in src)
        moves.append({"source":endpoint(src), "target":endpoint(target)})
    return {"reactants":left,"products":right,"electron_moves":moves,"verify_products":True}


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
