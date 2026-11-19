"""Compact crowded drawings, retaining complete expandable ligand definitions."""
import copy,math
import xml.etree.ElementTree as ET
from rdkit import Chem
from rdkit.Chem import rdDepictor

WHOLE={
    'Pd(PPh3)4':'Pd(PPh3)4',
    'PdCl2(PPh3)2':'PdCl2(PPh3)2',
    'RhCl(PPh3)3':'RhCl(PPh3)3',
    'Pd(dba)2':'Pd(dba)2',
    'Pd2(dba)3':'Pd2(dba)3',
}

def compact(root,mol,record):
    fragment=root.find('./page/fragment');label=record.get('compact_label') or next((WHOLE[a] for a in record['alias'].split() if a in WHOLE),None)
    if label:
        hidden=copy.deepcopy(fragment);hidden.set('id','900002');fragment.clear();fragment.set('id','2')
        atom=ET.SubElement(fragment,'n',id='900001',p='0 0',NodeType='Nickname',LabelDisplay='Shown')
        atom.append(hidden);text=ET.SubElement(atom,'t');ET.SubElement(text,'s',face='0' if record.get('compact_label') else '96').text=label
        return dict(kind='coordination-formula',label=label)
    rings=list(mol.GetRingInfo().AtomRings());members={i:sum(i in ring for ring in rings) for i in range(mol.GetNumAtoms())};groups=[]
    for ring in rings:
        if len(ring)!=6 or any(mol.GetAtomWithIdx(i).GetAtomicNum()!=6 or mol.GetAtomWithIdx(i).GetIsotope() or members[i]!=1 for i in ring):continue
        cycle=set(ring);inside=[b for b in mol.GetBonds() if b.GetBeginAtomIdx() in cycle and b.GetEndAtomIdx() in cycle]
        if len(inside)!=6 or sorted(int(b.GetBondTypeAsDouble()) for b in inside)!=[1,1,1,2,2,2]:continue
        outside=[b for b in mol.GetBonds() if (b.GetBeginAtomIdx() in cycle)!=(b.GetEndAtomIdx() in cycle)]
        if len(outside)!=1 or outside[0].GetBondType()!=Chem.BondType.SINGLE:continue
        b=outside[0];anchor=b.GetBeginAtomIdx() if b.GetBeginAtomIdx() in cycle else b.GetEndAtomIdx();other=b.GetOtherAtomIdx(anchor)
        groups.append((cycle,anchor,other,b.GetIdx()))
    if len(groups)<3:return None
    count=len(groups);labels={anchor:'Ph' for _,anchor,_,_ in groups}
    # Diphenylphosphino arms can point back over a folded biaryl scaffold.
    # Keep that scaffold visible and use one PPh2 label for each complete arm.
    for atom in mol.GetAtoms():
        p=atom.GetIdx();arms=[g for g in groups if g[2]==p]
        if atom.GetAtomicNum()!=15 or atom.GetDegree()!=3 or len(arms)!=2:continue
        hidden={p}.union(*(g[0] for g in arms));outside=[b for b in atom.GetBonds() if b.GetOtherAtomIdx(p) not in hidden]
        if len(outside)!=1:continue
        b=outside[0];groups=[g for g in groups if g not in arms];groups.append((hidden,p,b.GetOtherAtomIdx(p),b.GetIdx()));labels[p]='PPh2'
    nodes={int(n.get('id')):n for n in fragment.findall('n')};bonds=list(fragment.findall('b'));removed=set().union(*(cycle-{anchor} for cycle,anchor,_,_ in groups))
    editable=Chem.RWMol(mol)
    for _,anchor,_,_ in groups:
        atom=editable.GetAtomWithIdx(anchor);atom.SetAtomicNum(0);atom.SetNoImplicit(True);atom.SetNumExplicitHs(0)
    for i in sorted(removed,reverse=True):editable.RemoveAtom(i)
    small=editable.GetMol();small.UpdatePropertyCache(strict=False);Chem.GetSymmSSSR(small)
    rdDepictor.Compute2DCoords(small,forceRDKit=True,useRingTemplates=True)
    kept=[i for i in range(mol.GetNumAtoms()) if i not in removed];conf=small.GetConformer()
    for j,i in enumerate(kept):
        p=conf.GetAtomPosition(j);nodes[10+i].set('p',f'{p.x*22/1.5:.9f} {-p.y*22/1.5:.9f}')
    next_id=10000
    for cycle,anchor,other,outer_bond in groups:
        outer=nodes[10+anchor];hidden=ET.Element('fragment',id=str(next_id));next_id+=1;mapping={}
        # The original full ring coordinates and chemistry remain authored data.
        for i in sorted(cycle):mapping[10+i]=str(next_id);next_id+=1
        original=mol.GetConformer()
        for i in sorted(cycle):
            atom=copy.deepcopy(nodes[10+i]);atom.set('id',mapping[10+i]);p=original.GetAtomPosition(i);atom.set('p',f'{p.x*22/1.5:.9f} {-p.y*22/1.5:.9f}');hidden.append(atom)
        external=str(next_id);next_id+=1;p=original.GetAtomPosition(other)
        ET.SubElement(hidden,'n',id=external,NodeType='ExternalConnectionPoint',p=f'{p.x*22/1.5:.9f} {-p.y*22/1.5:.9f}')
        for b in bonds:
            a,z=int(b.get('B')),int(b.get('E'))
            if a in mapping and z in mapping:
                new=copy.deepcopy(b);new.set('id',str(next_id));next_id+=1;new.set('B',mapping[a]);new.set('E',mapping[z]);hidden.append(new)
        ET.SubElement(hidden,'b',id=str(next_id),B=mapping[10+anchor],E=external,Order='1');next_id+=1
        outer.attrib.pop('Element',None);outer.attrib.pop('NumHydrogens',None);outer.set('NodeType','Nickname');outer.set('BondOrdering',str(1000+outer_bond));outer.append(hidden)
        text=ET.SubElement(outer,'t');ET.SubElement(text,'s',face='96').text=labels[anchor]
    for i in removed:fragment.remove(nodes[10+i])
    for b in bonds:
        if int(b.get('B'))-10 in removed or int(b.get('E'))-10 in removed:fragment.remove(b)
    return dict(kind='terminal-phenyl-abbreviations',phenyl_groups=count,diphenylphosphino_groups=sum(labels[g[1]]=='PPh2' for g in groups))
