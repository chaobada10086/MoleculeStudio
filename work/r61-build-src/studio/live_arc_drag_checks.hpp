#pragma once
static void checkLiveArcDrag(App& app,const std::string& dir){
    auto require=[](bool ok,const char* why){if(!ok)throw std::runtime_error(std::string("live arc drag: ")+why);};
    auto& s=app.s;int totalFrames=0,totalCases=0;const auto originalView=s.view();
    std::ofstream captureLog(dir+"/live-arc-capture-cases.txt");
    for(int lock:{0,1,2})for(bool selected:{false,true})for(double zoom:{.5,1.,3.})for(double dpi:{1.,1.5})for(int preset:{0,58}){
    s.cancel();s.doc=Document();s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());s.arcBendLocked=lock==1;s.arcCurvatureLocked=lock==2;s.arcBendRatio=.516067;s.arcCurvature=.01;s.arcCurvatureMajor=false;
    auto p=arrows::resolvePreset(preset,Vec2(-120,0),Vec2(120,0),.6);if(preset==58)p.arcSagitta=-40;
    ObjectId id;require(arrows::addArrow(s.doc,p,.6,&id).ok(),"fixture");s.refresh();if(selected)s.selected={ObjectRef(ObjectKind::Arrow,id)};
    s.activateCommand("chooseSelectionTool_Marquee");auto view=s.view();view.center=Vec2();view.scale=zoom;view.dpiScale=dpi;s.setView(view);
    app.pointerOverCanvas=true;app.down=false;app.selectionArrowEdit=false;
    auto device=[&](Vec2 point){auto q=s.view().toDevice(point);return MAKELPARAM(int(std::lround(q.x)),int(std::lround(q.y)));};auto middle=arrows::arcMidpoint(p.begin,p.end,p.arcSagitta);auto digest=s.doc.digest();auto cursor=s.history.cursor();
    auto press=middle+Vec2(0,view.deviceRadiusToDocument(8*dpi));auto pixel=view.toDevice(press);Vec2 capture;
    captureLog<<lock<<' '<<selected<<' '<<zoom<<' '<<dpi<<' '<<preset<<" result="<<app.arrowBendAt(pixel,&capture)<<" expected="<<id<<" middle="<<middle.x<<','<<middle.y<<" capture="<<capture.x<<','<<capture.y<<" flags="<<app.down<<app.pan<<app.textEditor.active<<app.transformDragging<<app.wrapDragging<<app.tableDragPending<<app.patternPlacing<<app.ringPlacing<<app.bioPlacing<<app.radicalPlacing<<app.virtualBondTool<<app.perspectiveTool<<" owner="<<s.entries[s.activeIndex].owner<<std::endl;
    require(app.arrowBendAt(pixel,&capture)==id&&length(capture-middle)<1e-8,"broad middle acquisition");
    require(app.arrowBendAt(view.toDevice(middle)+Vec2(0,14*dpi))==kInvalidObjectId,"capture boundary");
    require(app.canvasCursorId(pixel)==32646,"middle drag cursor");
    SendMessageW(app.canvas,WM_MOUSEMOVE,0,device(press));
    render::OverlayScene overlay;app.appendArrowBendCapture(overlay);require(overlay.scene().primitives().size()==2,"contrasting capture halo");
    SendMessageW(app.canvas,WM_LBUTTONDOWN,MK_LBUTTON,device(press));
    auto tool=dynamic_cast<arrows::ArrowTool*>(s.entries[s.activeIndex].tool.get());
    require(tool&&tool->state().active&&tool->state().target==id&&tool->state().grabbed==arrows::HandleKind::Middle,"captured existing middle handle");
    auto actualPress=tool->state().rawPressPoint;
    for(int step=-20;step<=20;++step){auto pointer=press+Vec2(0,step*4.);auto packed=device(pointer);
        SendMessageW(app.canvas,WM_MOUSEMOVE,MK_LBUTTON,packed);
        auto actual=view.toDocument(Vec2(GET_X_LPARAM(packed),GET_Y_LPARAM(packed)));
        auto preview=tool->previewParams();require(std::abs(preview.arcSagitta-(p.arcSagitta+actual.y-actualPress.y))<1e-6,"continuous offset-preserving drag with saved locks");
        require(length(preview.begin-p.begin)<1e-8&&length(preview.end-p.end)<1e-8,"endpoints fixed");
        require(s.doc.digest()==digest&&s.history.cursor()==cursor,"preview has no document edits");
        Vec2 feedback;require(app.arrowBendFeedback(&feedback)&&length(feedback-arrows::arcMidpoint(preview.begin,preview.end,preview.arcSagitta))<1e-8,"halo tracks live curve");++totalFrames;
    }
    auto preview=tool->previewParams();SendMessageW(app.canvas,WM_LBUTTONUP,0,device(press+Vec2(0,80)));
    arrows::ArrowParams result;bool valid=false;arrows::readArrow(s.doc,id,&result,&valid);
    require(valid&&std::abs(result.arcSagitta-preview.arcSagitta)<1e-6,"release matches preview");
    require(s.history.cursor()==cursor+1&&s.undo().ok()&&s.doc.digest()==digest,"one undo restores original");
    SendMessageW(app.canvas,WM_LBUTTONDOWN,MK_LBUTTON,device(press));SendMessageW(app.canvas,WM_MOUSEMOVE,MK_LBUTTON,device(press+Vec2(0,-40)));SendMessageW(app.canvas,WM_KEYDOWN,VK_ESCAPE,0);
    require(s.doc.digest()==digest&&!app.down&&GetCapture()!=app.canvas,"cancel leaves original intact");++totalCases;
    }
    s.cancel();s.arcBendLocked=s.arcCurvatureLocked=false;s.setView(originalView);s.fit();s.selected.clear();app.pointerOverCanvas=false;app.arrowBendWasHot=false;
    s.save(dir+"/live-arc-manual-fixture.cdxml");
    std::ofstream(dir+"/live-arc-drag-checks.json")<<"{\"cases\":"<<totalCases<<",\"mouse_move_frames\":"<<totalFrames<<",\"saved_locks\":true,\"continuous_preview\":true,\"cross_chord\":true,\"capture_halo\":true,\"grab_offset_preserved\":true,\"zoom_and_dpi\":true,\"commit_on_release\":true,\"single_undo\":true,\"escape_cancel\":true}";
}
