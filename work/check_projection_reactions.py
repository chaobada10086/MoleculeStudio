from pathlib import Path
import sys,json
sys.path.insert(0,str(Path('out/Studio').resolve()))
from chemistry_open import one_molecule,identity
from rdkit import Chem,RDLogger
RDLogger.DisableLog('rdApp.warning')
directory=Path(sys.argv[1]);checks=0;identities={}
for mirror in range(2):
    for view in range(5):
        for mode in range(2):
            prefix=directory/f'projection-mechanism-{mirror}-{view}-{mode}'
            original=one_molecule(Path(str(prefix)+'-source.cdxml').read_text())
            product=one_molecule(Path(str(prefix)+'-product.cdxml').read_text())
            Chem.AssignStereochemistry(original,cleanIt=True,force=True);Chem.AssignStereochemistry(product,cleanIt=True,force=True)
            key=(mirror,mode);actual=identity(product)
            if key in identities:assert actual==identities[key],(mirror,view,mode,actual,identities[key])
            else:identities[key]=actual
            def center(mol,z):
                hetero=[a for a in mol.GetAtoms() if a.GetAtomicNum()==z]
                assert len(hetero)==1
                return hetero[0].GetNeighbors()[0]
            if mode==0:
                double=[b for b in product.GetBonds() if b.GetBondType()==Chem.BondType.DOUBLE and b.GetBeginAtom().GetAtomicNum()==6 and b.GetEndAtom().GetAtomicNum()==6]
                assert len(double)==1
                want=Chem.BondStereo.STEREOE if mirror==0 else Chem.BondStereo.STEREOZ
                assert double[0].GetStereo()==want,(mirror,view,'E/Z',double[0].GetStereo(),want)
            else:
                a0,b0=center(original,17),center(original,35);a1,b1=center(product,17),center(product,8)
                assert a0.HasProp('_CIPCode') and b0.HasProp('_CIPCode') and a1.HasProp('_CIPCode') and b1.HasProp('_CIPCode'),(mirror,view,'missing tetrahedral stereo')
                assert a0.GetProp('_CIPCode')==a1.GetProp('_CIPCode'),(mirror,view,'spectator centre changed')
                assert b0.GetProp('_CIPCode')!=b1.GetProp('_CIPCode'),(mirror,view,'SN2 did not invert')
            checks+=1
result={'cases':checks,'five_views_same_product':True,'E_Z_independently_checked':True,'SN2_inversion':True,'spectator_retention':True}
(directory/'independent-projection-reactions.json').write_text(json.dumps(result,indent=2));print(json.dumps(result))
