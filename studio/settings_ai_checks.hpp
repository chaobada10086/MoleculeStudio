#pragma once
struct ClipboardFixture {
    std::vector<std::pair<UINT,HANDLE>> saved;bool changed=false,available=true;
    static void release(UINT format,HANDLE handle){if(!handle)return;STGMEDIUM medium={};medium.tymed=format==CF_BITMAP?TYMED_GDI:format==CF_ENHMETAFILE?TYMED_ENHMF:format==CF_METAFILEPICT?TYMED_MFPICT:TYMED_HGLOBAL;medium.hGlobal=(HGLOBAL)handle;ReleaseStgMedium(&medium);}
    ClipboardFixture(HWND owner){
        if(!OpenClipboard(owner)){available=false;return;}UINT format=0;
        while((format=EnumClipboardFormats(format))!=0){auto original=GetClipboardData(format);auto duplicate=original?OleDuplicateData(original,format,0):nullptr;if(!duplicate){available=false;break;}saved.push_back({format,duplicate});}CloseClipboard();
    }
    bool install(HWND owner){if(!available||!OpenClipboard(owner))return false;
        const int w=28,h=20;HGLOBAL dib=GlobalAlloc(GMEM_MOVEABLE,sizeof(BITMAPINFOHEADER)+w*h*4);if(!dib){CloseClipboard();return false;}
        auto header=(BITMAPINFOHEADER*)GlobalLock(dib);if(!header){GlobalFree(dib);CloseClipboard();return false;}memset(header,0,sizeof(*header));header->biSize=sizeof(*header);header->biWidth=w;header->biHeight=h;header->biPlanes=1;header->biBitCount=32;header->biCompression=BI_RGB;auto pixels=(DWORD*)(header+1);for(int i=0;i<w*h;i++)pixels[i]=0x00187570;GlobalUnlock(dib);
        if(!EmptyClipboard()){GlobalFree(dib);CloseClipboard();return false;}changed=true;bool ok=SetClipboardData(CF_DIB,dib)!=nullptr;if(!ok)GlobalFree(dib);CloseClipboard();return ok;
    }
    ~ClipboardFixture(){
        if(changed&&OpenClipboard(nullptr)){EmptyClipboard();for(auto& item:saved)if(SetClipboardData(item.first,item.second))item.second=nullptr;CloseClipboard();}
        for(const auto& item:saved)release(item.first,item.second);
    }
};
void App::settingsAiCheck(const std::string& dir){
    auto require=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};
    if(std::getenv("MOLECULE_STUDIO_COMPLETION_FORMAT_CHECK")){
        showSettings(5);auto& sw=settingsWindow;auto edit=GetDlgItem(sw.hwnd,962);
        require(IsWindowVisible(GetDlgItem(sw.hwnd,985)),"format controls visible");
        sw.set(962,L"zzfmt=Ab2C3");
        auto apply=[&](LONG a,LONG b,int id){CHARRANGE range={a,b};::SendMessageW(edit,EM_EXSETSEL,0,(LPARAM)&range);::SendMessageW(sw.hwnd,WM_COMMAND,MAKEWPARAM(id,BN_CLICKED),0);};
        apply(6,8,987);apply(8,9,986);apply(10,11,985);
        ::SendMessageW(sw.hwnd,WM_COMMAND,MAKEWPARAM(963,BN_CLICKED),0);
        require(chemcompletion::normalize("ZZFMT")=="Ab2C3","alias case normalization");
        require(chemcompletion::normalize("aB2c3")=="Ab2C3","canonical case normalization");
        require(chemcompletion::savedFormat("Ab2C3")=="44102","rich edit style persistence");
        chemcompletion::State reload;chemcompletion::loadState(reload);require(reload.custom["zzfmt"]=="Ab2C3","alias persisted");
        auto pieces=chemcompletion::segments("Ab2C3",true);require(pieces.size()==5&&pieces[2].script==-1&&pieces[4].script==1,"custom scripts restored");
        auto t=chemcompletion::readKey(L"Format_ab2c3");require(t=="44102","format survives reload");
        sw.set(968,L"t-Bu");sw.fillCompletion();UpdateWindow(sw.hwnd);
        std::ofstream(dir+"/completion-format-check.txt")<<"passed: format buttons, rich editor, alias and canonical case variants, persisted format, subscript and superscript\n";
        chemcompletion::removeCustom("zzfmt");chemcompletion::saveFormat("Ab2C3","");
        s.dirty=false;PostMessageW(h,WM_CLOSE,0,0);return;
    }
    if(std::getenv("MOLECULE_STUDIO_GROUP_TOGGLE_CHECK")){
        _putenv_s("MOLECULE_STUDIO_GROUP_LIBRARY",(dir+"/groups.xml").c_str());customgroups::loaded()=false;customgroups::entries().clear();customgroups::readError().clear();customgroups::load();
        std::ofstream log(dir+"/group-toggle-check.txt");
        auto reset=[&](){s.cancel();s.doc=Document();s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());expandPending=false;};
        auto ready=[&](){s.refresh();s.fit();invalidate();UpdateWindow(h);UpdateWindow(canvas);ValidateRect(canvas,nullptr);};
        auto paint=[&](){require(GetUpdateRect(canvas,nullptr,FALSE)!=FALSE,"group action requests canvas repaint");auto count=canvasPaints;UpdateWindow(canvas);require(canvasPaints>count,"group action paints without blank click");};
        reset();ObjectId center,left,right,b1,b2;s.doc.addAtom(Vec2(),7,&center);s.doc.addAtom(Vec2(-30,0),6,&left);s.doc.addAtom(Vec2(30,0),6,&right);s.doc.addBond(center,left,&b1);s.doc.addBond(center,right,&b2);require(nicknames::apply(s.doc,left,"Ph").ok()&&nicknames::apply(s.doc,right,"Ph").ok(),"two abbreviation fixture");s.selected={ObjectRef(ObjectKind::Atom,left)};ready();auto original=s.doc.digest();{std::ofstream out(dir+"/original.native");integration::saveSafeNative(s.doc,out);}auto formula=studio::analyze(s.doc,s.selected);require(formula.valid&&formula.plain=="C12H11N","initial two-group formula");
        auto untouched=*s.doc.atoms().alive(right);auto otherRaw=*static_cast<const Document&>(s.doc).extensions(ObjectRef(ObjectKind::Atom,right)).findValue("studio.nickname.fragment");
        execute(Button("","","","@expandGroups"));paint();require(s.doc.stats().aliveAtoms==8,"single selected abbreviation alone expands");require(s.doc.atomLabel(right)=="Ph"&&s.doc.atoms().alive(right)->position==untouched.position&&*static_cast<const Document&>(s.doc).extensions(ObjectRef(ObjectKind::Atom,right)).findValue("studio.nickname.fragment")==otherRaw,"unselected abbreviation identity and definition preserved");require(s.doc.bonds().alive(b2)->beginAtom==center&&s.doc.bonds().alive(b2)->endAtom==right,"unselected external bond preserved");
        auto expanded=s.doc.digest();auto undoResult=s.undo();{std::ofstream out(dir+"/undone.native");integration::saveSafeNative(s.doc,out);}log<<"undo_ok="<<undoResult.ok()<<" history="<<s.history.cursor()<<" original="<<original<<" undone="<<s.doc.digest()<<"\n";log.flush();require(undoResult.ok()&&s.doc.digest()==original,"selected expansion atomic undo");require(s.undo(true).ok()&&s.doc.digest()==expanded,"selected expansion redo");
        std::ofstream(dir+"/partial-expanded.cdxml")<<persistence::writeCdxmlToString(s.doc);
        execute(Button("","","","@collapseGroups"));paint();require(s.doc.stats().aliveAtoms==3&&s.selected.size()==1,"collapse recognized subgraph only");require(!functional_groups::definition(s.doc,left).empty()&&s.doc.atomLabel(right)=="Ph","collapse restores abbreviation with unselected neighbor intact");auto collapsed=s.doc.digest();auto foldedFormula=studio::analyze(s.doc,s.selected);require(foldedFormula.valid&&foldedFormula.plain==formula.plain&&std::abs(foldedFormula.exactMass-formula.exactMass)<1e-8,"collapse preserves formula and exact mass");
        require(s.undo().ok()&&s.doc.digest()==expanded,"collapse atomic undo");require(s.undo(true).ok()&&s.doc.digest()==collapsed,"collapse redo");std::ofstream(dir+"/recollapsed.cdxml")<<persistence::writeCdxmlToString(s.doc);
        expandPending=false;execute(Button("","","","@expandGroups"));paint();std::ofstream(dir+"/reexpanded.cdxml")<<persistence::writeCdxmlToString(s.doc);require(s.doc.stats().aliveAtoms==8,"recollapsed fragment expands again");
        s.selected={ObjectRef(ObjectKind::Atom,right)};expandPending=false;execute(Button("","","","@expandGroups"));paint();require(s.doc.stats().aliveAtoms==13,"independently expand second abbreviation");
        if(!customgroups::entries().empty()){
            auto name=customgroups::entries().front().name;reset();ObjectId n,g,b;s.doc.addAtom(Vec2(),7,&n);s.doc.addAtom(Vec2(30,0),6,&g);s.doc.addBond(n,g,&b);require(nicknames::apply(s.doc,g,name).ok(),"custom fixture");s.selected={ObjectRef(ObjectKind::Atom,g)};ready();std::ofstream(dir+"/custom-before.cdxml")<<persistence::writeCdxmlToString(s.doc);auto before=studio::analyze(s.doc,s.selected);require(before.valid,"custom formula valid");double start=now();execute(Button("","","","@expandGroups"));paint();log<<"custom_expand_ms="<<now()-start<<"\n";std::ofstream(dir+"/custom-expanded.cdxml")<<persistence::writeCdxmlToString(s.doc);
            start=now();execute(Button("","","","@collapseGroups"));paint();log<<"custom_collapse_ms="<<now()-start<<"\n";require(s.doc.atomLabel(g)==name&&s.doc.stats().aliveAtoms==2,"custom library name preferred when collapsing");auto after=studio::analyze(s.doc,s.selected);require(after.valid&&before.plain==after.plain&&std::abs(before.exactMass-after.exactMass)<1e-8,"custom collapse formula invariant");std::ofstream(dir+"/custom-recollapsed.cdxml")<<persistence::writeCdxmlToString(s.doc);
        }
        reset();{ObjectId c,g,f,cl,br,b,extra;s.doc.addAtom(Vec2(),6,&c);s.doc.addAtom(Vec2(30,0),6,&g);s.doc.addAtom(Vec2(-15,-25),9,&f);s.doc.addAtom(Vec2(-15,25),17,&cl);s.doc.addAtom(Vec2(0,30),35,&br);s.doc.addBond(c,g,&b);s.doc.setBondDisplay(b,BondDisplay::Wedge);s.doc.addBond(c,f,&extra);s.doc.addBond(c,cl,&extra);s.doc.addBond(c,br,&extra);nicknames::apply(s.doc,g,"Ph");s.selected={ObjectRef(ObjectKind::Atom,g)};ready();std::ofstream(dir+"/stereo-before.cdxml")<<persistence::writeCdxmlToString(s.doc);execute(Button("","","","@expandGroups"));paint();execute(Button("","","","@collapseGroups"));paint();require(s.doc.bonds().alive(b)&&s.doc.bonds().alive(b)->beginAtom==c&&s.doc.bonds().alive(b)->endAtom==g&&s.doc.bonds().alive(b)->display==BondDisplay::Wedge,"external stereobond ID, direction and display preserved");std::ofstream(dir+"/stereo-after.cdxml")<<persistence::writeCdxmlToString(s.doc);}
        reset();ObjectId a,b,bondId;s.doc.addAtom(Vec2(),6,&a);s.doc.addAtom(Vec2(30,0),6,&b);s.doc.addBond(a,b,&bondId);s.activateCommand("chooseSelectionTool_Marquee");ready();auto originalR=s.doc.digest();pointerOverCanvas=true;pointerPixel=s.view().toDevice(s.doc.atoms().alive(b)->position);SendMessageW(canvas,WM_KEYDOWN,'R',0);paint();require(s.doc.atomLabel(b)=="R"&&s.doc.stats().aliveAtoms==2&&s.doc.stats().aliveBonds==1,"R key marks existing terminal without extra atom or bond");SendMessageW(canvas,WM_CHAR,'r',0);require(!textEditor.active,"R key character consumed");auto marked=s.doc.digest();require(s.undo().ok()&&s.doc.digest()==originalR,"R label one-step undo");require(s.undo(true).ok()&&s.doc.digest()==marked,"R label redo");SendMessageW(canvas,WM_KEYDOWN,'R',0);require(s.doc.stats().aliveAtoms==2,"repeated R does not duplicate group");
        auto canvasDigest=s.doc.digest();execute(Button("","","","@customGroups"));require(settingsWindow.category==6&&IsWindowVisible(GetDlgItem(settingsWindow.hwnd,974)),"template entry opens custom group manager");require(IsWindowVisible(GetDlgItem(settingsWindow.hwnd,973)),"delete button visible");
        size_t definitions=customgroups::entries().size();if(definitions){auto list=GetDlgItem(settingsWindow.hwnd,974);::SendMessageW(list,LB_SETCURSEL,0,0);::SendMessageW(list,WM_KEYDOWN,VK_DELETE,0);require(customgroups::entries().size()==definitions-1&&size_t(::SendMessageW(list,LB_GETCOUNT,0,0))==definitions-1,"Delete removes selected definition and refreshes list");require(s.doc.digest()==canvasDigest,"library deletion leaves document intact");}
        ShowWindow(settingsWindow.hwnd,SW_HIDE);switchTab(5);screenshot(dir+"/templates-custom-groups.png");
        log<<"single_selection=true\ncollapse=true\ncustom_roundtrip=true\nR_endpoint=true\nrepaint_without_click=true\ntemplate_entry=true\nDelete_key=true\npassed\n";s.dirty=false;PostMessageW(h,WM_CLOSE,0,0);return;
    }
    if(std::getenv("MOLECULE_STUDIO_EXPAND_REFRESH_CHECK")){
        _putenv_s("MOLECULE_STUDIO_GROUP_LIBRARY",(dir+"/groups.xml").c_str());customgroups::loaded()=false;customgroups::entries().clear();customgroups::readError().clear();customgroups::load();
        std::vector<std::string> labels={"Ph"};if(!customgroups::entries().empty())labels.push_back(customgroups::entries().front().name);
        std::ofstream log(dir+"/expand-refresh-check.txt");
        for(auto label:labels){
            s.cancel();s.doc=Document();s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());ObjectId n,g,b;
            s.doc.addAtom(Vec2(),7,&n);s.doc.addAtom(Vec2(14.4,0),6,&g);s.doc.addBond(n,g,&b);require(nicknames::apply(s.doc,g,label).ok(),"create nickname fixture");s.refresh();s.selected=s.all();s.fit();
            invalidate();UpdateWindow(h);UpdateWindow(canvas);ValidateRect(canvas,nullptr);
            std::vector<uint8_t> before,after;require(gl->readPixels(0,0,s.renderView.widthPx,s.renderView.heightPx,&before).ok(),"read collapsed picture");
            auto original=s.doc.digest();auto paints=canvasPaints;double start=now();
            execute(Button("","","","@expandGroups"));
            require(expandPending&&s.doc.digest()!=original,"toolbar expanded molecular graph");
            require(GetUpdateRect(canvas,nullptr,FALSE)!=FALSE,"expansion itself invalidates canvas without another click");
            UpdateWindow(canvas);require(canvasPaints>paints,"expansion paints without further pointer input");
            require(gl->readPixels(0,0,s.renderView.widthPx,s.renderView.heightPx,&after).ok()&&before!=after,"expanded structure immediately replaces collapsed picture");
            log<<label<<" expand_and_paint_ms="<<now()-start<<"\n";log.flush();
            auto expanded=s.doc.digest();require(s.undo().ok()&&s.doc.digest()==original,"expansion remains undoable");require(s.undo(true).ok()&&s.doc.digest()==expanded,"expansion redo");
            // Re-arm the existing second-click cleanup behavior after undo/redo.
            expandPending=true;expandDigest=s.doc.digest();expandDocument=s.doc.id();expandSelection=s.selected;
            invalidate();UpdateWindow(h);UpdateWindow(canvas);ValidateRect(canvas,nullptr);paints=canvasPaints;
            execute(Button("","","","@expandGroups"));require(!expandPending,"second toolbar click runs cleanup");
            require(GetUpdateRect(canvas,nullptr,FALSE)!=FALSE,"cleanup itself invalidates canvas");UpdateWindow(canvas);require(canvasPaints>paints,"cleanup paints without further pointer input");
        }
        s.dirty=false;log<<"passed\n";PostMessageW(h,WM_CLOSE,0,0);return;
    }
    if(std::getenv("MOLECULE_STUDIO_GROUP_PREVIEW_CHECK")){
        _putenv_s("MOLECULE_STUDIO_GROUP_LIBRARY",(dir+"/groups.xml").c_str());customgroups::loaded()=false;customgroups::entries().clear();customgroups::readError().clear();customgroups::load();require(!customgroups::entries().empty(),"custom fixture loaded");
        const auto first=customgroups::entries().front();customgroups::entries().push_back({"PhPreview",nickdata::find("Ph")->fragment});customgroups::entries().push_back({"BrokenPreview","broken"});
        studio_i18n::pathOverride()=W(dir+"/interface.ini");ai.storageOverride=W(dir+"/ai");CreateDirectoryW(ai.storageOverride.c_str(),nullptr);
        showSettings(6);auto& sw=settingsWindow;auto original=s.doc.digest();
        auto choose=[&](int row){::SendDlgItemMessageW(sw.hwnd,974,LB_SETCURSEL,row,0);::SendMessageW(sw.hwnd,WM_COMMAND,MAKEWPARAM(974,LBN_SELCHANGE),(LPARAM)GetDlgItem(sw.hwnd,974));};
        choose(0);require(sw.groupPreview&&sw.previewName==first.name&&sw.previewError.empty(),"selected custom group previews");
        std::vector<uint8_t> a,b;
        auto pixels=[&](std::vector<uint8_t>& out){InvalidateRect(sw.hwnd,nullptr,FALSE);UpdateWindow(sw.hwnd);require(sw.groupRenderer&&sw.groupRenderer->readPixels(0,0,sw.previewRect.right-sw.previewRect.left-int(12*GetDpiForWindow(sw.hwnd)/96.),sw.previewRect.bottom-sw.previewRect.top-int(24*GetDpiForWindow(sw.hwnd)/96.),&out).ok(),"read preview pixels");};
        pixels(a);choose(1);pixels(b);require(a!=b,"selection switches structure preview");
        choose(2);require(!sw.groupPreview&&!sw.previewError.empty(),"corrupt entry has clear preview error");choose(0);
        auto formulaFixture=[&](){Session v;ObjectId n,g,bond;v.doc.addAtom(Vec2(),7,&n);v.doc.addAtom(Vec2(14.4,0),6,&g);v.doc.addBond(n,g,&bond);require(nicknames::apply(v.doc,g,first.name).ok(),"apply user definition");v.refresh();v.selected=v.all();auto before=v.doc.digest();auto formula=studio::analyze(v.doc,v.selected);require(formula.valid,"user definition formula recognized");auto xml=persistence::writeCdxmlToString(v.doc);std::ofstream(dir+"/user-before.cdxml")<<xml;auto t=App::now();std::string expanded=nicknameexpand::expand(xml);double ms=App::now()-t;std::ofstream(dir+"/user-expanded.cdxml")<<expanded;require(v.doc.digest()==before,"analysis preserves source");std::ofstream(dir+"/user-formula.txt")<<formula.plain<<"\nmass="<<formula.averageMass<<" native_ms="<<ms<<"\n";};formulaFixture();
        auto capture=[&](const char* name){RedrawWindow(sw.hwnd,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW|RDW_ALLCHILDREN);RECT r;GetWindowRect(sw.hwnd,&r);HDC dc=GetDC(sw.hwnd),mem=CreateCompatibleDC(dc);auto bmp=CreateCompatibleBitmap(dc,r.right-r.left,r.bottom-r.top);auto old=SelectObject(mem,bmp);PrintWindow(sw.hwnd,mem,2);SelectObject(mem,old);Bitmap image(bmp,nullptr);UINT n=0,bytes=0;GetImageEncodersSize(&n,&bytes);std::vector<BYTE> data(bytes);auto codecs=(ImageCodecInfo*)data.data();GetImageEncoders(n,bytes,codecs);for(UINT i=0;i<n;++i)if(wcscmp(codecs[i].MimeType,L"image/png")==0)image.Save(W(dir+"/"+name+".png").c_str(),&codecs[i].Clsid,0);DeleteObject(bmp);DeleteDC(mem);ReleaseDC(sw.hwnd,dc);};
        capture("group-preview");UINT dpi=GetDpiForWindow(sw.hwnd);SetWindowPos(sw.hwnd,0,0,0,MulDiv(740,dpi,96),MulDiv(540,dpi,96),SWP_NOMOVE|SWP_NOZORDER);capture("group-preview-small");
        require(s.doc.digest()==original,"preview leaves canvas unchanged");sw.select(0);ShowWindow(sw.hwnd,SW_HIDE);s.dirty=false;std::ofstream(dir+"/group-preview-check.json")<<"{\"passed\":true,\"selection_switch\":true,\"user_formula\":true,\"source_preserved\":true,\"corrupt_entry\":true}";PostMessageW(h,WM_CLOSE,0,0);return;
    }
    if(std::getenv("MOLECULE_STUDIO_FULL_I18N")){
        CreateDirectoryW(W(dir).c_str(),nullptr);studio_i18n::pathOverride()=W(dir+"/language-test.ini");studio_i18n::current()=0;ai.storageOverride=W(dir+"/ai");CreateDirectoryW(ai.storageOverride.c_str(),nullptr);
        showSettings(0);auto digest=s.doc.digest();auto parent=settingsWindow.hwnd;
        auto button=studio_i18n::createWindowEx(0,L"BUTTON",L"开始",WS_CHILD,0,0,100,30,parent,(HMENU)7000,GetModuleHandleW(0),0);
        auto editFixture=studio_i18n::createWindowEx(0,L"EDIT",L"开始 表格 用户输入 123",WS_CHILD,0,0,100,30,parent,(HMENU)7001,GetModuleHandleW(0),0);
        auto combo=studio_i18n::createWindowEx(0,L"COMBOBOX",L"",WS_CHILD|CBS_DROPDOWNLIST,0,0,100,100,parent,(HMENU)7002,GetModuleHandleW(0),0);
        studio_i18n::sendMessage(combo,CB_ADDSTRING,0,(LPARAM)L"纵向");studio_i18n::sendMessage(combo,CB_ADDSTRING,0,(LPARAM)L"横向");studio_i18n::sendMessage(combo,CB_SETCURSEL,1,0);
        auto list=studio_i18n::createWindowEx(0,WC_LISTVIEWW,L"",WS_CHILD|LVS_REPORT,0,0,100,100,parent,(HMENU)7003,GetModuleHandleW(0),0);LVCOLUMNW col={};col.mask=LVCF_TEXT;col.pszText=(wchar_t*)L"快捷键";studio_i18n::sendMessage(list,LVM_INSERTCOLUMNW,0,(LPARAM)&col);LVITEMW item={};item.mask=LVIF_TEXT;item.pszText=(wchar_t*)L"原子热点";studio_i18n::sendMessage(list,LVM_INSERTITEMW,0,(LPARAM)&item);
        auto tabFixture=studio_i18n::createWindowEx(0,WC_TABCONTROLW,L"",WS_CHILD,0,0,100,100,parent,(HMENU)7004,GetModuleHandleW(0),0);TCITEMW tabItem={};tabItem.mask=TCIF_TEXT;tabItem.pszText=(wchar_t*)L"结构绘图";studio_i18n::sendMessage(tabFixture,TCM_INSERTITEMW,0,(LPARAM)&tabItem);
        auto actual=[](HWND h){wchar_t v[4096]={};::GetWindowTextW(h,v,4096);return std::wstring(v);};
        auto capture=[&](const std::string& name){RedrawWindow(h,0,0,RDW_INVALIDATE|RDW_UPDATENOW|RDW_ALLCHILDREN);RECT r;GetWindowRect(h,&r);int width=r.right-r.left,height=r.bottom-r.top;HDC dc=GetDC(h),mem=CreateCompatibleDC(dc);auto bmp=CreateCompatibleBitmap(dc,width,height);auto old=SelectObject(mem,bmp);PrintWindow(h,mem,2);SelectObject(mem,old);Bitmap image(bmp,nullptr);UINT n=0,bytes=0;GetImageEncodersSize(&n,&bytes);std::vector<BYTE> data(bytes);auto codecs=(ImageCodecInfo*)data.data();GetImageEncoders(n,bytes,codecs);for(UINT i=0;i<n;++i)if(wcscmp(codecs[i].MimeType,L"image/png")==0)image.Save(W(dir+"/"+name+".png").c_str(),&codecs[i].Clsid,0);DeleteObject(bmp);DeleteDC(mem);ReleaseDC(h,dc);};
        SetWindowPos(h,0,0,0,1500,950,SWP_NOMOVE|SWP_NOZORDER);ribbonCompact=false;ribbonVisible=true;ShowWindow(parent,SW_HIDE);
        for(int language=0;language<15;++language){studio_i18n::current()=language;settingsWindow.translate();studio_i18n::refreshAll();layout();
            wchar_t tabCaption[1024]={};TCITEMW tabRead={};tabRead.mask=TCIF_TEXT;tabRead.pszText=tabCaption;tabRead.cchTextMax=1024;::SendMessageW(tabFixture,TCM_GETITEMW,0,(LPARAM)&tabRead);require(std::wstring(tabCaption)==studio_i18n::tr(L"结构绘图"),"native tabs relocalize");
            require(actual(button)==studio_i18n::tr(L"开始"),"existing button relocalizes");require(actual(editFixture)==L"开始 表格 用户输入 123","user input remains verbatim");
            require(actual(combo)==studio_i18n::tr(L"横向"),"combo visible label localized");wchar_t original[100]={};studio_i18n::getWindowText(combo,original,100);require(std::wstring(original)==L"横向","combo retains canonical value");require((GetWindowLongPtrW(combo,GWL_STYLE)&WS_VISIBLE)==0,"refresh does not show hidden controls");
            wchar_t cell[1024]={};LVITEMW read={};read.iSubItem=0;read.pszText=cell;read.cchTextMax=1024;::SendMessageW(list,LVM_GETITEMTEXTW,0,(LPARAM)&read);require(std::wstring(cell)==studio_i18n::tr(L"原子热点"),"existing list cells relocalize");
            for(auto& row:studio_i18n::catalog()){require(row.second.text[language]&&*row.second.text[language],"all locale entries exist");if(language!=0&&language!=1&&language!=3)require(!studio_i18n::hasHan(row.second.text[language]),"no untranslated Chinese in locale catalog");require(std::wstring(row.second.text[language]).find(L'\u2047')==std::wstring::npos,"no tokenizer replacement symbols");}
            require(studio_i18n::tr(L"打开失败：C:\\temp\\开始.r62doc").find(L"C:\\temp\\开始.r62doc")!=std::wstring::npos,"paths in diagnostics are unchanged");
            if(language==2){for(int tab=0;tab<6;++tab){ribbonTab=tab;layout();capture("en-tab-"+std::to_string(tab));}ribbonTab=0;layout();}
            else if(language==0||language==3||language==5||language==6||language==8||language==11||language==12)capture("home-"+std::to_string(language));
        }
        require(s.doc.digest()==digest,"language changes leave document unchanged");
        DestroyWindow(tabFixture);DestroyWindow(button);DestroyWindow(editFixture);DestroyWindow(combo);DestroyWindow(list);studio_i18n::current()=0;
        std::ofstream report(dir+"/full-i18n-results.json");report<<"{\"passed\":true,\"languages\":15,\"catalog_keys\":"<<studio_i18n::catalog().size()<<",\"live_controls\":true,\"combo_canonical_value\":true,\"hidden_controls\":true,\"user_input_verbatim\":true,\"document_unchanged\":true,\"no_chinese_leaks\":true,\"english_tabs\":6}";PostQuitMessage(0);return;
    }
    if(std::getenv("MOLECULE_STUDIO_SETTINGS_ONLY")){
        CreateDirectoryW(W(dir).c_str(),nullptr);studio_i18n::pathOverride()=W(dir+"/language-test.ini");studio_i18n::current()=0;
        ai.storageOverride=W(dir+"/ai");CreateDirectoryW(ai.storageOverride.c_str(),nullptr);
        showSettings(0);auto& sw=settingsWindow;auto digest=s.doc.digest();
        require(!sw.completionReady&&!sw.groupsReady,"hidden lists are lazy");
        sw.set(910,L"18.5");sw.set(950,L"User draft stays unchanged / 用户草稿");
        for(int category=0;category<8;++category){sw.select(category);UpdateWindow(sw.hwnd);
            for(int id=910;id<=981;++id)if(sw.group(id)>=0&&GetDlgItem(sw.hwnd,id))require(bool(IsWindowVisible(GetDlgItem(sw.hwnd,id)))==(sw.group(id)==category),"exclusive page visibility");}
        require(sw.completionReady&&sw.groupsReady,"lists fill on first use");
        auto capture=[&](const std::string& file){UpdateWindow(sw.hwnd);RECT r;GetWindowRect(sw.hwnd,&r);int width=r.right-r.left,height=r.bottom-r.top;HDC dc=GetDC(sw.hwnd),mem=CreateCompatibleDC(dc);HBITMAP bmp=CreateCompatibleBitmap(dc,width,height);auto old=SelectObject(mem,bmp);PrintWindow(sw.hwnd,mem,2);SelectObject(mem,old);Bitmap image(bmp,nullptr);UINT n=0,bytes=0;GetImageEncodersSize(&n,&bytes);std::vector<BYTE> data(bytes);auto codecs=(ImageCodecInfo*)data.data();GetImageEncoders(n,bytes,codecs);for(UINT i=0;i<n;++i)if(wcscmp(codecs[i].MimeType,L"image/png")==0)image.Save(W(dir+"/"+file+".png").c_str(),&codecs[i].Clsid,0);DeleteObject(bmp);DeleteDC(mem);ReleaseDC(sw.hwnd,dc);};
        for(int language=0;language<15;++language){SendDlgItemMessageW(sw.hwnd,980,CB_SETCURSEL,language,0);SendMessageW(sw.hwnd,WM_COMMAND,MAKEWPARAM(980,CBN_SELCHANGE),0);
            require(studio_i18n::current()==language&&studio_i18n::load()==language,"language persists");
            require(sw.text(800)==studio_i18n::tr(L"保存设置"),"button translated");
            for(auto& row:studio_i18n::catalog())require(row.second.text[language]&&*row.second.text[language],"complete language catalog");
            require(sw.text(910)==L"18.5"&&sw.text(950)==L"User draft stays unchanged / 用户草稿","draft survives language change");
            sw.select(7);capture("language-"+std::to_string(language));sw.select(0);capture("drawing-"+std::to_string(language));
        }
        studio_i18n::current()=0;sw.translate();
        sw.set(968,L"SO4");
        SendMessageW(sw.hwnd,WM_TIMER,2,0);require(!sw.completionKeys.empty(),"debounced formula search");
        sw.set(968,L"");SendMessageW(sw.hwnd,WM_TIMER,2,0);
        // Compare original eager visibility/layout switching with optimized switching
        // in the same binary, window and warmed data. Include synchronous paint.
        auto legacyLayout=[&](){
        RECT r;GetClientRect(sw.hwnd,&r);double dpi=GetDpiForWindow(sw.hwnd)/96.;auto px=[&](int n){return int(n*dpi);};int w=r.right,h=r.bottom,x=px(202),right=w-px(30),content=right-x;
        auto move=[&](int id,int a,int b,int c,int d){MoveWindow(GetDlgItem(sw.hwnd,id),a,b,std::max(1,c),std::max(1,d),TRUE);};
        int order[]={0,1,4,2,3,5,6};for(int i=0;i<7;i++)move(810+order[i],px(16),px(87+i*46),px(146),px(40));
        move(916,x,px(104),content,px(200));move(914,x,px(166),px(230),px(200));move(915,x+px(250),px(166),px(86),px(32));move(910,x,px(240),px(160),px(32));move(911,x+px(204),px(240),px(160),px(32));move(912,x,px(292),content-px(158),px(30));move(913,right-px(144),px(292),px(144),px(34));
        move(917,x,px(332),content,px(30));
        move(920,x,px(134),content,px(32));move(921,x,px(202),content,px(32));move(922,x,px(242),content,px(26));
        move(923,x,px(308),std::max(px(180),content-px(120)),px(220));move(924,right-px(108),px(308),px(108),px(32));move(925,x,px(354),px(118),px(34));move(926,x,px(400),content,px(40));
        move(940,x,px(145),px(200),px(230));move(941,x+px(220),px(145),px(144),px(120));
        move(942,x,px(222),px(160),px(32));move(943,x+px(204),px(222),px(160),px(32));
        move(944,x,px(305),px(160),px(32));move(945,x+px(204),px(305),px(160),px(32));
        move(946,x,px(352),content,px(26));
        move(930,x,px(143),px(180),px(36));move(931,x,px(205),content,px(200));move(932,x,px(425),content,px(28));
        move(950,x,px(142),content,std::max(px(180),h-px(340)));move(951,x,h-px(162),px(158),px(34));move(952,x,px(108),content,px(24));move(953,x,h-px(120),content,px(24));
        // 基团补齐页：总开关、公式排版、可勾选缩写清单、自定义缩写。
        move(969,x,px(96),content,px(26));move(966,x,px(128),content,px(26));
        move(960,x,px(162),px(110),px(22));move(968,x+px(114),px(158),content-px(114),px(28));
        move(970,x,px(108),content,px(100));move(971,x,px(224),content-px(150),px(30));move(972,right-px(138),px(224),px(138),px(30));move(974,x,px(270),content,std::max(px(100),h-px(420)));move(973,right-px(138),h-px(140),px(138),px(30));
        {int listTop=px(190),listHeight=std::max(px(96),h-listTop-px(246));move(961,x,listTop,content,listHeight);
         auto list=GetDlgItem(sw.hwnd,961);if(list){
            int check=px(26),origin=px(86),nameWidth=std::max(px(120),content-check-origin-px(28));
            SendMessageW(list,LVM_SETCOLUMNWIDTH,0,WPARAM(check));SendMessageW(list,LVM_SETCOLUMNWIDTH,1,WPARAM(nameWidth));SendMessageW(list,LVM_SETCOLUMNWIDTH,2,WPARAM(origin));
         }}
        move(962,x,h-px(196),std::max(px(120),content-px(270)),px(30));
        move(963,right-px(250),h-px(196),px(108),px(30));move(964,right-px(134),h-px(196),px(134),px(30));
        move(965,x,h-px(156),px(150),px(30));move(967,x+px(164),h-px(156),std::max(px(120),content-px(164)),px(30));
        move(802,x,h-px(88),content,px(28));move(800,right-px(228),h-px(46),px(126),px(32));move(801,right-px(90),h-px(46),px(90),px(32));
    };
        auto bench=[&](bool legacy){sw.paintCacheDisabled=legacy;std::vector<double> samples;for(int round=0;round<4;++round)for(int category=0;category<7;++category){auto begin=std::chrono::steady_clock::now();
            if(legacy){sw.category=category;for(int id=910;id<=974;++id){int g=sw.group(id);if(g>=0)ShowWindow(GetDlgItem(sw.hwnd,id),g==category?SW_SHOW:SW_HIDE);}for(int id=810;id<=816;++id)InvalidateRect(GetDlgItem(sw.hwnd,id),0,TRUE);legacyLayout();InvalidateRect(sw.hwnd,0,TRUE);}
            else sw.select(category);RedrawWindow(sw.hwnd,0,0,RDW_UPDATENOW|RDW_ALLCHILDREN);samples.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count());}
            std::sort(samples.begin(),samples.end());return samples[size_t(samples.size()*.95)];};
        sw.select(6);double oldMs=bench(true),newMs=bench(false);sw.paintCacheDisabled=false;sw.select(7);
        require(s.doc.digest()==digest,"settings navigation leaves document unchanged");
        require(!studio_i18n::save(15)&&!studio_i18n::save(-1),"invalid languages rejected");
        WritePrivateProfileStringW(L"Interface",L"Language",L"invalid",studio_i18n::path().c_str());require(studio_i18n::load()==0,"unknown language falls back to Chinese");studio_i18n::save(0);
        std::ofstream report(dir+"/settings-results.json");report<<"{\"passed\":true,\"languages\":15,\"catalog_keys\":"<<studio_i18n::catalog().size()<<",\"legacy_switch_p95_ms\":"<<oldMs<<",\"optimized_switch_p95_ms\":"<<newMs<<",\"draft_preserved\":true,\"document_unchanged\":true,\"lazy_lists\":true,\"visibility\":true,\"persistence\":true}";
        PostQuitMessage(0);return;
    }
    CreateDirectoryW(W(dir+"/ai").c_str(),nullptr);ai.storageOverride=W(dir+"/ai");drawingSettings.path=W(dir+"/drawing.ini");
    showSettings(0);require(IsWindow(settingsWindow.hwnd),"settings window exists");
    require(IsWindowVisible(GetDlgItem(settingsWindow.hwnd,910))&&!IsWindowVisible(GetDlgItem(settingsWindow.hwnd,920)),"drawing category controls");
    bool completionVisible=IsWindowVisible(GetDlgItem(settingsWindow.hwnd,917))!=0;require(completionVisible,"group completion toggle visible in settings");
    {auto before=s.doc.digest();bool original=chemcompletion::enabled();auto apply=settingsWindow.applyDrawing;int applied=0;
        settingsWindow.applyDrawing=[&](const DrawingSettings&,bool){++applied;};
        CheckDlgButton(settingsWindow.hwnd,917,original?BST_UNCHECKED:BST_CHECKED);SendMessageW(settingsWindow.hwnd,WM_COMMAND,MAKEWPARAM(917,BN_CLICKED),0);
        bool isolated=settingsWindow.completionDirty&&!settingsWindow.dirty[0],saved=settingsWindow.save(),changed=chemcompletion::enabled()!=original;
        bool persisted=(GetPrivateProfileIntW(L"Text",L"CompleteGroupLabels",original?1:0,chemcompletion::settingsPath().c_str())!=0)==!original;
        bool restored=chemcompletion::saveEnabled(original);settingsWindow.applyDrawing=apply;CheckDlgButton(settingsWindow.hwnd,917,original?BST_CHECKED:BST_UNCHECKED);bool documentUnchanged=s.doc.digest()==before;
        {std::ofstream result(dir+"/completion-check.json");result<<std::boolalpha<<"{\"toggle_visible\":"<<completionVisible<<",\"isolated_preference_dirty\":"<<isolated<<",\"saved\":"<<saved<<",\"runtime_changed\":"<<changed<<",\"persisted\":"<<persisted<<",\"original_preference_restored\":"<<restored<<",\"drawing_apply_count\":"<<applied<<",\"document_unchanged\":"<<documentUnchanged<<"}";}
        require(isolated&&saved&&changed&&persisted&&restored&&applied==0&&documentUnchanged,"completion preference persists without applying document drawing styles");}
    {// 基团补齐页：开关、公式排版、可勾选清单与自定义缩写
        settingsWindow.select(5);
        auto list=GetDlgItem(settingsWindow.hwnd,961);
        bool pageVisible=IsWindowVisible(GetDlgItem(settingsWindow.hwnd,969))&&IsWindowVisible(GetDlgItem(settingsWindow.hwnd,966))&&IsWindowVisible(list);
        require(pageVisible,"group completion page shows its controls");
        int rows=list?int(SendMessageW(list,LVM_GETITEMCOUNT,0,0)):0;
        require(rows>0&&size_t(rows)==chemcompletion::list().size(),"completion list is populated from the dictionary");
        settingsWindow.set(968,L"Boc");settingsWindow.fillCompletion();require(int(SendMessageW(list,LVM_GETITEMCOUNT,0,0))>0&&int(SendMessageW(list,LVM_GETITEMCOUNT,0,0))<rows,"search filters abbreviations");settingsWindow.set(968,L"");settingsWindow.fillCompletion();
        auto entries=chemcompletion::list();auto sample=entries.front();
        ListView_SetCheckState(list,0,sample.enabled?FALSE:TRUE);
        NMLISTVIEW view={};view.hdr.hwndFrom=list;view.hdr.idFrom=961;view.hdr.code=LVN_ITEMCHANGED;view.iItem=0;view.uChanged=LVIF_STATE;
        SendMessageW(settingsWindow.hwnd,WM_NOTIFY,961,(LPARAM)&view);
        bool switched=false;for(const auto& entry:chemcompletion::list())if(entry.key==sample.key)switched=entry.enabled==!sample.enabled;
        require(switched,"list checkbox switches exactly one abbreviation");
        chemcompletion::setEnabled(sample.key,sample.enabled);settingsWindow.fillCompletion();
        // The page's master switch mirrors the one in 结构绘图.
        bool original=chemcompletion::enabled();
        CheckDlgButton(settingsWindow.hwnd,969,original?BST_UNCHECKED:BST_CHECKED);
        SendMessageW(settingsWindow.hwnd,WM_COMMAND,MAKEWPARAM(969,BN_CLICKED),0);
        bool mirrored=IsDlgButtonChecked(settingsWindow.hwnd,917)==(original?BST_UNCHECKED:BST_CHECKED)&&settingsWindow.completionDirty;
        CheckDlgButton(settingsWindow.hwnd,969,original?BST_CHECKED:BST_UNCHECKED);CheckDlgButton(settingsWindow.hwnd,917,original?BST_CHECKED:BST_UNCHECKED);settingsWindow.completionDirty=false;
        require(mirrored,"completion page master switch mirrors the drawing page");

        { // Exercise real search notifications and filtered checkbox identity.
            auto query=[&](const wchar_t* q){settingsWindow.set(968,q);SendMessageW(settingsWindow.hwnd,WM_COMMAND,MAKEWPARAM(968,EN_CHANGE),0);SendMessageW(settingsWindow.hwnd,WM_TIMER,2,0);};
            query(L"SO4");bool formulaRow=false;
            for(int i=0;i<int(settingsWindow.completionKeys.size());++i)if(settingsWindow.completionKeys[i].empty()){
                LVITEMW item={};item.stateMask=LVIS_STATEIMAGEMASK;auto image=SendMessageW(list,LVM_GETITEMSTATE,i,LVIS_STATEIMAGEMASK);require(image==0,"formula rows have no per-entry checkbox");formulaRow=true;
                NMLISTVIEW change={};change.hdr.hwndFrom=list;change.hdr.idFrom=961;change.hdr.code=LVN_ITEMCHANGED;change.iItem=i;change.uChanged=LVIF_STATE;auto disabled=chemcompletion::state().disabled;SendMessageW(settingsWindow.hwnd,WM_NOTIFY,961,(LPARAM)&change);require(chemcompletion::state().disabled==disabled,"formula selection does not alter abbreviation state");
            }
            require(formulaRow&&settingsWindow.text(802).find(L"公式")!=std::wstring::npos,"SO4 exposes formula scope and explanation");
            query(L"t bu");require(!settingsWindow.completionKeys.empty()&&settingsWindow.completionKeys[0]=="tbu","normalized filtered row keeps correct checkbox key");
            bool original=true;for(auto e:chemcompletion::list())if(e.key=="tbu")original=e.enabled;
            auto disabled=chemcompletion::state().disabled;ListView_SetCheckState(list,0,original?FALSE:TRUE);
            require((chemcompletion::state().disabled.count("tbu")!=0)==original,"filtered checkbox toggles matching abbreviation");require(chemcompletion::setEnabled("tbu",original),"restore checkbox state");require(chemcompletion::state().disabled==disabled,"other keys untouched");
            query(L"zzzzzzzzzzzzzzzz");require(settingsWindow.completionKeys.empty()&&settingsWindow.text(802).find(L"没有匹配")!=std::wstring::npos,"empty search explains no match");query(L"");
            std::ofstream(dir+"/fuzzy-ui-checks.json")<<"{\"passed\":true,\"so4_visible\":true,\"formula_no_checkbox\":true,\"filtered_checkbox_identity\":true,\"no_match_explanation\":true}";
        }
        for(int captureCategory:{1,5,6}){settingsWindow.set(968,captureCategory==5?L"SO4":L"");settingsWindow.fillCompletion();settingsWindow.select(captureCategory);UpdateWindow(settingsWindow.hwnd);// capture the page through the window itself, then encode with the
         // same PNG encoder lookup the canvas screenshots use.
            RECT box;GetWindowRect(settingsWindow.hwnd,&box);
            int w=std::max(1,int(box.right-box.left)),h=std::max(1,int(box.bottom-box.top));
            HDC screen=GetDC(nullptr);HDC mem=CreateCompatibleDC(screen);
            BITMAPINFO info={};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=w;
            info.bmiHeader.biHeight=-h;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
            info.bmiHeader.biCompression=BI_RGB;
            void* bits=nullptr;HBITMAP bitmap=CreateDIBSection(mem,&info,DIB_RGB_COLORS,&bits,nullptr,0);
            HGDIOBJ old=bitmap?SelectObject(mem,bitmap):nullptr;
            BOOL copied=PrintWindow(settingsWindow.hwnd,mem,2);
            if(!copied&&bits)memset(bits,0,size_t(w)*size_t(h)*4);
            if(old)SelectObject(mem,old);
            DeleteDC(mem);ReleaseDC(nullptr,screen);
            if(bitmap&&bits){
                Bitmap image(w,h,w*4,PixelFormat32bppARGB,(BYTE*)bits);
                UINT count=0,bytes=0;GetImageEncodersSize(&count,&bytes);
                std::vector<uint8_t> storage(bytes);auto codecs=(ImageCodecInfo*)storage.data();GetImageEncoders(count,bytes,codecs);
                for(UINT i=0;i<count;++i)if(wcscmp(codecs[i].MimeType,L"image/png")==0){image.Save(W(dir+(captureCategory==1?"/ai-api-settings.png":captureCategory==5?"/completion-settings.png":"/custom-group-settings.png")).c_str(),&codecs[i].Clsid,0);break;}
            }
            if(bitmap)DeleteObject(bitmap);
            SetForegroundWindow(settingsWindow.hwnd);
        }
        std::ofstream result(dir+"/completion-page-check.json");
        result<<std::boolalpha<<"{\"page_visible\":"<<pageVisible<<",\"list_rows\":"<<rows<<",\"checkbox_switch\":"<<switched<<",\"master_switch_mirrors\":"<<mirrored<<"}";
        settingsWindow.set(968,L"");settingsWindow.fillCompletion();settingsWindow.select(0);
    }
    {// 页面与排版页：把当前纸张与张数设为新建文档默认值的复选框
        pagedefaults::pathOverride()=W(dir+"/interface.ini");pagedefaults::reload();
        pagedefaults::Values original=pagedefaults::load();
        struct Restore{pagedefaults::Values value;~Restore(){pagedefaults::save(value);pagedefaults::pathOverride().clear();pagedefaults::reload();}}restore{original};
        settingsWindow.populate();settingsWindow.select(2);
        auto checkbox=GetDlgItem(settingsWindow.hwnd,946);
        bool pageVisible=checkbox&&IsWindowVisible(checkbox)!=0;
        require(pageVisible,"page category shows the default-page checkbox");
        settingsWindow.select(0);require(!IsWindowVisible(checkbox),"other categories hide the default-page checkbox");settingsWindow.select(2);
        require(IsDlgButtonChecked(settingsWindow.hwnd,946)==(original.enabled?BST_CHECKED:BST_UNCHECKED),"checkbox mirrors the stored default");
        settingsWindow.set(942,L"210");settingsWindow.set(943,L"297");settingsWindow.set(944,L"10");settingsWindow.set(945,L"10");
        SendMessageW(settingsWindow.hwnd,WM_COMMAND,MAKEWPARAM(942,EN_CHANGE),(LPARAM)GetDlgItem(settingsWindow.hwnd,942));
        CheckDlgButton(settingsWindow.hwnd,946,BST_CHECKED);
        SendMessageW(settingsWindow.hwnd,WM_COMMAND,MAKEWPARAM(946,BN_CLICKED),(LPARAM)checkbox);
        bool dirty=settingsWindow.dirty[2];require(dirty,"page draft marked dirty");
        bool saved=settingsWindow.save();require(saved,"save page with the default flag");
        auto stored=pagedefaults::load();
        require(stored.enabled&&stored.across==10&&stored.down==10,"default flag and page counts persist");
        require(std::abs(stored.width-595.2756)<.01&&std::abs(stored.height-841.8898)<.01,"default paper size persists");
        Document fresh;auto page=studioio::Page::get(fresh);
        require(page.across==10&&page.down==10&&std::abs(page.width-stored.width)<.01&&std::abs(page.x+page.width*.5)<.01,"new documents start from the default page");
        require(studioio::Page::get(s.doc).across==10,"the current document receives the saved page");
        {Document explicitPage;studioio::Page own;own.width=400;own.height=650;own.across=2;own.down=3;own.store(explicitPage);
         auto kept=studioio::Page::get(explicitPage);require(kept.across==2&&kept.down==3&&std::abs(kept.width-400)<.01,"documents with their own page keep it");}
        // The capture below is produced by an external screen grab; keep the
        // window on the page tab so the screenshot shows the new checkbox.
        if(GetEnvironmentVariableW(L"MOLECULE_STUDIO_PAGE_DEFAULT_KEEP_OPEN",nullptr,0)>0){settingsWindow.select(2);ShowWindow(settingsWindow.hwnd,SW_SHOWNORMAL);SetForegroundWindow(settingsWindow.hwnd);return;}
        CheckDlgButton(settingsWindow.hwnd,946,BST_UNCHECKED);
        SendMessageW(settingsWindow.hwnd,WM_COMMAND,MAKEWPARAM(946,BN_CLICKED),(LPARAM)checkbox);
        require(settingsWindow.save(),"save page without the default flag");
        require(!pagedefaults::load().enabled,"unchecking clears the stored default");
        Document blank;auto a4=studioio::Page::get(blank);
        require(a4.across==1&&a4.down==1&&std::abs(a4.width-595.2756)<.01,"new documents return to A4");
        std::ofstream(dir+"/page-defaults-check.json")<<std::boolalpha<<"{\"checkbox_visible\":"<<pageVisible<<",\"category_scoped\":true,\"persisted\":"<<stored.enabled<<",\"new_document_page\":"<<(page.across==10)<<",\"uncheck_restores_a4\":true}";
    }
    if(GetEnvironmentVariableW(L"MOLECULE_STUDIO_PAGE_DEFAULT_CHECK_ONLY",nullptr,0)>0){ShowWindow(settingsWindow.hwnd,SW_HIDE);s.dirty=false;PostMessageW(h,WM_CLOSE,0,0);return;}
    if(GetEnvironmentVariableW(L"MOLECULE_STUDIO_COMPLETION_CHECK_ONLY",nullptr,0)>0){ShowWindow(settingsWindow.hwnd,SW_HIDE);s.dirty=false;PostMessageW(h,WM_CLOSE,0,0);return;}
    settingsWindow.select(1);require(IsWindowVisible(GetDlgItem(settingsWindow.hwnd,920))&&!IsWindowVisible(GetDlgItem(settingsWindow.hwnd,910)),"API category controls");
    SendMessageW(settingsWindow.hwnd,WM_COMMAND,MAKEWPARAM(814,BN_CLICKED),(LPARAM)GetDlgItem(settingsWindow.hwnd,814));require(settingsWindow.category==4&&IsWindowVisible(GetDlgItem(settingsWindow.hwnd,950)),"preset navigation button opens editor");settingsWindow.set(950,L"测试预设：用简洁中文回复，优先排成三列。");SendMessageW(settingsWindow.hwnd,WM_COMMAND,MAKEWPARAM(950,EN_CHANGE),(LPARAM)GetDlgItem(settingsWindow.hwnd,950));require(settingsWindow.dirty[4],"prompt edit marks draft");require(settingsWindow.save(),"save custom prompt");
    ai.preset.clear();ai.load();require(ai.preset==U(settingsWindow.text(950)),"custom prompt persists");
    SendMessageW(settingsWindow.hwnd,WM_COMMAND,951,0);require(settingsWindow.text(950)==W(AiWindow::defaultPreset()),"reset preset button");require(settingsWindow.save(),"save reset preset");
    ai.preset="other";ai.load();require(ai.preset==AiWindow::defaultPreset(),"default preset reloads");
    ShowWindow(settingsWindow.hwnd,SW_HIDE);showAI();require(GetParent(ai.hwnd)==nullptr,"AI is independent top-level window");require((GetWindowLongPtrW(ai.hwnd,GWL_STYLE)&WS_MINIMIZEBOX)!=0,"AI has minimize control");require((GetWindowLongPtrW(ai.hwnd,GWL_EXSTYLE)&WS_EX_APPWINDOW)!=0,"AI appears in taskbar");
    require(!IsWindowVisible(GetDlgItem(ai.hwnd,702))&&!IsWindowVisible(GetDlgItem(ai.hwnd,706)),"API configuration is not in chat");
    ai.history.push_back({"user","conversation fixture"});ai.set(711,"draft remains through docking");ai.log("你：请画一个苯环。");ai.log("AI：conversation remains through docking");require(ai.chat.messages.size()>=3&&ai.chat.messages[ai.chat.messages.size()-2].role==L"你"&&ai.chat.messages.back().role==L"AI 助手","chat bubbles have explicit speakers");auto transcript=ai.get(710);auto digest=s.doc.digest();
    ribbonCompact=false;ribbonPinned=true;ribbonVisible=true;layout();int expandedTop=top;ribbonCompact=true;layout();require(top<expandedTop,"compact toolbar saves vertical canvas space");require(!compactMenus.empty(),"compact toolbar retains overflow tool menus");ribbonPinned=false;ribbonVisible=false;layout();require(top==int(88*scale),"unlocked toolbar collapses to tabs plus canvas spacing");for(const auto& q:chemistryQuick)require(q.r.X<-1000,"collapsed quick actions are hidden");switchTab(ribbonTab);require(ribbonVisible,"clicking active tab opens unlocked toolbar");execute(visibleGroups().front().items.front());require(!ribbonVisible,"choosing a tool auto-collapses unlocked toolbar");ribbonPinned=true;ribbonVisible=true;layout();
    RECT full;GetWindowRect(canvas,&full);ai.toggleDock();require(ai.docked&&GetParent(ai.hwnd)==h,"AI docks into main window");RECT canvasBox,aiBox;GetWindowRect(canvas,&canvasBox);GetWindowRect(ai.hwnd,&aiBox);require(canvasBox.right<=aiBox.left,"docked AI never covers canvas");require(canvasBox.right-canvasBox.left<full.right-full.left,"canvas makes room for sidebar");
    SendMessageW(ai.hwnd,WM_COMMAND,726,0);require(ai.paneLocked,"AI pane locks");ai.toggleDock();require(ai.docked,"locked AI pane cannot undock");SendMessageW(ai.hwnd,WM_COMMAND,726,0);require(!ai.paneLocked,"AI pane unlocks");require(ai.get(711)=="draft remains through docking"&&ai.get(710)==transcript&&ai.history.size()==1,"docking retains draft and conversation");
    RECT parent;GetWindowRect(h,&parent);SetWindowPos(h,nullptr,0,0,800,500,SWP_NOMOVE|SWP_NOZORDER);layout();RECT client;GetClientRect(ai.hwnd,&client);
    for(int id:{710,711,712,714,719,724}){RECT r;GetWindowRect(GetDlgItem(ai.hwnd,id),&r);MapWindowPoints(nullptr,ai.hwnd,(POINT*)&r,2);if(!(r.left>=0&&r.top>=0&&r.right<=client.right+1&&r.bottom<=client.bottom+1))throw std::runtime_error("sidebar control "+std::to_string(id)+" ["+std::to_string(r.left)+","+std::to_string(r.top)+","+std::to_string(r.right)+","+std::to_string(r.bottom)+"] client ["+std::to_string(client.right)+","+std::to_string(client.bottom)+"]");}
    ai.toggleDock();require(!ai.docked&&GetParent(ai.hwnd)==nullptr,"AI undocks");ShowWindow(ai.hwnd,SW_MINIMIZE);require(IsIconic(ai.hwnd),"independent AI minimizes");ShowWindow(ai.hwnd,SW_RESTORE);require(ai.get(711)=="draft remains through docking","minimize retains draft");require(s.doc.digest()==digest,"window changes do not modify document");
    SetWindowPos(h,nullptr,0,0,parent.right-parent.left,parent.bottom-parent.top,SWP_NOMOVE|SWP_NOZORDER);layout();
    bool clipboardTest=false;
    {ClipboardFixture clipboard(ai.hwnd);if(clipboard.available){require(clipboard.install(ai.hwnd),"install temporary clipboard image");require(AiWindow::clipboardImageAvailable(),"clipboard image detected");SendDlgItemMessageW(ai.hwnd,711,WM_PASTE,0,0);require(ai.imageTemporary&&!ai.imagePath.empty(),"Ctrl+V path creates image attachment");require(ai.get(711)=="draft remains through docking","image paste leaves text intact");Bitmap pasted(ai.imagePath.c_str());require(pasted.GetLastStatus()==Ok&&pasted.GetWidth()==28&&pasted.GetHeight()==20,"clipboard image decodes correctly");clipboardTest=true;}}
    auto temporary=ai.imagePath;ai.clearImage();if(!temporary.empty())require(GetFileAttributesW(temporary.c_str())==INVALID_FILE_ATTRIBUTES,"clearing attachment removes temporary image");
    ai.hide();settingsWindow.select(4);ShowWindow(settingsWindow.hwnd,SW_HIDE);
    std::ofstream(dir+"/checks.json")<<"{\"category_window\":true,\"preset_button_click\":true,\"speaker_bubbles\":true,\"compact_toolbar\":true,\"toolbar_autohide\":true,\"pane_lock\":true,\"api_category\":true,\"prompt_save_reload\":true,\"prompt_reset\":true,\"independent_ai\":true,\"minimize\":true,\"dock_undock\":true,\"conversation_retained\":true,\"canvas_nonoverlap\":true,\"compact_sidebar\":true,\"clipboard_preserved\":true,\"clipboard_image_paste\":"<<(clipboardTest?"true":"false")<<",\"temporary_image_cleanup\":true}";
    if(GetFileAttributesW(W(dir+"/keep-open").c_str())!=INVALID_FILE_ATTRIBUTES){scale=GetDpiForWindow(h)/96.f;ShowWindow(h,SW_MAXIMIZE);layout();ai.set(710,"");ai.log("你：请画一个苯环，在下方标上中文名称。");ai.log("AI：我会按你的要求绘制苯环，并将名称放在结构下方。\n\n这是消息气泡的界面测试示例。");ai.set(711,"");showAI();ai.toggleDock();s.dirty=false;return;}
    s.dirty=false;PostMessageW(h,WM_CLOSE,0,0);
}
