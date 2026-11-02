void App::tlcMenu(){
    HMENU menu=CreatePopupMenu();studio_i18n::appendMenu(menu,MF_STRING,1,L"绘制薄层板");studio_i18n::appendMenu(menu,MF_STRING,2,L"新建薄层板的点样设置…");
    bool selected=false;for(auto ref:s.selected)if(r62::experimental::isFigureObject(s.doc,ref))selected=true;
    studio_i18n::appendMenu(menu,MF_STRING|(selected?0:MF_GRAYED),3,L"编辑选中的薄层板…");
    POINT at;GetCursorPos(&at);int result=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_LEFTALIGN,at.x,at.y,0,h,0);DestroyMenu(menu);
    if(result==1)execute(Button("","薄层板","table","chooseTLCPlateTool"));
    if(result==2)tlcSettings(kInvalidObjectId);
    if(result==3)for(auto ref:s.selected)if(r62::experimental::isFigureObject(s.doc,ref)){tlcSettings(ref.id);break;}
}
void App::tlcSettings(ObjectId id,uint32_t selectedSpot){
    using namespace r62::experimental;
    TlcDialog dialog;dialog.value=studio_tlc::preferences();dialog.editing=id!=kInvalidObjectId;
    if(dialog.editing){auto e=readFigureAndFrame(s.doc,id,&dialog.figure,&dialog.frame,0);if(!e.ok()){error(e);return;}dialog.prepared=true;dialog.spotId=selectedSpot;
        const auto& bag=static_cast<const Document&>(s.doc).extensions(figureRef(id));auto show=bag.findValue("studio.tlc.showRf");dialog.value.showRf=show&&*show=="1";
        if(auto diameter=bag.findValue("studio.tlc.spotDiameter"))dialog.value.diameter=std::max(1.,std::min(16.,std::atof(diameter->c_str())));
        if(!dialog.figure.spots.empty())dialog.value.diameter=std::max(1.,std::min(16.,2*dialog.figure.spots.front().radiusU*dialog.frame.halfWidth()));
    }
    if(!dialog.show(h))return;
    if(dialog.editing){auto candidate=dialog.figure;double diameter=dialog.value.diameter;bool showRf=dialog.value.showRf;
        auto result=s.submit(Command("薄层板样品与Rf",kInvalidateStructure|kInvalidateGeometry|kInvalidateAppearance,[id,candidate,diameter,showRf](CommandContext& context){auto e=writeFigure(context.document,id,candidate);if(e.ok()){auto& bag=context.document.extensions(figureRef(id));bag.set("studio.tlc.spotDiameter",std::to_string(diameter));bag.set("studio.tlc.showRf",showRf?"1":"0");}return e.ok()?CommandResult::success():CommandResult::fail(e.code,e.message);}),"薄层板样品与Rf");error(result);if(result.ok())status="薄层板已更新 · 各样品点数与 Rf 独立保存 · Ctrl+Z 可撤销";
    }else{auto& preferences=studio_tlc::preferences();preferences=dialog.value;preferences.customTemplate=true;preferences.plateTemplate=dialog.figure;
        auto path=interfacePath();auto put=[&](const wchar_t* key,int value){auto text=std::to_wstring(value);WritePrivateProfileStringW(L"TLC",key,text.c_str(),path.c_str());};
        put(L"Lanes",preferences.lanes);put(L"DiameterTenths",int(std::round(preferences.diameter*10)));put(L"Guides",preferences.guides);put(L"Origin",preferences.origin);put(L"Front",preferences.front);put(L"ShowRf",preferences.showRf);
        auto payload=W(encodeFigure(dialog.figure));WritePrivateProfileStringW(L"TLC",L"Template",payload.c_str(),path.c_str());
        execute(Button("","薄层板","table","chooseTLCPlateTool"));status="独立点样模板已保存 · 拖动绘制后保留每列点数与各点 Rf";
    }
    invalidate();
}
