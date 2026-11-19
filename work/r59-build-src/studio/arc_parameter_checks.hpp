#pragma once
static void checkArcParameterPastes(App& app,const std::string& dir){
    auto require=[](bool ok,const char* reason){if(!ok)throw std::runtime_error(std::string("arc parameters: ")+reason);};
    auto& scene=app.s;scene.cancel();scene.doc=Document();scene.history.clear();scene.selected.clear();scene.input.bindDocument(scene.doc.id());scene.arcBendLocked=scene.arcCurvatureLocked=false;
    auto read=[&](ObjectId id){arrows::ArrowParams p;bool valid=false;require(arrows::readArrow(scene.doc,id,&p,&valid).ok()&&valid,"read saved arrow");return p;};
    auto close=[](double a,double b){return std::abs(a-b)<1e-6;};auto angleClose=[&](double a,double b){return close(std::sin(a-b),0)&&close(std::cos(a-b),1);};int cases=0;
    for(double sag:{-120.,-20.,20.,120.}){
        auto p=arrows::resolvePreset(58,Vec2(20,30),Vec2(120,65),.6);p.arcSagitta=sag;
        auto q=arrows::resolvePreset(58,Vec2(230,240),Vec2(290,330),.6);q.arcSagitta=40;ObjectId source,target;require(arrows::addArrow(scene.doc,p,.6,&source).ok()&&arrows::addArrow(scene.doc,q,.6,&target).ok(),"fixtures");scene.refresh();auto original=arc_properties::read(q),want=arc_properties::read(p);
        for(auto pair:{std::make_pair(48,49),std::make_pair(50,51),std::make_pair(40,41),std::make_pair(44,45),std::make_pair(52,53)}){
            auto before=scene.doc.digest();app.arcCurvatureAction(source,pair.first,false);require(scene.doc.digest()==before,"copy must not change document");app.arcCurvatureAction(target,pair.second,false);auto after=scene.doc.digest();auto actual=arc_properties::read(read(target));require(actual.valid,"pasted circle is valid");
            require(length(actual.center-(pair.first==48?want.center:original.center))<1e-6,"other parameters: centre");
            require(close(actual.radius,(pair.first==50||pair.first==44)?want.radius:original.radius),"other parameters: radius/curvature");
            require(close(actual.sweep,pair.first==40?want.sweep:original.sweep),"other parameters: sweep");
            require(angleClose(actual.start,pair.first==52?want.start:original.start),"other parameters: canvas start angle");
            require(scene.undo().ok()&&scene.doc.digest()==before&&scene.undo(true).ok()&&scene.doc.digest()==after,"paste undo redo");require(scene.undo().ok(),"restore target between independent pastes");++cases;
        }
        app.arcCurvatureAction(source,42,false);scene.activateCommand("chooseArrowTool_FreeArc");scene.selected={ObjectRef(ObjectKind::Arrow,source)};
        auto mid=arrows::arcMidpoint(p.begin,p.end,p.arcSagitta);scene.gesture(PointerPhase::Down,mid);scene.gesture(PointerPhase::Move,(p.begin+p.end)*.5);scene.gesture(PointerPhase::Up,(p.begin+p.end)*.5);
        auto straight=read(source);require(straight.family==arrows::ArrowFamily::Straight&&close(straight.arcSagitta,0),"middle drag overrides stored lock through zero");
        require(scene.undo().ok(),"restore locked arc");
        auto end=p.end+Vec2(30,10);scene.gesture(PointerPhase::Down,p.end);scene.gesture(PointerPhase::Move,end);scene.gesture(PointerPhase::Up,end);
        auto locked=read(source);require(locked.family==arrows::ArrowFamily::Arc&&close(locked.arcSagitta/length(locked.end-locked.begin),p.arcSagitta/length(p.end-p.begin)),"endpoint resize retains bend lock");
        app.arcCurvatureAction(source,43,false);
    }
    std::ofstream(dir+"/arc-parameter-checks.json")<<"{\"independent_pastes\":"<<cases<<",\"other_parameters_preserved\":true,\"canvas_start_angle\":true,\"minor_major_both_directions\":true,\"undo_redo\":true,\"direct_bend_through_zero\":true,\"endpoint_bend_lock\":true}";
}
