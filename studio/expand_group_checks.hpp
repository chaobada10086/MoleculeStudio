#pragma once
inline void expandGroupCheck(App& a,const std::string& dir){
 auto check=[](bool b,const char* why){if(!b)throw std::runtime_error(why);};
 ObjectId n,ph,b;a.s.doc.addAtom(Vec2(),7,&n);a.s.doc.addAtom(Vec2(14.4,0),6,&ph);a.s.doc.addBond(n,ph,&b);nicknames::apply(a.s.doc,ph,"Ph");a.s.refresh();a.s.selected=a.s.all();auto before=a.s.doc.digest();
 a.execute(Button("","","","@expandGroups"));check(a.expandPending,"first click expands and arms cleanup");check(a.s.doc.stats().aliveAtoms==7,"Ph expands six ring atoms");auto expanded=a.s.doc.digest();check(a.s.undo().ok()&&a.s.doc.digest()==before,"expansion atomic undo");check(a.s.undo(true).ok()&&a.s.doc.digest()==expanded,"expansion redo");
 a.execute(Button("","","","@expandGroups"));check(!a.expandPending&&a.s.doc.stats().aliveAtoms==7,"second click cleans");a.s.save(dir+"/ph-expanded.cdxml",true);
 a.expandPending=true;a.expandDigest=a.s.doc.digest();a.expandDocument=a.s.doc.id();a.expandSelection=a.s.selected;a.execute(Button("","","","@fit"));check(!a.expandPending,"other action cancels pending");
 auto digest=a.s.doc.digest();a.s.selected.clear();a.expandPending=true;a.execute(Button("","","","@expandGroups"));check(!a.expandPending&&a.s.doc.digest()==digest,"empty selection preserves document");
 App bad;ObjectId x;bad.s.doc.addAtom(Vec2(),6,&x);auto sem=bad.s.doc.atoms().get(x)->semantics;sem.nodeType=uint8_t(NodeType::Nickname);bad.s.doc.setAtomSemantics(x,sem);bad.s.doc.setAtomLabel(x,"UnknownGroup");bad.s.refresh();bad.s.selected=bad.s.all();auto old=bad.s.doc.digest();bad.expandGroups();check(bad.s.doc.digest()==old&&!bad.expandPending,"unknown abbreviation atomic failure");
 std::ofstream(dir+"/expand-group-checks.json")<<"{\"passed\":true,\"button\":true,\"real_atoms\":true,\"undo_redo\":true,\"second_cleanup\":true,\"action_cancels\":true,\"empty_selection\":true,\"unknown_atomic\":true}";
}
