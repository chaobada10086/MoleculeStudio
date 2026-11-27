#pragma once
#include "engine/chair_layout.hpp"
static void checkChairLayout(App& app,const std::string& dir){
    auto require=[](bool ok,const std::string& why){if(!ok)throw std::runtime_error("chair layout: "+why);};
    auto make=[&](Session& s,rings_chains::RingPresetId preset,Vec2 center,double rotation,bool explicitRotation){
        rings_chains::RingRequest request;request.preset=preset;request.pointer=center;request.rotationGiven=explicitRotation;request.rotationDeg=rotation;
        rings_chains::RingPlacement placement;require(rings_chains::resolveRingPlacement(s.doc,request,&placement).ok()&&placement.valid,"resolve chair");
        std::vector<ObjectId> ids;require(s.submit(rings_chains::commands::createRingCommand(placement,&ids,nullptr),"chair fixture").ok(),"draw chair");return ids;
    };
    int cases=0;
    for(auto preset:{rings_chains::RingPresetId::Chair1,rings_chains::RingPresetId::Chair2})for(double angle:{0.,30.,90.,173.})for(double unit:{14.4,24.}){
        Session s;s.doc.setDefaultBondLength(unit);const auto ids=make(s,preset,Vec2(90,80),angle,angle!=0);
        auto first=s.doc.atoms().alive(ids.front())->position;Box box(first,first);for(auto id:ids)box.add(s.doc.atoms().alive(id)->position);
        if(angle==0)require((box.max.x-box.min.x)/(box.max.y-box.min.y)>2.6,"chair is shallow and readable");
        for(size_t i=0;i<ids.size();++i){
            auto f=chair_layout::frame(s.doc,ids[i]);require(f.valid&&f.ring.size()==6,"recognize all six carbons");
            Vec2 axis=core::direction(angle+90)*(i%2?-1.:1.);if(preset==rings_chains::RingPresetId::Chair2)axis=axis*-1;
            require(dot(axis,f.axial)>.999,"alternating axial frame follows chair rotation and flip");
            require(std::abs(dot(f.axial,f.equatorial))<.83,"axial and equatorial remain distinct");
        }
        const ObjectId host=ids[3];const auto frame=chair_layout::frame(s.doc,host);const Vec2 origin=s.doc.atoms().alive(host)->position;
        s.activateCommand("chooseBondTool_Solid");const auto before=s.doc.digest();Vec2 axialEnd;
        for(int k=0;k<2;++k){
            const auto expected=chair_layout::target(s.doc,host,Vec2(),true);if(k)axialEnd=expected.end;require(expected.available&&expected.axial==(k==1),"equatorial first, then axial");
            require(s.gesture(PointerPhase::Down,origin).ok(),"chair bond press");
            require(!s.ghost.items().empty()&&length(s.ghost.items().back().points.back()-expected.end)<1e-7,"preview matches chair target");
            require(s.gesture(PointerPhase::Up,origin).ok(),"chair bond click");
            require(length(s.doc.atoms().records().back().position-expected.end)<1e-7,"commit uses exact projected target");
        }
        require(!chair_layout::target(s.doc,host,Vec2(),true).available,"both valences occupied");
        require(s.undo().ok()&&s.undo().ok()&&s.doc.digest()==before,"two bonds undo exactly");
        // Rapid 1-key bursts share the same equatorial-first slot policy.
        auto added=std::make_shared<studio::rapid_sprout::Addition>();
        require(s.submit(studio::rapid_sprout::command(host,{},added),"rapid chair").ok(),"first rapid chair bond");auto eqId=added->atom;auto eq=s.doc.atoms().alive(eqId)->position;
        require(length(eq-(origin+frame.equatorial*(unit*.65)))<1e-7,"rapid first is equatorial");
        require(s.submit(studio::rapid_sprout::command(host,{eqId},added),"rapid chair").ok(),"second rapid chair bond");
        require(s.doc.atoms().alive(eqId)->position==eq&&!added->arranged,"rapid second never reflows first chair bond");
        require(length(s.doc.atoms().alive(added->atom)->position-axialEnd)<1e-7,"rapid second is axial");
        require(s.undo().ok()&&s.undo().ok()&&s.doc.digest()==before,"rapid chair burst undo");
        ObjectId axial;s.doc.addAtom(origin+frame.axial*(unit*.5),6,&axial);s.doc.addBond(host,axial,nullptr);
        require(s.submit(studio::rapid_sprout::command(host,{},added),"rapid chair").ok()&&length(s.doc.atoms().alive(added->atom)->position-(origin+frame.equatorial*(unit*.65)))<1e-7,"existing axial prefers new equatorial");
        require(s.undo().ok()&&s.doc.removeAtom(axial).ok(),"restore chair fixture");s.cancel();s.refresh();
        const Vec2 pointer=origin+frame.axial*unit;
        require(s.gesture(PointerPhase::Down,origin).ok()&&s.gesture(PointerPhase::Move,pointer).ok(),"axial drag");
        const Vec2 expected=axialEnd;
        require(length(s.bondSnap.endpoint-expected)<1e-7&&!s.bondSnap.joined,"drag snaps to axial rather than ring vertex");
        require(s.gesture(PointerPhase::Up,pointer).ok()&&s.undo().ok(),"axial drag one undo");
        const std::string path=dir+"/chair-case-"+std::to_string(cases)+".cdxml";require(s.save(path,true).ok(),"chair save");Session back;require(back.open(path).ok(),"chair reopen");
        require(chair_layout::frame(back.doc,host).valid,"chair attachment frame survives standard coordinates");++cases;
    }
    {
        Session regular;auto ids=make(regular,rings_chains::RingPresetId::Cyclohexane,Vec2(50,50),0,false);
        for(auto id:ids)require(!chair_layout::frame(regular.doc,id).valid,"regular hexagon keeps ordinary bond logic");
    }
    for(auto preset:{rings_chains::RingPresetId::Chair1,rings_chains::RingPresetId::Chair2})for(double angle:{0.,43.,90.}){
        Session s;ObjectId a,b,edge;s.doc.addAtom(Vec2(80,80),6,&a);s.doc.addAtom(Vec2(80,80)+core::direction(angle)*14.4,6,&b);s.doc.addBond(a,b,&edge);
        const auto pa=s.doc.atoms().alive(a)->position,pb=s.doc.atoms().alive(b)->position;
        rings_chains::RingRequest request;request.preset=preset;request.mode=rings_chains::RingAttachMode::FuseOnBond;request.anchorBond=edge;request.pointer=Vec2(100,120);
        rings_chains::RingPlacement placement;require(rings_chains::resolveRingPlacement(s.doc,request,&placement).ok()&&placement.valid,"fused chair placement");
        require(std::abs(length(placement.geometry.vertices[1].local-placement.geometry.vertices[0].local)-14.4)<1e-7,"fused chair shared-edge scale");
        require(s.submit(rings_chains::commands::createRingCommand(placement,nullptr,nullptr),"fused chair").ok(),"fused chair commit");
        require(s.doc.atoms().alive(a)->position==pa&&s.doc.atoms().alive(b)->position==pb,"fusion keeps original endpoints");
        require(chair_layout::frame(s.doc,a).valid&&chair_layout::frame(s.doc,b).valid,"fusion retains complete chair projection");
    }
    Session gallery;gallery.doc.setDefaultBondLength(30);
    for(int col=0;col<2;++col){
        auto ids=make(gallery,col?rings_chains::RingPresetId::Chair1:rings_chains::RingPresetId::Chair2,Vec2(55+col*110,55),0,false);
        auto f=chair_layout::frame(gallery.doc,ids[3]);auto origin=gallery.doc.atoms().alive(ids[3])->position;
        for(int k=0;k<2;++k){Vec2 ray=k?f.axial:f.equatorial;ObjectId atom,bond;gallery.doc.addAtom(origin+ray*(30*(k?.5:.65)),k==(col?0:1)?6:1,&atom);gallery.doc.addBond(ids[3],atom,&bond);if(k==(col?0:1))gallery.doc.setAtomLabel(atom,"R");}
    }
    gallery.refresh();require(gallery.save(dir+"/chair-axial-equatorial.cdxml",true).ok(),"reference gallery export");
    std::string bytes;persistence::readFileBytes(dir+"/chair-axial-equatorial.cdxml",&bytes);std::string binary,why;require(cdxbinary::fromXml(bytes,binary,why),"chair CDX export");std::ofstream(dir+"/chair-axial-equatorial.cdx",std::ios::binary).write(binary.data(),binary.size());
    app.s.cancel();app.s.doc=gallery.doc;app.s.history.clear();app.s.selected.clear();app.s.input.bindDocument(app.s.doc.id());app.s.refresh();app.s.fit();app.invalidate();app.screenshot(dir+"/chair-axial-equatorial.png");
    std::ofstream(dir+"/chair-layout-checks.json")<<"{\"rotated_scaled_chair_cases\":"<<cases<<",\"six_axial_positions\":true,\"equatorial_and_axial_clicks\":true,\"axial_drag_preview_commit\":true,\"undo\":true,\"cdxml_reopen\":true,\"regular_hexagon_unchanged\":true}";
}
