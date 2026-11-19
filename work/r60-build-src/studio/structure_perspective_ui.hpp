#pragma once
bool App::beginStructurePerspective(Vec2 pixel){
    if(!perspectiveTool)return false;
    if(textEditor.active&&!finishText(true))return true;
    auto refs=studioio::expanded(s.doc,s.selected);perspectiveAtoms.clear();
    for(auto r:refs)if(r.kind==ObjectKind::Atom)perspectiveAtoms.insert(r.id);
    // Like the native tool, the first gesture makes a selection. Rotation
    // starts only inside that selection; outside gestures remain marquee input.
    if(perspectiveAtoms.size()<3)return false;
    if(!studioio::inside(studioio::selectionBounds(s),s.view().toDocument(pixel),s.view().deviceRadiusToDocument(4)))return false;
    auto first=s.doc.atoms().alive(*perspectiveAtoms.begin())->position;Box bounds(first,first);for(auto id:perspectiveAtoms)bounds.add(s.doc.atoms().alive(id)->position);
    perspectivePivot=(bounds.min+bounds.max)*.5;perspectivePress=pixel;perspectiveYaw=perspectivePitch=0;
    perspectiveDocument=s.doc.id();perspectiveRevision=s.doc.revision();perspectivePreview.reset(new Session());perspectivePreview->doc=s.doc;perspectivePreview->refresh();
#ifdef MOLECULE_STUDIO_DX11
    rotationGpuEligible=false;
#endif
    s.cancel();s.selected.clear();for(auto id:perspectiveAtoms)s.selected.push_back(ObjectRef(ObjectKind::Atom,id));
    perspectiveDragging=transformDragging=true;transformHandle=-1;SetFocus(canvas);SetCapture(canvas);status="结构透视：拖动调整倾角 · Shift 限制单轴 · Esc 取消";invalidate();return true;
}
void App::updateStructurePerspective(Vec2 pixel,uint32_t modifiers){
    Vec2 delta=(pixel-perspectivePress)*s.view().docPerDevice();if(modifiers&kModShift){if(std::abs(delta.x)>std::abs(delta.y))delta.y=0;else delta.x=0;}
    perspectiveYaw=-delta.x*.020;perspectivePitch=delta.y*.020;
    perspectivePreview->doc=s.doc;structure_perspective::tilt(perspectivePreview->doc,perspectiveAtoms,perspectivePivot,perspectiveYaw,perspectivePitch);perspectivePreview->refresh();invalidate();
}
void App::finishStructurePerspective(Vec2 pixel,uint32_t modifiers){
    updateStructurePerspective(pixel,modifiers);perspectiveDragging=transformDragging=false;if(GetCapture()==canvas)ReleaseCapture();
    if(std::abs(perspectiveYaw)+std::abs(perspectivePitch)>1e-7)error(s.submit(structure_perspective::command(perspectiveAtoms,perspectivePivot,perspectiveYaw,perspectivePitch,perspectiveDocument,perspectiveRevision),"结构透视"));
    perspectivePreview.reset();status="结构透视已调整 · Ctrl+Z 撤销 · 空格返回选择工具";updateCanvasCursor();invalidate();
}
