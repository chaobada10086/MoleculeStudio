from pathlib import Path
import sys,json,xml.etree.ElementTree as ET
sys.path.insert(0,str(Path('out/Studio').resolve()))
from chemistry_open import one_molecule,identity
directory=Path(sys.argv[1]);total=0
for source in range(5):
    baseline=None
    for target in range(5):
        molecule=one_molecule((directory/f'branch-{source}-{target}.cdxml').read_text())
        actual=identity(molecule)
        if baseline is None:baseline=actual
        assert actual==baseline,(source,target,actual,baseline)
        assert molecule.GetRingInfo().NumRings()==1,(source,target)
        tree=ET.parse(directory/f'branch-{source}-{target}.cdxml');nodes={n.get('id'):n for n in tree.findall('.//page/fragment/n')};edges=tree.findall('.//page/fragment/b')
        def position(n):return tuple(map(float,nodes[n].get('p').split()))
        def cross(a,b,c):return (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0])
        for e in edges:
            ids={e.get('B'),e.get('E')}
            if any(nodes[n].get('MoleculeStudioProjection') is not None for n in ids):continue
            a,b=map(position,(e.get('B'),e.get('E')))
            for other in edges:
                others={other.get('B'),other.get('E')}
                if ids&others or not any(nodes[n].get('MoleculeStudioProjection') is not None for n in others):continue
                c,d=map(position,(other.get('B'),other.get('E')))
                assert not(cross(a,b,c)*cross(a,b,d)<-1e-7 and cross(c,d,a)*cross(c,d,b)<-1e-7),(source,target,'ring crosses projection bond',ids,others)
        total+=1
result={'conversion_pairs':total,'isomeric_identity':True,'cyclopentane_preserved':True,'ring_clears_projection_bonds':True}
(directory/'independent-branches.json').write_text(json.dumps(result,indent=2))
print(json.dumps(result))
