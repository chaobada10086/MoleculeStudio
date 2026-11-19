"""Four formal rows for the carboxylic-acid / PPh3 / DEAD substitution pathway."""
from ai_plugins.mechanism_sites import MAP_SUFFIX


def scheme(args):
    from chemistry_open import Chem

    phenyls = '(c1ccccc1)(c1ccccc1)c1ccccc1'
    phosphine = '[P:3]' + phenyls
    azo = 'CCOC(=O)[N:1]=[N:2]C(=O)OCC'
    betaine = 'CCOC(=O)[N:1]([P+:3]' + phenyls + ')[N-:2]C(=O)OCC'
    protonated = betaine.replace('[N-:2]', '[NH:2]')

    def substrate(key, pattern):
        raw = args[key].strip().removeprefix('smiles:')
        mol = Chem.MolFromSmiles(raw) if 0 < len(raw) <= 320 else None
        if mol is None or len(Chem.GetMolFrags(mol)) != 1:
            raise ValueError(f'{key} 需要单个有效分子的 SMILES')
        for atom in mol.GetAtoms():
            atom.SetAtomMapNum(0)
        sites = mol.GetSubstructMatches(Chem.MolFromSmarts(pattern))
        if len(sites) != 1:
            raise ValueError(f'{key} 需要唯一的适用位点；多位点或其他底物请用 draw_scheme 指定位点')
        return mol, sites[0]

    alcohol, (c, o) = substrate('alcohol', '[C;X4;H1,H2,H3;+0]-[OH1;+0]')
    acid, (acid_o,) = substrate('acid','[O;H1;+0;$([O]-[C](=[O])-[#6]),$([O]-[CH]=[O])]')
    # Exclude hemiacetals and other heteroatom-substituted carbinol centers.
    if any(n.GetAtomicNum()!=6 for n in alcohol.GetAtomWithIdx(c).GetNeighbors() if n.GetIdx()!=o):
        raise ValueError('alcohol 需为甲醇、伯醇或仲醇')
    def explicit_oh(mol, oxygen, tag):
        copy = Chem.AddHs(mol,onlyOnAtoms=[oxygen])
        h = next(a for a in copy.GetAtomWithIdx(oxygen).GetNeighbors() if a.GetAtomicNum()==1)
        if h.GetIsotope():
            raise ValueError('羟基带氘或氚时请用 draw_scheme 显式追踪转移的同位素氢')
        h.SetAtomMapNum(tag)
        return Chem.MolToSmiles(copy)

    alcohol.GetAtomWithIdx(c).SetAtomMapNum(7)
    alcohol.GetAtomWithIdx(o).SetAtomMapNum(6)
    acid.GetAtomWithIdx(acid_o).SetAtomMapNum(4)
    acid_h = explicit_oh(acid, acid_o, 5)
    alcohol_h = explicit_oh(alcohol, o, 8)
    acid_atom = acid.GetAtomWithIdx(acid_o)
    acid_atom.SetNumExplicitHs(0)
    acid_atom.SetFormalCharge(-1)
    Chem.SanitizeMol(acid)
    carboxylate = Chem.MolToSmiles(acid)
    phos = Chem.MolFromSmiles(phosphine)
    p = alcohol.GetNumAtoms()
    activated = Chem.RWMol(Chem.CombineMols(alcohol,phos))
    activated.GetAtomWithIdx(o).SetNumExplicitHs(0)
    activated.GetAtomWithIdx(p).SetFormalCharge(1)
    activated.AddBond(o,p,Chem.BondType.SINGLE)
    Chem.SanitizeMol(activated)
    alkoxy = Chem.MolToSmiles(activated)

    ester = Chem.RWMol(Chem.CombineMols(alcohol,acid))
    incoming = alcohol.GetNumAtoms()+acid_o
    center = ester.GetAtomWithIdx(c)
    before = [n.GetIdx() for n in center.GetNeighbors()]
    ester.RemoveBond(c,o)
    ester.AddBond(c,incoming,Chem.BondType.SINGLE)
    ester.GetAtomWithIdx(incoming).SetFormalCharge(0)
    # RDKit tetrahedral tags depend on neighbour order. The entering neighbour
    # is now last; account for this permutation as well as physical inversion.
    if (len(before)-1-before.index(o)) % 2 == 0:
        center.InvertChirality()
    ester.RemoveAtom(o)
    Chem.SanitizeMol(ester)
    oxide = Chem.RWMol(phos)
    ox = Chem.Atom(8)
    ox.SetIsotope(alcohol.GetAtomWithIdx(o).GetIsotope())
    oxide.AddBond(0,oxide.AddAtom(ox),Chem.BondType.DOUBLE)
    Chem.SanitizeMol(oxide)
    def row(title, reactants, products, moves):
        return {'title':title,'reactants':['smiles:'+s for s in reactants],
                'products':['smiles:'+MAP_SUFFIX.sub('',s) for s in products],
                'electron_moves':[{'source':a,'target':b,'electrons':2} for a,b in moves]}
    rows = [
        row('1. 膦进攻偶氮氮，形成甜菜碱',[phosphine,azo],[betaine],
            [('1:@3','2:@1'),('2:@1-@2','2:@2')]),
        row('2. 羧酸向带负电的氮转移质子',[betaine,acid_h],[protonated,carboxylate],
            [('1:@2','2:@5'),('2:@4-@5','2:@4')]),
        row('3. 醇活化为烷氧基鏻盐（合并表示）',[alcohol_h,protonated],
            [alkoxy,'CCOC(=O)NNC(=O)OCC'],
            [('1:@6','2:@3'),('2:@3-@1','2:@1'),('2:@1','1:@8'),('1:@6-@8','1:@6')]),
        row('4. 羧酸根在醇碳上发生 SN2 取代',[carboxylate,alkoxy],
            [Chem.MolToSmiles(ester),Chem.MolToSmiles(oxide)],
            [('1:@4','2:@7'),('2:@7-@6','2:@6-@3')])]
    # The incoming carboxylate and the exposed substitution site face each other.
    rows[-1]['electron_moves'][0]['bend'] = .18
    for i,step in enumerate(rows):
        if 'titles' in args:
            step['title'] = args['titles'][i]
        if 'conditions_below' in args:
            step['conditions_below'] = args['conditions_below']
    return {'steps':rows, **{k:args[k] for k in ('x','y','row_gap') if k in args}}
