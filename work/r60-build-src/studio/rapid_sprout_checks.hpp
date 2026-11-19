#pragma once
static void checkRapidSproutHotkeys(App& app,const std::string& dir){
    auto& s=app.s;int cases=0;
    auto require=[](bool ok,const std::string& why){if(!ok)throw std::runtime_error("rapid single bond: "+why);};
    auto reset=[&](const Document& d){s.cancel();s.doc=d;s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());s.activateCommand("chooseSelectionTool_Marquee");app.rapidSprout.clear();app.down=false;app.pointerOverCanvas=true;s.refresh();};
    auto press=[&](ObjectId root){auto view=s.view();view.center=s.doc.atoms().alive(root)->position;view.scale=4;view.dpiScale=1;s.setView(view);auto p=view.toDevice(view.center);SendMessageW(app.canvas,WM_MOUSEMOVE,0,MAKELPARAM(int(std::lround(p.x)),int(std::lround(p.y))));auto count=s.doc.stats().aliveAtoms;require(app.shortcut('1',0,false),"real 1 shortcut route");require(s.doc.stats().aliveAtoms==count+1,"each 1 adds exactly one atom");require(app.rapidSprout.anchor==root,"hovered atom remains the burst anchor");};
    auto checkLength=[&](ObjectId root){for(auto tip:app.rapidSprout.tips)require(std::abs(length(s.doc.atoms().alive(tip)->position-s.doc.atoms().alive(root)->position)-s.doc.defaultBondLength())<1e-6,"fixed new bond lengths");};
    auto angles=[&](ObjectId root){std::vector<double> result;for(auto id:app.rapidSprout.tips)result.push_back(angleOf(s.doc.atoms().alive(id)->position-s.doc.atoms().alive(root)->position));return result;};
    auto at=[&](const std::vector<double>& a,double angle){for(double x:a)if(r62::sprout_layout::gap(x,angle)<1e-6)return true;return false;};
    Document gallery;gallery.setDefaultBondLength(25);std::vector<ObjectRef> galleryRefs;
    auto capture=[&](Vec2 offset){auto refs=s.all();require(studioio::append(s.doc,gallery,refs,offset,&galleryRefs).ok(),"gallery fixture");};
    for(double unit:{20.,32.}){
        Document d;d.setDefaultBondLength(unit);ObjectId root;d.addAtom(Vec2(),6,&root);reset(d);
        press(root);press(root);const auto two=s.doc.digest();press(root);auto a=angles(root);require(at(a,90)&&at(a,210)&&at(a,330),"isolated three-bond Y layout");checkLength(root);auto three=s.doc.digest();require(s.undo().ok()&&s.doc.digest()==two&&s.undo(true).ok()&&s.doc.digest()==three,"reflow and third bond undo/redo atomically");
        // Undo/redo interrupts the burst; restore an explicit fresh state only
        // for this independent fourth-bond geometry fixture.
        auto tips=app.rapidSprout.tips;app.rapidSprout.record(s.doc,root,GetTickCount64(),s.serial,s.history.cursor(),tips);
        if(unit==20)capture(Vec2(40,30));press(root);a=angles(root);require(at(a,90)&&at(a,210)&&at(a,330)&&at(a,270),"isolated fourth bond is the downward stem");checkLength(root);if(unit==20)capture(Vec2(110,30));++cases;
    }
    for(int kind:{1,5,6})for(double rotation:{0.,.63})for(double mirror:{-1.,1.}){
        Document d;d.setDefaultBondLength(25);std::vector<ObjectId> fixed;ObjectId root;
        auto pose=[&](Vec2 p){p.x*=mirror;double c=std::cos(rotation),t=std::sin(rotation);return Vec2(c*p.x-t*p.y,t*p.x+c*p.y);};
        if(kind==1){ObjectId parent;d.addAtom(Vec2(),6,&root);d.addAtom(pose(Vec2(-25,0)),6,&parent);d.setAtomLabel(parent,"R");d.addBond(root,parent,nullptr);fixed={root,parent};}
        else{double r=25/(2*std::sin(3.141592653589793/kind));for(int i=0;i<kind;++i){ObjectId id;double t=i*6.283185307179586/kind;d.addAtom(pose(Vec2(r*std::sin(t),r-r*std::cos(t))),6,&id);fixed.push_back(id);}root=fixed[0];for(int i=0;i<kind;++i)d.addBond(fixed[i],fixed[(i+1)%kind],nullptr);}
        reset(d);if(rotation==0&&mirror==1)capture(Vec2(40,kind==1?280:kind==5?100:180));press(root);const auto one=s.doc.digest();auto first=s.doc.atoms().alive(app.rapidSprout.tips.front())->position;
        if(rotation==0&&mirror==1)capture(Vec2(110,kind==1?280:kind==5?100:180));
        press(root);checkLength(root);auto a=angles(root);auto axis=kind==1?angleOf(pose(Vec2(1,0))):angleOf(pose(Vec2(0,-1)));const double fan=kind==1?60.:45.;require(at(a,axis-fan)&&at(a,axis+fan),"parent-relative symmetric fan for chain and ring");
        for(auto id:fixed)require(s.doc.atoms().alive(id)->position==d.atoms().alive(id)->position,"original parent and ring are untouched");
        auto after=s.doc.digest();require(s.undo().ok()&&s.doc.digest()==one&&s.doc.atoms().alive(app.rapidSprout.tips.front())->position==first&&s.undo(true).ok()&&s.doc.digest()==after,"second press and first-leaf rotation undo together");
        if(rotation==0&&mirror==1)capture(Vec2(180,kind==1?280:kind==5?100:180));
        if(kind==1){auto tips=app.rapidSprout.tips;app.rapidSprout.record(s.doc,root,GetTickCount64(),s.serial,s.history.cursor(),tips);press(root);a=angles(root);require(at(a,axis-60)&&at(a,axis+60)&&at(a,axis),"third branch adds the outward stem");if(rotation==0&&mirror==1)capture(Vec2(250,280));}
        ++cases;
    }
    // Inclusive 1-second boundary, same anchor, same document and same undo
    // position. Any intervening edit invalidates the movable-leaf list.
    {Document d;ObjectId root,tip;d.addAtom(Vec2(),6,&root);d.addAtom(Vec2(20,0),6,&tip);studio::rapid_sprout::State state;state.record(d,root,10000,7,3,{tip});
        require(state.continues(d,root,10999,7,3)&&state.continues(d,root,11000,7,3)&&!state.continues(d,root,11001,7,3),"999/1000/1001 ms boundary");require(!state.continues(d,tip,10500,7,3)&&!state.continues(d,root,10500,8,3)&&!state.continues(d,root,10500,7,2),"different anchor, edit, undo break the burst");Document other;require(!state.continues(other,root,10500,7,3),"new document breaks burst");}
    for(int interruption:{0,1,2,3}){
        Document d;ObjectId root,parent,other;d.addAtom(Vec2(),6,&root);d.addAtom(Vec2(-25,0),6,&parent);d.addBond(root,parent,nullptr);d.addAtom(Vec2(100,100),6,&other);d.setDefaultBondLength(25);reset(d);press(root);const auto tip=app.rapidSprout.tips.front();const auto original=s.doc.atoms().alive(tip)->position;
        if(interruption==0)app.rapidSprout.tick=GetTickCount64()-1001;
        else if(interruption==1){press(other);}
        else if(interruption==2){require(s.submit(Command("test unrelated edit",kInvalidateGeometry,[other](CommandContext& c){auto e=c.document.setAtomPosition(other,Vec2(110,100));return e.ok()?CommandResult::success():CommandResult::fail(e.code,e.message);}),"test unrelated edit").ok(),"intervening edit");}
        else{require(s.undo().ok()&&s.undo(true).ok(),"undo redo interruption");}
        press(root);require(app.rapidSprout.tips.size()==1&&s.doc.atoms().alive(tip)->position==original,"nonconsecutive press does not move a previous branch");++cases;
    }
    {Document d;ObjectId root,parent,bond;d.addAtom(Vec2(),6,&root);d.addAtom(Vec2(-25,0),6,&parent);d.addBond(root,parent,&bond);d.setBondDisplay(bond,BondDisplay::Wedge);reset(d);press(root);auto tip=app.rapidSprout.tips.front();auto point=s.doc.atoms().alive(tip)->position;press(root);require(s.doc.atoms().alive(tip)->position==point&&s.doc.bonds().alive(bond)->display==BondDisplay::Wedge,"stereochemical frame is never reflowed");++cases;}
    {Document d;ObjectId root,old,obstacle;d.setDefaultBondLength(25);d.addAtom(Vec2(),6,&root);d.addAtom(Vec2(-25,0),6,&old);d.addBond(root,old,nullptr);d.addAtom(direction(60)*25,6,&obstacle);reset(d);press(root);press(root);for(auto tip:app.rapidSprout.tips)require(length(s.doc.atoms().alive(tip)->position-s.doc.atoms().alive(obstacle)->position)>=25*.28-1e-5,"fan avoids existing obstacle");require(s.doc.atoms().alive(obstacle)->position==d.atoms().alive(obstacle)->position,"obstacle remains fixed");++cases;}
    reset(gallery);s.selected.clear();s.fit();app.invalidate();require(s.save(dir+"/rapid-sprout-gallery.cdxml",true).ok(),"save gallery");app.screenshot(dir+"/rapid-sprout-gallery.png");
    std::ofstream(dir+"/rapid-sprout-checks.json")<<"{\"cases\":"<<cases<<",\"interval_ms\":1000,\"real_1_shortcut\":true,\"three_four_bond_junctions\":true,\"five_six_rings\":true,\"rotation_mirror_scale\":true,\"original_frame_fixed\":true,\"timeout_interruption\":true,\"undo_redo\":true,\"stereo_protected\":true,\"obstacle_avoidance\":true}";
}
