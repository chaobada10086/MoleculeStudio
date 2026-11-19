#pragma once
static void checkHydroxylLonePairs(App& app,const std::string& dir){
    auto require=[](bool ok,const char* why){if(!ok)throw std::runtime_error(std::string("hydroxyl lone pair: ")+why);};
    auto& s=app.s;
    for(const char* label:{"OH","HO","SH","HS","NH2","H2N","OMe","OEt"})for(int mode:{0,1,2,3}){
        s.cancel();s.doc=Document();s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());
        const auto* group=studioions::find(label);require(group!=nullptr,"known attachment group");
        ObjectId host,alpha,methyl,link,tail;
        s.doc.addAtom(Vec2(90,45),mode==0?group->element:6,&host);
        s.doc.addAtom(Vec2(75,71),6,&alpha);s.doc.addAtom(Vec2(45,71),6,&methyl);
        s.doc.addBond(host,alpha,&link);s.doc.addBond(alpha,methyl,&tail);
        auto sem=s.doc.atoms().alive(host)->semantics;
        sem.nodeType=uint8_t(mode==1?NodeType::Nickname:mode==2?NodeType::Query:NodeType::Element);
        sem.implicitHydrogens=0;s.doc.setAtomSemantics(host,sem);s.doc.setAtomLabel(host,label);
        const auto position=s.doc.atoms().alive(host)->position;
        s.refresh();s.selected={ObjectRef(ObjectKind::Atom,host)};const auto before=s.doc.digest();
        app.execute(Button("","","","@mechanismLonePair"));
        require(mechanism::lonePairCount(s.doc,host)==1,"ribbon inserts a pair on both hydride spelling orders and legacy nodes");
        const auto* actual=s.doc.atoms().alive(host);
        require(actual->element==group->element&&actual->semantics.nodeType==uint8_t(NodeType::Element)&&actual->position==position&&
                actual->semantics.charge==0&&s.doc.atomLabel(host)==label&&s.doc.stats().aliveAtoms==3&&s.doc.stats().aliveBonds==2,
                "repair preserves compact label, atom positions, bonds and charge");
        const auto& visual=arrows::mechanismVisualBoxes(s.doc).at(host);
        require(visual.lonePairs.size()==1,"pair has a visible capture box");
        const auto pair=(visual.lonePairs[0].min+visual.lonePairs[0].max)*.5;
        require(length(pair-position)<15,"pair stays beside the oxygen or heteroatom");
        const auto marked=s.doc.digest();
        require(s.undo().ok()&&s.doc.digest()==before&&s.undo(true).ok()&&s.doc.digest()==marked,"host repair and annotation share one undo step");
        auto view=s.view();view.center=position;view.scale=4;view.dpiScale=1;s.setView(view);
        require(app.beginElectronMarkerDrag(view.toDevice(pair)),"new lone pair can be dragged");
        app.updateElectronMarkerDrag(view.toDevice(position+Vec2(45,-35)),true);
        const auto moved=arrows::mechanismVisualBoxes(s.doc).at(host).lonePairs.front();
        require(length((moved.min+moved.max)*.5-position)<=14.001,"pair drag remains attached and bounded");
        if(std::string(label)=="HO"&&mode==1){
            require(s.save(dir+"/hydroxyl-lone-pair.cdxml",true).ok(),"save compact hydroxyl pair");Session reopened;
            require(reopened.open(dir+"/hydroxyl-lone-pair.cdxml").ok(),"reopen compact hydroxyl pair");
            int pairs=0;for(const auto& a:reopened.doc.atoms().records())if(a.isAlive()&&a.element==8)pairs+=mechanism::lonePairCount(reopened.doc,a.id);
            require(pairs==1,"saved hydroxyl keeps its pair");
            s.selected.clear();s.fit();app.invalidate();app.screenshot(dir+"/hydroxyl-lone-pair.png");
        }
        // Use the actual mechanism-arrow gesture, starting at the moved dots.
        ObjectId proton;s.doc.addAtom(Vec2(155,30),1,&proton);auto hs=s.doc.atoms().alive(proton)->semantics;
        hs.charge=1;hs.implicitHydrogens=0;s.doc.setAtomSemantics(proton,hs);s.refresh();
        const auto start=(moved.min+moved.max)*.5;
        app.chooseArrowPreset(49);
        require(s.gesture(PointerPhase::Down,start).ok()&&s.gesture(PointerPhase::Move,Vec2(155,30)).ok()&&s.gesture(PointerPhase::Up,Vec2(155,30)).ok(),"draw electron arrow from the repaired group");
        arrows::ArrowParams arrow;bool ok=false;
        require(arrows::readArrow(s.doc,0,&arrow,&ok).ok()&&ok&&arrow.electronSource==ObjectRef(ObjectKind::Atom,host)&&arrow.sourceVisualAnchor==3,
                "mechanism arrow captures the lone pair's real attachment atom");
    }
    // Full user workflow: pull H out of OH, reposition the explicit H, then
    // immediately use the ribbon. Extraction leaves H selected, not oxygen.
    for(int form:{0,1,2,3})for(int selection:{0,1,2})for(double angle:{-2.5,-.7,1.2}){
        s.cancel();s.doc=Document();s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());
        ObjectId oxygen,carbon,methyl,co,cc;
        const Vec2 origin(90,65);
        s.doc.addAtom(origin,form==3?6:8,&oxygen);s.doc.addAtom(Vec2(75,91),6,&carbon);s.doc.addAtom(Vec2(45,91),6,&methyl);
        s.doc.addBond(oxygen,carbon,&co);s.doc.addBond(carbon,methyl,&cc);
        auto sem=s.doc.atoms().alive(oxygen)->semantics;sem.implicitHydrogens=form==3?-1:1;
        if(form==3)sem.nodeType=uint8_t(NodeType::Nickname);
        s.doc.setAtomSemantics(oxygen,sem);
        if(form)s.doc.setAtomLabel(oxygen,form==1?"OH":"HO");
        require(s.activateCommand("chooseSelectionTool_Marquee"),"select the real H extraction tool");
        s.refresh();auto view=s.view();view.center=origin;view.scale=4;view.dpiScale=1;s.setView(view);
        const auto& box=arrows::mechanismVisualBoxes(s.doc).at(oxygen);
        require(box.hasAttachedProton,"condensed hydroxyl exposes its draggable H glyph");
        const auto press=(box.attachedProton.min+box.attachedProton.max)*.5;
        const auto beforeExtraction=s.doc.digest();
        require(app.beginElectronMarkerDrag(view.toDevice(press))&&app.electronDragSite==App::attachedProtonDragSite,
                "grab the actual condensed H glyph");
        const auto end=view.toDevice(origin+Vec2(30,-30));
        app.updateElectronMarkerDrag(end,false);
        require(s.doc.digest()==beforeExtraction&&app.attachedProtonPreview,"H preview leaves the source drawing intact");
        app.updateElectronMarkerDrag(end,true);
        require(s.selected.size()==1&&s.selected.front().kind==ObjectKind::Atom&&s.doc.atoms().alive(s.selected.front().id)->element==1,
                "H extraction leaves the new explicit hydrogen selected");
        const auto hydrogen=s.selected.front().id;
        require(s.doc.atoms().alive(oxygen)->element==8&&s.doc.atoms().alive(oxygen)->semantics.implicitHydrogens==0&&
                s.doc.incidentBonds(oxygen).size()==2&&mechanism::lonePairCapacity(s.doc,oxygen)==2,
                "explicit O-H counts its hydrogen once and leaves two oxygen lone pairs available");
        const auto position=origin+Vec2(std::cos(angle),std::sin(angle))*s.doc.defaultBondLength()*1.2;
        require(s.submit(Command("移动显式氢",kInvalidateGeometry,[hydrogen,position](CommandContext& c){
            auto e=c.document.setAtomPosition(hydrogen,position);return e.ok()?CommandResult::success():CommandResult::fail(e.code,e.message);
        }),"移动显式氢").ok(),"reposition the explicit H before adding a lone pair");
        if(selection==1)s.selected={ObjectRef(ObjectKind::Atom,oxygen)};
        if(selection==2)s.selected={ObjectRef(ObjectKind::Atom,oxygen),ObjectRef(ObjectKind::Atom,hydrogen)};
        const auto beforePair=s.doc.digest();
        app.execute(Button("","","","@mechanismLonePair"));
        require(mechanism::lonePairCount(s.doc,oxygen)==1&&mechanism::lonePairCount(s.doc,hydrogen)==0&&
                s.selected==std::vector<ObjectRef>{ObjectRef(ObjectKind::Atom,oxygen)},
                "H-selected, O-selected and O-H-selected ribbon actions each add exactly one pair to oxygen");
        require(s.doc.atoms().alive(hydrogen)->position==position&&s.doc.atoms().alive(oxygen)->position==origin&&
                s.doc.stats().aliveAtoms==4&&s.doc.stats().aliveBonds==3&&s.doc.atoms().alive(oxygen)->semantics.implicitHydrogens==0,
                "lone-pair insertion preserves the moved H, explicit O-H bond and original skeleton");
        const auto afterPair=s.doc.digest();
        require(s.undo().ok()&&s.doc.digest()==beforePair&&s.undo(true).ok()&&s.doc.digest()==afterPair,
                "lone-pair insertion after H movement undoes and redoes as one edit");
        if(form==3&&selection==0&&angle< -2.){
            for(bool xml:{false,true}){
                const auto path=dir+"/moved-hydroxyl-lone-pair"+(xml?".cdxml":".r62doc");
                require(s.save(path,xml).ok(),"save the moved explicit hydroxyl");Session reopened;
                require(reopened.open(path).ok(),"reopen the moved explicit hydroxyl");
                ObjectId o=kInvalidObjectId,h=kInvalidObjectId;
                for(const auto& a:reopened.doc.atoms().records())if(a.isAlive()){if(a.element==8)o=a.id;else if(a.element==1)h=a.id;}
                require(o!=kInvalidObjectId&&h!=kInvalidObjectId&&mechanism::lonePairCount(reopened.doc,o)==1&&
                        length((reopened.doc.atoms().alive(h)->position-reopened.doc.atoms().alive(o)->position)-(position-origin))<1e-7,
                        "round-trip preserves lone pair, explicit H and its manually chosen position");
                require(reopened.submit(mechanism::setLonePairCommand({h}),"再添加孤对电子").ok()&&mechanism::lonePairCount(reopened.doc,o)==2,
                        "selecting the explicit H still adds a second pair to oxygen after reopening");
            }
            s.selected.clear();s.fit();app.invalidate();app.screenshot(dir+"/moved-hydroxyl-lone-pair.png");
        }
        const auto& visual=arrows::mechanismVisualBoxes(s.doc).at(oxygen);
        const auto start=(visual.lonePairs.front().min+visual.lonePairs.front().max)*.5;
        ObjectId proton;s.doc.addAtom(Vec2(155,35),1,&proton);auto hs=s.doc.atoms().alive(proton)->semantics;
        hs.charge=1;hs.implicitHydrogens=0;s.doc.setAtomSemantics(proton,hs);s.refresh();app.chooseArrowPreset(49);
        require(s.gesture(PointerPhase::Down,start).ok()&&s.gesture(PointerPhase::Move,Vec2(155,35)).ok()&&s.gesture(PointerPhase::Up,Vec2(155,35)).ok(),
                "draw a mechanism arrow from the moved hydroxyl's lone pair");
        arrows::ArrowParams arrow;bool ok=false;
        require(arrows::readArrow(s.doc,0,&arrow,&ok).ok()&&ok&&arrow.electronSource==ObjectRef(ObjectKind::Atom,oxygen)&&arrow.sourceVisualAnchor==3,
                "capture remains on oxygen after H extraction and movement");
        std::vector<mechanism::ElectronMove> moves;
        require(mechanism::inferElectronMoves(s.doc,{arrow},&moves).ok(),"the moved hydroxyl's electron arrow remains interpretable");
    }
    {
        Document d;ObjectId freeH,proton,carbon,carbonH,bond;
        d.addAtom(Vec2(),1,&freeH);d.addAtom(Vec2(20,0),1,&proton);auto sem=d.atoms().alive(proton)->semantics;sem.charge=1;d.setAtomSemantics(proton,sem);
        d.addAtom(Vec2(40,0),6,&carbon);d.addAtom(Vec2(55,0),1,&carbonH);d.addBond(carbon,carbonH,&bond);
        require(mechanism::lonePairTarget(d,freeH)==freeH&&mechanism::lonePairTarget(d,proton)==proton&&mechanism::lonePairTarget(d,carbonH)==carbonH,
                "free H, free proton and ordinary C-H are never redirected to an unrelated atom");
    }
}
