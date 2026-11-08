#pragma once
void App::aiCheck(const std::string& dir){
    auto require=[&](bool value,const char* why){std::ofstream(dir+"/ai-check-progress.txt",std::ios::app)<<why<<": "<<(value?"pass":"FAIL")<<"\n";if(!value)throw std::runtime_error(why);};
    if(GetFileAttributesW(W(dir+"/drawing-tools-plan.xml").c_str())!=INVALID_FILE_ATTRIBUTES){
        s.dirty=false;execute(Button("","新建","","@new"));s.graphicsWidth=0;s.selected.clear();aiContext();
        auto empty=s.doc.digest();auto xml=chemservice::read(W(dir+"/drawing-tools-plan.xml"));tinyxml2::XMLDocument plan;require(plan.Parse(xml.c_str())==tinyxml2::XML_SUCCESS,"parse composition fixture");auto expected=plan.RootElement()->FirstChildElement("expected");require(expected!=nullptr,"expected composition counts");
        applyAI(xml);auto stats=s.doc.stats();require(stats.aliveAtoms==expected->IntAttribute("atom")&&stats.aliveBonds==expected->IntAttribute("bond"),"editable molecular graph counts");require(stats.aliveTexts==expected->IntAttribute("text")&&s.doc.arrows().size()==size_t(expected->IntAttribute("arrow")),"captions and connectors on native canvas");
        for(auto item=expected->FirstChildElement("template");item;item=item->NextSiblingElement("template")){
            std::vector<ObjectRef> refs;int begin=item->IntAttribute("first_atom"),count=item->IntAttribute("atoms");
            for(int i=0;i<count;++i)refs.push_back(ObjectRef(ObjectKind::Atom,ObjectId(begin+i)));
            auto expanded=studioio::expanded(s.doc,refs);auto formula=studio::analyze(s.doc,std::vector<ObjectRef>(expanded.begin(),expanded.end()));
            require(formula.valid&&formula.plain==item->Attribute("formula"),"native AI template formula matches toolbar catalog");
        }
        require(s.save(dir+"/native-composition.cdxml",true).ok(),"export native composition");auto composed=s.doc.digest();require(s.undo().ok()&&s.doc.digest()==empty,"composition single-step undo");require(s.undo(true).ok()&&s.doc.digest()==composed,"composition redo restores exact digest");
        aiContext();auto actions=plan.RootElement()->FirstChildElement("actions");auto invalid=plan.NewElement("move_objects");invalid->SetAttribute("target","missing-object");invalid->SetAttribute("dx",1);actions->InsertEndChild(invalid);tinyxml2::XMLPrinter printer;plan.Print(&printer);bool rejected=false;try{applyAI(printer.CStr());}catch(...){rejected=true;}require(rejected&&s.doc.digest()==composed,"late native failure rolls back complete batch");
        for(const char* size:{"width='595.2756' height='841.8898'", "width='1190.5512' height='841.8898'", "width='14400' height='72'"}){
            aiContext();auto before=s.doc.digest();applyAI(std::string("<result ok='true'><actions><page ")+size+" across='2' down='3'/></actions></result>");
            auto changed=s.doc.digest();require(s.undo().ok()&&s.doc.digest()==before,"page undo");require(s.undo(true).ok()&&s.doc.digest()==changed,"page redo");require(s.undo().ok(),"restore page");
        }
        aiContext();applyAI("<result ok='true'><actions><add_text x='200000' y='-200000'>boundary</add_text><draw_path><point x='-200000' y='0'/><point x='200000' y='0'/></draw_path></actions></result>");require(s.undo().ok()&&s.doc.digest()==composed,"large coordinate batch undo");
        aiContext();require(aiSelection.empty(),"stale selection removed before AI snapshot");
        {std::string text;for(int i=0;i<200;++i)text+="中";require(aiobjects::textPreview(text)==text.substr(0,498),"UTF-8 text preview preserves whole characters");}
        for(const char* bad:{"<page width='71' height='842'/>","<page width='595' height='842' across='1.5'/>","<draw_arrow style='forward' x2='30' width='595'/>","<add_text x=''>bad</add_text>","<add_text x='nan'>bad</add_text>","<add_text x='200001'>bad</add_text>"}){
            aiContext();bool rejected=false;try{applyAI(std::string("<result ok='true'><actions><add_text>must rollback</add_text>")+bad+"</actions></result>");}catch(const std::exception& error){rejected=std::string(error.what()).find("AI 数值参数无效：")!=std::string::npos||std::string(error.what())=="AI 纸张张数无效";}
            require(rejected&&s.doc.digest()==composed,"numeric diagnostics and atomic rejection");
        }
        s.selected.clear();s.fit();invalidate();screenshot(dir+"/native-composition.png");std::ofstream(dir+"/native-checks.json")<<"{\"editable_graph\":true,\"captions\":true,\"connectors\":true,\"export\":true,\"single_undo\":true,\"redo\":true,\"atomic_failure\":true,\"page_dimensions\":true,\"numeric_boundaries\":true}";s.dirty=false;PostMessageW(h,WM_CLOSE,0,0);return;
    }
    s.dirty=false;execute(Button("","新建","","@new"));s.graphicsWidth=0;s.graphicsColor=0x000000FFu;switchTab(3);layout();
    require(ribbonTabs.size()==5&&ribbonTab==3&&visibleGroups().front().items.size()==3,"AI tab and commands present");
    aiContext();tinyxml2::XMLDocument result;auto root=result.NewElement("result");root->SetAttribute("ok",true);result.InsertEndChild(root);auto actions=result.NewElement("actions");root->InsertEndChild(actions);auto molecule=result.NewElement("molecule");actions->InsertEndChild(molecule);molecule->SetText(chemservice::read(W(dir+"/verified-artemisinin.cdxml")).c_str());tinyxml2::XMLPrinter out;result.Print(&out);
    auto before=s.doc.digest();applyAI(out.CStr());auto after=s.doc.digest();require(s.doc.atoms().size()==20&&s.doc.bonds().size()==23,"editable artemisinin atom and bond count");require(s.undo().ok()&&s.doc.digest()==before,"AI single-step undo");require(s.undo(true).ok()&&s.doc.digest()==after,"AI redo");
    require(s.save(dir+"/ai-artemisinin.cdxml",true).ok()&&s.save(dir+"/ai-artemisinin.r62doc").ok(),"save AI structure");
    {
        s.selected.clear();auto base=s.doc.digest();chemservice::write(W(dir+"/scene.xml"),aiContext());
        auto color=[&](ObjectRef r){auto v=static_cast<const Document&>(s.doc).extensions(r).findValue("studio.color.override");return v?*v:"";};
        std::map<ObjectRef,std::string> colors;for(auto r:s.all())colors[r]=color(r);
        applyAI("<result ok='true'><actions><style_objects target='document' kind='atom' element='O' color='#FF0000'/></actions></result>");
        int red=0;for(auto r:s.all()){auto a=r.kind==ObjectKind::Atom?s.doc.atoms().get(r.id):nullptr;if(a&&a->element==8){require(color(r)==std::to_string(0xFF0000FFu),"oxygen is red");++red;}else require(color(r)==colors[r],"oxygen filter leaves carbons bonds and other objects unchanged");}require(red==5,"all five oxygen atoms targeted without selection");
        require(s.save(dir+"/red-oxygen.cdxml",true).ok(),"save oxygen color result");auto redDigest=s.doc.digest();require(s.undo().ok()&&s.doc.digest()==base,"oxygen color one-step undo");require(s.undo(true).ok()&&s.doc.digest()==redDigest,"oxygen redo");require(s.undo().ok()&&s.doc.digest()==base,"restore fixture");
        aiContext();bool failed=false;try{applyAI("<result ok='true'><actions><style_objects target='document' element='O' color='#FF0000'/><move_objects target='missing' dx='1'/></actions></result>");}catch(...){failed=true;}require(failed&&s.doc.digest()==base,"late invalid object rolls back earlier edits");
        if(GetFileAttributesW(W(dir+"/editing-plan.xml").c_str())!=INVALID_FILE_ATTRIBUTES){
            aiContext();applyAI(chemservice::read(W(dir+"/editing-plan.xml")));require(s.doc.stats().aliveAtoms==26&&s.doc.stats().aliveBonds==27,"composed drawing and duplication counts");require(s.doc.stats().aliveTexts==1&&s.doc.paths().size()==1&&s.doc.arrows().size()==1,"composed text arrow path");
            int freshRed=0,freshBlue=0;for(const auto& a:s.doc.atoms().records())if(a.isAlive()&&a.id>=20){auto c=color(ObjectRef(ObjectKind::Atom,a.id));if(a.element==8){freshRed+=c==std::to_string(0xFF0000FFu);freshBlue+=c==std::to_string(0x0000FFFFu);}else require(c==std::to_string(0x000000FFu),"new carbon remains black");}require(freshRed==1&&freshBlue==1,"created handle and duplicate handle style independently");require(fullText(s.doc,0)=="Ethanol"&&s.doc.texts().get(0)->size==14&&(s.doc.texts().get(0)->format.flags&kBold),"text content size and bold applied");
            require(s.save(dir+"/composed.cdxml",true).ok(),"save composed plan");auto composed=s.doc.digest();require(s.undo().ok()&&s.doc.digest()==base,"entire multi-tool composition undo");require(s.undo(true).ok()&&s.doc.digest()==composed,"entire composition redo");
            s.selected.clear();aiContext();applyAI("<result ok='true'><actions><delete_objects target='document' kind='text' contains='Ethanol'/></actions></result>");require(s.doc.stats().aliveTexts==0,"delete matching text");require(s.undo().ok()&&s.doc.digest()==composed,"delete undo");require(s.undo().ok()&&s.doc.digest()==base,"restore composed fixture");
        }
        s.selected.clear();aiContext();applyAI("<result ok='true'><actions><add_text handle='new:1' x='0' y='0'>temporary</add_text><delete_objects target='new:1'/></actions></result>");require(s.doc.stats().aliveTexts==0&&s.selected.empty(),"create and delete leaves no dead selected references");require(s.undo().ok(),"undo create delete batch");
        std::ofstream(dir+"/ai-editing-checks.json")<<"{\"oxygen_only\":true,\"unselected_document\":true,\"late_failure_rollback\":true,\"composed_plan\":true,\"undo_redo\":true,\"delete\":true}";
    }
    s.selected=s.all();auto position=s.doc.atoms().get(0)->position;aiContext();applyAI("<result ok='true'><actions><style_selection color='#0046DC' width='0.75'/><move_selection dx='15' dy='-10'/></actions></result>");require(s.doc.atoms().get(0)->position==position+Vec2(15,-10),"AI moves selected structure");require(*static_cast<const Document&>(s.doc).extensions(ObjectRef(ObjectKind::Atom,0)).findValue("studio.color.override")==std::to_string(0x0046DCFFu),"AI selection style");require(s.undo().ok()&&s.doc.digest()==after,"AI style and movement undo together");
    aiContext();applyAI("<result ok='true'><actions><draw_shape shape='rectangle' x='-70' y='90' x2='-10' y2='120'/><draw_arrow style='forward' x='10' y='100' x2='80' y2='100'/><add_text x='-70' y='145'>AI 绘图测试</add_text></actions></result>");require(s.doc.arrows().size()==2&&s.doc.texts().size()==1,"AI shapes arrows and text");
    aiContext();before=s.doc.digest();bool rejected=false;try{applyAI("<result ok='true'><actions><add_text x='0' y='0'>bad</add_text><execute_shell/></actions></result>");}catch(...){rejected=true;}require(rejected&&s.doc.digest()==before,"invalid batch leaves document unchanged");
    aiContext();s.move({ObjectRef(ObjectKind::Atom,0)},Vec2(1,0));before=s.doc.digest();rejected=false;try{applyAI("<result ok='true'><actions><add_text x='0' y='0'>stale</add_text></actions></result>");}catch(...){rejected=true;}require(rejected&&s.doc.digest()==before,"stale document rejected");
    std::string secret="test-key-for-encryption-only";require(AiWindow::protect(AiWindow::protect(secret,true),false)==secret,"DPAPI credential roundtrip");
    ai.storageOverride=W(dir+"/settings");showAI("绘制一个青蒿素");
    {
        auto unchanged=s.doc.digest();auto transcript=ai.get(710);auto prompt=ai.get(711);
        tinyxml2::XMLDocument models;models.Parse("<models><model id='model-one'/><model id='model-two'/><model id='model-one'/><model id=''/></models>");
        ai.set(704,"model-two");ai.updateModels(models.RootElement());auto combo=GetDlgItem(ai.hwnd,704);
        require((GetWindowLongPtrW(combo,GWL_STYLE)&CBS_DROPDOWN)==CBS_DROPDOWN&&SendMessageW(combo,CB_GETCOUNT,0,0)==3,"model dropdown contains auto and unique model IDs");
        require(ai.get(704)=="model-two","model refresh preserves selection");SendMessageW(combo,CB_SETCURSEL,1,0);require(ai.get(704)=="model-one","model dropdown selection updates request value");
        ai.save();ai.set(704,"auto");ai.load();require(ai.get(704)=="model-one","selected model persists");
        ai.set(704,"custom-model");ai.updateModels(models.RootElement());require(ai.get(704)=="custom-model","manual model remains available");
        tinyxml2::XMLDocument empty;empty.Parse("<models/>");bool failed=false;try{ai.updateModels(empty.RootElement());}catch(...){failed=true;}
        require(failed&&ai.get(704)=="custom-model"&&SendMessageW(combo,CB_GETCOUNT,0,0)==3,"empty model result preserves previous list");
        ai.set(702,"https://api.deepseek.com");require(SendMessageW(combo,CB_GETCOUNT,0,0)==1,"endpoint change clears stale model list");
        require(s.doc.digest()==unchanged&&ai.get(710)==transcript&&ai.get(711)==prompt,"model controls do not change drawing or conversation");
        ai.set(704,"auto");ai.save();
        std::ofstream(dir+"/ai-model-checks.json")<<"{\"dropdown\":true,\"selection\":true,\"persistence\":true,\"manual_model\":true,\"empty_list_preserves_state\":true,\"endpoint_change\":true,\"document_unchanged\":true}";
    }
    if(aiLiveChecking){
        s.dirty=false;execute(Button("","新建","","@new"));auto key=AiWindow::protect(chemservice::read(W(dir+"/test-key.bin")),false);ai.set(706,key);SecureZeroMemory(&key[0],key.size());CheckDlgButton(ai.hwnd,707,BST_UNCHECKED);ai.set(711,"绘制一个青蒿素，把这个结构里的氧原子全部改成红色，在下方标注青蒿素，右侧画一个向右反应箭头。请一次完成。");ai.start(false);require(ai.process!=0,"live worker launched");
        while(ai.process){MSG msg;while(PeekMessageW(&msg,0,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}ai.poll();MsgWaitForMultipleObjects(0,0,FALSE,25,QS_ALLINPUT);}
        chemservice::write(W(dir+"/live-transcript.txt"),ai.get(710));require(s.doc.atoms().size()==20&&s.doc.bonds().size()==23,"live DeepSeek lookup inserts artemisinin");require(s.doc.stats().aliveTexts>=1&&s.doc.arrows().size()>=1,"live compound request includes label and arrow");int liveOxygen=0;for(const auto& a:s.doc.atoms().records())if(a.isAlive()&&a.element==8){auto v=static_cast<const Document&>(s.doc).extensions(ObjectRef(ObjectKind::Atom,a.id)).findValue("studio.color.override");require(v&&*v==std::to_string(0xFF0000FFu),"live request colors every oxygen red");++liveOxygen;}require(liveOxygen==5,"live five red oxygen atoms");require(s.save(dir+"/live-artemisinin.cdxml",true).ok(),"save live result");auto digest=s.doc.digest();require(s.undo().ok()&&s.doc.stats().aliveAtoms==0,"live result undo");require(s.undo(true).ok()&&s.doc.digest()==digest,"live result redo");
        s.selected.clear();ai.set(711,"把画布上所有氧原子改成蓝色（#0000FF），其他原子、化学键和文字保持原样。");ai.start(false);require(ai.process!=0,"live existing structure edit launched");
        while(ai.process){MSG msg;while(PeekMessageW(&msg,0,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}ai.poll();MsgWaitForMultipleObjects(0,0,FALSE,25,QS_ALLINPUT);}
        for(const auto& a:s.doc.atoms().records())if(a.isAlive()&&a.element==8){auto v=static_cast<const Document&>(s.doc).extensions(ObjectRef(ObjectKind::Atom,a.id)).findValue("studio.color.override");require(v&&*v==std::to_string(0x0000FFFFu),"live unselected oxygen edit turns blue");}require(s.undo().ok()&&s.doc.digest()==digest,"live oxygen edit undo leaves all other objects exact");chemservice::write(W(dir+"/live-transcript.txt"),ai.get(710));ai.set(706,"");
    }
    auto unchanged=s.doc.digest();ai.set(706,"cancellation-test-key");ai.set(711,"画一个矩形");CheckDlgButton(ai.hwnd,719,BST_UNCHECKED);ai.start(false);require(ai.process!=0,"cancel fixture worker started");ai.stop(false);require(!ai.process&&s.doc.digest()==unchanged,"cancel keeps document unchanged");ai.set(706,"");ai.set(711,"");
    ShowWindow(ai.hwnd,SW_HIDE);s.selected.clear();s.fit();{auto view=s.view();view.scale=std::min(view.scale,2.0);s.setView(view);}invalidate();screenshot(dir+"/ai-canvas.png");
    ShowWindow(ai.hwnd,SW_SHOWNOACTIVATE);UpdateWindow(ai.hwnd);
    {RECT rect;GetClientRect(ai.hwnd,&rect);HDC screen=GetDC(ai.hwnd),memory=CreateCompatibleDC(screen);HBITMAP bitmap=CreateCompatibleBitmap(screen,rect.right,rect.bottom);auto old=SelectObject(memory,bitmap);PrintWindow(ai.hwnd,memory,PW_CLIENTONLY);Bitmap image(bitmap,0);UINT count=0,bytes=0;GetImageEncodersSize(&count,&bytes);std::vector<uint8_t> storage(bytes);auto encoders=(ImageCodecInfo*)storage.data();GetImageEncoders(count,bytes,encoders);for(UINT i=0;i<count;++i)if(wcscmp(encoders[i].MimeType,L"image/png")==0)image.Save(W(dir+"/ai-window.png").c_str(),&encoders[i].Clsid,0);SelectObject(memory,old);DeleteObject(bitmap);DeleteDC(memory);ReleaseDC(ai.hwnd,screen);}
    ai.stop(false);ShowWindow(ai.hwnd,SW_HIDE);std::ofstream(dir+"/ai-checks.json")<<"{\"tab\":true,\"editable_molecule\":true,\"undo_redo\":true,\"shapes_arrows_text\":true,\"selection_format_move\":true,\"invalid_batch_atomic\":true,\"stale_document_guard\":true,\"dpapi\":true,\"live_deepseek\":"<<(aiLiveChecking?"true":"false")<<"}";s.dirty=false;PostMessageW(h,WM_CLOSE,0,0);
}

// Task 7/8 acceptance: the AI pane's "api not configured" banner and the centring of an AI
// drawing turn. No network: the worker is never started; the reply XML is synthesised and fed
// through App::applyAI exactly like a polled result, and the openSettings route is a test stub.
void App::aiUxCheck(const std::string& dir){
    auto require=[&](bool value,const char* why){if(!value){std::ofstream(dir+"/ai-ux-failure.txt",std::ios::app)<<why<<"\n";throw std::runtime_error(why);}};
    std::ofstream log(dir+"/ai-ux-checks.txt");
    CreateDirectoryW(W(dir+"/ai-ux").c_str(),nullptr);
    const std::wstring previousStorage=ai.storageOverride;ai.storageOverride=W(dir+"/ai-ux");

    // ---- Task 7: warning bar + direct route to the AI settings page ----
    ai.show(h,"",false,true);
    require(IsWindow(ai.hwnd),"AI pane is created");
    ai.set(702,"");ai.set(704,"auto");ai.set(706,"");ai.refreshConfiguration();
    auto bar=GetDlgItem(ai.hwnd,AiWindow::configMessageId);auto configButton=GetDlgItem(ai.hwnd,AiWindow::configButtonId);
    require(bar&&configButton,"banner and config button controls exist");
    require(IsWindowVisible(bar)&&IsWindowVisible(configButton),"incomplete API shows the banner and its button");
    require(AiWindow::controlText(configButton)==L"配置 API","config button carries the 配置 API label");
    int opened=0;auto previousOpen=ai.openSettings;ai.openSettings=[&](){++opened;};
    SendMessageW(ai.hwnd,WM_COMMAND,MAKEWPARAM(AiWindow::configButtonId,BN_CLICKED),(LPARAM)configButton);
    require(opened==1,"配置 API invokes the openSettings route (设置 → AI 与 API)");
    ai.openSettings=previousOpen;
    ai.set(702,"https://api.deepseek.com");ai.set(706,"");ai.refreshConfiguration();
    require(!ai.configurationComplete()&&IsWindowVisible(bar),"address + model without a key is still unconfigured");
    ai.set(706,"key");ai.set(704,"");ai.refreshConfiguration();
    require(!ai.configurationComplete(),"half configuration (missing model) counts as unconfigured");
    auto handle=ai.hwnd;ai.set(704,"auto");ai.set(706,"key");ai.refreshConfiguration();
    require(ai.hwnd==handle&&ai.configurationComplete(),"the same pane is reused when the configuration becomes complete");
    require(!IsWindowVisible(bar)&&!IsWindowVisible(configButton),"complete API hides the banner without recreating the pane");
    ai.set(706,"");ai.refreshConfiguration();require(IsWindowVisible(bar),"banner returns when the key is cleared");
    ai.set(711,"画一个苯环");auto unchanged=s.doc.digest();ai.start(false);
    require(ai.process==0,"an incomplete configuration never launches a worker");
    require(IsWindowVisible(bar)&&ai.configurationAlert,"send with an incomplete API surfaces and highlights the banner");
    require(s.doc.digest()==unchanged,"the failed send leaves the document unchanged");ai.set(711,"");
    for(int width:{300,420}){
        SetWindowPos(ai.hwnd,0,0,0,width,520,SWP_NOMOVE|SWP_NOZORDER);RECT client;GetClientRect(ai.hwnd,&client);
        for(int id:{AiWindow::configMessageId,AiWindow::configButtonId}){RECT r;GetWindowRect(GetDlgItem(ai.hwnd,id),&r);MapWindowPoints(nullptr,ai.hwnd,(POINT*)&r,2);require(r.left>=0&&r.top>=0&&r.right<=client.right+1&&r.bottom<=client.bottom+1,"narrow layout keeps the banner controls inside the pane");}
        require(IsWindowVisible(bar)&&IsWindowVisible(configButton)&&IsWindowEnabled(configButton),"narrow layout still shows the banner and keeps its button enabled");
    }
    ai.docked=true;ai.dockScale=1.25f;ai.updateFont();ai.layout();
    {RECT client;GetClientRect(ai.hwnd,&client);RECT r;GetWindowRect(configButton,&r);MapWindowPoints(nullptr,ai.hwnd,(POINT*)&r,2);require(r.right<=client.right+1&&r.bottom<=client.bottom+1,"docked layout keeps the config button visible");require(IsWindowVisible(configButton)&&IsWindowEnabled(configButton),"docked layout shows and enables the config button");}
    ai.docked=false;ai.dockScale=1;ai.updateFont();ai.layout();ai.hide();
    log<<"banner_visible_blank=true\nconfig_route=true\nhalf_config=true\nbanner_hides=true\nsend_prompts=true\nnarrow_visible=true\ndocked_visible=true\n";

    // ---- Task 8: an AI turn is centred on the visible viewport, existing objects untouched ----
    auto reset=[&](){s.cancel();s.doc=Document();drawingSettings.apply(s.doc);s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());s.refresh();};
    auto setView=[&](Vec2 centre,double zoom,int width,int height){auto v=s.view();v.center=centre;v.scale=zoom;v.dpiScale=1;v.viewportCenter=Vec2(width*.5,height*.5);s.renderView.widthPx=width;s.renderView.heightPx=height;s.setView(v);};
    auto boundsOf=[&](const std::vector<ObjectRef>& refs){return aiContentBounds(s.doc,refs,s.glyphs);};
    auto closeEnough=[&](double a,double b){return std::abs(a-b)<1e-6;};
    Session mol;{ObjectId a,b,c,x,y;mol.doc.addAtom(Vec2(-14.4,0),6,&a);mol.doc.addAtom(Vec2(0,-12.5),6,&b);mol.doc.addAtom(Vec2(14.4,0),6,&c);mol.doc.addBond(a,b,&x);mol.doc.addBond(b,c,&y);mol.refresh();}
    const std::string molXml=persistence::writeCdxmlToString(mol.doc);
    auto resultXml=[&](const std::function<void(tinyxml2::XMLElement*)>& fill){
        tinyxml2::XMLDocument doc;auto root=doc.NewElement("result");root->SetAttribute("ok",true);doc.InsertEndChild(root);
        auto actions=doc.NewElement("actions");root->InsertEndChild(actions);fill(actions);
        tinyxml2::XMLPrinter printer;doc.Print(&printer);return std::string(printer.CStr());
    };
    auto moleculeAction=[&](tinyxml2::XMLElement* actions,double x,double y){
        auto element=actions->GetDocument()->NewElement("molecule");element->SetAttribute("x",x);element->SetAttribute("y",y);element->SetText(molXml.c_str());actions->InsertEndChild(element);
    };

    // (a) request captured at (0,0)/1.0x, then the user panned and zoomed; the turn must land on
    // the viewport centre, not at the stale request origin.
    reset();setView(Vec2(0,0),1,900,600);
    ObjectId keep;require(s.doc.addText(Vec2(-420,260),12,"keep",&keep).ok(),"existing user caption fixture");s.refresh();
    auto keepPosition=s.doc.texts().get(keep)->position;auto beforeDigest=s.doc.digest();
    s.selected.clear();aiContext();
    auto panned=s.view();panned.center=Vec2(150,-80);panned.scale=1.7;panned.viewportCenter=Vec2(320,220);s.setView(panned);
    applyAI(resultXml([&](tinyxml2::XMLElement* actions){
        moleculeAction(actions,0,0);
        auto text=actions->GetDocument()->NewElement("add_text");text->SetAttribute("x",-60);text->SetAttribute("y",70);text->SetText("名称");actions->InsertEndChild(text);
        auto arrow=actions->GetDocument()->NewElement("draw_arrow");arrow->SetAttribute("style","forward");arrow->SetAttribute("x",10);arrow->SetAttribute("y",110);arrow->SetAttribute("x2",170);arrow->SetAttribute("y2",110);actions->InsertEndChild(arrow);
    }));
    Vec2 target=s.view().toDocument(s.view().viewportCenter);
    Box box=boundsOf(s.selected);
    require(!box.empty(),"the AI turn inserted selectable content");
    require(closeEnough((box.min.x+box.max.x)*.5,target.x)&&closeEnough((box.min.y+box.max.y)*.5,target.y),"molecule, caption and arrow are centred on the viewport centre after pan/zoom");
    require(s.doc.texts().get(keep)->position==keepPosition,"existing user content is not moved by the centring");
    require(std::abs(s.view().scale-1.7)<1e-9,"a fitting result never zooms the canvas");
    require(s.undo().ok()&&s.doc.digest()==beforeDigest,"the centred AI turn is a single undo step");
    require(s.undo(true).ok(),"redo restores the centred turn");
    log<<"centred_after_pan=true\nexisting_untouched=true\nsingle_undo=true\n";

    // (b) the reply also edits/moves the existing selection: only the new caption may be moved to
    // the centre, the requested move of the user's structure must stay exactly as asked.
    reset();setView(Vec2(-120,40),1.2,760,520);
    ObjectId left,right,bond;require(s.doc.addAtom(Vec2(-160,40),6,&left).ok(),"selection fixture atom");require(s.doc.addAtom(Vec2(-130,40),6,&right).ok(),"selection fixture atom 2");require(s.doc.addBond(left,right,&bond).ok(),"selection fixture bond");s.refresh();
    auto leftPosition=s.doc.atoms().get(left)->position;auto selectionDigest=s.doc.digest();
    s.selected={ObjectRef(ObjectKind::Atom,left),ObjectRef(ObjectKind::Atom,right)};aiContext();
    auto movedView=s.view();movedView.center=Vec2(60,-30);movedView.scale=1.4;s.setView(movedView);
    applyAI("<result ok='true'><actions><move_selection dx='25' dy='-10'/><add_text x='0' y='0'>新标注</add_text></actions></result>");
    require(s.doc.atoms().get(left)->position==leftPosition+Vec2(25,-10),"the requested move of the existing selection is applied exactly once");
    target=s.view().toDocument(s.view().viewportCenter);box=boundsOf(s.selected);
    require(!box.empty()&&closeEnough((box.min.x+box.max.x)*.5,target.x)&&closeEnough((box.min.y+box.max.y)*.5,target.y),"only the new caption is centred; the edited selection is not carried along");
    require(s.undo().ok()&&s.doc.digest()==selectionDigest,"centring a caption on top of a selection edit stays one undo step");
    log<<"selection_not_carried=true\n";

    // (c) docked pane: the canvas is narrower and the view centre/scale changed between request
    // and reply. The viewport centre must still be the anchor after the anamorphic resize.
    reset();setView(Vec2(-60,30),1.35,520,420);
    s.selected.clear();aiContext();
    auto dockedView=s.view();dockedView.center=Vec2(90,55);dockedView.scale=0.7;dockedView.viewportCenter=Vec2(260,210);s.setView(dockedView);
    auto dockedDigest=s.doc.digest();
    applyAI(resultXml([&](tinyxml2::XMLElement* actions){moleculeAction(actions,40,-20);auto text=actions->GetDocument()->NewElement("add_text");text->SetAttribute("x",-30);text->SetAttribute("y",60);text->SetText("停靠后");actions->InsertEndChild(text);}));
    target=s.view().toDocument(s.view().viewportCenter);
    require(closeEnough(target.x,s.view().center.x)&&closeEnough(target.y,s.view().center.y),"the viewport centre is the document-space anchor");
    box=boundsOf(s.selected);
    require(!box.empty()&&closeEnough((box.min.x+box.max.x)*.5,target.x)&&closeEnough((box.min.y+box.max.y)*.5,target.y),"a docked/narrow canvas still centres the AI turn");
    require(s.undo().ok()&&s.doc.digest()==dockedDigest,"the docked-canvas turn is one undo step");
    log<<"docked_centre=true\n";

    // (d) oversized result: it stays centred and is only shrunk enough to remain visible; the
    // canvas is never zoomed in for a small result (asserted in (a)).
    reset();setView(Vec2(0,0),1,300,220);
    s.selected.clear();aiContext();
    auto smallView=s.view();smallView.center=Vec2(-15,12);s.setView(smallView);
    applyAI(resultXml([&](tinyxml2::XMLElement* actions){
        auto path=actions->GetDocument()->NewElement("draw_path");path->SetAttribute("closed",false);
        const double pts[][2]={{-220,-160},{220,-160},{220,160},{-220,160}};
        for(const auto& pt:pts){auto point=path->GetDocument()->NewElement("point");point->SetAttribute("x",pt[0]+15);point->SetAttribute("y",pt[1]-12);path->InsertEndChild(point);}
        actions->InsertEndChild(path);
    }));
    target=s.view().toDocument(s.view().viewportCenter);box=boundsOf(s.selected);
    require(!box.empty()&&closeEnough((box.min.x+box.max.x)*.5,target.x)&&closeEnough((box.min.y+box.max.y)*.5,target.y),"an oversized result is still centred after adaptation");
    require(s.view().scale<0.999,"an oversized result is shrunk just enough to stay visible");
    log<<"oversized_centred=true\n";
    reset();s.dirty=false;ai.storageOverride=previousStorage;
    log<<"passed\n";
    std::ofstream(dir+"/ai-ux-checks.json")<<"{\"passed\":true,\"banner_visible\":true,\"config_route\":true,\"half_config\":true,\"banner_hides\":true,\"send_prompts\":true,\"narrow_visible\":true,\"docked_visible\":true,\"centred_after_pan\":true,\"existing_untouched\":true,\"selection_not_carried\":true,\"docked_centre\":true,\"oversized_centred\":true,\"single_undo\":true}";
}
