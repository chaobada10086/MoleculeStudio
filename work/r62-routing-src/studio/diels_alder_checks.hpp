#pragma once
static void checkAldolAnnulation(App& app,const std::string& dir){
    const auto fixture=dir+"/aldol-input.r62doc";
    if(GetFileAttributesW(W(fixture).c_str())==INVALID_FILE_ATTRIBUTES)return;
    auto require=[](bool ok,const std::string& why){if(!ok)throw std::runtime_error("aldol annulation: "+why);};
    auto& s=app.s;s.cancel();require(s.open(fixture).ok(),"open original");
    const auto before=s.doc.digest();
    s.selected={{ObjectKind::Arrow,17},{ObjectKind::Arrow,19},{ObjectKind::Arrow,20}};
    {
        std::vector<arrows::ArrowParams> input;
        for(auto ref:s.selected){arrows::ArrowParams p;bool ok=false;arrows::readArrow(s.doc,ref.id,&p,&ok);require(ok,"actual arrow");input.push_back(p);}
        Document product;std::vector<mechanism::ElectronMove> moves;std::vector<ObjectRef> made;
        auto replay=studio_drawn_step::prepare(s.doc,{},input,product,moves,made);
        require(replay.ok(),"direct replay uses the enolate terminal: "+replay.message);
        std::map<ObjectId,ObjectId> copies;for(auto ref:made)if(ref.kind==ObjectKind::Atom)copies[studio_mechanism_products::sourceAtom(product,ref.id)]=ref.id;
        require(product.bondBetween(copies.at(61),copies.at(69))!=kInvalidObjectId,"replay forms C-alpha to carbonyl C");
    }
    app.generateMechanismStep();std::map<ObjectId,ObjectId> copies;
    for(auto ref:s.selected)if(ref.kind==ObjectKind::Atom)copies[studio_mechanism_products::sourceAtom(s.doc,ref.id)]=ref.id;
    require(copies.size()==14,"complete source component: "+app.status);
    std::vector<mechanism::step_detail::Edge> links;std::map<ObjectId,Vec2> positions;
    for(auto item:copies){
        const auto a=s.doc.atoms().alive(item.second);
        require(a->semantics.charge==(item.first==64?-1:0)&&!a->semantics.radical,"charge and spin");
        positions[item.second]=a->position;
        for(auto other:copies)if(item.first<other.first){
            auto original=s.doc.bonds().alive(s.doc.bondBetween(item.first,other.first));
            auto product=s.doc.bonds().alive(s.doc.bondBetween(item.second,other.second));
            int order=original?int(original->order):0;
            if((item.first==61&&(other.first==64||other.first==69))||(item.first==67&&other.first==69))order=1;
            if(item.first==67&&other.first==68)order=2;
            require((product?int(product->order):0)==order,"new sigma bond and both carbonyls");
            if(product)links.emplace_back(item.second,other.second);
        }
    }
    auto span=length(positions.at(copies.at(61))-positions.at(copies.at(69)));
    require(span>10&&span<20,"closure has normal bond length");
    size_t budget=100000;require(mechanism::step_detail::collisionCost(positions,links,14.4,budget)==0,"product has no atom overlaps or crossed bonds");
    require(s.save(dir+"/aldol-annulation.cdxml",true).ok(),"save original with its new product");
    auto after=s.doc.digest();require(s.undo().ok()&&s.doc.digest()==before&&s.undo(true).ok()&&s.doc.digest()==after,"exact undo and redo");
}
static void checkDielsAlder(App& app,const std::string& dir){
    auto require=[](bool ok,const std::string& message){if(!ok)throw std::runtime_error("DA: "+message);};
    for(int mode=0;mode<7;++mode){
        Document d;d.setDefaultBondLength(30);
        std::array<Vec2,6> points={Vec2(0,0),Vec2(-26,15),Vec2(-26,45),Vec2(0,60),Vec2(38,45),Vec2(38,15)};
        if(mode==3){points[0]=Vec2(-52,0);points[4]=Vec2(125,45);points[5]=Vec2(125,15);}
        std::array<ObjectId,6> atoms;
        for(size_t i=0;i<6;++i)d.addAtom(points[i],6,&atoms[i]);
        auto bond=[&](int a,int b,int order){ObjectId id;d.addBond(atoms[a],atoms[b],&id);d.setBondOrder(id,BondOrder(order));return ObjectRef(ObjectKind::Bond,id);};
        const auto ab=bond(0,1,2),bc=bond(1,2,1),cd=bond(2,3,2),ef=bond(4,5,mode==4?3:2);
        if(mode==2||mode==5){
            // Distinguishable termini on both partners make the two pairings different.
            for(auto i:{0,4}){auto state=d.atoms().alive(atoms[i])->semantics;state.isotope=13;d.setAtomSemantics(atoms[i],state);}
        }
        const int e=mode==2?5:4,f=e==4?5:4;
        auto atom=[&](int i){return ObjectRef(ObjectKind::Atom,atoms[i]);};
        auto arrow=[&](ObjectRef src,ObjectRef dst,Vec2 head){
            Vec2 tail;arrows::mechanismAnchorPoint(d,src,&tail);
            auto p=arrows::resolvePreset(49,tail,head,.6);p.mechanism=true;p.electronSource=src;p.electronTarget=dst;return p;
        };
        auto toward=[&](int a,int b){return (points[a]+points[b])*.5;};
        std::vector<arrows::ArrowParams> input={
            arrow(ab,bc,toward(1,2)),arrow(cd,atom(e),points[e]),arrow(ef,atom(0),points[0])};
        if(mode==1){
            input={arrow(ab,ObjectRef(),toward(1,2)),arrow(cd,ObjectRef(),toward(3,e)),arrow(ef,ObjectRef(),toward(f,0))};
        }else if(mode==2||mode>=5){
            input[1]=arrow(cd,ef,mode>=5?toward(e,f):points[e]);
            input[2]=arrow(ef,ab,points[0]);
        }
        std::rotate(input.begin(),input.begin()+mode%3,input.end());
        std::vector<ObjectRef> refs;
        for(auto p:input){ObjectId id;require(arrows::addArrow(d,p,.6,&id).ok(),"create full arrow");refs.emplace_back(ObjectKind::Arrow,id);}
        auto& s=app.s;s.cancel();s.doc=d;s.history.clear();s.input.bindDocument(s.doc.id());s.refresh();s.selected=refs;
        auto before=s.doc.digest();app.generateMechanismStep();
        if(mode==5){require(s.doc.digest()==before&&app.status.find("DA")!=std::string::npos,"ambiguous regioisomers are distinguished");continue;}
        require(s.doc.stats().aliveAtoms==12,"three arrows generate six product atoms: "+app.status);
        std::map<ObjectId,ObjectId> copy;
        for(auto ref:s.selected)if(ref.kind==ObjectKind::Atom)copy[studio_mechanism_products::sourceAtom(s.doc,ref.id)]=ref.id;
        std::map<std::pair<int,int>,int> edges={{{0,1},1},{{1,2},2},{{2,3},1},{{3,e},1},{{4,5},mode==4?2:1},{{0,f},1}};
        for(int i=0;i<6;++i){
            const auto c=copy.at(atoms[i]);const auto& state=s.doc.atoms().alive(c)->semantics;
            require(!state.charge&&!state.radical,"neutral product without radicals");
            for(int j=i+1;j<6;++j){
                auto b=s.doc.bonds().alive(s.doc.bondBetween(c,copy.at(atoms[j])));
                require((b?int(b->order):0)==edges[{i,j}],"product bond order and regiochemistry");
            }
        }
        if(!mode)require(s.save(dir+"/diels-alder-without-guides.cdxml",true).ok(),"export editable example");
        const auto after=s.doc.digest();
        require(s.undo().ok()&&s.doc.digest()==before&&s.undo(true).ok()&&s.doc.digest()==after,"undo and redo");
    }
}
