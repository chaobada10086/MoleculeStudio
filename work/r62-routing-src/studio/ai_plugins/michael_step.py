"""Carbon Michael addition of an O-enolate to an alpha,beta-unsaturated carbonyl."""
def prepare_michael_step(args):
    from chemistry_open import Chem

    specs = args['reactants']
    if len(specs) != 2 or not all(isinstance(s, str) and s.lower().startswith('smiles:') for s in specs):
        raise ValueError('michael 需要两个 SMILES，依次为 O-烯醇盐和共轭羰基受体')
    if any(key in args for key in ('products', 'electron_moves', 'enolate')):
        raise ValueError('michael 从反应物生成四支电子箭头和加成后的烯醇盐，请省略 products、electron_moves、enolate')
    parser = Chem.SmilesParserParams()
    parser.removeHs = False
    donor, acceptor = (Chem.MolFromSmiles(s[7:], parser) for s in specs)
    if donor is None or acceptor is None:
        raise ValueError('Michael 加成反应物 SMILES 无法解析')
    site = args['michael']
    if not isinstance(site, dict) or set(site)-{'donor_alpha','acceptor_beta'} or any(type(n) != int or n<1 for n in site.values()):
        raise ValueError('michael 位点可用从 1 开始的 donor_alpha 和 acceptor_beta 指定')
    def match(mol, pattern, key):
        matches = [m for m in mol.GetSubstructMatches(Chem.MolFromSmarts(pattern)) if site.get(key,m[-1]+1)==m[-1]+1]
        if not matches:
            raise ValueError('未找到指定的 Michael 位点：' + key)
        ranks = Chem.CanonicalRankAtoms(mol, breakTies=False)
        if len({tuple(ranks[i] for i in m) for m in matches}) != 1:
            raise ValueError('有不等价位点，请指定 ' + key + '：' + '、'.join(str(m[-1]+1) for m in matches))
        return matches[0]
    o,c,a = match(donor,'[O-]-[C;+0]=[C;+0]','donor_alpha')
    z,k,u,b = match(acceptor,'[O;+0]=[C;+0]-[C;+0]=[C;+0]','acceptor_beta')
    product = Chem.RWMol(Chem.CombineMols(donor,acceptor))
    offset = donor.GetNumAtoms()
    single, double = Chem.BondType.SINGLE, Chem.BondType.DOUBLE
    for i,j,order in [(o,c,double),(c,a,single),(offset+z,offset+k,single),(offset+k,offset+u,double),(offset+u,offset+b,single)]:
        bond=product.GetBondBetweenAtoms(i,j)
        bond.SetBondType(order)
        bond.SetStereo(Chem.BondStereo.STEREONONE)
    product.AddBond(a,offset+b,single)
    product.GetAtomWithIdx(o).SetFormalCharge(0)
    product.GetAtomWithIdx(offset+z).SetFormalCharge(-1)
    Chem.SanitizeMol(product)
    Chem.AssignStereochemistry(product, cleanIt=True, force=True)
    def ref(side,*ids):
        return str(side)+':'+'-'.join(str(i+1) for i in ids)
    pairs=[(ref(1,o),ref(1,c,o)),(ref(1,c,a),ref(2,b)),(ref(2,b,u),ref(2,u,k)),(ref(2,k,z),ref(2,z))]
    return {**args,'products':['smiles:'+Chem.MolToSmiles(product)],
            'electron_moves':[{'source':start,'target':end} for start,end in pairs]}
