"""Open projections for tert-butyl and acyclic gem-dimethyl groups.

Only coordinates change. The heavier non-methyl branch supplies the frame;
fixed atoms, cyclic junctions and explicit stereochemistry remain untouched.
"""
import math


def project(points, elements, edges, hydrogens, weights=None, fixed=(), stereo=()):
    positions=dict(points); fixed=set(fixed); stereo=set(stereo); adjacent={i:[] for i in points}
    for a,b,order in edges:
        if a in adjacent and b in adjacent:
            adjacent[a].append((b,order));adjacent[b].append((a,order))
    weights=weights or {i:elements.get(i,6) for i in points}
    changed=0
    for center,neighbors in adjacent.items():
        if elements.get(center)!=6 or len(neighbors)!=4 or center in stereo or any(o!=1 for _,o in neighbors):continue
        methyls=[i for i,_ in neighbors if elements.get(i)==6 and len(adjacent[i])==1 and hydrogens.get(i)==3]
        if len(methyls)<2:continue
        branches={};cyclic=False
        for first,_ in neighbors:
            branch=[first];seen={center,first}
            for i in branch:
                for j,_ in adjacent[i]:
                    if i==first and j==center:continue
                    if j==center:cyclic=True
                    if j not in seen:seen.add(j);branch.append(j)
            branches[first]=branch
        if cyclic:continue
        core=[i for i,_ in neighbors if i not in methyls]
        if not core:core=[methyls.pop(0)]
        core.sort(key=lambda i:sum(weights.get(j,0) for j in branches[i]),reverse=True)
        pivot=positions[center]
        def angle(i,p=positions):return math.atan2(p[i][1]-pivot[1],p[i][0]-pivot[0])
        lengths={i:math.dist(positions[i],pivot) for i,_ in neighbors}
        if min(lengths.values())<1e-8:continue
        candidates=[]
        if len(core)==1:
            frame=angle(core[0]); candidates=[(None,[frame+v*math.pi/3 for v in (2,3,4)])]
        else:
            # AIBN-like centers: keep N/main scaffold, turn the short nitrile
            # branch to 120 degrees and fan the two methyls behind it.
            frame=angle(core[0])
            for sense in (-1,1):
                other=frame+sense*2*math.pi/3
                back=frame+sense*math.pi/3+math.pi
                candidates.append((other,[back-math.pi/6,back+math.pi/6]))
        best=None
        import itertools
        for other,targets in candidates:
            for order in itertools.permutations(methyls):
                trial=dict(positions)
                if other is not None:
                    turn=other-angle(core[1]);c,s=math.cos(turn),math.sin(turn)
                    for i in branches[core[1]]:
                        x,y=positions[i][0]-pivot[0],positions[i][1]-pivot[1]
                        trial[i]=(pivot[0]+c*x-s*y,pivot[1]+s*x+c*y)
                for i,target in zip(order,targets):trial[i]=(pivot[0]+lengths[i]*math.cos(target),pivot[1]+lengths[i]*math.sin(target))
                moved={i for i in trial if math.dist(trial[i],positions[i])>1e-7}
                if moved&fixed:continue
                unit=sum(lengths.values())/len(lengths);cost=0.
                for i in moved:
                    bonded={j for j,_ in adjacent[i]}
                    for j in trial:
                        if i==j or j in bonded:continue
                        cost+=max(0.,.65*unit-math.dist(trial[i],trial[j]))**2*10000
                cost+=sum(math.dist(trial[i],positions[i])**2 for i in moved)*.0001
                if best is None or cost<best[0]:best=(cost,trial,moved)
        if best and best[2]:positions=best[1];changed+=1
    return positions,changed


def molecule(mol,fixed=()):
    if not mol.GetNumConformers():return 0
    conf=mol.GetConformer();atoms=list(mol.GetAtoms())
    points={a.GetIdx():(conf.GetAtomPosition(a.GetIdx()).x,conf.GetAtomPosition(a.GetIdx()).y) for a in atoms}
    positions,count=project(points,{a.GetIdx():a.GetAtomicNum() for a in atoms},
        [(b.GetBeginAtomIdx(),b.GetEndAtomIdx(),b.GetBondTypeAsDouble()) for b in mol.GetBonds()],
        {a.GetIdx():a.GetTotalNumHs() if not a.GetIsotope() else -1 for a in atoms},{a.GetIdx():0. if a.HasProp("_STUDIO_LAYOUT_ABBREVIATION") else a.GetMass() for a in atoms},fixed,
        [a.GetIdx() for a in atoms if int(a.GetChiralTag())])
    for i,(x,y) in positions.items():conf.SetAtomPosition(i,(x,y,0.))
    return count


