from pathlib import Path
root=Path(__file__).resolve().parents[1]
def edit(name, old, new, count=1):
    p=root/name
    s=p.read_text(encoding='utf-8')
    assert s.count(old)==count,(name,s.count(old),old[:100])
    p.write_text(s.replace(old,new),encoding='utf-8',newline='\r\n')

edit('studio/engine/arrows_tools.cpp', 'double bestDistance = radius;', 'double bestDistance = view.deviceRadiusToDocument(std::max(deviceRadius,12.0*view.dpiScale));')
edit('studio/engine/arrows_tools.cpp', 'if (d < bestDistance) { bestDistance = d; best = i; }', 'const double capture = handles[i].kind==HandleKind::Middle ? view.deviceRadiusToDocument(std::max(deviceRadius,12.0*view.dpiScale)) : radius;\n        if (d < capture && d < bestDistance) { bestDistance = d; best = i; }')
edit('studio/engine/arrows_tools.cpp', '(presetId_!=58||dist(handles[hitIndex].point,pointer.position)<=host.view().deviceRadiusToDocument(2))', '(presetId_!=58||handles[hitIndex].kind==HandleKind::Middle||dist(handles[hitIndex].point,pointer.position)<=host.view().deviceRadiusToDocument(2))')
edit('studio/engine/arrows_tools.cpp', 'bool editingArc=false;', 'const bool directBend=state_.target!=core::kInvalidObjectId &&\n        (state_.grabbed==HandleKind::Middle||state_.grabbed==HandleKind::ArcRadius);\n    bool editingArc=false;')
edit('studio/engine/arrows_tools.cpp', 'p.arcSagitta = sagittaFromHandleDrag(p.begin, p.end, pointer.position);\n            if(arcCurvatureLocked_&&!p.mechanism)p.arcSagitta=sagittaAtCurvature(arcCurvature_,core::length(p.end-p.begin),arcCurvatureMajor_);', '// Preserve the grab offset within the capture halo: no jump on acquisition.\n            p.arcSagitta = sagittaFromHandleDrag(p.begin, p.end,\n                arcMidpoint(p.begin,p.end,p.arcSagitta)+pointer.position-state_.rawPressPoint);', 2)
edit('studio/engine/arrows_tools.cpp','if(arcBendLocked_&&!isMechanismArrow(state_.pending)', 'if(arcBendLocked_&&!directBend&&!isMechanismArrow(state_.pending)')
edit('studio/engine/arrows_tools.cpp','if(arcCurvatureLocked_&&!isMechanismArrow(state_.pending)', 'if(arcCurvatureLocked_&&!directBend&&!isMechanismArrow(state_.pending)')
edit('studio/engine/session.cpp', 'arrow.route.clear();arrow.arcSagitta=arrows::sagittaFromHandleDrag(arrow.begin,arrow.end,p);\n            if(arcBendLocked)arrow.arcSagitta=core::length(arrow.end-arrow.begin)*arcBendRatio;\n            if(arcCurvatureLocked)arrow.arcSagitta=arrows::sagittaAtCurvature(arcCurvature,core::length(arrow.end-arrow.begin),arcCurvatureMajor);', 'arrow.route.clear();arrow.arcSagitta=arrows::sagittaFromHandleDrag(arrow.begin,arrow.end,\n                arrows::arcMidpoint(arrow.begin,arrow.end,arrow.arcSagitta)+p-press);')
edit('studio/studio.cpp', 'bool projectionWasHot=false;', 'bool projectionWasHot=false;\n    ObjectId arrowBendAt(Vec2 pixel,Vec2* point=nullptr);\n    bool arrowBendFeedback(Vec2* point);\n    void appendArrowBendCapture(render::OverlayScene& overlay);\n    void drawArrowBendHint(Graphics& g);bool arrowBendWasHot=false;')
edit('studio/studio.cpp', '#include "projection_actions.hpp"', '#include "projection_actions.hpp"\n#include "arrow_bend_capture.hpp"')
edit('studio/studio.cpp', 'if(projectionCapture(pixel))return 32513;', 'Vec2 bendPoint;if((down&&arrowBendFeedback(&bendPoint))||arrowBendAt(pixel)!=kInvalidObjectId)return 32646;if(projectionCapture(pixel))return 32513;')
edit('studio/studio.cpp', 'appendElectronMarkerSelection(o);appendProjectionCapture(o);', 'appendElectronMarkerSelection(o);appendProjectionCapture(o);appendArrowBendCapture(o);')
edit('studio/studio.cpp', 'appendElectronMarkerSelection(overlay);appendProjectionCapture(overlay);', 'appendElectronMarkerSelection(overlay);appendProjectionCapture(overlay);appendArrowBendCapture(overlay);')
edit('studio/studio.cpp', 'if(a->beginElectronMarkerDrag(pixel,pointerModifiers))return 0;', '''// Use the same acquisition halo for hover, cursor and mouse-down.
        // Only the middle grip is broadened; dragging the shaft still moves it.
        if(!(pointerModifiers&kModControl)){
            auto bend=a->arrowBendAt(pixel);
            if(bend!=kInvalidObjectId){arrows::ArrowParams p;bool valid=false;
                if(arrows::readArrow(s.doc,bend,&p,&valid).ok()&&valid){
                    bool selection=s.entries[s.activeIndex].owner=="T05";
                    if(auto preset=arrows::presetById(p.presetId)){
                        s.selected={ObjectRef(ObjectKind::Arrow,bend)};s.activateCommand(preset->sourceCommand);
                        a->selectionArrowEdit=selection;
                    }
                }
            }
        }
        if(a->beginElectronMarkerDrag(pixel,pointerModifiers))return 0;''')
