from pathlib import Path
import sys,json
sys.path.insert(0,str(Path('out/Studio').resolve()))
from chemistry_open import one_molecule,identity
from rdkit import RDLogger
RDLogger.DisableLog('rdApp.warning')
directory=Path(sys.argv[1]);checks=0;starting=set();identities={}
def centre(m,z):return next(a for a in m.GetAtoms() if a.GetAtomicNum()==z).GetNeighbors()[0]
for am in range(2):
    for bm in range(2):
        for view in range(5):
            prefix=directory/f'sn2-{am}-{bm}-{view}'
            source=one_molecule(Path(str(prefix)+'-source.cdxml').read_text());product=one_molecule(Path(str(prefix)+'-product.cdxml').read_text())
            a,b=centre(source,17),centre(source,35);aa,bb=centre(product,17),centre(product,8)
            old=b.GetProp('_CIPCode');new=bb.GetProp('_CIPCode');starting.add(old)
            assert old!=new,(am,bm,view,'reaction centre did not invert',old,new)
            assert a.GetProp('_CIPCode')==aa.GetProp('_CIPCode'),(am,bm,view,'spectator changed')
            actual=identity(product);key=(am,bm)
            assert key not in identities or identities[key]==actual,(am,bm,view,'projection views disagree')
            identities[key]=actual;checks+=1
assert starting=={'R','S'}
result={'cases':checks,'starting_configurations':sorted(starting),'R_to_S':True,'S_to_R':True,'spectator_retention':True,'five_views_same_product':True}
(directory/'sn2-rs-matrix.json').write_text(json.dumps(result,indent=2));print(json.dumps(result))
