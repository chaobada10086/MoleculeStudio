#pragma once
static void checkMechanismGestureRouting(App& app,const std::string& dir){
    auto& s=app.s;auto require=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};
    for(int side:{-1,1}){
        s.cancel();s.doc=Document();s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());
        ObjectId a,b;s.doc.addAtom(Vec2(60,90),8,&a);s.doc.addAtom(Vec2(160,90),7,&b);
        s.refresh();auto view=s.view();view.scale=2;s.setView(view);app.chooseArrowPreset(49);
        require(s.gesture(PointerPhase::Down,Vec2(60,90)).ok()&&s.gesture(PointerPhase::Move,Vec2(110,90+side*35)).ok()&&s.gesture(PointerPhase::Move,Vec2(160,90)).ok()&&s.gesture(PointerPhase::Up,Vec2(160,90)).ok(),"draw side-directed gesture");
        arrows::ArrowParams p;bool ok=false;arrows::readArrow(s.doc,0,&p,&ok);double bow=0;for(auto q:p.route)bow+=q.y;
        require(ok&&p.mechanismSide==side&&bow*side>0,"clear arrow follows the upper/lower mouse gesture");
        auto encoded=arrows::encodeArrowParams(p);auto decoded=arrows::decodeArrowParams(encoded,&ok);require(ok&&decoded.mechanismSide==side,"gesture direction round trips");
        auto digest=s.doc.digest();require(s.move({ObjectRef(ObjectKind::Atom,a)},Vec2(20,15)).ok(),"move source to rebuild arrow");
        arrows::ArrowParams moved;arrows::readArrow(s.doc,0,&moved,&ok);Document freshDoc=s.doc;freshDoc.removeObject(ObjectRef(ObjectKind::Arrow,0));
        Vec2 from,to;arrows::mechanismAnchorPoint(freshDoc,p.electronSource,&from);arrows::mechanismAnchorPoint(freshDoc,p.electronTarget,&to);
        auto fresh=arrows::resolvePreset(49,from,to,.6);fresh.mechanism=true;fresh.mechanismSide=side;fresh.arcSagitta=length(to-from)*.22*side;fresh.electronSource=p.electronSource;fresh.electronTarget=p.electronTarget;fresh.sourceVisualAnchor=p.sourceVisualAnchor;fresh.targetVisualAnchor=p.targetVisualAnchor;
        fresh.mechanismDeparture=p.mechanismDeparture;fresh.mechanismArrival=p.mechanismArrival;
        arrows::routeMechanism(freshDoc,fresh);arrows::clearMechanismLabels(freshDoc,fresh);
        require(length(fresh.begin-moved.begin)<1e-6&&length(fresh.end-moved.end)<1e-6,"moved endpoints match a newly generated arrow");
        auto x=arrows::routedPoints(fresh),y=arrows::routedPoints(moved);require(x.size()==y.size(),"rebuilt curve has the same samples as a new curve");for(size_t i=0;i<x.size();++i)require(length(x[i]-y[i])<1e-6,"rebuilt curvature matches new generation");
        require(s.save(dir+"/gesture-routing-"+std::to_string(side)+".cdxml",true).ok(),"save gesture demo");s.selected.clear();s.fit();app.invalidate();app.screenshot(dir+"/gesture-routing-"+std::to_string(side)+".png");
        require(s.undo().ok()&&s.doc.digest()==digest,"rebuilding remains one undo");
    }
    // A real obstacle overrides the gesture preference.
    {
        Document flowing;auto curve=arrows::resolvePreset(49,Vec2(40,70),Vec2(180,70),.6);curve.mechanism=true;
        arrows::routeMechanism(flowing,curve);auto points=arrows::routedPoints(curve);double positive=0,negative=0;
        for(size_t i=1;i+1<points.size();++i){auto a=points[i]-points[i-1],b=points[i+1]-points[i];double turn=a.x*b.y-a.y*b.x;positive=std::max(positive,turn);negative=std::min(negative,turn);}
        require(positive>1e-6&&negative<-1e-6,"long mechanism arrow has a smooth inflection rather than constant circular curvature");
        ObjectId id;arrows::addArrow(flowing,curve,.6,&id);s.doc=flowing;s.refresh();s.selected.clear();s.fit();app.invalidate();app.screenshot(dir+"/long-flowing-curve.png");s.save(dir+"/long-flowing-curve.cdxml",true);
    }
    {
        // Reference return: turn near the electron source, then approach the
        // target along a substantially straighter final leg. Compare curvature
        // per unit arc length, independently of Bezier sample spacing.
        for(double scale:{.8,1.,1.5})for(double rotation:{0.,90.}){
            Document empty;auto u=core::direction(rotation);auto rotate=[&](Vec2 v){return Vec2(v.x*u.x-v.y*u.y,v.x*u.y+v.y*u.x);};
            auto curve=arrows::resolvePreset(49,rotate(Vec2(97,102)*scale),rotate(Vec2(72,65)*scale),.6);curve.mechanism=true;curve.mechanismSide=-1;curve.arcSagitta=-10*scale;
            auto chord=curve.end-curve.begin;double span=length(chord);auto local=[&](Vec2 v){v=rotate(v);return Vec2(dot(v,chord)/span,(chord.x*v.y-chord.y*v.x)/span);};
            curve.mechanismDeparture=local(Vec2(-.8,.6));curve.mechanismArrival=local(Vec2(-.08,-std::sqrt(1-.08*.08)));
            arrows::routeMechanism(empty,curve);auto points=arrows::routedPoints(curve);double total=0;for(size_t i=1;i<points.size();++i)total+=length(points[i]-points[i-1]);
            double run=0,frontTurn=0,frontLength=0,endTurn=0,endLength=0;
            for(size_t i=1;i+1<points.size();++i){auto a=points[i]-points[i-1],b=points[i+1]-points[i];double la=length(a),lb=length(b);run+=la;if(la<1e-8||lb<1e-8)continue;double turn=std::acos(std::max(-1.,std::min(1.,dot(a,b)/(la*lb))));
                if(run/total<.45){frontTurn+=turn;frontLength+=(la+lb)*.5;}if(run/total>.70){endTurn+=turn;endLength+=(la+lb)*.5;}}
            require(frontLength>0&&endLength>0&&frontTurn/frontLength>2.*endTurn/endLength,"reference return bends more strongly near its source than near its head");
            if(scale==1.&&rotation==0.){ObjectId id;arrows::addArrow(empty,curve,.6,&id);s.doc=empty;s.selected.clear();s.refresh();s.fit();app.invalidate();s.save(dir+"/eased-return.cdxml",true);app.screenshot(dir+"/eased-return.png");std::ofstream(dir+"/eased-return.json")<<"{\"front_curvature\":"<<frontTurn/frontLength<<",\"end_curvature\":"<<endTurn/endLength<<",\"cases\":6}";}
        }
    }
    Document doc;auto p=arrows::resolvePreset(49,Vec2(30,80),Vec2(150,80),.6);p.mechanism=true;p.mechanismSide=-1;p.arcSagitta=-26.4;
    ObjectId block;doc.addAtom(Vec2(90,54),8,&block);arrows::routeMechanism(doc,p);
    require(arrows::mechanismRouteScore(p,arrows::mechanismObstacles(doc),true)<1,"gesture routing clears an obstructing glyph");
    std::ofstream(dir+"/mechanism-gesture-checks.json")<<"{\"both_gesture_sides\":true,\"direction_roundtrip\":true,\"fresh_rebuild_equivalence\":true,\"undo\":true,\"obstacle_clearance\":true}";
    // Reproduce the reported crowded base/H-O drawing using real pointer input.
    s.cancel();s.doc=Document();s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());
    ObjectId base,h,o,c,ho,oc;s.doc.addAtom(Vec2(65,75),5,&base);auto sem=s.doc.atoms().alive(base)->semantics;sem.charge=-1;sem.implicitHydrogens=0;s.doc.setAtomSemantics(base,sem);
    s.doc.addAtom(Vec2(150,100),1,&h);s.doc.addAtom(Vec2(164,108),8,&o);s.doc.addAtom(Vec2(164,123),6,&c);s.doc.addBond(h,o,&ho);s.doc.addBond(o,c,&oc);
    ObjectId ring[6];ring[0]=c;for(int i=1;i<6;++i){double t=i*3.141592653589793/3.;s.doc.addAtom(Vec2(164+20*std::sin(t),143-20*std::cos(t)),6,&ring[i]);}for(int i=0;i<6;++i)s.doc.addBond(ring[i],ring[(i+1)%6],nullptr);
    ObjectId r1,r2,r3,r4;s.doc.addAtom(Vec2(198.641,123),6,&r1);s.doc.addAtom(Vec2(215.962,133),6,&r2);s.doc.addAtom(Vec2(215.962,153),6,&r3);s.doc.addAtom(Vec2(198.641,163),6,&r4);s.doc.addBond(ring[1],r1,nullptr);s.doc.addBond(r1,r2,nullptr);s.doc.addBond(r2,r3,nullptr);s.doc.addBond(r3,r4,nullptr);s.doc.addBond(r4,ring[2],nullptr);
    s.refresh();auto v=s.view();v.scale=3;s.setView(v);app.chooseArrowPreset(49);auto charge=arrows::mechanismChargeBox(s.doc,*s.doc.atoms().alive(base));auto start=(charge.min+charge.max)*.5;
    require(s.gesture(PointerPhase::Down,start).ok()&&s.gesture(PointerPhase::Move,Vec2(100,start.y)).ok()&&s.gesture(PointerPhase::Move,Vec2(135,start.y)).ok()&&s.gesture(PointerPhase::Move,Vec2(150,85)).ok()&&s.gesture(PointerPhase::Up,Vec2(150,100)).ok(),"draw reported long base-to-H gesture");
    s.selected.clear();require(s.gesture(PointerPhase::Down,Vec2(157,104)).ok()&&s.gesture(PointerPhase::Move,Vec2(158,96)).ok()&&s.gesture(PointerPhase::Up,Vec2(164,108)).ok(),"draw crowded H-O return by pointer");
    arrows::ArrowParams local;bool valid=false;arrows::readArrow(s.doc,1,&local,&valid);double height=0;for(auto q:local.route)height=std::max(height,std::abs(q.y));require(valid&&height>.70,"crowded manual H-O return retains a tall arch");
    s.selected.clear();s.refresh();s.fit();app.invalidate();require(s.save(dir+"/reported-base-ho.cdxml",true).ok(),"save reported drawing regression");app.screenshot(dir+"/reported-base-ho.png");
}
