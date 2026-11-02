{
    auto check=[&](int element,int charge,std::vector<int> orders,int hs,bool expected){
        Document d;ObjectId center;d.addAtom(Vec2(),element,&center);
        AtomSemantics sem;sem.charge=charge;sem.implicitHydrogens=hs;d.setAtomSemantics(center,sem);
        for(auto order:orders){ObjectId other,bond;d.addAtom(Vec2(30,0),6,&other);d.addBond(center,other,&bond);if(order==5)d.setBondSemantics(bond,BondSemantics::Aromatic);require(d.setBondOrder(bond,BondOrder(order)).ok(),"set fixture bond order");}
        auto detail="valence rule element="+std::to_string(element)+" charge="+std::to_string(charge)+" bonds="+std::to_string(orders.size())+" H="+std::to_string(hs);
        require(validation::overValent(d,*d.atoms().alive(center))==expected,detail.c_str());
    };
    check(6,0,{1,1,1,1},-1,false);check(6,0,{1,1,1,1,1},-1,true);
    check(6,0,{3,2},-1,true);check(6,0,{3,1},-1,false);
    check(7,0,{1,1,1,1},-1,true);check(7,1,{1,1,1,1},-1,false);
    check(7,1,{1,1,1,1,1},-1,true);check(8,0,{2,1},-1,true);
    check(8,1,{1,1,1},-1,false);check(8,-1,{1,1},-1,true);
    check(5,-1,{1,1,1,1},-1,false);check(6,1,{1,1,1,1},-1,true);
    check(15,0,{1,1,1,1,1},-1,false);check(16,0,{2,2,1,1},-1,false);
    check(16,0,{2,2,2,1},-1,true);check(9,0,{1,1},-1,true);
    check(6,0,{1,1},3,true);check(6,0,{5,5,1},-1,false);
    check(6,0,{5,5,1,1},-1,true);check(6,0,{5,5,5},-1,false);
    check(7,0,{5,5},1,false);check(26,0,{1,1,1,1,1,1},-1,false);
    check(18,0,{1},-1,false); // Legacy Ar aryl placeholder, no explicit label.
    {
        Document d;ObjectId ar,carbon,bond;d.addAtom(Vec2(),18,&ar);d.addAtom(Vec2(30,0),6,&carbon);d.addBond(ar,carbon,&bond);
        d.setAtomLabel(ar,"Ar");require(!validation::overValent(d,*d.atoms().alive(ar)),"Ar aryl label is not argon overvalence");
        AtomSemantics sem;sem.isotope=40;d.setAtomSemantics(ar,sem);
        require(validation::overValent(d,*d.atoms().alive(ar)),"explicit argon isotope remains checked");
    }
    Session v;ObjectId center;v.doc.addAtom(Vec2(),6,&center);
    std::vector<ObjectId> bonds;
    for(int i=0;i<5;++i){ObjectId other,bond;double t=6.283185307*i/5;v.doc.addAtom(Vec2(35*cos(t),35*sin(t)),6,&other);v.doc.addBond(center,other,&bond);bonds.push_back(bond);}
    v.refresh();
    auto markers=[&](){size_t count=0;auto overlay=v.overlay();for(const auto& p:overlay.scene().primitives())if(p.style.stroke.enabled&&p.style.stroke.rgba==0xE02020FF){++count;require(!p.style.fill.enabled&&p.points.size()==5,"warning is a closed hollow square");require(std::abs(v.view().toDevice(p.points[1]).x-v.view().toDevice(p.points[0]).x-7*v.view().scale*v.view().dpiScale)<.001,"warning scales in fixed proportion to molecule");require(std::abs(p.style.strokeWidth-.65)<.001,"stroke scales in document space");require(std::abs((p.points[0].x+p.points[2].x)*.5)<.001&&std::abs((p.points[0].y+p.points[2].y)*.5)<.001,"square centered on erroneous atom");}return count;};
    require(markers()==1,"five-valent carbon gets one red square");
    auto view=v.view();view.scale=3;v.setView(view);require(markers()==1,"warning survives zoom");
    require(v.submit(commands::deleteObjects({ObjectRef(ObjectKind::Bond,bonds.back())}),"fix valence").ok(),"delete excess bond");
    require(markers()==0,"warning clears after repair");require(v.undo().ok()&&markers()==1,"undo restores warning");
    require(v.undo(true).ok()&&markers()==0,"redo clears warning");v.undo();
    require(v.save(testOut+"/valence_warning.cdxml",true).ok(),"warning fixture export");
    Session loaded;require(loaded.open(testOut+"/valence_warning.cdxml").ok(),"reload warning fixture");
    require(validation::overValentAtoms(loaded.doc).size()==1,"imported overvalence detected");
    require(v.submit(validation::setIgnored(center,true),"ignore valence").ok()&&markers()==0,"ignore removes red frame");
    require(v.undo().ok()&&markers()==1,"undo ignore restores frame");
    require(v.undo(true).ok()&&markers()==0,"redo ignore removes frame");
    require(v.save(testOut+"/valence_ignored.r62doc").ok(),"save ignored atom");
    Session ignoredReload;require(ignoredReload.open(testOut+"/valence_ignored.r62doc").ok(),"reload ignored atom");
    require(validation::ignored(ignoredReload.doc,center)&&validation::overValentAtoms(ignoredReload.doc).empty(),"native save retains ignore setting");
    require(v.submit(validation::setIgnored(center,false),"restore valence").ok()&&markers()==1,"restore checking shows error again");
    v.doc.setAtomLabel(center,"Ph");v.refresh();require(markers()==0,"abbreviation not treated as carbon");
    std::ofstream(testOut+"/valence_warning_tests.json")<<"{\"chemical_cases\":22,\"overlay\":true,\"proportional_zoom\":true,\"repair_undo_redo\":true,\"import\":true,\"abbreviation\":true,\"ignore_restore_undo_redo\":true,\"ignore_native_roundtrip\":true}";
}
