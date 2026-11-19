#pragma once
static void checkCatalytic(App& app,const std::string& dir){
 using namespace studio::catalytic;auto require=[](bool ok,const std::string& why){if(!ok)throw std::runtime_error("catalytic: "+why);};
 app.switchTab(6);
 SetWindowPos(app.h,nullptr,0,0,int(1500*app.scale),int(900*app.scale),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
 for(bool compact:{true,false}){
     app.ribbonCompact=compact;app.layout();
     const auto& groups=app.visibleGroups();
     require(groups.back().items.front().cmd=="@catalytic","cycle commands are in the last group");
     for(size_t i=0;i+1<groups.size();++i)require(groups[i].r.GetRight()<=groups.back().r.X,"cycle group is rightmost");
     app.invalidate();app.screenshot(dir+(compact?"/mechanism-compact.png":"/mechanism-expanded.png"));
 }
 Session demo;auto& d=demo.doc;d.setDefaultBondLength(22);d.setDefaultTextSize(10);studioio::Page page;page.x=page.y=0;page.width=600;page.height=650;page.store(d);
 std::vector<ObjectId> nodes;for(int i=0;i<4;++i){ObjectId metal,ligand,b;d.addAtom(Vec2(50+i*65,100),46,&metal);d.addAtom(Vec2(50+i*65,78),6,&ligand);d.addBond(metal,ligand,&b);auto sem=d.atoms().alive(metal)->semantics;sem.implicitHydrogens=0;sem.showLabel=true;d.setAtomSemantics(metal,sem);d.setAtomLabel(metal,"Pd");sem=d.atoms().alive(ligand)->semantics;sem.nodeType=uint8_t(NodeType::Query);sem.implicitHydrogens=0;sem.showLabel=true;d.setAtomSemantics(ligand,sem);d.setAtomLabel(ligand,"R"+std::to_string(i+1));d.extensions(ObjectRef(ObjectKind::Atom,metal)).set("studio.color.override","32768");nodes.push_back(metal);}
 auto original=d;std::vector<ObjectRef> made;require(arrange(d,nodes,Vec2(290,270),145,125,true,&made).ok()&&made.size()==4,"four editable connectors");
 for(int i=0;i<4;++i){require(std::abs(length(d.atoms().alive(nodes[i])->position-d.atoms().alive(d.neighbours(nodes[i])[0])->position)-22)<1e-7,"layout preserves internal bond lengths");arrows::ArrowParams p;bool ok=false;arrows::readArrow(d,made[i].id,&p,&ok);require(ok&&!p.mechanism&&p.electronSource.id==nodes[i],"reaction connectors are distinct from electron arrows");}
 ObjectId reagent;d.addAtom(Vec2(490,120),6,&reagent);d.setAtomLabel(reagent,"RZnX");auto sem=d.atoms().alive(reagent)->semantics;sem.nodeType=uint8_t(NodeType::Query);sem.showLabel=true;sem.implicitHydrogens=0;d.setAtomSemantics(reagent,sem);ObjectId branch;require(link(d,ObjectRef(ObjectKind::Atom,reagent),made[0],"in",.3,&branch).ok(),"entering reagent docks on cycle curve");
 demo.refresh();auto before=d.digest();arrows::ArrowParams old;bool valid=false;arrows::readArrow(d,made[0].id,&old,&valid);auto id=nodes[0];auto result=demo.submit(Command("移动循环中间体",kInvalidateGeometry,[id](CommandContext& c){auto e=integration::translateDocumentObjects(c.document,component(c.document,id),Vec2(12,-16));return e.ok()?CommandResult::success():CommandResult::fail(e.code,e.message);}),"移动循环中间体");require(result.ok(),"move bound intermediate");arrows::ArrowParams moved;arrows::readArrow(d,made[0].id,&moved,&valid);require(length(moved.begin-old.begin)>1,"connector follows intermediate");auto after=d.digest();require(demo.undo().ok()&&d.digest()==before&&demo.undo(true).ok()&&d.digest()==after,"linked motion is one undo and redo");
 std::vector<ObjectRef> reused;require(arrange(d,nodes,Vec2(290,270),140,120,false,&reused).ok()&&reused==made,"reverse layout reuses connector identities and branches");
 for(auto ext:{std::string("cdxml"),std::string("r62doc")}){auto path=dir+"/catalytic-demo."+ext;require(demo.save(path,ext=="cdxml").ok(),"save "+ext);Session reopened;require(reopened.open(path).ok(),"reopen "+ext);int links=0;for(const auto& a:reopened.doc.arrows().records())if(a.isAlive()&&reopened.doc.extensions(ObjectRef(ObjectKind::Arrow,a.id)).findValue(key())){arrows::ArrowParams p;bool ok=false;arrows::readArrow(reopened.doc,a.id,&p,&ok);require(ok&&p.electronSource.valid()&&p.electronTarget.valid(),"file refs rebound");++links;}require(links==5,"all five connectors persist");require(update(reopened.doc).ok(),"reopened connectors remain editable");}
 app.s.cancel();app.s.doc=d;app.s.selected.clear();app.s.history.clear();app.s.refresh();app.s.fit();app.invalidate();app.screenshot(dir+"/catalytic-demo.png",false,false);
 std::ofstream(dir+"/catalytic-checks.json")<<"{\"cycle_nodes\":4,\"reagent_branches\":1,\"real_editable_structures\":true,\"bond_lengths_preserved\":true,\"live_follow\":true,\"undo_redo\":true,\"native_round_trip\":true,\"cdxml_references\":true}";
}
