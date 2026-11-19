#pragma once
inline void bondHydrogenCheck(const std::string& dir){
    CreateDirectoryW(W(dir).c_str(),0);int cases=0;
    auto require=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    // Real session transactions: diazo N, oxonium, editable charge and radicals.
    for(int element:{7,8}){
        Session v;ObjectId a,b,c,ab,ac;
        v.doc.addAtom(Vec2(),element,&a);v.doc.addAtom(Vec2(-30,0),6,&b);v.doc.addAtom(Vec2(30,0),7,&c);
        v.doc.addBond(a,b,&ab);v.doc.addBond(a,c,&ac);
        if(element==7)v.doc.setBondOrder(ab,BondOrder::Double);
        v.refresh();auto before=v.doc.digest();
        require(v.submit(chemistry::setBondOrderWithRefreshCommand(ac,BondOrder::Double),"化学键级").ok(),"form hypervalent N/O");
        require(v.doc.atoms().get(a)->semantics.charge==1&&v.doc.atoms().get(a)->semantics.implicitHydrogens==0,"N four / O three automatically positive without H");
        require(!studio::validation::overValent(v.doc,*v.doc.atoms().get(a)),"inferred positive charge resolves valence warning");
        auto after=v.doc.digest();require(v.undo().ok()&&v.doc.digest()==before&&v.undo(true).ok()&&v.doc.digest()==after,"autocharge undo redo atomic");
        require(v.save(dir+"/auto-positive-"+std::to_string(element)+".cdxml",true).ok(),"save inferred charge");
        Session reload;require(reload.open(dir+"/auto-positive-"+std::to_string(element)+".cdxml").ok()&&reload.doc.atoms().get(a)->semantics.charge==1,"inferred charge survives reload");
        require(v.submit(chemistry::setBondOrderWithRefreshCommand(ac,BondOrder::Single),"化学键级").ok()&&v.doc.atoms().get(a)->semantics.charge==0,"lowering valence clears inferred charge");++cases;
    }
    for(int element:{7,8})for(bool text:{false,true}){
        Session v;ObjectId a,b,ab;v.doc.addAtom(Vec2(),element,&a);v.doc.addAtom(Vec2(30,0),6,&b);v.doc.addBond(a,b,&ab);
        const int neutral=element==7?2:1;
        if(text){v.doc.setAtomLabel(a,element==7?"NH2":"OH");auto sem=v.doc.atoms().get(a)->semantics;sem.implicitHydrogens=neutral;v.doc.setAtomSemantics(a,sem);}
        v.refresh();auto before=v.doc.digest();
        require(v.submit(chemistry::setChargeCommand({a},-1),"原子电荷").ok(),"apply heteroatom negative charge");
        require(chemistry::refreshAtomDerived(v.doc,a,{}).newHydrogenCount==neutral-1,"negative charge removes attached H");
        require(v.undo().ok()&&v.doc.digest()==before,"charge restores H on undo");
        require(v.submit(chemistry::setChargeCommand({a},1),"原子电荷").ok()&&chemistry::refreshAtomDerived(v.doc,a,{}).newHydrogenCount==neutral+1,"positive charge adds H");
        require(v.submit(chemistry::setRadicalCommand({a},int(Radical::Doublet)),"自由基").ok()&&chemistry::refreshAtomDerived(v.doc,a,{}).newHydrogenCount==neutral,"radical cation consumes one H");
        require(v.submit(chemistry::setRadicalCommand({a},0),"自由基").ok()&&chemistry::refreshAtomDerived(v.doc,a,{}).newHydrogenCount==neutral+1,"removing radical restores H");
        require(v.submit(chemistry::setChargeCommand({a},0),"原子电荷").ok()&&chemistry::refreshAtomDerived(v.doc,a,{}).newHydrogenCount==neutral,"neutralizing restores H count");
        require(v.submit(chemistry::setRadicalCommand({a},int(Radical::Doublet)),"自由基").ok()&&chemistry::refreshAtomDerived(v.doc,a,{}).newHydrogenCount==neutral-1,"neutral radical consumes H");++cases;
    }
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
    // Existing OH/HO labels must behave the same as newly typed hydroxyls.
    for(bool legacy:{false,true})for(bool constrained:{false,true})for(bool literal:{false,true}){
        Session v;
        const double length=24;
        v.doc.setDefaultBondLength(length);
        const Vec2 origin(0,0),oxygen(length,0);
        ObjectId a,o,c,z,bond;
        v.doc.addAtom(origin,6,&a);
        v.doc.addAtom(oxygen,legacy?6:8,&o);
        v.doc.addAtom(Vec2(24,-24),6,&c);
        v.doc.addAtom(Vec2(0,-24),6,&z);
        v.doc.addBond(a,z,&bond);v.doc.addBond(z,c,&bond);v.doc.addBond(c,o,&bond);
        auto sem=v.doc.atoms().alive(o)->semantics;
        sem.showLabel=true;sem.implicitHydrogens=legacy?-1:0;
        sem.nodeType=uint8_t(legacy?NodeType::Nickname:NodeType::Element);
        v.doc.setAtomSemantics(o,sem);v.doc.setAtomLabel(o,"HO");
        if(legacy)v.doc.extensions(ObjectRef(ObjectKind::Atom,o)).set("studio.nickname.fragment","legacy OH");
        if(literal)v.doc.extensions(ObjectRef(ObjectKind::Atom,o)).set("pst.attr.MoleculeStudioLiteralLabel","1");
        v.fixedLen=true;v.fixedAngle=true;
        v.refresh();auto view=v.view();view.scale=3;v.setView(view);
        const auto before=v.doc.digest();
        v.activateCommand("chooseBondTool_Solid");
        const Vec2 release=constrained?Vec2(44,0):oxygen;
        require(v.gesture(PointerPhase::Down,origin).ok()&&v.gesture(PointerPhase::Move,release).ok(),"preview hydroxyl ring closure");
        require(v.bondSnap.joined&&v.bondSnap.target==ObjectRef(ObjectKind::Atom,o)&&v.bondSnap.endpoint==oxygen,"ring closure uses exact oxygen coordinates");
        require(v.gesture(PointerPhase::Up,release).ok()&&v.doc.stats().aliveAtoms==4&&v.doc.bondBetween(a,o)!=kInvalidObjectId,"ring closure reuses oxygen rather than adding an endpoint");
        const auto* atom=v.doc.atoms().alive(o);
        require(atom->element==8&&atom->semantics.nodeType==uint8_t(NodeType::Element)&&atom->semantics.implicitHydrogens==0&&v.doc.atomLabel(o)=="O","two bonds replace the condensed hydroxyl H");
        const auto after=v.doc.digest();
        v.gesture(PointerPhase::Down,origin);v.gesture(PointerPhase::Move,oxygen);
        require(v.gesture(PointerPhase::Up,oxygen).code==ErrorCode::DuplicateBond&&v.doc.digest()==after,"duplicate bond reports a failure without changing the drawing");
        require(v.undo().ok()&&v.doc.digest()==before,"undo restores the original hydroxyl label and the open ring");
        ++cases;
    }
    {Session v;ObjectId o,c1,c2,bond;
     v.doc.addAtom(Vec2(),8,&o);v.doc.addAtom(Vec2(30,0),6,&c1);v.doc.addAtom(Vec2(-30,0),6,&c2);
     v.doc.setAtomLabel(o,"HO");v.doc.addBond(o,c1,&bond);v.doc.addBond(o,c2,&bond);
     require(studio::validation::overValent(v.doc,*v.doc.atoms().alive(o)),"pre-existing invalid HO with two bonds receives a valence warning");++cases;}
    {
        Session old;
        ObjectId stale,terminal,charged,explicitH,abbrev,left,right,h,bond;
        old.doc.addAtom(Vec2(0,0),6,&stale);
        old.doc.addAtom(Vec2(0,30),8,&terminal);
        old.doc.addAtom(Vec2(0,60),8,&charged);
        old.doc.addAtom(Vec2(0,90),8,&explicitH);
        old.doc.addAtom(Vec2(0,120),8,&abbrev);
        old.doc.addAtom(Vec2(-30,15),6,&left);
        old.doc.addAtom(Vec2(30,15),6,&right);
        old.doc.addAtom(Vec2(-25,90),1,&h);
        for(auto id:{stale,terminal,charged,explicitH,abbrev})old.doc.addBond(id,right,&bond);
        for(auto id:{stale,charged,abbrev})old.doc.addBond(id,left,&bond);
        old.doc.addBond(explicitH,h,&bond);
        auto sem=old.doc.atoms().alive(stale)->semantics;
        sem.nodeType=uint8_t(NodeType::Query);sem.showLabel=true;
        old.doc.setAtomSemantics(stale,sem);old.doc.setAtomLabel(stale,"HO");
        old.doc.extensions(ObjectRef(ObjectKind::Atom,stale)).set("pst.attr.MoleculeStudioLiteralLabel","1");
        old.doc.setAtomLabel(terminal,"OH");
        old.doc.setAtomLabel(explicitH,"OH");
        old.doc.setAtomLabel(abbrev,"OMe");
        sem=old.doc.atoms().alive(charged)->semantics;
        sem.charge=1;sem.implicitHydrogens=1;
        old.doc.setAtomSemantics(charged,sem);old.doc.setAtomLabel(charged,"O");
        const auto path=dir+"/legacy-hydroxyl.r62doc";
        require(old.save(path).ok(),"save stale structure");
        Session reload;
        require(reload.open(path).ok()&&reload.dirty,"opening stale native structure records repair");
        const auto* atom=reload.doc.atoms().alive(stale);
        require(atom->element==8&&atom->semantics.nodeType==uint8_t(NodeType::Element)&&
                atom->semantics.implicitHydrogens==0&&reload.doc.atomLabel(stale)=="O"&&
                reload.doc.bondBetween(stale,left)!=kInvalidObjectId&&reload.doc.bondBetween(stale,right)!=kInvalidObjectId,
                "old HO query label becomes oxygen and retains both original bonds");
        require(reload.doc.atomLabel(terminal)=="OH"&&reload.doc.atomLabel(abbrev)=="OMe"&&
                reload.doc.atoms().alive(charged)->semantics.implicitHydrogens==1&&
                reload.doc.bondBetween(explicitH,h)!=kInvalidObjectId,"terminal hydroxyl, abbreviation, oxonium and explicit H survive");
        const auto clean=dir+"/repaired-hydroxyl.r62doc";
        require(reload.save(clean).ok()&&reload.open(clean).ok()&&!reload.dirty,"opening repaired structure makes no further changes");
        ++cases;
    }
    {
        const auto path=dir+"/legacy-hydroxyl.cdxml";
        std::ofstream(path)<<R"(<CDXML><page><fragment>
            <n id="1" p="60 60" NodeType="GenericNickname"><t><s>HO</s></t></n>
            <n id="2" p="80 40"/><n id="3" p="80 80"/>
            <n id="4" p="100 60" Element="8" NumHydrogens="1"><t><s>O</s></t></n>
            <b id="5" B="1" E="2"/><b id="6" B="1" E="3"/>
            <b id="7" B="4" E="2"/><b id="8" B="4" E="3"/>
            </fragment></page></CDXML>)";
        Session reload;
        require(reload.open(path).ok()&&reload.dirty,"old CDXML is repaired on open");
        for(auto id:{ObjectId(0),ObjectId(3)}){
            const auto* atom=reload.doc.atoms().alive(id);
            require(atom&&atom->element==8&&atom->semantics.implicitHydrogens==0&&
                    reload.doc.atomLabel(id)=="O"&&reload.doc.incidentBonds(id).size()==2,
                    "both condensed HO and stale oxygen hydrogen counts are repaired after reading all bonds");
        }
        ++cases;
    }
    {
        Session v;
        v.doc.setDefaultBondLength(30);
        ObjectId center;
        v.doc.addAtom(Vec2(),6,&center);
        const Vec2 ends[]={Vec2(-30,0),Vec2(15,-26),Vec2(15,26),Vec2(-15,26),Vec2(0,-30)};
        for(int i=0;i<5;++i){
            ObjectId other;
            v.doc.addAtom(ends[i],6,&other);
            BondRecord b;
            b.beginAtom=center;b.endAtom=other;b.order=BondOrder::Single;
            if(i==0)b.display=BondDisplay::Wedge;
            if(i==1)b.display=BondDisplay::WedgeHash;
            if(i==4){b.order=BondOrder::Any;b.semantics=BondSemantics::Query;}
            ObjectId bond;
            require(v.doc.addBondRecord(b,&bond).ok(),"create four known bonds and one query bond");
        }
        v.refresh();
        auto view=v.view();view.scale=5;v.setView(view);
        require(v.valenceWarnings().empty(),"four known bonds do not warn based on an ambiguous edge");
        v.activateCommand("chooseBondTool_Solid");
        require(v.gesture(PointerPhase::Down,Vec2()).ok()&&v.gesture(PointerPhase::Move,Vec2(30,0)).ok()&&
                v.gesture(PointerPhase::Up,Vec2(30,0)).ok(),"manual fifth covalent bond is drawn");
        auto assessed=studio::validation::assess(v.doc,*v.doc.atoms().alive(center));
        require(assessed.used==5&&assessed.limit==4&&v.valenceWarnings().count(center)==1,"known covalent excess survives an incident query bond");
        auto overlay=v.overlay();
        require(std::any_of(overlay.scene().primitives().begin(),overlay.scene().primitives().end(),[](const render::Primitive& p){
            return p.style.stroke.enabled&&p.style.stroke.rgba==0xE02020FF&&p.points.size()==5;
        }),"fifth bond immediately adds the red frame to the canvas overlay");
        require(v.undo().ok()&&v.valenceWarnings().empty(),"undo immediately clears the warning");
        require(v.undo(true).ok()&&v.valenceWarnings().count(center)==1,"redo immediately restores the warning");
        ++cases;
    }
    for(bool charged:{false,true})for(const auto* tag:{"化学键级","化学键属性","hotspot bond order"}){
        Session v;ObjectId c,o,edge;
        v.doc.addAtom(Vec2(0,30),6,&c);v.doc.addAtom(Vec2(0,0),8,&o);v.doc.addBond(c,o,&edge);
        v.doc.setBondOrder(edge,BondOrder::Double);
        auto sem=v.doc.atoms().alive(o)->semantics;sem.implicitHydrogens=0;sem.charge=charged?1:0;v.doc.setAtomSemantics(o,sem);
        v.doc.setAtomLabel(o,"O");v.refresh();
        require(v.submit(chemistry::setBondOrderWithRefreshCommand(edge,BondOrder::Single),tag).ok(),"decrease carbonyl bond order");
        require(v.doc.atoms().alive(o)->semantics.implicitHydrogens==(charged?0:1),"only neutral carbonyl oxygen gains hydrogen");
        if(!charged){
            auto path=dir+"/hydroxyl-"+std::to_string(cases)+".cdxml";require(v.save(path,true).ok(),"save reduced carbonyl");
            Session reload;require(reload.open(path).ok()&&reload.doc.atoms().alive(o)->semantics.implicitHydrogens==1,"reduced carbonyl retains H in CDXML");
            require(v.submit(chemistry::setBondOrderWithRefreshCommand(edge,BondOrder::Double),tag).ok()&&
                    v.doc.atoms().alive(o)->semantics.implicitHydrogens==0,"restoring double bond removes the added H");
        }
        ++cases;
    }
    std::ofstream(dir+"/hydrogen-checks.json")<<"{\"passed\":true,\"cases\":"<<cases<<",\"gesture\":true,\"undo_redo\":true,\"cdxml\":true,\"bond_order\":true,\"import_preserved\":true,\"abbreviation_preserved\":true}";
}
