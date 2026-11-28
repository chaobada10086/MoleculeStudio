#pragma once
static void checkMechanismLocalCleanup(App& app,const std::string& dir){
    auto require=[](bool ok,const std::string& why){if(!ok)throw std::runtime_error("local step cleanup: "+why);};
    Document d;d.setDefaultBondLength(30);std::vector<ObjectId> ring;for(int i=0;i<6;++i){ObjectId id;double angle=i*3.141592653589793/3;d.addAtom(Vec2(80+30*std::cos(angle),90+30*std::sin(angle)),6,&id);ring.push_back(id);}for(int i=0;i<6;++i){ObjectId b;d.addBond(ring[i],ring[(i+1)%6],&b);if(i%2==0)d.setBondOrder(b,BondOrder::Double);}
    ObjectId c,o,n,tail,co;d.addAtom(Vec2(140,90),6,&c);d.addAtom(Vec2(155,64.0192378865),8,&o);d.addAtom(Vec2(155,115.9807621135),7,&n);d.addAtom(Vec2(133,98),7,&tail);d.addBond(ring[0],c,nullptr);d.addBond(c,o,&co);d.setBondOrder(co,BondOrder::Double);d.addBond(c,n,nullptr);d.addBond(n,tail,nullptr);auto sem=d.atoms().alive(n)->semantics;sem.implicitHydrogens=1;d.setAtomSemantics(n,sem);sem=d.atoms().alive(tail)->semantics;sem.implicitHydrogens=2;d.setAtomSemantics(tail,sem);
    Document source=d;sem=source.atoms().alive(n)->semantics;sem.charge=1;sem.implicitHydrogens=2;source.setAtomSemantics(n,sem);std::vector<ObjectRef> made;for(const auto& a:d.atoms().records())if(a.isAlive()){made.push_back({ObjectKind::Atom,a.id});d.extensions(ObjectRef(ObjectKind::Atom,a.id)).set("studio.mechanism.source.atom",std::to_string(a.id));}
    auto local=studio_local_cleanup::scope(d,source,made);require(local.active.count(n)&&local.movable.count(n)&&local.movable.count(tail),"changed protonation selects nearby chain");for(auto id:ring)require(!local.movable.count(id),"aromatic frame locked");auto initial=d;auto q=studio_local_cleanup::quality(d,local);require(q.shape>.2||q.crowd>0,"fixture needs branch cleanup");
    studio_local_cleanup::Request input;require(studio_local_cleanup::request(d,local,input),"build constrained request");auto digest=d.digest();require(!studio_local_cleanup::apply(d,input,"<CDXML><page/></CDXML>")&&d.digest()==digest,"incomplete response changes nothing");require(!studio_local_cleanup::apply(d,input,input.xml)&&d.digest()==digest,"unchanged response changes nothing");
    auto result=studio_local_cleanup::run(app.h,d,source,made);require(result.attempted&&result.changed,"actual local service improves branch: "+result.detail);auto now=studio_local_cleanup::quality(d,local);require(now.crowd<=q.crowd+1e-6&&(now.shape<q.shape-.01||now.crowd<q.crowd-.01),"angle quality improves without new collisions");
    for(auto id:ring)require(d.atoms().alive(id)->position==initial.atoms().alive(id)->position,"all ring coordinates unchanged");require(d.stats().aliveAtoms==initial.stats().aliveAtoms&&d.stats().aliveBonds==initial.stats().aliveBonds,"graph size unchanged");for(const auto& a:initial.atoms().records())if(a.isAlive())require(d.atoms().alive(a.id)->element==a.element&&d.atoms().alive(a.id)->semantics.charge==a.semantics.charge&&d.atoms().alive(a.id)->semantics.implicitHydrogens==a.semantics.implicitHydrogens,"element charge and hydrogens preserved");
    auto settled=d.digest();auto repeat=studio_local_cleanup::run(app.h,d,source,made);require(!repeat.changed&&d.digest()==settled,"repeated local cleanup stable");
    auto save=[&](const Document& doc,const std::string& name){app.s.cancel();app.s.doc=doc;app.s.history.clear();app.s.selected.clear();app.s.refresh();app.s.fit();app.invalidate();app.screenshot(dir+"/"+name+".png");require(app.s.save(dir+"/"+name+".cdxml",true).ok(),"save example");};save(initial,"local-step-before");save(d,"local-step-after");
    // Healthy structures do not make a service request on every generation.
    auto healthy=studio_local_cleanup::run(app.h,d,d,made);require(!healthy.attempted&&!healthy.changed,"unchanged healthy component skipped");
    // Stereo centres and the neighbours that encode their projection are fixed.
    auto stereo=initial;auto edge=stereo.bondBetween(n,tail);stereo.setBondDisplay(edge,BondDisplay::Wedge);stereo.setBondStereo(edge,BondStereo::Up);auto guarded=studio_local_cleanup::scope(stereo,source,made);require(!guarded.movable.count(n)&&!guarded.movable.count(tail)&&!guarded.movable.count(c),"wedge neighborhood locked");
    {
        // Reproduce the malformed exocyclic alkene after loss of chloride.
        Document product;product.setDefaultBondLength(30);
        std::vector<ObjectRef> generated;
        auto atom=[&](Vec2 p,int element){ObjectId id;product.addAtom(p,element,&id);const ObjectRef ref(ObjectKind::Atom,id);
            product.extensions(ref).set("studio.mechanism.source.atom",std::to_string(id));generated.push_back(ref);return id;};
        auto bond=[&](ObjectId a,ObjectId b,BondOrder order=BondOrder::Single){ObjectId id;product.addBond(a,b,&id);
            product.setBondOrder(id,order);generated.emplace_back(ObjectKind::Bond,id);return id;};
        const auto alpha=atom({125,100},6),ipso=atom({110,74},6),farTop=atom({80,74},6),
            farCarbon=atom({65,100},6),farBottom=atom({80,126},6),nearBottom=atom({110,126},6),oxygen=atom({124,48},8);
        bond(alpha,ipso);bond(ipso,farTop);bond(farTop,farCarbon,BondOrder::Double);bond(farCarbon,farBottom);
        bond(farBottom,nearBottom,BondOrder::Double);bond(nearBottom,alpha);bond(ipso,oxygen,BondOrder::Double);
        const auto distal=atom({155,100},6),hydrogen=atom({170,74},1),chloro=atom({185,100},17);
        const auto alkene=bond(alpha,distal,BondOrder::Double);
        bond(distal,hydrogen);bond(distal,chloro);
        Document precursor=product;precursor.setBondOrder(alkene,BondOrder::Single);
        const auto damaged=product;
        const auto cleaned=studio_local_cleanup::run(app.h,product,precursor,generated);
        require(cleaned.changed,"clean up the formerly locked new alkene");
        const auto pivot=product.atoms().alive(distal)->position;
        auto ray=[&](ObjectId id){return product.atoms().alive(id)->position-pivot;};
        auto trigonal=[&](ObjectId a,ObjectId b){return std::abs(dot(ray(a),ray(b))/length(ray(a))/length(ray(b))+.5)<1e-7;};
        require(trigonal(alpha,hydrogen)&&trigonal(alpha,chloro)&&trigonal(hydrogen,chloro),"all three angles at the alkene end are 120 degrees");
        require(product.atoms().alive(hydrogen)->position.y<pivot.y&&product.atoms().alive(chloro)->position.y>pivot.y,"preserve the defined side of the hydrogen");
        for(auto id:{alpha,ipso,farTop,farCarbon,farBottom,nearBottom,distal})
            require(product.atoms().alive(id)->position==damaged.atoms().alive(id)->position,"ring and double-bond axis stay fixed");
        auto position=[&](const Document& doc,ObjectId id){return doc.atoms().alive(id)->position;};
        const auto ca=position(product,alpha)-position(product,ipso);
        const auto cf=position(product,farTop)-position(product,ipso);
        const auto outward=ca*(-1/length(ca))+cf*(-1/length(cf));
        auto followsBisector=[&](Vec2 axis){return dot(axis,outward)>0&&
            std::abs(mechanism::step_detail::cross(axis,outward))<1e-7;};
        require(followsBisector(position(product,oxygen)-position(product,ipso)),"ring carbonyl follows the outward bisector");
        auto settledAlkene=product.digest();
        require(!studio_local_cleanup::run(app.h,product,precursor,generated).changed&&product.digest()==settledAlkene,"new alkene cleanup is stable");
        auto established=damaged;
        established.setAtomPosition(oxygen,product.atoms().alive(oxygen)->position);
        auto establishedDigest=established.digest();
        require(!studio_local_cleanup::run(app.h,established,damaged,generated).changed&&established.digest()==establishedDigest,"existing alkene stays protected");
        Document phenol=damaged;
        phenol.setBondOrder(phenol.bondBetween(ipso,oxygen),BondOrder::Single);
        phenol.setBondOrder(phenol.bondBetween(ipso,alpha),BondOrder::Double);
        phenol.setBondOrder(alkene,BondOrder::Single);
        ObjectId hydroxylH,oh;
        phenol.addAtom({109,24},1,&hydroxylH);phenol.addBond(oxygen,hydroxylH,&oh);
        const ObjectRef hydroxylRef(ObjectKind::Atom,hydroxylH);
        phenol.extensions(hydroxylRef).set("studio.mechanism.source.atom",std::to_string(hydroxylH));
        auto phenolRefs=generated;
        phenolRefs.push_back(hydroxylRef);phenolRefs.emplace_back(ObjectKind::Bond,oh);
        const auto tilted=phenol;
        require(studio_local_cleanup::run(app.h,phenol,tilted,phenolRefs).changed,"align explicit phenolic OH");
        const auto phenolicAxis=position(phenol,oxygen)-position(phenol,ipso);
        const auto oldAxis=position(tilted,oxygen)-position(tilted,ipso);
        const auto ohRay=position(phenol,hydroxylH)-position(phenol,oxygen);
        const auto oldOH=position(tilted,hydroxylH)-position(tilted,oxygen);
        require(followsBisector(phenolicAxis)&&std::abs(length(ohRay)-length(oldOH))<1e-7&&
                std::abs(dot(phenolicAxis,ohRay)-dot(oldAxis,oldOH))<1e-7,"explicit O–H follows the C–O rotation");
    }
    const auto path=dir+"/user-hydrazine-input.r62doc";bool actual=false;
    if(GetFileAttributesW(W(path).c_str())!=INVALID_FILE_ATTRIBUTES){
        Session user;require(user.open(path).ok(),"load actual hydrazine source");auto& s=app.s;s.cancel();s.doc=user.doc;s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());
        const ObjectId carbon=209,oxygen=211,nu=220;ObjectId leaving=kInvalidObjectId;for(auto bid:s.doc.incidentBonds(carbon)){auto b=s.doc.bonds().alive(bid);auto other=b->beginAtom==carbon?b->endAtom:b->beginAtom;if(s.doc.atoms().alive(other)->element==7)leaving=other;}require(leaving!=kInvalidObjectId,"find imide leaving nitrogen");
        for(auto endpoints:std::vector<std::pair<ObjectRef,ObjectRef>>{{{ObjectKind::Bond,s.doc.bondBetween(carbon,oxygen)},{ObjectKind::Atom,oxygen}},{{ObjectKind::Atom,nu},{ObjectKind::Atom,carbon}},{{ObjectKind::Bond,s.doc.bondBetween(carbon,leaving)},{ObjectKind::Atom,leaving}}}){Vec2 a,b;arrows::mechanismAnchorPoint(s.doc,endpoints.first,&a);arrows::mechanismAnchorPoint(s.doc,endpoints.second,&b);auto p=arrows::resolvePreset(49,a,b,.6);p.mechanism=true;p.electronSource=endpoints.first;p.electronTarget=endpoints.second;p.head.atBegin=endpoints.first==ObjectRef(ObjectKind::Bond,s.doc.bondBetween(carbon,oxygen));arrows::routeMechanism(s.doc,p);ObjectId id;require(arrows::addArrow(s.doc,p,.6,&id).ok(),"add addition elimination arrow");s.selected.emplace_back(ObjectKind::Arrow,id);}
        s.refresh();auto before=s.doc.digest();auto count=s.doc.stats().aliveAtoms;app.generateMechanismStep(1);require(s.doc.stats().aliveAtoms==count+20,"actual screenshot reaction generates with proton transfer: "+app.status);actual=app.status.find("已自动整理")!=std::string::npos;std::ofstream(dir+"/local-actual-status.txt")<<app.status;require(actual,"actual screenshot branches automatically cleaned: "+app.status);auto after=s.doc.digest();require(s.undo().ok()&&s.doc.digest()==before&&s.undo(true).ok()&&s.doc.digest()==after,"generation and local cleanup share one undo step");
        for(auto ref:s.selected)if(ref.kind==ObjectKind::Atom)s.doc.extensions(ref).set("studio.test.local.open","1");require(s.save(dir+"/local-open-chain.r62doc").ok(),"save native open-chain fixture for next cyclization");
        std::vector<ObjectRef> refs=s.selected;for(ObjectId id=201;id<=218;++id)refs.emplace_back(ObjectKind::Atom,id);for(ObjectId id:{220,221})refs.emplace_back(ObjectKind::Atom,id);for(const auto& b:s.doc.bonds().records())if(b.isAlive()&&b.beginAtom>=201&&b.beginAtom<=221&&b.endAtom>=201&&b.endAtom<=221)refs.emplace_back(ObjectKind::Bond,b.id);for(auto ref:s.all())if(ref.kind==ObjectKind::Arrow&&ref.id>=user.doc.arrows().size())refs.push_back(ref);Session excerpt;require(studioio::append(s.doc,excerpt.doc,refs,Vec2(),nullptr).ok(),"extract actual local cleanup result");save(excerpt.doc,"local-actual-reaction");
    }
    std::ofstream(dir+"/local-step-cleanup-checks.json")<<"{\"active_neighborhood\":true,\"ring_fixed\":true,\"real_service\":true,\"angle_improved\":true,\"no_new_collisions\":true,\"graph_charge_hydrogen_preserved\":true,\"atomic_rejection\":true,\"repeat_stable\":true,\"healthy_skipped\":true,\"stereo_guard\":true,\"actual_reaction_auto_cleaned\":"<<(actual?"true":"false")<<"}";
}
