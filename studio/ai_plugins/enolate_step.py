"""Build one alkoxide deprotonation step from the molecular graph."""
import json


def prepare_enolate_step(args):
    from chemistry_open import Chem

    specs = args["reactants"]
    if len(specs) != 2 or not all(s.lower().startswith("smiles:") for s in specs):
        raise ValueError("enolate 需要两个 SMILES，依次为氢氧根或醇盐、羰基底物")
    if "electron_moves" in args or "products" in args:
        raise ValueError("enolate 自动生成电子移动和产物，请省略 electron_moves 和 products")
    parser = Chem.SmilesParserParams()
    parser.removeHs = False
    base, substrate = (Chem.MolFromSmiles(s[7:].strip(), parser) for s in specs)
    if base is None or substrate is None or len(Chem.GetMolFrags(substrate)) != 1:
        raise ValueError("请检查醇盐和羰基底物的 SMILES，底物需为单个分子")
    donors = base.GetSubstructMatches(Chem.MolFromSmarts("[O-;X1]-[C;X4]"))
    donors = [m[0] for m in donors] + [a.GetIdx() for a in base.GetAtoms()
              if a.GetAtomicNum()==8 and a.GetFormalCharge()==-1 and a.GetDegree()==0 and a.GetTotalNumHs()==1]
    if len(donors) != 1:
        raise ValueError("第一项需要一个氢氧根或醇盐氧负离子，如 smiles:[OH-] 或 smiles:CC[O-]")
    donor = donors[0]
    parts = Chem.GetMolFrags(base)
    counterions = [part for part in parts if donor not in part]
    if any(len(part) != 1 or base.GetAtomWithIdx(part[0]).GetAtomicNum() not in (3,11,19,37,55)
           or base.GetAtomWithIdx(part[0]).GetFormalCharge() != 1 for part in counterions) or len(counterions)>1:
        raise ValueError("醇盐可带一个碱金属反离子，其他试剂请单独列入反应条件")
    # An ester alkoxy carbon is separated from the carbonyl carbon by oxygen.
    candidates = substrate.GetSubstructMatches(Chem.MolFromSmarts("[C;X4;+0]-[C;X3;+0]=[O;X1;+0]"))
    site = args["enolate"]
    def usable_h(atom):
        return atom.GetTotalNumHs() or any(n.GetAtomicNum()==1 and n.GetIsotope()==0 for n in atom.GetNeighbors())
    candidates = [(a,c,o) for a,c,o in candidates if usable_h(substrate.GetAtomWithIdx(a))
                  and site.get("alpha", a+1)==a+1 and site.get("carbonyl", c+1)==c+1]
    if not candidates:
        raise ValueError("底物所选位点缺少带可转移氢、与羰基碳直接相连的 α-碳")
    ranks = Chem.CanonicalRankAtoms(substrate, breakTies=False)
    if len({(ranks[a],ranks[c]) for a,c,o in candidates}) != 1:
        choices = "、".join(f"{a+1}/{c+1}" for a,c,o in candidates)
        raise ValueError(f"有多个不等价位点，请按底物 SMILES 指定 alpha/carbonyl：{choices}")
    alpha, carbonyl, oxygen = candidates[0]
    substrate = Chem.RWMol(substrate)
    carbon = substrate.GetAtomWithIdx(alpha)
    hydrogen = next((n.GetIdx() for n in carbon.GetNeighbors() if n.GetAtomicNum()==1 and not n.GetIsotope()), None)
    if hydrogen is None:
        if carbon.GetNumExplicitHs():
            carbon.SetNumExplicitHs(carbon.GetNumExplicitHs()-1)
        hydrogen = substrate.AddAtom(Chem.Atom(1))
        substrate.AddBond(alpha, hydrogen, Chem.BondType.SINGLE)
    Chem.SanitizeMol(substrate)
    enolate = Chem.RWMol(substrate)
    enolate.GetBondBetweenAtoms(alpha, carbonyl).SetBondType(Chem.BondType.DOUBLE)
    enolate.GetBondBetweenAtoms(carbonyl, oxygen).SetBondType(Chem.BondType.SINGLE)
    enolate.GetAtomWithIdx(oxygen).SetFormalCharge(-1)
    enolate.GetAtomWithIdx(alpha).SetChiralTag(Chem.ChiralType.CHI_UNSPECIFIED)
    enolate.RemoveAtom(hydrogen)
    alcohol = Chem.RWMol(base)
    alcohol_oxygen = alcohol.GetAtomWithIdx(donor)
    alcohol_oxygen.SetFormalCharge(0)
    alcohol_oxygen.SetNumExplicitHs(alcohol_oxygen.GetNumExplicitHs()+1)
    if counterions:
        ion = counterions[0][0]
        enolate.AddAtom(Chem.Atom(base.GetAtomWithIdx(ion)))
        alcohol.RemoveAtom(ion)
    for mol in (alcohol, enolate):
        Chem.SanitizeMol(mol)
    smiles = Chem.MolToSmiles(substrate, canonical=False)
    order = {idx: n for n,idx in enumerate(json.loads(substrate.GetProp("_smilesAtomOutputOrder")), 1)}
    a,h,c,o = (order[i] for i in (alpha,hydrogen,carbonyl,oxygen))
    return {"arrow":"equilibrium", **args, "reactants":[specs[0], "smiles:"+smiles],
            "products":["smiles:"+Chem.MolToSmiles(enolate), "smiles:"+Chem.MolToSmiles(alcohol)],
            "electron_moves":[{"source":f"1:{donor+1}", "target":f"2:{h}"},
                              {"source":f"2:{a}-{h}", "target":f"2:{a}-{c}"},
                              {"source":f"2:{c}-{o}", "target":f"2:{o}"}]}
