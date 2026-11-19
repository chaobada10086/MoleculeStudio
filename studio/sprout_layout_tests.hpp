#pragma once
static void sproutLayoutCheck(App&,const std::string& dir){
    auto require=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    using namespace r62;
    for(int flip:{-1,1})for(const char* tool:{"chooseBondTool_Solid","chooseBondTool_Wedged","chooseBondTool_Hashed_Wedged"}){
        Session v;v.doc.setDefaultBondLength(20);ObjectId center;v.doc.addAtom(Vec2(),6,&center);
        for(double deg:{30.,150.,270.}){ObjectId a,b;auto p=core::direction(deg)*20;p.y*=flip;v.doc.addAtom(p,6,&a);v.doc.addBond(center,a,&b);}
        v.refresh();auto before=v.doc.digest();require(v.activateCommand(tool),"angular methyl bond tool");
        require(v.gesture(PointerPhase::Down,Vec2()).ok()&&length(v.bondSnap.endpoint-Vec2(0,-flip*20))<1e-7,"angular methyl previews along the vertical stem");
        auto preview=v.bondSnap.endpoint;
        require(v.gesture(PointerPhase::Up,Vec2()).ok()&&length(v.doc.atoms().alive(4)->position-preview)<1e-7,"angular methyl commits vertically with the requested bond style");
        if(flip==1&&std::string(tool)=="chooseBondTool_Solid")require(v.save(dir+"/angular-methyl.cdxml",true).ok(),"save vertical angular methyl");
        require(v.undo().ok()&&v.doc.digest()==before,"angular methyl creation is one undo");
    }
    for(int z:{6,7})for(bool drag:{false,true}){
        Session v;v.doc.setDefaultBondLength(20);ObjectId a,b,ab;
        v.doc.addAtom(Vec2(),z,&a);v.doc.addAtom(Vec2(-20,0),6,&b);v.doc.addBond(a,b,&ab);v.doc.setBondOrder(ab,BondOrder::Double);v.refresh();
        const auto before=v.doc.digest();v.activateCommand("chooseMultipleBondsTool_DoubleBond");
        require(v.gesture(PointerPhase::Down,Vec2()).ok(),"cumulene press");
        const Vec2 release=drag?Vec2(18,7):Vec2();
        require(v.gesture(PointerPhase::Move,release).ok()&&length(v.bondSnap.endpoint-Vec2(20,0))<1e-7,"second double previews straight even from neutral N");
        require(v.doc.digest()==before,"cumulene preview does not mutate document");
        require(v.gesture(PointerPhase::Up,release).ok()&&length(v.doc.atoms().alive(2)->position-Vec2(20,0))<1e-7,"second double commits at preview");
        if(z==7)require(v.doc.atoms().alive(a)->semantics.charge==1,"straight diazo nitrogen automatically positive");
        require(v.undo().ok()&&v.doc.digest()==before,"straight double drawing undoes together");
    }
    {
        Session v;ObjectId a,b,c,ab,ac,tail,bt;
        v.doc.addAtom(Vec2(),7,&a);v.doc.addAtom(Vec2(-20,0),6,&b);v.doc.addAtom(Vec2(19,-3),7,&c);v.doc.addAtom(Vec2(-40,10),6,&tail);
        v.doc.addBond(a,b,&ab);v.doc.setBondOrder(ab,BondOrder::Double);v.doc.addBond(a,c,&ac);v.doc.addBond(b,tail,&bt);v.refresh();
        const auto before=v.doc.digest();
        require(v.submit(chemistry::setBondOrderWithRefreshCommand(ac,BondOrder::Double),"化学键级").ok(),"double order edit straightens diazo group");
        require(v.doc.atoms().alive(b)->position==Vec2(-20,0)&&v.doc.atoms().alive(tail)->position==Vec2(-40,10)&&v.doc.atoms().alive(c)->position==Vec2(studio::diazo::nitrogenLength(v.doc),0),"keep scaffold and align outer N at equal bond length");
        require(v.save(dir+"/aligned-diazo.cdxml",true).ok(),"save aligned diazo sample");
        require(v.undo().ok()&&v.doc.digest()==before,"geometry charge and bond order undo atomically");
    }
    {
        Session v;v.activateCommand("chooseRingTool_Cyclohexane");const auto before=v.doc.digest();
        require(v.gesture(PointerPhase::Down,Vec2()).ok(),"six ring preview");const auto preview=v.ghost.items();
        require(v.gesture(PointerPhase::Up,Vec2()).ok()&&v.doc.stats().aliveAtoms==6,"six ring placement");
        double low=1e9,high=-1e9;int vertical=0;
        for(const auto& a:v.doc.atoms().records())if(a.isAlive()){low=std::min(low,a.position.y);high=std::max(high,a.position.y);}
        for(const auto& b:v.doc.bonds().records())if(b.isAlive()){
            auto p=v.doc.atoms().alive(b.beginAtom)->position,q=v.doc.atoms().alive(b.endAtom)->position;if(std::abs(p.x-q.x)<1e-7)++vertical;
        }
        require(vertical==2&&std::abs(high-low-2*v.doc.defaultBondLength())<1e-7,"default six ring turned thirty degrees with vertical sides");
        require(!preview.empty()&&v.undo().ok()&&v.doc.digest()==before,"ring preview and undo remain available");
    }
    for(const char* tool:{"chooseRingTool_Cyclopentane","chooseRingTool_Cyclopentadiene"}){
        Session v;v.activateCommand(tool);const auto before=v.doc.digest();
        require(v.gesture(PointerPhase::Down,Vec2()).ok()&&v.doc.digest()==before,"upright pentagon preview leaves document unchanged");
        require(v.gesture(PointerPhase::Up,Vec2()).ok()&&v.doc.stats().aliveAtoms==5,"upright pentagon placement");
        double top=1e9,bottom=-1e9;for(const auto& a:v.doc.atoms().records())if(a.isAlive()){top=std::min(top,a.position.y);bottom=std::max(bottom,a.position.y);}
        int apex=0,base=0;for(const auto& a:v.doc.atoms().records())if(a.isAlive()&&std::abs(a.position.y-top)<1e-7){++apex;require(std::abs(a.position.x)<1e-7,"pentagon apex centered above origin");}
        for(const auto& b:v.doc.bonds().records())if(b.isAlive()){auto p=v.doc.atoms().alive(b.beginAtom)->position,q=v.doc.atoms().alive(b.endAtom)->position;if(std::abs(p.y-bottom)<1e-7&&std::abs(q.y-bottom)<1e-7)++base;require(std::abs(length(p-q)-v.doc.defaultBondLength())<1e-7,"pentagon keeps regular bond lengths");}
        require(apex==1&&base==1,"pentagon points up with a horizontal bottom edge");
        if(std::string(tool)=="chooseRingTool_Cyclopentane")require(v.save(dir+"/upright-pentagon.cdxml",true).ok(),"save upright pentagon");
        require(v.undo().ok()&&v.doc.digest()==before,"upright pentagon undo");
        rings_chains::RingRequest request;request.preset=rings_chains::RingPresetId::Cyclopentane;request.rotationGiven=true;request.rotationDeg=0.;rings_chains::RingPlacement placement;
        require(rings_chains::resolveRingPlacement(v.doc,request,&placement).ok()&&std::abs(placement.rotationDeg)<1e-7,"explicit ring angle remains respected");
    }
    // The reported crowded side chain, in screen-oriented coordinates. Check
    // both its terminal carbon and the branch carbon against the WHOLE ring.
    std::vector<Vec2> fixture={{180,92},{251,133},{251,214},{180,255},{110,214},{110,133},{322,255},{393,214},{268,269}};
    std::vector<std::pair<int,int>> links={{0,1},{1,2},{2,3},{3,4},{4,5},{5,0},{2,6},{6,7},{6,8}};
    int cases=0;double worst=1e9;
    for(int rotation=0;rotation<360;rotation+=15)for(int mirror:{-1,1})for(double L:{14.4,32.})for(int anchor:{6,8}){
        Session s;auto& d=s.doc;d.setDefaultBondLength(L);double t=rotation*3.141592653589793/180.;
        for(auto p:fixture){p=(p-fixture[anchor])*(L/82.);p.x*=mirror;Vec2 q(p.x*cos(t)-p.y*sin(t),p.x*sin(t)+p.y*cos(t));ObjectId id;d.addAtom(q+Vec2(42,-17),6,&id);}
        for(auto edge:links){ObjectId id;d.addBond(edge.first,edge.second,&id);if(edge.first<6&&edge.second<6&&edge.first%2==0)d.setBondOrder(id,BondOrder::Double);}
        s.refresh();if(rotation==0&&mirror==1&&L==14.4&&anchor==6)s.save(dir+"/reported_before.cdxml",true);auto before=d.digest();auto choice=sprout_layout::resolve(d,anchor);require(choice.ok,"reported layout has a safe automatic direction");
        auto origin=d.atoms().get(anchor)->position,tip=origin+core::direction(choice.directionDeg)*L;
        // Independent geometric assertions, not the candidate scoring function.
        for(const auto& a:d.atoms().records())if(a.isAlive()&&a.id!=ObjectId(anchor)){
            double distance=length(a.position-tip)/L;worst=std::min(worst,distance);require(distance>.28,"new atom avoids existing atoms");
        }
        for(auto edge:links)if(edge.first!=anchor&&edge.second!=anchor){
            Vec2 a=d.atoms().get(edge.first)->position,b=d.atoms().get(edge.second)->position;
            double x=sprout_layout::cross(tip-origin,a-origin),y=sprout_layout::cross(tip-origin,b-origin);
            double z=sprout_layout::cross(b-a,origin-a),w=sprout_layout::cross(b-a,tip-a);
            require(!(x*y< -1e-8&&z*w< -1e-8),"new bond does not cross existing bond");
        }
        require(s.activateCommand("chooseBondTool_Solid"),"activate solid bond");
        require(s.gesture(PointerPhase::Down,origin,0).ok(),"safe automatic preview");require(d.digest()==before,"preview does not change document");
        auto preview=s.bondSnap.endpoint;require(length(preview-tip)<1e-7,"preview uses collision checked endpoint");
        require(s.gesture(PointerPhase::Up,origin,0).ok(),"commit automatic bond");
        require(d.stats().aliveAtoms==10&&d.stats().aliveBonds==10,"automatic growth adds one atom and one bond without joining another atom");
        require(length(d.atoms().get(9)->position-preview)<1e-7,"committed bond matches preview without angle rounding");
        auto after=d.digest();require(s.undo().ok()&&d.digest()==before,"safe growth is one undo");require(s.undo(true).ok()&&d.digest()==after,"safe growth redo exact");
        if(rotation==0&&mirror==1&&L==14.4){require(s.save(dir+"/reported_anchor_"+std::to_string(anchor)+".cdxml",true).ok(),"save reported regression");Session reopened;require(reopened.open(dir+"/reported_anchor_"+std::to_string(anchor)+".cdxml").ok()&&reopened.doc.stats().aliveAtoms==10,"reopen corrected structure");}
        ++cases;
    }
    // A long bond can cross the entire new segment even with distant endpoints.
    {
        Document d;d.setDefaultBondLength(20);ObjectId a,b,c,e,bond;d.addAtom(Vec2(),6,&a);d.addAtom(Vec2(-20,0),6,&b);d.addBond(a,b,&bond);
        d.addAtom(Vec2(10,-100),6,&c);d.addAtom(Vec2(10,100),6,&e);d.addBond(c,e,&bond);
        auto result=sprout_layout::resolve(d,a);require(result.ok,"long crossing obstacle has an alternative");
        require((core::direction(result.directionDeg)*20).x<10-3.6,"avoid long bond even when endpoints are distant");
    }
    // Growing from a ring vertex must go outside, including rings large enough
    // that an inward bond would stop before crossing any edge.
    for(int count:{6,12,18}){
        Document d;d.setDefaultBondLength(20);double radius=20/(2*sin(3.141592653589793/count));
        for(int i=0;i<count;++i){ObjectId id;d.addAtom(core::direction(i*360./count)*radius,6,&id);}
        for(int i=0;i<count;++i){ObjectId id;d.addBond(i,(i+1)%count,&id);}
        auto result=sprout_layout::resolve(d,0);require(result.ok,"ring vertex has outward direction");
        require(core::direction(result.directionDeg).x>0,"automatic ring substituent goes outside ring");
    }
    // No silent collision when the entire neighbourhood is blocked.
    {
        Session s;s.doc.setDefaultBondLength(20);ObjectId source;s.doc.addAtom(Vec2(),6,&source);
        for(int i=0;i<36;++i){ObjectId id;s.doc.addAtom(core::direction(i*10.)*20,6,&id);}s.refresh();auto before=s.doc.digest();
        require(!sprout_layout::resolve(s.doc,source).ok,"enclosed atom has no safe automatic direction");
        s.activateCommand("chooseBondTool_Solid");s.gesture(PointerPhase::Down,Vec2(),0);
        require(!s.gesture(PointerPhase::Up,Vec2(),0).ok()&&s.doc.digest()==before,"blocked automatic click creates no overlapping bond");
    }
    // Linear chemistry is preserved; an obstacle on the only linear direction
    // refuses automatic growth rather than introducing an alkyne kink.
    for(bool blocked:{false,true}){
        Document d;d.setDefaultBondLength(20);ObjectId a,b,bond;d.addAtom(Vec2(),6,&a);d.addAtom(Vec2(-20,0),6,&b);d.addBond(a,b,&bond);d.setBondOrder(bond,BondOrder::Triple);
        if(blocked){ObjectId id;d.addAtom(Vec2(20,0),6,&id);}
        auto result=sprout_layout::resolve(d,a);require(blocked?!result.ok:result.ok&&sprout_layout::gap(result.directionDeg,0)<1e-7,"alkyne keeps straight continuation or refuses when blocked");
    }
    // Explicit dragging still allows intentional atom joins/ring closure.
    {
        Session s;s.doc.setDefaultBondLength(20);ObjectId a,b;
        const Vec2 origin(84,63),target(104,63);
        s.doc.addAtom(origin,6,&a);s.doc.addAtom(target,6,&b);s.refresh();
        auto view=s.view();view.scale=3;s.setView(view);
        s.activateCommand("chooseBondTool_Solid");
        require(s.gesture(PointerPhase::Down,origin,0).ok()&&s.gesture(PointerPhase::Move,target,0).ok(),"preview explicit atom join");
        require(s.bondSnap.target==ObjectRef(ObjectKind::Atom,b)&&s.ghost.items().size()==1&&
                s.ghost.items().front().points==std::vector<Vec2>({origin,target}),"joined bond preview has the exact two atom coordinates");
        require(s.gesture(PointerPhase::Up,target,0).ok(),"explicit drag join works");
        require(s.doc.stats().aliveAtoms==2&&s.doc.stats().aliveBonds==1,"explicit join reuses target atom");
        const auto middle=(origin+target)*.5;
        require(s.gesture(PointerPhase::Down,middle,0).ok()&&s.ghost.items().empty(),"pressing an existing bond does not preview an unrelated bond");
        require(s.gesture(PointerPhase::Up,middle,0).ok()&&s.doc.bonds().alive(0)->order==BondOrder::Double,
                "click changes only the existing bond");
    }
    for(bool fuse:{false,true})for(double zoom:{1.,4.}){
        Session s;s.doc.setDefaultBondLength(20);
        const Vec2 anchor(95,77),end(115,77);
        ObjectId a,b,edge;s.doc.addAtom(anchor,6,&a);s.doc.addAtom(end,6,&b);s.doc.addBond(a,b,&edge);
        s.refresh();auto view=s.view();view.scale=zoom;s.setView(view);
        for(const auto* command:{"chooseRingTool_Cyclopentane","chooseRingTool_Benzene","chooseRingTool_Chair_Cyclohexane1"}){
            require(s.activateCommand(command),"activate ring preview fixture");
            const auto press=fuse?(anchor+end)*.5:anchor+Vec2(.25,.25);
            const auto before=s.doc.digest();
            require(s.gesture(PointerPhase::Down,press,0).ok(),"press off-origin ring anchor");
            auto* tool=dynamic_cast<rings_chains::RingTool*>(s.entries[s.activeIndex].tool.get());
            require(tool&&tool->pendingPlacement().valid,"ring preview resolves");
            const auto plan=tool->pendingPlacement();
            require(plan.reusedAtomCount==(fuse?2:1),"ring preview captured the intended anchor kind");
            for(size_t i=0;i<plan.reuseAtom.size();++i)if(plan.reuseAtom[i]!=kInvalidObjectId)
                require(plan.worldPosition[i]==s.doc.atoms().alive(plan.reuseAtom[i])->position,"reused ring vertex uses its actual coordinate");
            auto checkPreview=[&](){
                require(!s.ghost.items().empty()&&s.doc.digest()==before,"preview is visible and does not edit the document");
                for(const auto& stroke:s.ghost.items())
                    require(stroke.points.size()==2&&length(stroke.points.back()-stroke.points.front())<30,
                            "no ring preview stroke extends to the canvas origin");
            };
            checkPreview();
            const auto release=press+Vec2(-3,4);
            require(s.gesture(PointerPhase::Move,release,0).ok(),"update ring preview");
            checkPreview();
            const auto preview=s.ghost.items();
            require(s.gesture(PointerPhase::Up,release,0).ok()&&s.ghost.items().empty(),"commit clears ring preview");
            require(s.doc.stats().aliveBonds==size_t(1+plan.createdBondCount),"commit matches ring preview topology");
            if(std::string(command)=="chooseRingTool_Cyclopentane")for(const auto& stroke:preview){
                bool found=false;
                for(const auto& bond:s.doc.bonds().records())if(bond.isAlive()&&bond.id!=edge){
                    const auto a=s.doc.atoms().alive(bond.beginAtom)->position,b=s.doc.atoms().alive(bond.endAtom)->position;
                    const auto p=stroke.points.front(),q=stroke.points.back();
                    found|=(length(p-a)<1e-6&&length(q-b)<1e-6)||(length(p-b)<1e-6&&length(q-a)<1e-6);
                }
                require(found,"ring preview stroke matches the committed edge");
            }
            require(s.undo().ok()&&s.doc.digest()==before,"undo restores the ring anchor fixture");
        }
    }
    for(const auto* command:{"chooseAlkaneTool_Snaking_Chain","chooseAlkaneTool_Acyclic_Chain"}){
        Session s;s.doc.setDefaultBondLength(20);ObjectId anchor;
        const Vec2 start(74,65),end(74,122);
        s.doc.addAtom(start,6,&anchor);s.refresh();
        require(s.activateCommand(command)&&s.gesture(PointerPhase::Down,start,0).ok()&&
                !s.ghost.items().empty()&&s.ghost.items().front().points.front()==start,"chain previews from the snapped atom immediately");
        require(s.gesture(PointerPhase::Move,end,0).ok(),"update chain preview");
        const auto preview=s.ghost.items();
        require(s.gesture(PointerPhase::Up,end,0).ok()&&s.doc.stats().aliveBonds==preview.size(),"chain preview matches committed bond count");
        for(const auto& bond:s.doc.bonds().records())if(bond.isAlive()){
            const auto& points=preview.at(bond.id).points;
            require(points.front()==s.doc.atoms().alive(bond.beginAtom)->position&&
                    points.back()==s.doc.atoms().alive(bond.endAtom)->position,"chain preview matches committed endpoints");
        }
    }
    std::ofstream(dir+"/sprout_layout_tests.json")<<"{\"reported_rotated_mirrored_scaled_cases\":"<<cases<<",\"minimum_atom_clearance_in_bond_lengths\":"<<worst<<",\"whole_segment_crossing\":true,\"ring_interior\":true,\"long_crossing_bond\":true,\"blocked_refusal\":true,\"linear_alkyne\":true,\"explicit_drag_join\":true,\"preview_matches_commit\":true,\"undo_redo\":true,\"save_reload\":true}";
}
