#pragma once
static void checkElectronMarkerDeletion(App& app,const std::string& dir){
    auto& s=app.s;
    int checks=0;
    auto require=[&](bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(std::string("selected electron deletion: ")+why);};
    auto reset=[&](){app.cancelElectronMarkerDrag();app.selectedElectronMarker={};s.cancel();s.doc=Document();s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());s.activateCommand("chooseSelectionTool_Marquee");};
    auto atom=[&](Vec2 p,int z=6){ObjectId id;require(s.doc.addAtom(p,z,&id).ok(),"fixture atom");return id;};
    auto link=[&](ObjectId a,ObjectId b){ObjectId id;require(s.doc.addBond(a,b,&id).ok(),"fixture bond");return id;};
    auto point=[&](Vec2 p){auto px=s.view().toDevice(p);return MAKELPARAM(int(std::lround(px.x)),int(std::lround(px.y)));};
    auto select=[&](ObjectId id,int site,int pair=0){
        s.refresh();auto view=s.view();view.center=s.doc.atoms().alive(id)->position;view.scale=5;view.dpiScale=1;s.setView(view);
        const auto& box=arrows::mechanismVisualBoxes(s.doc).at(id);
        const auto center=site==2?(box.charge.min+box.charge.max)*.5:site==3?(box.lonePairs[pair].min+box.lonePairs[pair].max)*.5:box.radicalPoints.front();
        s.selected={ObjectRef(ObjectKind::Atom,id)};
        auto mouse=point(center);
        SendMessageW(app.canvas,WM_LBUTTONDOWN,MK_LBUTTON,mouse);SendMessageW(app.canvas,WM_LBUTTONUP,0,mouse);
        require(app.electronMarkerSelected()&&app.selectedElectronMarker.atom==id&&app.selectedElectronMarker.site==site&&s.selected.empty(),"click selects the marker instead of its atom");
        SendMessageW(app.canvas,WM_MOUSEMOVE,0,MAKELPARAM(30,30));
        require(app.electronMarkerSelected(),"selection survives moving the pointer away");
    };
    struct Case{int z,charge,radical,bonds,h;const char* label;};
    int cases=0;
    for(const auto& c:std::vector<Case>{
        {6,1,0,3,1,""},{6,-1,0,1,3,""},{6,0,int(Radical::Doublet),3,1,""},
        {6,0,int(Radical::Singlet),2,2,""},{6,0,int(Radical::Triplet),2,2,""},
        {7,1,0,2,1,"N+"},{7,-1,0,1,2,"HN−"},{8,1,0,1,1,"O"},{8,-1,0,1,1,"O−"},
        {15,1,0,2,1,"P"},{15,-1,0,2,1,"P"},{16,0,int(Radical::Doublet),1,1,"S"}
    })for(int mode:{0,1,2}){
        reset();const auto host=atom(Vec2(80,80),c.z);const auto ref=ObjectRef(ObjectKind::Atom,host);
        for(int i=0;i<c.bonds;++i)link(host,atom(Vec2(80,80)+core::direction(i*120.+30)*24));
        auto sem=s.doc.atoms().alive(host)->semantics;sem.charge=c.charge;sem.radical=c.radical;sem.implicitHydrogens=0;s.doc.setAtomSemantics(host,sem);
        if(*c.label)s.doc.setAtomLabel(host,c.label);
        if(mode){auto& bag=s.doc.extensions(ref);bag.set(integration::molecular::electronMarkerKey(c.radical!=0),"0.8 -0.8");if(c.charge&&mode==2)bag.set("pst.attr.MoleculeStudioChargeCircle","1");}
        select(host,c.radical?4:2);const auto before=s.doc.digest();const auto history=s.history.cursor();
        if(mode==1)app.execute(Button("","","","@delete"));
        else SendMessageW(app.canvas,WM_KEYDOWN,VK_DELETE,0);
        const auto* actual=s.doc.atoms().alive(host);
        require(actual&&!actual->semantics.charge&&!actual->semantics.radical&&actual->semantics.implicitHydrogens==c.h,"delete clears charge/radical/carbene and recomputes H");
        require(s.doc.stats().aliveAtoms==size_t(c.bonds+1)&&s.doc.stats().aliveBonds==size_t(c.bonds)&&actual->position==Vec2(80,80),"delete preserves all atoms, bonds and coordinates");
        const auto after=s.doc.digest();require(s.history.cursor()==history+1&&s.undo().ok()&&s.doc.digest()==before&&s.undo(true).ok()&&s.doc.digest()==after,"deletion plus hydrogen restoration is one undo/redo");
        require(!app.electronMarkerSelected(),"deleted marker is no longer selected");++cases;
    }
    // Remove one pair without deleting/repositioning the other, including after a drag.
    reset();auto oxygen=atom(Vec2(80,80),8),carbon=atom(Vec2(104,90));link(oxygen,carbon);
    auto os=s.doc.atoms().alive(oxygen)->semantics;os.implicitHydrogens=1;s.doc.setAtomSemantics(oxygen,os);
    s.doc.extensions(ObjectRef(ObjectKind::Atom,oxygen)).set(mechanism::lonePairKey(),"2");
    mechanism::setLonePairOffset(s.doc,oxygen,0,Vec2(-.8,-.8));mechanism::setLonePairOffset(s.doc,oxygen,1,Vec2(.8,-.8));
    select(oxygen,3,0);auto selectionOverlay=s.overlay();const auto primitiveCount=selectionOverlay.scene().size();app.appendElectronMarkerSelection(selectionOverlay);
    require(selectionOverlay.scene().size()==primitiveCount+1&&selectionOverlay.scene().primitives().back().style.stroke.rgba==0x0078D4FFu,"selected marker has a blue outline in the canvas overlay");
    app.invalidate();app.screenshot(dir+"/selected-lone-pair.png");
    const auto beforePair=s.doc.digest();const auto remainingOffset=mechanism::lonePairOffsets(s.doc,oxygen).at(1);
    SendMessageW(app.canvas,WM_KEYDOWN,VK_DELETE,0);
    require(mechanism::lonePairCount(s.doc,oxygen)==1&&mechanism::lonePairOffsets(s.doc,oxygen).size()==1&&mechanism::lonePairOffsets(s.doc,oxygen).at(0)==remainingOffset,"one-pair removal compacts indexes while preserving the other position");
    require(s.doc.atoms().alive(oxygen)->semantics.implicitHydrogens==1&&s.doc.stats().aliveAtoms==2&&s.doc.stats().aliveBonds==1,"lone-pair deletion retains OH and its existing bond");
    const auto afterPair=s.doc.digest();require(s.undo().ok()&&s.doc.digest()==beforePair&&s.undo(true).ok()&&s.doc.digest()==afterPair,"pair deletion undo/redo");
    select(oxygen,3,0);
    const auto start=app.selectedElectronMarker.center;
    require(app.beginElectronMarkerDrag(s.view().toDevice(start)),"select remaining pair for drag");
    app.updateElectronMarkerDrag(s.view().toDevice(Vec2(70,91)),true);
    require(app.electronMarkerSelected(),"drag completion retains the selected marker");
    SendMessageW(app.canvas,WM_MOUSEMOVE,0,MAKELPARAM(30,30));SendMessageW(app.canvas,WM_KEYDOWN,VK_DELETE,0);
    require(mechanism::lonePairCount(s.doc,oxygen)==0&&!s.doc.extensions(ObjectRef(ObjectKind::Atom,oxygen)).findValue(mechanism::lonePairPositionsKey()),"last pair deletes after being repositioned");
    // Nu has a default/printed pair: explicit zero must suppress it until re-added.
    for(const char* role:{"nucleophile","nucleophile-neutral"}){
        reset();const auto nu=atom(Vec2(80,80));require(mechanism::setGenericRole(s.doc,nu,role).ok(),"create Nu");
        select(nu,3);SendMessageW(app.canvas,WM_KEYDOWN,VK_DELETE,0);
        require(mechanism::lonePairCount(s.doc,nu)==0&&s.doc.atomLabel(nu)=="Nu"&&arrows::mechanismVisualBoxes(s.doc).at(nu).lonePairs.empty(),"Nu pair and printed colon do not reappear after deletion");
        require(s.submit(mechanism::setLonePairCommand({nu}),"重新添加孤对电子").ok()&&mechanism::lonePairCount(s.doc,nu)==1,"deleted Nu pair can be added again");
    }
    // Existing explicit isotope H counts as a bond; it must never be duplicated or removed.
    reset();oxygen=atom(Vec2(80,80),8);auto deuterium=atom(Vec2(65,80),1);link(oxygen,deuterium);link(oxygen,atom(Vec2(100,90)));
    auto hs=s.doc.atoms().alive(deuterium)->semantics;hs.isotope=2;hs.implicitHydrogens=0;s.doc.setAtomSemantics(deuterium,hs);
    os=s.doc.atoms().alive(oxygen)->semantics;os.charge=1;os.implicitHydrogens=0;s.doc.setAtomSemantics(oxygen,os);
    select(oxygen,2);SendMessageW(app.canvas,WM_KEYDOWN,VK_DELETE,0);
    require(s.doc.atoms().alive(oxygen)->semantics.implicitHydrogens==0&&s.doc.atoms().alive(deuterium)->semantics.isotope==2&&s.doc.stats().aliveAtoms==3,"explicit isotope H survives and prevents duplicate hydrogen");
    // Clear selection on Escape, canvas clicks and document changes.
    reset();const auto ion=atom(Vec2(80,80),7);auto ns=s.doc.atoms().alive(ion)->semantics;ns.charge=-1;s.doc.setAtomSemantics(ion,ns);
    select(ion,2);SendMessageW(app.canvas,WM_KEYDOWN,VK_ESCAPE,0);require(!app.electronMarkerSelected(),"Escape cancels marker selection");
    select(ion,2);SendMessageW(app.canvas,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(30,30));SendMessageW(app.canvas,WM_LBUTTONUP,0,MAKELPARAM(30,30));require(!app.electronMarkerSelected(),"click away clears marker selection");
    select(ion,2);s.doc=Document();require(!app.electronMarkerSelected()&&!app.deleteSelectedElectronMarker(),"stale selection cannot affect a new document");
    std::ofstream(dir+"/selected-electron-deletion.json")<<"{\"pass\":true,\"state_cases\":"<<cases<<",\"assertions\":"<<checks<<"}";
}
