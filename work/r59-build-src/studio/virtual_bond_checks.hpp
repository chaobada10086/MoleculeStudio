#pragma once
static void checkVirtualBonds(App& app,const std::string& dir){
    auto require=[](bool ok,const std::string& why){if(!ok)throw std::runtime_error("virtual bond: "+why);};
    auto& s=app.s;
    for(int mode:{0,1,2}){
        s.cancel();s.doc=Document();s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());s.refresh();
        const Vec2 points[]={{0,0},{-26,15},{-26,45},{0,60},{30,45},{30,15}};
        std::vector<ObjectId> atoms,bonds;
        for(int i=0;i<6;++i){ObjectId id;s.doc.addAtom(points[i],mode==2&&i==5?1:6,&id);atoms.push_back(id);}
        const std::vector<std::array<int,3>> links=mode==0?
            std::vector<std::array<int,3>>{{{0,1,2}},{{1,2,1}},{{2,3,2}},{{4,5,2}}}:
            mode==1?std::vector<std::array<int,3>>{{{0,1,2}},{{1,2,1}},{{2,3,1}},{{3,4,1}},{{4,5,2}}}:
            std::vector<std::array<int,3>>{{{0,1,2}},{{1,2,1}},{{2,3,2}},{{3,4,1}},{{4,5,1}}};
        for(auto link:links){ObjectId id;s.doc.addBond(atoms[link[0]],atoms[link[1]],&id);s.doc.setBondOrder(id,BondOrder(link[2]));bonds.push_back(id);}
        s.refresh();s.fit();
        const auto original=s.doc;
        auto A=[&](int n){return ObjectRef(ObjectKind::Atom,atoms[n]);};
        auto B=[&](int n){return ObjectRef(ObjectKind::Bond,bonds[n]);};
        auto addGuide=[&](int a,int b){
            app.execute(Button("","","","@virtualBond"));
            auto capture=[&](ObjectId id){
                for(const auto& item:s.ghost.items())if(item.subject==ObjectRef(ObjectKind::Atom,id)&&
                    item.style.colorRGBA==0x0078D4FFu&&item.points.size()>8){
                    require(std::abs(length(item.points.front()-s.doc.atoms().alive(id)->position)-
                        s.view().deviceRadiusToDocument(14.))<1e-7,"guide capture has the mechanism-arrow circle radius");
                    return true;
                }
                return false;
            };
            app.virtualBondGesture(PointerPhase::Move,points[a]);
            require(capture(atoms[a]),"idle guide hover displays the full-size capture circle");
            app.virtualBondGesture(PointerPhase::Down,points[a]);
            app.virtualBondGesture(PointerPhase::Move,points[b]);
            require(capture(atoms[a])&&capture(atoms[b]),"guide drag displays both endpoint capture circles");
            if(mode==0){app.switchTab(6);app.render();app.screenshot(dir+"/virtual-bond-endpoint-capture.png");}
            app.virtualBondGesture(PointerPhase::Up,points[b]);
            require(s.ghost.empty(),"releasing guide drag clears endpoint capture circles");
            require(s.selected.size()==1&&virtual_bonds::isGuide(s.doc,s.selected.front()),"normal tool creates an anchored guide");
            return s.selected.front();
        };
        const auto first=mode==0?addGuide(3,4):mode==1?addGuide(0,5):addGuide(5,0);
        const auto second=mode==0?addGuide(5,0):ObjectRef();
        auto checkPink=[&](const Session& session){
            bool pink=false;
            for(const auto& primitive:session.scene.primitives())if(virtual_bonds::isGuide(session.doc,primitive.source)){
                require(primitive.style.stroke.rgba==virtual_bonds::color,"guide strokes are pink on the canvas");
                pink=true;
            }
            require(pink,"guide is visible on the canvas");
        };
        checkPink(s);
        {
            const auto before=s.doc.digest();const auto moved=points[0]+Vec2(3,2);
            require(s.submit(Command("移动待成键端点",kInvalidateGeometry,[&](CommandContext& c){
                auto e=c.document.setAtomPosition(atoms[0],moved);return e.ok()?CommandResult::success():CommandResult::fail(e.code,e.message);
            }),"移动待成键端点").ok(),"move an attached atom");
            const auto guide=mode==0?second:first;
            arrows::ArrowParams p;bool valid=false;arrows::readArrow(s.doc,guide.id,&p,&valid);
            require(valid&&(p.begin==moved||p.end==moved),"guide follows its moved atom");
            bool attached=false;for(const auto& primitive:s.scene.primitives())if(primitive.source==guide)
                for(auto point:primitive.points)attached|=length(point-moved)<1e-6;
            require(attached,"incremental canvas refresh follows the guide endpoint");
            require(s.undo().ok()&&s.doc.digest()==before,"undo restores the guide and its atom");
        }
        for(auto a:atoms){
            require(s.doc.incidentBonds(a).size()==original.incidentBonds(a).size(),"guide leaves connectivity unchanged");
            require(chemistry::refreshAtomDerived(s.doc,a,{}).newHydrogenCount==chemistry::refreshAtomDerived(original,a,{}).newHydrogenCount,
                    "guide leaves implicit hydrogens unchanged");
        }
        require(s.doc.stats().aliveBonds==original.stats().aliveBonds&&s.valenceWarnings().empty(),"guide creates no covalent bond or valence warning");
        auto checkGuides=[&](const Document& doc){
            int count=0;for(const auto& record:doc.arrows().records())if(record.isAlive()){
                arrows::ArrowParams p;bool valid=false;arrows::readArrow(doc,record.id,&p,&valid);
                if(p.virtualBond){++count;require(valid&&doc.atoms().alive(p.electronSource.id)&&doc.atoms().alive(p.electronTarget.id),"guide references live copied atoms");}
                if(p.mechanism&&p.electronTarget.kind==ObjectKind::Arrow)require(virtual_bonds::isGuide(doc,p.electronTarget),"electron arrow references its copied guide");
            }
            require(count==(mode==0?2:1),"guide count retained");
        };
        const std::vector<mechanism::ElectronMove> moves=mode==0?
            std::vector<mechanism::ElectronMove>{{B(0),B(1)},{B(2),first},{B(3),second}}:
            mode==1?std::vector<mechanism::ElectronMove>{{B(0),first},{B(4),B(3)},{B(2),B(1)}}:
            std::vector<mechanism::ElectronMove>{{B(4),first},{B(0),B(1)},{B(2),B(3)}};
        for(auto move:moves){
            Vec2 from,to;require(arrows::mechanismAnchorPoint(s.doc,move.source,&from)&&arrows::mechanismAnchorPoint(s.doc,move.target,&to),"guide has a mechanism anchor");
            auto p=arrows::resolvePreset(49,from,to,.6);p.mechanism=true;p.electronSource=move.source;p.electronTarget=move.target;
            p.head.length=3.2;p.head.width=2.24;arrows::routeMechanism(s.doc,p);
            require(arrows::addArrow(s.doc,p,.6,nullptr).ok(),"create electron arrows with guide targets");
        }
        s.refresh();app.execute(Button("","","","@mechanism"));
        auto tool=dynamic_cast<arrows::ArrowTool*>(s.entries[s.activeIndex].tool.get());
        Vec2 midpoint;arrows::mechanismAnchorPoint(s.doc,first,&midpoint);
        s.selected.clear();tool->hoverPreview(s,midpoint,0);
        bool captured=false;for(const auto& cue:s.ghost.items())captured|=cue.subject==first&&cue.style.colorRGBA==virtual_bonds::color;
        require(captured,"mechanism hover captures the guide in pink");
        for(bool xml:{false,true}){
            const auto path=dir+"/virtual-"+std::to_string(mode)+(xml?".cdxml":".r62doc");
            require(s.save(path,xml).ok(),"save guides and arrows");
            Session reload;require(reload.open(path).ok(),"reopen guides and arrows");checkGuides(reload.doc);checkPink(reload);
        }
        {
            Document copy;ObjectId unused;copy.addAtom(Vec2(-100,-100),6,&unused);
            ObjectId unrelated;auto unrelatedParams=arrows::resolvePreset(8,Vec2(-100,-100),Vec2(-80,-100),.6);arrows::addArrow(copy,unrelatedParams,.6,&unrelated);
            std::vector<ObjectRef> made;require(studioio::append(s.doc,copy,s.all(),Vec2(100,30),&made).ok(),"copy guides with fresh ids");
            checkGuides(copy);
        }
        app.s.cancel();s.selected=s.all();const auto before=s.doc.digest();
        app.generateMechanismStep();
        require(s.doc.stats().aliveAtoms==12,"normal generate-next-step creates the guided product");
        std::map<ObjectId,ObjectId> products;
        for(auto ref:s.selected)if(ref.kind==ObjectKind::Atom)products[studio_mechanism_products::sourceAtom(s.doc,ref.id)]=ref.id;
        require(products.size()==6,"guided product has every input atom");
        const std::vector<std::array<int,3>> expected=mode==0?
            std::vector<std::array<int,3>>{{{0,1,1}},{{1,2,2}},{{2,3,1}},{{3,4,1}},{{4,5,1}},{{5,0,1}}}:
            mode==1?std::vector<std::array<int,3>>{{{0,1,1}},{{1,2,2}},{{0,5,1}},{{3,4,2}},{{4,5,1}}}:
            std::vector<std::array<int,3>>{{{0,1,1}},{{1,2,2}},{{2,3,1}},{{3,4,2}},{{5,0,1}}};
        for(int a=0;a<6;++a)for(int b=a+1;b<6;++b){
            int order=0;for(auto link:expected)if((link[0]==a&&link[1]==b)||(link[0]==b&&link[1]==a))order=link[2];
            auto bond=functional_groups::bond(s.doc,products.at(atoms[a]),products.at(atoms[b]));
            require(order?(bond&&int(bond->order)==order):!bond,"product has the exact guided sigma and pi connectivity");
        }
        for(auto ref:s.selected)if(ref.kind==ObjectKind::Atom)require(!s.doc.atoms().alive(ref.id)->semantics.charge,"concerted step does not manufacture charges");
        for(auto ref:s.selected)require(!virtual_bonds::isGuide(s.doc,ref),"product contains formal bonds instead of guide lines");
        const auto after=s.doc.digest();
        require(s.undo().ok()&&s.doc.digest()==before&&s.undo(true).ok()&&s.doc.digest()==after,"guided step undo and redo are atomic");
        s.selected.clear();app.switchTab(6);s.fit();app.invalidate();app.screenshot(dir+"/virtual-product-"+std::to_string(mode)+".png");
    }
    app.virtualBondTool=false;app.virtualBondOrigin=kInvalidObjectId;
    std::ofstream(dir+"/virtual-bond-checks.json")<<"{\"diels_alder\":true,\"cope_33\":true,\"hydrogen_15\":true,\"normal_tool\":true,\"no_valence_change\":true,\"capture\":true,\"native_cdxml\":true,\"copy_remap\":true,\"undo_redo\":true}";
}
