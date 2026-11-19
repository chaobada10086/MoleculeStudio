#pragma once
static void checkExpandedReagents(App& app,const std::string& dir){
    std::ofstream log(dir+"/expanded-reagent-progress.txt");
    auto require=[&](bool value,const std::string& why){log<<why<<": "<<(value?"pass":"FAIL")<<std::endl;if(!value)throw std::runtime_error(why);};
    auto reset=[&](){app.s.cancel();app.s.doc=Document();app.s.history.clear();app.s.selected.clear();app.s.input.bindDocument(app.s.doc.id());app.s.refresh();};
    auto atom=[](Document& d,Vec2 p,int z,int h,int q=0){ObjectId id;d.addAtom(p,z,&id);auto sem=d.atoms().alive(id)->semantics;sem.implicitHydrogens=h;sem.charge=q;d.setAtomSemantics(id,sem);return id;};
    auto bond=[](Document& d,ObjectId a,ObjectId b,int order=1){ObjectId id;d.addBond(a,b,&id);d.setBondOrder(id,BondOrder(order));return id;};
    auto A=[](ObjectId id){return ObjectRef(ObjectKind::Atom,id);};auto B=[](ObjectId id){return ObjectRef(ObjectKind::Bond,id);};
    auto product=[&](ObjectId original){for(auto ref:app.s.selected)if(ref.kind==ObjectKind::Atom&&studio_mechanism_products::sourceAtom(app.s.doc,ref.id)==original)return ref.id;return kInvalidObjectId;};
    auto save=[&](const std::string& name){app.s.refresh();app.s.selected.clear();app.s.fit();app.s.save(dir+"/"+name+".cdxml",true);app.switchTab(1);app.layout();app.invalidate();app.screenshot(dir+"/"+name+".png");};
    int linear=0;bool lawesson=false,hatu=false,pcl5=false;
    for(size_t index=0;index<reagentEntries().size();++index){
        auto source=readReagentTemplate(int(index));const auto& d=source->doc;const std::string alias=reagentEntries()[index].alias;
        lawesson|=alias.find("Lawesson")!=std::string::npos;hatu|=alias=="HATU";
        for(const auto& a:d.atoms().records())if(a.isAlive()){
            const auto& links=d.incidentBonds(a.id);
            if((a.element==6||a.element==7)&&links.size()==2){
                const auto& one=*d.bonds().alive(links[0]);const auto& two=*d.bonds().alive(links[1]);
                const int x=int(one.order),y=int(two.order);
                if(!((x==3&&y==1)||(x==1&&y==3)||(x==2&&y==2)))continue;
                auto u=d.atoms().alive(one.beginAtom==a.id?one.endAtom:one.beginAtom)->position-a.position;
                auto v=d.atoms().alive(two.beginAtom==a.id?two.endAtom:two.beginAtom)->position-a.position;
                require(dot(u,v)/(length(u)*length(v))<-.99999,"linear center in "+alias);++linear;
            }
            if(alias=="PCl5"&&a.element==15){
                int wedges=0,hashes=0;std::vector<Vec2> axial;
                for(auto id:links){const auto& b=*d.bonds().alive(id);wedges+=b.display==BondDisplay::Wedge;hashes+=b.display==BondDisplay::WedgeHash;
                    auto offset=d.atoms().alive(b.beginAtom==a.id?b.endAtom:b.beginAtom)->position-a.position;
                    if(std::abs(offset.x)<1e-7)axial.push_back(offset);
                }
                require(wedges==1&&hashes==1&&axial.size()==2&&dot(axial[0],axial[1])<0,"PCl5 has two opposite axial bonds and explicit equatorial depth");pcl5=true;
            }
        }
        if(alias=="NaN3"||alias=="DDQ"||alias=="PCl5"||alias=="HATU"||alias.find("Lawesson")!=std::string::npos){reset();app.s.doc=d;save("review-"+(alias.find("Lawesson")!=std::string::npos?std::string("Lawesson"):alias));}
    }
    require(linear>=30&&lawesson&&hatu&&pcl5&&reagentEntries().size()>=380,"expanded library includes reviewed reagents and linear centers");
    for(int origin=0;origin<5;++origin){
        reset();RingLibraryPicker picker;
        picker.model=origin==0?ringLibraryModel():origin==1?bioLibraryModel():origin==2?commonReagentModel(true):origin==3?commonReagentModel(false):custom_templates::modelFor(custom_templates::entries());
        const auto local=picker.model.parents.size();app.shareTemplateSearch(picker,origin);
        require(picker.model.parents.size()==local,"opening a library preserves its normal browsing list");
        picker.expandSharedSearch();
        for(const auto& query:std::vector<std::wstring>{L"HATU",L"ATP",L"adamantane",L"bu3snh",L"CO2",L"CS2"}){
            bool found=false;for(size_t parent=0;parent<picker.model.parents.size();++parent)if(picker.model.matches(int(parent),query)){
                found=true;const auto index=picker.model.parents[parent].firstVariant;
                require(picker.model.load(index)->doc.stats().aliveAtoms>0,"shared search result loads its original editable template");
                if(origin==0&&query==L"HATU"){
                    auto before=app.s.doc.digest();require(picker.chooseSharedResult(index)&&app.bioPlacing&&app.reagentPlacing,"ring library can select an organic reagent result");
                    const auto count=app.bioSource->doc.stats().aliveAtoms;app.placeBioComponent(Vec2(45,45));
                    require(app.s.doc.stats().aliveAtoms==count&&app.s.undo().ok()&&app.s.doc.digest()==before,"foreign search result places correctly in one undo");
                }
            }
            require(found,"every library searches rings, biomolecules and reagents");
        }
    }
    for(const auto& input:std::vector<std::wstring>{L"oh-",L"meo-",L"EtO-",L"tbuo-",L"AcO-",L"NH2-",L"H3O+",L"NH4+"}){
        reset();const bool layout=chemcompletion::formulaLayout();chemcompletion::formulaLayout()=true;
        app.openText(kInvalidObjectId);app.textEditor.select(0,-1);
        studio_i18n::sendMessage(app.textEditor.hwnd,EM_REPLACESEL,TRUE,(LPARAM)input.c_str());
        log<<"input="<<U(input)<<" editor="<<U(app.textEditor.value())<<std::endl;
        require(app.finishText(true),"commit charged formula");chemcompletion::formulaLayout()=layout;
        log<<"atoms="<<app.s.doc.stats().aliveAtoms<<" texts="<<app.s.doc.stats().aliveTexts<<" selection="<<app.s.selected.size()<<std::endl;
        for(const auto& t:app.s.doc.texts().records())if(t.isAlive())log<<"caption="<<app.s.doc.plainText(t.id)<<std::endl;
        require(app.s.selected.size()==1&&app.s.selected.front().kind==ObjectKind::Atom,"charged input creates an atom rather than a caption");
        const auto id=app.s.selected.front().id;const auto* a=app.s.doc.atoms().alive(id);int q=0;const auto* entry=studioions::find(U(input),&q);
        require(entry&&a->element==entry->element&&a->semantics.charge==q&&studioions::hiddenValence(app.s.doc,id)==entry->hidden,"typed ion preserves charge and compact group valence");
        require(arrows::mechanismVisualBoxes(app.s.doc).at(id).hasCharge,"typed ion has a capturable draggable charge marker");
        if(input==L"meo-")save("typed-methoxide");
    }
    reset();{
        ObjectId text;app.s.doc.addText(Vec2(40,40),10,"OH",&text);auto before=app.s.doc.digest();app.s.selected={{ObjectKind::Text,text}};
        app.applyMechanismElectronState(-1);
        require(app.s.selected.size()==1&&app.s.selected.front().kind==ObjectKind::Atom,"OH caption accepts the negative charge button");
        auto a=app.s.doc.atoms().alive(app.s.selected.front().id);
        require(a->element==8&&a->semantics.charge==-1&&a->semantics.implicitHydrogens==1,"OH becomes hydroxide with one hydrogen");
        require(app.s.undo().ok()&&app.s.doc.digest()==before,"caption to hydroxide is one undo");
    }
    auto arrow=[&](ObjectRef from,ObjectRef to,Vec2 begin,Vec2 end){
        auto p=arrows::resolvePreset(49,begin,end,.6);p.mechanism=true;p.electronSource=from;p.electronTarget=to;p.head.sides=arrows::HeadSides::Both;
        ObjectId id;auto e=arrows::addArrow(app.s.doc,p,.6,&id);require(e.ok(),"add explicit mechanism arrow");return id;
    };
    reset();{
        auto& d=app.s.doc;
        const auto c=atom(d,Vec2(70,48),6,3),br=atom(d,Vec2(48,48),35,0);
        bond(d,c,br);app.s.refresh();
        const auto before=d.digest();
        for(int sign:{-1,1}){
            app.execute(Button("","","","@partialCharge",sign));
            require(app.partialChargePlacement==sign,"partial charge ribbon tool arms text placement");
            auto point=app.s.view().toDevice(Vec2(sign<0?48:70,33));
            SendMessageW(app.canvas,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(int(point.x),int(point.y)));
            SendMessageW(app.canvas,WM_LBUTTONUP,0,MAKELPARAM(int(point.x),int(point.y)));
            require(!app.partialChargePlacement&&app.s.selected.size()==1&&app.s.selected.front().kind==ObjectKind::Text,
                    "canvas click inserts an editable text annotation");
            const auto& runs=d.runs(app.s.selected.front().id);
            require(runs.size()==2&&runs[0].utf8=="δ"&&(runs[1].flags&kSuperscript)&&runs[1].utf8==(sign<0?"−":"+"),
                    "partial charge sign is visibly superscripted");
        }
        require(d.stats().aliveAtoms==2&&d.stats().aliveBonds==1&&d.stats().aliveTexts==2&&
                d.atoms().alive(c)->semantics.charge==0&&d.atoms().alive(br)->semantics.charge==0&&app.s.valenceWarnings().empty(),
                "partial charge labels do not modify the chemical graph or its valence");
        require(app.s.undo().ok()&&app.s.undo().ok()&&d.digest()==before,"each partial charge annotation is one undo");
        app.execute(Button("","","","@partialCharge",-1));
        SendMessageW(app.canvas,WM_KEYDOWN,VK_ESCAPE,0);
        require(!app.partialChargePlacement&&d.digest()==before,"Escape cancels partial charge placement");
    }
    for(bool staged:{false,true}){
        reset();
        auto& d=app.s.doc;
        d.setDefaultBondLength(25);
        const auto c=atom(d,Vec2(65,45),6,0),o=atom(d,Vec2(65,20),8,0);
        const auto me=atom(d,Vec2(44,58),6,3),cl=atom(d,Vec2(86,58),17,0);
        const auto nu=atom(d,Vec2(18,85),6,0);
        require(mechanism::setGenericRole(d,nu,"nucleophile").ok(),"insert editable Nu minus for acyl substitution");
        const auto carbonyl=bond(d,c,o,2),carbonMethyl=bond(d,c,me),carbonChlorine=bond(d,c,cl);
        const auto opening=arrow(A(c),A(o),Vec2(67,41),Vec2(70,22));
        if(!staged){
            // The short return arrow has captured the adjacent C–Cl line.
            // The matching opening arrow identifies it as a return to C–O.
            const auto closing=arrow(A(o),B(carbonChlorine),Vec2(60,22),Vec2(66,44));
            app.s.refresh();app.s.selected={{ObjectKind::Arrow,opening},{ObjectKind::Arrow,closing}};
            const auto before=d.digest();app.generateMechanismStep();
            require(d.digest()==before&&app.status.find("互相抵消")!=std::string::npos,"reciprocal carbonyl arrows alone report no net change");
        }
        arrow(A(nu),B(carbonMethyl),Vec2(25,83),Vec2(62,46));
        if(!staged)arrow(B(carbonChlorine),A(cl),Vec2(76,52),Vec2(84,57));
        app.s.refresh();app.s.selected=app.s.all();app.generateMechanismStep();
        ObjectId pc=product(c),po=product(o),pn=product(nu),px=product(cl),pm=product(me);
        require(pc!=kInvalidObjectId&&po!=kInvalidObjectId&&pn!=kInvalidObjectId&&px!=kInvalidObjectId&&pm!=kInvalidObjectId,
                "acyl addition and elimination generate products: "+app.status);
        auto order=[&](ObjectId a,ObjectId b){auto edge=d.bonds().alive(d.bondBetween(a,b));return edge?int(edge->order):0;};
        auto charge=[&](ObjectId id){return d.atoms().alive(id)->semantics.charge;};
        if(staged){
            require(order(pc,po)==1&&order(pc,pn)==1&&order(pc,px)==1&&charge(po)==-1&&charge(pn)==0,
                    "separate addition gives a tetrahedral alkoxide with chloride still attached");
            const auto& cp=d.atoms().alive(pc)->position;
            const auto& op=d.atoms().alive(po)->position;
            const auto& xp=d.atoms().alive(px)->position;
            const auto sigma=d.bondBetween(pc,px);
            const auto a=arrow(A(po),B(sigma),op,cp+(xp-cp)*.08);
            const auto b=arrow(B(sigma),A(px),(cp+xp)*.5,xp);
            app.s.selected={{ObjectKind::Arrow,a},{ObjectKind::Arrow,b}};
            app.s.refresh();app.generateMechanismStep();
            pc=product(pc);po=product(po);pn=product(pn);px=product(px);pm=product(pm);
            require(pc!=kInvalidObjectId&&po!=kInvalidObjectId&&pn!=kInvalidObjectId&&px!=kInvalidObjectId&&pm!=kInvalidObjectId,
                    "collapse of the tetrahedral intermediate generates products: "+app.status);
        }
        require(order(pc,po)==2&&order(pc,pn)==1&&order(pc,pm)==1&&order(pc,px)==0&&
                charge(pc)==0&&charge(po)==0&&charge(pn)==0&&charge(px)==-1,
                "acyl substitution restores C=O and preserves methyl, Nu and detached chloride");
        require(app.s.valenceWarnings().empty(),"acyl substitution does not create an overvalent carbon");
        save(staged?"acyl-substitution-staged":"acyl-substitution-four-arrows");
    }
    reset();{
        auto& d=app.s.doc;auto base=atom(d,Vec2(10,5),6,0);mechanism::setGenericRole(d,base,"base");
        auto c=atom(d,Vec2(65,35),6,0,1);for(auto p:{Vec2(65,13),Vec2(46,46),Vec2(84,46)})bond(d,c,atom(d,p,6,3));
        auto target=d.incidentBonds(c).back();arrow(A(base),B(target),Vec2(15,5),Vec2(60,35));
        app.s.refresh();auto before=d.digest();app.s.selected.clear();app.generateMechanismStep();require(d.digest()==before&&app.s.selected.empty(),"empty selection stays empty and never generates automatically");
        app.s.selected=app.s.all();app.generateMechanismStep();auto b=product(base),pc=product(c);
        require(b!=kInvalidObjectId&&pc!=kInvalidObjectId&&d.bondBetween(b,pc)!=kInvalidObjectId,"B minus can attack a carbocation despite a nearby bond capture");
        require(!d.atoms().alive(b)->semantics.charge&&!d.atoms().alive(pc)->semantics.charge,"direct bond drawing transfers the pair and neutralizes both charges");
        require(app.s.undo().ok()&&d.digest()==before,"drawn mechanism is one undo");
        app.s.selected=app.s.all();app.generateMechanismStep();save("drawn-base-attack");
    }
    reset();{
        auto& d=app.s.doc;auto nu=atom(d,Vec2(0,40),6,0);mechanism::setGenericRole(d,nu,"nucleophile");
        auto a=atom(d,Vec2(45,15),6,2),b=atom(d,Vec2(67,15),6,2);auto pi=bond(d,a,b,2);
        arrow(A(nu),B(pi),Vec2(8,36),Vec2(56,15));app.s.refresh();app.s.selected=app.s.all();app.generateMechanismStep();
        auto pn=product(nu),pa=product(a),pb=product(b);
        require(pn!=kInvalidObjectId&&pa!=kInvalidObjectId&&pb!=kInvalidObjectId&&d.bondBetween(pn,pa)!=kInvalidObjectId,"pair arrow into a pi bond generates addition without a second arrow");
        require(d.bonds().alive(d.bondBetween(pa,pb))->order==BondOrder::Single,"addition to a pi bond consumes one pi bond");save("drawn-pi-addition");
    }
    reset();{
        auto& d=app.s.doc;auto base=atom(d,Vec2(0,5),6,0);mechanism::setGenericRole(d,base,"base");
        auto alpha=atom(d,Vec2(42,45),6,1),beta=atom(d,Vec2(64,32),6,1),h=atom(d,Vec2(64,10),1,0);
        bond(d,alpha,atom(d,Vec2(23,32),6,3));auto ab=bond(d,alpha,beta),bh=bond(d,beta,h);
        auto ome=atom(d,Vec2(42,67),6,0);d.setAtomLabel(ome,"OMe");auto old=d.atoms().alive(ome)->semantics;old.nodeType=uint8_t(NodeType::Nickname);d.setAtomSemantics(ome,old);auto leaving=bond(d,alpha,ome);
        auto acyl=atom(d,Vec2(86,45),6,0);bond(d,beta,acyl);bond(d,acyl,atom(d,Vec2(86,67),8,0),2);bond(d,acyl,atom(d,Vec2(105,32),6,3));
        arrow(A(base),A(h),Vec2(8,5),Vec2(60,10));arrow(B(bh),B(ab),Vec2(64,21),Vec2(53,38));arrow(B(leaving),A(ome),Vec2(42,56),Vec2(42,64));
        app.s.refresh();app.s.selected=app.s.all();app.generateMechanismStep();auto po=product(ome),pa=product(alpha),pb=product(beta);
        require(po!=kInvalidObjectId&&pa!=kInvalidObjectId&&pb!=kInvalidObjectId,"OMe elimination generates a product");
        require(d.bondBetween(pa,po)==kInvalidObjectId&&d.bonds().alive(d.bondBetween(pa,pb))->order==BondOrder::Double,"OMe elimination breaks C-O and forms the requested C=C");
        log<<"OMe product element="<<d.atoms().alive(po)->element<<" q="<<int(d.atoms().alive(po)->semantics.charge)<<" hidden="<<studioions::hiddenValence(d,po)<<" label="<<d.atomLabel(po)<<std::endl;
        app.s.save(dir+"/debug-OMe.cdxml",true);
        require(d.atoms().alive(po)->element==8&&d.atoms().alive(po)->semantics.charge==-1&&studioions::hiddenValence(d,po)==1,"OMe leaves as a compact methoxide ion");save("OMe-elimination");
    }
    reset();{
        auto& d=app.s.doc;auto c=atom(d,Vec2(60,50),6,0),n=atom(d,Vec2(40,30),7,2),o=atom(d,Vec2(80,30),8,2,1);
        auto cn=bond(d,c,n),co=bond(d,c,o);auto left=atom(d,Vec2(40,70),6,3),right=atom(d,Vec2(80,70),6,3);bond(d,c,left);bond(d,c,right);
        d.extensions(A(n)).set(mechanism::lonePairKey(),"1");
        arrow(A(n),B(cn),Vec2(36,25),Vec2(50,40));arrow(B(co),A(o),Vec2(70,40),Vec2(78,32));
        app.s.refresh();app.s.selected=app.s.all();app.generateMechanismStep(2);
        const auto pc=product(c),pn=product(n),pl=product(left),pr=product(right);
        require(pc!=kInvalidObjectId&&pn!=kInvalidObjectId&&pl!=kInvalidObjectId&&pr!=kInvalidObjectId,"dehydration plus deprotonation produces the imine");
        auto axis=d.atoms().alive(pn)->position-d.atoms().alive(pc)->position;
        auto a=d.atoms().alive(pl)->position-d.atoms().alive(pc)->position,b=d.atoms().alive(pr)->position-d.atoms().alive(pc)->position;
        require(std::abs(dot(axis,a)/(length(axis)*length(a))+.5)<1e-7&&std::abs(dot(axis,b)/(length(axis)*length(b))+.5)<1e-7&&std::abs(dot(a,b)/(length(a)*length(b))+.5)<1e-7,"final imine carbon has three 120 degree angles");
        bool protonCaption=false,waterCaption=false,super=false;double protonY=0,waterY=0;for(auto ref:app.s.selected)if(ref.kind==ObjectKind::Text){
            const auto text=d.plainText(ref.id);if(text=="\xE2\x88\x92 H+"){protonCaption=true;protonY=d.texts().alive(ref.id)->position.y;}if(text=="\xE2\x88\x92 H2O"){waterCaption=true;waterY=d.texts().alive(ref.id)->position.y;}
            log<<"loss caption="<<text<<std::endl;
            for(const auto& run:d.runs(ref.id)){log<<"run="<<run.utf8<<" flags="<<run.flags<<std::endl;if(run.utf8=="+"&&(run.flags&kSuperscript))super=true;}
        }
        app.s.save(dir+"/debug-imine.cdxml",true);
        require(protonCaption&&waterCaption&&super&&std::abs(protonY-waterY)>=d.defaultTextSize()*1.5,"proton and water losses occupy separate spaced rows, charge stays superscripted");save("imine-dehydration-deprotonation");
    }
    reset();
}
