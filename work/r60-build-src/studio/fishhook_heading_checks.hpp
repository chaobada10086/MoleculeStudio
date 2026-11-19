#pragma once
static void checkFishhookHeading(App& app,const std::string& dir){
    auto require=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};auto& s=app.s;s.cancel();s.doc=Document();s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());int cases=0;
    for(double turn:{0.,90.,180.,270.})for(double sign:{-1.,1.})for(double span:{7.,30.}){
        auto u=core::direction(turn);auto p=arrows::resolvePreset(57,Vec2(60+cases%8*45,60+cases/8*65),Vec2(),.6);p.end=p.begin+u*span;p.arcSagitta=sign*span*.45;p.mechanism=true;
        auto routing=Document();arrows::finishAutomaticMechanismEndpoints(routing,p);arrows::orientAutomaticFishhook(p);
        arrows::ArrowGeometry geometry;require(arrows::buildArrowGeometry(p,.6,arrows::Tessellation(),&geometry).ok()&&geometry.heads.size()==1,"fishhook heading geometry");auto points=geometry.shafts.front();std::vector<Vec2> untrimmed;arrows::flattenArc(p.begin,p.end,p.arcSagitta,arrows::Tessellation(),&untrimmed);auto tangent=untrimmed.back()-untrimmed[untrimmed.size()-2];auto direction=core::direction(geometry.heads.front().angleDeg);require(dot(direction,tangent)/length(tangent)>.999,"fishhook head follows electron travel at tip");
        auto tip=geometry.heads.front().tip,wing=geometry.heads.front().outline[1];double closest=1e30;
        for(size_t i=1;i<points.size();++i){if(length(points[i]-tip)<1e-6)continue;require(!mechanism::step_detail::segmentsCross(wing,tip,points[i-1],points[i]),"outer fishhook barb never cuts across its own shaft");closest=std::min(closest,mechanism::step_detail::pointSegment(wing,points[i-1],points[i]));}
        require(closest>.5,"fishhook barb stays visibly separate from shaft");
        auto bent=p;auto oldSide=p.head.sides;bent.route.clear();bent.arcSagitta=-p.arcSagitta;arrows::orientAutomaticFishhook(bent);require(bent.head.sides!=oldSide,"bending an existing fishhook through the other side updates the outer barb");
        ObjectId id;arrows::addArrow(s.doc,p,.6,&id);++cases;
    }
    s.refresh();s.fit();app.invalidate();require(s.save(dir+"/outer-fishhook-heads.cdxml",true).ok(),"save outward fishhook heading matrix");
    std::ofstream(dir+"/fishhook-heading-checks.json")<<"{\"cases\":"<<cases<<",\"forward_tangent\":true,\"outer_barb_clear\":true}";
}