edit('studio/studio.cpp', 'a->projectionWasHot=projectionHot;a->updateCanvasCursor();', 'a->projectionWasHot=projectionHot;bool bendHot=a->arrowBendAt(pixel)!=kInvalidObjectId;changed=changed||bendHot||a->arrowBendWasHot;a->arrowBendWasHot=bendHot;a->updateCanvasCursor();')
edit('studio/studio.cpp', 'SetCursor(LoadCursorW(0,MAKEINTRESOURCEW(32512)));InvalidateRect(h,0,FALSE);\n            if(a->now()-a->lastMovePaintMs>=8.0)', 'a->updateCanvasCursor();InvalidateRect(h,0,FALSE);\n            if(a->now()-a->lastMovePaintMs>=8.0)')
edit('studio/studio.cpp', 'SetCursor(LoadCursorW(0,MAKEINTRESOURCEW(32512)));InvalidateRect(h,0,FALSE);return 0;\n        }if(a->wrapDragging)', 'a->updateCanvasCursor();InvalidateRect(h,0,FALSE);return 0;\n        }if(a->wrapDragging)')
edit('studio/canvas_guides.hpp', 'void App::canvasGuides(){TableControls', 'void App::canvasGuides(){Graphics bendGraphics(dc);drawArrowBendHint(bendGraphics);TableControls')
edit('studio/app_actions.hpp','曲率已锁定，应用于之后新画和调整的箭头', '曲率已锁定：新画及拖动端点时保持；拖动中点可自由调弯')
edit('studio/app_actions.hpp','弧度已锁定，应用于之后新画和调整的箭头', '弧度已锁定：新画及拖动端点时保持；拖动中点可自由调弯')

# Preserve lock coverage for endpoint edits; direct manipulation is now explicitly free.
edit('studio/arc_parameter_checks.hpp', 'auto locked=read(source);require(locked.family==arrows::ArrowFamily::Arc&&close(locked.arcSagitta/length(locked.end-locked.begin),p.arcSagitta/length(p.end-p.begin)),"lock survives dragging curve onto straight chord");', '''auto straight=read(source);require(straight.family==arrows::ArrowFamily::Straight&&close(straight.arcSagitta,0),"middle drag overrides stored lock through zero");
        require(scene.undo().ok(),"restore locked arc");
        auto end=p.end+Vec2(30,10);scene.gesture(PointerPhase::Down,p.end);scene.gesture(PointerPhase::Move,end);scene.gesture(PointerPhase::Up,end);
        auto locked=read(source);require(locked.family==arrows::ArrowFamily::Arc&&close(locked.arcSagitta/length(locked.end-locked.begin),p.arcSagitta/length(p.end-p.begin)),"endpoint resize retains bend lock");''')
edit('studio/arc_parameter_checks.hpp', '\\"bend_lock_through_zero\\":true', '\\"direct_bend_through_zero\\":true,\\"endpoint_bend_lock\\":true')
edit('studio/free_arc_checks.hpp', 'require(std::abs(read(scene,locked).arcSagitta+80)<1e-8,"locked bend survives editing");', 'require(std::abs(read(scene,locked).arcSagitta+40)<1e-8,"middle drag freely overrides preset bend lock");')
print('r21 edits applied')
