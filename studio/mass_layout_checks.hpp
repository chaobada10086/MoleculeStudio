#pragma once
static void checkMassLayout(App& app,const std::string& dir){
    auto require=[](bool ok,const std::string& why){if(!ok)throw std::runtime_error("mass layout: "+why);};auto& s=app.s;int checked=0;
    for(int mode:{0,1,2}){
        Document doc;ObjectId center,oxygen,heavy;doc.addAtom(Vec2(100,100),6,&center);doc.addAtom(Vec2(100,80),8,&oxygen);doc.addAtom(Vec2(80,100),mode==0?53:6,&heavy);ObjectId bond;doc.addBond(center,oxygen,&bond);doc.setBondOrder(bond,BondOrder::Double);doc.addBond(center,heavy,nullptr);
        if(mode){doc.setAtomLabel(heavy,mode==1?"Ph":"CustomHeavy");auto sem=doc.atoms().alive(heavy)->semantics;sem.nodeType=uint8_t(NodeType::Nickname);sem.implicitHydrogens=0;sem.showLabel=true;doc.setAtomSemantics(heavy,sem);doc.extensions(ObjectRef(ObjectKind::Atom,heavy)).set("studio.nickname.fragment",nickdata::findAlias("Ph")->fragment);}
        ObjectId previous=center;std::vector<ObjectId> light;
        for(int i=0;i<4;++i){ObjectId id;doc.addAtom(Vec2(120+20*i,100),6,&id);doc.addBond(previous,id,nullptr);light.push_back(id);previous=id;}
        auto masses=studio_fragment_mass::weights(doc);double lightMass=0;for(auto id:light)lightMass+=masses.at(id);if(mode==0)require(masses.at(heavy)>lightMass,"visible iodine outweighs four visible carbons");
        else require(masses.at(heavy)==0,"Ph and custom stored abbreviations contribute zero layout mass");
        std::map<ObjectId,AtomRecord> atoms;std::map<ObjectId,Vec2> positions;std::map<mechanism::step_detail::Edge,int> links;
        for(const auto& a:doc.atoms().records())if(a.isAlive()){atoms[a.id]=a;positions[a.id]=a.position;}for(const auto& b:doc.bonds().records())if(b.isAlive())links[mechanism::step_detail::edge(b.beginAtom,b.endAtom)]=int(b.order);
        const auto before=positions;size_t budget=1000000;std::set<ObjectId> active{center};mechanism::step_detail::alignTrigonalBranches(links,atoms,{},20,budget,positions,&active,&masses);
        require(length(positions.at(center)-before.at(center))<1e-8,"central pivot stays fixed");
        require(length(positions.at(mode?light.front():heavy)-before.at(mode?light.front():heavy))<1e-8,"visible heaviest branch stays fixed");
        require(length(positions.at(mode?heavy:light.front())-before.at(mode?heavy:light.front()))>1,"lower priority branch rotates");
        std::vector<Vec2> axes{positions.at(heavy)-positions.at(center),positions.at(oxygen)-positions.at(center),positions.at(light.front())-positions.at(center)};
        for(size_t i=0;i<axes.size();++i)for(size_t j=i+1;j<axes.size();++j)require(std::abs(dot(axes[i],axes[j])/(length(axes[i])*length(axes[j]))+.5)<1e-7,"three branches form 120-degree angles around the fixed heavy branch");
        for(int i=0;i<3;++i)require(std::abs(length(positions.at(light[i+1])-positions.at(light[i]))-20)<1e-8,"small fragment rotates rigidly without changing bond lengths");++checked;
    }
    require(studio_fragment_mass::elemental(6,13)>studio_fragment_mass::elemental(6),"assigned isotope mass contributes to priority");
    const auto fixture=dir+"/user-carbon-iodine-input.r62doc";
    if(GetFileAttributesW(W(fixture).c_str())!=INVALID_FILE_ATTRIBUTES){
        s.cancel();s.history.clear();require(s.open(fixture).ok(),"open user's alkoxide/CS2 reaction");const auto before=s.doc.digest();const auto count=s.doc.stats().aliveAtoms;s.selected={ObjectRef(ObjectKind::Arrow,4),ObjectRef(ObjectKind::Arrow,5)};app.generateMechanismStep();require(s.doc.stats().aliveAtoms==count+19,"actual alkoxide and carbon disulfide produce nineteen product atoms");
        std::map<ObjectId,ObjectId> copied;for(auto ref:s.selected)if(ref.kind==ObjectKind::Atom)copied[studio_mechanism_products::sourceAtom(s.doc,ref.id)]=ref.id;
        for(const auto& b:s.doc.bonds().records())if(b.isAlive()&&b.beginAtom>=26&&b.beginAtom<=41&&b.endAtom>=26&&b.endAtom<=41){auto old=s.doc.atoms().alive(b.endAtom)->position-s.doc.atoms().alive(b.beginAtom)->position;auto now=s.doc.atoms().alive(copied.at(b.endAtom))->position-s.doc.atoms().alive(copied.at(b.beginAtom))->position;require(length(now-old)<1e-7,"all bonds within the large scaffold preserve exact original orientation and length");}
        require(s.doc.bonds().alive(s.doc.bondBetween(copied.at(36),copied.at(42)))->order==BondOrder::Single,"new O--C bond remains editable");
        require(s.doc.atoms().alive(copied.at(43))->semantics.charge==-1&&s.doc.bonds().alive(s.doc.bondBetween(copied.at(42),copied.at(43)))->order==BondOrder::Single&&s.doc.bonds().alive(s.doc.bondBetween(copied.at(42),copied.at(44)))->order==BondOrder::Double,"xanthate keeps its single S minus and double S bonds");
        Session clean;clean.doc.setDefaultFont(s.doc.defaultFont());auto excerpt=s.selected;for(ObjectId id=26;id<=44;++id)excerpt.emplace_back(ObjectKind::Atom,id);for(const auto& b:s.doc.bonds().records())if(b.isAlive()&&b.beginAtom>=26&&b.beginAtom<=44&&b.endAtom>=26&&b.endAtom<=44)excerpt.emplace_back(ObjectKind::Bond,b.id);for(ObjectId id:{4,5})excerpt.emplace_back(ObjectKind::Arrow,id);require(studioio::append(s.doc,clean.doc,excerpt,Vec2(),nullptr).ok()&&clean.save(dir+"/mass-fixed-xanthate.cdxml",true).ok(),"save actual large-fragment orientation example");
        require(s.undo().ok()&&s.doc.digest()==before,"mass-priority generation undoes atomically");++checked;
    }
    std::ofstream(dir+"/mass-layout-checks.json")<<"{\"cases\":"<<checked<<",\"mass_over_atom_count\":true,\"abbreviations_excluded\":true,\"large_scaffold_vectors_exact\":true}";
}
