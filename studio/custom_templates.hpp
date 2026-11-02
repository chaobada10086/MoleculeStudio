#pragma once
namespace custom_templates {
static std::wstring directory(){
    if(auto path=std::getenv("MOLECULE_STUDIO_CUSTOM_TEMPLATE_DIR"))return W(path);
    wchar_t exe[32768]={};GetModuleFileNameW(0,exe,32768);std::wstring path=exe;return path.substr(0,path.find_last_of(L"\\/"))+L"\\custom_templates";
}
static std::string trimmed(std::string value){auto a=value.find_first_not_of(" \t\r\n");return a==std::string::npos?"":value.substr(a,value.find_last_not_of(" \t\r\n")-a+1);}
struct Entry {std::string name,group;std::wstring path;};
static std::shared_ptr<Session> read(const std::wstring& path){auto source=std::make_shared<Session>();auto e=source->open(U(path));if(!e.ok())throw std::runtime_error(e.message);return source;}
static std::vector<Entry> entries(){
    std::vector<Entry> result;WIN32_FIND_DATAW data;auto dir=directory();HANDLE find=FindFirstFileW((dir+L"\\*.t01doc").c_str(),&data);if(find==INVALID_HANDLE_VALUE)return result;
    do{if(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)continue;try{auto path=dir+L"\\"+data.cFileName;auto source=read(path);auto& ext=source->doc.documentExtensions();auto name=ext.findValue("studio.template.name"),group=ext.findValue("studio.template.group");if(name&&group)result.push_back({*name,*group,path});}catch(...) { /* A damaged file cannot hide other templates. */ }}while(FindNextFileW(find,&data));FindClose(find);
    std::sort(result.begin(),result.end(),[](const Entry& a,const Entry& b){return a.group==b.group?a.name<b.name:a.group<b.group;});return result;
}
static std::shared_ptr<Session> snapshot(Session& current){
    if(current.selected.empty())throw std::runtime_error("请先框选要保存的结构");
    auto source=std::make_shared<Session>();source->doc.setDefaultBondLength(current.doc.defaultBondLength());source->doc.setDefaultLineWidth(current.doc.defaultLineWidth());source->doc.setDefaultFont(current.doc.defaultFont());source->doc.setDefaultTextSize(current.doc.defaultTextSize());
    for(const auto& entry:current.doc.documentExtensions().entries())if(entry.key.compare(0,13,"studio.style.")==0||entry.key.compare(0,15,"studio.palette.")==0)source->doc.documentExtensions().set(entry.key,entry.value);
    auto e=studioio::append(current.doc,source->doc,current.selected,Vec2(),nullptr);if(!e.ok())throw std::runtime_error(e.message);source->refresh();if(source->all().empty())throw std::runtime_error("所选内容无法保存为模板");return source;
}
static std::wstring save(Session& source,std::string group,std::string name){
    group=trimmed(group);name=trimmed(name);if(group.empty()||name.empty())throw std::runtime_error("请输入分组和模板名称");
    for(const auto& entry:entries())if(W(entry.group)==W(group)&&CompareStringOrdinal(W(entry.name).c_str(),-1,W(name).c_str(),-1,TRUE)==CSTR_EQUAL)throw std::runtime_error("该分组已有同名模板，请换一个名称");
    auto dir=directory();if(!CreateDirectoryW(dir.c_str(),0)&&GetLastError()!=ERROR_ALREADY_EXISTS)throw std::runtime_error("无法创建模板目录");
    GUID guid; if(FAILED(CoCreateGuid(&guid)))throw std::runtime_error("无法创建模板编号");wchar_t id[64];StringFromGUID2(guid,id,64);auto path=dir+L"\\"+id+L".t01doc",temp=path+L".tmp";
    source.doc.documentExtensions().set("studio.template.name",name);source.doc.documentExtensions().set("studio.template.group",group);
    auto e=source.save(U(temp),false);if(!e.ok()){DeleteFileW(temp.c_str());throw std::runtime_error(e.message);}if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_WRITE_THROUGH)){DeleteFileW(temp.c_str());throw std::runtime_error("模板保存失败，请检查目录权限");}return path;
}
static std::wstring importFile(const std::wstring& path){
    auto source=read(path);auto& ext=source->doc.documentExtensions();auto n=ext.findValue("studio.template.name"),g=ext.findValue("studio.template.group");if(!n||!g||trimmed(*n).empty()||trimmed(*g).empty()||source->all().empty())throw std::runtime_error("这不是有效的自定义模板文件");
    std::string original=*n,name=original,group=*g;auto existing=entries();int suffix=2;auto used=[&](){for(const auto& e:existing)if(e.group==group&&CompareStringOrdinal(W(e.name).c_str(),-1,W(name).c_str(),-1,TRUE)==CSTR_EQUAL)return true;return false;};while(used())name=original+"（导入 "+std::to_string(suffix++)+"）";return save(*source,group,name);
}
static TemplateLibraryModel modelFor(const std::vector<Entry>& entries){
    TemplateLibraryModel model;model.title="我的模板";model.noun="模板";model.cue="搜索模板名称或分组…";model.windowClass=L"MoleculeStudioCustomTemplates";model.cardHeight=167;model.thumbnailHeight=107;
    for(size_t i=0;i<entries.size();++i){const auto& e=entries[i];model.parents.push_back({e.name,e.group,e.group,e.group,int(i),1});model.variants.push_back({int(i),"保存的结构",""});if(std::find(model.categories.begin(),model.categories.end(),e.group)==model.categories.end())model.categories.push_back(e.group);}
    model.load=[entries](int i){return read(entries.at(i).path);};return model;
}
struct SavePage {
    HWND hwnd=0,owner=0,group=0,name=0,saveButton=0,errorLabel=0;HFONT font=0;float dpi=1;bool saved=false;std::shared_ptr<Session> source;std::shared_ptr<TemplateThumbnail> thumbnail;
    static std::string value(HWND h){wchar_t text[512]={};studio_i18n::getWindowText(h,text,512);return trimmed(U(text));}
    void validate(){EnableWindow(saveButton,!value(group).empty()&&!value(name).empty());studio_i18n::setWindowText(errorLabel,L"");}
    void accept(){try{save(*source,value(group),value(name));saved=true;DestroyWindow(hwnd);}catch(const std::exception& e){studio_i18n::setWindowText(errorLabel,W(e.what()).c_str());}}
    static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){auto self=(SavePage*)GetWindowLongPtrW(h,GWLP_USERDATA);if(m==WM_NCCREATE){self=(SavePage*)((CREATESTRUCTW*)l)->lpCreateParams;self->hwnd=h;SetWindowLongPtrW(h,GWLP_USERDATA,(LONG_PTR)self);}if(!self)return DefWindowProcW(h,m,w,l);
        if(m==WM_CREATE){self->font=CreateFontW(-int(13*self->dpi),0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");
            auto child=[&](const wchar_t* type,const wchar_t* text,DWORD style,int id,int x,int y,int width,int height){auto c=studio_i18n::createWindowEx(0,type,text,WS_CHILD|WS_VISIBLE|style,int(x*self->dpi),int(y*self->dpi),int(width*self->dpi),int(height*self->dpi),h,(HMENU)UINT_PTR(id),GetModuleHandleW(0),0);studio_i18n::sendMessage(c,WM_SETFONT,(WPARAM)self->font,TRUE);return c;};
            child(L"STATIC",L"添加自定义模板",0,0,24,20,510,28);child(L"STATIC",L"保存所选结构，之后可从“我的模板”再次插入。",0,0,24,54,530,24);
            child(L"STATIC",L"结构预览",0,0,24,92,220,24);child(L"STATIC",L"分组（可输入新分组）",0,0,286,100,260,24);
            self->group=child(L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWN|WS_VSCROLL,201,286,130,270,220);studio_i18n::protect(self->group);studio_i18n::sendMessage(self->group,CB_LIMITTEXT,120,0);std::set<std::string> groups;for(const auto& e:entries())groups.insert(e.group);if(groups.empty())groups.insert(U(studio_i18n::tr(L"我的结构")));for(const auto& g:groups)studio_i18n::sendMessage(self->group,CB_ADDSTRING,0,(LPARAM)W(g).c_str());studio_i18n::sendMessage(self->group,CB_SETCURSEL,0,0);
            child(L"STATIC",L"模板名称",0,0,286,184,260,24);self->name=child(L"EDIT",L"",WS_TABSTOP|WS_BORDER|ES_AUTOHSCROLL,202,286,214,270,30);studio_i18n::sendMessage(self->name,EM_SETLIMITTEXT,120,0);studio_i18n::sendMessage(self->name,EM_SETCUEBANNER,TRUE,(LPARAM)L"例如：吡啶取代片段");
            self->errorLabel=child(L"STATIC",L"",0,205,24,312,532,40);self->saveButton=child(L"BUTTON",L"保存模板",WS_TABSTOP|BS_DEFPUSHBUTTON,IDOK,326,366,110,32);child(L"BUTTON",L"取消",WS_TABSTOP,IDCANCEL,448,366,108,32);self->validate();return 0;
        }
        if(m==WM_PAINT){PAINTSTRUCT ps;auto dc=BeginPaint(h,&ps);Graphics g(dc);g.SetPageUnit(UnitPixel);g.SetSmoothingMode(SmoothingModeAntiAlias);g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);g.Clear(Color(255,247,249,248));g.ScaleTransform(self->dpi,self->dpi);roundbox(g,RectF(24,124,238,176),6,white);self->thumbnail->paint(g,RectF(24,124,238,176));EndPaint(h,&ps);return 0;}
        if(m==WM_CTLCOLORSTATIC){SetBkColor((HDC)w,RGB(247,249,248));SetTextColor((HDC)w,RGB(39,55,57));static HBRUSH background=CreateSolidBrush(RGB(247,249,248));return (LRESULT)background;}
        if(m==WM_COMMAND){int id=LOWORD(w),code=HIWORD(w);if(id==IDCANCEL)DestroyWindow(h);else if(id==IDOK)self->accept();else if((id==201&&(code==CBN_EDITCHANGE||code==CBN_SELCHANGE))||(id==202&&code==EN_CHANGE))self->validate();return 0;}
        if(m==WM_CLOSE){DestroyWindow(h);return 0;}return DefWindowProcW(h,m,w,l);
    }
    bool show(HWND parent,std::shared_ptr<Session> selected){source=selected;auto preview=std::make_shared<Session>();preview->doc=source->doc;preview->refresh();thumbnail=std::make_shared<TemplateThumbnail>(preview);owner=GetAncestor(parent,GA_ROOT);dpi=GetDpiForWindow(owner)/96.f;WNDCLASSW wc={};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(0);wc.hCursor=LoadCursorW(0,MAKEINTRESOURCEW(32512));wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);wc.lpszClassName=L"MoleculeStudioSaveTemplate";RegisterClassW(&wc);RECT r={0,0,int(580*dpi),int(420*dpi)};AdjustWindowRectEx(&r,WS_CAPTION|WS_SYSMENU,0,WS_EX_DLGMODALFRAME);RECT host;GetWindowRect(owner,&host);
        studio_i18n::createWindowEx(WS_EX_DLGMODALFRAME,wc.lpszClassName,L"添加自定义模板",WS_POPUP|WS_CAPTION|WS_SYSMENU,(host.left+host.right-(r.right-r.left))/2,(host.top+host.bottom-(r.bottom-r.top))/2,r.right-r.left,r.bottom-r.top,owner,0,wc.hInstance,this);if(!hwnd)return false;EnableWindow(owner,FALSE);ShowWindow(hwnd,SW_SHOW);SetFocus(name);MSG msg={};while(IsWindow(hwnd)&&GetMessageW(&msg,0,0,0)>0)if(!IsDialogMessageW(hwnd,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}if(msg.message==WM_QUIT)PostQuitMessage(int(msg.wParam));EnableWindow(owner,TRUE);SetForegroundWindow(owner);if(font)DeleteObject(font);return saved;
    }
};
}
void App::addCustomTemplate(){
    if(textEditor.active&&!finishText(true))return;if(s.selected.empty()){status="请先框选要保存的结构";invalidate();return;}
    switchTab(5);try{custom_templates::SavePage page;if(page.show(h,custom_templates::snapshot(s)))status="自定义模板已保存 · 可在 模板 → 我的模板 中使用";}catch(const std::exception& e){status=e.what();}SetFocus(canvas);invalidate();
}
void App::customTemplateMenu(){
    auto entries=custom_templates::entries();if(entries.empty()){studio_i18n::messageBox(h,L"还没有自定义模板。\n请先框选画板上的结构，再点击“添加自定义模板”。",L"我的模板",MB_OK|MB_ICONINFORMATION);return;}
    RingLibraryPicker picker;picker.model=custom_templates::modelFor(entries);int chosen=picker.show(h,0,{});if(chosen>=0)try{bioSource=custom_templates::read(entries[chosen].path);customPlacementName=entries[chosen].name;s.cancel();customPlacement=true;bioPlacing=true;status=customPlacementName+"：点击画板放置，Esc 取消";SetFocus(canvas);updateCanvasCursor();}catch(const std::exception& e){status=e.what();}invalidate();
}
void App::transferCustomTemplate(bool importing){
    try{
        std::wstring source;std::wstring proposed=L"自定义模板.mstemplate";
        if(!importing){auto entries=custom_templates::entries();if(entries.empty()){status="请先添加自定义模板，再导出";invalidate();return;}RingLibraryPicker picker;picker.model=custom_templates::modelFor(entries);picker.model.title="导出自定义模板";picker.actionLabel=L"选择并导出";int selected=picker.show(h,0,{});if(selected<0)return;source=entries[selected].path;auto name=W(entries[selected].name);for(auto& c:name)if(std::wstring(L"\\/:*?\"<>|").find(c)!=std::wstring::npos)c=L'_';proposed=name+L".mstemplate";}
        wchar_t file[32768]={};if(!importing)wcsncpy(file,proposed.c_str(),32767);OPENFILENAMEW ofn={};ofn.lStructSize=sizeof(ofn);ofn.hwndOwner=GetAncestor(h,GA_ROOT);ofn.lpstrFilter=L"MoleculeStudio 自定义模板 (*.mstemplate)\0*.mstemplate\0原生模板 (*.t01doc)\0*.t01doc\0\0";ofn.lpstrFile=file;ofn.nMaxFile=32768;ofn.lpstrDefExt=L"mstemplate";ofn.lpstrTitle=importing?L"导入自定义模板":L"导出自定义模板";ofn.Flags=OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|(importing?OFN_FILEMUSTEXIST:OFN_OVERWRITEPROMPT);
        if(importing?studio_i18n::openFileName(&ofn):GetSaveFileNameW(&ofn)){if(importing){custom_templates::importFile(file);status="模板已导入 · 在“我的模板”中使用";}else{auto templateSource=custom_templates::read(source);auto e=templateSource->save(U(file),false);if(!e.ok())throw std::runtime_error(e.message);status="自定义模板已导出（含分组、名称及可编辑结构）";}}
    }catch(const std::exception& e){studio_i18n::messageBox(h,W(e.what()).c_str(),importing?L"导入失败":L"导出失败",MB_OK|MB_ICONERROR);status=e.what();}invalidate();
}
void App::placeCustomTemplate(Vec2 center){
    auto source=bioSource;bioSource.reset();bioPlacing=false;customPlacement=false;if(!source)return;auto box=source->scene.bounds();auto delta=center-(box.min+box.max)*.5;auto created=std::make_shared<std::vector<ObjectRef>>();
    auto e=s.submit(Command("插入自定义模板",kInvalidateStructure|kInvalidateGeometry|kInvalidateAppearance,[source,delta,created](CommandContext& c){auto e=studioio::append(source->doc,c.document,source->all(),delta,created.get());return e.ok()?CommandResult::success():CommandResult::fail(e.code,e.message);}),"插入自定义模板");if(!e.ok())error(e);else{s.activateCommand("chooseSelectionTool_Marquee");s.selected=*created;status=customPlacementName+" · 已插入，可继续编辑";}updateCanvasCursor();invalidate();
}
static int customCheckMode=0;static bool customCheckSeen=false;
static void CALLBACK exerciseCustomPage(HWND owner,UINT,UINT_PTR timer,DWORD){
    HWND dialog=FindWindowW(customCheckMode==2?L"MoleculeStudioCustomTemplates":L"MoleculeStudioSaveTemplate",nullptr);DWORD pid=0;if(dialog)GetWindowThreadProcessId(dialog,&pid);if(pid!=GetCurrentProcessId())return;KillTimer(owner,timer);customCheckSeen=true;
    if(customCheckMode==1){studio_i18n::setWindowText(GetDlgItem(dialog,201),L"中文分组");studio_i18n::setWindowText(GetDlgItem(dialog,202),L"局部苯环");PostMessageW(dialog,WM_COMMAND,IDOK,0);}else PostMessageW(dialog,WM_COMMAND,customCheckMode==2?IDOK:IDCANCEL,0);
}
void App::customTemplateCheck(const std::string& dir){
    auto require=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};SetEnvironmentVariableW(L"MOLECULE_STUDIO_CUSTOM_TEMPLATE_DIR",W(dir+"/saved").c_str());_putenv_s("MOLECULE_STUDIO_CUSTOM_TEMPLATE_DIR",(dir+"/saved").c_str());
    for(auto lists:{makeTools(),makeExpandedTools(false)})for(auto g:lists)for(auto b:g.items)require(b.cmd!="@otherRings"&&b.cmd!="@bioComponents","builtins moved out of Home and Graphics");
    switchTab(5);invalidate();require(!templateGroups[1].items.back().enabled,"Add disabled without selection");
    s.activateCommand("chooseRingTool_Benzene");s.gesture(PointerPhase::Down,Vec2(),0);s.gesture(PointerPhase::Up,Vec2(),0);auto chosen=s.all();s.gesture(PointerPhase::Down,Vec2(180,100),0);s.gesture(PointerPhase::Up,Vec2(180,100),0);s.selected=chosen;s.refresh();invalidate();require(templateGroups[1].items.back().enabled,"Add enabled with selection");auto before=s.doc.digest();auto refs=s.selected;
    auto source=custom_templates::snapshot(s);require(source->doc.stats().aliveAtoms==6&&source->doc.stats().aliveBonds==6&&s.doc.stats().aliveAtoms==12,"snapshot includes only selected structure");
    customCheckMode=0;customCheckSeen=false;SetTimer(h,11,80,exerciseCustomPage);addCustomTemplate();KillTimer(h,11);require(customCheckSeen&&custom_templates::entries().empty()&&s.doc.digest()==before&&s.selected==refs&&IsWindowEnabled(GetAncestor(h,GA_ROOT)),"cancel real save page preserves document and selection");
    customCheckMode=1;customCheckSeen=false;SetTimer(h,11,80,exerciseCustomPage);execute(Button("","添加自定义模板","","@addCustomTemplate"));KillTimer(h,11);require(customCheckSeen,"save page reached through ribbon command");auto items=custom_templates::entries();require(items.size()==1&&items[0].name=="局部苯环"&&items[0].group=="中文分组","Chinese group/name persisted");auto reopened=custom_templates::read(items[0].path);require(reopened->doc.stats().aliveAtoms==6&&reopened->doc.stats().aliveBonds==6,"saved selection reopens");bool duplicate=false;try{custom_templates::save(*source,"中文分组","局部苯环");}catch(...){duplicate=true;}require(duplicate&&custom_templates::entries().size()==1,"duplicate name cannot overwrite");auto exported=W(dir+"/exchange.mstemplate");require(reopened->save(U(exported),false).ok(),"export exchange file");auto imported=custom_templates::importFile(exported);auto exchanged=custom_templates::read(imported);require(exchanged->doc.stats().aliveAtoms==6&&custom_templates::entries().size()==2,"imported graph and duplicate rename");require(*exchanged->doc.documentExtensions().findValue("studio.template.group")=="中文分组","import preserves grouping");bool rejected=false;Session empty;empty.save(dir+"/invalid.mstemplate",false);try{custom_templates::importFile(W(dir+"/invalid.mstemplate"));}catch(...){rejected=true;}require(rejected,"reject non-template files");bool blank=false;try{custom_templates::save(*source," ","x");}catch(...){blank=true;}require(blank,"blank group rejected");require(s.doc.digest()==before&&s.selected==refs,"saving leaves drawing unchanged");
    customCheckMode=2;customCheckSeen=false;SetTimer(h,11,80,exerciseCustomPage);execute(Button("","我的模板","","@customTemplates"));KillTimer(h,11);require(customCheckSeen&&customPlacement&&bioPlacing&&s.doc.digest()==before,"picker arms placement without mutation");cancelBioPlacement();require(!customPlacement&&!bioPlacing&&s.doc.digest()==before,"Esc placement cancellation");
    bioSource=reopened;customPlacement=true;bioPlacing=true;customPlacementName="局部苯环";auto pixel=s.view().toDevice(Vec2(300,150));studio_i18n::sendMessage(canvas,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(int(pixel.x),int(pixel.y)));studio_i18n::sendMessage(canvas,WM_LBUTTONUP,0,MAKELPARAM(int(pixel.x),int(pixel.y)));require(s.doc.stats().aliveAtoms==18&&!bioPlacing,"canvas click inserts custom structure");auto after=s.doc.digest();require(s.undo().ok()&&s.doc.digest()==before,"one undo removes template");require(s.undo(true).ok()&&s.doc.digest()==after,"redo restores template");s.selected.clear();invalidate();require(!templateGroups[1].items.back().enabled,"Add disables after selection cleared");screenshot(dir+"/templates-disabled.png");s.selected=refs;invalidate();screenshot(dir+"/templates-enabled.png");
    std::ofstream(dir+"/custom-template-checks.json")<<"{\"moved_entries\":true,\"selection_enabled\":true,\"partial_structure\":true,\"real_save_page\":true,\"cancel\":true,\"unicode_persistence\":true,\"duplicate_protection\":true,\"library_picker\":true,\"canvas_placement\":true,\"undo_redo\":true,\"export_import\":true,\"invalid_file_rejected\":true}";s.dirty=false;PostMessageW(h,WM_CLOSE,0,0);
}
