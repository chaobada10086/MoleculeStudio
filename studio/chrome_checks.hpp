#pragma once
void App::chromeCheck(const std::string& dir){
    auto require=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
    if(std::getenv("MOLECULE_STUDIO_OCSR_MODELESS_CHECK")){
        demo();s.activateCommand("chooseSelectionTool_Marquee");s.selected.clear();s.refresh();
        auto findDialog=[&](){struct Search{HWND owner,result;};Search search={h,nullptr};EnumThreadWindows(GetCurrentThreadId(),[](HWND window,LPARAM value)->BOOL{auto* search=reinterpret_cast<Search*>(value);wchar_t name[128]={};GetClassNameW(window,name,128);if(wcscmp(name,L"MoleculeStudioOcsrDialog")==0&&GetWindow(window,GW_OWNER)==search->owner){search->result=window;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&search));return search.result;};
        openImageToStructure(L"");HWND dialog=findDialog();
        require(IsWindow(dialog)&&IsWindowVisible(dialog),"recognition window opens");
        require(IsWindowEnabled(h)&&IsWindowEnabled(canvas),"recognition window keeps canvas enabled");
        auto atom=s.doc.atoms().records().front().id;auto point=s.view().toDevice(s.doc.atoms().get(atom)->position);
        SetFocus(canvas);SendMessageW(canvas,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(int(point.x),int(point.y)));SendMessageW(canvas,WM_LBUTTONUP,0,MAKELPARAM(int(point.x),int(point.y)));
        require(!s.selected.empty(),"canvas selection works with recognition window open");
        auto before=s.doc.digest();openText(kInvalidObjectId,atom);require(textEditor.active,"canvas inline editor opens with recognition window open");
        textEditor.select(0,-1);SendMessageW(edit,EM_REPLACESEL,TRUE,(LPARAM)L"NO2");require(finishText(true),"canvas edits commit while recognition window stays open");
        require(s.doc.atomLabel(atom)=="NO2"&&IsWindow(dialog),"recognition window survives canvas editing");
        require(s.undo().ok()&&s.doc.digest()==before,"canvas undo works with recognition window open");
        DestroyWindow(dialog);require(!IsWindow(dialog)&&IsWindowEnabled(h),"closing recognition leaves canvas enabled");
        openImageToStructure(L"");dialog=findDialog();require(IsWindow(dialog)&&IsWindowEnabled(h),"recognition window can reopen without locking canvas");DestroyWindow(dialog);
        std::ofstream(dir+"/modeless-check.json")<<"{\"window_open\":true,\"canvas_enabled\":true,\"native_canvas_selection\":true,\"inline_edit_commit\":true,\"undo\":true,\"close_and_reopen\":true}";
        s.dirty=false;PostMessageW(h,WM_CLOSE,0,0);return;
    }

    require(s.doc.defaultFont()=="Arial"&&textEditor.nextFamily=="Arial","new document and editor default to the ACS 1996 Arial preset");
    {Document explicitId(123);require(explicitId.defaultFont()=="Times New Roman","explicit-id Document keeps the engine default; the App applies the ACS preset");}
    // The responsive-layout assertions below describe the expanded ribbon; the
    // stored interface.ini preference defaults to the compact one.
    ribbonCompact=false;ribbonVisible=true;layout();
    demo();
    {
        render();auto digest=s.doc.digest();auto before=resizeLayouts;
        SendMessageW(h,WM_ENTERSIZEMOVE,0,0);
        for(int i=0;i<60;++i)SendMessageW(h,WM_SIZE,SIZE_RESTORED,MAKELPARAM(width+i,height+i));
        require(resizeLayouts==before&&resizePending,"native resize burst coalesces without layout or rendering");
        SendMessageW(h,WM_TIMER,71,0);require(resizeLayouts==before+1&&!resizePending,"resize timer performs one layout");
        auto previews=resizePreviews;InvalidateRect(canvas,0,FALSE);UpdateWindow(canvas);
#ifdef MOLECULE_STUDIO_DX11
        require(resizePreviews>previews,"native resize uses last completed frame instead of GPU target rebuild");
#endif
        SendMessageW(h,WM_EXITSIZEMOVE,0,0);require(!liveSizing&&!resizePending&&resizeLayouts==before+2,"resize exit always flushes final exact layout");
        require(s.doc.digest()==digest,"resize preview preserves document");
        std::ofstream(dir+"/resize-coalescing.json")<<"{\"burst_messages\":60,\"burst_layouts\":0,\"timer_layouts\":1,\"cached_preview\":true,\"final_layout\":true,\"document_preserved\":true}";
    }
    if(GetEnvironmentVariableW(L"MOLECULE_STUDIO_RESIZE_CHECK_ONLY",nullptr,0)){s.dirty=false;PostMessageW(h,WM_CLOSE,0,0);return;}
    {
        const char* ids[]={"textalign0","textalign1","textalign2","textalign3","align0","align1","align2","align3","align4","align5","distH","distV","group","ungroup","merge","rotate","flipH","flipV","front","back","layerUp","layerDown","sizeUp","sizeDown"};
        const char* labels[]={"文字左对齐","文字居中","文字右对齐","两端对齐","左对齐","水平居中","右对齐","顶部对齐","垂直居中","底部对齐","水平分布","垂直分布","编组","解组","合并","旋转","水平翻转","垂直翻转","置于顶层","置于底层","前移一层","后移一层","增大字号","减小字号"};
        Bitmap sheet(720,240,PixelFormat32bppARGB);Graphics g(&sheet);g.Clear(white);g.SetSmoothingMode(SmoothingModeAntiAlias);g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
        for(int i=0;i<24;++i){float x=float(i%8)*90,y=float(i/8)*80;icon(g,ids[i],RectF(x+12,y+8,28,28),ink);icon(g,ids[i],RectF(x+54,y+14,17,17),ink);txt(g,labels[i],RectF(x,y+44,90,20),10,ink,false,true);}
        UINT count=0,bytes=0;GetImageEncodersSize(&count,&bytes);std::vector<uint8_t> storage(bytes);auto encoders=(ImageCodecInfo*)storage.data();GetImageEncoders(count,bytes,encoders);for(UINT i=0;i<count;++i)if(wcscmp(encoders[i].MimeType,L"image/png")==0)sheet.Save(W(dir+"/utility-icons.png").c_str(),&encoders[i].Clsid,0);
    }
    // Render actual owner-drawn menu rows off screen, covering every preset.
    int rows=int(arrows::arrowPresetCount());HDC screen=GetDC(h);HDC memory=CreateCompatibleDC(screen);
    HBITMAP surface=CreateCompatibleBitmap(screen,1040,((rows+2)/3+2)*36);HGDIOBJ old=SelectObject(memory,surface);
    RECT all={0,0,1040,((rows+2)/3+2)*36};FillRect(memory,&all,(HBRUSH)GetStockObject(WHITE_BRUSH));
    for(int i=0;i<4;++i){DRAWITEMSTRUCT item={};item.CtlType=ODT_MENU;item.hDC=memory;item.itemData=900+i;item.rcItem={i*260,0,(i+1)*260,34};require(drawArrowMenuItem(&item),"category preview");}
    for(int i=0;i<rows;++i){auto& preset=arrows::arrowPresets()[i];DRAWITEMSTRUCT item={};item.CtlType=ODT_MENU;item.hDC=memory;item.itemData=1000+preset.presetId;int x=i%3*340,y=(i/3+1)*36;item.rcItem={x,y,x+330,y+34};require(drawArrowMenuItem(&item),"arrow preview row");
        if(preset.implemented){chooseArrowPreset(preset.presetId);invalidate(false);require(s.entries[s.activeIndex].command==preset.sourceCommand,"menu chooses real tool");
            bool matched=false;for(const auto& group:groups)for(const auto& b:group.items)if(b.id=="chooseArrowTool_Large_Solid")matched=b.cmd==preset.sourceCommand;require(matched&&arrowPreset==preset.presetId,"toolbar follows selected arrow");}}
    {Bitmap preview(surface,nullptr);UINT count=0,bytes=0;GetImageEncodersSize(&count,&bytes);std::vector<uint8_t> storage(bytes);auto encoders=(ImageCodecInfo*)storage.data();GetImageEncoders(count,bytes,encoders);for(UINT i=0;i<count;++i)if(wcscmp(encoders[i].MimeType,L"image/png")==0)preview.Save(W(dir+"/arrow-previews.png").c_str(),&encoders[i].Clsid,0);}
    SelectObject(memory,old);DeleteObject(surface);DeleteDC(memory);ReleaseDC(h,screen);
    for(int w:{800,1100,1500}){
        scale=1;SetWindowPos(h,0,0,0,w,900,SWP_NOMOVE|SWP_NOZORDER);layout();
        for(const auto& b:quick){if(b.r.X<-1000.f)continue;require(b.r.Y>=40.f&&b.r.GetBottom()<=76.f,"file actions share the title row below the tab band");for(const auto& control:windowControls)require(!b.r.IntersectsWith(control.r),"file actions clear caption controls");}
        for(const auto& group:groups){for(const auto& b:group.items){if(b.r.X<-1000.f)continue;require(b.r.X>=0&&b.r.GetRight()<=width+1,"tools in client bounds");}for(const auto& b:chemistryQuick)require(!b.r.IntersectsWith(group.r),"chemistry actions clear groups");}
        POINT origin={0,0};ClientToScreen(h,&origin);auto hit=[&](int x,int y){return SendMessageW(h,WM_NCHITTEST,0,MAKELPARAM(origin.x+x,origin.y+y));};
        require(hit(50,16)==HTCAPTION,"custom title drags window");require(hit(2,height/2)==HTLEFT,"resize border retained");
        RectF action;for(const auto& b:quick)if(b.id=="fileMenu")action=b.r;require(hit(int(action.X+10),int(action.Y+10))==HTCLIENT,"file action remains clickable");
        screenshot(dir+"/shell-"+std::to_string(w)+".png");
    }
    scale=2;SetWindowPos(h,0,0,0,2600,1800,SWP_NOMOVE|SWP_NOZORDER);layout();screenshot(dir+"/shell-dpi200.png");
    require(quick.back().r.GetRight()<windowControls.front().r.X,"DPI-scaled caption clear");
    scale=1;ShowWindow(h,SW_MAXIMIZE);layout();RECT client;GetClientRect(h,&client);POINT origin={0,0};ClientToScreen(h,&origin);MONITORINFO monitor={};monitor.cbSize=sizeof(monitor);GetMonitorInfoW(MonitorFromWindow(h,MONITOR_DEFAULTTONEAREST),&monitor);
    {std::ofstream log(dir+"/maximize.txt");log<<origin.x<<","<<origin.y<<" client="<<client.right<<","<<client.bottom<<" work="<<monitor.rcWork.left<<","<<monitor.rcWork.top<<","<<monitor.rcWork.right<<","<<monitor.rcWork.bottom;}
    require(origin.x==monitor.rcWork.left&&origin.y==monitor.rcWork.top&&client.right==monitor.rcWork.right-monitor.rcWork.left&&client.bottom==monitor.rcWork.bottom-monitor.rcWork.top,"maximized window respects work area");
    screenshot(dir+"/shell-maximized.png");ShowWindow(h,SW_RESTORE);
    auto clickControl=[&](size_t i){auto r=windowControls[i].r;SendMessageW(h,WM_LBUTTONUP,0,MAKELPARAM(int((r.X+r.Width*.5f)*scale),int((r.Y+r.Height*.5f)*scale)));};
    layout();clickControl(1);require(IsZoomed(h),"custom maximize button");clickControl(1);require(!IsZoomed(h),"custom restore button");
    clickControl(0);require(IsIconic(h),"custom minimize button");ShowWindow(h,SW_RESTORE);
    auto clickTab=[&](int i){auto r=ribbonTabs[i].r;SendMessageW(h,WM_LBUTTONUP,0,MAKELPARAM(int((r.X+r.Width*.5f)*scale),int((r.Y+r.Height*.5f)*scale)));require(ribbonTab==i,"tab click changes ribbon");};
    auto digest=s.doc.digest();auto selection=s.selected;int tool=s.activeIndex;
    for(int dpi:{1,2})for(int width:{800,1100,1500}){
        scale=float(dpi);SetWindowPos(h,0,0,0,width*dpi,900*dpi,SWP_NOMOVE|SWP_NOZORDER);layout();
        for(int tab=0;tab<4;++tab){clickTab(tab);require(s.doc.digest()==digest&&s.selected==selection&&s.activeIndex==tool,"tab switch preserves document selection and tool");
            require(bool(GetWindowLongPtrW(fontBox,GWL_STYLE)&WS_VISIBLE)==(tab==2)&&bool(GetWindowLongPtrW(sizeBox,GWL_STYLE)&WS_VISIBLE)==(tab==2),"font and size controls appear only on text tab");
            for(const auto& group:visibleGroups())for(const auto& b:group.items){require(b.r.GetRight()*scale<=this->width+1&&b.r.GetBottom()*scale<top,"expanded ribbon stays inside window");if(b.cmd.compare(0,6,"choose")==0)require(b.enabled,"expanded tool is implemented");}
            if(dpi==1||width==1100)screenshot(dir+"/tab-"+std::to_string(tab)+"-"+std::to_string(width)+"-dpi"+std::to_string(dpi)+".png");
        }
    }
    scale=1;SetWindowPos(h,0,0,0,1100,900,SWP_NOMOVE|SWP_NOZORDER);layout();clickTab(1);
    int arrowCards=0,flatArrows=0,formatButtons=0;for(const auto& group:graphicsGroups)for(const auto& b:group.items){if(b.id=="graphics.arrowStyle"){++arrowCards;require(group.r.Width>=120&&group.r.Height>=96,"current-arrow preview has a full-size ribbon card");}if(b.id=="graphics.width"||b.id=="graphics.color")++formatButtons;if(arrows::presetByCommand(b.cmd))++flatArrows;}
    require(formatButtons==2,"independent width and color controls");
    require(arrowCards==1&&flatArrows==0,"graphics tab uses one arrow picker instead of flat style buttons");
    for(const char* command:{"chooseArrowTool_Large_Equilibrium","chooseArrowTool_270_CW"}){auto preset=arrows::presetByCommand(command);require(preset!=nullptr,"preview fixture preset");chooseArrowPreset(preset->presetId);invalidate(false);require(arrowPreset==preset->presetId,"large preview tracks chosen style");screenshot(dir+"/arrow-card-"+std::to_string(preset->presetId)+".png");}
    std::ofstream(dir+"/arrow-card-checks.json")<<"{\"single_picker\":true,\"no_flat_style_buttons\":true,\"large_preview\":true,\"style_sync\":true}";
    auto clickCommand=[&](const std::string& command,int arg=0){for(const auto& group:visibleGroups())for(const auto& b:group.items)if(b.cmd==command&&b.arg==arg){auto r=b.r;SendMessageW(h,WM_LBUTTONUP,0,MAKELPARAM(int((r.X+r.Width*.5f)*scale),int((r.Y+r.Height*.5f)*scale)));return;}require(false,"expanded command exists");};
    clickCommand("chooseDrawingTool_Rectangle_Hollow");require(s.entries[s.activeIndex].command=="chooseDrawingTool_Rectangle_Hollow","graphics tab dispatches drawing tool");
    ObjectId text;s.doc.addText(Vec2(0,100),12,"Ribbon text",&text);s.refresh();s.selected={ObjectRef(ObjectKind::Text,text)};digest=s.doc.digest();
    clickTab(2);clickCommand("@flag",kBold);require((s.doc.runs(text).front().flags&kBold)!=0,"text tab applies bold");require(s.undo().ok()&&s.doc.digest()==digest,"text ribbon format undo");
    openText(text);require(textEditor.active&&textEditor.family=="Arial","inline text uses new document default");clickTab(0);clickTab(2);require(!textEditor.active&&fullText(s.doc,text)=="Ribbon text","tab switch commits inline text without losing content");
    s.doc.setDefaultFont("Arial");s.refresh();openText(text);require(textEditor.family=="Arial","existing text inherits its original document font");finishText(false);s.doc.setDefaultFont("Times New Roman");s.refresh();
    require(s.save(dir+"/default-font.r62doc").ok(),"save font default");Session reloaded;require(reloaded.open(dir+"/default-font.r62doc").ok()&&reloaded.doc.defaultFont()=="Times New Roman","saved document retains font default");
    {Session imported;require(persistence::readCdxml(projectBenzeneXml(),imported.doc).ok()&&imported.doc.defaultFont()=="Arial","imported explicit font remains Arial");}
    textEditor.nextFamily="Arial";s.dirty=false;execute(Button("","新建","","@new"));require(s.doc.defaultFont()=="Arial"&&textEditor.nextFamily=="Arial","new command resets default font");
    std::ofstream(dir+"/font-checks.json")<<"{\"default_font\":\"Arial\",\"text_tab_only\":true,\"new_document_reset\":true,\"native_roundtrip\":true,\"existing_fonts_preserved\":true}";
    clickTab(0);
    graphicStyleCheck(dir);
    cdxmlColorCheck(dir);
    std::ofstream(dir+"/tab-checks.json")<<"{\"tabs\":4,\"click_dispatch\":true,\"preserves_document_selection_tool\":true,\"all_tools_implemented\":true,\"responsive_and_dpi\":true,\"graphics_tool\":true,\"text_format_undo\":true,\"inline_editor_preserved\":true}";
    std::ofstream(dir+"/checks.json")<<"{\"arrow_previews\":true,\"arrow_toolbar_selection\":true,\"custom_caption\":true,\"caption_actions\":true,\"resize\":true,\"maximized_work_area\":true,\"responsive_layout\":true,\"dpi200\":true}";
    s.dirty=false;PostMessageW(h,WM_CLOSE,0,0);
}
