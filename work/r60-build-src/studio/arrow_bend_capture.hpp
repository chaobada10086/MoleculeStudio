#pragma once
// One screen-space hit test drives acquisition, hover feedback and the cursor.
ObjectId App::arrowBendAt(Vec2 pixel,Vec2* point){
    if(down||pan||textEditor.active||transformDragging||wrapDragging||tableDragPending||
       patternPlacing||ringPlacing||bioPlacing||radicalPlacing||virtualBondTool||perspectiveTool)return kInvalidObjectId;
    const auto& entry=s.entries[s.activeIndex];
    bool selection=entry.owner=="T05"&&entry.command.find("Eraser")==std::string::npos;
    auto tool=dynamic_cast<arrows::ArrowTool*>(entry.tool.get());
    if(!selection&&!tool)return kInvalidObjectId;
    if(tool&&(tool->state().active||tool->presetId()==49||tool->presetId()==57))return kInvalidObjectId;
    const auto view=s.view();double best=12*view.dpiScale;ObjectId result=kInvalidObjectId;
    for(const auto& record:s.doc.arrows().records())if(record.isAlive()){
        arrows::ArrowParams p;bool valid=false;
        if(!arrows::readArrow(s.doc,record.id,&p,&valid).ok()||!valid||p.virtualBond||arrows::isMechanismArrow(p))continue;
        if(!selection&&std::find(s.selected.begin(),s.selected.end(),ObjectRef(ObjectKind::Arrow,record.id))==s.selected.end())continue;
        auto middle=arrows::arcMidpoint(p.begin,p.end,p.arcSagitta);
        double distance=length(view.toDevice(middle)-pixel);
        if(distance>=best)continue;
        // A nearby endpoint or head-size handle must win on short arrows.
        bool nearer=false;
        for(auto handle:arrows::arrowHandles(p,s.doc.defaultLineWidth(),arrows::Tessellation()))
            if(handle.kind!=arrows::HandleKind::Middle&&length(view.toDevice(handle.point)-pixel)<distance){nearer=true;break;}
        if(nearer)continue;
        result=record.id;best=distance;if(point)*point=middle;
    }
    return result;
}
bool App::arrowBendFeedback(Vec2* point){
    if(!pointerOverCanvas&&!down)return false;
    if(down){
        auto tool=dynamic_cast<arrows::ArrowTool*>(s.entries[s.activeIndex].tool.get());
        if(!tool||!tool->state().active||tool->state().target==kInvalidObjectId||
           tool->state().grabbed!=arrows::HandleKind::Middle||arrows::isMechanismArrow(tool->previewParams()))return false;
        auto p=tool->previewParams();*point=arrows::arcMidpoint(p.begin,p.end,p.arcSagitta);return true;
    }
    return arrowBendAt(pointerPixel,point)!=kInvalidObjectId;
}
void App::appendArrowBendCapture(render::OverlayScene& overlay){
    Vec2 point;if(!arrowBendFeedback(&point))return;
    render::Primitive ring;ring.kind=render::PrimKind::Polyline;ring.style.stroke=render::Paint(0x2563EBFF,true);
    ring.style.fill.enabled=false;ring.style.strokeWidth=s.view().deviceRadiusToDocument(2.5);
    ring.layer=render::LayerId(900);ring.z=110;
    const double radius=s.view().deviceRadiusToDocument(9);
    for(int i=0;i<=40;++i)ring.points.push_back(point+Vec2(std::cos(i*6.283185307/40),std::sin(i*6.283185307/40))*radius);
    ring.bounds=render::geom::computeBounds(ring,.6);auto halo=ring;halo.style.stroke.rgba=0xFFFFFFFF;
    halo.style.strokeWidth=s.view().deviceRadiusToDocument(5);halo.bounds=render::geom::computeBounds(halo,.6);
    overlay.scene().add(halo);overlay.scene().add(ring);
}
