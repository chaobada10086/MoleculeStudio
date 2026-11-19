#pragma once
static void checkDiazo(App& app,const std::string& dir){
    auto& s=app.s;auto require=[](bool ok,const std::string& why){if(!ok)throw std::runtime_error("diazo: "+why);};
    auto fixture=[&](){s.cancel();s.doc=Document();s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());s.doc.setDefaultBondLength(14.4);s.activateCommand("chooseRingTool_Cyclopentane");require(s.gesture(PointerPhase::Down,Vec2()).ok()&&s.gesture(PointerPhase::Up,Vec2()).ok(),"ring fixture");s.refresh();};
    auto verify=[&](){
        require(s.doc.atoms().alive(0)->element==6&&s.doc.incidentBonds(0).size()==3,"original ring carbon retained");
        int cs=0,ns=0;ObjectId middle=kInvalidObjectId,end=kInvalidObjectId;
        for(const auto& a:s.doc.atoms().records())if(a.isAlive()){cs+=a.element==6;if(a.element==7){++ns;if(a.semantics.charge==1)middle=a.id;if(a.semantics.charge==-1)end=a.id;require(a.semantics.implicitHydrogens==0,"nitrogens carry no H");}}
        require(cs==5&&ns==2&&middle!=kInvalidObjectId&&end!=kInvalidObjectId,"C=N+ =N- graph and charges");
        auto b1=s.doc.bonds().alive(s.doc.bondBetween(0,middle)),b2=s.doc.bonds().alive(s.doc.bondBetween(middle,end));require(b1&&b2&&b1->order==BondOrder::Double&&b2->order==BondOrder::Double,"two true double bonds");
        auto u=s.doc.atoms().alive(middle)->position-s.doc.atoms().alive(0)->position,v=s.doc.atoms().alive(end)->position-s.doc.atoms().alive(middle)->position;
        require(dot(u,v)>0&&std::abs(u.x*v.y-u.y*v.x)<1e-6,"double bonds remain collinear");
        integration::RingGeometry rings(s.doc);auto labels=integration::molecular::labels(s.doc,s.glyphs,rings);auto ink=rings.build(*b2);integration::molecular::clip(ink,*b2,labels,1.6);
        require(ink.shapes.size()==2,"both NN strokes survive clipping");for(const auto& line:ink.shapes)require(line.points.size()==2&&length(line.points[1]-line.points[0])>s.doc.defaultBondLength()*.4,"NN double has visible length at standard size");
        auto carbonInk=rings.build(*b1);Vec2 normal(-u.y/length(u),u.x/length(u));
        auto spacing=[&](const bonds::BondRenderData& data){return std::abs(dot(data.shapes[0].points.front()-data.shapes[1].points.front(),normal));};
        require(carbonInk.shapes.size()==2&&std::abs(spacing(carbonInk)-spacing(ink))<1e-6,"C=N and N=N stroke spacing aligns");
        require(s.valenceWarnings().empty(),"diazo structure has no valence warning");
    };
    fixture();auto before=s.doc.digest();app.openText(kInvalidObjectId,0);app.textEditor.select(0,-1);std::wstring text=L"n2";SendMessageW(app.edit,EM_REPLACESEL,TRUE,LPARAM(text.c_str()));require(app.finishText(true),"actual editor n2 commit");verify();
    s.selected.clear();s.fit();require(s.save(dir+"/typed-diazo.cdxml",true).ok(),"save typed diazo");app.screenshot(dir+"/typed-diazo.png");require(s.undo().ok()&&s.doc.digest()==before,"typing diazo is one undo");
    fixture();require(s.doc.setAtomLabel(0,"N2").ok(),"legacy N2 label");auto sem=s.doc.atoms().alive(0)->semantics;sem.nodeType=uint8_t(NodeType::Nickname);s.doc.setAtomSemantics(0,sem);s.doc.extensions(ObjectRef(ObjectKind::Atom,0)).set("studio.compound.fragment",compounddata::find("N2")->fragment);s.refresh();s.selected={ObjectRef(ObjectKind::Atom,0)};before=s.doc.digest();app.expandPending=false;app.expandGroups();verify();require(s.save(dir+"/expanded-diazo.cdxml",true).ok(),"save expanded diazo");require(s.undo().ok()&&s.doc.digest()==before,"N2 expansion is one undo");
    fixture();ObjectId middle,end,b;auto p=s.doc.atoms().alive(0)->position;auto direction=studio::diazo::axis(s.doc,0);s.doc.addAtom(p+direction*14.4,7,&middle);s.doc.addAtom(p+direction*28.8,7,&end);s.doc.addBond(0,middle,&b);s.doc.setBondOrder(b,BondOrder::Double);s.doc.addBond(middle,end,&b);s.doc.setBondOrder(b,BondOrder::Double);auto state=s.doc.atoms().alive(middle)->semantics;state.charge=1;s.doc.setAtomSemantics(middle,state);s.save(dir+"/legacy-short-diazo.cdxml",true);require(s.open(dir+"/legacy-short-diazo.cdxml").ok(),"reopen old short diazo");verify();
    Session gas;ObjectId id;gas.doc.addAtom(Vec2(),6,&id);require(nicknames::apply(gas.doc,id,"N2").ok()&&gas.doc.stats().aliveAtoms==1&&gas.doc.atomLabel(id)=="N2","isolated N2 remains molecular nitrogen shorthand");
    for(auto order:{BondOrder::Double,BondOrder::Triple}){
        Document d;ObjectId a,b,ab;d.addAtom(Vec2(0,0),7,&a);d.addAtom(Vec2(40,0),7,&b);d.addBond(a,b,&ab);d.setBondOrder(ab,order);
        for(auto atom:{a,b}){auto sem=d.atoms().alive(atom)->semantics;sem.implicitHydrogens=0;sem.charge=order==BondOrder::Double?(atom==a?1:-1):0;d.setAtomSemantics(atom,sem);}
        auto beforeA=d.atoms().alive(a)->semantics,beforeB=d.atoms().alive(b)->semantics;bool changed=false;
        require(studio::diazo::compactNitrogenBond(d,a,b,changed).ok()&&changed,"oversized free N-N bond compacts");
        require(std::abs(length(d.atoms().alive(a)->position-d.atoms().alive(b)->position)-studio::diazo::nitrogenLength(d))<1e-7&&studio::diazo::nitrogenLength(d)<26,"new N-N length is shorter");
        require(d.bonds().alive(ab)->order==order&&d.atoms().alive(a)->semantics==beforeA&&d.atoms().alive(b)->semantics==beforeB,"length adjustment preserves bond order, charge and hydrogen states");
    }
    // A single-bond endpoint is N+ triple N, without changing the CH2 route.
    for(bool legacy:{false,true}){
        fixture();ObjectId tip,attachment;s.doc.addAtom(s.doc.atoms().alive(0)->position+studio::diazo::axis(s.doc,0)*14.4,6,&tip);s.doc.addBond(0,tip,&attachment);s.refresh();before=s.doc.digest();
        if(legacy){s.doc.setAtomLabel(tip,"N2");auto state=s.doc.atoms().alive(tip)->semantics;state.nodeType=uint8_t(NodeType::Nickname);s.doc.setAtomSemantics(tip,state);s.doc.extensions(ObjectRef(ObjectKind::Atom,tip)).set("studio.compound.fragment",compounddata::find("N2")->fragment);s.refresh();s.selected={ObjectRef(ObjectKind::Atom,tip)};before=s.doc.digest();app.expandPending=false;app.expandGroups();}
        else{app.openText(kInvalidObjectId,tip);app.textEditor.select(0,-1);std::wstring value=L"n2";SendMessageW(app.edit,EM_REPLACESEL,TRUE,LPARAM(value.c_str()));require(app.finishText(true),"terminal n2 editor commit");}
        int carbons=0,nitrogens=0;ObjectId outer=kInvalidObjectId;
        for(const auto& a:s.doc.atoms().records())if(a.isAlive()){carbons+=a.element==6;if(a.element==7){++nitrogens;if(a.id!=tip)outer=a.id;require(a.semantics.implicitHydrogens==0,"diazonium nitrogen has no H");}}
        require(carbons==5&&nitrogens==2&&s.doc.atoms().alive(tip)->element==7&&s.doc.atoms().alive(tip)->semantics.charge==1,"endpoint becomes first positive nitrogen with no extra carbon");
        require(outer!=kInvalidObjectId&&s.doc.atoms().alive(outer)->semantics.charge==0&&s.doc.bonds().alive(attachment)->order==BondOrder::Single&&s.doc.bonds().alive(s.doc.bondBetween(tip,outer))->order==BondOrder::Triple,"ring-N single and N triple N with correct charges");
        require(s.valenceWarnings().empty(),"terminal diazonium valence is valid");
        if(!legacy){s.selected.clear();s.fit();s.save(dir+"/terminal-diazonium.cdxml",true);app.screenshot(dir+"/terminal-diazonium.png");}
        require(s.undo().ok()&&s.doc.digest()==before,"terminal n2 input or expansion is one undo");
    }
    fixture();ObjectId nitrogen,cn;auto root=s.doc.atoms().alive(0)->position;auto out=studio::diazo::axis(s.doc,0);s.doc.addAtom(root+out*25,7,&nitrogen);s.doc.addBond(0,nitrogen,&cn);s.doc.setBondOrder(cn,BondOrder::Double);auto nh=s.doc.atoms().alive(nitrogen)->semantics;nh.implicitHydrogens=1;s.doc.setAtomSemantics(nitrogen,nh);
    for(auto target:{ObjectId(0),nitrogen}){auto a=arrows::resolvePreset(49,(root+s.doc.atoms().alive(nitrogen)->position)*.5,s.doc.atoms().alive(target)->position,.6);a.mechanism=true;a.electronSource=ObjectRef(ObjectKind::Bond,cn);a.electronTarget=ObjectRef(ObjectKind::Atom,target);ObjectId aid;arrows::addArrow(s.doc,a,.6,&aid);}
    s.refresh();s.selected=s.all();before=s.doc.digest();app.generateMechanismStep();ObjectId carbene=kInvalidObjectId;
    for(auto ref:s.selected)if(ref.kind==ObjectKind::Atom){auto a=s.doc.atoms().alive(ref.id);if(a->element==6&&a->semantics.radical==uint8_t(Radical::Singlet))carbene=a->id;}
    require(carbene!=kInvalidObjectId,"alpha elimination marks the actual carbene carbon: "+app.status);int dots=0;
    for(const auto& p:s.scene.primitives())if(p.source==ObjectRef(ObjectKind::Atom,carbene)&&p.kind==render::PrimKind::Polygon&&p.z==12&&p.points.size()==32)++dots;
    require(dots==2,"carbene product displays two atom-bound electron dots");s.selected.clear();s.fit();s.save(dir+"/alpha-elimination-carbene.cdxml",true);app.screenshot(dir+"/alpha-elimination-carbene.png");require(s.undo().ok()&&s.doc.digest()==before,"carbene marking undoes with generated step");
    std::ofstream(dir+"/diazo-checks.json")<<"{\"typed_n2\":true,\"expanded_n2\":true,\"carbon_retained\":true,\"charges\":true,\"no_terminal_h\":true,\"visible_double\":true,\"legacy_reopen\":true,\"undo\":true,\"isolated_nitrogen\":true}";
}
