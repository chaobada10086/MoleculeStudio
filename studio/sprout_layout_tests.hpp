#pragma once
static void sproutLayoutCheck(App&,const std::string& dir){
    auto require=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    using namespace r62;
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
        Session s;s.doc.setDefaultBondLength(20);ObjectId a,b;s.doc.addAtom(Vec2(),6,&a);s.doc.addAtom(Vec2(20,0),6,&b);s.refresh();
        s.activateCommand("chooseBondTool_Solid");s.gesture(PointerPhase::Down,Vec2(),0);require(s.gesture(PointerPhase::Up,Vec2(20,0),0).ok(),"explicit drag join works");
        require(s.doc.stats().aliveAtoms==2&&s.doc.stats().aliveBonds==1,"explicit join reuses target atom");
    }
    std::ofstream(dir+"/sprout_layout_tests.json")<<"{\"reported_rotated_mirrored_scaled_cases\":"<<cases<<",\"minimum_atom_clearance_in_bond_lengths\":"<<worst<<",\"whole_segment_crossing\":true,\"ring_interior\":true,\"long_crossing_bond\":true,\"blocked_refusal\":true,\"linear_alkyne\":true,\"explicit_drag_join\":true,\"preview_matches_commit\":true,\"undo_redo\":true,\"save_reload\":true}";
}
