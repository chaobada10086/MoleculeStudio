#pragma once
static void checkShortHookCurvature(App& app,const std::string& dir){
    auto& s=app.s;auto require=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};
    s.cancel();s.doc=Document();s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());
    size_t cases=0;double lowest=1e10,largestTurn=0;Session demo;
    for(auto preset:{49u,57u})for(double unit:{12.,18.,24.,30.})for(double rotation:{0.,30.,90.,180.})for(auto display:{BondDisplay::Normal,BondDisplay::Wedge}){
        s.doc=Document();
        Vec2 origin(70+(cases/2%8)*90,80+(cases/16)*120);auto transform=[&](Vec2 p){auto u=core::direction(rotation);return origin+Vec2(p.x*u.x-p.y*u.y,p.x*u.y+p.y*u.x);};
        ObjectId ring[6],oxygen,sourceBond;for(int i=0;i<6;++i){double angle=i*3.141592653589793/3.;require(s.doc.addAtom(transform(Vec2(unit*std::sin(angle),-unit*std::cos(angle))),6,&ring[i]).ok(),"short hook ring atom");}
        for(int i=0;i<6;++i)require(s.doc.addBond(ring[i],ring[(i+1)%6],nullptr).ok(),"short hook ring bond");
        require(s.doc.addAtom(transform(Vec2(0,-2*unit)),8,&oxygen).ok()&&s.doc.addBond(ring[0],oxygen,&sourceBond).ok()&&s.doc.setBondDisplay(sourceBond,display).ok(),"short hook hydroxyl fixture");
        Document routing=s.doc;std::vector<arrows::ArrowParams> hooks;
        for(auto target:{ring[0],oxygen}){
            auto source=ObjectRef(ObjectKind::Bond,sourceBond),destination=ObjectRef(ObjectKind::Atom,target);Vec2 from,to;arrows::mechanismAnchorPoint(s.doc,source,&from);arrows::mechanismAnchorPoint(s.doc,destination,&to);
            auto p=arrows::resolvePreset(preset,from,to,.6);p.mechanism=true;p.electronSource=source;p.electronTarget=destination;p.sourceVisualAnchor=5;
            auto digest=routing.digest();arrows::routeMechanism(routing,p);arrows::clearMechanismLabels(routing,p);require(routing.digest()==digest&&p.electronSource==source&&p.electronTarget==destination,"round return preserves chemistry and references");
            auto points=arrows::routedPoints(p);double radius=1e10,maxTurn=0;
            for(size_t i=1;i+1<points.size();++i){auto a=points[i]-points[i-1],b=points[i+1]-points[i];double x=length(a),y=length(b);if(x<1e-8||y<1e-8)continue;double angle=std::acos(std::max(-1.,std::min(1.,dot(a,b)/(x*y))));maxTurn=std::max(maxTurn,angle);if(angle>1e-5)radius=std::min(radius,(x+y)*.5/angle);}
            lowest=std::min(lowest,radius);largestTurn=std::max(largestTurn,maxTurn);
            if(!(radius>1.5&&maxTurn<.20))throw std::runtime_error("short return cusp preset="+std::to_string(preset)+" unit="+std::to_string(unit)+" rotation="+std::to_string(rotation)+" target="+std::to_string(target)+" radius="+std::to_string(radius)+" turn="+std::to_string(maxTurn));
            if(target==ring[0])require(radius>2.,"skeletal carbon return retains a readable turning radius");
            if(preset==49){
                arrows::ArrowGeometry geometry;require(arrows::buildArrowGeometry(p,.6,arrows::Tessellation(),&geometry).ok(),"render short return");
                const auto& outline=geometry.heads.front().outline;
                require(outline.size()==4,"electron-pair head has two swept wings and a recessed tail");
                auto base=(outline[1]+outline[3])*.5,axis=outline[0]-base;
                double notch=dot(outline[2]-base,axis)/dot(axis,axis);
                require(notch>.10&&notch<.18,"head has a shallow recess without long rear spikes");
                double neckGap=1e100;for(size_t j=1;j<points.size();++j)neckGap=std::min(neckGap,arrows::mechanismDistance(outline[2],points[j-1],points[j]));
                require(neckGap<geometry.strokeWidth*.5+.15,"head recess remains centered on the visible incoming curve");
                // Test the actual visible stroke: the old renderer passed the
                // route-radius test and then inserted a bent neck of its own.
                for(auto q:geometry.shafts[0]){double nearest=1e100;for(size_t j=1;j<points.size();++j)nearest=std::min(nearest,arrows::mechanismDistance(q,points[j-1],points[j]));require(nearest<.02,"rendered short shaft stays on the routed curve without a replacement neck");}
            }
            hooks.push_back(p);++cases;
        }
        for(auto p:hooks){ObjectId id;require(arrows::addArrow(s.doc,p,.6,&id).ok(),"save rounded hook fixture");}
        require(studioio::append(s.doc,demo.doc,s.all(),Vec2(),nullptr).ok(),"collect independent short-hook fixtures");
    }
    s.doc=demo.doc;s.refresh();s.selected.clear();s.fit();app.invalidate();require(s.save(dir+"/short-rounded-fishhooks.cdxml",true).ok(),"save short rounded fishhook matrix");app.screenshot(dir+"/short-rounded-fishhooks.png");
    std::ofstream(dir+"/short-hook-curvature.json")<<"{\"cases\":"<<cases<<",\"minimum_radius\":"<<lowest<<",\"maximum_turn\":"<<largestTurn<<"}";
    // The reported example: an oblique H-O bond above a wedge to carbon.
    s.doc=Document();s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());
    ObjectId h,o,c,ho,oc;s.doc.addAtom(Vec2(60,65),1,&h);s.doc.addAtom(Vec2(78,76),8,&o);s.doc.addAtom(Vec2(78,100),6,&c);s.doc.addBond(h,o,&ho);s.doc.addBond(o,c,&oc);s.doc.setBondDisplay(oc,BondDisplay::Wedge);
    Vec2 from,to;auto source=ObjectRef(ObjectKind::Bond,ho),target=ObjectRef(ObjectKind::Atom,o);arrows::mechanismAnchorPoint(s.doc,source,&from);arrows::mechanismAnchorPoint(s.doc,target,&to);
    auto p=arrows::resolvePreset(49,from,to,.6);p.mechanism=true;p.sourceVisualAnchor=5;p.electronSource=source;p.electronTarget=target;p.mechanismSide=-1;p.arcSagitta=-std::abs(p.arcSagitta);
    arrows::routeMechanism(s.doc,p);arrows::clearMechanismLabels(s.doc,p);double fullness=0;for(auto q:p.route)fullness=std::max(fullness,std::abs(q.y));
    require(fullness>.65,"oblique H-O return has a tall textbook arch rather than a shallow cap");
    ObjectId id;require(arrows::addArrow(s.doc,p,.6,&id).ok(),"save H-O arch fixture");s.refresh();s.fit();app.invalidate();s.save(dir+"/textbook-ho-arch.cdxml",true);app.screenshot(dir+"/textbook-ho-arch.png");
    std::ofstream(dir+"/textbook-ho-arch.json")<<"{\"arch_height_over_chord\":"<<fullness<<",\"rendered_shaft_preserves_curve\":true}";
}
