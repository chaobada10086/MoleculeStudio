#pragma once
inline void bondHydrogenCheck(const std::string& dir){
    CreateDirectoryW(W(dir).c_str(),0);int cases=0;
    auto require=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    for(int element:{6,7,8,15,16})for(bool text:{false,true}){
        Session v;ObjectId a,b,ab;v.doc.addAtom(Vec2(0,0),element,&a);v.doc.addAtom(Vec2(-30,0),6,&b);v.doc.addBond(a,b,&ab);
        auto sem=v.doc.atoms().get(a)->semantics;sem.showLabel=true;sem.implicitHydrogens=text?-1:1;v.doc.setAtomSemantics(a,sem);
        v.doc.setAtomLabel(a,std::string(core::elementSymbol(element))+(text?"H":""));v.refresh();const auto before=v.doc.digest();
        v.activateCommand("chooseBondTool_Solid");v.gesture(PointerPhase::Down,Vec2(0,0));auto e=v.gesture(PointerPhase::Up,Vec2(0,0));
        require(e.ok(),"real bond gesture succeeds");require(v.doc.stats().aliveBonds==2,"gesture adds bond from labeled atom");
        require(v.doc.atoms().get(a)->semantics.implicitHydrogens==0,"new single bond consumes elemental H");
        require(v.doc.atomLabel(a)==core::elementSymbol(element),"stale H text removed");const auto after=v.doc.digest();
        require(v.undo().ok()&&v.doc.digest()==before,"one undo restores bond and H");require(v.undo(true).ok()&&v.doc.digest()==after,"redo consumes H again");
        auto path=dir+"/case-"+std::to_string(cases)+".cdxml";require(v.save(path,true).ok(),"save updated H");Session reload;require(reload.open(path).ok()&&reload.doc.atoms().get(a)->semantics.implicitHydrogens==0,"CDXML updated H roundtrip");++cases;
    }
    {Session v;ObjectId a,b,ab;v.doc.addAtom(Vec2(),7,&a);v.doc.addAtom(Vec2(30,0),6,&b);v.doc.addBond(a,b,&ab);auto sem=v.doc.atoms().get(a)->semantics;sem.implicitHydrogens=2;v.doc.setAtomSemantics(a,sem);v.doc.setAtomLabel(a,"NH2");v.refresh();
     require(v.submit(chemistry::setBondOrderWithRefreshCommand(ab,BondOrder::Double),"化学键级").ok(),"increase bond order");require(v.doc.atoms().get(a)->semantics.implicitHydrogens==1&&v.doc.atomLabel(a)=="N","NH2 becomes NH");++cases;}
    {Session v;ObjectId a,b;v.doc.addAtom(Vec2(),8,&a);auto sem=v.doc.atoms().get(a)->semantics;sem.implicitHydrogens=1;v.doc.setAtomSemantics(a,sem);v.doc.setAtomLabel(a,"OH");v.doc.addAtom(Vec2(30,0),6,&b);ObjectId bond;v.doc.addBond(a,b,&bond);
     require(v.doc.atoms().get(a)->semantics.implicitHydrogens==1&&v.doc.atomLabel(a)=="OH","raw import/paste building preserves source H");++cases;}
    {Session v;ObjectId a,b,ab;v.doc.addAtom(Vec2(),8,&a);v.doc.addAtom(Vec2(-30,0),6,&b);v.doc.addBond(a,b,&ab);v.doc.setAtomLabel(a,"OMe");v.refresh();v.activateCommand("chooseBondTool_Solid");v.gesture(PointerPhase::Down,Vec2());v.gesture(PointerPhase::Up,Vec2());require(v.doc.atomLabel(a)=="OMe","opaque abbreviation untouched");++cases;}
    std::ofstream(dir+"/hydrogen-checks.json")<<"{\"passed\":true,\"cases\":"<<cases<<",\"gesture\":true,\"undo_redo\":true,\"cdxml\":true,\"bond_order\":true,\"import_preserved\":true,\"abbreviation_preserved\":true}";
}
