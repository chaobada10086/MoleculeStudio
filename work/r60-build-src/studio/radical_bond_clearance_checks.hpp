#pragma once
static void checkRadicalBondClearance(App& app,const std::string& dir){
    auto require=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};auto& s=app.s;int cases=0;
    for(double turn:{0.,30.,90.,180.,270.})for(bool labelled:{false,true}){
        s.cancel();s.doc=Document();s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());ObjectId c;s.doc.addAtom(Vec2(80,80),6,&c);
        for(double angle:{-90.,30.,150.}){ObjectId n;s.doc.addAtom(Vec2(80,80)+core::direction(angle+turn)*25,angle==30?8:16,&n);s.doc.addBond(c,n,nullptr);}
        auto sem=s.doc.atoms().alive(c)->semantics;sem.radical=uint8_t(Radical::Doublet);sem.implicitHydrogens=0;sem.showLabel=labelled;s.doc.setAtomSemantics(c,sem);if(labelled)s.doc.setAtomLabel(c,"C");
        auto preferred=core::direction(-90.+turn)*.8;std::ostringstream value;value<<preferred.x<<' '<<preferred.y;s.doc.extensions(ObjectRef(ObjectKind::Atom,c)).set("pst.attr.MoleculeStudioRadicalPosition",value.str());s.refresh();auto before=s.doc.digest();
        auto dots=arrows::mechanismRadicalPoints(s.doc,c);require(dots.size()==1,"single carbon radical remains one dot");auto radial=dots[0]-s.doc.atoms().alive(c)->position;
        require(dot(radial,core::direction(90.+turn))/length(radial)>.999999,"blocked radical chooses the opposite C-S/C-O bisector");
        for(auto bid:s.doc.incidentBonds(c)){auto b=s.doc.bonds().alive(bid);auto other=s.doc.atoms().alive(b->beginAtom==c?b->endAtom:b->beginAtom);require(mechanism::step_detail::pointSegment(dots[0],s.doc.atoms().alive(c)->position,other->position)>arrows::mechanismRadicalRadius(s.doc,c)+.5,"radical dot has visible clearance from bonds");}
        require(s.doc.digest()==before,"collision repair is a non-mutating display operation");++cases;
    }
    for(double turn:{0.,30.,90.,180.,270.})for(bool stored:{false,true}){
        s.cancel();s.doc=Document();s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());
        std::vector<Vec2> points{{0,-12.5},{0,12.5},{-21.650635,-25},{-43.30127,-12.5},{-43.30127,12.5},{-21.650635,25},{21.650635,-25},{43.30127,-12.5},{43.30127,12.5},{21.650635,25}};std::vector<ObjectId> ids;
        auto rotate=[&](Vec2 p){auto u=core::direction(turn);return Vec2(p.x*u.x-p.y*u.y,p.x*u.y+p.y*u.x);};
        for(auto p:points){ObjectId id;s.doc.addAtom(Vec2(100,100)+rotate(p),6,&id);ids.push_back(id);}
        for(auto edge:std::vector<std::pair<int,int>>{{0,1},{0,2},{2,3},{3,4},{4,5},{5,1},{0,6},{6,7},{7,8},{8,9},{9,1}})s.doc.addBond(ids[edge.first],ids[edge.second],nullptr);
        for(int index:{0,1}){auto sem=s.doc.atoms().alive(ids[index])->semantics;sem.radical=uint8_t(Radical::Doublet);sem.implicitHydrogens=0;s.doc.setAtomSemantics(ids[index],sem);if(stored)s.doc.extensions(ObjectRef(ObjectKind::Atom,ids[index])).set("pst.attr.MoleculeStudioRadicalPosition","auto");}
        s.refresh();auto before=s.doc.digest();for(int index:{0,1}){auto dots=arrows::mechanismRadicalPoints(s.doc,ids[index]);auto radial=dots.at(0)-s.doc.atoms().alive(ids[index])->position;require(dot(radial,rotate(Vec2(0,index?1:-1)))/length(radial)>.999999,"fused-ring bridgehead radical points outside the rings after rotation");require(length(radial)>=5.4&&length(radial)<=6.,"automatic skeletal radical remains close to its atom");++cases;}require(before==s.doc.digest(),"ring-aware layout never changes the molecule");
        if(turn==0&&stored){s.fit();require(s.save(dir+"/radical-structural-direction.cdxml",true).ok(),"save bridgehead automatic directions");Session reopen;require(reopen.open(dir+"/radical-structural-direction.cdxml").ok(),"reopen automatic radical directions");for(int index:{0,1}){auto p=arrows::mechanismRadicalPoints(reopen.doc,ids[index]).at(0);require((p.y-reopen.doc.atoms().alive(ids[index])->position.y)*(index?1:-1)>0,"automatic direction survives save/reopen");}}
    }
    std::ofstream(dir+"/radical-bond-clearance.json")<<"{\"cases\":"<<cases<<",\"bisector\":true,\"no_bond_overlap\":true,\"ring_exterior\":true,\"rotates_with_structure\":true,\"non_mutating\":true}";
}
