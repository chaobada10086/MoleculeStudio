"""Readable full depictions and geometric preflight for cyanoarene donors."""
import math
from statistics import median
from rdkit import Chem
UNIT=1.5

def audit(mol):
    conf=mol.GetConformer();p=[(conf.GetAtomPosition(i).x,conf.GetAtomPosition(i).y) for i in range(mol.GetNumAtoms())]
    edges=[(b.GetBeginAtomIdx(),b.GetEndAtomIdx()) for b in mol.GetBonds()]
    # Projected rings have shorter drawn edges.  Do not let them reduce the
    # required clearance for the unprojected atom labels used by the editor.
    unit=max(UNIT,median(math.dist(p[a],p[b]) for a,b in edges)) if edges else UNIT
    close=[];crossings=[];on_bonds=[]
    for i in range(len(p)):
        for j in range(i):
            if mol.GetBondBetweenAtoms(i,j) is None and math.dist(p[i],p[j])<unit*.4:close.append((j,i))
    def cross(a,b,c):return (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0])
    for k,(a,b) in enumerate(edges):
        x,y=p[a];dx,dy=p[b][0]-x,p[b][1]-y;length=dx*dx+dy*dy
        for i,q in enumerate(p):
            if i in (a,b) or not length:continue
            t=((q[0]-x)*dx+(q[1]-y)*dy)/length
            if .03<t<.97 and math.dist(q,(x+t*dx,y+t*dy))<unit*.18:on_bonds.append((i,a,b))
        for c,d in edges[:k]:
            if len({a,b,c,d})<4:continue
            if cross(p[a],p[b],p[c])*cross(p[a],p[b],p[d])< -1e-9 and cross(p[c],p[d],p[a])*cross(p[c],p[d],p[b])< -1e-9:crossings.append((a,b,c,d))
    return dict(close_atoms=close,crossing_bonds=crossings,atoms_on_bonds=on_bonds)

