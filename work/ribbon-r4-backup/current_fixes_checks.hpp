#pragma once
#include "alkoxide_current_checks.hpp"
#include "metal_expansion_checks.hpp"
#include "native_compat_checks.hpp"
#include "chair_layout_checks.hpp"
#include "projection_checks.hpp"
#include "grouped_save_checks.hpp"
#include "engine/molecular_render.hpp"
static void checkMetalLabels(App& app,const std::string& dir){
    auto require=[](bool ok,const std::string& why){if(!ok)throw std::runtime_error("metal labels: "+why);};auto& s=app.s;
    s.cancel();s.doc=Document();s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());s.doc.setDefaultBondLength(22.);
    int cases=0;std::vector<ObjectId> metals;
    for(const char* label:{"LnRu","RuLn","LnPd","PdCl2","(PPh3)2PdCl2","IrLn","LnFe","Ni(CO)4"})for(int side:{-1,1}){
        ObjectId host,metal,bond;Vec2 position(side<0?140:350,30+cases/2*55);s.doc.addAtom(position-Vec2(side*22.,0),6,&host);s.doc.addAtom(position,6,&metal);s.doc.addBond(host,metal,&bond);s.doc.setAtomLabel(metal,label);auto sem=s.doc.atoms().alive(metal)->semantics;sem.nodeType=uint8_t(NodeType::Query);sem.showLabel=true;sem.implicitHydrogens=0;s.doc.setAtomSemantics(metal,sem);
        TextRun run;run.utf8=label;run.fontName="Arial";run.size=10;run.flags=kFormula;studioatom::set(s.doc,metal,{run});metals.push_back(metal);++cases;
    }
    auto verify=[&](Session& session){integration::RingGeometry rings(session.doc);auto labels=integration::molecular::labels(session.doc,session.glyphs,rings);
        for(const auto& atom:session.doc.atoms().records())if(atom.isAlive()&&studio::metal_labels::active(session.doc,atom.id)){
            auto span=studio::metal_labels::find(session.doc.atomLabel(atom.id));auto placed=labels.at(atom.id);Box ink;bool first=true;size_t start=span.start;
            for(size_t i=start;i<start+span.size;++i)for(const auto& contour:placed.layout.glyphs.at(i).contours)for(auto p:contour){p=p+placed.layout.anchor;if(first){ink=Box(p,p);first=false;}else ink.add(p);}
            require(!first&&length((ink.min+ink.max)*.5-atom.position)<1e-6,"metal glyph centred on bond endpoint");
            bool right=studio::metal_labels::bondOnRight(session.doc,atom.id);for(size_t i=0;i<placed.layout.glyphs.size();++i)if(i<start||i>=start+span.size){auto g=placed.layout.glyphs[i];require(right?g.x<placed.layout.glyphs[start].x:g.x>=placed.layout.glyphs[start+span.size-1].x,"ligands extend away from bond");}
        }
    };
    s.refresh();verify(s);require(s.save(dir+"/metal-labels.cdxml",true).ok(),"export labels");Session back;require(back.open(dir+"/metal-labels.cdxml").ok(),"reopen labels");verify(back);
    std::string xml;persistence::readFileBytes(dir+"/metal-labels.cdxml",&xml);require(xml.find("LabelAlignment=\"Right\"")!=std::string::npos&&xml.find("LabelAlignment=\"Left\"")!=std::string::npos,"standard connection-order alignment");
    for(auto plain:{"CO2Me","Boc","CN","PPh3","NMe2"})require(!studio::metal_labels::find(plain).valid(),"ordinary groups unchanged");
    s.selected.clear();s.fit();app.screenshot(dir+"/metal-labels.png",false,false);
    int expanded=0;
    for(const char* label:{"PdCl2","RuLn","LnPd","NiCl2(PPh3)2","(PPh3)2PdCl2"}){
        s.cancel();s.doc=Document();s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());ObjectId host,metal,bond;s.doc.addAtom(Vec2(-22,0),6,&host);s.doc.addAtom(Vec2(),6,&metal);s.doc.addBond(host,metal,&bond);s.doc.setAtomLabel(metal,label);auto sem=s.doc.atoms().alive(metal)->semantics;sem.nodeType=uint8_t(NodeType::Query);sem.showLabel=true;s.doc.setAtomSemantics(metal,sem);s.refresh();
        auto complex=metal_complexes::parse(label);require(complex.valid(),"recognized composition");auto plan=functional_groups::prepareExpansion(s.doc,metal);auto original=s.doc.digest();
        require(s.submit(Command("展开配体",kInvalidateStructure|kInvalidateGeometry,[plan](CommandContext& c){std::vector<ObjectRef> made;functional_groups::applyExpansion(c.document,plan,made);return CommandResult::success();}),"官能团展开").ok(),"expand ligands");
        require(s.doc.atoms().alive(metal)->element==complex.element&&s.doc.bondBetween(host,metal)==bond,"actual metal retains existing bond");require(s.doc.stats().aliveAtoms==2+complex.ligands.size()&&s.doc.incidentBonds(metal).size()==1+complex.ligands.size(),"all ligand counts expanded");
        auto after=s.doc.digest();require(s.undo().ok()&&s.doc.digest()==original&&s.undo(true).ok()&&s.doc.digest()==after,"expansion undo/redo");
        if(std::string(label)=="PdCl2"){s.selected.clear();s.fit();s.save(dir+"/expanded-pdcl2.cdxml",true);app.screenshot(dir+"/expanded-pdcl2.png",false,false);}
        ++expanded;
    }
    // Drive the real right-click action path, including its automatic cleanup.
    s.cancel();s.doc=Document();s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());ObjectId metal;s.doc.addAtom(Vec2(),6,&metal);s.doc.setAtomLabel(metal,"PdCl2");auto sem=s.doc.atoms().alive(metal)->semantics;sem.nodeType=uint8_t(NodeType::Query);sem.showLabel=true;s.doc.setAtomSemantics(metal,sem);s.refresh();s.selected={ObjectRef(ObjectKind::Atom,metal)};auto before=s.doc.digest();app.expandPending=false;app.execute(Button("","展开官能团","","@expandGroups"));require(s.doc.stats().aliveAtoms==3&&s.doc.atoms().alive(metal)->element==46,"right-click expansion action");require(s.undo().ok()&&s.doc.digest()==before,"right-click expansion single undo");
    std::ofstream(dir+"/metal-label-checks.json")<<"{\"oriented_labels\":"<<cases<<",\"standard_cdxml\":true,\"ligand_expansions\":"<<expanded<<",\"right_click_action\":true,\"undo_redo\":true}";
}
static void checkCurrentCarbene(App& app,const std::string& dir){
    auto require=[](bool ok,const std::string& why){if(!ok)throw std::runtime_error("carbene fixes: "+why);};auto& s=app.s;int cases=0;
    for(int site:{1,4}){
        s.cancel();s.doc=Document();s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());s.doc.setDefaultBondLength(22);std::vector<ObjectId> ring;
        const double radius=22/(2*std::sin(3.141592653589793/5));for(int i=0;i<5;++i){ObjectId id;s.doc.addAtom(Vec2(std::cos(i*6.283185307179586/5),std::sin(i*6.283185307179586/5))*radius,6,&id);ring.push_back(id);}for(int i=0;i<5;++i)s.doc.addBond(ring[i],ring[(i+1)%5],nullptr);
        auto sem=s.doc.atoms().alive(ring[0])->semantics;sem.radical=uint8_t(Radical::Singlet);sem.implicitHydrogens=0;s.doc.setAtomSemantics(ring[0],sem);
        ObjectId a,b,pi;s.doc.addAtom(Vec2(85,-15),6,&a);s.doc.addAtom(Vec2(110,20),6,&b);s.doc.addBond(a,b,&pi);s.doc.setBondOrder(pi,BondOrder::Double);s.refresh();
        auto make=[&](ObjectRef from,ObjectRef to){Vec2 x,y;arrows::mechanismAnchorPoint(s.doc,from,&x);arrows::mechanismAnchorPoint(s.doc,to,&y);auto p=arrows::resolvePreset(49,x,y,.6);p.mechanism=true;p.electronSource=from;p.electronTarget=to;p.sourceVisualAnchor=from.kind==ObjectKind::Atom?site:5;p.targetVisualAnchor=to.kind==ObjectKind::Atom?site:5;
            if(site==4){auto points=arrows::mechanismRadicalPoints(s.doc,ring[0]);if(from.kind==ObjectKind::Atom)x=points.front();else y=points.front();p.begin=x;p.end=y;}arrows::routeMechanism(s.doc,p,true);return p;};
        auto first=make(ObjectRef(ObjectKind::Atom,ring[0]),ObjectRef(ObjectKind::Bond,pi));auto second=make(ObjectRef(ObjectKind::Bond,pi),ObjectRef(ObjectKind::Atom,ring[0]));
        auto draw=[&](const arrows::ArrowParams& p){Vec2 x,y;arrows::mechanismAnchorPoint(s.doc,p.electronSource,&x);arrows::mechanismAnchorPoint(s.doc,p.electronTarget,&y);if(site==4){auto points=arrows::mechanismRadicalPoints(s.doc,ring[0]);if(p.electronSource.kind==ObjectKind::Atom)x=points.front();else y=points.front();}s.activateCommand(arrows::presetById(49)->sourceCommand);return s.gesture(PointerPhase::Down,x).ok()&&s.gesture(PointerPhase::Move,(x+y)*.5+Vec2(0,12)).ok()&&s.gesture(PointerPhase::Up,y).ok();};
        require(draw(first)&&draw(second),"draw directed arrows");int arrowsAlive=0;std::vector<arrows::ArrowParams> inputs;for(const auto& record:s.doc.arrows().records())if(record.isAlive()){++arrowsAlive;arrows::ArrowParams p;bool ok=false;arrows::readArrow(s.doc,record.id,&p,&ok);require(ok&&!p.head.atBegin&&p.head.atEnd,"each arrow is single-headed");inputs.push_back(p);}require(arrowsAlive==2,"carbene arrows never merge");
        std::vector<mechanism::ElectronMove> moves;auto inferred=mechanism::inferElectronMoves(s.doc,inputs,&moves);require(inferred.ok(),"atom and dot anchors both calculate: "+inferred.message);
        auto source=s.doc;s.selected=s.all();auto before=s.doc.digest();app.generateMechanismStep();require(s.doc.digest()!=before,"generate cyclopropane: "+app.status);
        std::map<ObjectId,ObjectId> product;for(auto ref:s.selected)if(ref.kind==ObjectKind::Atom)product[studio_mechanism_products::sourceAtom(s.doc,ref.id)]=ref.id;
        require(product.count(a)&&product.count(b)&&product.count(ring[0]),"product maps all cycloaddition atoms");auto c=product[ring[0]],pa=product[a],pb=product[b];require(!s.doc.atoms().alive(c)->semantics.radical&&s.doc.bondBetween(c,pa)!=kInvalidObjectId&&s.doc.bondBetween(c,pb)!=kInvalidObjectId,"carbene consumed and triangle formed");
        for(auto edge:std::vector<std::pair<ObjectId,ObjectId>>{{c,pa},{c,pb},{pa,pb}})require(std::abs(length(s.doc.atoms().alive(edge.first)->position-s.doc.atoms().alive(edge.second)->position)-22)<.15,"regular equilateral small ring");
        auto after=s.doc.digest();require(s.undo().ok()&&s.doc.digest()==before&&s.undo(true).ok()&&s.doc.digest()==after,"whole generated step undoes");
        if(site==1){s.selected.clear();s.fit();s.save(dir+"/carbene-cyclopropane.cdxml",true);app.screenshot(dir+"/carbene-cyclopropane.png",false,false);}++cases;
    }
    Document source;source.setDefaultBondLength(22);ObjectId a,b,bond;source.addAtom(Vec2(),7,&a);source.addAtom(Vec2(60,0),7,&b);source.addBond(a,b,&bond);Document product=source;product.setBondOrder(bond,BondOrder::Triple);for(auto id:{a,b})product.extensions(ObjectRef(ObjectKind::Atom,id)).set("studio.mechanism.source.atom",std::to_string(id));std::vector<ObjectRef> made{ObjectRef(ObjectKind::Atom,a),ObjectRef(ObjectKind::Atom,b)};
    auto report=studio_local_cleanup::run(app.h,product,source,made);require(report.changed&&std::abs(length(product.atoms().alive(a)->position-product.atoms().alive(b)->position)-22)<1e-7,"two-atom byproduct is cleaned");
    std::ofstream(dir+"/carbene-current-checks.json")<<"{\"atom_and_symbol_cases\":"<<cases<<",\"two_distinct_arrows\":true,\"equilateral_product\":true,\"small_byproduct_cleanup\":true,\"undo_redo\":true}";
}
static void checkOwnedElectronEndpoints(const std::string& dir){
    auto require=[](bool ok,const std::string& why){if(!ok)throw std::runtime_error("electron ownership: "+why);};int cases=0;
    for(bool radical:{false,true}){Document doc;doc.setDefaultBondLength(22);ObjectId from,to,methyl;doc.addAtom(Vec2(0,0),radical?6:8,&from);doc.addAtom(Vec2(70,0),6,&to);
        auto sem=doc.atoms().alive(from)->semantics;sem.charge=radical?0:-1;sem.radical=radical?uint8_t(Radical::Doublet):0;sem.implicitHydrogens=radical?3:0;doc.setAtomSemantics(from,sem);
        sem=doc.atoms().alive(to)->semantics;sem.charge=radical?0:1;sem.radical=radical?uint8_t(Radical::Doublet):0;sem.implicitHydrogens=3;doc.setAtomSemantics(to,sem);
        if(!radical){doc.addAtom(Vec2(-22,0),6,&methyl);doc.addBond(methyl,from,nullptr);}
        const auto& boxes=arrows::mechanismVisualBoxes(doc);std::string expected;
        auto point=[&](ObjectId id,bool marker){if(!marker)return doc.atoms().alive(id)->position;const auto& box=boxes.at(id);return radical?box.radicalPoints.front():(box.charge.min+box.charge.max)*.5;};
        for(bool sourceMarker:{false,true})for(bool targetMarker:{false,true}){
            auto p=arrows::resolvePreset(radical?57:49,point(from,sourceMarker),point(to,targetMarker),.6);p.mechanism=true;p.electronSource=ObjectRef(ObjectKind::Atom,from);p.electronTarget=ObjectRef(ObjectKind::Atom,to);p.sourceVisualAnchor=sourceMarker?(radical?4:2):1;p.targetVisualAnchor=targetMarker?(radical?4:2):1;
            std::vector<arrows::ArrowParams> params{p};if(radical)params.push_back(arrows::reversedArrow(p));std::vector<mechanism::ElectronMove> moves;auto e=mechanism::inferElectronMoves(doc,params,&moves);require(e.ok(),"body and marker calculate: "+e.message);
            Document product=doc;std::vector<ObjectRef> made;e=mechanism::generateNextStep(product,moves,&made);require(e.ok(),"generate equivalent product");auto key=mechanism::infer_detail::resultKey(product,made);if(expected.empty())expected=key;else require(key==expected,"same chemistry for body and marker endpoints");++cases;
        }
        if(!radical){AnnotationRecord marker;marker.anchors[0]=from;marker.geometry[0]=point(from,true);ObjectId symbol;doc.addAnnotation(ObjectKind::Symbol,marker,&symbol);auto p=arrows::resolvePreset(49,point(from,true),point(to,false),.6);p.mechanism=true;p.electronSource=ObjectRef(ObjectKind::Symbol,symbol);p.electronTarget=ObjectRef(ObjectKind::Atom,to);std::vector<mechanism::ElectronMove> moves;auto e=mechanism::inferElectronMoves(doc,{p},&moves);require(e.ok()&&!moves.empty()&&moves.front().source==ObjectRef(ObjectKind::Atom,from),"owned symbol resolves to its atom");++cases;}
    }
    std::ofstream(dir+"/electron-owner-checks.json")<<"{\"cases\":"<<cases<<",\"positive_negative_radical\":true,\"body_marker_equivalence\":true,\"owned_symbols\":true}";
}
void App::currentFixesCheck(const std::string& dir){
    // Check the actual Win32 layout at a narrow but ordinary desktop width.
    RECT originalWindow;GetWindowRect(h,&originalWindow);const int originalTab=ribbonTab;
    SetWindowPos(h,nullptr,0,0,int(900*scale),int(800*scale),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    auto checkCompact=[&](int tab){ribbonTab=tab;layout();if(visibleGroups().empty()||visibleGroups().front().r.Y!=80.f)throw std::runtime_error("responsive ribbon has no first row");for(const auto& group:visibleGroups())if(group.r.X>=0&&group.r.Y>80.f)throw std::runtime_error("responsive ribbon wraps at 900 logical pixels");};
    checkCompact(0);if(!homeAutoCompact)throw std::runtime_error("home ribbon not compact at 900 logical pixels");
    float tabEnd=0;for(size_t i=0;i<ribbonTabs.size();++i)if(i!=3)tabEnd=std::max(tabEnd,ribbonTabs[i].r.GetRight());
    for(const auto& b:quick)if(b.r.X>=0&&b.id!="fileMenu"&&b.r.X<tabEnd+8.f)throw std::runtime_error("quick control overlaps a tab");
    int previousVisible=0;bool savedCompact=ribbonCompact;ribbonCompact=false;
    for(int logicalWidth:{900,1100,1300,1500}){
        SetWindowPos(h,nullptr,0,0,int(logicalWidth*scale),int(800*scale),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);ribbonTab=0;layout();int shown=0;
        for(const auto& group:groups){if(group.r.X<0||group.r.Y!=80.f)throw std::runtime_error("home group hidden or wrapped");for(const auto& button:group.items)if(button.r.X>=0){++shown;if(button.r.GetBottom()>group.r.GetBottom()-19)throw std::runtime_error("home icon overlaps group caption");}for(const auto& button:chemistryQuick)if(button.r.X>=0&&button.r.IntersectsWith(group.r))throw std::runtime_error("home tool overlaps quick action");}
        if(shown<=18||shown<previousVisible)throw std::runtime_error("home does not progressively expand");previousVisible=shown;
        screenshot(dir+"/ribbon-home-"+std::to_string(logicalWidth)+".png");
    }
    ribbonCompact=savedCompact;SetWindowPos(h,nullptr,0,0,int(900*scale),int(800*scale),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    checkCompact(1);if(!graphicsAutoCompact)throw std::runtime_error("graphics ribbon not compact at 900 logical pixels");
    checkCompact(5);if(!otherAutoCompact)throw std::runtime_error("template ribbon not compact at 900 logical pixels");
    // At smaller widths captions give way to complete grids of icon buttons.
    for(int logicalWidth:{640,900,1100,1500})for(int tab:{1,2,4,5,6}){
        SetWindowPos(h,nullptr,0,0,int(logicalWidth*scale),int(800*scale),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);ribbonTab=tab;ribbonCompact=false;layout();
        if(logicalWidth<1100&&!ribbonIconsOnly)throw std::runtime_error("small window retains button captions");
        for(const auto& group:visibleGroups()){
            if(group.r.Y!=80.f||group.r.GetRight()>width/scale)throw std::runtime_error("icon ribbon wraps or clips");
            for(const auto& button:group.items)if(button.r.X>=0){if(button.r.GetRight()>group.r.GetRight()||button.r.GetBottom()>group.r.GetBottom()-19)throw std::runtime_error("icon button outside group");if(logicalWidth==900&&tab!=6&&ribbonIconsOnly&&(button.r.Width>32||button.r.Height>30))throw std::runtime_error("narrow button retains caption width");}
            if(logicalWidth==900)for(const auto& button:group.items)if(button.r.X<0)throw std::runtime_error("900px icon layout hides a tool");
            if(tab==6&&group.name=="生成产物"){if(group.items.size()!=3||group.r.Width>200)throw std::runtime_error("generation buttons not one column");for(size_t j=0;j<3;++j){auto r=group.items[j].r;if(r.X!=group.items[0].r.X||r.Height>30||(j&&r.Y<=group.items[j-1].r.Y))throw std::runtime_error("generation buttons not stacked rectangles");}}
        }
        if(logicalWidth==900||logicalWidth==1500)screenshot(dir+"/ribbon-tab-"+std::to_string(tab)+"-"+std::to_string(logicalWidth)+".png");
    }
    std::ofstream(dir+"/responsive-icons-checks.json")<<"{\"widths\":[640,900,1100,1500],\"tabs\":[1,2,4,5,6],\"icon_grid\":true,\"stacked_generation_buttons\":true}";
    ribbonCompact=savedCompact;
    SetWindowPos(h,nullptr,0,0,originalWindow.right-originalWindow.left,originalWindow.bottom-originalWindow.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);ribbonTab=originalTab;layout();
    checkMetalExpansionLayout(*this,dir);arrowSizePreviewCheck(*this,dir);checkCurrentAlkoxides(*this,dir);checkMetalLabels(*this,dir);checkCurrentCarbene(*this,dir);checkOwnedElectronEndpoints(dir);checkBaseByproduct(*this,dir);checkMultilineByproducts(*this,dir);checkReciprocalArrows(*this,dir);
    int typographyCases=0;for(const char* font:{"Times New Roman","Arial"})for(double size:{8.,14.,10.}){checkBaseByproduct(*this,dir,font,size);++typographyCases;}std::ofstream(dir+"/bh-typography-checks.json")<<"{\"font_size_cases\":"<<typographyCases<<",\"matches_single_run_BH\":true,\"baseline\":true,\"editable_atoms\":true}";
    // Validate standard electron-state properties without private attributes.
    Document states;ObjectId id;for(int state=0;state<4;++state){states.addAtom(Vec2(state*40.,0),6,&id);auto sem=states.atoms().alive(id)->semantics;sem.implicitHydrogens=state==0?2:3;sem.radical=state==0?uint8_t(Radical::Singlet):state==1?uint8_t(Radical::Doublet):0;sem.charge=state==2?1:state==3?-1:0;states.setAtomSemantics(id,sem);}
    auto xml=persistence::writeCdxmlToString(states);if(xml.find("Radical=\"Singlet\"")==std::string::npos||xml.find("Radical=\"Doublet\"")==std::string::npos||xml.find("Charge=\"-1\"")==std::string::npos||xml.find("Charge=\"1\"")==std::string::npos)throw std::runtime_error("standard electron properties");Document reloaded;auto e=persistence::readCdxml(xml,reloaded);if(!e.ok()||reloaded.atoms().alive(0)->semantics.radical!=uint8_t(Radical::Singlet)||reloaded.atoms().alive(1)->semantics.radical!=uint8_t(Radical::Doublet))throw std::runtime_error("standard electron reload");std::ofstream(dir+"/electron-states-standard.cdxml")<<xml;
    if(xml.find("<represent")==std::string::npos||xml.find("SymbolType=\"Electron\"")==std::string::npos)throw std::runtime_error("standard editable electron symbols");
    Session nativeBack;if(!nativeBack.open(dir+"/electron-states-standard.cdxml").ok())throw std::runtime_error("standard symbols reopen");for(const auto& symbol:nativeBack.doc.symbols().records())if(symbol.isAlive())throw std::runtime_error("attached symbols must be promoted to their atoms");
    std::string binary,decoded,reason;if(!cdxbinary::fromXml(xml,binary,reason)||!cdxbinary::toXml(binary,decoded,reason))throw std::runtime_error("electron CDX conversion: "+reason);
    std::ofstream(dir+"/electron-states-standard.cdx",std::ios::binary).write(binary.data(),binary.size());
    if(decoded.find("<represent")==std::string::npos)throw std::runtime_error("CDX electron ownership lost");
    Session binaryBack;if(!binaryBack.open(dir+"/electron-states-standard.cdx").ok())throw std::runtime_error("CDX states reopen");for(const auto& symbol:binaryBack.doc.symbols().records())if(symbol.isAlive())throw std::runtime_error("CDX electron symbols duplicated");
    std::ofstream(dir+"/current-fixes-checks.json")<<"{\"arrow_sizes_preview\":true,\"metal_labels_and_expansion\":true,\"carbene_atom_symbol_equivalence\":true,\"small_products\":true,\"bh_layout\":true,\"standard_electron_properties\":true}";
    checkProjectionTemplates(*this,dir);checkGroupedSave(*this,dir);checkChairLayout(*this,dir);checkNativeCompatibility(*this,dir);s.dirty=false;PostMessageW(h,WM_CLOSE,0,0);
}
