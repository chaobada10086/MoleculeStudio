#pragma once
// One screen-space hit test drives acquisition, hover feedback and the cursor.
ObjectId App::arrowHandleAt(Vec2 pixel,Vec2* point,uint32_t modifiers){
    if(down||pan||textEditor.active||transformDragging||wrapDragging||tableDragPending||
       patternPlacing||ringPlacing||bioPlacing||radicalPlacing||virtualBondTool||perspectiveTool)return kInvalidObjectId;
    const auto& entry=s.entries[s.activeIndex];
    bool selection=entry.owner=="T05"&&entry.command.find("Eraser")==std::string::npos;
    auto tool=dynamic_cast<arrows::ArrowTool*>(entry.tool.get());
    if((!selection&&!tool)||(selection&&(modifiers&kModControl))||
       (tool&&tool->state().active))return kInvalidObjectId;
    if(selection&&s.selected.size()>1&&selectionHandleAt(pixel)>=0)return kInvalidObjectId;
    const auto view=s.view();
    const auto documentPoint=view.toDocument(pixel);
    ObjectId result=kInvalidObjectId;
    double best=1e100;
    for(auto ref:s.selected)if(ref.kind==ObjectKind::Arrow&&s.doc.arrows().alive(ref.id)){
        arrows::ArrowParams params;bool valid=false;
        if(arrows::readArrow(s.doc,ref.id,&params,&valid).ok()&&valid&&!params.virtualBond&&arrows::presetById(params.presetId)){
            const auto handles=arrows::arrowHandles(params,s.doc.defaultLineWidth(),arrows::Tessellation());
            const auto hit=arrows::pickSelectedArrowHandle(handles,documentPoint,s.snapRadiusDevice(),view);
            const auto preset=tool?tool->presetId():params.presetId;
            if(hit<handles.size()&&!arrows::mechanismGlyphTakesPriority(
                s.doc,documentPoint,view,s.snapRadiusDevice(),preset,modifiers,&handles[hit])){
                const double distance=length(handles[hit].point-documentPoint);
                if(distance<best){
                    best=distance;result=ref.id;
                    if(point)*point=handles[hit].point;
                }
            }
        }
        // The arrow tool edits the first selected arrow. The selection tool can switch targets.
        if(tool)break;
    }
    if(result!=kInvalidObjectId||!selection)return result;
    best=12*view.dpiScale;
    for(const auto& record:s.doc.arrows().records())if(record.isAlive()){
        arrows::ArrowParams p;bool valid=false;
        if(!arrows::readArrow(s.doc,record.id,&p,&valid).ok()||!valid||p.virtualBond||arrows::isMechanismArrow(p)||!arrows::presetById(p.presetId))continue;
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
bool App::arrowHandleFeedback(Vec2* point){
    if(!pointerOverCanvas&&!down)return false;
    if(down){
        auto tool=dynamic_cast<arrows::ArrowTool*>(s.entries[s.activeIndex].tool.get());
        if(!tool||!tool->state().active||tool->state().target==kInvalidObjectId||
           tool->state().grabbed==arrows::HandleKind::None)return false;
        const auto handles=arrows::arrowHandles(tool->previewParams(),s.doc.defaultLineWidth(),arrows::Tessellation());
        const auto index=tool->state().handleIndex;
        if(index>=handles.size()||handles[index].kind!=tool->state().grabbed)return false;
        *point=handles[index].point;
        return true;
    }
    return arrowHandleAt(pointerPixel,point)!=kInvalidObjectId;
}
void App::appendArrowHandleCapture(render::OverlayScene& overlay){
    Vec2 point;if(!arrowHandleFeedback(&point))return;
    const auto view=s.view();
    render::Primitive ring;ring.kind=render::PrimKind::Polyline;ring.style.stroke=render::Paint(0x2563EBFF,true);
    ring.style.fill.enabled=false;ring.style.strokeWidth=view.deviceRadiusToDocument(2.5*view.dpiScale);
    ring.layer=render::LayerId(900);ring.z=110;
    const double radius=view.deviceRadiusToDocument(9*view.dpiScale);
    for(int i=0;i<=40;++i)ring.points.push_back(point+Vec2(std::cos(i*6.283185307/40),std::sin(i*6.283185307/40))*radius);
    ring.bounds=render::geom::computeBounds(ring,.6);auto halo=ring;halo.style.stroke.rgba=0xFFFFFFFF;
    halo.style.strokeWidth=view.deviceRadiusToDocument(5*view.dpiScale);halo.bounds=render::geom::computeBounds(halo,.6);
    overlay.scene().add(halo);overlay.scene().add(ring);
}
