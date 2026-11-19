#pragma once
// Real user drawing: three textbook-style arrows with gaps, at 14.4 pt bonds.
void App::mechanismInferCheck(const std::string& dir){
    auto require=[&](bool value,const char* why){std::ofstream(dir+"/mechanism-infer-progress.txt",std::ios::app)<<why<<": "<<(value?"pass":"FAIL")<<"\n";if(!value)throw std::runtime_error(why);};
    require(s.open(dir+"/input.cdxml").ok(),"load real user drawing");s.history.clear();s.selected.clear();
    std::vector<ObjectId> arrowsToInfer;std::vector<arrows::ArrowParams> beforeArrows;
    std::map<int,ObjectId> fileAtoms,fileBonds;for(const auto& a:s.doc.atoms().records())if(a.isAlive())fileAtoms[a.sourceId]=a.id;for(const auto& b:s.doc.bonds().records())if(b.isAlive())fileBonds[b.sourceId]=b.id;
    for(const auto& a:s.doc.arrows().records())if(a.isAlive()){arrows::ArrowParams p;bool ok=false;arrows::readArrow(s.doc,a.id,&p,&ok);if(ok&&p.mechanism&&!p.electronSource.valid()&&!p.electronTarget.valid()){arrowsToInfer.push_back(a.id);beforeArrows.push_back(p);}}
    require(arrowsToInfer.size()==3,"actual fixture has exactly three unbound manual arrows");
    std::vector<AtomRecord> originals;for(const auto& a:s.doc.atoms().records())if(a.isAlive())originals.push_back(a);
    auto oldStats=s.doc.stats();auto oldAtomSlots=s.doc.atoms().size();auto before=s.doc.digest();for(auto id:arrowsToInfer)s.selected.push_back(ObjectRef(ObjectKind::Arrow,id));generateMechanismStep();
    require(s.doc.stats().aliveAtoms==oldStats.aliveAtoms+8&&s.doc.stats().aliveBonds==oldStats.aliveBonds+7,"unbound user arrows generate complete eight atom product");
    std::map<ObjectId,ObjectId> copies;std::vector<ObjectId> sourceAtoms,newAtoms;for(int source=28;source<=35;++source){require(fileAtoms.count(source)!=0,"expected original source atom exists");sourceAtoms.push_back(fileAtoms[source]);}std::sort(sourceAtoms.begin(),sourceAtoms.end());for(const auto& a:s.doc.atoms().records())if(a.isAlive()&&a.id>=oldAtomSlots)newAtoms.push_back(a.id);require(newAtoms.size()==8,"only intended user reactants are cloned");for(size_t i=0;i<8;++i)copies[sourceAtoms[i]]=newAtoms[i];
    auto product=[&](int source){return copies.at(fileAtoms.at(source));};
    auto order=[&](ObjectId a,ObjectId b){for(const auto& edge:s.doc.bonds().records())if(edge.isAlive()&&((edge.beginAtom==a&&edge.endAtom==b)||(edge.beginAtom==b&&edge.endAtom==a)))return int(edge.order);return 0;};
    require(order(product(35),product(32))==2&&order(product(32),product(33))==1&&order(product(33),product(28))==1&&order(product(28),product(31))==1,"real user arrows yield intended aldol connectivity");
    require(order(product(32),product(34))==1&&order(product(28),product(29))==1&&order(product(28),product(30))==1,"real user spectator bonds stay intact");
    int charge=0,hydrogens=0,carbon=0,oxygen=0;for(auto id:newAtoms){auto a=s.doc.atoms().get(id);charge+=a->semantics.charge;carbon+=a->element==6;oxygen+=a->element==8;int h=a->semantics.implicitHydrogens;if(h<0){int sum=0;for(auto bond:s.doc.incidentBonds(id))sum+=int(s.doc.bonds().get(bond)->order);h=(a->element==6?4-std::abs(int(a->semantics.charge)):2+int(a->semantics.charge))-sum;}hydrogens+=h;}
    require(carbon==6&&oxygen==2&&charge==-1&&hydrogens==11&&s.doc.atoms().get(product(35))->semantics.charge==0&&s.doc.atoms().get(product(31))->semantics.charge==-1,"real drawing conserves formula and localizes alkoxide charge");
    for(const auto& old:originals){auto a=s.doc.atoms().get(old.id);require(a&&a->position==old.position&&a->element==old.element&&a->semantics==old.semantics,"all original reactants and previous product remain unchanged");}
    const int expectedSourceKind[]={0,1,1},expectedSourceId[]={35,49,50},expectedTargetKind[]={1,0,0},expectedTargetId[]={52,31,28};
    for(size_t i=0;i<arrowsToInfer.size();++i){arrows::ArrowParams p;bool ok=false;arrows::readArrow(s.doc,arrowsToInfer[i],&p,&ok);auto src=ObjectRef(expectedSourceKind[i]?ObjectKind::Bond:ObjectKind::Atom,expectedSourceKind[i]?fileBonds.at(expectedSourceId[i]):fileAtoms.at(expectedSourceId[i]));auto dst=ObjectRef(expectedTargetKind[i]?ObjectKind::Bond:ObjectKind::Atom,expectedTargetKind[i]?fileBonds.at(expectedTargetId[i]):fileAtoms.at(expectedTargetId[i]));require(ok&&p.electronSource==src&&p.electronTarget==dst,"inferred bindings match intended chemical objects");auto geometryBefore=beforeArrows[i];geometryBefore.electronSource=p.electronSource;geometryBefore.electronTarget=p.electronTarget;require(arrows::encodeArrowParams(geometryBefore)==arrows::encodeArrowParams(p),"binding preserves every original arrow geometry field");}
    auto after=s.doc.digest();require(s.undo().ok()&&s.doc.digest()==before,"inference binding and generation undo together");require(s.undo(true).ok()&&s.doc.digest()==after,"inference binding and generation redo together");
    require(s.save(dir+"/user-inferred-result.cdxml",true).ok(),"save inferred editable product");s.selected.clear();s.fit();invalidate();screenshot(dir+"/user-inferred-result.png");
    {Session reopened;require(reopened.open(dir+"/user-inferred-result.cdxml").ok(),"reopen inferred bindings");int bound=0;for(const auto& a:reopened.doc.arrows().records())if(a.isAlive()){arrows::ArrowParams p;bool ok=false;arrows::readArrow(reopened.doc,a.id,&p,&ok);if(ok&&p.mechanism){Vec2 from,to;require(arrows::mechanismAnchorPoint(reopened.doc,p.electronSource,&from)&&arrows::mechanismAnchorPoint(reopened.doc,p.electronTarget,&to),"persisted semantic references resolve");++bound;}}require(bound==6,"old and newly inferred arrows all retain bindings");}
    {auto params=beforeArrows;params[0].electronSource=ObjectRef(ObjectKind::Atom,999999);std::vector<mechanism::ElectronMove> moves;auto digest=s.doc.digest();auto e=mechanism::inferElectronMoves(s.doc,params,&moves);require(!e.ok()&&s.doc.digest()==digest,"explicit stale reference cannot silently retarget");}
    {Document ambiguous;ObjectId a,b,h;ambiguous.addAtom(Vec2(0,-3),8,&a);ambiguous.addAtom(Vec2(0,3),8,&b);ambiguous.addAtom(Vec2(30,0),1,&h);for(auto id:{a,b}){auto sem=ambiguous.atoms().get(id)->semantics;sem.charge=-1;ambiguous.setAtomSemantics(id,sem);}auto sem=ambiguous.atoms().get(h)->semantics;sem.charge=1;ambiguous.setAtomSemantics(h,sem);auto p=arrows::resolvePreset(49,Vec2(0,0),Vec2(30,0),.6);p.mechanism=true;p.arcSagitta=0;p.family=arrows::ArrowFamily::Straight;auto digest=ambiguous.digest();std::vector<mechanism::ElectronMove> moves;auto e=mechanism::inferElectronMoves(ambiguous,{p},&moves);require(!e.ok()&&ambiguous.digest()==digest,"equally plausible donor atoms are not guessed");}
    {
        Document d;ObjectId oxygen[2],h,id;
        for(int i=0;i<2;++i){
            d.addAtom(Vec2(0,i?3:-3),8,&oxygen[i]);auto sem=d.atoms().alive(oxygen[i])->semantics;
            sem.charge=-1;d.setAtomSemantics(oxygen[i],sem);
        }
        d.addAtom(Vec2(30,0),1,&h);auto sem=d.atoms().alive(h)->semantics;sem.charge=1;d.setAtomSemantics(h,sem);
        auto p=arrows::resolvePreset(49,Vec2(0,0),Vec2(30,0),.6);p.mechanism=true;p.electronTarget=ObjectRef(ObjectKind::Atom,h);
        require(arrows::addArrow(d,p,.6,&id).ok(),"create ambiguous native arrow");
        for(bool stale:{false,true}){
            if(stale){p.electronSource=ObjectRef(ObjectKind::Atom,999999);arrows::writeArrow(d,id,p,.6);}
            auto before=d.digest();bool rejected=false;
            try{planMechanismStep(d,{{ObjectKind::Arrow,id}},{{ObjectKind::Arrow,id}},0,0,false);}
            catch(const std::runtime_error&){rejected=true;}
            require(rejected&&d.digest()==before,"generate action preserves drawing on unresolved or stale donor");
        }
    }
    std::ofstream(dir+"/mechanism-infer-checks.json")<<"{\"actual_user_drawing\":true,\"aldol_graph\":true,\"formula_charge\":true,\"geometry_preserved\":true,\"undo_redo\":true,\"bindings_persisted\":true,\"stale_reference_rejected\":true,\"ambiguity_rejected\":true}";s.dirty=false;PostMessageW(h,WM_CLOSE,0,0);
}