def radial(mol):
    rings=mol.GetRingInfo().AtomRings();core=None
    for ring in rings:
        if len(ring)!=6 or any(mol.GetAtomWithIdx(i).GetAtomicNum()!=6 for i in ring):continue
        donors=cyano=0
        for i in ring:
            for n in mol.GetAtomWithIdx(i).GetNeighbors():
                if n.GetIdx() in ring:continue
                donors+=n.GetAtomicNum()==7 and mol.GetBondBetweenAtoms(i,n.GetIdx()).GetBondType()==Chem.BondType.SINGLE
                cyano+=n.GetAtomicNum()==6 and any(b.GetBondType()==Chem.BondType.TRIPLE and b.GetOtherAtom(n).GetAtomicNum()==7 for b in n.GetBonds())
        if donors>=2 and cyano>=1:core=list(ring);break
    if core is None:return False
    # Put the donor between the two meta nitriles at twelve o'clock.  This is
    # the familiar IPN drawing frame, rather than a traversal-dependent pose.
    def is_cyano(i):
        return any(n.GetIdx() not in core and n.GetAtomicNum()==6 and
                   any(b.GetBondType()==Chem.BondType.TRIPLE for b in n.GetBonds())
                   for n in mol.GetAtomWithIdx(i).GetNeighbors())
    for k,i in enumerate(core):
        if is_cyano(core[(k-1)%6]) and is_cyano(core[(k+1)%6]):
            core=core[k:]+core[:k];break
    conf=mol.GetConformer();original=[(conf.GetAtomPosition(i).x,conf.GetAtomPosition(i).y) for i in range(mol.GetNumAtoms())]
    def branch(start,blocked):
        todo=[start];seen=set(blocked)|{start}
        for i in todo:
            for atom in mol.GetAtomWithIdx(i).GetNeighbors():
                j=atom.GetIdx()
                if j not in seen:seen.add(j);todo.append(j)
        return todo
    ipn=sum(is_cyano(i) for i in core)==2 and is_cyano(core[1]) and is_cyano(core[5])
    best=None
    # Crowded donor rings are depicted as shallow projections.  Stretching the
    # core--N bonds avoids intersections but makes an ungainly spider drawing.
    # Keep those bonds close to the normal length and fit each donor into its
    # radial sector instead, applying the same projection to both phenyl arms.
    for extension in (1.1,1.2,1.3,1.4):
      for projection in (.85,.8,.75,.7,.65,.6,.55,.5,.45):
        spread=50.
        p=list(original)
        for k,i in enumerate(core):
            angle=math.pi/2+k*math.pi/3;u=(math.cos(angle),math.sin(angle));origin=(u[0]*UNIT,u[1]*UNIT);p[i]=origin
            for neighbor in mol.GetAtomWithIdx(i).GetNeighbors():
                root=neighbor.GetIdx()
                if root in core:continue
                members=branch(root,core)
                if neighbor.GetAtomicNum()==6:
                    if len(members)!=2:return False
                    p[root]=(origin[0]+u[0]*UNIT,origin[1]+u[1]*UNIT)
                    other=next(j for j in members if j!=root);p[other]=(origin[0]+u[0]*2*UNIT,origin[1]+u[1]*2*UNIT);continue
                pivot=(origin[0]+u[0]*UNIT*extension,origin[1]+u[1]*UNIT*extension);p[root]=pivot
                if not neighbor.IsInRing():
                    arms=[a.GetIdx() for a in neighbor.GetNeighbors() if a.GetIdx() not in core]
                    if len(arms)!=2:return False
                    for sign,anchor in zip((-1,1),arms):
                        atoms=branch(anchor,set(core)|{root});cycle=next((list(r) for r in rings if anchor in r and set(r)==set(atoms)),None)
                        if cycle is None or len(cycle)!=6:return False
                        fan=math.radians(-28 if k==2 else 28) if ipn and k in (2,4) else 0.
                        direction=angle+math.radians(spread)*sign+fan;v=(math.cos(direction),math.sin(direction));centre=(pivot[0]+2*UNIT*v[0],pivot[1]+2*UNIT*v[1]);start=cycle.index(anchor)
                        for t in range(6):
                            theta=direction+math.pi+t*math.pi/3;p[cycle[(start+t)%6]]=(centre[0]+UNIT*math.cos(theta),centre[1]+UNIT*math.sin(theta))
                else:
                    centre=tuple(sum(original[j][axis] for j in members)/len(members) for axis in (0,1));axis=(centre[0]-original[root][0],centre[1]-original[root][1]);turn=angle-math.atan2(axis[1],axis[0])
                    # Fan the two lateral carbazoles away from the lower donor.
                    # This keeps their benzene rings broad enough for ordinary
                    # double-bond spacing instead of collapsing them edge-on.
                    if ipn and k in (2,4):turn+=math.radians(-28 if k==2 else 28)
                    c,s=math.cos(turn),math.sin(turn)
                    for j in members:
                        x,y=original[j][0]-original[root][0],original[j][1]-original[root][1];p[j]=(pivot[0]+x*c-y*s,pivot[1]+x*s+y*c)
                upright=ipn and k in (0,3)
                axial_scale=1. if upright else .9
                tangent_scale=1. if upright else projection
                for j in members:
                    x,y=p[j][0]-pivot[0],p[j][1]-pivot[1]
                    axial=x*u[0]+y*u[1];tangent=-x*u[1]+y*u[0]
                    p[j]=(pivot[0]+axial_scale*axial*u[0]-tangent_scale*tangent*u[1],
                          pivot[1]+axial_scale*axial*u[1]+tangent_scale*tangent*u[0])
        for i,q in enumerate(p):conf.SetAtomPosition(i,(q[0],q[1],0.))
        report=audit(mol);bad=sum(len(v) for v in report.values());cost=bad*10000+extension*100+(1-projection)*10
        if best is None or cost<best[0]:best=cost,p,report
        if not bad:break
      if best and not any(best[2].values()):break
    if best is None or any(best[2].values()):
        for i,q in enumerate(original):conf.SetAtomPosition(i,(q[0],q[1],0.))
        raise ValueError('cyanoarene full depiction could not be separated')
    for i,q in enumerate(best[1]):conf.SetAtomPosition(i,(q[0],q[1],0.))
    return True
