#pragma once
void App::toolInsertCheck(const std::string& dir){
    auto require=[](bool value,const std::string& message){if(!value)throw std::runtime_error(message);};
    auto reset=[&](){s.cancel();s.doc=Document();drawingSettings.apply(s.doc);s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());s.refresh();down=pan=false;pointerOverCanvas=false;hover.clear();};
    const auto savedShortcuts=shortcuts;
    ribbonCompact=false;ribbonPinned=true;ribbonVisible=true;
    SetWindowPos(h,nullptr,0,0,1600,1000,SWP_NOMOVE|SWP_NOZORDER);layout();
    int ringCases=0;
    std::vector<std::string> commands;
    for(const auto& entry:s.entries)if(rings_chains::ringPresetForCommand(entry.command))commands.push_back(entry.command);
    // Both a terminal carbon and an internal two-bond carbon. Tool activation
    // must create a connected graph and preserve all original coordinates.
    for(const auto& command:commands)for(int neighbours:{0,1,2}){
        reset();ObjectId anchor;s.doc.addAtom(Vec2(123,78),6,&anchor);
        std::map<ObjectId,Vec2> original{{anchor,Vec2(123,78)}};
        const double unit=s.doc.defaultBondLength();
        for(int i=0;i<neighbours;++i){ObjectId atom,bond;Vec2 point=Vec2(123,78)+core::direction(neighbours==1?180.:(i?120.:240.))*unit;s.doc.addAtom(point,6,&atom);s.doc.addBond(anchor,atom,&bond);original[atom]=point;}
        s.refresh();s.selected={ObjectRef(ObjectKind::Atom,anchor)};
        auto before=s.doc.digest();const auto count=s.doc.stats();
        // Simulate an unfinished prior bond preview; no extra segment survives.
        s.activateCommand("chooseBondTool_Solid");s.gesture(PointerPhase::Down,Vec2(123,78));
        execute(Button("",command,"",command));
        const auto* preset=rings_chains::ringPresetForCommand(command);
        rings_chains::RingGeometry shape;rings_chains::buildRingGeometry(preset->id,unit,&shape);int ringValence=0;for(const auto& edge:shape.bonds)if(!edge.from||!edge.to)ringValence+=int(edge.order);bool connector=neighbours+ringValence>4;
        require(s.doc.stats().aliveAtoms==count.aliveAtoms+preset->size-1+(connector?1:0),"selected atom ring count: "+command+" neighbours="+std::to_string(neighbours)+" status="+status);
        require(s.doc.stats().aliveBonds==count.aliveBonds+preset->size+(connector?1:0),"selected atom ring bond count: "+command);
        require(!s.bondDrawing&&s.ghost.items().empty(),"old gesture cancelled");
        for(auto value:original)require(length(s.doc.atoms().alive(value.first)->position-value.second)<1e-8,"original atoms preserved");
        require(s.doc.incidentBonds(anchor).size()==size_t(neighbours+(connector?1:2)),"ring connects at the selected atom");
        for(const auto& first:s.doc.bonds().records())if(first.isAlive())for(const auto& second:s.doc.bonds().records())if(second.isAlive()&&first.id<second.id&&first.beginAtom!=second.beginAtom&&first.beginAtom!=second.endAtom&&first.endAtom!=second.beginAtom&&first.endAtom!=second.endAtom){
            auto a=s.doc.atoms().alive(first.beginAtom)->position,b=s.doc.atoms().alive(first.endAtom)->position,c=s.doc.atoms().alive(second.beginAtom)->position,d=s.doc.atoms().alive(second.endAtom)->position;
            require(sprout_layout::segmentSq(a,b,c,d)>unit*unit*.001,"ring has no crossing with existing bonds");
        }
        auto after=s.doc.digest();require(s.undo().ok()&&s.doc.digest()==before,"one undo removes entire inserted ring");require(s.undo(true).ok()&&s.doc.digest()==after,"redo exact ring");
        if(command=="chooseRingTool_Cyclopentane"&&neighbours==2){s.save(dir+"/selected-pentagon.cdxml",true);s.selected.clear();s.fit();screenshot(dir+"/selected-pentagon.png",false,false);}
        ++ringCases;
    }
    // Fusing on a selected bond reuses the edge rather than drawing over it.
    for(const auto& command:commands){reset();ObjectId a,b,bond;s.doc.addAtom(Vec2(),6,&a);s.doc.addAtom(Vec2(s.doc.defaultBondLength(),0),6,&b);s.doc.addBond(a,b,&bond);s.refresh();s.selected={ObjectRef(ObjectKind::Bond,bond)};execute(Button("",command,"",command));const auto* preset=rings_chains::ringPresetForCommand(command);require(s.doc.stats().aliveAtoms==size_t(preset->size)&&s.doc.stats().aliveBonds==size_t(preset->size),"selected bond reuses edge: "+command);++ringCases;}
    int alleneCases=0;
    for(int mode:{0,1,2,3}){
        reset();ObjectId anchor=kInvalidObjectId;Vec2 origin(91,37);
        if(mode==2){s.activateCommand("chooseRingTool_Cyclohexane");s.gesture(PointerPhase::Down,origin);s.gesture(PointerPhase::Up,origin);anchor=s.doc.atoms().records().front().id;origin=s.doc.atoms().alive(anchor)->position;}
        else if(mode){s.doc.addAtom(origin,6,&anchor);if(mode==3){ObjectId atom,bond;s.doc.addAtom(origin-Vec2(s.doc.defaultBondLength(),0),6,&atom);s.doc.addBond(anchor,atom,&bond);}}
        s.refresh();if(mode)s.selected={ObjectRef(ObjectKind::Atom,anchor)};
        const auto before=s.doc.digest();auto count=s.doc.stats();execute(Button("","插入连烯","allene","@allene"));
        require(s.doc.stats().aliveAtoms==count.aliveAtoms+(mode?2:3)&&s.doc.stats().aliveBonds==count.aliveBonds+2,"allene connected atom counts");
        std::vector<ObjectId> doubled;for(const auto& atom:s.doc.atoms().records())if(atom.isAlive()&&mechanism::cumuleneCenter(s.doc,atom))doubled.push_back(atom.id);
        require(doubled.size()==1,"one allene middle carbon");auto center=doubled.front();auto links=s.doc.incidentBonds(center);std::vector<Vec2> points;
        for(auto id:links){auto bond=s.doc.bonds().alive(id);require(bond->order==BondOrder::Double,"allene double bonds");points.push_back(s.doc.atoms().alive(bond->beginAtom==center?bond->endAtom:bond->beginAtom)->position);}
        require(length((points[0]+points[1])*.5-s.doc.atoms().alive(center)->position)<1e-7,"allene linear");
        if(mode){require(length(s.doc.atoms().alive(anchor)->position-origin)<1e-7,"selected anchor not moved");require(s.doc.bondBetween(anchor,center)!=kInvalidObjectId,"selected atom is allene terminal");}
        if(mode==2){s.save(dir+"/ring-allene.cdxml",true);s.selected.clear();s.fit();screenshot(dir+"/ring-allene.png",false,false);}
        auto after=s.doc.digest();require(s.undo().ok()&&s.doc.digest()==before,"allene one undo");require(s.undo(true).ok()&&s.doc.digest()==after,"allene redo");++alleneCases;
    }
    // Impossible valence must leave both the drawing and selection untouched.
    reset();ObjectId saturated;s.doc.addAtom(Vec2(),6,&saturated);for(int i=0;i<3;++i){ObjectId atom,bond;s.doc.addAtom(core::direction(i*120.)*s.doc.defaultBondLength(),6,&atom);s.doc.addBond(saturated,atom,&bond);}s.refresh();s.selected={ObjectRef(ObjectKind::Atom,saturated)};auto before=s.doc.digest();auto selection=s.selected;insertReactiveCenter(true);require(s.doc.digest()==before&&s.selected==selection,"invalid allene is atomic and preserves selection");
    reset();switchTab(6);layout();
    for(const auto& group:mechanismGroups)for(const auto& button:group.items){if(group.name=="原子电荷")require(button.iconOnly&&button.r.Width<=36,"charges compact and symbol only");}
    Button unbound("mechanism.allene","插入连烯","allene","@allene");require(toolCaption(unbound)=="插入连烯"&&hint(unbound).find("快捷键")==std::string::npos,"unbound tool has no fake key");
    Button undo("undo","撤销","undo","@undo");require(toolCaption(undo).find("Ctrl+Z")!=std::string::npos,"actual default shortcut");
    for(auto& row:shortcuts.rows)if(row.command=="@undo"){row.key=VK_F12;row.modifiers=kModControl;}
    ++shortcuts.generation;require(toolCaption(undo).find("F12")!=std::string::npos&&hint(undo).find("Ctrl+Z")==std::string::npos,"custom shortcut synced to footer and hover");
    shortcuts=savedShortcuts;++shortcuts.generation;hintGeneration=~uint64_t(0);pointerOverCanvas=false;
    for(const auto& group:mechanismGroups)for(const auto& button:group.items)if(button.id=="mechanism.positive"){
        SendMessageW(h,WM_MOUSEMOVE,0,MAKELPARAM(int((button.r.X+5)*scale),int((button.r.Y+5)*scale)));SendMessageW(h,WM_TIMER,13,0);
        require(IsWindow(toolTip)&&IsWindowVisible(toolTip)&&toolTipText.find(L"正电荷")!=std::wstring::npos,"real hover tooltip describes hidden charge label");hideToolTip();
    }
    s.selected.clear();s.fit();screenshot(dir+"/mechanism-toolbar.png");
    std::ofstream(dir+"/tool-insert-checks.json")<<"{\"ring_cases\":"<<ringCases<<",\"allene_cases\":"<<alleneCases<<",\"anchor_preserved\":true,\"undo_redo\":true,\"compact_charges\":true,\"hover_tooltip\":true,\"custom_shortcut_sync\":true}";
    s.dirty=false;PostMessageW(h,WM_CLOSE,0,0);
}
