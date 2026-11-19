#pragma once
static void checkReagentMechanisms(App& app,const std::string& dir){
    std::ofstream log(dir+"/reagent-mechanism-progress.txt");
    auto require=[&](bool ok,const std::string& message){log<<message<<": "<<(ok?"pass":"FAIL")<<std::endl;if(!ok)throw std::runtime_error(message);};
    auto reset=[&](){app.s.cancel();app.s.doc=Document();app.s.history.clear();app.s.selected.clear();app.s.input.bindDocument(app.s.doc.id());app.s.refresh();};
    int inorganic=0,organic=0;
    for(size_t i=0;i<reagentEntries().size();++i){
        auto source=readReagentTemplate(int(i));
        const auto& entry=reagentEntries()[i];
        const auto expected=source->doc.stats();
        require(expected.aliveAtoms>0&&source->doc.validate().ok(),std::string("editable reagent ")+entry.title);
        if(entry.inorganic){
            ++inorganic;
            require(source->valenceWarnings().empty(),std::string("valid inorganic template: ")+entry.alias);
            reset();
            app.bioSource=source;
            app.placeReagent(Vec2(50,50));
            const auto inserted=app.s.doc.stats();
            require(inserted.aliveAtoms==expected.aliveAtoms&&inserted.aliveBonds==expected.aliveBonds&&
                app.s.valenceWarnings().empty(),std::string("inserted without valence warnings: ")+entry.alias);
        }else ++organic;
    }
    require(inorganic>=60&&organic>=60,"both reagent catalogs are installed");
    reset();app.insertReactiveCenter(true);
    ObjectId center=kInvalidObjectId;
    for(const auto& a:app.s.doc.atoms().records())if(a.isAlive()&&mechanism::cumuleneCenter(app.s.doc,a))center=a.id;
    require(center!=kInvalidObjectId&&!app.s.doc.atoms().alive(center)->semantics.radical,"allene has an automatic central dot with no radical semantics");
    require(app.s.doc.stats().aliveAtoms==3&&app.s.doc.stats().aliveBonds==2,"allene insertion creates two editable double bonds");
    require(app.s.save(dir+"/allene.cdxml",true).ok(),"save editable allene");
    Session reopened;require(reopened.open(dir+"/allene.cdxml").ok()&&mechanism::cumuleneCenter(reopened.doc,*reopened.doc.atoms().alive(center)),"allene dot survives roundtrip");
    auto atom=[](Document& d,Vec2 p,int z,int charge=0,int h=-1,int radical=0){
        ObjectId id;d.addAtom(p,z,&id);auto s=d.atoms().alive(id)->semantics;
        s.charge=charge;s.implicitHydrogens=h;s.radical=radical;d.setAtomSemantics(id,s);return id;
    };
    auto bond=[](Document& d,ObjectId a,ObjectId b,int order=1){ObjectId id;d.addBond(a,b,&id);d.setBondOrder(id,BondOrder(order));return id;};
    auto A=[](ObjectId id){return ObjectRef(ObjectKind::Atom,id);};
    auto B=[](ObjectId id){return ObjectRef(ObjectKind::Bond,id);};
    auto product=[](const Document& d,const std::vector<ObjectRef>& made,ObjectId old){
        for(auto ref:made)if(ref.kind==ObjectKind::Atom&&studio_mechanism_products::sourceAtom(d,ref.id)==old)return ref.id;
        return kInvalidObjectId;
    };
    auto saveExample=[&](const Document& doc,const std::string& name){
        const Document snapshot=doc;reset();app.s.doc=snapshot;app.s.refresh();app.s.fit();
        require(app.s.save(dir+"/"+name+".cdxml",true).ok(),"save editable "+name);
        app.s.selected.clear();app.switchTab(1);app.layout();app.invalidate();app.screenshot(dir+"/"+name+".png");
    };
    reset();app.insertReactiveCenter(false);
    require(app.s.doc.stats().aliveAtoms==1&&app.s.doc.atoms().alive(0)->semantics.implicitHydrogens==2&&
        app.s.doc.atoms().alive(0)->semantics.radical==uint8_t(Radical::Singlet),"carbene button inserts divalent CH2");
    require(arrows::mechanismVisualBoxes(app.s.doc).at(0).hasBody,"isolated carbene has a visible CH2 label");
    saveExample(app.s.doc,"inserted-carbene");
    {
        Document d;auto c=atom(d,Vec2(20,20),6,-1,0);
        auto x=atom(d,Vec2(0,20),17,0,0);auto leaving=bond(d,c,x);
        bond(d,c,atom(d,Vec2(30,3),17,0,0));bond(d,c,atom(d,Vec2(30,37),17,0,0));
        auto before=d.digest();std::vector<ObjectRef> made;
        auto e=mechanism::generateNextStep(d,{{B(leaving),A(x)}},&made);
        require(e.ok(),"alpha elimination produces a carbene: "+e.message);
        auto pc=d.atoms().alive(product(d,made,c)),px=d.atoms().alive(product(d,made,x));
        require(pc&&pc->semantics.radical==uint8_t(Radical::Singlet)&&!pc->semantics.charge&&px->semantics.charge==-1,"alpha elimination conserves charge and produces a singlet carbon");
        require(mechanism::lonePairCapacity(d,pc->id)==1,"singlet carbene exposes its nonbonding electron pair");
        saveExample(d,"alpha-elimination");
    }
    for(bool stereo:{false,true}){
        Document d;auto c=atom(d,Vec2(65,25),6,0,2,int(Radical::Singlet));
        auto a=atom(d,Vec2(10,20),6),b=atom(d,Vec2(30,20),6);
        auto pi=bond(d,a,b,2);
        ObjectId marked=kInvalidObjectId;
        if(stereo){
            marked=bond(d,a,atom(d,Vec2(0,3),6));
            d.setBondDisplay(marked,BondDisplay::Wedge);d.setBondStereo(marked,BondStereo::Up);
        }
        std::vector<ObjectRef> made;
        auto e=mechanism::generateNextStep(d,{{B(pi),A(c)},{A(c),A(a)}},&made);
        require(e.ok(),"carbene addition to an alkene: "+e.message);
        const auto ca=product(d,made,a),cb=product(d,made,b),cc=product(d,made,c);
        require(d.bondBetween(ca,cc)!=kInvalidObjectId&&d.bondBetween(cb,cc)!=kInvalidObjectId&&
                d.bonds().alive(d.bondBetween(ca,cb))->order==BondOrder::Single&&!d.atoms().alive(cc)->semantics.radical,
                "carbene cyclopropanation forms all three ring edges and consumes the pair");
        if(!stereo)saveExample(d,"carbene-cyclopropanation");
    }
    {
        Document d;
        const auto c=atom(d,Vec2(0,20),6),oh=atom(d,Vec2(17,10),8,0,1),iodine=atom(d,Vec2(65,20),53,0,0);
        const auto stereo=bond(d,c,oh);d.setBondDisplay(stereo,BondDisplay::WedgeHash);d.setBondStereo(stereo,BondStereo::Down);
        const auto ox=atom(d,Vec2(65,0),8,0,0);const auto pi=bond(d,iodine,ox,2);
        bond(d,iodine,atom(d,Vec2(85,20),8,0,0),2);
        bond(d,iodine,atom(d,Vec2(65,40),8,0,0),2);
        bond(d,iodine,atom(d,Vec2(45,20),8,-1,0));
        std::vector<ObjectRef> made;
        auto e=mechanism::generateNextStep(d,{{A(oh),A(iodine)},{B(pi),A(ox)}},&made);
        require(e.ok(),"A044 alcohol attacks hypervalent iodine: "+e.message);
        require(d.atoms().alive(product(d,made,oh))->semantics.charge==1&&
                d.atoms().alive(product(d,made,ox))->semantics.charge==-1&&
                d.atoms().alive(product(d,made,iodine))->semantics.charge==0,"periodate addition has the correct charges");
        require(validation::overValentAtoms(d).empty(),"periodate addition has no false valence warning");
        const auto* wedge=d.bonds().alive(d.bondBetween(product(d,made,c),product(d,made,oh)));
        require(wedge&&wedge->display==BondDisplay::WedgeHash,"periodate addition preserves adjacent carbon stereochemistry");
        saveExample(d,"periodate-addition");
    }
    {
        Document source;auto c=atom(source,Vec2(20,20),6),o=atom(source,Vec2(40,20),8,1,2);
        auto co=bond(source,c,o);
        bond(source,c,atom(source,Vec2(10,3),6));bond(source,c,atom(source,Vec2(10,37),6));bond(source,c,atom(source,Vec2(0,20),6));
        Document d=source;std::vector<ObjectRef> made;
        auto e=mechanism::generateNextStep(d,{{B(co),A(o)}},&made);require(e.ok(),"water leaving group graph");
        e=studio_mechanism_products::finish(d,source,made,0);require(e.ok(),"finish dehydration");
        require(!d.atoms().alive(product(d,made,o)),"newly detached water is omitted from the next structure");
        bool caption=false;for(auto ref:made)if(ref.kind==ObjectKind::Text)caption|=d.plainText(ref.id)=="\xE2\x88\x92 H2O";
        require(caption,"dehydration is written on the reaction arrow");
    }
    {
        Document d;
        const auto i=atom(d,Vec2(50,0),53,0,0),a=atom(d,Vec2(30,20),8,0,0),b=atom(d,Vec2(70,20),8,0,0);
        const auto x=atom(d,Vec2(30,40),6,0,1),y=atom(d,Vec2(70,40),6,0,1);
        const auto ia=bond(d,i,a),ib=bond(d,i,b),ax=bond(d,a,x),by=bond(d,b,y),xy=bond(d,x,y);
        bond(d,x,atom(d,Vec2(10,50),6));bond(d,y,atom(d,Vec2(90,50),6));
        bond(d,i,atom(d,Vec2(30,-20),8,0,0),2);bond(d,i,atom(d,Vec2(70,-20),8,0,0),2);
        bond(d,i,atom(d,Vec2(50,-30),8,-1,0));
        std::vector<ObjectRef> made;
        auto e=mechanism::generateNextStep(d,{{B(xy),B(ax)},{B(ia),A(i)},{B(ib),B(by)}},&made);
        require(e.ok(),"A044 cyclic periodate ester fragmentation: "+e.message);
        require(d.bondBetween(product(d,made,x),product(d,made,y))==kInvalidObjectId&&
            d.bonds().alive(d.bondBetween(product(d,made,a),product(d,made,x)))->order==BondOrder::Double&&
            d.bonds().alive(d.bondBetween(product(d,made,b),product(d,made,y)))->order==BondOrder::Double,
            "periodate cleavage produces both carbonyls and breaks the carbon-carbon bond");
        saveExample(d,"periodate-cleavage");
    }
    {
        Document source;
        const auto c=atom(source,Vec2(40,20),6,0,0),o=atom(source,Vec2(40,0),8,0,0),h=atom(source,Vec2(20,0),1,0,0);
        const auto cl=atom(source,Vec2(60,20),17,0,0),end=atom(source,Vec2(20,40),6,0,2);
        const auto oh=bond(source,o,h),cx=bond(source,c,cl),co=bond(source,o,c);bond(source,c,end,2);
        Document d=source;std::vector<ObjectRef> made;
        auto e=mechanism::generateNextStep(d,{{B(oh),B(co)},{B(cx),A(h)}},&made);
        require(e.ok(),"A066 elimination to a ketene: "+e.message);
        require(mechanism::cumuleneCenter(d,*d.atoms().alive(product(d,made,c))),"ketene gets the same automatic cumulene center dot");
        e=studio_mechanism_products::finish(d,source,made,0);require(e.ok(),"finish HCl elimination");
        bool caption=false;for(auto ref:made)if(ref.kind==ObjectKind::Text)caption|=d.plainText(ref.id)=="\xE2\x88\x92 HCl";
        require(caption&&!d.atoms().alive(product(d,made,cl)),"HCl is omitted and placed above the arrow");
        saveExample(d,"ketene-HCl-elimination");
    }
    for(const auto& loss:studio_mechanism_products::smallLosses()){
        Document d;std::vector<ObjectId> part;
        for(size_t i=0;i<loss.elements.size();++i)part.push_back(atom(d,Vec2(20*i,0),loss.elements[i],0,loss.hydrogens[i]));
        for(const auto& edge:loss.bonds)bond(d,part[edge[0]],part[edge[1]],edge[2]);
        require(studio_mechanism_products::matchesLoss(d,part,loss),std::string("small molecule graph recognition ")+loss.name);
        auto sem=d.atoms().alive(part.front())->semantics;sem.isotope=18;d.setAtomSemantics(part.front(),sem);
        require(!studio_mechanism_products::matchesLoss(d,part,loss),"isotopically labelled molecules remain visible");
    }
    for(int mode:{1,2}){
        reset();auto& source=app.s.doc;
        auto o=atom(source,Vec2(20,20),8,1,2);bond(source,o,atom(source,Vec2(0,20),6));
        ObjectId n=kInvalidObjectId;
        if(mode==1){n=atom(source,Vec2(80,20),7,-1,1);bond(source,n,atom(source,Vec2(100,20),6));}
        app.s.refresh();app.s.selected=app.s.all();auto before=source.digest();
        app.generateMechanismStep(mode);
        ObjectId po=product(source,app.s.selected,o);
        require(po!=kInvalidObjectId&&!source.atoms().alive(po)->semantics.charge&&
                source.atoms().alive(po)->semantics.implicitHydrogens==1,"automatic proton option updates the donor");
        if(mode==1){
            auto pn=product(source,app.s.selected,n);
            require(pn!=kInvalidObjectId&&!source.atoms().alive(pn)->semantics.charge&&
                    source.atoms().alive(pn)->semantics.implicitHydrogens==2,"proton transfer updates the accepting nitrogen");
        }
        require(app.s.undo().ok()&&source.digest()==before,"automatic proton operation is one undo");
    }
    reset();
}
