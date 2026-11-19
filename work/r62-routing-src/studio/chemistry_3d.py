"""Local 3D conformer backend using RDKit ETKDG/MMFF."""
import sys, math
from pathlib import Path
def run(source):
    from rdkit import Chem
    from rdkit.Chem import AllChem
    lines=source.splitlines(); n,m=map(int,lines[0].split()); length=float(lines[1]); mol=Chem.RWMol(); positions=[]
    if not 1<=n<=1000: raise ValueError('三维整理支持 1–1000 个原子')
    for line in lines[2:2+n]:
        z,charge,isotope,hydrogens,x,y=map(float,line.split()); atom=Chem.Atom(int(z));atom.SetFormalCharge(int(charge));atom.SetIsotope(int(isotope))
        if hydrogens>=0: atom.SetNumExplicitHs(int(hydrogens));atom.SetNoImplicit(True)
        mol.AddAtom(atom);positions.append((x,y))
    conformer=Chem.Conformer(n);conformer.Set3D(False)
    for i,(x,y) in enumerate(positions): conformer.SetAtomPosition(i,(x,-y,0))
    orders={1:Chem.BondType.SINGLE,2:Chem.BondType.DOUBLE,3:Chem.BondType.TRIPLE,4:Chem.BondType.AROMATIC}
    for line in lines[2+n:2+n+m]:
        a,b,order,wedge=map(int,line.split());mol.AddBond(a,b,orders[order]);bond=mol.GetBondBetweenAtoms(a,b)
        if order==4:
            mol.GetAtomWithIdx(a).SetIsAromatic(True);mol.GetAtomWithIdx(b).SetIsAromatic(True)
        if wedge: bond.SetBondDir(Chem.BondDir.BEGINWEDGE if wedge==1 else Chem.BondDir.BEGINDASH)
    mol.AddConformer(conformer);mol=mol.GetMol();Chem.SanitizeMol(mol)
    Chem.AssignChiralTypesFromBondDirs(mol);Chem.AssignStereochemistry(mol,cleanIt=True,force=True)
    # Keep specified alkene geometry when generating the 3D conformer.
    for bond in mol.GetBonds():
        if bond.GetBondType()!=Chem.BondType.DOUBLE or bond.IsInRing(): continue
        a,b=bond.GetBeginAtomIdx(),bond.GetEndAtomIdx()
        left=[v.GetIdx() for v in mol.GetAtomWithIdx(a).GetNeighbors() if v.GetIdx()!=b]
        right=[v.GetIdx() for v in mol.GetAtomWithIdx(b).GetNeighbors() if v.GetIdx()!=a]
        if not left or not right: continue
        ax,ay=positions[a];bx,by=positions[b];lx,ly=positions[left[0]];rx,ry=positions[right[0]]
        sa=(bx-ax)*(ly-ay)-(by-ay)*(lx-ax);sb=(bx-ax)*(ry-by)-(by-ay)*(rx-bx)
        if abs(sa*sb)>1e-8:
            bond.SetStereoAtoms(left[0],right[0]);bond.SetStereo(Chem.BondStereo.STEREOCIS if sa*sb>0 else Chem.BondStereo.STEREOTRANS)
    work=Chem.AddHs(mol);params=AllChem.ETKDGv3();params.randomSeed=0x52CD;params.numThreads=1
    if AllChem.EmbedMolecule(work,params)!=0: raise ValueError('无法为该结构生成三维构象')
    if AllChem.MMFFHasAllMoleculeParams(work): AllChem.MMFFOptimizeMolecule(work,maxIters=400)
    elif AllChem.UFFHasAllMoleculeParams(work): AllChem.UFFOptimizeMolecule(work,maxIters=400)
    xyz=work.GetConformer().GetPositions()[:n]
    distances=[float(((xyz[b.GetBeginAtomIdx()]-xyz[b.GetEndAtomIdx()])**2).sum())**.5 for b in mol.GetBonds()]
    factor=length/(sum(distances)/len(distances) if distances else 1.5);xyz=(xyz-xyz.mean(axis=0))*factor
    # Stable orthographic view: expose depth without changing connectivity or stereo.
    rows=[]
    for x,y,z in xyz:
        rx=.9063078*x+.4226183*z;rz=-.4226183*x+.9063078*z
        # CDXML uses down-positive Y. Keep Z in the conformer's direction:
        # reflecting Y alone makes the exported xyz coordinates agree with
        # CDXML readers' handedness and the 2D wedge representation.
        rows.append((rx,-(.8660254*y-.5*rz),(.5*y+.8660254*rz)))
    # Recompute the drawing's wedge placement for the new projection. Keeping
    # old wedge directions after moving neighbours can invert an absolute CIP label.
    drawing=Chem.Mol(mol);drawing.RemoveAllConformers();flat=Chem.Conformer(n);flat.Set3D(False)
    for i,(x,y,z) in enumerate(rows): flat.SetAtomPosition(i,(x,-y,0))
    drawing.AddConformer(flat)
    for bond in drawing.GetBonds(): bond.SetBondDir(Chem.BondDir.NONE)
    Chem.WedgeMolBonds(drawing,drawing.GetConformer())
    updates=[]
    for bond in drawing.GetBonds():
        a,b=bond.GetBeginAtomIdx(),bond.GetEndAtomIdx();direction=bond.GetBondDir()
        chiral=any(drawing.GetAtomWithIdx(i).GetChiralTag()!=Chem.ChiralType.CHI_UNSPECIFIED for i in (a,b))
        was_wedge=mol.GetBondWithIdx(bond.GetIdx()).GetBondDir() in (Chem.BondDir.BEGINWEDGE,Chem.BondDir.BEGINDASH)
        code=1 if direction==Chem.BondDir.BEGINWEDGE else 2 if direction==Chem.BondDir.BEGINDASH else 0 if chiral and was_wedge else -1
        updates.append((bond.GetIdx(),a,b,code))
    return 'OK\n'+str(n)+'\n'+'\n'.join(' '.join(format(float(v),'.12g') for v in row) for row in rows)+'\nBONDS\n'+str(m)+'\n'+'\n'.join(' '.join(map(str,row)) for row in updates)+'\n'
if __name__=='__main__':
    try: result=run(Path(sys.argv[1]).read_text(encoding='utf-8'));code=0
    except Exception as exc: result='三维整理失败：'+str(exc);code=2
    Path(sys.argv[2]).write_text(result,encoding='utf-8');sys.exit(code)
