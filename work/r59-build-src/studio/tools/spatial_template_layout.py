"""Independently authored publication-style projections for editable templates.

Ring depth is drawing geometry, not an assignment of absolute stereochemistry.
All output uses standard CDXML xyz, Bold/BoldWidth and wedge displays.
"""
import math
from rdkit import Chem
from photosensitizer_layout import audit

PHOTO_CATEGORIES = {
    '有机光催化剂', '吖啶鎓与吡喃鎓光催化剂', '吩噻嗪与吩噁嗪光催化剂',
    '芳烃光敏剂', '芳香酮光敏剂', '染料光敏剂', '金属配合物光催化剂',
}
BULKY = {'PPh3', 'OPPh3', 'PCy3'}


def selected(record):
    return record['category'] in PHOTO_CATEGORIES or bool(set(record['alias'].split()) & (BULKY | {'EosinY-Na2'}))


def iridium_molecule():
    """Three connected C,N chelates; covalent metal drawing convention.

    The C--Ir single bonds remove exactly one H from each ppy ligand. Neutral
    formal charges and explicit hydrogen counts give C33H24IrN3.
    The six-coordinate drawing is a fac projection, with no Delta/Lambda tag.
    """
    ligand = Chem.MolFromSmiles('c1ccc(-c2ccccn2)cc1')
    combined = Chem.MolFromSmiles('[Ir]')
    for _ in range(3):
        combined = Chem.CombineMols(combined, ligand)
    editable = Chem.RWMol(combined)
    conf = Chem.Conformer(combined.GetNumAtoms())
    conf.SetAtomPosition(0, (0, 0, 0))
    for k in range(3):
        offset = 1 + k * ligand.GetNumAtoms()
        nitrogen = next(a.GetIdx() for a in ligand.GetAtoms() if a.GetAtomicNum() == 7)
        bridge_n = next(a.GetIdx() for a in ligand.GetAtomWithIdx(nitrogen).GetNeighbors()
                        if any(not b.IsInRing() for b in a.GetBonds()))
        bridge_c = next(a.GetIdx() for a in ligand.GetAtomWithIdx(bridge_n).GetNeighbors()
                        if not ligand.GetBondBetweenAtoms(bridge_n, a.GetIdx()).IsInRing())
        carbon = next(a.GetIdx() for a in ligand.GetAtomWithIdx(bridge_c).GetNeighbors()
                      if a.GetIdx() != bridge_n)
        editable.AddBond(offset + carbon, 0, Chem.BondType.SINGLE)
        editable.AddBond(offset + nitrogen, 0, Chem.BondType.DATIVE)
        positions = {}
        for anchor, adjacent, origin, theta in (
            (nitrogen, bridge_n, (0, 1.9), math.pi / 2),
            (carbon, bridge_c, (-math.sqrt(3), .9), 0),
        ):
            ring = next(list(r) for r in ligand.GetRingInfo().AtomRings() if anchor in r)
            start = ring.index(anchor)
            if ring[(start + 1) % 6] != adjacent:
                ring.reverse(); start = ring.index(anchor)
            # Centre and start angle for a regular six-membered ligand ring.
            start_angle = -math.pi / 2 if anchor == nitrogen else -math.pi / 6
            centre = (origin[0] - math.cos(start_angle), origin[1] - math.sin(start_angle))
            sense = -1 if anchor == nitrogen else 1
            for j in range(6):
                t = start_angle + sense * j * math.pi / 3
                positions[ring[(start+j)%6]] = (centre[0]+math.cos(t), centre[1]+math.sin(t))
        c, s = math.cos(k*2*math.pi/3), math.sin(k*2*math.pi/3)
        for i, (x, y) in positions.items():
            conf.SetAtomPosition(offset+i, (1.5*(c*x-s*y), 1.5*(s*x+c*y), 0))
        for i, kind in ((carbon, Chem.BondDir.BEGINDASH if k != 1 else None),
                        (nitrogen, Chem.BondDir.BEGINWEDGE if k != 0 else None)):
            bond = editable.GetBondBetweenAtoms(0, offset+i)
            bond.SetIntProp('studio.root', 0)
            if kind is not None:
                bond.SetBondDir(kind)
    mol = editable.GetMol()
    mol.UpdatePropertyCache(strict=False)
    Chem.GetSymmSSSR(mol)
    Chem.Kekulize(mol, clearAromaticFlags=True)
    mol.AddConformer(conf)
    mol.SetProp('studio.projections', '[]')
    return mol


