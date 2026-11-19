#pragma once
static void checkFishhookBetaScission(const std::string& dir){
    auto require=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    for(bool explicitPartner:{false,true}){
        Document d;ObjectId carbon,h,br,hbr,guide;
        d.addAtom(Vec2(0,0),6,&carbon);d.addAtom(Vec2(30,0),1,&h);d.addAtom(Vec2(60,0),35,&br);d.addBond(h,br,&hbr);
        auto sem=d.atoms().alive(carbon)->semantics;sem.radical=uint8_t(Radical::Doublet);sem.implicitHydrogens=3;d.setAtomSemantics(carbon,sem);
        require(virtual_bonds::add(d,carbon,h,&guide).ok(),"pending C-H bond");
        ObjectRef target(ObjectKind::Arrow,guide),source(ObjectKind::Bond,hbr);
        std::vector<mechanism::ElectronMove> moves={{ObjectRef(ObjectKind::Atom,carbon),target,1},{source,target,1}};
        if(explicitPartner)moves.emplace_back(source,ObjectRef(ObjectKind::Atom,br),1);
        std::vector<ObjectRef> made;require(mechanism::generateNextStep(d,moves,&made).ok(),"fishhooks transfer H to a pending bond");
        std::map<ObjectId,ObjectId> copies;for(auto ref:made)if(ref.kind==ObjectKind::Atom)copies[studio_mechanism_products::sourceAtom(d,ref.id)]=ref.id;
        auto c=copies.at(carbon),hydrogen=copies.at(h),bromine=copies.at(br);
        require(d.bondBetween(c,hydrogen)!=kInvalidObjectId&&d.incidentBonds(bromine).empty(),"C-H formed and H-Br cleaved");
        for(auto id:{c,hydrogen,bromine}){
            const auto& state=d.atoms().alive(id)->semantics;
            require(!state.charge&&state.radical==(id==bromine?uint8_t(Radical::Doublet):0),"only bromine retains an unpaired electron");
        }
    }
    for(bool full:{false,true}){
        Document source;ObjectId c,o,leaving,a,b,co,oc,ca,cb;source.addAtom(Vec2(60,50),6,&c);source.addAtom(Vec2(90,65),8,&o);source.addAtom(Vec2(90,95),6,&leaving);source.addAtom(Vec2(30,50),6,&a);source.addAtom(Vec2(60,20),6,&b);source.addBond(c,o,&co);source.addBond(o,leaving,&oc);source.addBond(c,a,&ca);source.addBond(c,b,&cb);
        for(auto id:{c,o,leaving,a,b}){auto sem=source.atoms().alive(id)->semantics;sem.implicitHydrogens=id==c||id==o?0:3;sem.radical=id==c?uint8_t(Radical::Doublet):0;source.setAtomSemantics(id,sem);}
        std::vector<mechanism::ElectronMove> moves={{ObjectRef(ObjectKind::Atom,c),ObjectRef(ObjectKind::Bond,co),1},{ObjectRef(ObjectKind::Bond,oc),ObjectRef(ObjectKind::Bond,co),1}};if(full)moves.emplace_back(ObjectRef(ObjectKind::Bond,oc),ObjectRef(ObjectKind::Atom,leaving),1);
        std::vector<arrows::ArrowParams> arrows;for(auto move:moves){Vec2 from,to;arrows::mechanismAnchorPoint(source,move.source,&from);arrows::mechanismAnchorPoint(source,move.target,&to);auto p=arrows::resolvePreset(57,from,to,.6);p.mechanism=true;p.electronSource=move.source;p.electronTarget=move.target;arrows.push_back(p);}
        Document result;std::vector<ObjectRef> made;std::vector<mechanism::ElectronMove> inferred;require(studio_drawn_step::prepare(source,{},arrows,result,inferred,made).ok(),"fishhook beta scission drawing fallback succeeds with two or three hooks");
        std::map<ObjectId,ObjectId> copied;for(auto ref:made)if(ref.kind==ObjectKind::Atom)copied[studio_mechanism_products::sourceAtom(result,ref.id)]=ref.id;
        auto cc=result.atoms().alive(copied.at(c)),oo=result.atoms().alive(copied.at(o)),radical=result.atoms().alive(copied.at(leaving));require(!cc->semantics.charge&&!cc->semantics.radical&&!oo->semantics.charge&&!oo->semantics.radical,"carbonyl beta scission never invents C minus or O plus radical");require(radical->semantics.radical==uint8_t(Radical::Doublet)&&!radical->semantics.charge,"departing alkyl receives the radical");require(result.bonds().alive(result.bondBetween(cc->id,oo->id))->order==BondOrder::Double&&result.bondBetween(oo->id,radical->id)==kInvalidObjectId,"beta scission forms carbonyl and cleaves alkyl oxygen bond");
        arrows.push_back(arrows.back());
        require(!studio_drawn_step::prepare(source,{},arrows,result,inferred,made).ok(),"duplicate fishhook cannot replay a second time");
    }
    std::ofstream(dir+"/fishhook-beta-checks.json")<<"{\"two_hooks\":true,\"three_hooks\":true,\"neutral_carbonyl\":true,\"alkyl_radical\":true}";
}
