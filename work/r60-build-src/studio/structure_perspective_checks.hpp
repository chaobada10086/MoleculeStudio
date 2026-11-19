#pragma once
static void checkStructurePerspective(App& app,const std::string& dir){
    auto& s=app.s;auto require=[](bool ok,const char* why){if(!ok)throw std::runtime_error(std::string("structure perspective: ")+why);};
    Document d;std::vector<ObjectId> ids;std::vector<ObjectId> bonds;
    for(int i=0;i<6;++i){ObjectId id;d.addAtom(direction(i*60)*32,6,&id);ids.push_back(id);}
    for(int i=0;i<6;++i){ObjectId id;d.addBond(ids[i],ids[(i+1)%6],&id);if(i%2==0)d.setBondOrder(id,BondOrder::Double);bonds.push_back(id);}
    ObjectId other;d.addAtom(Vec2(140,80),8,&other);
    s.cancel();s.doc=d;s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());s.refresh();s.fit();
    app.execute(Button("test.perspective","结构透视","perspective","@perspective"));require(app.perspectiveTool,"toolbar activation");
    for(auto id:ids)s.selected.push_back(ObjectRef(ObjectKind::Atom,id));
    auto p=s.view().toDevice(d.atoms().alive(ids[0])->position);auto target=p+Vec2(0,1.05/(.020*s.view().docPerDevice()));
    auto send=[&](UINT m,Vec2 v,WPARAM mod=0){SendMessageW(app.canvas,m,mod,MAKELPARAM(int(std::lround(v.x)),int(std::lround(v.y))));};
    auto before=s.doc.digest();send(WM_LBUTTONDOWN,p);require(app.perspectiveDragging&&app.perspectiveAtoms.size()==6,"pick expands just the connected ring");send(WM_MOUSEMOVE,target,MK_LBUTTON|MK_SHIFT);
    require(s.doc.digest()==before&&s.history.cursor()==0,"preview leaves live model and history unchanged");require(app.perspectiveYaw==0,"Shift locks axis");
    const auto preview=app.perspectivePreview->doc.digest();send(WM_LBUTTONUP,target,MK_SHIFT);require(!app.transformDragging&&s.doc.digest()==preview&&s.history.cursor()==1,"preview equals one atomic commit");
    require(s.doc.atoms().alive(other)->position==d.atoms().alive(other)->position,"unselected molecule stays fixed");
    double minY=1e9,maxY=-1e9;int bold=0,doubles=0;
    for(auto id:ids){auto a=s.doc.atoms().alive(id);minY=std::min(minY,a->position.y);maxY=std::max(maxY,a->position.y);}
    for(auto id:bonds){auto b=s.doc.bonds().alive(id);doubles+=b->order==BondOrder::Double;bold+=b->display==BondDisplay::Bold;}
    require(maxY-minY<40&&bold>=1&&bold<=3&&doubles==3,"flattening, near-side bold and double bonds");
    int boldInk=0;for(const auto& ink:s.scene.primitives())if(ink.source.kind==ObjectKind::Bond&&ink.style.strokeWidth>d.defaultLineWidth()*1.5)++boldInk;require(boldInk>=bold,"bold metadata reaches actual rendered strokes");
    auto reversed=s.doc;std::set<ObjectId> ringIds(ids.begin(),ids.end());require(structure_perspective::tilt(reversed,ringIds,app.perspectivePivot,0,-app.perspectivePitch).ok(),"reverse tilt");for(auto id:ids)require(length(reversed.atoms().alive(id)->position-d.atoms().alive(id)->position)<1e-8,"depth preserves reversibility across gestures");
    for(auto id:ids)require(s.doc.extensions(ObjectRef(ObjectKind::Atom,id)).findValue("studio.atom.z")!=nullptr,"standard xyz depth");
    require(app.canvasCursorId(s.view().toDevice(Vec2(500,500)))==32512,"arrow outside selection");
    require(app.canvasCursorId(s.view().toDevice(app.perspectivePivot))==32649,"hand inside selection");
    auto after=s.doc.digest();require(s.undo().ok()&&s.doc.digest()==before,"undo exact source");require(s.undo(true).ok()&&s.doc.digest()==after,"redo exact result");
    require(s.save(dir+"/structure-perspective.r62doc",false).ok(),"native save");Session reopen;require(reopen.open(dir+"/structure-perspective.r62doc").ok()&&reopen.doc.digest()==after,"native persistence including depth and bold");
    require(s.save(dir+"/structure-perspective.cdxml",true).ok(),"CDXML export");Session xml;require(xml.open(dir+"/structure-perspective.cdxml").ok(),"CDXML reopen");int xmlBold=0;for(auto& b:xml.doc.bonds().records())if(b.isAlive())xmlBold+=b.display==BondDisplay::Bold;require(xmlBold==bold,"CDXML drawing metadata round trip");
    p=s.view().toDevice(s.doc.atoms().alive(ids[0])->position);send(WM_LBUTTONDOWN,p);send(WM_MOUSEMOVE,p+Vec2(28,22),MK_LBUTTON);SendMessageW(app.canvas,WM_KEYDOWN,VK_ESCAPE,0);require(!app.transformDragging&&s.doc.digest()==after,"Esc cancels without changes");
    // Standard native file produced by the installed ChemDraw, including xyz.
    if(const char* sample=std::getenv("MOLECULE_STUDIO_NATIVE_PERSPECTIVE")){Session native;require(native.open(sample).ok(),"open actual ChemDraw perspective");require(native.save(dir+"/native-roundtrip.cdxml",true).ok(),"write actual ChemDraw perspective back");}
    app.perspectiveTool=false;s.selected.clear();s.fit();app.screenshot(dir+"/structure-perspective.png");
    bool cut=false,tool=false;for(auto& b:app.groups[0].items){cut|=b.cmd=="@cut";tool|=b.cmd=="@perspective";}require(!cut&&tool,"home perspective replaces cut button");
    s.doc=Document();s.history.clear();s.selected.clear();s.refresh();s.activateCommand("chooseSelectionTool_Marquee");
    std::ofstream(dir+"/structure-perspective-checks.json")<<"{\"real_window_drag\":true,\"preview_no_mutation\":true,\"one_undo_step\":true,\"shift_axis\":true,\"near_bonds_bold\":true,\"double_bonds_preserved\":true,\"unselected_structure_fixed\":true,\"undo_redo\":true,\"native_and_cdxml_round_trip\":true,\"escape_cancel\":true,\"home_cut_removed\":true}";
}

