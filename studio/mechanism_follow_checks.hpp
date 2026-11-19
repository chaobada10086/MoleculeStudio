#pragma once
static void checkMechanismFollow(App& app,const std::string& dir){
    auto require=[](bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);};auto& s=app.s;
    s.cancel();s.doc=Document();s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());s.doc.setDefaultBondLength(30);
    ObjectId nu,c,br,tip,cb,ct;s.doc.addAtom(Vec2(70,100),7,&nu);s.doc.addAtom(Vec2(180,135),6,&c);s.doc.addAtom(Vec2(180,165),35,&br);s.doc.addAtom(Vec2(180,105),6,&tip);s.doc.addBond(c,br,&cb);s.doc.addBond(c,tip,&ct);
    auto sem=s.doc.atoms().alive(nu)->semantics;sem.charge=-1;s.doc.setAtomSemantics(nu,sem);
    auto arrow=[&](ObjectRef from,ObjectRef to){Vec2 a,b;arrows::mechanismAnchorPoint(s.doc,from,&a);arrows::mechanismAnchorPoint(s.doc,to,&b);auto p=arrows::resolvePreset(49,a,b,.6);p.mechanism=true;p.electronSource=from;p.electronTarget=to;arrows::routeMechanism(s.doc,p);ObjectId id;require(arrows::addArrow(s.doc,p,.6,&id).ok(),"add bound arrow");return id;};
    auto attack=arrow(ObjectRef(ObjectKind::Atom,nu),ObjectRef(ObjectKind::Atom,c));auto leaving=arrow(ObjectRef(ObjectKind::Bond,cb),ObjectRef(ObjectKind::Atom,br));
    ObjectId ordinary;auto reaction=arrows::resolvePreset(1,Vec2(260,135),Vec2(310,135),.6);arrows::addArrow(s.doc,reaction,.6,&ordinary);
    auto read=[&](ObjectId id){arrows::ArrowParams p;bool ok=false;arrows::readArrow(s.doc,id,&p,&ok);require(ok,"read arrow after movement");return p;};
    s.refresh();auto old=read(attack),oldLeave=read(leaving);auto digest=s.doc.digest();const Vec2 delta(30,25);
    require(s.move({ObjectRef(ObjectKind::Atom,nu)},delta).ok(),"move only nucleophile");auto moved=read(attack);
    require(length(moved.begin-old.begin)>15&&moved.electronSource==old.electronSource&&moved.electronTarget==old.electronTarget,"source follows its atom and retains both bindings");
    require(length(moved.end-old.end)<12&&read(leaving).begin==oldLeave.begin,"stationary target stays docked and unrelated arrow stays fixed");
    require(s.undo().ok()&&s.doc.digest()==digest,"single-end follow undoes atomically");
    std::vector<ObjectRef> atoms={ObjectRef(ObjectKind::Atom,nu),ObjectRef(ObjectKind::Atom,c),ObjectRef(ObjectKind::Atom,br),ObjectRef(ObjectKind::Atom,tip)};
    require(s.move(atoms,delta).ok(),"move both components without selecting arrows");moved=read(attack);
    require(length(moved.begin-old.begin-delta)<1e-8&&length(moved.end-old.end-delta)<1e-8&&moved.route==old.route&&moved.arcSagitta==old.arcSagitta,"two-end move translates the complete original curve");
    require(length(read(leaving).begin-oldLeave.begin-delta)<1e-8&&length(read(leaving).end-oldLeave.end-delta)<1e-8,"bond-to-atom arrow follows its component");
    require(read(ordinary).begin==reaction.begin&&read(ordinary).end==reaction.end,"ordinary reaction arrow remains independent");
    require(s.undo().ok()&&s.doc.digest()==digest,"both-end follow has exact undo");
    auto withArrow=atoms;withArrow.push_back(ObjectRef(ObjectKind::Arrow,attack));require(s.move(withArrow,delta).ok(),"move selected arrow with its components");
    require(length(read(attack).begin-old.begin-delta)<1e-8,"explicitly selected bound arrow moves only once");
    auto after=s.doc.digest();require(s.undo().ok()&&s.doc.digest()==digest&&s.undo(true).ok()&&s.doc.digest()==after,"follow redo is exact");
    require(s.save(dir+"/mechanism-follow.cdxml",true).ok(),"save moved arrow bindings");Session reopened;require(reopened.open(dir+"/mechanism-follow.cdxml").ok(),"reopen followed arrows");
    s.selected.clear();s.fit();app.invalidate();app.screenshot(dir+"/mechanism-follow.png");
    s.doc=Document();s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());
    ObjectId a,b,bond;s.doc.addAtom(Vec2(30,30),6,&a);s.doc.addAtom(Vec2(60,50),8,&b);s.doc.addBond(a,b,&bond);auto id=arrow(ObjectRef(ObjectKind::Bond,bond),ObjectRef(ObjectKind::Atom,b));
    s.refresh();s.activateCommand("chooseSelectionTool_Marquee");s.selected=s.all();s.selected.erase(std::remove_if(s.selected.begin(),s.selected.end(),[](ObjectRef r){return r.kind==ObjectKind::Arrow;}),s.selected.end());s.dragFixedLength=false;
    auto beforeDrag=s.doc.digest();auto base=read(id);auto v=s.view();v.scale=2;s.setView(v);
    require(s.gesture(PointerPhase::Down,Vec2(30,30),0).ok()&&s.gesture(PointerPhase::Move,Vec2(50,45),0).ok(),"preview bound-component drag");
    auto arrowSlots=[&](ObjectId arrowId){
        std::vector<size_t> slots;
        for(size_t i=0;i<s.scene.size();++i)
            if(s.scene.primitives()[i].source==ObjectRef(ObjectKind::Arrow,arrowId))slots.push_back(i);
        require(!slots.empty(),"arrow has preview geometry");
        return slots;
    };
    for(size_t i:arrowSlots(id))
        require(length(s.displayScene().primitives()[i+2].bounds.min-s.scene.primitives()[i].bounds.min-Vec2(20,15))<1e-8,"cached arrow translates in live preview");
    require(s.doc.digest()==beforeDrag,"rigid preview preserves original document");
    s.cancel();require(s.doc.digest()==beforeDrag,"cancel restores the molecule and its arrows");
    s.selected={ObjectRef(ObjectKind::Atom,a),ObjectRef(ObjectKind::Atom,b),ObjectRef(ObjectKind::Bond,bond)};
    s.gesture(PointerPhase::Down,Vec2(30,30),0);s.gesture(PointerPhase::Move,Vec2(50,45),0);s.gesture(PointerPhase::Move,Vec2(55,48),0);require(s.gesture(PointerPhase::Up,Vec2(55,48),0).ok(),"commit repeated live drag");
    require(length(read(id).begin-base.begin-Vec2(25,18))<1e-8,"preview and commit do not accumulate drift");require(s.undo().ok()&&s.doc.digest()==beforeDrag,"live drag is one undo");
    s.dragFixedLength=true;
    s.doc=Document();s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());ObjectId boron,donor;s.doc.addAtom(Vec2(50,65),5,&boron);s.doc.addAtom(Vec2(130,25),6,&donor);auto negative=s.doc.atoms().alive(boron)->semantics;negative.charge=-1;negative.implicitHydrogens=0;s.doc.setAtomSemantics(boron,negative);
    auto charge=arrows::mechanismChargeBox(s.doc,*s.doc.atoms().alive(boron));auto p=arrows::resolvePreset(49,Vec2(130,25),(charge.min+charge.max)*.5,.6);p.mechanism=true;p.electronSource=ObjectRef(ObjectKind::Atom,donor);p.electronTarget=ObjectRef(ObjectKind::Atom,boron);p.targetVisualAnchor=2;arrows::routeMechanism(s.doc,p);ObjectId chargeArrow;require(arrows::addArrow(s.doc,p,.6,&chargeArrow).ok(),"charge-bound arrow");s.refresh();auto chargeBefore=s.doc.digest();
    require(s.move({ObjectRef(ObjectKind::Atom,boron)},Vec2(18,12)).ok(),"move charged boron with bound arrow");auto routed=read(chargeArrow);auto box=arrows::mechanismChargeBox(s.doc,*s.doc.atoms().alive(boron));require(arrows::mechanismBoxDistance(box,routed.end)>=routed.head.width*.5+.5,"moved arrowhead clears the minus glyph");require(routed.electronTarget==ObjectRef(ObjectKind::Atom,boron),"visual clearance preserves chemical binding");
    s.selected.clear();s.fit();app.invalidate();app.screenshot(dir+"/moved-charge-arrow.png");require(s.undo().ok()&&s.doc.digest()==chargeBefore,"charge clearance moves undo atomically");
    s.selected={ObjectRef(ObjectKind::Atom,donor)};
    auto slots=arrowSlots(chargeArrow);const auto serial=s.serial;
    require(s.gesture(PointerPhase::Down,Vec2(130,25),0).ok(),"begin single-end drag");
    for(int x=1;x<=60;++x){
        require(s.gesture(PointerPhase::Move,Vec2(140+x,35),0).ok(),"move single-end preview");
        for(auto i:slots){
            const auto& primitive=s.displayScene().primitives()[i+2];
            require(primitive.points.empty()&&primitive.subpaths.empty()&&!primitive.style.stroke.enabled&&!primitive.style.fill.enabled,"only deferred path is masked");
        }
    }
    require(s.doc.digest()==chargeBefore&&s.serial==serial,"sixty moves preserve document and scene generation");
    s.cancel();require(s.doc.digest()==chargeBefore&&read(chargeArrow).electronTarget==ObjectRef(ObjectKind::Atom,boron),"cancel single-end drag restores original");
    s.selected={ObjectRef(ObjectKind::Atom,donor)};
    s.gesture(PointerPhase::Down,Vec2(130,25),0);s.gesture(PointerPhase::Move,Vec2(165,35),0);
    require(s.gesture(PointerPhase::Up,Vec2(165,35),0).ok()&&length(read(chargeArrow).begin-p.begin)>20,"release reroutes hidden arrow");
    const auto committed=s.doc.digest();
    require(s.undo().ok()&&s.doc.digest()==chargeBefore&&s.undo(true).ok()&&s.doc.digest()==committed,"single-end drag undo and redo");
    std::ofstream(dir+"/mechanism-follow-checks.json")<<"{\"single_endpoint\":true,\"rigid_curve_translation\":true,\"bond_capture\":true,\"selected_arrow_no_double_move\":true,\"live_preview\":true,\"cancel\":true,\"no_drift\":true,\"undo_redo\":true,\"save_reopen\":true,\"charge_head_clearance\":true}";
}
