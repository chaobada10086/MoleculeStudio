#pragma once
static void fixedDragCheck(App& app,const std::string& dir){
    auto require=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};
    auto& s=app.s;
    auto reset=[&](){s.cancel();s.doc=Document();s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());s.dragFixedLength=true;s.activateCommand("chooseSelectionTool_Marquee");};
    auto atom=[&](double x,double y){ObjectId id;require(s.doc.addAtom(Vec2(x,y),6,&id).ok(),"add atom");return id;};
    auto bond=[&](ObjectId a,ObjectId b){ObjectId id;require(s.doc.addBond(a,b,&id).ok(),"add bond");return id;};
    auto lengths=[&](){std::map<ObjectId,double> result;for(const auto& b:s.doc.bonds().records())if(b.isAlive())result[b.id]=core::length(s.doc.atoms().get(b.beginAtom)->position-s.doc.atoms().get(b.endAtom)->position);return result;};
    auto checkLengths=[&](const std::map<ObjectId,double>& before){auto after=lengths();require(after.size()==before.size(),"bond count");for(auto p:before)require(std::abs(after[p.first]-p.second)<1e-5,"bond length changed");};
    auto positions=[&](){std::map<ObjectId,Vec2> r;for(const auto& a:s.doc.atoms().records())if(a.isAlive())r[a.id]=a.position;return r;};
    auto same=[&](const std::map<ObjectId,Vec2>& p){for(auto a:p)require(core::length(s.doc.atoms().get(a.first)->position-a.second)<1e-6,"positions differ");};
    reset();auto a=atom(0,0),b=atom(30,0),farAtom=atom(300,300);bond(a,b);s.refresh();auto before=lengths();auto initial=positions();s.selected={ObjectRef(ObjectKind::Atom,b)};
    s.gesture(PointerPhase::Down,Vec2(30,0));s.gesture(PointerPhase::Move,Vec2(40,10));s.gesture(PointerPhase::Move,Vec2(30,20));
    checkLengths(before);require(s.doc.atoms().get(b)->position.y>0,"single branch rotates in pointer direction");require(core::length(s.doc.atoms().get(a)->position-initial[a])<1e-8,"single branch pivot moved");
    auto preview=positions();s.gesture(PointerPhase::Up,Vec2(30,20));same(preview);checkLengths(before);require(core::length(s.doc.atoms().get(farAtom)->position-initial[farAtom])<1e-8,"unrelated atom moved");
    require(s.undo().ok(),"undo");same(initial);require(s.undo(true).ok(),"redo");same(preview);
    reset();a=atom(0,0);b=atom(30,0);auto c=atom(60,0);bond(a,b);bond(b,c);s.refresh();before=lengths();initial=positions();s.selected={ObjectRef(ObjectKind::Atom,b)};
    s.gesture(PointerPhase::Down,Vec2(30,0));s.gesture(PointerPhase::Move,Vec2(35,12));s.gesture(PointerPhase::Move,Vec2(37,15));checkLengths(before);preview=positions();
    auto previewScene=s.displayScene();require(core::length(preview[b]-Vec2(37,15))<1e-6,"middle atom does not follow pointer");s.gesture(PointerPhase::Up,Vec2(37,15));same(preview);checkLengths(before);std::string why;require(dragpreview::sameGeometry(previewScene,s.displayScene(),1e-5,&why),"preview drawing differs from commit");require(s.undo().ok(),"middle undo");same(initial);
    reset();std::vector<ObjectId> ring;for(int i=0;i<6;++i){double t=6.283185307179586*i/6;ring.push_back(atom(30*std::cos(t),30*std::sin(t)));}for(int i=0;i<6;++i)bond(ring[i],ring[(i+1)%6]);
    s.refresh();before=lengths();initial=positions();s.selected={ObjectRef(ObjectKind::Atom,ring[0])};s.gesture(PointerPhase::Down,Vec2(30,0));s.gesture(PointerPhase::Move,Vec2(43,12));checkLengths(before);preview=positions();s.gesture(PointerPhase::Up,Vec2(43,12));same(preview);checkLengths(before);
    require(s.undo().ok(),"ring undo");same(initial);require(s.undo(true).ok(),"ring redo");same(preview);
    reset();a=atom(0,0);b=atom(30,0);bond(a,b);s.refresh();initial=positions();s.selected={ObjectRef(ObjectKind::Atom,b)};s.gesture(PointerPhase::Down,Vec2(30,0));s.gesture(PointerPhase::Move,Vec2(30,20));s.gesture(PointerPhase::Move,Vec2(30,0));same(initial);s.gesture(PointerPhase::Up,Vec2(30,0));same(initial);
    reset();a=atom(0,0);b=atom(30,0);c=atom(60,0);bond(a,b);bond(b,c);ObjectId txt,group;require(s.doc.addText(Vec2(30,40),10,"test",&txt).ok(),"add text");GroupRecord g;g.members={ObjectRef(ObjectKind::Atom,b),ObjectRef(ObjectKind::Text,txt)};require(s.doc.addGroup(g,&group).ok(),"add group");s.refresh();before=lengths();initial=positions();s.selected={ObjectRef(ObjectKind::Group,group)};
    require(s.moveFixedLength(s.selected,Vec2(7,15)).ok(),"group move");checkLengths(before);require(core::length(s.doc.atoms().get(b)->position-Vec2(37,15))<1e-6,"group atom position");require(core::length(s.doc.texts().get(txt)->position-Vec2(37,55))<1e-6,"group text position");require(s.undo().ok(),"group undo");same(initial);
    reset();a=atom(0,0);b=atom(30,0);c=atom(60,0);bond(a,b);bond(b,c);s.refresh();before=lengths();s.dragFixedLength=false;s.selected={ObjectRef(ObjectKind::Atom,b)};require(s.move(s.selected,Vec2(0,15)).ok(),"unlocked move");require(std::abs(lengths().begin()->second-before.begin()->second)>1,"unlocked drag should be free");
    // Exercise the shell's higher-priority handle hit before the gesture, just
    // as WM_LBUTTONDOWN does when an endpoint overlaps a resize handle.
    reset();a=atom(0,0);b=atom(30,0);auto ab=bond(a,b);s.refresh();before=lengths();initial=positions();s.selected={ObjectRef(ObjectKind::Bond,ab)};
    auto endpointPixel=s.view().toDevice(Vec2(30,0));require(app.selectionHandleAt(endpointPixel)>=0,"fixture endpoint must overlap a handle");
    require(!app.beginSelectionTransform(endpointPixel,0),"locked endpoint must not start resize");require(s.selected.size()==1&&s.selected[0]==ObjectRef(ObjectKind::Atom,b),"endpoint must become drag target");
    s.gesture(PointerPhase::Down,Vec2(30,0));s.gesture(PointerPhase::Move,Vec2(40,15));checkLengths(before);preview=positions();s.gesture(PointerPhase::Up,Vec2(40,15));same(preview);checkLengths(before);require(core::length(s.doc.atoms().get(a)->position-initial[a])<1e-8,"endpoint pivot moved");
    s.selected={ObjectRef(ObjectKind::Bond,ab)};auto handles=app.selectionHandlePoints();auto corner=s.view().toDevice(handles[0]);require(!app.beginSelectionTransform(corner,0),"locked resize handle must not scale chemistry");
    s.dragFixedLength=false;s.selected={ObjectRef(ObjectKind::Bond,ab)};handles=app.selectionHandlePoints();require(app.beginSelectionTransform(s.view().toDevice(handles[0]),0),"unlocked resize handle");app.cancelSelectionTransform();
    std::ofstream(dir+"/fixed-drag-check.json")<<"{\"single_branch\":true,\"multiple_attachments\":true,\"ring\":true,\"group_and_text\":true,\"endpoint_handle\":true,\"resize_blocked\":true,\"unlocked\":true,\"preview_matches_commit\":true,\"undo_redo\":true,\"return_to_origin\":true}";
}
