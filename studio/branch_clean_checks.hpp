#pragma once
static void checkBranchAndClean(App& app,const std::string& dir){
    auto require=[](bool ok,const char* why){if(!ok)throw std::runtime_error(std::string("branch/clean: ")+why);};
    auto reset=[&](const Document& d){app.s.cancel();app.s.doc=d;app.s.history.clear();app.s.selected.clear();app.s.input.bindDocument(app.s.doc.id());app.s.refresh();};
    Document d;d.setDefaultBondLength(30);std::vector<ObjectId> ring;for(int i=0;i<6;++i){ObjectId id;double t=i*3.141592653589793/3;d.addAtom(Vec2(130+30*std::cos(t),100+30*std::sin(t)),6,&id);ring.push_back(id);}for(int i=0;i<6;++i){ObjectId b;d.addBond(ring[i],ring[(i+1)%6],&b);if(i%2==0)d.setBondOrder(b,BondOrder::Double);}
    ObjectId root,o,n,n2,attachment,co;d.addAtom(Vec2(160,150),6,&root);d.addAtom(Vec2(190,143),8,&o);d.addAtom(Vec2(139,174),7,&n);d.addAtom(Vec2(111,164),7,&n2);d.addBond(ring[1],root,&attachment);d.addBond(root,o,&co);d.setBondOrder(co,BondOrder::Double);d.addBond(root,n,nullptr);d.addBond(n,n2,nullptr);
    auto sem=d.atoms().alive(n)->semantics;sem.implicitHydrogens=1;d.setAtomSemantics(n,sem);sem.implicitHydrogens=2;d.setAtomSemantics(n2,sem);
    std::vector<ObjectRef> branch;for(auto id:{root,o,n,n2})branch.push_back({ObjectKind::Atom,id});studio::branch_direction::Plan plan;
    require(studio::branch_direction::resolve(d,branch,&plan).ok(),"resolve selected acyl branch");
    auto incomplete=branch;incomplete.erase(incomplete.begin());studio::branch_direction::Plan autoRoot;require(studio::branch_direction::resolve(d,incomplete,&autoRoot).ok()&&autoRoot.atoms==plan.atoms,"marquee missing common carbon resolves attachment");
    studio::branch_direction::Plan bondPlan;require(studio::branch_direction::resolve(d,{{ObjectKind::Bond,attachment}},&bondPlan).ok()&&bondPlan.atoms==plan.atoms,"attachment bond chooses smaller independent branch");
    require(!studio::branch_direction::resolve(d,{{ObjectKind::Bond,0}},nullptr).ok(),"ring bond rejected");
    reset(d);app.s.selected=branch;auto before=app.s.doc.digest();auto u=d.atoms().alive(root)->position-d.atoms().alive(ring[1])->position;u=u*(1./length(u));
    app.s.selected.clear();app.s.fit();app.invalidate();app.screenshot(dir+"/branch-direction-before.png");app.s.selected=branch;
    app.execute(Button("testBranch","换向","","@branchDirection"));require(app.s.doc.digest()!=before,"context action executes branch flip");
    for(auto id:ring)require(app.s.doc.atoms().alive(id)->position==d.atoms().alive(id)->position,"main skeleton fixed");require(app.s.doc.atoms().alive(root)->position==d.atoms().alive(root)->position,"attachment root fixed");
    for(auto id:{o,n,n2}){auto a=d.atoms().alive(id)->position-d.atoms().alive(root)->position,b=app.s.doc.atoms().alive(id)->position-d.atoms().alive(root)->position;require(std::abs(dot(a,u)-dot(b,u))<1e-8&&std::abs((a.x+b.x)*u.y-(a.y+b.y)*u.x)<1e-8,"reflect branch about attachment axis");require(app.s.doc.atoms().alive(id)->semantics.charge==d.atoms().alive(id)->semantics.charge,"charge preserved");}
    for(const auto& b:d.bonds().records())require(std::abs(length(d.atoms().alive(b.beginAtom)->position-d.atoms().alive(b.endAtom)->position)-length(app.s.doc.atoms().alive(b.beginAtom)->position-app.s.doc.atoms().alive(b.endAtom)->position))<1e-8,"bond lengths preserved");
    auto after=app.s.doc.digest();require(app.s.undo().ok()&&app.s.doc.digest()==before&&app.s.undo(true).ok()&&app.s.doc.digest()==after,"branch flip atomic undo and redo");
    app.s.selected.clear();app.s.fit();app.invalidate();app.screenshot(dir+"/branch-direction.png");require(app.s.save(dir+"/branch-direction.cdxml",true).ok(),"branch save");Session reopened;require(reopened.open(dir+"/branch-direction.cdxml").ok(),"branch CDXML reload");
    app.s.selected=branch;app.execute(Button("testBranch","换向","","@branchDirection"));for(auto id:{root,o,n,n2})require(length(app.s.doc.atoms().alive(id)->position-d.atoms().alive(id)->position)<1e-8,"second flip restores geometry");
    auto all=app.s.all();auto stable=app.s.doc.digest();app.s.selected=all;app.execute(Button("testBranch","换向","","@branchDirection"));require(app.s.doc.digest()==stable,"whole structure rejected without mutation");
    // Reflection changes the displayed wedge to preserve absolute stereochemistry.
    ObjectId wedge=d.bondBetween(n,n2);d.setBondDisplay(wedge,BondDisplay::Wedge);d.setBondStereo(wedge,BondStereo::Up);reset(d);require(app.s.submit(studio::branch_direction::command(plan),"换向").ok(),"stereo flip");require(app.s.doc.bonds().alive(wedge)->display==BondDisplay::WedgeHash&&app.s.doc.bonds().alive(wedge)->stereo==BondStereo::Down,"stereo depiction inverted under reflection");
    HMENU menu=CreatePopupMenu();appendSelectionContextMenu(menu,true,true);wchar_t title[64]={};GetMenuStringW(menu,kContextBranchDirectionId,title,64,MF_BYCOMMAND);require(std::wstring(title)==studio_i18n::tr(L"换向")&&(GetMenuState(menu,kContextBranchDirectionId,MF_BYCOMMAND)&MF_GRAYED)==0,"right click label and enabled state");DestroyMenu(menu);menu=CreatePopupMenu();appendSelectionContextMenu(menu,true,false);require((GetMenuState(menu,kContextBranchDirectionId,MF_BYCOMMAND)&MF_GRAYED)!=0,"no selection disables branch action");DestroyMenu(menu);
    // Verify chemical identity, not just wedge pixels, on an actual stereocentre.
    std::string chiralXml;require(chemservice::run(app.h,"(2S)-2-aminopropanoic acid","chemical/x-name","text/xml","convert",&chiralXml),"load chiral acceptance molecule");Session chiral;require(persistence::readCdxml(chiralXml,chiral.doc).ok(),"import chiral molecule");reset(chiral.doc);
    ObjectId methyl=kInvalidObjectId;for(const auto& atom:app.s.doc.atoms().records())if(atom.isAlive()&&atom.element==6&&app.s.doc.incidentBonds(atom.id).size()==1)methyl=atom.id;require(methyl!=kInvalidObjectId,"find chiral branch attachment");
    std::vector<ObjectRef> chiralBranch;for(const auto& atom:app.s.doc.atoms().records())if(atom.isAlive()&&atom.id!=methyl)chiralBranch.push_back({ObjectKind::Atom,atom.id});studio::branch_direction::Plan chiralPlan;require(studio::branch_direction::resolve(app.s.doc,chiralBranch,&chiralPlan).ok(),"resolve chiral branch");
    auto name=[&](){std::string xml,out;require(studioio::exportXml(app.s.doc,&xml).ok(),"export chiral structure");require(chemservice::run(app.h,xml,"text/xml","chemical/x-name","convert",&out),"name chiral structure");return out;};auto nameBefore=name();require(app.s.submit(studio::branch_direction::command(chiralPlan),"换向").ok()&&name()==nameBefore,"branch flip preserves absolute stereochemical name");
    // An exocyclic C=C is a bridge. Select just the two outer arms to also
    // exercise the same marquee inference used when their shared carbon is missed.
    Document alkene;
    ObjectId left,right,up,down,ar,lower,axis;
    alkene.addAtom(Vec2(110,100),6,&left);alkene.addAtom(Vec2(140,100),6,&right);
    alkene.addAtom(Vec2(155,74),8,&up);alkene.addAtom(Vec2(155,126),1,&down);
    alkene.addAtom(Vec2(95,74),6,&ar);alkene.addAtom(Vec2(95,126),6,&lower);
    alkene.addBond(left,right,&axis);alkene.setBondOrder(axis,BondOrder::Double);
    alkene.addBond(right,up,nullptr);alkene.addBond(right,down,nullptr);
    alkene.addBond(left,ar,nullptr);alkene.addBond(left,lower,nullptr);
    std::vector<ObjectId> opposite{left,right,ar,lower};
    auto extend=[&](ObjectId start,Vec2 point,int element,BondOrder order=BondOrder::Single){
        ObjectId atom,bond;alkene.addAtom(point,element,&atom);alkene.addBond(start,atom,&bond);alkene.setBondOrder(bond,order);
        opposite.push_back(atom);return atom;
    };
    extend(ar,Vec2(110,48),8,BondOrder::Double);
    const auto upperLeft=extend(ar,Vec2(65,74),6);
    const auto farLeft=extend(upperLeft,Vec2(50,100),6,BondOrder::Double);
    const auto lowerLeft=extend(farLeft,Vec2(65,126),6);
    ObjectId ringEdge;alkene.addBond(lowerLeft,lower,&ringEdge);alkene.setBondOrder(ringEdge,BondOrder::Double);
    const ObjectRef doubleBond(ObjectKind::Bond,axis);
    alkene.extensions(doubleBond).set("studio.bond.unspecifiedEZ","yes");
    reset(alkene);before=app.s.doc.digest();
    const std::vector<ObjectRef> alkeneSide{{ObjectKind::Atom,up},{ObjectKind::Atom,down}};
    app.s.selected=alkeneSide;
    app.execute(Button("testBranch","换向","","@branchDirection"));
    require(app.s.doc.atoms().alive(up)->position==Vec2(155,126)&&
            app.s.doc.atoms().alive(down)->position==Vec2(155,74),"selected alkene arms swap sides");
    for(auto id:opposite)require(app.s.doc.atoms().alive(id)->position==alkene.atoms().alive(id)->position,"C=C and opposite ring stay fixed");
    require(app.s.doc.bonds().alive(axis)->order==BondOrder::Double&&!static_cast<const Document&>(app.s.doc)
            .extensions(doubleBond).findValue("studio.bond.unspecifiedEZ"),"flip gives the double bond explicit drawn geometry");
    after=app.s.doc.digest();
    require(app.s.undo().ok()&&app.s.doc.digest()==before&&app.s.undo(true).ok()&&app.s.doc.digest()==after,"alkene switch has one undo and redo");
    require(app.s.save(dir+"/alkene-reoriented.cdxml",true).ok(),"save switched alkene geometry");
    Document unsupported=d;
    unsupported.setBondOrder(attachment,BondOrder::Triple);
    require(!studio::branch_direction::resolve(unsupported,branch,nullptr).ok(),"triple attachment rejected");
    unsupported.setBondOrder(attachment,BondOrder::Double);
    require(!studio::branch_direction::resolve(unsupported,branch,nullptr).ok(),"cumulated double attachment rejected");
    // Two independent hydrazines overlap the ring after normalization.
    d.setBondDisplay(wedge,BondDisplay::Normal);d.setBondStereo(wedge,BondStereo::None);ObjectId a,b,c,e,fixed;d.addAtom(Vec2(150,143),7,&a);d.addAtom(Vec2(175,153),7,&b);d.addBond(a,b,nullptr);d.addAtom(Vec2(150,143),7,&c);d.addAtom(Vec2(175,153),7,&e);d.addBond(c,e,nullptr);for(auto id:{a,b,c,e}){auto h=d.atoms().alive(id)->semantics;h.implicitHydrogens=2;d.setAtomSemantics(id,h);}auto protonated=d.atoms().alive(a)->semantics;protonated.charge=1;protonated.implicitHydrogens=3;d.setAtomSemantics(a,protonated);d.extensions(ObjectRef(ObjectKind::Atom,a)).set("pst.attr.MoleculeStudioChargePosition","1.4 0");d.extensions(ObjectRef(ObjectKind::Atom,a)).set("pst.attr.MoleculeStudioChargeCircle","yes");d.addAtom(Vec2(250,165),35,&fixed);
    std::vector<ObjectId> selected;for(const auto& atom:d.atoms().records())if(atom.isAlive()&&atom.id!=fixed)selected.push_back(atom.id);reset(d);
    auto separate=[selected](CommandContext& ctx){auto e=studio::clean_spacing::separate(ctx.document,selected);return e.ok()?CommandResult::success():CommandResult::fail(e.code,e.message);};before=app.s.doc.digest();require(app.s.submit(Command("整理结构",kInvalidateGeometry,separate),"整理结构").ok(),"pack selected components");
    auto parts=studio::clean_spacing::components(app.s.doc,selected,nullptr);for(size_t i=0;i<parts.size();++i)for(size_t j=0;j<i;++j)require(!studio::clean_spacing::overlaps(parts[i].box,parts[j].box,app.s.doc.defaultBondLength()*.6),"glyph extents of separate structures do not overlap");
    for(auto id:ring)require(app.s.doc.atoms().alive(id)->position==d.atoms().alive(id)->position,"principal skeleton stationary");require(app.s.doc.atoms().alive(fixed)->position==d.atoms().alive(fixed)->position,"unselected structure fixed");require(length((app.s.doc.atoms().alive(b)->position-app.s.doc.atoms().alive(a)->position)-(d.atoms().alive(b)->position-d.atoms().alive(a)->position))<1e-8,"small molecule moves rigidly");
    after=app.s.doc.digest();require(studio::clean_spacing::separate(app.s.doc,selected).ok()&&app.s.doc.digest()==after,"packing is idempotent");require(app.s.undo().ok()&&app.s.doc.digest()==before&&app.s.undo(true).ok()&&app.s.doc.digest()==after,"packing atomic undo redo");
    // Partial selections retain every fixed atom in that connected component.
    Document partial=d;std::set<ObjectId> movable(selected.begin(),selected.end());movable.erase(ring[0]);require(studio::clean_spacing::separate(partial,selected,&movable).ok(),"partial clean spacing");for(auto id:ring)require(partial.atoms().alive(id)->position==d.atoms().alive(id)->position,"partial component not translated");
    reset(d);app.s.fit();app.invalidate();app.screenshot(dir+"/multi-clean-before.png");for(auto id:selected)app.s.selected.push_back({ObjectKind::Atom,id});before=app.s.doc.digest();require(app.chemistryAction("clean"),"actual multi-structure clean action");require(app.s.doc.digest()!=before,"backend clean succeeded");parts=studio::clean_spacing::components(app.s.doc,selected,nullptr);for(size_t i=0;i<parts.size();++i)for(size_t j=0;j<i;++j)require(!studio::clean_spacing::overlaps(parts[i].box,parts[j].box,app.s.doc.defaultBondLength()*.6),"actual cleanup separates rendered molecules");require(app.s.doc.atoms().alive(fixed)->position==d.atoms().alive(fixed)->position,"actual clean leaves unselected atom fixed");after=app.s.doc.digest();require(app.s.undo().ok()&&app.s.doc.digest()==before&&app.s.undo(true).ok()&&app.s.doc.digest()==after,"actual clean and packing share one undo step");
    app.s.selected.clear();app.s.fit();app.invalidate();app.screenshot(dir+"/multi-clean-spacing.png");require(app.s.save(dir+"/multi-clean-spacing.cdxml",true).ok(),"save spaced structures");
    std::ofstream(dir+"/branch-clean-checks.json")<<"{\"context_menu\":true,\"partial_branch_reflection\":true,\"fixed_scaffold\":true,\"stereo\":true,\"undo_redo\":true,\"cdxml\":true,\"glyph_aware_spacing\":true,\"rigid_translation\":true,\"fixed_unselected\":true,\"partial_clean\":true,\"idempotent\":true,\"actual_clean_action\":true}";
}
