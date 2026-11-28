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
    captureLog<<lock<<' '<<selected<<' '<<zoom<<' '<<dpi<<' '<<preset<<" result="<<app.arrowHandleAt(pixel,&capture)<<" expected="<<id<<" middle="<<middle.x<<','<<middle.y<<" capture="<<capture.x<<','<<capture.y<<" flags="<<app.down<<app.pan<<app.textEditor.active<<app.transformDragging<<app.wrapDragging<<app.tableDragPending<<app.patternPlacing<<app.ringPlacing<<app.bioPlacing<<app.radicalPlacing<<app.virtualBondTool<<app.perspectiveTool<<" owner="<<s.entries[s.activeIndex].owner<<std::endl;
    require(app.arrowHandleAt(pixel,&capture)==id&&length(capture-middle)<1e-8,"broad middle acquisition");
    require(app.arrowHandleAt(view.toDevice(middle)+Vec2(0,14*dpi))==kInvalidObjectId,"capture boundary");
    require(app.canvasCursorId(pixel)==32646,"middle drag cursor");
    SendMessageW(app.canvas,WM_MOUSEMOVE,0,device(press));
    render::OverlayScene overlay;app.appendArrowHandleCapture(overlay);require(overlay.scene().primitives().size()==2,"contrasting capture halo");
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
        Vec2 feedback;require(app.arrowHandleFeedback(&feedback)&&length(feedback-arrows::arcMidpoint(preview.begin,preview.end,preview.arcSagitta))<1e-8,"halo tracks live curve");++totalFrames;
    }
    auto preview=tool->previewParams();SendMessageW(app.canvas,WM_LBUTTONUP,0,device(press+Vec2(0,80)));
    arrows::ArrowParams result;bool valid=false;arrows::readArrow(s.doc,id,&result,&valid);
    require(valid&&std::abs(result.arcSagitta-preview.arcSagitta)<1e-6,"release matches preview");
    require(s.history.cursor()==cursor+1&&s.undo().ok()&&s.doc.digest()==digest,"one undo restores original");
    SendMessageW(app.canvas,WM_LBUTTONDOWN,MK_LBUTTON,device(press));SendMessageW(app.canvas,WM_MOUSEMOVE,MK_LBUTTON,device(press+Vec2(0,-40)));SendMessageW(app.canvas,WM_KEYDOWN,VK_ESCAPE,0);
    require(s.doc.digest()==digest&&!app.down&&GetCapture()!=app.canvas,"cancel leaves original intact");++totalCases;
    }
    const bool savedFixedAngle=s.fixedAngle;
    s.fixedAngle=false;
    for(double zoom:{.5,1.,3.})for(double dpi:{1.,1.5})for(auto kind:{arrows::HandleKind::Begin,arrows::HandleKind::End,arrows::HandleKind::HeadSize}){
        s.cancel();s.doc=Document();s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());
        s.arcBendLocked=s.arcCurvatureLocked=false;
        const auto preset=kind==arrows::HandleKind::End?58u:0u;
        auto arrow=arrows::resolvePreset(preset,Vec2(-120,0),Vec2(120,0),.6);
        if(preset==58)arrow.arcSagitta=-35;
        ObjectId id;require(arrows::addArrow(s.doc,arrow,.6,&id).ok(),"selected arrow fixture");s.refresh();
        s.activateCommand("chooseSelectionTool_Marquee");auto view=s.view();view.center=Vec2();view.scale=zoom;view.dpiScale=dpi;s.setView(view);
        const auto handles=arrows::arrowHandles(arrow,.6,arrows::Tessellation());
        auto handle=std::find_if(handles.begin(),handles.end(),[&](const arrows::Handle& item){return item.kind==kind;});
        require(handle!=handles.end(),"selected handle exists");
        const Vec2 offset=kind==arrows::HandleKind::Begin?Vec2(-11*dpi,0):
            kind==arrows::HandleKind::End?Vec2(11*dpi,0):Vec2(0,11*dpi);
        auto device=[](Vec2 point){return MAKELPARAM(int(std::lround(point.x)),int(std::lround(point.y)));};
        auto unpack=[](LPARAM value){return Vec2(GET_X_LPARAM(value),GET_Y_LPARAM(value));};
        auto press=device(view.toDevice(handle->point)+offset);
        require(app.arrowHandleAt(unpack(press))==kInvalidObjectId,"unselected endpoint does not capture");
        s.selected={ObjectRef(ObjectKind::Arrow,id)};
        Vec2 captured;
        require(app.arrowHandleAt(unpack(press),&captured)==id&&length(captured-handle->point)<1e-8,"selected handle captured beyond six pixels");
        require(app.arrowHandleAt(unpack(press),nullptr,kModControl)==kInvalidObjectId,"Ctrl preserves selection operation");
        SendMessageW(app.canvas,WM_MOUSEMOVE,0,press);
        render::OverlayScene overlay;app.appendArrowHandleCapture(overlay);
        require(overlay.scene().primitives().size()==2&&app.canvasCursorId(unpack(press))==32646,"selected handle ring and cursor agree");
        const auto digest=s.doc.digest();const auto history=s.history.cursor();
        SendMessageW(app.canvas,WM_LBUTTONDOWN,MK_LBUTTON,press);
        auto tool=dynamic_cast<arrows::ArrowTool*>(s.entries[s.activeIndex].tool.get());
        require(tool&&tool->state().active&&tool->state().target==id&&tool->state().grabbed==kind,"mouse-down grabs highlighted handle");
        auto move=device(unpack(press)+(kind==arrows::HandleKind::HeadSize?Vec2(0,12*dpi):Vec2(8*dpi,16*dpi)));
        SendMessageW(app.canvas,WM_MOUSEMOVE,MK_LBUTTON,move);
        const auto preview=tool->previewParams();
        const auto delta=view.toDocument(unpack(move))-view.toDocument(unpack(press));
        if(kind==arrows::HandleKind::HeadSize){
            require(preview.head.width>arrow.head.width&&preview.begin==arrow.begin&&preview.end==arrow.end,"resize head without moving endpoints");
        }else{
            const auto endpoint=kind==arrows::HandleKind::Begin?preview.begin:preview.end;
            require(length(endpoint-handle->point-delta)<1e-8,"endpoint follows drag without jumping");
        }
        const auto previewHandles=arrows::arrowHandles(preview,.6,arrows::Tessellation());
        require(app.arrowHandleFeedback(&captured)&&length(captured-previewHandles[tool->state().handleIndex].point)<1e-8,"ring follows the grabbed handle");
        require(s.doc.digest()==digest&&s.history.cursor()==history,"preview does not edit the document");
        SendMessageW(app.canvas,WM_LBUTTONUP,0,move);
        require(s.history.cursor()==history+1&&s.undo().ok()&&s.doc.digest()==digest,"handle edit is one undo step");
    }
    s.fixedAngle=savedFixedAngle;
    s.cancel();s.arcBendLocked=s.arcCurvatureLocked=false;s.setView(originalView);s.fit();s.selected.clear();app.pointerOverCanvas=false;app.arrowHandleWasHot=false;
    s.save(dir+"/live-arc-manual-fixture.cdxml");
    std::ofstream(dir+"/live-arc-drag-checks.json")<<"{\"cases\":"<<totalCases<<",\"mouse_move_frames\":"<<totalFrames<<",\"saved_locks\":true,\"continuous_preview\":true,\"cross_chord\":true,\"capture_halo\":true,\"grab_offset_preserved\":true,\"zoom_and_dpi\":true,\"commit_on_release\":true,\"single_undo\":true,\"escape_cancel\":true}";
}
