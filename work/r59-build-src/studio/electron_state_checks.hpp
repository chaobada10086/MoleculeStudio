#pragma once
#include "electron_spacing_checks.hpp"
#include "charge_side_checks.hpp"
#include "radical_bond_clearance_checks.hpp"
#include "fishhook_heading_checks.hpp"
#include "short_hook_curvature_checks.hpp"
#include "mechanism_gesture_checks.hpp"
#include "hydroxyl_lone_pair_checks.hpp"
#include "electron_marker_delete_checks.hpp"
#include "electron_drag_owner_checks.hpp"
#include "isolated_lone_pair_checks.hpp"
#include "compact_donor_lone_pair_checks.hpp"
#include "polyatomic_ion_checks.hpp"
void App::electronStateCheck(const std::string& dir){
    struct RestoreTypography {
        bool enabled=chemcompletion::enabled(),formula=chemcompletion::formulaLayout();
        ~RestoreTypography(){chemcompletion::enabled()=enabled;chemcompletion::formulaLayout()=formula;}
    } restoreTypography;
    // Caption-format fixtures require typography on; do not depend on, or
    // overwrite, the user's saved automatic-layout preference.
    chemcompletion::enabled()=true;chemcompletion::formulaLayout()=true;
    checkElectronSpacing(*this,dir);
    checkAutomaticChargeSides(*this,dir);
    checkRadicalBondClearance(*this,dir);
    checkFishhookHeading(*this,dir);
    checkMechanismGestureRouting(*this,dir);
    checkShortHookCurvature(*this,dir);
    checkElectronDragOwner(*this,dir);
    checkElectronMarkerDeletion(*this,dir);
    checkHydroxylLonePairs(*this,dir);
    checkIsolatedLonePairs(*this,dir);
    checkCompactDonorLonePairs(*this,dir);
    checkPolyatomicIons(*this,dir);
    auto require=[&](bool value,const char* why){std::ofstream(dir+"/electron-state-progress.txt",std::ios::app)<<why<<": "<<(value?"pass":"FAIL")<<"\n";if(!value)throw std::runtime_error(why);};
    auto captureColor=[](ObjectRef ref){return ref.kind==ObjectKind::Bond?0xE67E00FFu:0x0078D4FFu;};
    auto matchesCaptureColor=[&](ObjectRef ref,uint32_t color){
        return ref.valid()?color==captureColor(ref):(color==0xE67E00FFu||color==0x0078D4FFu);
    };
    auto reset=[&](){s.cancel();s.doc=Document();s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());s.activateCommand("chooseSelectionTool_Marquee");s.refresh();};
    auto atom=[&](Vec2 p,int z=6){ObjectId id;require(s.doc.addAtom(p,z,&id).ok(),"fixture atom");return id;};
    auto bond=[&](ObjectId a,ObjectId b,BondOrder order=BondOrder::Single){ObjectId id;require(s.doc.addBond(a,b,&id).ok()&&s.doc.setBondOrder(id,order).ok(),"fixture bond");return id;};
    auto H=[&](ObjectId id){return chemistry::refreshAtomDerived(s.doc,id,{}).newHydrogenCount;};
    // Tool digits depend on the mouse hotspot, never on selection emptiness.
    auto hoverAt=[&](Vec2 p){auto v=s.view();v.center=p;v.scale=4;s.setView(v);s.refresh();const auto px=s.view().toDevice(p);SendMessageW(canvas,WM_MOUSEMOVE,0,MAKELPARAM(int(std::lround(px.x)),int(std::lround(px.y))));s.input.refreshHotspot(&s);};
    for(UINT key:std::vector<UINT>{'1','2','3','5','6','X'}){
        reset();auto host=atom(Vec2(80,80));hoverAt(Vec2(80,80));
        require(s.selected.empty()&&s.input.hotspot().kind==interaction::HotspotKind::Atom,"unselected atom is captured by the mouse");
        auto before=s.doc.digest();const auto tool=s.entries[s.activeIndex].command;
        require(shortcut(key,0),"captured atom retains native shortcut");
        require(s.doc.digest()!=before&&s.entries[s.activeIndex].command==tool,"atom shortcut edits captured structure rather than selecting a tool");
        if(key=='2'){bool carbonyl=false;for(auto bid:s.doc.incidentBonds(host)){auto b=s.doc.bonds().alive(bid);auto a=s.doc.atoms().alive(b->beginAtom==host?b->endAtom:b->beginAtom);carbonyl|=b->order==BondOrder::Double&&a->element==8;}require(carbonyl,"captured atom 2 still adds carbonyl");}
        if(key=='X')require(chemistry::readAtomLabel(s.doc,host,{}).text=="X","captured atom X retains original label shortcut");
        require(s.undo().ok()&&s.doc.digest()==before,"captured atom shortcut undoes normally");
        reset();auto a=atom(Vec2(80,80)),b=atom(Vec2(110,80));auto ab=bond(a,b,key=='1'?BondOrder::Double:BondOrder::Single);hoverAt(Vec2(95,80));
        require(s.selected.empty()&&s.input.hotspot().kind==interaction::HotspotKind::Bond,"unselected bond is captured by the mouse");before=s.doc.digest();
        require(shortcut(key,0),"captured bond retains native shortcut");
        if(key=='1'||key=='2'||key=='3')require(int(s.doc.bonds().alive(ab)->order)==int(key-'0'),"captured bond digits change bond order");
        if(key=='5'||key=='6')require(s.doc.stats().aliveAtoms==size_t(key-'0'),"captured bond digits fuse native ring size");
        if(key=='X')require(s.doc.digest()==before&&s.entries[s.activeIndex].command=="chooseBondTool_Solid","bond X retains generic single tool fallback");
    }
    {
        reset();auto selected=atom(Vec2(80,80));s.selected={ObjectRef(ObjectKind::Atom,selected)};hoverAt(Vec2(300,250));
        require(s.input.hotspot().kind==interaction::HotspotKind::None,"blank canvas has no capture despite a selected atom");
        const auto before=s.doc.digest();require(shortcut('2',0)&&s.entries[s.activeIndex].command=="chooseMultipleBondsTool_DoubleBond"&&s.doc.digest()==before,"no capture switches tool even when selection exists");
    }
    reset();
    for(const auto& entry:std::vector<std::pair<UINT,std::string>>{{'1',"chooseBondTool_Solid"},{'X',"chooseBondTool_Solid"},{'2',"chooseMultipleBondsTool_DoubleBond"},{'3',"chooseMultipleBondsTool_TripleBond"},{'5',"chooseRingTool_Cyclopentane"},{'6',"chooseRingTool_Cyclohexane"}}){
        s.activateCommand("chooseSelectionTool_Marquee");const auto before=s.doc.digest();
        require(shortcut(entry.first,0)&&s.entries[s.activeIndex].command==entry.second,"no capture shortcut chooses requested tool");
        require(s.doc.digest()==before,"tool shortcut does not modify structure");
        require(!shortcut(entry.first,0,true),"tool digit is left for text entry while editor is focused");
    }
    for(int element:{7,8}){
        reset();auto center=atom(Vec2(80,80));
        for(int i=0;i<(element==7?4:3);++i)bond(center,atom(Vec2(80+30*std::cos(i*1.57),80+30*std::sin(i*1.57))));
        auto before=s.doc.digest();
        require(preferAtomHydride(center,element),"N/O element shortcut accepts positive valence");
        require(s.doc.atoms().get(center)->element==element&&s.doc.atoms().get(center)->semantics.charge==1&&H(center)==0,"N/O shortcut creates saturated positive atom");
        require(s.undo().ok()&&s.doc.digest()==before,"N/O shortcut charge and element undo together");
    }
    auto selectAway=[&](const std::vector<ObjectId>& ids){s.refresh();SendMessageW(canvas,WM_MOUSEMOVE,0,MAKELPARAM(1100,700));s.selected.clear();for(auto id:ids)s.selected.push_back(ObjectRef(ObjectKind::Atom,id));require(s.input.hotspot().kind!=interaction::HotspotKind::Atom,"pointer is away from selected atoms");};
    auto dragCanvasMarker=[&](ObjectId id,bool radical,const char* tool,WPARAM modifiers){
        require(s.activateCommand(tool),"activate marker test tool");
        const auto host=s.doc.atoms().alive(id)->position;
        auto center=[&](){
            const auto& box=arrows::mechanismVisualBoxes(s.doc).at(id);
            if(radical)return (box.radicalPoints.front()+box.radicalPoints.back())*.5;
            require(box.hasCharge,"the atom has a visible charge");
            return (box.charge.min+box.charge.max)*.5;
        };
        const auto start=center();
        const auto semantics=s.doc.atoms().alive(id)->semantics;
        const auto bondsBefore=s.doc.stats().aliveBonds;
        const auto arrowsBefore=s.doc.arrows().size();
        const auto before=s.doc.digest();
        auto view=s.view();view.center=host;view.scale=4;s.setView(view);
        auto point=[&](Vec2 p){
            auto px=s.view().toDevice(p);
            return MAKELPARAM(int(std::lround(px.x)),int(std::lround(px.y)));
        };
        const auto end=point(host+Vec2(-60,60));
        SendMessageW(canvas,WM_LBUTTONDOWN,MK_LBUTTON|modifiers,point(start));
        require(electronDragAtom==id&&electronDragSite==(radical?4:2),"canvas press captures the intended electron marker");
        SendMessageW(canvas,WM_MOUSEMOVE,MK_LBUTTON|modifiers,end);
        require(s.doc.digest()==before,"electron-marker motion only previews");
        SendMessageW(canvas,WM_LBUTTONUP,modifiers,end);
        const auto moved=center();
        const auto* changed=s.doc.atoms().alive(id);
        const double em=integration::molecular::setting(s.doc,"LabelSize",10);
        require(length(moved-start)>2&&length(moved-host)<=em*1.4+.001&&changed->position==host&&
                changed->semantics==semantics&&s.doc.stats().aliveBonds==bondsBefore&&s.doc.arrows().size()==arrowsBefore,
                "drag moves just the marker within its range");
        require(s.undo().ok()&&s.doc.digest()==before,"electron marker placement is one undo");
    };
    struct MarkerCase {int element,charge;bool radical;const char* label;};
    for(const auto& fixture:std::vector<MarkerCase>{
        {7,-1,false,""},{8,-1,false,""},{6,-1,false,""},{7,1,false,""},{8,1,false,""},
        {15,1,false,"Ph3P"},{6,1,false,""},{6,0,true,""},{7,0,true,""},{8,0,true,""}
    }){
        reset();
        auto host=atom(Vec2(80,80),fixture.element),other=atom(Vec2(105,80),7);
        bond(host,other);bond(other,atom(Vec2(120,64)));
        auto sem=s.doc.atoms().alive(host)->semantics;
        sem.charge=fixture.charge;sem.radical=fixture.radical?uint8_t(Radical::Doublet):0;sem.implicitHydrogens=0;
        require(s.doc.setAtomSemantics(host,sem).ok(),"set atom-owned marker semantics");
        if(*fixture.label)require(s.doc.setAtomLabel(host,fixture.label).ok(),"set compact phosphonium label");
        s.refresh();
        dragCanvasMarker(host,fixture.radical,"chooseSelectionTool_Marquee",0);
        dragCanvasMarker(host,fixture.radical,"chooseBondTool_Solid",0);
        dragCanvasMarker(host,fixture.radical,"chooseArrowTool_90_CW",MK_SHIFT);
    }
    struct DeleteCase {int element,charge;bool radical,circled;int bonds,hydrogens;const char* label;};
    for(const auto& fixture:std::vector<DeleteCase>{
        {6,1,false,false,3,1,""},{6,-1,false,true,1,3,""},
        {6,0,true,false,2,2,""},{7,-1,false,false,1,2,"HN−"},
        {8,-1,false,false,1,1,""},{8,0,true,false,1,1,""},
        {7,1,false,false,4,0,""},{1,1,false,true,0,0,"H"}
    }){
        reset();
        const Vec2 position(80,80);
        const auto host=atom(position,fixture.element);
        for(int i=0;i<fixture.bonds;++i)bond(host,atom(position+core::direction(90.*i)*24));
        auto sem=s.doc.atoms().alive(host)->semantics;
        sem.charge=fixture.charge;sem.radical=fixture.radical?uint8_t(Radical::Doublet):0;sem.implicitHydrogens=0;
        require(s.doc.setAtomSemantics(host,sem).ok(),"set deletable electron state");
        if(*fixture.label)require(s.doc.setAtomLabel(host,fixture.label).ok(),"set imported marker label");
        const auto ref=ObjectRef(ObjectKind::Atom,host);
        auto& bag=s.doc.extensions(ref);
        if(fixture.circled||fixture.radical)bag.set(integration::molecular::electronMarkerKey(fixture.radical),"0.4 -0.75");
        if(fixture.circled)bag.set("pst.attr.MoleculeStudioChargeCircle","1");
        s.refresh();
        auto view=s.view();view.center=position;view.scale=4;s.setView(view);
        const auto& markers=arrows::mechanismVisualBoxes(s.doc).at(host);
        const auto center=fixture.radical?markers.radicalPoints.front():(markers.charge.min+markers.charge.max)*.5;
        const auto px=s.view().toDevice(center);
        const auto mouse=MAKELPARAM(int(std::lround(px.x)),int(std::lround(px.y)));
        const auto before=s.doc.digest();
        const auto history=s.history.cursor();
        if(fixture.radical){
            s.selected={ref};
            SendMessageW(canvas,WM_LBUTTONDOWN,MK_LBUTTON,mouse);
            SendMessageW(canvas,WM_LBUTTONUP,0,mouse);
            require(s.selected.empty()&&s.doc.digest()==before,"marker click leaves the atom intact and directs Delete to the dot");
            SendMessageW(canvas,WM_KEYDOWN,VK_DELETE,0);
        }else{
            require(s.activateCommand("chooseEdittingTool_Eraser"),"activate marker eraser");
            SendMessageW(canvas,WM_LBUTTONDOWN,MK_LBUTTON,mouse);
            SendMessageW(canvas,WM_LBUTTONUP,0,mouse);
        }
        const auto* neutral=s.doc.atoms().alive(host);
        require(neutral&&neutral->position==position&&!neutral->semantics.charge&&!neutral->semantics.radical&&
                s.doc.incidentBonds(host).size()==size_t(fixture.bonds)&&s.doc.stats().aliveAtoms==size_t(fixture.bonds+1),
                "remove only the marker, keeping the original atoms and bonds");
        require(H(host)==fixture.hydrogens&&neutral->semantics.implicitHydrogens==fixture.hydrogens,
                "recompute hydrogen count for the remaining bonds");
        if(fixture.element==7&&fixture.charge==-1)
            require(s.doc.atomLabel(host)=="N"&&arrows::mechanismVisualBoxes(s.doc).at(host).labelGlyphs.size()>=3,
                    "imported nitrogen label loses its minus and displays NH2");
        const auto& remaining=s.doc.extensions(ref);
        require(!remaining.findValue(integration::molecular::electronMarkerKey(fixture.radical))&&
                !remaining.findValue("pst.attr.MoleculeStudioChargeCircle"),"clear obsolete marker position and circle");
        const auto after=s.doc.digest();
        require(s.history.cursor()==history+1&&s.undo().ok()&&s.doc.digest()==before&&
                s.undo(true).ok()&&s.doc.digest()==after,"marker removal and hydrogen update are one undo step");
    }
    const auto actualMarkerFile=dir+"/user-marker-copy.cdxml";
    if(std::ifstream(actualMarkerFile).good()){
        reset();
        require(s.open(actualMarkerFile).ok(),"open the user's copied mechanism");
        ObjectId nitrogen=kInvalidObjectId;
        for(const auto& a:s.doc.atoms().records())if(a.isAlive()&&a.element==7&&a.semantics.charge==-1)
            nitrogen=a.id;
        require(nitrogen!=kInvalidObjectId,"find the product nitrogen anion from the screenshot");
        dragCanvasMarker(nitrogen,false,"chooseBondTool_Solid",0);
        dragCanvasMarker(nitrogen,false,"chooseArrowTool_90_CW",MK_SHIFT);
    }
    // Reproduce the two connected five-membered rings and rotate only the
    // right-hand terminal bond, with a single press-and-drag.
    {
        reset();
        const auto left=atom(Vec2(38,43)),right=atom(Vec2(60,45));
        auto ring=[&](ObjectId first,const std::vector<Vec2>& corners){
            auto previous=first;
            for(auto point:corners){const auto corner=atom(point);bond(previous,corner);previous=corner;}
            return bond(previous,first);
        };
        const auto ringEdge=ring(left,{Vec2(25,25),Vec2(4,32),Vec2(4,54),Vec2(25,61)});
        ring(right,{Vec2(75,29),Vec2(95,38),Vec2(93,60),Vec2(71,65)});
        bond(left,right);bond(left,atom(Vec2(49,24)));
        const auto terminal=atom(Vec2(59,23));
        const auto selectedBond=bond(right,terminal);
        s.refresh();
        auto view=s.view();view.center=Vec2(60,45);view.scale=4;view.dpiScale=1;s.setView(view);
        auto pointer=[&](UINT message,Vec2 p,WPARAM keys=0){
            const auto px=view.toDevice(p);
            SendMessageW(canvas,message,keys,MAKELPARAM(int(std::lround(px.x)),int(std::lround(px.y))));
        };
        std::map<ObjectId,Vec2> original;
        for(const auto& a:s.doc.atoms().records())if(a.isAlive())original[a.id]=a.position;
        const auto before=s.doc.digest();
        const Vec2 grab(59.5,34),drop(60.5,56),expected(61,67);
        pointer(WM_LBUTTONDOWN,grab,MK_LBUTTON);
        pointer(WM_MOUSEMOVE,drop,MK_LBUTTON);
        require(transformDragging&&s.selected==std::vector<ObjectRef>{ObjectRef(ObjectKind::Bond,selectedBond)}&&
                bondRotationAtoms==std::vector<ObjectRef>{ObjectRef(ObjectKind::Atom,terminal)}&&s.doc.digest()==before,
                "press-drag selects the terminal bond and previews only its free end");
        const auto& preview=selectionTransformScene();
        require(preview.size()>0&&bondRotationPreview&&
                length(bondRotationPreview->doc.atoms().alive(terminal)->position-expected)<1e-6,
                "terminal bond visibly turns from above the ring to below it");
        pointer(WM_LBUTTONUP,drop);
        require(!transformDragging&&length(s.doc.atoms().alive(terminal)->position-expected)<1e-6,
                "mouse-up commits the displayed planar angle");
        for(const auto& entry:original)if(entry.first!=terminal)
            require(s.doc.atoms().alive(entry.first)->position==entry.second,"both ring skeletons and the other branch stay fixed");
        const auto finalBond=s.doc.bonds().alive(selectedBond);
        require(finalBond->beginAtom==right&&finalBond->endAtom==terminal&&finalBond->order==BondOrder::Single&&
                std::abs(length(expected-original.at(right))-length(original.at(terminal)-original.at(right)))<1e-6,
                "rotated bond keeps its length and connectivity");
        const auto after=s.doc.digest();
        require(s.undo().ok()&&s.doc.digest()==before&&s.undo(true).ok()&&s.doc.digest()==after,
                "planar bond rotation is one undoable and redoable edit");
        const auto handle=view.toDevice(selectionHandlePoints()[8]);
        require(beginSelectionTransform(handle,kModShift),"bond rotation handle also supports angle snapping");
        updateSelectionTransform(handle+Vec2(20,0),kModShift);
        SendMessageW(canvas,WM_CAPTURECHANGED,0,0);
        require(!transformDragging&&s.doc.digest()==after,"losing capture cancels the rotation preview");
        const auto edge=s.doc.bonds().alive(ringEdge);
        const auto middle=(original.at(edge->beginAtom)+original.at(edge->endAtom))*.5;
        s.selected={ObjectRef(ObjectKind::Bond,ringEdge)};
        pointer(WM_LBUTTONDOWN,middle,MK_LBUTTON);pointer(WM_MOUSEMOVE,middle+Vec2(8,6),MK_LBUTTON);
        pointer(WM_LBUTTONUP,middle+Vec2(8,6));
        require(!transformDragging&&s.doc.digest()==after,"ring bonds do not stretch or translate when dragged");
    }
    // Default-length bonds must be independently selectable at ordinary zoom.
    {
        reset();
        const double unit=s.doc.defaultBondLength();
        const auto a=atom(Vec2(0,0)),b=atom(Vec2(unit,0));
        const auto fixed=atom(Vec2(-8,12)),moving=atom(Vec2(unit+8,12));
        const auto axle=bond(a,b);
        bond(a,fixed);bond(b,moving);
        s.refresh();
        auto view=s.view();view.center=Vec2(unit*.5,0);view.scale=.75;view.dpiScale=1;s.setView(view);
        auto pointer=[&](UINT message,Vec2 p,WPARAM keys=0){
            const auto px=view.toDevice(p);
            SendMessageW(canvas,message,keys,MAKELPARAM(int(std::lround(px.x)),int(std::lround(px.y))));
        };
        const auto middle=Vec2(unit*.5,0),grab=Vec2(unit*.64,0);
        const auto ref=ObjectRef(ObjectKind::Bond,axle);
        require(s.nearestAtom(middle,8)!=kInvalidObjectId,"zoomed-out fixture overlaps the old atom radius");
        require(s.pick(middle)==ref&&s.pick(Vec2())==ObjectRef(ObjectKind::Atom,a),
                "select the middle of the short bond and still hit its actual endpoint");
        const auto before=s.doc.digest();
        pointer(WM_LBUTTONDOWN,grab,MK_LBUTTON);
        pointer(WM_MOUSEMOVE,grab+Vec2(0,64),MK_LBUTTON);
        require(transformDragging&&s.selected==std::vector<ObjectRef>{ref},"first drag on short bond starts planar rotation");
        pointer(WM_LBUTTONUP,grab+Vec2(0,64));
        require(s.doc.atoms().alive(a)->position==Vec2(0,0)&&s.doc.atoms().alive(fixed)->position==Vec2(-8,12)&&
                length(s.doc.atoms().alive(b)->position-Vec2(unit,0))>1&&
                length((s.doc.atoms().alive(moving)->position-s.doc.atoms().alive(b)->position)-Vec2(8,12))<1e-6,
                "short bond rotates only its attached branch at ordinary zoom");
        require(s.undo().ok()&&s.doc.digest()==before,"first-drag rotation undoes exactly");
        s.selected=s.all();
        require(s.selected.size()>1,"generated-product style multi-selection fixture");
        pointer(WM_LBUTTONDOWN,grab,MK_LBUTTON);pointer(WM_LBUTTONUP,grab);
        require(s.selected==std::vector<ObjectRef>{ref}&&s.doc.digest()==before,
                "click a bond inside the multi-selection to select only the bond");
        pointer(WM_LBUTTONDOWN,grab,MK_LBUTTON);
        pointer(WM_MOUSEMOVE,grab+Vec2(0,64),MK_LBUTTON);
        pointer(WM_LBUTTONUP,grab+Vec2(0,64));
        require(s.doc.atoms().alive(a)->position==Vec2(0,0)&&length(s.doc.atoms().alive(b)->position-Vec2(unit,0))>1&&
                std::abs(length(s.doc.atoms().alive(b)->position)-unit)<1e-6,
                "selected bond rotates without dragging the previous selection");
    }
    // The keyboard must repaint both the bond and the stationary mechanism
    // hover after an edit branches from a document with a redo entry.
    {
        reset();
        auto carbon=atom(Vec2(0,0)),oxygen=atom(Vec2(30,0),8);
        auto carbonyl=bond(carbon,oxygen,BondOrder::Double);
        require(s.submit(chemistry::setBondOrderWithRefreshCommand(carbonyl,BondOrder::Triple),"fixture redo").ok()&&
                s.undo().ok(),"carbonyl fixture has a redo entry");
        auto view=s.view();view.center=Vec2(15,0);view.scale=2;s.setView(view);
        chooseArrowPreset(49);
        auto segments=[&](){
            size_t count=0;
            for(const auto& p:s.scene.primitives())if(p.source==ObjectRef(ObjectKind::Bond,carbonyl))++count;
            return count;
        };
        Vec2 contact;
        require(arrows::mechanismBondContact(s.doc,carbonyl,Vec2(15,4),&contact),"capture actual carbonyl ink");
        auto px=s.view().toDevice(contact);
        SendMessageW(canvas,WM_MOUSEMOVE,0,MAKELPARAM(int(std::lround(px.x)),int(std::lround(px.y))));
        UpdateWindow(canvas);
        auto preview=s.ghost.generation();
        require(!s.ghost.empty()&&segments()==2,"mechanism hover highlights the double bond");
        s.selected={ObjectRef(ObjectKind::Bond,carbonyl)};
        SendMessageW(canvas,WM_KEYDOWN,'1',0);
        require(s.doc.bonds().alive(carbonyl)->order==BondOrder::Single&&segments()==1,
                "key 1 immediately replaces double-bond scene geometry");
        require(s.ghost.generation()!=preview,"capture preview updates without another mouse event");
        require(s.doc.atoms().alive(oxygen)->semantics.implicitHydrogens==1,"carbonyl oxygen becomes a hydroxyl on key 1");
        integration::RingGeometry rings(s.doc);
        const auto labels=integration::molecular::labels(s.doc,s.glyphs,rings);
        const auto& glyphs=labels.at(oxygen).layout.glyphs;
        require(glyphs.size()==2&&glyphs[0].codepoint=='O'&&glyphs[1].codepoint=='H'&&glyphs[0].x<glyphs[1].x,
                "the refreshed canvas displays OH immediately");
        require(s.undo().ok()&&s.doc.bonds().alive(carbonyl)->order==BondOrder::Double&&
                s.doc.atoms().alive(oxygen)->semantics.implicitHydrogens<1,"one undo restores C=O");
        require(s.undo(true).ok()&&s.doc.atoms().alive(oxygen)->semantics.implicitHydrogens==1,"redo restores OH");
    }
    // A short C–O return aims its solid head at the O in a crowded oxonium.
    {
        reset();
        const auto oxygen=atom(Vec2(60,36),8),center=atom(Vec2(60,51)),other=atom(Vec2(46,51));
        const auto edge=bond(oxygen,center);
        bond(oxygen,atom(Vec2(47,28),1));bond(oxygen,atom(Vec2(73,28),1));bond(center,other);
        auto ring=[&](ObjectId start,const std::vector<Vec2>& points){
            auto previous=start;
            for(auto point:points){const auto next=atom(point);bond(previous,next);previous=next;}
            bond(previous,start);
        };
        ring(center,{Vec2(69,40),Vec2(83,45),Vec2(83,58),Vec2(69,63)});
        ring(other,{Vec2(37,40),Vec2(23,45),Vec2(23,58),Vec2(37,63)});
        bond(other,atom(Vec2(53,64),8));
        auto sem=s.doc.atoms().alive(oxygen)->semantics;
        sem.charge=1;sem.implicitHydrogens=0;s.doc.setAtomSemantics(oxygen,sem);
        const auto o=ObjectRef(ObjectKind::Atom,oxygen);
        auto& attributes=s.doc.extensions(o);
        attributes.set(integration::molecular::electronMarkerKey(false),"0 -.9");
        attributes.set("pst.attr.MoleculeStudioChargeCircle","1");
        s.refresh();
        auto view=s.view();view.center=Vec2(56,46);view.scale=5;view.dpiScale=1;s.setView(view);
        chooseArrowPreset(49);
        const auto box=arrows::mechanismAtomBodyBox(s.doc,*s.doc.atoms().alive(oxygen));
        const auto glyphCenter=(box.min+box.max)*.5;
        Vec2 from;
        require(arrows::mechanismBondContact(s.doc,edge,Vec2(60,46),&from),"short return starts on visible C–O ink");
        const auto obstacles=arrows::mechanismObstacles(s.doc);
        require(s.gesture(PointerPhase::Down,from).ok()&&s.gesture(PointerPhase::Move,glyphCenter).ok()&&
                s.gesture(PointerPhase::Up,glyphCenter).ok(),"draw the reference C–O return with normal gestures");
        arrows::ArrowParams arrow;bool present=false;
        require(arrows::readArrow(s.doc,0,&arrow,&present).ok()&&present&&
                arrow.electronSource==ObjectRef(ObjectKind::Bond,edge)&&arrow.electronTarget==o&&arrow.targetVisualAnchor==1,
                "short return captures the oxygen body independently of the H atoms and plus sign");
        arrows::ArrowGeometry geometry;
        require(arrows::buildArrowGeometry(arrow,.6,arrows::Tessellation(),&geometry).ok()&&geometry.heads.size()==1&&
                geometry.heads[0].outline.size()==4,"short return has a visible solid arrowhead");
        const auto& head=geometry.heads.front();
        const auto direction=head.tip-(head.outline[1]+head.outline[3])*.5,aim=glyphCenter-head.tip;
        const auto gap=arrows::mechanismBoxDistance(box,head.tip);
        const auto heading=direction*(1./length(direction));
        require(dot(heading,aim)>0&&arrows::mechanismBoxDistance(box,head.tip+heading*dot(heading,aim))<.2&&gap>.45&&gap<2.6,
                "visible arrowhead is close to the O and its axis enters the chosen glyph");
        require(arrows::mechanismRouteScore(arrow,obstacles,true)==0&&
                arrows::mechanismOutsideBondInk(s.doc,edge,arrow.begin,.8),
                "the short curve clears printed glyphs and starts beside the selected bond");
        s.selected.clear();s.fit();invalidate();screenshot(dir+"/short-oxygen-return.png");
        s.selected={ObjectRef(ObjectKind::Arrow,0)};
        generateMechanismStep();
        ObjectRef product;
        for(auto ref:s.selected)if(ref.kind==ObjectKind::Atom){
            const auto* a=s.doc.atoms().alive(ref.id);
            if(a&&a->element==6&&a->semantics.charge==1)product=ref;
        }
        require(product.valid()&&s.doc.incidentBonds(product.id).size()==3,
                "C–O cleavage generates a tertiary carbocation");
        const auto originalPosition=s.doc.atoms().alive(product.id)->position;
        const auto mark=arrows::mechanismVisualBoxes(s.doc).at(product.id).charge;
        const auto markCenter=(mark.min+mark.max)*.5;
        require(!integration::molecular::attachedElectronMarker(s.doc,product.id,false),
                "the generated charge starts at the default position without a stored offset");
        s.fit();
        auto at=s.view().toDevice(markCenter),dragTo=s.view().toDevice(originalPosition+Vec2(60,70));
        auto send=[&](UINT msg,Vec2 p,WPARAM flags=0){
            SendMessageW(canvas,msg,flags,MAKELPARAM(int(std::lround(p.x)),int(std::lround(p.y))));
        };
        const auto digest=s.doc.digest();
        send(WM_LBUTTONDOWN,at,MK_LBUTTON);
        require(electronDragAtom==product.id&&electronDragSite==2&&!transformDragging,
                "actual mouse press picks the generated positive charge within the selected product");
        send(WM_MOUSEMOVE,dragTo,MK_LBUTTON);send(WM_LBUTTONUP,dragTo);
        const auto moved=arrows::mechanismVisualBoxes(s.doc).at(product.id).charge;
        const auto offset=(moved.min+moved.max)*.5-originalPosition;
        const auto em=integration::molecular::setting(s.doc,"LabelSize",10);
        require(std::abs(length(offset)-em*1.4)<1e-4&&offset.y>0,
                "dragging the generated charge changes its direction and clamps its range");
        require(s.doc.atoms().alive(product.id)->position==originalPosition&&
                s.doc.atoms().alive(product.id)->semantics.charge==1,
                "the carbocation center and its formal charge stay on the original atom");
        const auto result=s.doc.digest();
        require(s.undo().ok()&&s.doc.digest()==digest&&s.undo(true).ok()&&s.doc.digest()==result,
                "moving the generated marker is one undoable edit");
    }
    // Semipinacol rearrangement from the screenshot: two five-membered
    // rings joined by a bond, with a carbocation beside the alcohol.
    {
        reset();
        const auto acceptor=atom(Vec2(30,22)),donor=atom(Vec2(44,23.5));
        const auto migrating=atom(Vec2(54,13)),oxygen=atom(Vec2(39,37),8);
        auto cycle=[&](ObjectId first,const std::vector<Vec2>& corners){
            auto last=first;
            for(auto point:corners){auto next=atom(point);bond(last,next);last=next;}
            bond(last,first);
        };
        cycle(acceptor,{Vec2(22,10),Vec2(8,14),Vec2(8,28),Vec2(22,34)});
        const auto migrationBond=bond(donor,migrating);
        const auto top=atom(Vec2(67,18.5)),bottom=atom(Vec2(65.5,33)),lower=atom(Vec2(51,36));
        bond(migrating,top);bond(top,bottom);bond(bottom,lower);bond(lower,donor);
        bond(acceptor,donor);const auto co=bond(donor,oxygen);
        auto sem=s.doc.atoms().alive(acceptor)->semantics;
        sem.charge=1;sem.implicitHydrogens=0;s.doc.setAtomSemantics(acceptor,sem);
        sem=s.doc.atoms().alive(oxygen)->semantics;
        sem.implicitHydrogens=1;s.doc.setAtomSemantics(oxygen,sem);
        auto ref=[](ObjectId id){return ObjectRef(ObjectKind::Atom,id);};
        s.doc.extensions(ref(oxygen)).set(mechanism::lonePairKey(),"1");
        mechanism::setLonePairOffset(s.doc,oxygen,0,Vec2(-.65,-.35));
        s.refresh();
        auto view=s.view();view.center=Vec2(38,22);view.scale=5;view.dpiScale=1;s.setView(view);
        chooseArrowPreset(49);
        const auto& boxes=arrows::mechanismVisualBoxes(s.doc);
        const auto charge=boxes.at(acceptor).charge,pair=boxes.at(oxygen).lonePairs.front();
        Vec2 source,target;
        require(arrows::mechanismBondContact(s.doc,migrationBond,Vec2(49,18),&source)&&
                arrows::mechanismBondContact(s.doc,co,Vec2(42,28),&target),"ring migration fixture has visible bond targets");
        auto draw=[&](Vec2 a,Vec2 b){
            return s.gesture(PointerPhase::Down,a).ok()&&s.gesture(PointerPhase::Move,b).ok()&&s.gesture(PointerPhase::Up,b).ok();
        };
        require(draw(source,(charge.min+charge.max)*.5)&&draw((pair.min+pair.max)*.5,target),
                "draw both screenshot arrows with normal pointer capture");
        std::vector<arrows::ArrowParams> input(2);
        for(size_t i=0;i<input.size();++i){
            bool ok=false;
            require(arrows::readArrow(s.doc,ObjectId(i),&input[i],&ok).ok()&&ok,"read each drawn rearrangement arrow");
        }
        require(input[0].electronSource==ObjectRef(ObjectKind::Bond,migrationBond)&&input[0].electronTarget==ref(acceptor)&&
                input[1].electronSource==ref(oxygen)&&input[1].electronTarget==ObjectRef(ObjectKind::Bond,co),
                "both migration and oxygen donation retain their captured endpoints");
        std::vector<mechanism::ElectronMove> moves;
        const auto inferred=mechanism::inferElectronMoves(s.doc,input,&moves);
        if(!inferred.ok())throw std::runtime_error(inferred.message);
        require(moves.size()==2,"jointly infer ring expansion and oxygen donation");
        auto toCarbon=input;
        toCarbon[1].electronTarget=ref(donor);toCarbon[1].end=s.doc.atoms().alive(donor)->position;toCarbon[1].targetVisualAnchor=1;
        std::reverse(toCarbon.begin(),toCarbon.end());
        std::vector<mechanism::ElectronMove> alternate;
        require(mechanism::inferElectronMoves(s.doc,toCarbon,&alternate).ok()&&alternate.size()==2,
                "oxygen arrow can also target the carbon directly and arrow order does not matter");
        auto freeEnds=input;
        freeEnds[0].electronSource=ObjectRef();freeEnds[0].sourceVisualAnchor=0;
        freeEnds[0].begin=Vec2(48.5,16.0);
        freeEnds[0].electronTarget=ObjectRef();freeEnds[0].targetVisualAnchor=0;
        freeEnds[0].end=Vec2(33.7,14);
        freeEnds[1].electronTarget=ObjectRef();freeEnds[1].targetVisualAnchor=0;
        freeEnds[1].end=Vec2(41.2,27.3);
        require(mechanism::inferElectronMoves(s.doc,freeEnds,&alternate).ok()&&alternate.size()==2&&
                alternate[0].source==ObjectRef(ObjectKind::Bond,migrationBond)&&alternate[0].target==ref(acceptor),
                "infer the same reaction from nearby unbound tips and the displayed carbocation sign");
        Document trial=s.doc;std::vector<ObjectRef> created;
        require(mechanism::generateNextStep(trial,moves,&created).ok(),"lay out the expanded ring without stretching its closing bond");
        auto mapped=[&](ObjectId id){return created.at(id).id;};
        auto order=[&](ObjectId a,ObjectId b){
            for(auto id:trial.incidentBonds(mapped(a))){
                const auto* e=trial.bonds().alive(id);
                if(e->beginAtom==mapped(b)||e->endAtom==mapped(b))return int(e->order);
            }
            return 0;
        };
        require(order(donor,migrating)==0&&order(acceptor,migrating)==1&&order(acceptor,donor)==1&&
                order(migrating,top)==1&&order(top,bottom)==1&&order(bottom,lower)==1&&order(lower,donor)==1,
                "the original five-membered migrating ring becomes a six-membered ring at the spiro center");
        require(order(donor,oxygen)==2&&trial.atoms().alive(mapped(oxygen))->semantics.charge==1&&
                trial.atoms().alive(mapped(acceptor))->semantics.charge==0&&
                mechanism::lonePairCount(trial,mapped(oxygen))==0,
                "produce protonated carbonyl and consume the drawn oxygen pair");
        const auto before=s.doc.digest();
        s.selected={ObjectRef(ObjectKind::Arrow,0),ObjectRef(ObjectKind::Arrow,1)};
        generateMechanismStep();
        require(s.doc.stats().aliveAtoms==22&&s.doc.digest()!=before,"generate the rearrangement through the actual app command");
        ObjectRef protonated;
        for(auto r:s.selected)if(r.kind==ObjectKind::Atom){
            const auto* a=s.doc.atoms().alive(r.id);
            if(a&&a->element==8&&a->semantics.charge==1&&a->semantics.implicitHydrogens==1)protonated=r;
        }
        require(protonated.valid(),"the generated carbonyl has one attached proton");
        const auto anchor=s.doc.atoms().alive(protonated.id)->position;
        const auto& visual=arrows::mechanismVisualBoxes(s.doc).at(protonated.id);
        require(visual.hasAttachedProton,"the visible H in the charged carbonyl has its own drag area");
        const auto hCenter=(visual.attachedProton.min+visual.attachedProton.max)*.5;
        const auto dragStart=s.view().toDevice(hCenter);
        const auto* coBond=s.doc.bonds().alive(s.doc.incidentBonds(protonated.id).front());
        const auto carbon=coBond->beginAtom==protonated.id?coBond->endAtom:coBond->beginAtom;
        const auto outward=anchor-s.doc.atoms().alive(carbon)->position;
        const auto dragEnd=s.view().toDevice(anchor+outward*5.);
        auto pointer=[&](UINT msg,Vec2 p,WPARAM flags=0){
            SendMessageW(canvas,msg,flags,MAKELPARAM(int(std::lround(p.x)),int(std::lround(p.y))));
        };
        const auto compact=s.doc.digest();
        pointer(WM_LBUTTONDOWN,dragStart,MK_LBUTTON);
        pointer(WM_LBUTTONUP,dragStart);
        require(s.doc.digest()==compact,"a click on the inline H does not change the molecule");
        pointer(WM_LBUTTONDOWN,dragStart,MK_LBUTTON);
        pointer(WM_MOUSEMOVE,dragEnd,MK_LBUTTON);
        require(electronDragAtom==protonated.id&&electronDragSite==attachedProtonDragSite&&
                attachedProtonPreview&&attachedProtonPreview->doc.stats().aliveAtoms==23&&
                attachedProtonPreview->doc.atoms().alive(protonated.id)->semantics.implicitHydrogens==0&&s.doc.digest()==compact,
                "dragging the H previews an explicit hydrogen and leaves the live document untouched");
        SendMessageW(canvas,WM_KEYDOWN,VK_ESCAPE,0);
        require(!attachedProtonPreview&&s.doc.digest()==compact,"Escape cancels proton extraction");
        pointer(WM_LBUTTONDOWN,dragStart,MK_LBUTTON);
        pointer(WM_MOUSEMOVE,dragEnd,MK_LBUTTON);
        pointer(WM_LBUTTONUP,dragEnd);
        require(s.selected.size()==1&&s.selected.front().kind==ObjectKind::Atom,"the exposed H is selected");
        const auto hydrogen=s.selected.front();
        const auto* h=s.doc.atoms().alive(hydrogen.id);
        const auto* o=s.doc.atoms().alive(protonated.id);
        require(h&&h->element==1&&h->semantics.charge==0&&o->position==anchor&&
                o->semantics.charge==1&&o->semantics.implicitHydrogens==0,
                "one implicit H becomes explicit and the host position and charge are preserved");
        const auto distance=length(h->position-anchor);
        require(std::abs(distance-s.doc.defaultBondLength()*1.5)<1e-6&&dot(h->position-anchor,outward)>0,
                "the new bond follows the drag direction and stops at one and a half standard lengths");
        const auto incident=s.doc.incidentBonds(hydrogen.id);
        require(incident.size()==1,"the exposed H has one bond");
        const auto* oh=s.doc.bonds().alive(incident.front());
        require(oh&&oh->order==BondOrder::Single&&(oh->beginAtom==protonated.id||oh->endAtom==protonated.id),
                "the extracted H is covalently bonded to the original oxygen");
        Vec2 bondContact;
        require(arrows::mechanismBondContact(s.doc,oh->id,(h->position+anchor)*.5,&bondContact),
                "the new O–H single bond has visible ink available to mechanism arrows");
        require(s.undo().ok()&&s.doc.digest()==compact,"proton extraction is one undoable edit");
        require(s.undo().ok()&&s.doc.digest()==before,"rearrangement and both arrow bindings undo together");
    }
    // Main-group lone pairs depend on the actual bond order and charge.
    {
        struct PairCase {int element,bonds,charge,pairs;};
        const PairCase cases[]={
            {15,3,0,1},{15,4,1,0},{15,5,0,0}, // phosphine, phosphonium, P(V)
            {33,3,0,1},{51,3,0,1},{83,3,0,1},
            {16,2,0,2},{16,4,0,1},{16,6,0,0},
            {34,2,0,2},{52,2,0,2},{53,3,0,2},
            {14,4,0,0},{50,2,0,1},{82,2,0,1}
        };
        for(const auto& fixture:cases){
            reset();
            const auto center=atom(Vec2(40,40),fixture.element);
            auto sem=s.doc.atoms().alive(center)->semantics;
            sem.charge=fixture.charge;sem.implicitHydrogens=0;s.doc.setAtomSemantics(center,sem);
            for(int i=0;i<fixture.bonds;++i){
                const auto angle=6.28318530718*i/fixture.bonds;
                bond(center,atom(Vec2(40,40)+Vec2(std::cos(angle),std::sin(angle))*15.));
            }
            require(mechanism::lonePairCapacity(s.doc,center)==fixture.pairs,"main-group lone-pair capacity matches the tested valence");
            s.refresh();s.selected={ObjectRef(ObjectKind::Atom,center)};
            const auto digest=s.doc.digest();
            for(int i=0;i<fixture.pairs+1;++i)execute(Button("","","","@mechanismLonePair"));
            require(mechanism::lonePairCount(s.doc,center)==fixture.pairs&&
                    (fixture.pairs>0||s.doc.digest()==digest),"lone-pair button respects actual capacity");
            if(fixture.pairs>0)require(arrows::mechanismVisualBoxes(s.doc).at(center).lonePairs.size()==size_t(fixture.pairs),
                    "newly supported lone pairs appear as real, capturable glyphs");
        }
    }
    for(bool caption:{false,true})for(const char* formula:{"PPh3","Ph3P","PPh₃","Ph₃P"}){
        reset();
        const Vec2 point(55,55);
        ObjectId id;
        if(caption){
            require(s.doc.addText(point,10,"",&id).ok(),"typed triphenylphosphine caption fixture");
            s.refresh();
            openText(id);
            textEditor.select(0,-1);
            const auto typed=W(formula);
            SendMessageW(edit,EM_REPLACESEL,TRUE,LPARAM(typed.c_str()));
            require(finishText(true),"commit phosphine through the actual caption editor");
            if(std::string(formula).find('3')!=std::string::npos){
                bool subscript=false;
                std::string full;
                for(const auto& run:s.doc.runs(id)){
                    full+=run.utf8;
                    subscript|=run.utf8=="3"&&(run.flags&kSubscript);
                }
                require(full==formula&&subscript&&s.doc.plainText(id)==formula,
                        "phosphine caption uses separate styled runs including the subscript");
            }
            s.selected={ObjectRef(ObjectKind::Text,id)};
        }else{
            id=atom(point);
            s.refresh();
            openText(kInvalidObjectId,id);
            textEditor.select(0,-1);
            const auto typed=W(formula);
            SendMessageW(edit,EM_REPLACESEL,TRUE,LPARAM(typed.c_str()));
            require(finishText(true)&&s.doc.atomLabel(id)==formula,"type compact phosphine through the atom label editor");
            s.selected={ObjectRef(ObjectKind::Atom,id)};
        }
        s.refresh();
        const auto before=s.doc.digest();
        if(caption){
            switchTab(6);
            RectF button;
            for(const auto& group:visibleGroups())for(const auto& item:group.items)
                if(item.cmd=="@mechanismLonePair")button=item.r;
            require(button.Width>0&&button.X>=0,"lone-pair ribbon button is visible");
            const auto px=MAKELPARAM(int(std::lround((button.X+button.Width*.5)*scale)),
                                    int(std::lround((button.Y+button.Height*.5)*scale)));
            SendMessageW(h,WM_LBUTTONDOWN,MK_LBUTTON,px);
            SendMessageW(h,WM_LBUTTONUP,0,px);
        }else execute(Button("","","","@mechanismLonePair"));
        require(s.selected.size()==1&&s.selected.front().kind==ObjectKind::Atom,"triphenylphosphine becomes an atom-owned annotation");
        const auto phosphorus=s.selected.front();
        require(mechanism::lonePairCount(s.doc,phosphorus.id)==1&&s.doc.atomLabel(phosphorus.id)==formula,
                "triphenylphosphine keeps its spelling and receives one lone pair");
        const auto& visual=arrows::mechanismVisualBoxes(s.doc).at(phosphorus.id);
        const auto host=visual.lonePairHost,pair=(visual.lonePairs.front().min+visual.lonePairs.front().max)*.5;
        const auto label=arrows::mechanismLabelEnvelope(visual);
        require((std::string(formula)[1]=='P'?host.x<(label.min.x+label.max.x)*.5:host.x>(label.min.x+label.max.x)*.5)&&
                std::abs(pair.x-host.x)<1e-6&&pair.y<host.y,"lone pair is above the phosphorus in either label order");
        const auto marked=s.doc.digest();
        execute(Button("","","","@mechanismLonePair"));
        require(s.doc.digest()==marked,"phosphine does not gain a second lone pair");
        auto view=s.view();view.center=host;view.scale=4;view.dpiScale=1;s.setView(view);
        require(beginElectronMarkerDrag(view.toDevice(pair))&&electronDragSite==3,"grab the compact phosphine lone pair");
        updateElectronMarkerDrag(view.toDevice(host+Vec2(60,30)),true);
        const auto moved=arrows::mechanismVisualBoxes(s.doc).at(phosphorus.id).lonePairs.front();
        const auto center=(moved.min+moved.max)*.5;
        require(length(center-pair)>2&&length(center-host)<=14.001,"drag range is measured from the drawn P");
        chooseArrowPreset(49);s.selected.clear();
        auto* tool=dynamic_cast<arrows::ArrowTool*>(s.entries[s.activeIndex].tool.get());
        require(tool&&s.gesture(PointerPhase::Down,center).ok()&&tool->previewParams().electronSource==phosphorus&&
                tool->previewParams().sourceVisualAnchor==3,"a mechanism arrow can start from the phosphine lone pair");
        s.cancel();
        require(s.undo().ok()&&s.doc.digest()==marked&&s.undo().ok()&&s.doc.digest()==before,
                "dragging and adding the phosphine lone pair undo independently");
    }
    // The regular double-bond tool has its own snap resolver. At the user's
    // zoom level, nearly all of the drawn HN/NH is outside the atom-center halo.
    for(bool mirrored:{false,true}){
        reset();
        const double side=mirrored?-1.:1.;
        const auto nitrogen=atom(Vec2(45,55),7),partner=atom(Vec2(45+16*side,55),7);
        auto sem=s.doc.atoms().alive(nitrogen)->semantics;
        sem.implicitHydrogens=1;s.doc.setAtomSemantics(nitrogen,sem);
        bond(nitrogen,partner,BondOrder::Double);bond(partner,atom(Vec2(45+24*side,41)));
        const auto source=atom(Vec2(45-5*side,70));
        s.refresh();
        auto view=s.view();view.center=Vec2(45,55);view.scale=12.37;view.dpiScale=1;s.setView(view);
        require(s.activateCommand("chooseMultipleBondsTool_DoubleBond"),"choose the regular double-bond tool");
        const auto& label=arrows::mechanismVisualBoxes(s.doc).at(nitrogen);
        require(label.labelGlyphs.size()==2,"regular bond test has an HN or NH label");
        auto hydrogen=label.labelGlyphs.front();
        for(const auto& box:label.labelGlyphs)
            if(std::abs((box.min.x+box.max.x)*.5-45)>std::abs((hydrogen.min.x+hydrogen.max.x)*.5-45))hydrogen=box;
        const Vec2 farH=(hydrogen.min+hydrogen.max)*.5;
        const Vec2 farN((label.body.min.x+label.body.max.x)*.5,label.body.min.y+.2);
        const auto center=s.doc.atoms().alive(nitrogen)->position;
        const auto ref=ObjectRef(ObjectKind::Atom,nitrogen);
        require(length(farH-center)>view.deviceRadiusToDocument(s.snapRadiusDevice())&&
                length(farN-center)>view.deviceRadiusToDocument(s.snapRadiusDevice()),
                "visible H and N lie beyond the old nucleus-only halo");
        auto send=[&](UINT message,Vec2 p,WPARAM keys=0){
            const auto px=view.toDevice(p);
            SendMessageW(canvas,message,keys,MAKELPARAM(int(std::lround(px.x)),int(std::lround(px.y))));
        };
        for(auto point:{farH,farN}){
            send(WM_MOUSEMOVE,point);
            render();
            require(s.input.hotspot().object==ref&&s.input.hotspot().point==center,
                    "regular tool hovers on the full label and still reports the original N position");
            send(WM_LBUTTONDOWN,point,MK_LBUTTON);
            require(s.bondOrigin.joined&&s.bondOrigin.target==ref&&s.bondOrigin.point==center,
                    "ordinary bond press starts at the original N even far from its center");
            SendMessageW(canvas,WM_CAPTURECHANGED,0,0);
        }
        const auto sourcePosition=s.doc.atoms().alive(source)->position;
        const auto atomCount=s.doc.stats().aliveAtoms,bondCount=s.doc.stats().aliveBonds;
        send(WM_LBUTTONDOWN,sourcePosition,MK_LBUTTON);
        send(WM_MOUSEMOVE,farH,MK_LBUTTON);render();
        require(s.bondSnap.joined&&s.bondSnap.target==ref&&s.bondSnap.endpoint==center,
                "regular double-bond drag preview snaps to the labeled nitrogen");
        send(WM_LBUTTONUP,farH);
        require(s.doc.stats().aliveAtoms==atomCount&&s.doc.stats().aliveBonds==bondCount+1,
                "regular bond joins the existing N instead of adding a duplicate atom");
        bool linked=false;
        for(auto id:s.doc.incidentBonds(source)){
            const auto* edge=s.doc.bonds().alive(id);
            if(edge->order==BondOrder::Double&&(edge->beginAtom==nitrogen||edge->endAtom==nitrogen))linked=true;
        }
        require(linked,"committed double bond is attached to the correct N");
    }
    // Reproduce the HN=N label and the methyl bond near the right-hand nitrogen.
    for(int preset:{49,57})for(double zoom:{1.,4.,12.}){
        reset();
        const Vec2 vertices[]={{30,0},{15,-26},{-15,-26},{-30,0},{-15,26},{15,26}};
        std::vector<ObjectId> ring;
        for(auto p:vertices)ring.push_back(atom(p));
        std::vector<ObjectId> edges;
        for(size_t i=0;i<6;++i)edges.push_back(bond(ring[i],ring[(i+1)%6],i%2?BondOrder::Single:BondOrder::Double));
        s.refresh();
        auto view=s.view();view.scale=zoom;view.dpiScale=1;view.center=Vec2();s.setView(view);
        chooseArrowPreset(preset);
        auto tool=dynamic_cast<arrows::ArrowTool*>(s.entries[s.activeIndex].tool.get());
        const auto nearVertex=vertices[0]+(vertices[1]-vertices[0])*(view.deviceRadiusToDocument(4.)/length(vertices[1]-vertices[0]));
        const auto atomRef=ObjectRef(ObjectKind::Atom,ring[0]);
        const auto bondRef=ObjectRef(ObjectKind::Bond,edges[4]);
        auto cue=[&](ObjectRef ref){
            for(const auto& item:s.ghost.items())if(item.subject==ref&&item.style.colorRGBA==captureColor(ref))return true;
            return false;
        };
        tool->hoverPreview(s,nearVertex,0);
        require(cue(atomRef),"benzene vertex captures the carbon in blue within four screen pixels");
        const auto start=(vertices[4]+vertices[5])*.5;
        tool->hoverPreview(s,start,0);
        require(cue(bondRef),"benzene bond interior captures the bond in orange");
        require(s.gesture(PointerPhase::Down,start).ok()&&s.gesture(PointerPhase::Move,nearVertex).ok()&&
                cue(atomRef)&&cue(bondRef),"drag preview distinguishes the bond source and carbon target");
        if(preset==49&&zoom==4.){render();screenshot(dir+"/benzene-atom-bond-capture.png");}
        require(s.gesture(PointerPhase::Up,nearVertex).ok(),"commit benzene vertex capture");
        arrows::ArrowParams result;bool valid=false;
        require(arrows::readArrow(s.doc,0,&result,&valid).ok()&&valid&&result.electronSource==bondRef&&
                result.electronTarget==atomRef&&result.targetVisualAnchor==1,
                "committed arrow retains the carbon target and bond source");
    }
    for(int preset:{49,57}){
        reset();
        const auto left=atom(Vec2(40,65),7),right=atom(Vec2(56,65),7);
        auto sem=s.doc.atoms().alive(left)->semantics;sem.implicitHydrogens=1;s.doc.setAtomSemantics(left,sem);
        const auto doubleBond=bond(left,right,BondOrder::Double);
        bond(right,atom(Vec2(64,51)));
        const auto distant=atom(Vec2(81,82));
        const auto leftRef=ObjectRef(ObjectKind::Atom,left),rightRef=ObjectRef(ObjectKind::Atom,right);
        const auto bondRef=ObjectRef(ObjectKind::Bond,doubleBond);
        s.refresh();
        auto view=s.view();view.center=Vec2(60,68);view.scale=8;view.dpiScale=1;s.setView(view);
        chooseArrowPreset(preset);
        auto tool=dynamic_cast<arrows::ArrowTool*>(s.entries[s.activeIndex].tool.get());
        require(tool!=nullptr,"nitrogen fixture uses mechanism tool");
        const auto& boxes=arrows::mechanismVisualBoxes(s.doc);
        const auto& leftBox=boxes.at(left);
        require(leftBox.labelGlyphs.size()==2,"HN exposes both displayed glyphs for capture");
        auto hydrogen=leftBox.labelGlyphs[0];
        for(const auto& glyph:leftBox.labelGlyphs)if(glyph.min.x<hydrogen.min.x)hydrogen=glyph;
        const auto onHydrogen=(hydrogen.min+hydrogen.max)*.5;
        const auto body=boxes.at(right).body;
        const auto nearN=body.max+Vec2(view.deviceRadiusToDocument(11.),-view.deviceRadiusToDocument(1.));
        auto cue=[&](ObjectRef ref,uint32_t color){
            for(const auto& item:s.ghost.items())
                if(item.subject==ref&&item.style.colorRGBA==color&&item.primitive==PreviewPrimitive::Polyline)return true;
            return false;
        };
        tool->hoverPreview(s,onHydrogen,0);require(cue(leftRef,captureColor(leftRef)),"clickable H belongs to the left nitrogen");
        tool->hoverPreview(s,nearN,0);require(cue(rightRef,captureColor(rightRef)),"right N captures eleven pixels from the visible glyph");
        bool named=false;
        for(const auto& item:s.ghost.items())if(item.subject==rightRef&&item.text=="电子来源：原子 N")named=true;
        require(named,"capture names the nitrogen");
        const auto& envelope=arrows::mechanismBondEnvelopes(s.doc).at(doubleBond);
        require(envelope.strokes.size()==2,"both N=N strokes are visible");
        for(const auto& stroke:envelope.strokes){
            tool->hoverPreview(s,(stroke.begin+stroke.end)*.5,0);
            require(cue(bondRef,captureColor(bondRef)),"either actual double-bond stroke still captures the bond");
        }
        tool->hoverPreview(s,nearN+Vec2(8,8),0);
        require(s.ghost.empty(),"nearby empty area stays free");
        tool->hoverPreview(s,nearN,kModAlt);require(s.ghost.empty(),"Alt still bypasses capture");
        const auto leftN=leftBox.body;
        const auto onLeftN=(leftN.min+leftN.max)*.5;
        const auto interGlyph=(hydrogen.max.x+leftN.min.x)*.5;
        const auto spacing=leftN.min.x-hydrogen.max.x;
        require(spacing>0,"left H and N have separate visible glyphs");
        auto zoom=view;zoom.center=onLeftN;zoom.scale=std::max(30.,32./spacing);s.setView(zoom);
        const Vec2 gap(interGlyph,onLeftN.y);
        const Vec2 above(onLeftN.x,leftN.min.y-leftN.height()*.13);
        require(arrows::mechanismBoxDistance(hydrogen,gap)>zoom.deviceRadiusToDocument(12.)&&
                arrows::mechanismBoxDistance(leftN,gap)>zoom.deviceRadiusToDocument(12.),
                "the zoomed HN gap exceeds the old per-glyph capture radius");
        for(const auto point:{onLeftN,gap,above}){
            tool->hoverPreview(s,point,0);
            require(cue(leftRef,captureColor(leftRef)),"left N captures over its glyph, in the HN gap, and near its edge at high zoom");
        }
        require(s.gesture(PointerPhase::Down,gap).ok()&&tool->previewParams().electronSource==leftRef&&
                tool->previewParams().sourceVisualAnchor==1,"clicking inside the HN gap anchors at the left nitrogen");
        s.cancel();
        require(s.gesture(PointerPhase::Down,s.doc.atoms().alive(distant)->position).ok()&&
                s.gesture(PointerPhase::Move,gap).ok()&&cue(leftRef,captureColor(leftRef))&&
                tool->previewParams().electronTarget==leftRef,"dragging into the HN gap targets the left nitrogen");
        s.cancel();s.setView(view);

        const auto untouched=s.doc.digest();
        require(s.gesture(PointerPhase::Down,onHydrogen).ok()&&s.gesture(PointerPhase::Up,onHydrogen).ok()&&
                s.doc.digest()==untouched,"a click on the far end of the HN label does not create an arrow");
        auto start=s.doc.atoms().alive(distant)->position;
        require(s.gesture(PointerPhase::Down,start).ok()&&s.gesture(PointerPhase::Move,nearN).ok()&&
                cue(rightRef,captureColor(rightRef))&&s.gesture(PointerPhase::Up,nearN).ok(),"drag preview and mouse-up capture the same N target");
        arrows::ArrowParams arrow;bool ok=false;
        require(arrows::readArrow(s.doc,0,&arrow,&ok).ok()&&ok&&arrow.electronTarget==rightRef&&arrow.targetVisualAnchor==1,
                "saved arrow binds to the right-hand nitrogen");
        s.selected={ObjectRef(ObjectKind::Arrow,0)};
        require(s.gesture(PointerPhase::Down,arrow.end,kModAlt).ok()&&tool->state().target==0&&
                s.gesture(PointerPhase::Move,onHydrogen).ok()&&s.gesture(PointerPhase::Up,onHydrogen).ok(),
                "edit an existing endpoint through the HN label");
        require(arrows::readArrow(s.doc,0,&arrow,&ok).ok()&&ok&&arrow.electronTarget==leftRef&&arrow.targetVisualAnchor==1,
                "edited arrow binds to the left-hand nitrogen");
        s.selected.clear();
        require(s.gesture(PointerPhase::Down,onHydrogen).ok()&&tool->previewParams().electronSource==leftRef,
                "start a new arrow from the full HN label");
        s.cancel();
    }
    // A typed Nu: can be converted into a drawable anionic nucleophile.
    {
        reset();
        ObjectId caption;require(s.doc.addText(Vec2(29,80),10,"Nu:",&caption).ok(),"typed Nu colon fixture");
        s.selected={ObjectRef(ObjectKind::Text,caption)};
        insertMechanismRole(false);
        require(s.selected.size()==1&&s.selected.front().kind==ObjectKind::Atom,"typed Nu: converts into the anionic symbolic nucleophile");
        auto nu=s.selected.front();
        require(mechanism::genericRole(s.doc,nu.id)=="nucleophile"&&s.doc.atoms().alive(nu.id)->semantics.charge==-1,
                "new Nu carries a negative charge");
        auto carbon=atom(Vec2(65,45));
        for(auto delta:{Vec2(0,-14.4),Vec2(-12.47,7.2),Vec2(12.47,7.2)})bond(carbon,atom(Vec2(65,45)+delta));
        auto sem=s.doc.atoms().alive(carbon)->semantics;sem.charge=1;sem.implicitHydrogens=0;s.doc.setAtomSemantics(carbon,sem);
        const auto carbonRef=ObjectRef(ObjectKind::Atom,carbon);
        s.doc.extensions(carbonRef).set(integration::molecular::electronMarkerKey(false),"0 1.4");
        s.refresh();
        auto view=s.view();view.center=Vec2(52,65);view.scale=4;view.dpiScale=1;s.setView(view);
        chooseArrowPreset(49);s.selected.clear();
        auto tool=dynamic_cast<arrows::ArrowTool*>(s.entries[s.activeIndex].tool.get());
        require(tool!=nullptr,"Nu capture fixture uses mechanism tool");
        const auto& boxes=arrows::mechanismVisualBoxes(s.doc);
        const auto& lonePairs=boxes.at(nu.id).lonePairs;
        require(lonePairs.size()==1&&mechanism::lonePairCount(s.doc,nu.id)==1,"Nu anion displays one real pair");
        const auto colon=lonePairs.front();
        const auto from=Vec2((colon.min.x+colon.max.x)*.5,colon.min.y+.1);
        const auto charge=boxes.at(carbon).charge;
        const auto to=(charge.min+charge.max)*.5;
        tool->hoverPreview(s,from,0);
        bool pairCue=false;
        for(const auto& item:s.ghost.items())if(item.subject==nu&&item.text=="电子来源：孤对电子")pairCue=true;
        require(pairCue,"hover over either colon dot identifies the Nu lone pair");
        require(s.gesture(PointerPhase::Down,from).ok()&&s.gesture(PointerPhase::Move,to).ok()&&s.gesture(PointerPhase::Up,to).ok(),
                "draw from the Nu lone pair to a displaced positive charge");
        arrows::ArrowParams p;bool ok=false;
        require(arrows::readArrow(s.doc,0,&p,&ok).ok()&&ok&&p.electronSource==nu&&p.sourceVisualAnchor==3&&
                p.electronTarget==carbonRef&&p.targetVisualAnchor==2,"Nu pair and positive charge both retain exact bindings");
        require(arrows::mechanismBoxDistance(colon,p.begin)<=2.5&&arrows::mechanismBoxDistance(charge,p.end)<=2.5,
                "endpoints stay beside the two visible symbols");
        std::vector<mechanism::ElectronMove> moves;
        require(mechanism::inferElectronMoves(s.doc,{p},&moves).ok(),"displaced positive charge is accepted by generation");
        s.activateCommand("chooseSelectionTool_Marquee");
        const auto oldCenter=(colon.min+colon.max)*.5;
        const auto host=s.doc.atoms().alive(nu.id)->position;
        const auto desired=host+Vec2(0,80);
        require(beginElectronMarkerDrag(s.view().toDevice(oldCenter))&&electronDragSite==3,
                "grab the Nu lone pair through its rendered dots");
        updateElectronMarkerDrag(s.view().toDevice(desired),true);
        const auto offset=mechanism::lonePairPosition(s.doc,nu.id,0)-host;
        const auto em=integration::molecular::setting(s.doc,"LabelSize",10);
        require(offset.y>0&&std::abs(length(offset)-em*1.4)<1e-5,"lone pair rotates within the allowed radius");
        require(arrows::readArrow(s.doc,0,&p,&ok).ok()&&ok&&
                mechanism::inferElectronMoves(s.doc,{p},&moves).ok(),"attached arrow follows the relocated lone pair");
        for(bool xml:{false,true}){
            auto file=dir+(xml?"/nu-carbon-capture.cdxml":"/nu-carbon-capture.r62doc");
            require(s.save(file,xml).ok(),"save Nu charge capture");
            Session copy;require(copy.open(file).ok(),"reopen Nu charge capture");
            arrows::ArrowParams arrow;bool read=false;
            require(arrows::readArrow(copy.doc,0,&arrow,&read).ok()&&read&&
                    mechanism::inferElectronMoves(copy.doc,{arrow},&moves).ok(),"Nu pair and charge bindings survive reopening");
            const auto id=arrow.electronSource.id;
            const auto* copiedNu=copy.doc.atoms().alive(id);
            require(copiedNu&&length(mechanism::lonePairPosition(copy.doc,id,0)-copiedNu->position-offset)<1e-4,
                    "moved lone pair survives reopening relative to its atom");
        }
        s.selected={ObjectRef(ObjectKind::Arrow,0)};generateMechanismStep();
        ObjectId productNu=kInvalidObjectId;
        for(auto ref:s.selected)if(ref.kind==ObjectKind::Atom&&mechanism::genericRole(s.doc,ref.id)=="nucleophile")productNu=ref.id;
        require(productNu!=kInvalidObjectId&&s.doc.atoms().alive(productNu)->semantics.charge==0&&
                s.doc.incidentBonds(productNu).size()==1,"Nu anion forms one bond and becomes neutral");
        const auto* link=s.doc.bonds().alive(s.doc.incidentBonds(productNu).front());
        const auto partner=link->beginAtom==productNu?link->endAtom:link->beginAtom;
        require(link->order==BondOrder::Single&&s.doc.atoms().alive(partner)->semantics.charge==0,"new Nu–C bond neutralizes the carbocation center");
    }
    // The standard two-electron tool protonates a hydroxyl using the H+ button.
    {
        reset();
        const auto oxygen=atom(Vec2(32,60),8),carbon=atom(Vec2(22,77));
        const auto spectator=atom(Vec2(41,90),8);
        bond(oxygen,carbon);bond(carbon,spectator);
        auto sem=s.doc.atoms().alive(oxygen)->semantics;
        sem.implicitHydrogens=1;s.doc.setAtomSemantics(oxygen,sem);
        s.selected={ObjectRef(ObjectKind::Atom,oxygen)};
        execute(Button("","","","@mechanismLonePair"));
        mechanism::setLonePairOffset(s.doc,oxygen,0,Vec2(0,-.8));
        s.selected={ObjectRef(ObjectKind::Atom,spectator)};
        execute(Button("","","","@mechanismLonePair"));
        auto view=s.view();view.center=Vec2(66,40);view.scale=5;view.dpiScale=1;s.setView(view);
        execute(Button("","","","@mechanismProton"));
        require(s.selected.size()==1&&s.selected.front().kind==ObjectKind::Atom,"proton button inserts an atom");
        const auto proton=s.selected.front();
        const auto* hydrogen=s.doc.atoms().alive(proton.id);
        require(hydrogen&&hydrogen->element==1&&hydrogen->semantics.charge==1&&hydrogen->semantics.implicitHydrogens==0,
                "proton button creates genuine H+ semantics");
        chooseArrowPreset(49);s.selected.clear();
        const auto& visuals=arrows::mechanismVisualBoxes(s.doc);
        require(visuals.at(oxygen).lonePairs.size()==1&&visuals.at(proton.id).hasCharge,"OH pair and H+ sign are visible");
        const auto pair=visuals.at(oxygen).lonePairs.front(),charge=visuals.at(proton.id).charge;
        const auto begin=(pair.min+pair.max)*.5,end=(charge.min+charge.max)*.5;
        const auto obstacles=arrows::mechanismObstacles(s.doc);
        require(visuals.at(oxygen).labelGlyphs.size()==2,"routing receives separate O and H glyphs");
        auto throughHydrogen=arrows::resolvePreset(49,begin,end,.5);
        throughHydrogen.electronSource=ObjectRef(ObjectKind::Atom,oxygen);
        throughHydrogen.sourceVisualAnchor=3;
        throughHydrogen.electronTarget=proton;throughHydrogen.targetVisualAnchor=2;
        const auto hydrogenGlyph=visuals.at(oxygen).labelGlyphs.back();
        const auto glyphCenter=(hydrogenGlyph.min+hydrogenGlyph.max)*.5;
        const auto d=end-begin,n=Vec2(-d.y,d.x),v=glyphCenter-begin;
        throughHydrogen.route={Vec2(0,0),Vec2(dot(v,d)/dot(d,d),dot(v,n)/dot(d,d)),Vec2(1,0)};
        require(arrows::mechanismRouteScore(throughHydrogen,obstacles,true)>0,
                "the donor's attached H is an obstacle even close to the start");
        require(s.gesture(PointerPhase::Down,begin).ok()&&s.gesture(PointerPhase::Move,end).ok()&&
                s.gesture(PointerPhase::Up,end).ok(),"draw the standard mechanism arrow from OH to H+");
        arrows::ArrowParams arrow;bool present=false;
        require(arrows::readArrow(s.doc,0,&arrow,&present).ok()&&present&&arrow.head.sides==arrows::HeadSides::Both&&
                arrow.electronTarget==proton&&arrow.targetVisualAnchor==2,"full arrow binds to the H+ sign");
        require(arrows::mechanismRouteScore(arrow,obstacles,true)==0,
                "automatically drawn shaft clears both OH and proton glyphs");
        std::vector<mechanism::ElectronMove> moves;
        require(mechanism::inferElectronMoves(s.doc,{arrow},&moves).ok()&&moves.front().sourceLonePair==0,
                "OH pair to H+ carries the visual pair index into the reaction");
        Document twoPairs=s.doc;
        twoPairs.extensions(ObjectRef(ObjectKind::Atom,oxygen)).set(mechanism::lonePairKey(),"2");
        const Vec2 remainingOffset(-.8,-.1);
        mechanism::setLonePairOffset(twoPairs,oxygen,1,remainingOffset);
        std::vector<ObjectRef> created;
        require(mechanism::generateNextStep(twoPairs,moves,&created).ok(),"generate with two authored donor pairs");
        bool remaining=false;
        for(auto ref:created)if(ref.kind==ObjectKind::Atom&&twoPairs.atoms().alive(ref.id)->element==8&&
            twoPairs.atoms().alive(ref.id)->semantics.charge==1){
            const auto offsets=mechanism::lonePairOffsets(twoPairs,ref.id);
            remaining=mechanism::lonePairCount(twoPairs,ref.id)==1&&offsets.count(0)&&
                length(offsets.at(0)-remainingOffset)<1e-6;
        }
        require(remaining,"consume only the selected pair and retain the other pair's authored position");
        auto blank=arrow;blank.end=Vec2(180,160);blank.electronTarget=ObjectRef();blank.targetVisualAnchor=0;
        const auto invalid=mechanism::inferElectronMoves(s.doc,{blank},&moves);
        require(!invalid.ok()&&invalid.message.find("双电子")!=std::string::npos&&invalid.message.find("鱼钩")==std::string::npos,
                "unbound full arrow gets the correct message");
        s.selected={ObjectRef(ObjectKind::Arrow,0)};generateMechanismStep();
        ObjectId newO=kInvalidObjectId,newH=kInvalidObjectId,otherO=kInvalidObjectId;
        for(auto ref:s.selected)if(ref.kind==ObjectKind::Atom){
            const auto* a=s.doc.atoms().alive(ref.id);
            if(a->element==8&&a->semantics.charge==1&&a->semantics.implicitHydrogens==1)newO=ref.id;
            if(a->element==1&&a->semantics.charge==0)newH=ref.id;
            if(a->element==8&&a->semantics.charge==0)otherO=ref.id;
        }
        require(newO!=kInvalidObjectId&&newH!=kInvalidObjectId,"protonation produces oxonium and neutral bonded hydrogen");
        bool linked=false;
        for(auto id:s.doc.incidentBonds(newO)){
            const auto* b=s.doc.bonds().alive(id);
            linked|=b->order==BondOrder::Single&&(b->beginAtom==newH||b->endAtom==newH);
        }
        require(linked,"protonation creates the O–H single bond");
        require(mechanism::lonePairCount(s.doc,oxygen)==1&&mechanism::lonePairCount(s.doc,newO)==0,
                "the authored pair remains in the reactant and is absent on the product donor");
        require(otherO!=kInvalidObjectId&&mechanism::lonePairCount(s.doc,otherO)==1,
                "the other OH retains its unreacted lone pair");
    }
    for(int sign:{-1,1}){
        reset();const auto center=atom(Vec2(20,30));
        const auto ref=ObjectRef(ObjectKind::Atom,center);
        s.selected={ref};
        const auto command=sign>0?"@mechanismPositive":"@mechanismNegative";
        execute(Button("","","",command,1));
        const auto before=s.doc.atoms().alive(center)->semantics;
        require(before.charge==sign&&integration::molecular::circledElectronCharge(s.doc,center),"circled charge keeps its chemical sign");
        bool rendered=false;
        for(const auto& primitive:s.scene.primitives())
            if(primitive.source==ref&&primitive.kind==render::PrimKind::Polyline&&primitive.points.size()==49&&
                primitive.style.stroke.enabled&&!primitive.style.fill.enabled)rendered=true;
        require(rendered,"circled charge renders a hollow outline");
        execute(Button("","","",command));
        const auto after=s.doc.atoms().alive(center)->semantics;
        require(after.charge==sign&&after.implicitHydrogens==before.implicitHydrogens&&
                !integration::molecular::circledElectronCharge(s.doc,center),"switching to the plain sign preserves the hydrogen count");
    }
    // A selected nearby arrow must not steal real bond ink or a radical dot.
    for(int site:{5,4}){
        reset();auto oldView=s.view();auto testView=oldView;testView.scale=1.33;testView.dpiScale=1;s.setView(testView);
        auto a=atom(Vec2(0,0)),b=atom(Vec2(40,0));ObjectId edge=kInvalidObjectId;Vec2 ink;
        if(site==5){edge=bond(a,b);require(arrows::mechanismBondContact(s.doc,edge,Vec2(20,0),&ink),"selected handle fixture has visible bond ink");}
        else{auto sem=s.doc.atoms().get(a)->semantics;sem.radical=uint8_t(Radical::Doublet);sem.implicitHydrogens=3;s.doc.setAtomSemantics(a,sem);ink=arrows::mechanismRadicalPoints(s.doc,a).front()+Vec2(0,arrows::mechanismRadicalRadius(s.doc));}
        auto p=arrows::resolvePreset(49,ink+Vec2(0,site==5?1.4:.65),Vec2(40,40),.6);p.mechanism=true;p.sourceVisualAnchor=uint8_t(site);p.electronSource=ObjectRef(site==5?ObjectKind::Bond:ObjectKind::Atom,site==5?edge:a);ObjectId arrow;
        require(arrows::addArrow(s.doc,p,.6,&arrow).ok(),"add selected arrow beside real ink");s.refresh();s.activateCommand("chooseArrowTool_90_CW");s.selected={ObjectRef(ObjectKind::Arrow,arrow)};
        auto tool=dynamic_cast<arrows::ArrowTool*>(s.entries[s.activeIndex].tool.get());require(tool!=nullptr,"selected ink test uses arrow tool");auto before=s.doc.digest();tool->hoverPreview(s,ink,0);bool captured=false;for(const auto& item:s.ghost.items())captured|=item.style.colorRGBA==(site==5?0xE67E00FFu:0x0078D4FFu);
        require(captured&&s.doc.digest()==before,"real ink hover wins over nearby selected endpoint without editing");require(s.gesture(PointerPhase::Down,ink).ok()&&tool->state().target==kInvalidObjectId&&tool->previewParams().sourceVisualAnchor==site,"real ink press starts new arrow rather than editing nearby handle");s.cancel();
        s.selected={ObjectRef(ObjectKind::Arrow,arrow)};tool->hoverPreview(s,p.begin,0);require(s.ghost.items().empty(),"exact handle outside ink has no false source preview");require(s.gesture(PointerPhase::Down,p.begin).ok()&&tool->state().target==arrow,"exact handle outside ink still edits selected arrow");s.cancel();s.setView(oldView);
    }
    // Optional full-page visual regression, preserving the user's chemistry.
    if(std::ifstream(dir+"/beauty-input.r62doc").good()){
        require(s.open(dir+"/beauty-input.r62doc").ok(),"open real full mechanism for beauty regression");
        s.selected.clear();s.refresh();s.fit();invalidate();screenshot(dir+"/beauty-before.png");
        auto original=s.doc;std::vector<ObjectId> ids;
        for(const auto& record:s.doc.arrows().records())if(record.isAlive()){arrows::ArrowParams p;bool ok=false;
            if(arrows::readArrow(s.doc,record.id,&p,&ok).ok()&&ok&&p.mechanism)ids.push_back(record.id);}
        Document routing=s.doc;for(auto id:ids)routing.removeObject(ObjectRef(ObjectKind::Arrow,id));
        std::ofstream evidence(dir+"/beauty-geometry.csv");evidence<<"id,bound,shaft_length,head_length\n";
        for(auto id:ids){arrows::ArrowParams p;bool ok=false;arrows::readArrow(original,id,&p,&ok);auto source=p.electronSource,target=p.electronTarget;
            Vec2 from,to;bool bound=arrows::mechanismAnchorPoint(original,source,&from)&&arrows::mechanismAnchorPoint(original,target,&to);
            if(bound){if(source.kind==ObjectKind::Bond)p.begin=from;p.end=to;p.route.clear();arrows::routeMechanism(routing,p,true);arrows::clearMechanismLabels(routing,p);}
            else {p.head.length=2.8;p.head.width=1.95;p.strokeWidth=.5;}
            require(p.electronSource==source&&p.electronTarget==target,"beauty preserves source and target semantics");
            arrows::ArrowGeometry g;require(arrows::buildArrowGeometry(p,s.doc.defaultLineWidth(),arrows::Tessellation(),&g).ok(),"beauty geometry valid");
            double shaft=0;for(const auto& line:g.shafts)for(size_t j=1;j<line.size();++j)shaft+=length(line[j]-line[j-1]);
            evidence<<id<<","<<bound<<","<<shaft<<","<<p.head.length<<"\n";
            require(arrows::writeArrow(s.doc,id,p,s.doc.defaultLineWidth()).ok(),"write beauty arrow only");
        }
        require(s.doc.atoms().size()==original.atoms().size()&&s.doc.bonds().size()==original.bonds().size(),"beauty retains every chemical object");
        s.refresh();s.selected.clear();s.fit();invalidate();screenshot(dir+"/beauty-after.png");require(s.save(dir+"/beauty-editable.cdxml",true).ok(),"save editable full beauty mechanism");
        auto fitView=s.view();for(size_t i=0;i<ids.size();++i){arrows::ArrowParams p;bool ok=false;arrows::readArrow(s.doc,ids[i],&p,&ok);if(length(p.end-p.begin)>22)continue;
            auto view=fitView;view.center=(p.begin+p.end)*.5;view.scale=8.;s.setView(view);invalidate();screenshot(dir+"/beauty-detail-"+std::to_string(ids[i])+".png");}
        s.setView(fitView);s.dirty=false;PostMessageW(h,WM_CLOSE,0,0);return;
    }
    if(GetFileAttributesW(W(dir+"/aldol-input.r62doc").c_str())!=INVALID_FILE_ATTRIBUTES){
        require(s.open(dir+"/aldol-input.r62doc").ok(),"open actual aldol failure drawing");
        auto before=s.doc.digest();auto oldCount=s.doc.stats().aliveAtoms;s.selected={ObjectRef(ObjectKind::Arrow,12),ObjectRef(ObjectKind::Arrow,13),ObjectRef(ObjectKind::Arrow,14)};
        generateMechanismStep();std::ofstream(dir+"/actual-aldol-status.txt")<<status;require(s.doc.stats().aliveAtoms==oldCount+8,"actual aldol adds eight product atoms");
        std::vector<ObjectId> aa;int bb=0,totalCharge=0,totalH=0;for(auto r:s.selected)if(r.kind==ObjectKind::Atom){aa.push_back(r.id);auto a=s.doc.atoms().get(r.id);totalCharge+=a->semantics.charge;totalH+=H(r.id);}else if(r.kind==ObjectKind::Bond)++bb;
        require(aa.size()==8&&bb==7&&totalCharge==-1&&totalH==11,"actual aldol product conserves graph charge and hydrogens");
        auto order=[&](int i,int j){for(auto bid:s.doc.incidentBonds(aa[i])){auto b=s.doc.bonds().get(bid);if(b->beginAtom==aa[j]||b->endAtom==aa[j])return int(b->order);}return 0;};
        require(order(0,2)==2&&order(0,3)==1&&order(3,4)==1&&order(4,6)==1&&s.doc.atoms().get(aa[6])->semantics.charge==-1,"actual aldol reforms carbonyl consumes pi and opens attacked carbonyl");
        arrows::ArrowParams p;bool ok=false;arrows::readArrow(s.doc,13,&p,&ok);require(ok&&p.electronSource==ObjectRef(ObjectKind::Atom,10),"actual atom-start pi arrow retains specified bonding carbon");
        auto after=s.doc.digest();require(s.undo().ok()&&s.doc.digest()==before,"actual aldol one undo restores source");require(s.undo(true).ok()&&s.doc.digest()==after,"actual aldol redo");require(s.save(dir+"/actual-aldol-result.cdxml",true).ok(),"save actual aldol result");s.selected.clear();s.fit();invalidate();screenshot(dir+"/actual-aldol-result.png");
    }
    if(GetFileAttributesW(W(dir+"/anchor-input.r62doc").c_str())!=INVALID_FILE_ATTRIBUTES){
        require(s.open(dir+"/anchor-input.r62doc").ok(),"open actual charge-anchor failure drawing");
        s.selected={ObjectRef(ObjectKind::Arrow,4),ObjectRef(ObjectKind::Arrow,6),ObjectRef(ObjectKind::Arrow,7)};std::vector<arrows::ArrowParams> params;for(auto ref:s.selected){arrows::ArrowParams p;bool ok=false;require(arrows::readArrow(s.doc,ref.id,&p,&ok).ok()&&ok,"read actual charge-anchor arrow");params.push_back(p);}
        std::vector<mechanism::ElectronMove> moves;require(mechanism::inferElectronMoves(s.doc,params,&moves).ok(),"actual superscript charge click remains valid for inference");
        {auto baseline=moves;for(int site:{1,2}){auto variant=params;variant[0].sourceVisualAnchor=uint8_t(site);std::vector<mechanism::ElectronMove> equivalent;require(mechanism::inferElectronMoves(s.doc,variant,&equivalent).ok()&&equivalent.size()==baseline.size(),"atom and charge site yield same complete aldol electron moves");for(size_t k=0;k<baseline.size();++k)require(equivalent[k].source==baseline[k].source&&equivalent[k].target==baseline[k].target&&equivalent[k].electronCount==baseline[k].electronCount,"visual site never changes chemical electron source or target");}}

        {auto displaced=params;displaced[0].begin=displaced[0].begin+Vec2(60,60);auto digest=s.doc.digest();require(!mechanism::inferElectronMoves(s.doc,displaced,&moves).ok()&&s.doc.digest()==digest,"moving visible endpoint far from bound charge rejects");}
        {auto shifted=s.doc;auto a=shifted.atoms().get(9);shifted.setAtomPosition(9,a->position+Vec2(60,60));require(!mechanism::inferElectronMoves(shifted,params,&moves).ok(),"moving bound atom away rejects stale association");}
        auto before=s.doc.digest();auto count=s.doc.stats().aliveAtoms;generateMechanismStep();std::ofstream(dir+"/anchor-user-status.txt")<<status;require(s.doc.stats().aliveAtoms==count+8,"actual charge-anchor arrows generate aldol product");int charge=0,hydrogen=0,bonds=0;for(auto ref:s.selected)if(ref.kind==ObjectKind::Atom){charge+=s.doc.atoms().get(ref.id)->semantics.charge;hydrogen+=H(ref.id);}else if(ref.kind==ObjectKind::Bond)++bonds;require(charge==-1&&hydrogen==11&&bonds==7,"charge-anchor product conserves charge hydrogens and graph");
        auto after=s.doc.digest();require(s.undo().ok()&&s.doc.digest()==before,"charge-anchor product single undo");require(s.undo(true).ok()&&s.doc.digest()==after,"charge-anchor product redo");require(s.save(dir+"/anchor-user-result.cdxml",true).ok(),"save actual charge-anchor product");s.selected.clear();s.fit();invalidate();screenshot(dir+"/anchor-user-result.png");
    }
    if(GetFileAttributesW(W(dir+"/oxygen-input.r62doc").c_str())!=INVALID_FILE_ATTRIBUTES){
        require(s.open(dir+"/oxygen-input.r62doc").ok(),"open actual oxygen start displacement drawing");require(s.doc.atoms().alive(3)&&s.doc.bonds().alive(2),"actual oxygen gesture fixture semantic objects");
        require(s.doc.removeObject(ObjectRef(ObjectKind::Arrow,0)).ok(),"remove old arrow only in regression copy");s.refresh();s.fit();chooseArrowPreset(49);auto box=arrows::mechanismAtomBodyBox(s.doc,*s.doc.atoms().get(3));auto from=(box.min+box.max)*.5;Vec2 to;require(arrows::mechanismAnchorPoint(s.doc,ObjectRef(ObjectKind::Bond,2),&to),"actual oxygen target bond anchor");
        require(s.gesture(PointerPhase::Down,from).ok()&&s.gesture(PointerPhase::Move,to).ok()&&s.gesture(PointerPhase::Up,to).ok(),"replay actual oxygen glyph to adjacent bond gesture");arrows::ArrowParams p;bool ok=false;for(const auto& a:s.doc.arrows().records())if(a.isAlive())arrows::readArrow(s.doc,a.id,&p,&ok);
        require(ok&&p.sourceVisualAnchor==1&&p.electronSource==ObjectRef(ObjectKind::Atom,3)&&p.electronTarget==ObjectRef(ObjectKind::Bond,2),"actual oxygen capture stays atom glyph with adjacent bond target");double dx=std::max(0.,std::max(box.min.x-p.begin.x,p.begin.x-box.max.x)),dy=std::max(0.,std::max(box.min.y-p.begin.y,p.begin.y-box.max.y));require(std::sqrt(dx*dx+dy*dy)<=2,"actual oxygen source remains close to selected O glyph");
        require(s.save(dir+"/oxygen-site-result.cdxml",true).ok(),"save actual oxygen visual-site replay");s.selected.clear();s.fit();invalidate();screenshot(dir+"/oxygen-site-result.png");
    }
    // Visual atom/charge targets remain distinct while sharing atom chemistry.
    for(int sign:{-1,1})for(int sourceSite:{1,2})for(int targetSite:{1,2}){
        reset();auto source=atom(Vec2(80,80),8),target=atom(Vec2(150,110),8);for(auto id:{source,target}){auto sem=s.doc.atoms().get(id)->semantics;sem.charge=id==source?sign:-sign;s.doc.setAtomSemantics(id,sem);}s.refresh();s.fit();chooseArrowPreset(49);
        auto sb=sourceSite==1?arrows::mechanismAtomBodyBox(s.doc,*s.doc.atoms().get(source)):arrows::mechanismChargeBox(s.doc,*s.doc.atoms().get(source));
        auto tb=targetSite==1?arrows::mechanismAtomBodyBox(s.doc,*s.doc.atoms().get(target)):arrows::mechanismChargeBox(s.doc,*s.doc.atoms().get(target));auto from=(sb.min+sb.max)*.5,to=(tb.min+tb.max)*.5;
        require(s.gesture(PointerPhase::Down,from).ok()&&s.gesture(PointerPhase::Move,to).ok()&&s.gesture(PointerPhase::Up,to).ok(),"draw independent atom or signed-charge capture sites");arrows::ArrowParams p;bool ok=false;require(arrows::readArrow(s.doc,0,&p,&ok).ok()&&ok,"read visual-site gesture arrow");
        require(p.electronSource==ObjectRef(ObjectKind::Atom,source)&&p.electronTarget==ObjectRef(ObjectKind::Atom,target)&&p.sourceVisualAnchor==sourceSite&&p.targetVisualAnchor==targetSite,"both endpoints retain selected atom versus charge visual site");
        auto gap=[](Vec2 point,Box box){double dx=std::max(0.,std::max(box.min.x-point.x,point.x-box.max.x)),dy=std::max(0.,std::max(box.min.y-point.y,point.y-box.max.y));return std::sqrt(dx*dx+dy*dy);};
        require(gap(p.begin,sb)<=2&&gap(p.end,tb)<=2,"visual endpoints stay beside the selected glyphs");
        auto reverse=arrows::reversedArrow(p);require(reverse.sourceVisualAnchor==targetSite&&reverse.targetVisualAnchor==sourceSite,"reversal swaps both visual capture sites");
        if(sourceSite==2&&targetSite==1){
            for(bool xml:{false,true}){auto path=dir+"/visual-sites-"+std::to_string(sign)+(xml?".cdxml":".r62doc");require(s.save(path,xml).ok(),"save independent visual sites");Session reopened;require(reopened.open(path).ok(),"reopen independent visual sites");arrows::ArrowParams q;bool read=false;require(arrows::readArrow(reopened.doc,0,&q,&read).ok()&&read&&q.sourceVisualAnchor==sourceSite&&q.targetVisualAnchor==targetSite&&q.electronSource.valid()&&q.electronTarget.valid(),"native and CDXML retain visual sites and semantic bindings");
                reopened.activateCommand("chooseArrowTool_90_CW");reopened.selected={ObjectRef(ObjectKind::Arrow,0)};
                for(int side:{0,1})for(int variant:{0,1}){int site=side?(variant?1:2):(variant?2:1);arrows::ArrowParams before;bool valid=false;require(arrows::readArrow(reopened.doc,0,&before,&valid).ok()&&valid,"read saved arrow before endpoint handle drag");auto ref=side?before.electronTarget:before.electronSource;auto a=reopened.doc.atoms().alive(ref.id);require(a!=nullptr,"saved arrow endpoint atom exists");auto glyph=site==1?arrows::mechanismAtomBodyBox(reopened.doc,*a):arrows::mechanismChargeBox(reopened.doc,*a);auto destination=(glyph.min+glyph.max)*.5;auto handles=arrows::arrowHandles(before,reopened.doc.defaultLineWidth(),arrows::Tessellation());Vec2 grab;bool found=false;for(const auto& handle:handles)if(handle.kind==(side?arrows::HandleKind::End:arrows::HandleKind::Begin)){grab=handle.point;found=true;}require(found,"saved arrow exposes actual endpoint handle");auto beforeEdit=reopened.doc.digest();
                    require(reopened.gesture(PointerPhase::Down,grab).ok()&&reopened.gesture(PointerPhase::Move,destination).ok()&&reopened.gesture(PointerPhase::Up,destination).ok(),"drag existing saved arrow handle to atom or charge glyph");arrows::ArrowParams after;valid=false;int liveArrows=0;for(const auto& record:reopened.doc.arrows().records())if(record.isAlive())++liveArrows;require(arrows::readArrow(reopened.doc,0,&after,&valid).ok()&&valid&&liveArrows==1,"handle drag edits existing arrow without adding one");require(after.electronSource==before.electronSource&&after.electronTarget==before.electronTarget&&(side?after.targetVisualAnchor:after.sourceVisualAnchor)==site,"saved arrow handle drag retains host and changes requested visual site");require((side?after.sourceVisualAnchor:after.targetVisualAnchor)==(side?before.sourceVisualAnchor:before.targetVisualAnchor),"endpoint edit preserves opposite visual site");require(gap(side?after.end:after.begin,glyph)<=2,"saved arrow edited endpoint clears selected glyph without drifting");auto afterEdit=reopened.doc.digest();require(reopened.undo().ok()&&reopened.doc.digest()==beforeEdit,"saved arrow endpoint edit undo");require(reopened.undo(true).ok()&&reopened.doc.digest()==afterEdit,"saved arrow endpoint edit redo");
                }
}
            Document copy;std::vector<ObjectRef> inserted;require(studioio::append(s.doc,copy,s.all(),Vec2(40,30),&inserted).ok(),"copy atoms and visual-site arrow");arrows::ArrowParams q;bool read=false;require(arrows::readArrow(copy,0,&q,&read).ok()&&read&&q.sourceVisualAnchor==sourceSite&&q.targetVisualAnchor==targetSite&&copy.atoms().alive(q.electronSource.id)&&copy.atoms().alive(q.electronTarget.id),"copied visual sites retain remapped semantic atoms");
        }
    }
    for(int sign:{-1,1}){
        reset();auto oxygen=atom(Vec2(90,80),8),carbon=atom(Vec2(90,94.4));auto bondId=bond(oxygen,carbon);auto sem=s.doc.atoms().get(oxygen)->semantics;sem.charge=sign;s.doc.setAtomSemantics(oxygen,sem);s.refresh();s.fit();chooseArrowPreset(49);
        Vec2 from;require(arrows::mechanismAnchorPoint(s.doc,ObjectRef(ObjectKind::Bond,bondId),&from),"local bond source exists");auto box=arrows::mechanismChargeBox(s.doc,*s.doc.atoms().get(oxygen));auto to=(box.min+box.max)*.5;
        require(s.gesture(PointerPhase::Down,from).ok()&&s.gesture(PointerPhase::Move,to).ok()&&s.gesture(PointerPhase::Up,to).ok(),"draw local bond to its charged atom sign");arrows::ArrowParams p;bool ok=false;require(arrows::readArrow(s.doc,0,&p,&ok).ok()&&ok&&p.electronSource==ObjectRef(ObjectKind::Bond,bondId)&&p.electronTarget==ObjectRef(ObjectKind::Atom,oxygen)&&p.targetVisualAnchor==2,"local bond return retains positive or negative charge target site");
        double dx=std::max(0.,std::max(box.min.x-p.end.x,p.end.x-box.max.x)),dy=std::max(0.,std::max(box.min.y-p.end.y,p.end.y-box.max.y));require(std::sqrt(dx*dx+dy*dy)<=2,"local bond target remains beside selected charge rather than atom body");
    }
    if(GetFileAttributesW(W(dir+"/base-capture-input.r62doc").c_str())!=INVALID_FILE_ATTRIBUTES){
        require(s.open(dir+"/base-capture-input.r62doc").ok(),"open actual B minus and carbonyl crossing drawing");require(mechanism::genericRole(s.doc,6)=="base"&&s.doc.atoms().get(6)->semantics.charge==-1&&s.doc.atoms().get(5)->element==1,"actual B minus and explicit hydrogen identities");
        s.refresh();s.fit();chooseArrowPreset(49);s.selected={ObjectRef(ObjectKind::Arrow,0)};auto originalArrowText=*static_cast<const Document&>(s.doc).extensions(ObjectRef(ObjectKind::Arrow,0)).findValue("arc.arrow.params.1");auto oldArrowSlots=s.doc.arrows().size();auto chargeGlyph=arrows::mechanismChargeBox(s.doc,*s.doc.atoms().get(6)),hGlyph=arrows::mechanismAtomBodyBox(s.doc,*s.doc.atoms().get(5));auto fromCharge=(chargeGlyph.min+chargeGlyph.max)*.5,toHydrogen=(hGlyph.min+hGlyph.max)*.5;
        require(s.gesture(PointerPhase::Down,fromCharge).ok()&&s.gesture(PointerPhase::Move,toHydrogen).ok()&&s.gesture(PointerPhase::Up,toHydrogen).ok(),"draw actual B minus while previous arrow remains selected");require(s.doc.arrows().size()==oldArrowSlots+1&&*static_cast<const Document&>(s.doc).extensions(ObjectRef(ObjectKind::Arrow,0)).findValue("arc.arrow.params.1")==originalArrowText,"B sign starts new arrow without altering selected old arrow");arrows::ArrowParams selectedReplay;bool replayOK=false;require(arrows::readArrow(s.doc,ObjectId(oldArrowSlots),&selectedReplay,&replayOK).ok()&&replayOK&&selectedReplay.sourceVisualAnchor==2&&selectedReplay.electronSource==ObjectRef(ObjectKind::Atom,6)&&selectedReplay.electronTarget==ObjectRef(ObjectKind::Atom,5),"selected-arrow B sign replay retains B donor rather than reversing to H");require(s.doc.removeObject(ObjectRef(ObjectKind::Arrow,ObjectId(oldArrowSlots))).ok(),"remove replay duplicate only in regression copy");s.selected.clear();
        require(s.doc.removeObject(ObjectRef(ObjectKind::Arrow,0)).ok()&&s.doc.removeObject(ObjectRef(ObjectKind::Arrow,2)).ok(),"remove two old arrows only in actual regression copy");s.refresh();s.fit();chooseArrowPreset(49);
        auto baseBox=arrows::mechanismChargeBox(s.doc,*s.doc.atoms().get(6)),hydrogenBox=arrows::mechanismAtomBodyBox(s.doc,*s.doc.atoms().get(5));auto begin=(baseBox.min+baseBox.max)*.5,end=(hydrogenBox.min+hydrogenBox.max)*.5;
        auto draw=[&](Vec2 from,Vec2 to){s.selected.clear();require(s.gesture(PointerPhase::Down,from).ok()&&s.gesture(PointerPhase::Move,to).ok()&&s.gesture(PointerPhase::Up,to).ok(),"replay actual reagent or carbonyl arrow gesture");ObjectId result=kInvalidObjectId;for(const auto& a:s.doc.arrows().records())if(a.isAlive())result=a.id;return result;};
        auto baseArrowId=draw(begin,end);arrows::ArrowParams bp;bool ok=false;require(arrows::readArrow(s.doc,baseArrowId,&bp,&ok).ok()&&ok&&bp.sourceVisualAnchor==2&&bp.electronSource==ObjectRef(ObjectKind::Atom,6)&&bp.electronTarget==ObjectRef(ObjectKind::Atom,5),"actual B minus sign captures base atom and attacks explicit H");
        Vec2 bondMidpoint;require(arrows::mechanismAnchorPoint(s.doc,ObjectRef(ObjectKind::Bond,1),&bondMidpoint),"actual carbonyl double bond exists");auto oxygenBox=arrows::mechanismAtomBodyBox(s.doc,*s.doc.atoms().get(2));auto carbonylArrowId=draw(bondMidpoint,(oxygenBox.min+oxygenBox.max)*.5);arrows::ArrowParams cp;ok=false;require(arrows::readArrow(s.doc,carbonylArrowId,&cp,&ok).ok()&&ok&&cp.electronSource==ObjectRef(ObjectKind::Bond,1)&&cp.electronTarget==ObjectRef(ObjectKind::Atom,2),"carbonyl return keeps exact double bond and oxygen references");
        s.refresh();arrows::ArrowGeometry curve;require(arrows::buildArrowGeometry(cp,s.doc.defaultLineWidth(),arrows::Tessellation(),&curve).ok()&&!curve.shafts.empty(),"actual carbonyl arrow shaft geometry");double inkGap=1e30;int strokeSegments=0;
        for(const auto& primitive:s.scene.primitives())if(primitive.source==ObjectRef(ObjectKind::Bond,1)&&primitive.style.stroke.enabled&&primitive.points.size()>=2){for(size_t j=1;j<primitive.points.size();++j){++strokeSegments;auto a=primitive.points[j-1],b=primitive.points[j];for(const auto& shaft:curve.shafts)for(size_t k=1;k<shaft.size();++k){auto c=shaft[k-1],d=shaft[k];double distance=mechanism::step_detail::segmentsCross(a,b,c,d)?0:std::min(std::min(mechanism::step_detail::pointSegment(a,c,d),mechanism::step_detail::pointSegment(b,c,d)),std::min(mechanism::step_detail::pointSegment(c,a,b),mechanism::step_detail::pointSegment(d,a,b)));inkGap=std::min(inkGap,distance-(primitive.style.strokeWidth+curve.strokeWidth)*.5);}}}
        std::ofstream(dir+"/base-carbonyl-clearance.txt")<<"rendered bond stroke segments="<<strokeSegments<<" minimum net ink gap="<<inkGap;require(strokeSegments>=2&&inkGap>=.05,"actual carbonyl shaft clears both rendered double-bond strokes");
        for(bool xml:{false,true}){auto path=dir+(xml?"/base-capture-replay.cdxml":"/base-capture-replay.r62doc");require(s.save(path,xml).ok(),"save actual B charge gesture");Session reopened;require(reopened.open(path).ok(),"reopen actual B charge gesture");bool retained=false;for(const auto& a:reopened.doc.arrows().records())if(a.isAlive()){arrows::ArrowParams p;bool read=false;arrows::readArrow(reopened.doc,a.id,&p,&read);if(read&&p.sourceVisualAnchor==2&&p.electronSource.kind==ObjectKind::Atom&&mechanism::genericRole(reopened.doc,p.electronSource.id)=="base"){auto h=reopened.doc.atoms().alive(p.electronTarget.id);retained=h&&h->element==1;}}require(retained,"actual B sign capture survives native and CDXML");}
        auto before=s.doc.digest();auto count=s.doc.stats().aliveAtoms;s.selected={ObjectRef(ObjectKind::Arrow,baseArrowId),ObjectRef(ObjectKind::Arrow,1),ObjectRef(ObjectKind::Arrow,carbonylArrowId)};generateMechanismStep();std::ofstream(dir+"/base-capture-generation.txt")<<status;require(s.doc.stats().aliveAtoms==count+7,"actual B minus first step generates seven product atoms");int charge=0,hydrogen=0,bonds=0;bool neutralBase=false;for(auto r:s.selected)if(r.kind==ObjectKind::Atom){auto a=s.doc.atoms().get(r.id);charge+=a->semantics.charge;if(mechanism::genericRole(s.doc,r.id)=="base")neutralBase=a->semantics.charge==0&&s.doc.incidentBonds(r.id).size()==1;else hydrogen+=a->element==1?1:H(r.id);}else if(r.kind==ObjectKind::Bond)++bonds;require(charge==-1&&hydrogen==8&&bonds==5&&neutralBase,"actual B first step conserves charge and forms BH plus enolate");auto after=s.doc.digest();require(s.undo().ok()&&s.doc.digest()==before,"actual B first step one undo");require(s.undo(true).ok()&&s.doc.digest()==after,"actual B first step redo");require(s.save(dir+"/base-capture-product.cdxml",true).ok(),"save actual B first step result");s.selected.clear();s.fit();invalidate();screenshot(dir+"/base-capture-product.png");
    }
    // Idle capture must use real canvas mouse messages without creating a gesture.
    reset();auto hoverBase=atom(Vec2(70,70));require(mechanism::setGenericRole(s.doc,hoverBase,"base").ok(),"hover fixture uses actual generic B minus");auto hoverO=atom(Vec2(140,70),8),hoverC=atom(Vec2(140,100));auto hoverBond=bond(hoverO,hoverC);s.refresh();s.fit();chooseArrowPreset(49);s.selected={ObjectRef(ObjectKind::Atom,hoverBase)};
    auto hoverDigest=s.doc.digest();auto hoverSelection=s.selected;auto hoverEntries=s.history.entries(),hoverCursor=s.history.cursor();
    auto hasCapture=[&](ObjectRef ref){bool semantic=!ref.valid();for(const auto& item:s.ghost.items())if(item.subject==ref&&matchesCaptureColor(ref,item.style.colorRGBA)&&item.points.size()>8)semantic=true;auto overlay=s.overlay();for(const auto& p:overlay.scene().primitives())if(p.style.stroke.enabled&&matchesCaptureColor(ref,p.style.stroke.rgba)&&p.points.size()>8)return semantic;return false;};
    auto sendHover=[&](Vec2 world){auto point=s.view().toDevice(world);SendMessageW(canvas,WM_MOUSEMOVE,0,MAKELPARAM(int(std::lround(point.x)),int(std::lround(point.y))));render();};
    auto baseCharge=arrows::mechanismChargeBox(s.doc,*s.doc.atoms().get(hoverBase));auto baseChargePoint=(baseCharge.min+baseCharge.max)*.5;auto oxygenBody=arrows::mechanismAtomBodyBox(s.doc,*s.doc.atoms().get(hoverO));Vec2 hoverBondPoint;require(arrows::mechanismAnchorPoint(s.doc,ObjectRef(ObjectKind::Bond,hoverBond),&hoverBondPoint),"hover bond center exists");
    for(int site=0;site<3;++site){auto point=site==0?baseChargePoint:site==1?(oxygenBody.min+oxygenBody.max)*.5:hoverBondPoint;auto ref=ObjectRef(site==2?ObjectKind::Bond:ObjectKind::Atom,site==0?hoverBase:site==1?hoverO:hoverBond);sendHover(point);require(hasCapture(ref),site==0?"idle B minus sign hover displays capture circle":site==1?"idle atom glyph hover displays capture circle":"idle bond hover displays capture circle");require(s.doc.digest()==hoverDigest&&s.selected==hoverSelection&&s.history.entries()==hoverEntries&&s.history.cursor()==hoverCursor,"idle capture leaves document selection and history unchanged");}
    SendMessageW(canvas,WM_MOUSEMOVE,0,MAKELPARAM(1,1));render();require(!hasCapture(ObjectRef()),"idle capture circle clears over blank canvas");sendHover(baseChargePoint);require(hasCapture(ObjectRef(ObjectKind::Atom,hoverBase)),"idle capture returns when hovering B charge again");screenshot(dir+"/idle-B-charge-capture.png");SendMessageW(canvas,WM_MOUSELEAVE,0,0);render();require(!hasCapture(ObjectRef()),"idle capture clears on mouse leave");sendHover(baseChargePoint);require(hasCapture(ObjectRef(ObjectKind::Atom,hoverBase)),"idle B capture returns before switching tool");s.activateCommand("chooseSelectionTool_Marquee");render();require(!hasCapture(ObjectRef()),"switching away from mechanism tool clears idle capture");require(s.doc.arrows().size()==0,"hover without pointer down never creates an arrow");require(s.doc.digest()==hoverDigest&&s.selected==hoverSelection&&s.history.entries()==hoverEntries&&s.history.cursor()==hoverCursor,"hover departure leaves document selection and history unchanged");
    auto hoverCircleAt=[&](ObjectRef ref,Vec2 expected){sendHover(expected);require(hasCapture(ref),"v18 idle circle belongs to exact atom or bond");bool centered=false;for(const auto& item:s.ghost.items())if(item.subject==ref&&matchesCaptureColor(ref,item.style.colorRGBA)&&item.points.size()>8){Vec2 mean;size_t n=item.points.size();if(length(item.points.front()-item.points.back())<1e-6)--n;for(size_t k=0;k<n;++k)mean=mean+item.points[k];mean=mean*(1./n);if(length(mean-expected)<=s.view().deviceRadiusToDocument(1.5)+.05)centered=true;}require(centered,"v18 hover circle follows visible dot or noncentral bond ink");};
    for(int mode=0;mode<3;++mode){
        reset();auto a=atom(Vec2(80,80)),b=atom(Vec2(120,80)),target=atom(Vec2(165,120),7);auto sourceBond=bond(a,b,mode?BondOrder::Double:BondOrder::Single);s.refresh();s.fit();chooseArrowPreset(49);std::vector<std::pair<Vec2,Vec2>> strokes;for(const auto& primitive:s.scene.primitives())if(primitive.source==ObjectRef(ObjectKind::Bond,sourceBond)&&primitive.style.stroke.enabled&&primitive.points.size()>=2)strokes.push_back({primitive.points.front(),primitive.points.back()});require(strokes.size()>size_t(mode==2?1:0),"actual rendered single or both double strokes available");auto stroke=strokes[mode==2?1:0];auto point=stroke.first*.69+stroke.second*.31;auto digest=s.doc.digest();auto selected=s.selected;auto cursor=s.history.cursor();hoverCircleAt(ObjectRef(ObjectKind::Bond,sourceBond),point);require(s.doc.digest()==digest&&s.selected==selected&&s.history.cursor()==cursor,"noncentral bond hover is read only");
        auto targetBox=arrows::mechanismAtomBodyBox(s.doc,*s.doc.atoms().get(target));auto end=(targetBox.min+targetBox.max)*.5;require(s.gesture(PointerPhase::Down,point).ok()&&s.gesture(PointerPhase::Move,end).ok()&&s.gesture(PointerPhase::Up,end).ok(),"draw from actual noncentral single or double bond stroke");arrows::ArrowParams p;bool ok=false;require(arrows::readArrow(s.doc,0,&p,&ok).ok()&&ok&&p.electronSource==ObjectRef(ObjectKind::Bond,sourceBond)&&p.sourceVisualAnchor==5,"noncentral bond ink gesture stores bond semantics and visual site");auto axis=stroke.second-stroke.first;axis=axis*(1./length(axis));auto delta=p.begin-point;require(std::abs(delta.x*axis.x+delta.y*axis.y)<=3,"noncentral bond start does not jump to bond midpoint");
    }
    for(int element:{8,6}){
        reset();auto center=atom(Vec2(90,90),element),neighbour=atom(Vec2(90,120)),target=atom(Vec2(170,90),6);bond(center,neighbour);require(s.submit(chemistry::setRadicalCommand({center},int(Radical::Doublet)),"radical hover fixture").ok(),"create oxygen or skeletal carbon radical");s.refresh();s.fit();chooseArrowPreset(57);auto dots=arrows::mechanismRadicalPoints(s.doc,center);require(dots.size()==1,"actual radical dot position exists");auto digest=s.doc.digest();auto selected=s.selected;auto cursor=s.history.cursor();hoverCircleAt(ObjectRef(ObjectKind::Atom,center),dots[0]);require(s.doc.digest()==digest&&s.selected==selected&&s.history.cursor()==cursor,"radical dot hover is read only");auto end=s.doc.atoms().get(target)->position;require(s.gesture(PointerPhase::Down,dots[0]).ok()&&s.gesture(PointerPhase::Move,end).ok()&&s.gesture(PointerPhase::Up,end).ok(),"fishhook begins at actual radical dot");arrows::ArrowParams p;bool ok=false;require(arrows::readArrow(s.doc,0,&p,&ok).ok()&&ok&&p.sourceVisualAnchor==4&&p.electronSource==ObjectRef(ObjectKind::Atom,center)&&p.head.sides!=arrows::HeadSides::Both,"radical fishhook keeps single electron and host atom");require(length(p.begin-dots[0])<=arrows::mechanismRadicalRadius(s.doc)+2,"radical fishhook begins beside visible dot");auto reverse=arrows::reversedArrow(p);require(reverse.targetVisualAnchor==4&&reverse.electronTarget==p.electronSource,"radical visual site reverses with its semantic host");
        for(bool xml:{false,true}){auto path=dir+"/radical-site-"+std::to_string(element)+(xml?".cdxml":".r62doc");require(s.save(path,xml).ok(),"save radical visual capture");Session reopened;require(reopened.open(path).ok(),"reopen radical visual capture");arrows::ArrowParams q;bool read=false;require(arrows::readArrow(reopened.doc,0,&q,&read).ok()&&read&&q.sourceVisualAnchor==4&&reopened.doc.atoms().alive(q.electronSource.id)->semantics.radical==uint8_t(Radical::Doublet),"native and CDXML keep radical site and electron state");}
        {Document copy;std::vector<ObjectRef> added;require(studioio::append(s.doc,copy,s.all(),Vec2(40,30),&added).ok(),"copy radical visual capture");arrows::ArrowParams q;bool read=false;require(arrows::readArrow(copy,0,&q,&read).ok()&&read&&q.sourceVisualAnchor==4&&copy.atoms().alive(q.electronSource.id)->semantics.radical==uint8_t(Radical::Doublet),"copy keeps remapped radical site");}
    }
    reset();auto radicalA=atom(Vec2(80,80)),radicalB=atom(Vec2(140,80));require(s.submit(chemistry::setRadicalCommand({radicalA,radicalB},int(Radical::Doublet)),"radical coupling fixture").ok(),"create two real radical donors");s.refresh();s.fit();chooseArrowPreset(57);
    for(int side=0;side<2;++side){s.selected.clear();auto from=arrows::mechanismRadicalPoints(s.doc,side?radicalB:radicalA).front(),to=arrows::mechanismRadicalPoints(s.doc,side?radicalA:radicalB).front();require(s.gesture(PointerPhase::Down,from).ok()&&s.gesture(PointerPhase::Move,to).ok()&&s.gesture(PointerPhase::Up,to).ok(),"draw paired actual radical-dot fishhooks");}
    std::vector<arrows::ArrowParams> radicalParams;for(const auto& a:s.doc.arrows().records())if(a.isAlive()){arrows::ArrowParams p;bool ok=false;arrows::readArrow(s.doc,a.id,&p,&ok);require(ok&&p.sourceVisualAnchor==4&&p.targetVisualAnchor==4,"paired radical source and target capture exact dots");radicalParams.push_back(p);}std::vector<mechanism::ElectronMove> radicalMoves;require(mechanism::inferElectronMoves(s.doc,radicalParams,&radicalMoves).ok(),"radical visual sites infer single electron moves");Document coupled=s.doc;std::vector<ObjectRef> radicalProducts;require(mechanism::generateNextStep(coupled,radicalMoves,&radicalProducts).ok(),"paired dot-origin fishhooks form editable chemical bond");int pairedAtoms=0,pairedBonds=0,pairedHydrogens=0;for(auto ref:radicalProducts)if(ref.kind==ObjectKind::Atom){auto a=coupled.atoms().get(ref.id);++pairedAtoms;pairedHydrogens+=a->semantics.implicitHydrogens;require(!a->semantics.radical&&!a->semantics.charge,"radical coupling consumes unpaired electrons without changing total charge");}else if(ref.kind==ObjectKind::Bond)++pairedBonds;require(pairedAtoms==2&&pairedBonds==1&&pairedHydrogens==6,"paired radical product is ethane graph");
    for(bool xml:{false,true}){auto path=dir+(xml?"/radical-coupling.cdxml":"/radical-coupling.r62doc");require(s.save(path,xml).ok(),"save complete radical-dot coupling");Session reopened;require(reopened.open(path).ok(),"reopen complete radical-dot coupling");std::vector<arrows::ArrowParams> pp;for(const auto& a:reopened.doc.arrows().records())if(a.isAlive()){arrows::ArrowParams p;bool ok=false;arrows::readArrow(reopened.doc,a.id,&p,&ok);require(ok&&p.sourceVisualAnchor==4&&p.targetVisualAnchor==4,"reopened coupling retains both radical visual sites");pp.push_back(p);}std::vector<mechanism::ElectronMove> mm;require(mechanism::inferElectronMoves(reopened.doc,pp,&mm).ok(),"saved radical dots still infer valid chemistry");std::vector<ObjectRef> products;require(mechanism::generateNextStep(reopened.doc,mm,&products).ok(),"saved radical-dot arrows still form chemical bond");}

    // The screenshot uses two bulky radicals, with both hooks aimed at an
    // empty bond site rather than at the opposite radical.
    {
        reset();
        std::vector<ObjectId> centers;
        for(int side=0;side<2;++side){
            const auto origin=side?Vec2(160,100):Vec2(45,125);
            auto center=atom(origin);
            for(auto delta:{Vec2(0,-30),Vec2(-26,15),Vec2(26,15)})
                bond(center,atom(origin+delta*(side?-1:1)));
            auto sem=s.doc.atoms().alive(center)->semantics;sem.implicitHydrogens=0;sem.radical=uint8_t(Radical::Doublet);
            s.doc.setAtomSemantics(center,sem);centers.push_back(center);
        }
        s.refresh();s.fit();chooseArrowPreset(57);
        for(size_t i=0;i<centers.size();++i){
            auto dot=arrows::mechanismRadicalPoints(s.doc,centers[i]).front();
            auto tip=Vec2(99,69-5*i);
            require(s.gesture(PointerPhase::Down,dot).ok()&&s.gesture(PointerPhase::Move,tip).ok()&&s.gesture(PointerPhase::Up,tip).ok(),
                    "draw tert-butyl fishhook into blank space");
        }
        std::vector<arrows::ArrowParams> inputs;
        auto readInputs=[&](){inputs.clear();for(const auto& a:s.doc.arrows().records())if(a.isAlive()){
            arrows::ArrowParams p;bool ok=false;require(arrows::readArrow(s.doc,a.id,&p,&ok).ok()&&ok,"read free-tip arrow");
            if(p.mechanism)inputs.push_back(p);
        }};
        readInputs();
        std::vector<mechanism::ElectronMove> moves;
        require(inputs.size()==2&&!inputs[0].electronTarget.valid()&&!inputs[1].electronTarget.valid(),"two visibly free arrowheads");
        require(!mechanism::inferElectronMoves(s.doc,{inputs.front()},&moves).ok(),"a lone free fishhook cannot invent a partner");
        auto inference=mechanism::inferElectronMoves(s.doc,inputs,&moves);
        if(!inference.ok())throw std::runtime_error("free fishhooks: "+inference.message);
        require(moves.size()==2&&moves[0].sharedTarget&&moves[1].sharedTarget,
                "nearby free fishhooks infer one new radical coupling bond");
        s.selected=s.all();generateMechanismStep();
        int bonds=0,atoms=0,h=0;for(auto ref:s.selected){
            if(ref.kind==ObjectKind::Bond)++bonds;
            if(ref.kind==ObjectKind::Atom){++atoms;auto sem=s.doc.atoms().alive(ref.id)->semantics;h+=H(ref.id);require(!sem.radical,"two tertiary radicals are consumed");}
        }
        if(atoms!=8||bonds!=7||h!=18)throw std::runtime_error("C8H18 product A="+std::to_string(atoms)+" B="+std::to_string(bonds)+" H="+std::to_string(h)+" "+status);
        require(true,"two tert-butyl radicals form one C8H18 product");
        readInputs();
        require(!inputs[0].electronTarget.valid()&&!inputs[1].electronTarget.valid()&&mechanism::inferElectronMoves(s.doc,inputs,&moves).ok(),
                "generation keeps the meeting arrowheads free and reusable");
    }
    // v11: ribbon charge commands require an atom selection; hover + targets
    // the actual pointed atom, independently of a previous selection.
    reset();auto hoverCarbon=atom(Vec2(90,90)),otherCarbon=atom(Vec2(180,90));
    auto unchanged=s.doc.digest();execute(Button("","","","@mechanismPositive"));require(s.doc.digest()==unchanged,"charge ribbon without selection is inert");
    s.selected={ObjectRef(ObjectKind::Atom,otherCarbon)};execute(Button("","","","@mechanismNegative"));
    require(s.doc.atoms().get(otherCarbon)->semantics.charge==-1&&H(otherCarbon)==3,"negative ribbon creates CH3 anion");
    s.refresh();auto hoverPixel=s.view().toDevice(s.doc.atoms().get(hoverCarbon)->position);SendMessageW(canvas,WM_MOUSEMOVE,0,MAKELPARAM(int(hoverPixel.x),int(hoverPixel.y)));
    auto hoverBefore=s.doc.digest();require(shortcut(VK_OEM_PLUS,kModShift),"hover plus handled");
    require(s.doc.atoms().get(hoverCarbon)->semantics.charge==1&&H(hoverCarbon)==3&&s.doc.atoms().get(otherCarbon)->semantics.charge==-1,"hover plus creates carbocation without editing selected neighbour");
    auto hoverAfter=s.doc.digest();require(s.undo().ok()&&s.doc.digest()==hoverBefore,"hover charge undo");require(s.undo(true).ok()&&s.doc.digest()==hoverAfter,"hover charge redo");
    s.selected={ObjectRef(ObjectKind::Atom,otherCarbon)};execute(Button("","","","@mechanismPositive"));require(s.doc.atoms().get(otherCarbon)->semantics.charge==1,"positive ribbon sets positive charge");
    for(const auto& mode:std::vector<std::pair<const char*,int>>{
        {"@mechanismNegative",-1},{"@mechanismPositive",1},{"@mechanismRadical",2}}){
        reset();
        auto center=atom(Vec2(100,100));
        for(auto point:{Vec2(100,70),Vec2(74,115),Vec2(126,115)})bond(center,atom(point));
        auto hydrogen=atom(Vec2(74,85),1);bond(center,hydrogen);
        const auto host=ObjectRef(ObjectKind::Atom,center);
        s.selected={host};
        auto before=s.doc.digest();
        execute(Button("","","",mode.first));
        require(!s.doc.atoms().alive(hydrogen)&&s.doc.stats().aliveBonds==3&&H(center)==0,"mechanism insertion removes one explicit H and its bond");
        const bool radical=mode.second==2;
        auto sem=s.doc.atoms().alive(center)->semantics;
        require(radical?sem.radical==uint8_t(Radical::Doublet):sem.charge==mode.second,"inserted marker owns atom semantics");
        require(s.undo().ok()&&s.doc.digest()==before,"undo restores explicit H and its bond");
        require(s.undo(true).ok(),"redo removes explicit H");
        s.fit();
        auto position=[&](const Document& doc,ObjectId id){
            if(radical)return arrows::mechanismRadicalPoints(doc,id).at(0);
            auto box=arrows::mechanismChargeBox(doc,*doc.atoms().alive(id));
            return (box.min+box.max)*.5;
        };
        auto from=position(s.doc,center);
        auto point=[&](Vec2 p){auto px=s.view().toDevice(p);return MAKELPARAM(int(std::lround(px.x)),int(std::lround(px.y)));};
        auto farPoint=point(Vec2(10,100));
        const auto beforeDrag=s.doc.digest();
        SendMessageW(canvas,WM_LBUTTONDOWN,MK_LBUTTON,point(from));
        require(electronDragAtom==center,"selection tool grabs the actual attached symbol");
        SendMessageW(canvas,WM_MOUSEMOVE,MK_LBUTTON,farPoint);
        require(s.doc.digest()==beforeDrag,"drag previews without editing document");
        SendMessageW(canvas,WM_LBUTTONUP,0,farPoint);
        const auto offset=position(s.doc,center)-Vec2(100,100);
        require(offset.x<0&&length(offset)<=14.001&&s.doc.atoms().alive(center)->position==Vec2(100,100),"drag can orbit but stops close to its unchanged host");
        for(bool xml:{false,true}){
            auto file=dir+"/marker-"+std::to_string(mode.second)+(xml?".cdxml":".r62doc");
            require(s.save(file,xml).ok(),"save attached marker");
            Session reopened;require(reopened.open(file).ok(),"reopen attached marker");
            for(const auto& a:reopened.doc.atoms().records())if(a.isAlive()&&(a.semantics.charge||a.semantics.radical))
                require(length((position(reopened.doc,a.id)-a.position)-offset)<.001,"marker offset survives native and CDXML roundtrip");
        }
    }
    {
        reset();auto center=atom(Vec2(80,80)),target=atom(Vec2(130,80));
        auto sem=s.doc.atoms().alive(center)->semantics;sem.radical=uint8_t(Radical::Doublet);sem.implicitHydrogens=3;
        s.doc.setAtomSemantics(center,sem);
        auto key=integration::molecular::electronMarkerKey(true);
        s.doc.extensions(ObjectRef(ObjectKind::Atom,center)).set(key,"1.1 -0.6");
        auto from=arrows::mechanismRadicalPoints(s.doc,center).front();
        auto arrow=arrows::resolvePreset(57,from,Vec2(130,80),.6);
        arrow.mechanism=true;arrow.electronSource=ObjectRef(ObjectKind::Atom,center);arrow.sourceVisualAnchor=4;
        arrow.electronTarget=ObjectRef(ObjectKind::Atom,target);
        ObjectId aid;require(arrows::addArrow(s.doc,arrow,.6,&aid).ok(),"add attached arrow before copy");
        Document pasted;
        const auto shift=Vec2(35,50);
        require(studioio::append(s.doc,pasted,s.all(),shift,nullptr).ok(),"copy atom and bound arrow");
        require(length(arrows::mechanismRadicalPoints(pasted,center).front()-from-shift)<1e-6,"copy retains the radical's relative location");
        // Older clipboard copies are missing this attribute but still have
        // atom semantics. They must be immediately draggable too.
        pasted.extensions(ObjectRef(ObjectKind::Atom,center)).erase(key);
        s.doc=std::move(pasted);s.input.bindDocument(s.doc.id());s.refresh();s.fit();
        const auto dot=arrows::mechanismRadicalPoints(s.doc,center).front();
        auto where=[&](Vec2 p){auto px=s.view().toDevice(p);return MAKELPARAM(int(std::lround(px.x)),int(std::lround(px.y)));};
        SendMessageW(canvas,WM_LBUTTONDOWN,MK_LBUTTON,where(dot));
        require(electronDragAtom==center,"previously copied radical is draggable without a placement attribute");
        SendMessageW(canvas,WM_LBUTTONUP,0,where(dot+Vec2(-35,10)));
        auto after=arrows::mechanismRadicalPoints(s.doc,center).front();
        require(length(after-dot)>3&&length(after-s.doc.atoms().alive(center)->position)<=14.001,"copied radical remains bounded and mobile");
        arrows::ArrowParams edited;bool ok=false;
        require(arrows::readArrow(s.doc,aid,&edited,&ok).ok()&&ok&&length(edited.begin-after)<=arrows::mechanismRadicalRadius(s.doc)+1.001,
                "moving a copied radical updates the bound fishhook");
    }
    reset();auto oxygenWithPair=atom(Vec2(90,90),8);s.selected={ObjectRef(ObjectKind::Atom,oxygenWithPair)};auto pairBefore=s.doc.digest();
    execute(Button("","","","@mechanismLonePair"));auto pairAfter=s.doc.digest();require(pairAfter!=pairBefore,"lone pair ribbon adds semantic display state");
    require(s.doc.atoms().get(oxygenWithPair)->semantics.charge==0,"lone pair ribbon preserves formal charge");
    require(s.undo().ok()&&s.doc.digest()==pairBefore,"lone pair button undo");require(s.undo(true).ok()&&s.doc.digest()==pairAfter,"lone pair button redo");
    require(mechanism::lonePairCount(s.doc,oxygenWithPair)==1,"one lone pair belongs to oxygen");
    for(bool xml:{false,true}){auto path=dir+(xml?"/lone-pair.cdxml":"/lone-pair.r62doc");require(s.save(path,xml).ok(),"save lone pair semantic state");Session reopened;require(reopened.open(path).ok(),"reopen lone pair semantic state");int pairs=0;for(const auto& a:reopened.doc.atoms().records())if(a.isAlive())pairs+=mechanism::lonePairCount(reopened.doc,a.id);require(pairs==1,"native and CDXML retain attached lone pair");}
    {Document copied;std::vector<ObjectRef> inserted;require(studioio::append(s.doc,copied,{ObjectRef(ObjectKind::Atom,oxygenWithPair)},Vec2(40,30),&inserted).ok(),"copy atom with lone pair");require(inserted.size()==1&&mechanism::lonePairCount(copied,inserted[0].id)==1,"copied lone pair remains on remapped atom");auto original=mechanism::lonePairPosition(s.doc,oxygenWithPair,0),moved=mechanism::lonePairPosition(copied,inserted[0].id,0);require(length(moved-original-Vec2(40,30))<1e-7,"copied lone pair position follows translated atom");}

    switchTab(6);s.fit();screenshot(dir+"/mechanism-electron-buttons.png");
    reset();auto captureO=atom(Vec2(90,90),8),captureC=atom(Vec2(135,110)),captureD=atom(Vec2(165,110));auto captureBond=bond(captureC,captureD);
    require(s.submit(chemistry::setChargeCommand({captureO},-1),"capture oxygen").ok(),"capture O minus fixture");s.refresh();s.fit();chooseArrowPreset(49);
    auto oldGraphicsColor=s.graphicsColor;s.graphicsColor=0xFF8000FFu;auto captureChargeBox=arrows::mechanismChargeBox(s.doc,*s.doc.atoms().get(captureO));Vec2 electronPress=(captureChargeBox.min+captureChargeBox.max)*.5,captureTarget(150,110);
    require(s.gesture(PointerPhase::Down,electronPress).ok()&&s.gesture(PointerPhase::Move,captureTarget).ok(),"drag from oxygen charge to bond");
    auto feedback=s.overlay();bool blueCapture=false,orangeCapture=false;
    for(const auto& primitive:feedback.scene().primitives()){blueCapture|=primitive.style.stroke.rgba==0x0078D4FFu;orangeCapture|=primitive.style.stroke.rgba==0xE67E00FFu;}
    require(blueCapture&&orangeCapture,(std::string("capture colors survive custom graphics color; source=")+(blueCapture?"yes":"no")+" target="+(orangeCapture?"yes":"no")+" tool="+s.entries[s.activeIndex].command+" status="+s.status).c_str());screenshot(dir+"/capture-feedback.png");
    require(s.gesture(PointerPhase::Up,captureTarget).ok(),"commit captured electron arrow");arrows::ArrowParams captureArrow;bool captureArrowOK=false;
    for(const auto& record:s.doc.arrows().records())if(record.isAlive())arrows::readArrow(s.doc,record.id,&captureArrow,&captureArrowOK);
    require(captureArrowOK&&captureArrow.electronSource==ObjectRef(ObjectKind::Atom,captureO)&&captureArrow.electronTarget==ObjectRef(ObjectKind::Bond,captureBond),"charge-origin arrow preserves explicit atom and bond bindings");
    double captureGapX=std::max(0.,std::max(captureChargeBox.min.x-captureArrow.begin.x,captureArrow.begin.x-captureChargeBox.max.x)),captureGapY=std::max(0.,std::max(captureChargeBox.min.y-captureArrow.begin.y,captureArrow.begin.y-captureChargeBox.max.y));require(captureArrow.sourceVisualAnchor==2&&std::sqrt(captureGapX*captureGapX+captureGapY*captureGapY)<=1.8,"arrow keeps charge-origin glyph clearance within 1.8 points");s.graphicsColor=oldGraphicsColor;
    if(GetFileAttributesW(W(dir+"/short-input.r62doc").c_str())!=INVALID_FILE_ATTRIBUTES){
        require(s.open(dir+"/short-input.r62doc").ok(),"open actual user short arrow native drawing");
        std::ofstream log(dir+"/short-user-diagnostic.txt");
        for(const auto& a:s.doc.atoms().records())if(a.isAlive())log<<"atom "<<a.id<<" z="<<a.element<<" q="<<int(a.semantics.charge)<<" h="<<int(a.semantics.implicitHydrogens)<<" label="<<s.doc.atomLabel(a.id)<<" role="<<mechanism::genericRole(s.doc,a.id)<<" xy="<<a.position.x<<","<<a.position.y<<"\n";
        for(const auto& b:s.doc.bonds().records())if(b.isAlive())log<<"bond "<<b.id<<" "<<b.beginAtom<<"-"<<b.endAtom<<" order="<<int(b.order)<<"\n";
        // IDs identify only this saved regression fixture, never production inference.
        s.selected={ObjectRef(ObjectKind::Arrow,9),ObjectRef(ObjectKind::Arrow,10)};
        std::vector<arrows::ArrowParams> pp;for(auto r:s.selected){arrows::ArrowParams p;bool ok=false;require(arrows::readArrow(s.doc,r.id,&p,&ok).ok()&&ok,"read actual short arrow pair");pp.push_back(p);}
        Document prepared;std::vector<mechanism::ElectronMove> mm;std::vector<ObjectRef> made;
        auto e=studio_mechanism::prepareProduct(s.doc,s.selected,pp,&prepared,&mm,&made);log<<"prepare: "<<e.message<<"\n";log.flush();require(e.ok(),"actual short arrow pair prepares proton transfer");
        auto before=s.doc.digest();auto count=s.doc.stats().aliveAtoms;generateMechanismStep();log<<"generation: "<<status<<"\n";log.flush();require(s.doc.stats().aliveAtoms>count,"actual short arrow generates editable products");
        auto after=s.doc.digest();require(s.undo().ok()&&s.doc.digest()==before,"actual short arrow result one undo");require(s.undo(true).ok()&&s.doc.digest()==after,"actual short arrow result redo");
        require(s.save(dir+"/short-user-result.cdxml",true).ok(),"save actual short arrow result");s.selected.clear();s.fit();invalidate();screenshot(dir+"/short-user-result.png");
        for(size_t i=0;i<mm.size();++i){auto p=pp[i];p.electronSource=mm[i].source;p.electronTarget=mm[i].target;Vec2 a,z;require(arrows::mechanismAnchorPoint(s.doc,p.electronSource,&a)&&arrows::mechanismAnchorPoint(s.doc,p.electronTarget,&z),"actual short arrow anchors resolve");Document routing=s.doc;auto id=ObjectId(9+i);routing.removeObject(ObjectRef(ObjectKind::Arrow,id));p.begin=a;p.end=z;p.route.clear();arrows::routeMechanism(routing,p,true);arrows::clearMechanismLabels(routing,p);require(p.electronSource==mm[i].source&&p.electronTarget==mm[i].target,"actual smoothing retains electron semantics");if(i==1){arrows::ArrowGeometry geometry;require(arrows::buildArrowGeometry(p,.6,arrows::Tessellation(),&geometry).ok()&&!geometry.shafts.empty(),"actual short arrow geometry exists");double curveLength=0;for(size_t j=1;j<geometry.shafts[0].size();++j)curveLength+=core::length(geometry.shafts[0][j]-geometry.shafts[0][j-1]);require(curveLength>=6,"actual short arrow retains readable curved shaft");}require(arrows::writeArrow(s.doc,id,p,s.doc.defaultLineWidth()).ok(),"smooth actual short arrow demonstration");}
        s.refresh();s.selected.clear();s.fit();invalidate();require(s.save(dir+"/short-user-smoothed.cdxml",true).ok(),"save separate smoothed actual drawing");screenshot(dir+"/short-user-smoothed.png");
    }
    // Real keyboard route for caption -> reagent -> explicit B-H.
    reset();ObjectId caption;require(s.doc.addText(Vec2(120,120),12,"B",&caption).ok(),"B caption fixture");s.selected={ObjectRef(ObjectKind::Text,caption)};
    auto textDigest=s.doc.digest();require(shortcut(VK_OEM_MINUS,0),"selected B caption minus handled");auto baseId=s.selected.front().id;
    require(mechanism::genericRole(s.doc,baseId)=="base"&&s.doc.atoms().get(baseId)->semantics.charge==-1,"B caption becomes chemical B minus");auto baseDigest=s.doc.digest();
    require(shortcut(VK_OEM_MINUS,0)&&shortcut(VK_OEM_MINUS,0)&&s.doc.digest()==baseDigest,"repeated B minus is idempotent and adds no undo entry");
    require(s.undo().ok()&&s.doc.digest()==textDigest,"B caption conversion undo");require(s.undo(true).ok()&&s.doc.digest()==baseDigest,"B caption conversion redo");
    s.refresh();auto pixel=s.view().toDevice(s.doc.atoms().get(baseId)->position);SendMessageW(canvas,WM_MOUSEMOVE,0,MAKELPARAM(int(pixel.x),int(pixel.y)));
    require(shortcut('H',0),"hover B hydrogen shortcut handled");require(s.doc.incidentBonds(baseId).size()==1&&s.doc.atoms().get(baseId)->semantics.charge==0,"B H creates actual bond and neutral base");auto bhDigest=s.doc.digest();
    require(s.undo().ok()&&s.doc.digest()==baseDigest,"B H undo");require(s.undo(true).ok()&&s.doc.digest()==bhDigest,"B H redo");
    execute(Button("","","","@mechanismFishhook"));require(s.entries[s.activeIndex].command=="chooseArrowTool_MechanismFishhook","independent mechanism fishhook activated");
    auto hook=arrows::resolvePreset(57,Vec2(0,0),Vec2(30,0),.6);require(hook.head.sides==arrows::HeadSides::Right,"mechanism fishhook defaults to requested right barb");
    hook.mechanism=true;
    for(double bend:{-.4,.4}){
        hook.arcSagitta=bend*30;
        arrows::ArrowGeometry geometry;
        require(arrows::buildArrowGeometry(hook,.6,arrows::Tessellation(),&geometry).ok()&&geometry.heads.size()==1,
                "fishhook geometry for either bend");
        const auto& head=geometry.heads.front();
        const auto tangent=core::direction(head.angleDeg);
        const auto normal=Vec2(tangent.y,-tangent.x);
        require(head.filled&&head.outline.size()==3&&dot(head.outline[1]-hook.end,normal)<0,
                "right fishhook stays on the selected side for either bend");
        auto mirrored=hook;mirrored.head.sides=arrows::HeadSides::Left;arrows::ArrowGeometry leftGeometry;require(arrows::buildArrowGeometry(mirrored,.6,arrows::Tessellation(),&leftGeometry).ok()&&dot(leftGeometry.heads.front().outline[1]-mirrored.end,normal)>0,"explicit left fishhook is respected without curvature flipping");
        const auto& shaft=geometry.shafts.front();double maxTurn=0;
        for(size_t i=2;i<shaft.size();++i){auto a=shaft[i-1]-shaft[i-2],b=shaft[i]-shaft[i-1];double la=length(a),lb=length(b);if(la>1e-8&&lb>1e-8)maxTurn=std::max(maxTurn,std::acos(std::max(-1.,std::min(1.,dot(a,b)/(la*lb)))));}
        require(maxTurn<.18,"fishhook curve and terminal neck have no visible angular join");
    }
    reset();for(int i=0;i<4;++i){auto p=arrows::resolvePreset(57,Vec2(60+i*36,65),Vec2(82+i*36,48),.6);p.mechanism=true;p.arcSagitta=(i%2?1:-1)*12;p.head.length=2.8;p.head.width=1.95;ObjectId id;require(arrows::addArrow(s.doc,p,.6,&id).ok(),"smooth fishhook demonstration");}s.refresh();s.fit();screenshot(dir+"/smooth-fishhooks.png");
    switchTab(6);s.selected.clear();s.fit();screenshot(dir+"/mechanism-ui.png");require(s.save(dir+"/mechanism-ui.cdxml",true).ok(),"save keyboard B H result");
    // H on the left of B: the short return arrow describes heterolysis,
    // while alkoxide attacks the same hydrogen. Check core and routed inference.
    for(bool bind:{true,false}){
        Document d;ObjectId oxygen,carbon,h,base,bh,oc;
        d.addAtom(Vec2(30,70),8,&oxygen);d.addAtom(Vec2(30,100),6,&carbon);d.addBond(oxygen,carbon,&oc);
        auto os=d.atoms().get(oxygen)->semantics;os.charge=-1;d.setAtomSemantics(oxygen,os);
        d.addAtom(Vec2(80,30),1,&h);d.addAtom(Vec2(110,30),6,&base);require(mechanism::setGenericRole(d,base,"base").ok(),"reversed BH role");
        auto bs=d.atoms().get(base)->semantics;bs.charge=0;d.setAtomSemantics(base,bs);d.addBond(h,base,&bh);
        std::vector<arrows::ArrowParams> pp;
        for(auto move:std::vector<mechanism::ElectronMove>{{ObjectRef(ObjectKind::Atom,oxygen),ObjectRef(ObjectKind::Atom,h)},{ObjectRef(ObjectKind::Bond,bh),ObjectRef(ObjectKind::Atom,base)}}){
            Vec2 a,z;arrows::mechanismAnchorPoint(d,move.source,&a);arrows::mechanismAnchorPoint(d,move.target,&z);auto p=arrows::resolvePreset(49,a,z,.6);p.mechanism=true;p.electronSource=move.source;p.electronTarget=move.target;arrows::routeMechanism(d,p,true);arrows::clearMechanismLabels(d,p);
            if(!bind){p.electronSource=ObjectRef();p.electronTarget=ObjectRef();}pp.push_back(p);
        }
        std::vector<mechanism::ElectronMove> mm;auto inferred=mechanism::inferElectronMoves(d,pp,&mm);
        if(!inferred.ok()){
            std::ofstream debug(dir+"/short-synthetic-diagnostic.txt",std::ios::app);debug<<"bound="<<bind<<" error="<<inferred.message<<"\n";
            for(size_t k=0;k<pp.size();++k){auto p=pp[k];std::vector<Vec2> pts;arrows::Tessellation tess;tess.tolerance=.1;if(p.route.size()>1)pts=arrows::routedPoints(p);else arrows::flattenArc(p.begin,p.end,p.arcSagitta,tess,&pts);Vec2 dirs[2]={p.begin-p.end,p.end-p.begin};if(pts.size()>1){dirs[0]=pts[0]-pts[1];dirs[1]=pts.back()-pts[pts.size()-2];}
                for(int side=0;side<2;++side){Vec2 point=side?p.end:p.begin;double unit=mechanism::infer_detail::localUnit(d,point);debug<<"arrow "<<k<<" side "<<side<<" point "<<point.x<<","<<point.y<<" dir "<<dirs[side].x<<","<<dirs[side].y<<"\n";
                    for(const auto& a:d.atoms().records())if(a.isAlive()){double score;ObjectRef ref(ObjectKind::Atom,a.id);if(mechanism::infer_detail::nearRef(d,ref,point,dirs[side],unit,&score))debug<<" atom "<<a.id<<" score "<<score<<" directed "<<mechanism::infer_detail::nearRef(d,ref,point,dirs[side],unit,&score,true)<<"\n";}
                    for(const auto& b:d.bonds().records())if(b.isAlive()){double score;ObjectRef ref(ObjectKind::Bond,b.id);if(mechanism::infer_detail::nearRef(d,ref,point,dirs[side],unit,&score))debug<<" bond "<<b.id<<" "<<b.beginAtom<<"-"<<b.endAtom<<" score "<<score<<" directed "<<mechanism::infer_detail::nearRef(d,ref,point,dirs[side],unit,&score,true)<<"\n";}
                }
            }
        }
        require(inferred.ok(),bind?"short H B bound arrows infer proton transfer":"short H B unbound arrows infer proton transfer");
        std::vector<ObjectRef> made;require(mechanism::generateNextStep(d,mm,&made).ok(),"short H B proton transfer generates product");
        int bases=0,oh=0;for(auto r:made)if(r.kind==ObjectKind::Atom&&mechanism::genericRole(d,r.id)=="base"){++bases;require(d.atoms().get(r.id)->semantics.charge==-1&&d.incidentBonds(r.id).empty(),"departing B is unbonded minus");}else if(r.kind==ObjectKind::Bond){auto b=d.bonds().get(r.id);auto a=d.atoms().get(b->beginAtom),z=d.atoms().get(b->endAtom);if((a->element==8&&z->element==1)||(a->element==1&&z->element==8))++oh;}
        require(bases==1&&oh==1,"hydrogen transfers from B to oxygen");
    }
    for(int sign:{-1,1}){reset();auto c=atom(Vec2(80,80));selectAway({c});auto before=s.doc.digest();require(H(c)==4,"isolated neutral carbon starts CH4");require(shortcut(sign<0?VK_OEM_MINUS:VK_OEM_PLUS,sign<0?0:kModShift),"selected carbon charge key handled");require(s.doc.atoms().get(c)->semantics.charge==sign&&H(c)==3,"selected carbon charge key produces CH3 ion");auto f=studio::analyze(s.doc,{ObjectRef(ObjectKind::Atom,c)});require(f.valid&&f.plain.find("CH3")!=std::string::npos&&f.plain.find(sign<0?"−":"+")!=std::string::npos,"formula reports changed hydrogens and charge");auto after=s.doc.digest();require(s.undo().ok()&&s.doc.digest()==before&&H(c)==4,"charge and hydrogen change undo together");require(s.undo(true).ok()&&s.doc.digest()==after,"charge and hydrogen redo together");selectAway({c});require(shortcut(sign<0?VK_OEM_PLUS:VK_OEM_MINUS,sign<0?kModShift:0),"neutralizing charge key handled");require(s.doc.atoms().get(c)->semantics.charge==0&&H(c)==4,"neutralizing restores fourth hydrogen");}
    reset();auto radical=atom(Vec2(80,80));auto digest=s.doc.digest();selectAway({radical});applyRadical({radical});require(s.doc.atoms().get(radical)->semantics.radical==uint8_t(Radical::Doublet)&&H(radical)==3,"radical action produces CH3 doublet");s.refresh();int dots=0;for(const auto& p:s.scene.primitives())if(p.source==ObjectRef(ObjectKind::Atom,radical)&&p.kind==render::PrimKind::Polygon&&p.style.fill.enabled&&p.points.size()==32&&p.z==12&&std::abs(length(p.points[0]-p.points[16])-2*arrows::mechanismRadicalRadius(s.doc))<1e-6)++dots;require(dots==1,"carbon radical renders one solid dot");require(s.undo().ok()&&s.doc.digest()==digest,"radical and hydrogen undo together");applyRadical({radical});applyRadical({radical});require(!s.doc.atoms().get(radical)->semantics.radical&&H(radical)==4,"radical toggle restores CH4");
    for(int mode:{-1,1,0}){reset();auto center=atom(Vec2(80,80)),left=atom(Vec2(54,95)),right=atom(Vec2(106,95));bond(center,left);bond(center,right);auto sem=s.doc.atoms().get(center)->semantics;sem.implicitHydrogens=2;s.doc.setAtomSemantics(center,sem);selectAway({center});auto before=s.doc.digest();if(mode)require(shortcut(mode<0?VK_OEM_MINUS:VK_OEM_PLUS,mode<0?0:kModShift),"degree two carbon charge key handled");else applyRadical({center});require(H(center)==1&&s.doc.atoms().get(center)->semantics.implicitHydrogens==1,"frozen CH2 count becomes CH1 for charged or radical carbon");require(s.undo().ok()&&s.doc.digest()==before&&H(center)==2,"frozen hydrogen undo restores exactly two");}
    // An explicit C-H (or C-D) graph bond is never deleted to make room for charge.
    reset();auto carbon=atom(Vec2(80,80));std::vector<ObjectId> explicitH;for(auto p:{Vec2(80,50),Vec2(110,80),Vec2(80,110),Vec2(50,80)}){auto h=atom(p,1);explicitH.push_back(h);bond(carbon,h);}auto isotope=s.doc.atoms().get(explicitH[0])->semantics;isotope.isotope=2;s.doc.setAtomSemantics(explicitH[0],isotope);auto beforeExplicit=s.doc.digest();auto e=s.submit(chemistry::setChargeCommand({carbon},-1),"explicit H guard");require(!e.ok()&&s.doc.digest()==beforeExplicit&&s.doc.stats().aliveAtoms==5&&s.doc.stats().aliveBonds==4&&s.doc.atoms().get(explicitH[0])->semantics.isotope==2,"explicit hydrogens and deuterium survive rejected charge edit");e=s.submit(chemistry::setRadicalCommand({carbon},int(Radical::Doublet)),"explicit H radical guard");require(!e.ok()&&s.doc.digest()==beforeExplicit,"explicit four bond carbon radical rejects without deleting hydrogen");
    // Selected-set dispatch must be atomic, including rollback when one target fails.
    auto good=atom(Vec2(160,80));selectAway({good,carbon});auto beforeBatch=s.doc.digest();shortcut(VK_OEM_MINUS,0);require(s.doc.digest()==beforeBatch,"mixed valid invalid selected charge edit is atomic");
    reset();auto a=atom(Vec2(80,80)),b=atom(Vec2(140,80));selectAway({a,b});auto beforeMulti=s.doc.digest();require(shortcut(VK_OEM_MINUS,0),"multiple selected charge key handled");require(s.doc.atoms().get(a)->semantics.charge==-1&&s.doc.atoms().get(b)->semantics.charge==-1&&H(a)==3&&H(b)==3,"both selected atoms updated");require(s.undo().ok()&&s.doc.digest()==beforeMulti,"multiple selected charge edit one undo");
    // Dimethyl malonate: C5H8O4 -> C5H7O4− at the central methylene.
    reset();auto center=atom(Vec2(100,100)),cl=atom(Vec2(74,85)),cr=atom(Vec2(126,85));bond(center,cl);bond(center,cr);auto ol=atom(Vec2(74,55),8),orr=atom(Vec2(126,55),8);bond(cl,ol,BondOrder::Double);bond(cr,orr,BondOrder::Double);auto el=atom(Vec2(48,100),8),er=atom(Vec2(152,100),8);bond(cl,el);bond(cr,er);auto ml=atom(Vec2(22,85)),mr=atom(Vec2(178,85));bond(el,ml);bond(er,mr);s.refresh();auto neutral=studio::analyze(s.doc,{ObjectRef(ObjectKind::Atom,center)});require(neutral.valid&&neutral.plain=="C5H8O4"&&H(center)==2,"dimethyl malonate neutral formula");selectAway({center});require(shortcut(VK_OEM_MINUS,0),"malonate central carbon negative key handled");auto ion=studio::analyze(s.doc,{ObjectRef(ObjectKind::Atom,center)});require(H(center)==1&&ion.valid&&ion.plain.find("C5H7O4")!=std::string::npos&&ion.plain.find("−")!=std::string::npos,"malonate carbanion removes one hidden hydrogen");s.refresh();s.selected.clear();s.fit();invalidate();screenshot(dir+"/malonate-charge-render.png");bool chargeVisible=false,carbonGlyph=false;for(const auto& primitive:s.scene.primitives())if(primitive.source==ObjectRef(ObjectKind::Atom,center)){
        if(primitive.kind==render::PrimKind::Polygon&&primitive.style.fill.enabled)carbonGlyph=true;
        if(primitive.kind==render::PrimKind::Segment&&primitive.style.stroke.enabled&&primitive.style.strokeWidth>0&&primitive.points.size()==2){auto a=primitive.points[0],b=primitive.points[1];if(std::abs(a.y-b.y)<1e-8&&std::abs(a.x-b.x)>1&&a.y<s.doc.atoms().get(center)->position.y)chargeVisible=true;}
    }require(chargeVisible&&!carbonGlyph,"skeletal carbanion renders horizontal minus without carbon glyph");
    s.selected.clear();s.fit();invalidate();require(s.save(dir+"/malonate-carbanion.cdxml",true).ok(),"save malonate carbanion");screenshot(dir+"/malonate-carbanion.png");Session reopened;require(reopened.open(dir+"/malonate-carbanion.cdxml").ok(),"reopen malonate carbanion");auto reopenedFormula=studio::analyze(reopened.doc,reopened.all());require(reopenedFormula.valid&&reopenedFormula.plain==ion.plain,"saved carbanion keeps correct hydrogen formula");
    // Restore neutral malonate, add an explicit transferable H and symbolic base.
    require(s.submit(chemistry::setChargeCommand({center},0),"neutral malonate").ok(),"neutralize malonate before explicit proton fixture");auto centerSem=s.doc.atoms().get(center)->semantics;centerSem.implicitHydrogens=1;s.doc.setAtomSemantics(center,centerSem);auto proton=atom(Vec2(100,130),1);auto ch=bond(center,proton);auto base=atom(Vec2(100,170));require(mechanism::setGenericRole(s.doc,base,"base").ok(),"create symbolic base B minus");require(!studio::analyze(s.doc,{ObjectRef(ObjectKind::Atom,base)}).valid,"symbolic base has no invented concrete formula");auto boron=atom(Vec2(250,170),5);require(s.doc.atoms().get(boron)->element==5,"real boron retains element five");auto sourceSlots=s.doc.atoms().size();std::vector<ObjectRef> generated;auto step=mechanism::generateNextStep(s.doc,{{ObjectRef(ObjectKind::Atom,base),ObjectRef(ObjectKind::Atom,proton)},{ObjectRef(ObjectKind::Bond,ch),ObjectRef(ObjectKind::Atom,center)}},&generated);require(step.ok(),"symbolic B minus deprotonates malonate");ObjectId productCenter=kInvalidObjectId,productBase=kInvalidObjectId,productH=kInvalidObjectId;for(auto r:generated)if(r.kind==ObjectKind::Atom){auto a=s.doc.atoms().get(r.id);if(s.doc.atomLabel(r.id)=="B")productBase=r.id;else if(a->element==1)productH=r.id;else if(a->element==6&&a->semantics.charge==-1)productCenter=r.id;}require(productCenter!=kInvalidObjectId&&productBase!=kInvalidObjectId&&productH!=kInvalidObjectId,"base reaction product roles identified");bool bh=false,remainingCH=false;for(const auto& b:s.doc.bonds().records())if(b.isAlive()){if((b.beginAtom==productBase&&b.endAtom==productH)||(b.endAtom==productBase&&b.beginAtom==productH))bh=true;if((b.beginAtom==productCenter&&b.endAtom==productH)||(b.endAtom==productCenter&&b.beginAtom==productH))remainingCH=true;}require(bh&&!remainingCH&&s.doc.atoms().get(productBase)->semantics.charge==0&&H(productCenter)==1,"proton transfers to B while alpha carbon becomes CH minus");require(s.doc.atoms().get(boron)->element==5&&s.doc.atoms().get(boron)->id<sourceSlots,"symbolic base does not alter true boron spectator");require(s.save(dir+"/symbolic-base-result.cdxml",true).ok(),"save symbolic base proton transfer");Session baseReopen;require(baseReopen.open(dir+"/symbolic-base-result.cdxml").ok(),"reopen symbolic base proton transfer");int symbolicBases=0;for(const auto& a:baseReopen.doc.atoms().records())if(a.isAlive()&&baseReopen.doc.atomLabel(a.id)=="B"){require(a.element!=5&&a.semantics.nodeType!=uint8_t(NodeType::Element),"symbolic B stays non-elemental on reload");++symbolicBases;}require(symbolicBases==2,"both symbolic base states survive save");
    reset();auto methyl=atom(Vec2(60,80)),chlorine=atom(Vec2(90,80),17),nu=atom(Vec2(15,80));auto ccl=bond(methyl,chlorine);require(mechanism::setGenericRole(s.doc,nu,"nucleophile").ok(),"create symbolic Nu minus");require(!studio::analyze(s.doc,{ObjectRef(ObjectKind::Atom,nu)}).valid,"symbolic nucleophile has no invented concrete formula");generated.clear();step=mechanism::generateNextStep(s.doc,{{ObjectRef(ObjectKind::Atom,nu),ObjectRef(ObjectKind::Atom,methyl)},{ObjectRef(ObjectKind::Bond,ccl),ObjectRef(ObjectKind::Atom,chlorine)}},&generated);require(step.ok(),"symbolic Nu minus substitution generates product");ObjectId productNu=kInvalidObjectId,productMethyl=kInvalidObjectId,productCl=kInvalidObjectId;for(auto r:generated)if(r.kind==ObjectKind::Atom){auto a=s.doc.atoms().get(r.id);if(s.doc.atomLabel(r.id)=="Nu")productNu=r.id;else if(a->element==17)productCl=r.id;else if(a->element==6)productMethyl=r.id;}require(productNu!=kInvalidObjectId&&productMethyl!=kInvalidObjectId&&productCl!=kInvalidObjectId&&s.doc.atoms().get(productNu)->semantics.charge==0&&s.doc.atoms().get(productCl)->semantics.charge==-1&&H(productMethyl)==3&&s.doc.incidentBonds(productCl).empty(),"Nu product and chloride charge correct");bool nuc=false;for(auto id:s.doc.incidentBonds(productNu)){auto b=s.doc.bonds().get(id);if(b->beginAtom==productMethyl||b->endAtom==productMethyl)nuc=true;}require(nuc,"new Nu carbon bond exists");require(s.save(dir+"/symbolic-nucleophile-result.cdxml",true).ok(),"save symbolic nucleophile result");
    {Document copy;std::vector<ObjectRef> inserted;require(studioio::append(s.doc,copy,s.all(),Vec2(30,40),&inserted).ok(),"copy symbolic nucleophile scheme");int roles=0;for(const auto& a:copy.atoms().records())if(a.isAlive()&&copy.atomLabel(a.id)=="Nu"){require(mechanism::genericRole(copy,a.id)=="nucleophile","copy preserves symbolic role marker");++roles;}require(roles==2,"copy retains reagent and product symbolic roles");require(s.save(dir+"/symbolic-nucleophile-result.r62doc").ok(),"save native symbolic role");Session native;require(native.open(dir+"/symbolic-nucleophile-result.r62doc").ok(),"reopen native symbolic role");int retained=0;for(const auto& a:native.doc.atoms().records())if(a.isAlive()&&mechanism::genericRole(native.doc,a.id)=="nucleophile")++retained;require(retained==2,"native save preserves symbolic roles");}
    {Session manual;ObjectId c,h,b;manual.doc.addAtom(Vec2(0,0),6,&c);manual.doc.addAtom(Vec2(14.4,0),1,&h);manual.doc.addBond(c,h,&b);manual.refresh();manual.activateCommand("chooseArrowTool_90_CW");manual.gesture(PointerPhase::Down,Vec2(7.2,0));manual.gesture(PointerPhase::Move,Vec2(0,0));manual.gesture(PointerPhase::Up,Vec2(0,0));arrows::ArrowParams p;bool ok=false;arrows::readArrow(manual.doc,0,&p,&ok);require(ok&&arrows::arrowStrokeWidth(p,.6)<=.51,"actual manual short arrow defaults to fine stroke");}
    // Preserve a substantial curved shaft even at textbook C-H bond lengths.
    reset();std::vector<arrows::ArrowParams> shortParams;bool shortClear=true,shortReadable=true;for(int i=0;i<5;++i){double distance=i==0?10:i==1?14.4:i==2?8:i==3?12:14.4;Vec2 origin(55+i*55,70);auto from=atom(origin,i<2||i==4?6:8),to=atom(origin+Vec2(distance,0),i<2?1:7);if(i==4){auto sem=s.doc.atoms().get(from)->semantics;sem.charge=-1;sem.implicitHydrogens=1;s.doc.setAtomSemantics(from,sem);auto left=atom(origin+Vec2(-12,-7)),right=atom(origin+Vec2(-12,7));bond(from,left);bond(from,right);}s.refresh();std::vector<Box> inkBoxes;for(const auto& primitive:s.scene.primitives())if(primitive.source.kind==ObjectKind::Atom&&(primitive.source.id==from||primitive.source.id==to)&&primitive.kind==render::PrimKind::Polygon&&!primitive.bounds.empty())inkBoxes.push_back(primitive.bounds);ObjectId sourceBond=kInvalidObjectId;if(i<2)sourceBond=bond(from,to);auto source=ObjectRef(i<2?ObjectKind::Bond:ObjectKind::Atom,i<2?sourceBond:from),target=ObjectRef(ObjectKind::Atom,i<2?from:to);Vec2 begin,end;arrows::mechanismAnchorPoint(s.doc,source,&begin);arrows::mechanismAnchorPoint(s.doc,target,&end);auto p=arrows::resolvePreset(49,begin,end,.6);p.mechanism=true;p.electronSource=source;p.electronTarget=target;p.head.length=3.2;p.head.width=2.24;arrows::routeMechanism(s.doc,p);arrows::clearMechanismLabels(s.doc,p);require(p.electronSource==source&&p.electronTarget==target,"short arrow clipping retains electron references");arrows::ArrowGeometry geometry;require(arrows::buildArrowGeometry(p,.6,arrows::Tessellation(),&geometry).ok()&&!geometry.shafts.empty()&&geometry.heads.size()==1&&geometry.heads[0].filled&&geometry.heads[0].outline.size()==4,"short arrow has a filled swept tip with a recessed tail");double shaft=0;for(size_t j=1;j<geometry.shafts[0].size();++j)shaft+=core::length(geometry.shafts[0][j]-geometry.shafts[0][j-1]);shortReadable=shortReadable&&(shaft>=std::max(6.,p.head.length*2.5)&&geometry.strokeWidth<=.51);if(i>=2){require(!inkBoxes.empty(),"short labelled fixture has actual glyph outlines");auto outside=[&](Vec2 point){for(const auto& box:inkBoxes)if(point.x>box.min.x+.05&&point.x<box.max.x-.05&&point.y>box.min.y+.05&&point.y<box.max.y-.05){std::ofstream(dir+"/short-arrow-collision.txt",std::ios::app)<<"case="<<i<<" point="<<point.x<<","<<point.y<<" glyph="<<box.min.x<<","<<box.min.y<<".."<<box.max.x<<","<<box.max.y<<"\n";return false;}return true;};for(auto point:geometry.shafts[0])shortClear=outside(point)&&shortClear;for(auto point:geometry.heads[0].outline)shortClear=outside(point)&&shortClear;}ObjectId id;require(arrows::addArrow(s.doc,p,.6,&id).ok(),"save short arrow record");shortParams.push_back(p);ObjectId text;s.doc.addText(origin+Vec2(-9,30),6,i<2?"C-H":i<4?"O to N":"CH- to N",&text);}
    s.refresh();s.selected.clear();s.fit();invalidate();require(s.save(dir+"/short-mechanism-arrows.cdxml",true).ok(),"save short arrow fixture");screenshot(dir+"/short-mechanism-arrows.png");require(shortReadable,"short arrows retain readable curved shafts and fine strokes");require(shortClear,"short curves and heads stay outside actual glyph boxes");Session shortReopen;require(shortReopen.open(dir+"/short-mechanism-arrows.cdxml").ok(),"reopen short arrow fixture");for(size_t i=0;i<shortParams.size();++i){arrows::ArrowParams p;bool ok=false;arrows::readArrow(shortReopen.doc,ObjectId(i),&p,&ok);require(ok&&p.route==shortParams[i].route&&std::abs(p.head.length-shortParams[i].head.length)<1e-7&&p.electronSource.valid()&&p.electronTarget.valid(),"short arrow route head and references persist");}
    // User-facing nickname workflow: preserve compact CO2Me in both drawings.
    reset();auto alpha=atom(Vec2(100,100)),groupL=atom(Vec2(74,85)),groupR=atom(Vec2(126,85)),me=atom(Vec2(220,100)),iodine=atom(Vec2(250,100),53);bond(alpha,groupL);bond(alpha,groupR);auto ci=bond(me,iodine);require(nicknames::apply(s.doc,groupL,"CO2Me").ok()&&nicknames::apply(s.doc,groupR,"CO2Me").ok(),"real CO2Me nickname fragments installed");auto alphaSem=s.doc.atoms().get(alpha)->semantics;alphaSem.charge=-1;alphaSem.implicitHydrogens=1;s.doc.setAtomSemantics(alpha,alphaSem);auto fragmentL=*static_cast<const Document&>(s.doc).extensions(ObjectRef(ObjectKind::Atom,groupL)).findValue("studio.nickname.fragment"),fragmentR=*static_cast<const Document&>(s.doc).extensions(ObjectRef(ObjectKind::Atom,groupR)).findValue("studio.nickname.fragment");std::vector<ObjectId> semanticArrows;for(auto movement:std::vector<mechanism::ElectronMove>{{ObjectRef(ObjectKind::Atom,alpha),ObjectRef(ObjectKind::Atom,me)},{ObjectRef(ObjectKind::Bond,ci),ObjectRef(ObjectKind::Atom,iodine)}}){Vec2 from,to;arrows::mechanismAnchorPoint(s.doc,movement.source,&from);arrows::mechanismAnchorPoint(s.doc,movement.target,&to);auto p=arrows::resolvePreset(49,from,to,.6);p.mechanism=true;p.electronSource=movement.source;p.electronTarget=movement.target;p.head.length=3.2;p.head.width=2.24;arrows::routeMechanism(s.doc,p);arrows::clearMechanismLabels(s.doc,p);ObjectId id;require(arrows::addArrow(s.doc,p,.6,&id).ok(),"nickname reaction arrow");semanticArrows.push_back(id);}s.refresh();s.selected=s.all();auto beforeNickname=s.doc.digest();auto oldAtoms=s.doc.stats().aliveAtoms;generateMechanismStep();std::ofstream(dir+"/electron-state-progress.txt",std::ios::app)<<"generation status: "<<status<<"\n";require(s.doc.stats().aliveAtoms==oldAtoms+5,"nickname alkylation keeps both ester groups compact");auto composition=studio::analyze(s.doc,s.selected);require(composition.valid&&composition.plain.find("C6H10IO4")!=std::string::npos&&composition.plain.find("−")!=std::string::npos,"compact malonate methylation has correct formula and iodide charge");int preservedEsters=0;for(auto ref:s.selected)if(ref.kind==ObjectKind::Atom&&s.doc.atomLabel(ref.id)=="CO2Me")++preservedEsters;require(preservedEsters==2,"both generated ester labels retain their abbreviations");require(s.doc.atomLabel(groupL)=="CO2Me"&&s.doc.atomLabel(groupR)=="CO2Me"&&*static_cast<const Document&>(s.doc).extensions(ObjectRef(ObjectKind::Atom,groupL)).findValue("studio.nickname.fragment")==fragmentL&&*static_cast<const Document&>(s.doc).extensions(ObjectRef(ObjectKind::Atom,groupR)).findValue("studio.nickname.fragment")==fragmentR,"source CO2Me labels and hidden fragments remain intact");auto afterNickname=s.doc.digest();require(s.undo().ok()&&s.doc.digest()==beforeNickname,"compact product insertion undoes atomically");require(s.undo(true).ok()&&s.doc.digest()==afterNickname,"nickname product redo");s.selected.clear();s.fit();invalidate();require(s.save(dir+"/malonate-nickname-alkylation.cdxml",true).ok(),"save nickname alkylation");screenshot(dir+"/malonate-nickname-alkylation.png");
    if(GetFileAttributesW(W(dir+"/user-input.cdxml").c_str())!=INVALID_FILE_ATTRIBUTES){
        require(s.open(dir+"/user-input.cdxml").ok(),"open actual user malonate drawing including native payload");
        require(s.doc.atoms().alive(24)&&s.doc.atoms().alive(25)&&s.doc.atoms().get(25)->element==1&&s.doc.atomLabel(23)=="CO2Me"&&s.doc.atomLabel(26)=="CO2Me","real fixture retains source carbon explicit H and two ester aliases");
        require(s.doc.texts().alive(0)&&s.doc.texts().alive(1)&&s.doc.plainText(0)=="B"&&s.doc.plainText(1)=="-","actual base is two text captions rather than boron");
        auto beforeConversion=s.doc.digest();s.selected={ObjectRef(ObjectKind::Text,0),ObjectRef(ObjectKind::Text,1)};insertMechanismRole(true);require(s.selected.size()==1&&s.selected[0].kind==ObjectKind::Atom&&mechanism::genericRole(s.doc,s.selected[0].id)=="base","convert user B and minus captions through reagent UI");auto realBase=s.selected[0].id;require(!s.doc.texts().alive(0)&&!s.doc.texts().alive(1),"converted captions replaced by one chemical reagent");auto converted=s.doc.digest();require(s.undo().ok()&&s.doc.digest()==beforeConversion,"caption conversion one undo");require(s.undo(true).ok()&&s.doc.digest()==converted,"caption conversion redo restores role");
        std::vector<AtomRecord> sourceAtoms;std::vector<BondRecord> sourceBonds;for(const auto& a:s.doc.atoms().records())if(a.isAlive())sourceAtoms.push_back(a);for(const auto& b:s.doc.bonds().records())if(b.isAlive())sourceBonds.push_back(b);auto sourceFragment23=*static_cast<const Document&>(s.doc).extensions(ObjectRef(ObjectKind::Atom,23)).findValue("studio.nickname.fragment"),sourceFragment26=*static_cast<const Document&>(s.doc).extensions(ObjectRef(ObjectKind::Atom,26)).findValue("studio.nickname.fragment");
        require(s.save(dir+"/user-malonate-input.cdxml",true).ok(),"save prepared copy of actual user input");auto beforeGeneration=s.doc.digest();auto countBefore=s.doc.stats().aliveAtoms;s.selected={ObjectRef(ObjectKind::Atom,23),ObjectRef(ObjectKind::Atom,24),ObjectRef(ObjectKind::Atom,25),ObjectRef(ObjectKind::Atom,26),ObjectRef(ObjectKind::Atom,realBase),ObjectRef(ObjectKind::Arrow,4),ObjectRef(ObjectKind::Arrow,5)};generateMechanismStep();std::ofstream(dir+"/electron-state-progress.txt",std::ios::app)<<"generation status: "<<status<<"\n";require(s.doc.stats().aliveAtoms==countBefore+5,"actual handwritten malonate arrows generate a compact product");
        ObjectId alphaProduct=kInvalidObjectId,baseProduct=kInvalidObjectId,hydrogenProduct=kInvalidObjectId;int productAtomCount=0;for(auto ref:s.selected)if(ref.kind==ObjectKind::Atom){++productAtomCount;auto a=s.doc.atoms().get(ref.id);if(mechanism::genericRole(s.doc,ref.id)=="base")baseProduct=ref.id;else if(a->element==1)hydrogenProduct=ref.id;else if(a->element==6&&a->semantics.charge==-1)alphaProduct=ref.id;}require(productAtomCount==5&&alphaProduct!=kInvalidObjectId&&baseProduct!=kInvalidObjectId&&hydrogenProduct!=kInvalidObjectId&&H(alphaProduct)==1,"actual alpha carbon retains one hidden hydrogen and negative charge");bool protonOnBase=false,protonOnCarbon=false;for(auto id:s.doc.incidentBonds(hydrogenProduct)){auto b=s.doc.bonds().get(id);auto other=b->beginAtom==hydrogenProduct?b->endAtom:b->beginAtom;protonOnBase|=other==baseProduct;protonOnCarbon|=other==alphaProduct;}require(protonOnBase&&!protonOnCarbon&&s.doc.atoms().get(baseProduct)->semantics.charge==0,"actual explicit H transfers from carbon to neutral BH");arrows::ArrowParams reaction;bool foundReaction=false;for(auto ref:s.selected)if(ref.kind==ObjectKind::Arrow){bool valid=false;arrows::readArrow(s.doc,ref.id,&reaction,&valid);foundReaction=valid;}require(foundReaction,"BH reaction arrow exists");auto bhCenter=(s.doc.atoms().get(baseProduct)->position+s.doc.atoms().get(hydrogenProduct)->position)*.5;require(bhCenter.y<reaction.begin.y&&bhCenter.x>reaction.begin.x&&bhCenter.x<reaction.end.x,"BH sits above the reaction arrow");
        for(const auto& old:sourceAtoms){auto a=s.doc.atoms().get(old.id);require(a&&a->position==old.position&&a->element==old.element&&a->semantics==old.semantics,"real source atoms and prior examples remain unchanged");}for(const auto& old:sourceBonds){auto b=s.doc.bonds().get(old.id);require(b&&b->beginAtom==old.beginAtom&&b->endAtom==old.endAtom&&b->order==old.order&&b->semantics==old.semantics,"real source bonds remain unchanged");}require(*static_cast<const Document&>(s.doc).extensions(ObjectRef(ObjectKind::Atom,23)).findValue("studio.nickname.fragment")==sourceFragment23&&*static_cast<const Document&>(s.doc).extensions(ObjectRef(ObjectKind::Atom,26)).findValue("studio.nickname.fragment")==sourceFragment26,"real source nickname definitions unchanged");auto generatedDigest=s.doc.digest();require(s.undo().ok()&&s.doc.digest()==beforeGeneration,"real user inference expansion and insertion one undo");require(s.undo(true).ok()&&s.doc.digest()==generatedDigest,"real user result redo");s.selected.clear();s.fit();invalidate();require(s.save(dir+"/user-malonate-result.cdxml",true).ok(),"save actual user malonate result");screenshot(dir+"/user-malonate-result.png");
        // Demonstration only: refresh the original short C-H electron arrow in a
        // separate output after all original-geometry preservation checks pass.
        {arrows::ArrowParams p;bool ok=false;require(arrows::readArrow(s.doc,5,&p,&ok).ok()&&ok&&p.mechanism,"read bound original short arrow for polished demo");Vec2 from,to;require(arrows::mechanismAnchorPoint(s.doc,p.electronSource,&from)&&arrows::mechanismAnchorPoint(s.doc,p.electronTarget,&to),"polished demo uses inferred exact anchors");Document routing=s.doc;require(routing.removeObject(ObjectRef(ObjectKind::Arrow,5)).ok(),"exclude original arrow from its own route search");p.begin=from;p.end=to;p.route.clear();p.arcSagitta=(p.arcSagitta<0?-1:1)*core::length(to-from)*.42;p.strokeWidth=0;p.head.length=3.2;p.head.width=2.24;arrows::routeMechanism(routing,p);arrows::clearMechanismLabels(routing,p);require(arrows::writeArrow(s.doc,5,p,s.doc.defaultLineWidth()).ok(),"refresh sample short arrow appearance");s.refresh();s.selected.clear();s.fit();invalidate();require(s.save(dir+"/user-malonate-polished.cdxml",true).ok(),"save separate polished user demonstration");screenshot(dir+"/user-malonate-polished.png");}
    }
    std::ofstream(dir+"/electron-state-checks.json")<<"{\"selected_keyboard_charge\":true,\"carbon_hydrogen_valence\":true,\"radical\":true,\"explicit_isotope_guard\":true,\"atomic_multiselect\":true,\"malonate_formula\":true,\"visible_charge\":true,\"symbolic_base\":true,\"symbolic_nucleophile\":true,\"short_arrow_geometry\":true}";s.dirty=false;PostMessageW(h,WM_CLOSE,0,0);
}
