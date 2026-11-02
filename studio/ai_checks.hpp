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
