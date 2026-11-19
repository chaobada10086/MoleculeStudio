#pragma once
static void checkFusedCarbeneMigration(App& app,const std::string& dir){
    auto require=[](bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);};
    auto& s=app.s;
    for(bool fused:{false,true})for(double turn:{0.,1.1})for(double mirror:{-1.,1.}){
        Session original;auto& src=original.doc;src.setDefaultBondLength(30);
        const Vec2 points[]={{0,-25},{24,-8},{15,20},{-15,20},{-24,-8},{12,-53}};
        std::vector<ObjectId> old;
        auto transform=[&](Vec2 p){p.x*=mirror;return mechanism::step_detail::rotate(p,turn)+Vec2(100,100);};
        for(size_t i=0;i<6;++i){ObjectId id;src.addAtom(transform(points[i]),i==5?8:6,&id);old.push_back(id);}
        std::vector<ObjectId> links;
        for(size_t i=0;i<5;++i){ObjectId id;src.addBond(old[i],old[(i+1)%5],&id);links.push_back(id);}
        ObjectId co;src.addBond(old[0],old[5],&co);src.setBondOrder(co,BondOrder::Double);
        auto sem=src.atoms().alive(old[1])->semantics;sem.radical=uint8_t(Radical::Singlet);sem.implicitHydrogens=0;src.setAtomSemantics(old[1],sem);
        for(auto move:std::vector<mechanism::ElectronMove>{{ObjectRef(ObjectKind::Atom,old[1]),ObjectRef(ObjectKind::Bond,links[0])},{ObjectRef(ObjectKind::Bond,links[4]),ObjectRef(ObjectKind::Atom,old[1])}}){
            Vec2 from,to;arrows::mechanismAnchorPoint(src,move.source,&from);arrows::mechanismAnchorPoint(src,move.target,&to);
            auto arrow=arrows::resolvePreset(49,from,to,.6);arrow.mechanism=true;arrow.electronSource=move.source;arrow.electronTarget=move.target;
            arrows::routeMechanism(src,arrow,true);require(arrows::addArrow(src,arrow,.6,nullptr).ok(),"add carbene contraction arrows");
        }
        // Copy the complete source and its bound arrows before adding the second ring.
        s.cancel();s.doc=Document();s.history.clear();s.selected.clear();s.doc.setDefaultBondLength(30);
        ObjectId dummy;s.doc.addAtom(Vec2(-100,-100),6,&dummy);s.doc.removeObject(ObjectRef(ObjectKind::Atom,dummy));
        const Vec2 offset(25,40);std::vector<ObjectRef> inserted;
        require(studioio::append(src,s.doc,original.all(),offset,&inserted).ok(),"copy carbene structure and arrows");
        std::vector<ObjectId> ids;
        for(auto id:old){ObjectId copy=kInvalidObjectId;for(auto ref:inserted)if(ref.kind==ObjectKind::Atom&&length(s.doc.atoms().alive(ref.id)->position-src.atoms().alive(id)->position-offset)<1e-7)copy=ref.id;
            require(copy!=kInvalidObjectId,"copy maps every source atom");ids.push_back(copy);}
        if(fused){
            const Vec2 extra[]={{30,46},{15,72},{-15,72},{-30,46}};auto previous=ids[2];
            for(auto p:extra){ObjectId id,b;s.doc.addAtom(transform(p)+offset,6,&id);s.doc.addBond(previous,id,&b);previous=id;ids.push_back(id);}
            ObjectId b;s.doc.addBond(previous,ids[3],&b);
        }
        const Document source=s.doc;s.input.bindDocument(s.doc.id());s.refresh();s.selected=s.all();const auto before=s.doc.digest();
        app.generateMechanismStep();
        require(s.doc.digest()!=before,"carbene ring contraction generates a product: "+app.status);
        require(app.status.find("已按箭头直接")==std::string::npos,"carbene migration uses the constrained solver");
        std::map<ObjectId,ObjectId> product;for(auto ref:s.selected)if(ref.kind==ObjectKind::Atom)product[studio_mechanism_products::sourceAtom(s.doc,ref.id)]=ref.id;
        require(product.size()==ids.size(),"contraction retains every atom");
        auto checkBond=[&](size_t a,size_t b,int order){auto id=s.doc.bondBetween(product.at(ids[a]),product.at(ids[b]));auto bond=s.doc.bonds().alive(id);require(order?(bond&&int(bond->order)==order):!bond,"ketene contraction connectivity");};
        checkBond(0,4,0);checkBond(1,4,1);checkBond(0,1,2);checkBond(0,5,2);
        checkBond(1,2,1);checkBond(2,3,1);checkBond(3,4,1);
        for(auto item:product){auto a=s.doc.atoms().alive(item.second);require(!a->semantics.charge&&!a->semantics.radical,"neutral ketene has no residual charge or carbene");}
        auto pos=[&](size_t i){return s.doc.atoms().alive(product.at(ids[i]))->position;};
        require(length((pos(0)-pos(1))-(pos(5)-pos(0)))<1e-6,"product ketene is collinear with equal bond lengths");
        for(auto pair:std::vector<std::pair<size_t,size_t>>{{1,2},{2,3},{3,4},{4,1}})require(std::abs(length(pos(pair.first)-pos(pair.second))-30)<1e-6,"contracted ring is regular");
        if(fused){std::vector<size_t> fixed={2,3,6,7,8,9};for(auto i:fixed)for(auto j:fixed)
            require(length((pos(i)-pos(j))-(source.atoms().alive(ids[i])->position-source.atoms().alive(ids[j])->position))<1e-6,"entire fused six-membered ring preserves its orientation and geometry");}
        if(turn==0&&mirror==1){auto name=fused?"fused-carbene-contraction":"single-carbene-contraction";require(s.save(dir+"/"+name+".cdxml",true).ok(),"save carbene contraction example");s.selected.clear();s.fit();app.invalidate();app.screenshot(dir+"/"+name+".png");}
        require(s.undo().ok()&&s.doc.digest()==before,"carbene contraction is one exact undo");
    }
    std::ofstream(dir+"/fused-carbene-checks.json")<<"{\"cases\":8,\"copied_arrows\":true,\"neutral_ketene\":true,\"regular_contracted_ring\":true,\"linear_ketene\":true,\"fused_scaffold_fixed\":true,\"undo\":true}";
}
