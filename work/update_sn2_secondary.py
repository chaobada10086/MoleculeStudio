from pathlib import Path
p=Path('work/sn2_matrix.cpp');s=p.read_text();needle='auto loss=leaving->id;';assert s.count(needle)==1
s=s.replace(needle,needle+'''for(auto bid:s.doc.incidentBonds(center)){auto e=s.doc.bonds().alive(bid);auto n=e->beginAtom==center?e->endAtom:e->beginAtom;auto atom=s.doc.atoms().alive(n);if(atom->element==6&&s.doc.atomLabel(n).empty()&&s.doc.incidentBonds(n).size()==1){check(s.doc.setElement(n,1));auto h=s.doc.atoms().alive(n)->semantics;h.implicitHydrogens=0;check(s.doc.setAtomSemantics(n,h));break;}}''')
p.write_text(s)