def xml_fragment(fragment):
    """Update direct nodes; recurse into complete expandable definitions."""
    count=0
    for node in fragment.findall('n'):
        for hidden in node.findall('fragment'):count+=xml_fragment(hidden)
    nodes={n.get('id'):n for n in fragment.findall('n') if n.get('p')}
    points={i:tuple(map(float,n.get('p').split())) for i,n in nodes.items()}
    elements={i:int(n.get('Element','6')) if not n.get('NodeType') else 0 for i,n in nodes.items()}
    edges=[];stereo=set()
    for b in fragment.findall('b'):
        a,z=b.get('B'),b.get('E')
        try:order=float(b.get('Order','1'))
        except ValueError:order=0
        edges.append((a,z,order))
        if b.get('Display') in ('WedgeBegin','WedgedHashBegin','WedgeEnd','WedgedHashEnd'):stereo.update((a,z))
    # A carbon with two identical methyls is not a stereocenter. Some library
    # sources used artificial wedges here solely to escape a planar cross.
    # Replace those with the open projection, retaining wedges rooted at a
    # genuinely stereogenic neighboring atom and isotope-distinguished methyls.
    adjacent={i:[] for i in nodes}
    for a,z,o in edges:
        if a in adjacent and z in adjacent:adjacent[a].append((z,o));adjacent[z].append((a,o))
    unmarked=set()
    for i,neighbors in adjacent.items():
        methyls=[j for j,o in neighbors if o==1 and elements[j]==6 and len(adjacent[j])==1 and nodes[j].get('NumHydrogens')=='3' and not any(nodes[j].get(k) for k in ('Isotope','Charge','Radical'))]
        if elements[i]==6 and len(neighbors)==4 and len(methyls)>=2:
            stereo.discard(i)
            for b in fragment.findall('b'):
                if (b.get('B')==i and b.get('Display') in ('WedgeBegin','WedgedHashBegin')) or (b.get('E')==i and b.get('Display') in ('WedgeEnd','WedgedHashEnd')):
                    b.attrib.pop('Display');unmarked.add(i)
    # Atomic masses, rather than atom counts, choose the fixed branch.
    from rdkit import Chem
    table=Chem.GetPeriodicTable()
    positions,changed=project(points,elements,edges,{i:int(n.get('NumHydrogens','-1')) if not n.get('Isotope') else -1 for i,n in nodes.items()},
        {i:table.GetAtomicWeight(z) if z else 0 for i,z in elements.items()},stereo=stereo)
    if changed:
        for i,p in positions.items():
            if math.dist(points[i],p)>1e-7:nodes[i].set('p',f'{p[0]:.9f} {p[1]:.9f}')
    return count+changed+len(unmarked)


def aibn_fragment(fragment):
    """Authored upright AIBN preview; the two nitriles stay explicitly linear."""
    nodes={n.get('id'):n for n in fragment.findall('n')}
    if len(nodes)!=12:return 0
    edges={};adjacent={i:[] for i in nodes}
    for b in fragment.findall('b'):
        a,z=b.get('B'),b.get('E');o=b.get('Order','1');edges[frozenset((a,z))]=o
        if a in adjacent and z in adjacent:adjacent[a].append(z);adjacent[z].append(a)
    pairs=[(a,z) for e,o in edges.items() if o=='2' for a,z in [tuple(e)] if all(nodes[i].get('Element')=='7' for i in (a,z))]
    if len(pairs)!=1:return 0
    first,second=sorted(pairs[0],key=lambda i:int(i))
    centers=[];methyls=[];nitriles=[]
    for nitrogen,other in ((first,second),(second,first)):
        carbon=[j for j in adjacent[nitrogen] if j!=other and nodes[j].get('Element')=='6' and len(adjacent[j])==4]
        if len(carbon)!=1:return 0
        c=carbon[0];me=[j for j in adjacent[c] if nodes[j].get('Element')=='6' and len(adjacent[j])==1 and nodes[j].get('NumHydrogens')=='3']
        cn=[j for j in adjacent[c] if j not in me and j!=nitrogen and nodes[j].get('Element')=='6']
        if len(me)!=2 or len(cn)!=1:return 0
        end=[j for j in adjacent[cn[0]] if j!=c and nodes[j].get('Element')=='7' and edges.get(frozenset((j,cn[0])))=='3']
        if len(end)!=1:return 0
        centers.append(c);methyls.append(sorted(me,key=lambda i:int(i)));nitriles.append((cn[0],end[0]))
    old={i:tuple(map(float,n.get('p').split())) for i,n in nodes.items()};unit=math.dist(old[first],old[second]);u=math.sqrt(3)/2
    c,d=centers;m,n=methyls;(a,b),(e,f)=nitriles
    positions={c:(0,0),first:(u,.5),second:(u,1.5),d:(2*u,2),m[0]:(-.5,-u),m[1]:(.5,-u),a:(-u,.5),b:(-2*u,1),n[0]:(2*u+.5,2-u),n[1]:(2*u+1,2),e:(2*u,3),f:(2*u,4)}
    changed=any(math.dist(old[i],(x*unit,y*unit))>1e-6 for i,(x,y) in positions.items())
    if changed:
        for i,(x,y) in positions.items():nodes[i].set('p',f'{x*unit:.9f} {y*unit:.9f}')
    return int(changed)
