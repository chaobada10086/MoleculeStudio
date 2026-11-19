"""Geometry, chemistry and partial-selection regressions for alkyl projection."""
from pathlib import Path
import math,sys,xml.etree.ElementTree as ET
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from rdkit import Chem
from rdkit.Chem import rdDepictor
from branched_alkyl_layout import molecule
import chemistry_open as backend


def main():
    cases=['CC(C)(C)O','CC(C)(C#N)N=NC(C)(C)C#N',
           'CC(C)(C)OC(=O)OC(=O)OC(C)(C)C',
           'CC(C)(C)c1cc(C(C)(C)C)cc(C)c1O','CC(C)(C)[Si](C)(C)Cl','CC1(C)CCCC1']
    for smiles in cases:
        m=Chem.MolFromSmiles(smiles);rdDepictor.Compute2DCoords(m)
        original=Chem.Mol(m);identity=Chem.MolToSmiles(m);changed=molecule(m)
        assert Chem.MolToSmiles(m)==identity
        for b in m.GetBonds():
            a,z=b.GetBeginAtomIdx(),b.GetEndAtomIdx()
            length=lambda mol:(mol.GetConformer().GetAtomPosition(a)-mol.GetConformer().GetAtomPosition(z)).Length()
            assert abs(length(m)-length(original))<1e-8
        points=[tuple(m.GetConformer().GetAtomPosition(i)) for i in range(m.GetNumAtoms())]
        molecule(m)
        assert all(math.dist(tuple(m.GetConformer().GetAtomPosition(i)),p)<1e-7 for i,p in enumerate(points))
        if smiles=='CC1(C)CCCC1':assert changed==0
        points=[tuple(original.GetConformer().GetAtomPosition(i)) for i in range(original.GetNumAtoms())]
        molecule(original,range(original.GetNumAtoms()))
        assert all(tuple(original.GetConformer().GetAtomPosition(i))==p for i,p in enumerate(points))
    for filename in ['reagent-123.cdxml','reagent-118.cdxml','reagent-117.cdxml','reagent-333.cdxml','reagent-367.cdxml']:
        source=(ROOT/'reagent_templates'/filename).read_text(encoding='utf-8')
        identity=lambda raw:backend.identity(backend.one_molecule(raw,allow_disconnected=True,allow_queries=True))
        assert identity(backend.clean_structure(source))==identity(source)
        tree=ET.fromstring(source);fragment=tree.find('./page/fragment')
        nodes={n.get('id'):n for n in fragment.findall('n')};adj={i:[] for i in nodes}
        for b in fragment.findall('b'):adj[b.get('B')].append(b.get('E'));adj[b.get('E')].append(b.get('B'))
        center=next(i for i,n in nodes.items() if n.get('Element','6')=='6' and len(adj[i])==4)
        methyls=[j for j in adj[center] if len(adj[j])==1 and nodes[j].get('NumHydrogens')=='3']
        if filename=='reagent-123.cdxml':
            p=lambda i:tuple(map(float,nodes[i].get('p').split()))
            vectors=[(p(i)[0]-p(center)[0],p(i)[1]-p(center)[1]) for i in methyls]
            angle=math.degrees(math.acos(sum(a*b for a,b in zip(*vectors))/(math.hypot(*vectors[0])*math.hypot(*vectors[1]))))
            assert abs(angle-60)<1e-6
            ns=[n for n in nodes if nodes[n].get('Element')=='7' and len(adj[n])==2]
            assert len(ns)==2 and abs(p(ns[0])[0]-p(ns[1])[0])<1e-7
        fixed=set(nodes)-set(methyls);tree.set('MoleculeStudioCleanFixedAtoms',' '.join(fixed))
        result=ET.fromstring(backend.clean_structure(ET.tostring(tree,encoding='unicode')))
        actual={n.get('id'):n for n in result.findall('./page/fragment/n')}
        for i in fixed:assert math.dist(tuple(map(float,nodes[i].get('p').split())),tuple(map(float,actual[i].get('p').split())))<1e-7
    print('PASS: six molecular projections, five cleanup/partial-selection fixtures, upright AIBN')


if __name__=='__main__':main()