def apply(root, mol, record):
    if not selected(record):
        return None
    root.set('BondSpacing', '18')
    root.set('BoldWidth', '1.332')
    if record.get('spatial_layout') == 'fac-ppy':
        # Publication convention: coordination connections are plain or
        # wedged lines, rather than donor arrows. Explicit H counts preserve
        # the pyridyl valence representation in both editors.
        for b in root.findall('./page/fragment/b'):
            if b.get('Order') == 'dative':b.set('Order','1')
        for n in root.findall('./page/fragment/n'):
            if n.get('Element')=='7':n.set('IgnoreWarnings','yes')
        return dict(kind='fac-octahedral', solid_wedges=2, hashed_wedges=2)
    conf = mol.GetConformer()
    original = [(conf.GetAtomPosition(i).x, conf.GetAtomPosition(i).y, 0.) for i in range(mol.GetNumAtoms())]
    rings = [set(r) for r in mol.GetRingInfo().AtomRings()]
    # Fuse intersecting rings, so a carbazole/naphthalene remains one plane.
    systems = []
    for ring in rings:
        touching = [r for r in systems if r & ring]
        merged = set(ring).union(*touching)
        systems = [r for r in systems if r not in touching] + [merged]
    systems.sort(key=lambda r: (-len(r), min(r)))
    base = systems[0] if systems else set()
    if record['alias'].split()[0] in {'4CzIPN','4CzTPN','2CzPN','5CzBN','4DPAIPN','3DPAFIPN'}:
        base = max(systems,key=lambda r:sum(n.GetIdx() not in r for i in r for n in mol.GetAtomWithIdx(i).GetNeighbors()))
    proposals = []
    for system in systems:
        # Rotate each pendant ring system, carrying substituents with it.
        outgoing = [(i, n.GetIdx()) for i in system for n in mol.GetAtomWithIdx(i).GetNeighbors()
                    if n.GetIdx() not in system and not mol.GetBondBetweenAtoms(i,n.GetIdx()).IsInRing()]
        if system == base or not outgoing:
            continue
        anchor, other = min(outgoing, key=lambda p: (p[1] not in base, p))
        members, todo = set(system), list(system)
        for i in todo:
            for n in mol.GetAtomWithIdx(i).GetNeighbors():
                j = n.GetIdx()
                if j == other or j in members or (j in base and j not in system):
                    continue
                members.add(j); todo.append(j)
        if members & base:
            continue
        proposals.append((anchor, other, members))
    best = None
    # Prefer a visible tilt; keep the original clearances as the hard constraint.
    for compression in (.78, .85, .91, .96, .985):
        p = [list(q) for q in original]
        # A slight overall tilt gives even planar sensitizers a readable edge.
        for i, (x,y,_) in enumerate(original):
            p[i] = [x, y*.96, y*math.sqrt(1-.96**2)]
        for k, (anchor, other, members) in enumerate(proposals):
            ax, ay, _ = p[anchor]; bx, by, _ = p[other]
            norm = math.hypot(ax-bx, ay-by)
            ux, uy = (ax-bx)/norm, (ay-by)/norm
            for i in members:
                x, y = p[i][0]-ax, p[i][1]-ay
                along, across = x*ux+y*uy, -x*uy+y*ux
                p[i][0] = ax+along*ux-compression*across*uy
                p[i][1] = ay+along*uy+compression*across*ux
                p[i][2] += across*math.sqrt(1-compression**2)*(-1 if k%2 else 1)
        for i,q in enumerate(p): conf.SetAtomPosition(i,q)
        report = audit(mol)
        if not any(report.values()):
            best = p; break
    if best is None:
        # Uniform affine projection never introduces a new bond crossing.
        best = [[x,y*.985,y*math.sqrt(1-.985**2)] for x,y,_ in original]
    nodes = {int(n.get('id'))-10:n for n in root.findall('./page/fragment/n')}
    for i,(x,y,z) in enumerate(best):
        nodes[i].set('p', f'{x*22/1.5:.9f} {-y*22/1.5:.9f}')
        nodes[i].set('xyz', f'{x*22/1.5:.9f} {-y*22/1.5:.9f} {z*22/1.5:.9f}')
        conf.SetAtomPosition(i,(x,y,z))
    bonds = {int(b.get('id'))-1000:b for b in root.findall('./page/fragment/b')}
    bold = set()
    # Only foreground perimeter edges are bold; fused internal edges stay thin.
    for ring in rings:
        if len(systems)>1 and ring <= base:continue
        edges = [b for b in mol.GetBonds() if b.GetBeginAtomIdx() in ring and b.GetEndAtomIdx() in ring]
        candidates = [b for b in edges if sum(b.GetBeginAtomIdx() in r and b.GetEndAtomIdx() in r for r in rings)==1]
        if not candidates: continue
        lo, hi = min(best[i][2] for i in ring), max(best[i][2] for i in ring)
        for b in candidates:
            if mol.GetAtomWithIdx(b.GetBeginAtomIdx()).GetAtomicNum()!=6 or mol.GetAtomWithIdx(b.GetEndAtomIdx()).GetAtomicNum()!=6:continue
            if hi-lo>1e-5 and (best[b.GetBeginAtomIdx()][2]+best[b.GetEndAtomIdx()][2])*.5 < lo+(hi-lo)*.32:
                target = bonds[b.GetIdx()]
                if target.get('Display') in (None,'Bold'):
                    target.set('Display','Bold'); target.set('BoldWidth','1.332'); bold.add(b.GetIdx())
    return dict(kind='ring-perspective', ring_systems=len(systems), bold_bonds=len(bold), compression=compression if p==best else .985)
