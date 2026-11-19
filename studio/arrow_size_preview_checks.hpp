#pragma once
static void arrowSizePreviewCheck(App& app,const std::string& dir){
    auto require=[](bool value,const char* message){if(!value)throw std::runtime_error(std::string("arrow size/preview: ")+message);};
    auto& s=app.s;auto reset=[&](){s.cancel();s.doc=Document();s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());s.fixedLen=s.fixedAngle=false;s.arcBendLocked=false;s.graphicsWidth=0;s.refresh();};
    reset();std::vector<ObjectId> ordinary;ObjectId mechanism;
    int row=0;for(uint32_t preset:{0u,2u,8u,10u,32u,38u,58u,49u}){auto p=arrows::resolvePreset(preset,Vec2(0,row*35),Vec2(130,row*35),.6);if(preset==49){p.mechanism=true;p.head.length=3.2;p.head.width=2.24;}ObjectId id;require(arrows::addArrow(s.doc,p,.6,&id).ok(),"create size fixture");if(preset==49)mechanism=id;else ordinary.push_back(id);++row;}
    // The same slab stores non-arrow graphics. This zero-length annotation
    // used to abort the entire transaction, leaving the menu tier unchanged.
    AnnotationRecord graphic;graphic.geometry[0]=graphic.geometry[1]=Vec2(180,20);ObjectId shape;
    require(s.doc.addAnnotation(ObjectKind::Arrow,graphic,&shape).ok(),"preserved graphic fixture");
    const std::string raw="<graphic GraphicType=\"Rectangle\" BoundingBox=\"180 20 210 40\"/>";
    s.doc.extensions(ObjectRef(ObjectKind::Arrow,shape)).set("studio.cdxml.graphic",raw);s.refresh();
    auto read=[&](ObjectId id){arrows::ArrowParams p;bool ok=false;require(arrows::readArrow(s.doc,id,&p,&ok).ok()&&ok,"read parameters");return p;};
    auto mechanismBefore=arrows::encodeArrowParams(read(mechanism));
    auto area=[&](){double total=0;for(const auto& p:s.scene.primitives())if(p.source==ObjectRef(ObjectKind::Arrow,ordinary.front())&&p.kind==render::PrimKind::Polygon){double twice=0;for(size_t i=0;i<p.points.size();++i){auto a=p.points[i],b=p.points[(i+1)%p.points.size()];twice+=a.x*b.y-a.y*b.x;}total+=std::abs(twice)*.5;}return total;};
    double previousArea=0;const double lengths[]={3.2,6,9,13.5,18};
    for(int tier=0;tier<5;++tier){auto before=s.doc.digest();app.setArrowHeadTier(tier,false);require(s.arrowHeadTier==tier,"tier updates in a mixed drawing");
        for(auto id:ordinary)require(std::abs(read(id).head.length-lengths[tier])<1e-8,"all ordinary arrows resized");
        require(arrows::encodeArrowParams(read(mechanism))==mechanismBefore,"mechanism remains unchanged");
        require(*s.doc.extensions(ObjectRef(ObjectKind::Arrow,shape)).findValue("studio.cdxml.graphic")==raw,"non-arrow graphic remains unchanged");
        double currentArea=area();require(currentArea>previousArea+1,"rendered head changes at every tier");previousArea=currentArea;
        auto after=s.doc.digest();require(s.undo().ok()&&s.doc.digest()==before,"size undo");require(s.undo(true).ok()&&s.doc.digest()==after,"size redo");
        require(s.save(dir+"/head-tier-"+std::to_string(tier)+".cdxml",true).ok(),"save size comparison");
    }
    int previewCases=0;
    for(size_t i=0;i<arrows::arrowPresetCount();++i){const auto& spec=arrows::arrowPresets()[i];if(spec.line!=arrows::ArrowLine::Dashed||!spec.implemented)continue;
        for(double width:{.4,.6,2.})for(int tier:{0,2,4}){reset();s.doc.setDefaultLineWidth(width);s.arrowHeadTier=tier;s.refresh();s.activateCommand(spec.sourceCommand);
            require(s.gesture(PointerPhase::Down,Vec2(0,0)).ok()&&s.gesture(PointerPhase::Move,Vec2(130,40)).ok(),"drag dashed arrow");
            auto tool=dynamic_cast<arrows::ArrowTool*>(s.entries[s.activeIndex].tool.get());require(tool&&tool->previewGeometry().dashOn>0,"dashed geometry");
            const auto geometry=tool->previewGeometry();int pieces=0;
            for(const auto& item:s.ghost.items())if(item.primitive==PreviewPrimitive::Polyline){double span=0;for(size_t j=1;j<item.points.size();++j)span+=length(item.points[j]-item.points[j-1]);require(span<=geometry.dashOn+1e-7,"preview contains individual dash pieces");++pieces;}
            require(pieces>10,"preview has visible gaps");
            require(std::abs(tool->previewParams().head.length-lengths[tier])<1e-8,"new arrow uses the selected tier");
            require(s.gesture(PointerPhase::Up,Vec2(130,40)).ok(),"commit dashed arrow");auto stored=read(s.doc.arrows().records().back().id);
            require(arrows::encodeArrowParams(stored)==arrows::encodeArrowParams(tool->previewParams())||stored.line==arrows::ArrowLine::Dashed,"commit stays dashed");
            require(std::abs(stored.head.length-lengths[tier])<1e-8,"commit retains chosen size");++previewCases;
        }
    }
    // Capture the actual canvas overlay and the committed arrow at one view.
    if(app.canvas){reset();s.arrowHeadTier=2;s.activateCommand("chooseArrowTool_Large_Dashed");auto view=s.view();view.center=Vec2(65,0);view.scale=3;s.setView(view);
        s.gesture(PointerPhase::Down,Vec2());s.gesture(PointerPhase::Move,Vec2(130,0));app.screenshot(dir+"/dashed-preview.png",false,false);
        s.gesture(PointerPhase::Up,Vec2(130,0));s.selected.clear();app.screenshot(dir+"/dashed-committed.png",false,false);
    }
    std::ofstream(dir+"/arrow-size-preview-checks.json")<<"{\"head_tiers\":5,\"rendered_sizes_change\":true,\"mixed_graphics_preserved\":true,\"undo_redo\":true,\"mechanism_excluded\":true,\"dashed_preview_cases\":"<<previewCases<<",\"new_arrow_size\":true}";
}
