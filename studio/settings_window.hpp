#pragma once
#include "chem_completion.hpp"
#include "completion_search.hpp"
#include "page_defaults.hpp"
#include "ui_language.hpp"
struct StudioSettingsWindow {
    static LRESULT CALLBACK groupListKey(HWND list,UINT message,WPARAM key,LPARAM data,UINT_PTR subclass,DWORD_PTR context){
        auto self=reinterpret_cast<StudioSettingsWindow*>(context);
        if(message==WM_KEYDOWN&&key==VK_DELETE){if(!(data&(1LL<<30)))::SendMessageW(self->hwnd,WM_COMMAND,MAKEWPARAM(973,BN_CLICKED),(LPARAM)GetDlgItem(self->hwnd,973));return 0;}
        if(message==WM_NCDESTROY)RemoveWindowSubclass(list,groupListKey,subclass);return DefSubclassProc(list,message,key,data);
    }
    HWND hwnd=0,owner=0;HFONT font=0;int category=0;bool loading=false,dirty[5]={false,false,false,false,false},waiting=false,completionDirty=false,formulaDirty=false,completionFilling=false;
    std::vector<std::string> completionNames;
    std::vector<std::string> completionKeys;std::function<void(const std::string&)> saveGroup;
    std::unique_ptr<Session> groupPreview;std::unique_ptr<compatible::Renderer> groupRenderer;
    std::string previewFragment,previewName,previewError;RECT previewRect={};
    void updateGroupPreview(){
        int row=int(::SendDlgItemMessageW(hwnd,974,LB_GETCURSEL,0,0));customgroups::load();
        const auto& entries=customgroups::entries();
        if(row<0||size_t(row)>=entries.size()){groupPreview.reset();groupRenderer.reset();previewFragment.clear();previewName.clear();previewError.clear();InvalidateRect(hwnd,&previewRect,FALSE);return;}
        const auto& entry=entries[size_t(row)];if(previewFragment==entry.fragment&&previewName==entry.name)return;
        groupPreview.reset();groupRenderer.reset();previewFragment=entry.fragment;previewName=entry.name;previewError.clear();
        try{tinyxml2::XMLDocument xml;if(xml.Parse(entry.fragment.c_str())!=tinyxml2::XML_SUCCESS||!xml.RootElement())throw std::runtime_error("结构数据损坏");
            nicknameexpand::walk(xml.RootElement(),[&](tinyxml2::XMLElement* e){if(e->Attribute("NodeType")&&std::string(e->Attribute("NodeType"))=="ExternalConnectionPoint"){e->SetAttribute("NodeType","GenericNickname");e->SetAttribute("NumHydrogens",0);auto t=xml.NewElement("t"),s=xml.NewElement("s");s->SetText("R");t->InsertEndChild(s);e->InsertEndChild(t);}});
            tinyxml2::XMLPrinter p;xml.Print(&p);std::unique_ptr<Session> session(new Session());auto error=persistence::readCdxml(std::string("<CDXML BondLength=\"14.4\"><page id=\"2000000000\">")+p.CStr()+"</page></CDXML>",session->doc);if(!error.ok())throw std::runtime_error(error.message);
            session->refresh();groupPreview=std::move(session);groupRenderer.reset(new compatible::Renderer());groupRenderer->initialize(nullptr);
        }catch(const std::exception& e){previewError=e.what();}
        InvalidateRect(hwnd,&previewRect,FALSE);
    }
    void drawGroupPreview(HDC dc){
        if(category!=6)return;float dpi=GetDpiForWindow(hwnd)/96.f;
        {Graphics g(dc);SolidBrush bg(Color::White);g.FillRectangle(&bg,int(previewRect.left),int(previewRect.top),int(previewRect.right-previewRect.left),int(previewRect.bottom-previewRect.top));
        studio_ui::label(g,previewName.empty()?L"结构式预览":W(previewName),RectF(float(previewRect.left+8*dpi),float(previewRect.top),float(previewRect.right-previewRect.left-16*dpi),20*dpi),11*dpi,studio_ui::muted());}
        int w=previewRect.right-previewRect.left-int(12*dpi),h=previewRect.bottom-previewRect.top-int(24*dpi);
        if(groupPreview&&groupRenderer&&w>0&&h>0){
            auto& session=*groupPreview;auto bounds=session.scene.bounds();if(bounds.empty())return;
            auto view=session.renderView;view.widthPx=w;view.heightPx=h;view.transform.dpiScale=1;view.transform.viewportCenter=Vec2(w*.5,h*.5);view.transform.center=(bounds.min+bounds.max)*.5;
            view.transform.scale=std::min(2.0*double(dpi),std::min((w-12*dpi)/std::max(1.,bounds.width()),(h-12*dpi)/std::max(1.,bounds.height())));
            if(groupRenderer->draw(session.scene,nullptr,view,nullptr).ok()){int saved=SaveDC(dc);SetViewportOrgEx(dc,previewRect.left+int(6*dpi),previewRect.top+int(22*dpi),nullptr);groupRenderer->present(dc);RestoreDC(dc,saved);}
        }else{Graphics g(dc);studio_ui::label(g,previewError.empty()?L"选择一行查看结构":W(previewError),RectF(float(previewRect.left+8*dpi),float(previewRect.top+32*dpi),float(previewRect.right-previewRect.left-16*dpi),60*dpi),11*dpi,studio_ui::muted());}
    }
    void fillGroups(){groupsReady=true;auto list=GetDlgItem(hwnd,974);studio_i18n::sendMessage(list,LB_RESETCONTENT,0,0);customgroups::load();for(auto& e:customgroups::entries())studio_i18n::sendMessage(list,LB_ADDSTRING,0,(LPARAM)W(e.name).c_str());if(!customgroups::entries().empty())::SendMessageW(list,LB_SETCURSEL,0,0);updateGroupPreview();}

    static const int kCategories=8;
    bool completionReady=false,groupsReady=false;
    HBITMAP pagePaint[8]={};int paintWidth=0,paintHeight=0,paintLanguage=-1;UINT paintDpi=0;bool paintCacheDisabled=false;
    void clearPaintCache(){for(auto& image:pagePaint){if(image)DeleteObject(image);image=nullptr;}}

    std::map<int,std::wstring> captions;std::wstring statusSource;
    void translate(){bool previousLoading=loading;loading=true;for(auto& c:captions){studio_i18n::setDlgItemText(hwnd,c.first,studio_i18n::tr(c.second).c_str());auto control=GetDlgItem(hwnd,c.first);auto ex=GetWindowLongPtrW(control,GWL_EXSTYLE);SetWindowLongPtrW(control,GWL_EXSTYLE,studio_i18n::rtl()?ex|WS_EX_RTLREADING:ex&~WS_EX_RTLREADING);}studio_i18n::setWindowText(hwnd,(L"Molecule Studio · "+studio_i18n::tr(L"设置")).c_str());
        auto combo=GetDlgItem(hwnd,941);int selected=int(studio_i18n::sendMessage(combo,CB_GETCURSEL,0,0));studio_i18n::sendMessage(combo,CB_RESETCONTENT,0,0);for(auto v:{L"纵向",L"横向"})studio_i18n::sendMessage(combo,CB_ADDSTRING,0,(LPARAM)studio_i18n::tr(v).c_str());studio_i18n::sendMessage(combo,CB_SETCURSEL,selected,0);
        auto paper=GetDlgItem(hwnd,940);selected=int(studio_i18n::sendMessage(paper,CB_GETCURSEL,0,0));studio_i18n::sendMessage(paper,CB_DELETESTRING,6,0);studio_i18n::sendMessage(paper,CB_ADDSTRING,0,(LPARAM)studio_i18n::tr(L"自定义").c_str());studio_i18n::sendMessage(paper,CB_SETCURSEL,selected,0);
        for(int col=1;col<3;++col){auto name=studio_i18n::tr(col==1?L"缩写":L"来源");LVCOLUMNW c={};c.mask=LVCF_TEXT;c.pszText=&name[0];studio_i18n::sendDlgItemMessage(hwnd,961,LVM_SETCOLUMNW,col,(LPARAM)&c);}
        studio_i18n::sendDlgItemMessage(hwnd,968,EM_SETCUEBANNER,TRUE,(LPARAM)studio_i18n::tr(L"搜索缩写、别名或公式（如 SO4）…").c_str());
        studio_i18n::sendDlgItemMessage(hwnd,971,EM_SETCUEBANNER,TRUE,(LPARAM)studio_i18n::tr(L"缩写名称，例如 Ns").c_str());
        if(completionReady)fillCompletion();if(shortcutSummary)set(931,shortcutSummary());
        if(!statusSource.empty())studio_i18n::setDlgItemText(hwnd,802,studio_i18n::tr(statusSource).c_str());
        studio_i18n::sendDlgItemMessage(hwnd,980,CB_SETCURSEL,studio_i18n::current(),0);RedrawWindow(hwnd,0,0,RDW_INVALIDATE|RDW_ALLCHILDREN);loading=previousLoading;
    }
    DrawingSettings* drawing=nullptr;AiWindow* ai=nullptr;studioio::Page page;DocumentId pageDocument=0;
    std::function<studioio::Page()> getPage;
    std::function<DocumentId()> getDocument;
    std::function<void(const DrawingSettings&,bool)> applyDrawing;
    std::function<void(const studioio::Page&,DocumentId)> applyPage;
    std::function<void()> editShortcuts;
    std::function<std::wstring()> shortcutSummary;
    ~StudioSettingsWindow(){clearPaintCache();if(IsWindow(hwnd))DestroyWindow(hwnd);if(font)DeleteObject(font);}
    std::wstring text(int id)const{return AiWindow::controlText(GetDlgItem(hwnd,id));}
    void set(int id,const std::wstring& value){if(id==802)statusSource=value;auto translated=(id==802?studio_i18n::tr(value):value);if(id==926)translated=studio_i18n::tr(translated);if(text(id)!=translated)studio_i18n::setDlgItemText(hwnd,id,translated.c_str());}
    static std::wstring number(double value){wchar_t s[64];swprintf(s,64,L"%.6g",value);return s;}
    static int group(int id){if(id>=985&&id<=987)return 5;if(id==980||id==981)return 7;if(id>=970&&id<=974)return 6;if(id==968)return 5;if(id>=960&&id<=974)return 5;if(id>=950&&id<=953)return 4;if(id>=910&&id<=917)return 0;if(id>=920&&id<=926)return 1;if(id>=940&&id<=946)return 2;if(id>=930&&id<=932)return 3;return -1;}
    void refreshModels(){
        auto combo=GetDlgItem(hwnd,923);auto original=text(923);studio_i18n::sendMessage(combo,CB_RESETCONTENT,0,0);auto src=GetDlgItem(ai->hwnd,704);int n=int(studio_i18n::sendMessage(src,CB_GETCOUNT,0,0));
        for(int i=0;i<n;i++){int length=int(studio_i18n::sendMessage(src,CB_GETLBTEXTLEN,i,0));if(length<0||length>500)continue;std::wstring s(size_t(length)+1,0);studio_i18n::sendMessage(src,CB_GETLBTEXT,i,(LPARAM)&s[0]);studio_i18n::sendMessage(combo,CB_ADDSTRING,0,(LPARAM)s.c_str());}
        set(923,original.empty()?W(ai->get(704)):original);
    }
    void fillCompletion(){
        auto list=GetDlgItem(hwnd,961);if(!list)return;
        completionReady=true;completionFilling=true;
        studio_i18n::sendMessage(list,WM_SETREDRAW,FALSE,0);
        studio_i18n::sendMessage(list,LVM_DELETEALLITEMS,0,0);
        completionKeys.clear();completionNames.clear();auto entries=completionsearch::search(U(text(968)));
        LVITEMW item={};item.mask=LVIF_TEXT|LVIF_PARAM;item.iSubItem=0;item.pszText=const_cast<wchar_t*>(L"");
        int row=0;size_t formulas=0;for(size_t i=0;i<entries.size();++i){
            auto& hit=entries[i];auto& entry=hit.entry;formulas+=hit.formula;completionKeys.push_back(entry.key);completionNames.push_back(entry.name);
            // Column 0 holds only the check box; the abbreviation itself lives in
            // column 1 and its origin in column 2.
            item.iItem=row;item.lParam=LPARAM(row);
            studio_i18n::sendMessage(list,LVM_INSERTITEMW,0,(LPARAM)&item);
            {std::wstring name=W(entry.name+(hit.formula?" → "+completionsearch::preview(entry.name):""));LVITEMW column={};column.mask=LVIF_TEXT;column.iItem=row;column.iSubItem=1;column.pszText=&name[0];studio_i18n::sendMessage(list,LVM_SETITEMW,0,(LPARAM)&column);}
            {std::wstring origin=hit.formula?L"公式排版":entry.custom?L"自定义":L"内置";LVITEMW column={};column.mask=LVIF_TEXT;column.iItem=row;column.iSubItem=2;origin=studio_i18n::tr(origin);column.pszText=&origin[0];studio_i18n::sendMessage(list,LVM_SETITEMW,0,(LPARAM)&column);}
            if(hit.formula){LVITEMW state={};state.stateMask=LVIS_STATEIMAGEMASK;state.state=0;studio_i18n::sendMessage(list,LVM_SETITEMSTATE,row,(LPARAM)&state);}else ListView_SetCheckState(list,row,entry.enabled?TRUE:FALSE);++row;
        }
        studio_i18n::sendMessage(list,WM_SETREDRAW,TRUE,0);
        InvalidateRect(list,nullptr,TRUE);
        completionFilling=false;
        set(967,std::to_wstring(entries.size())+L" · "+studio_i18n::tr(L"缩写清单"));
        if(!text(968).empty())set(802,entries.empty()?L"没有匹配结果；支持缩写、别名与常用公式，清空可查看全部缩写。":formulas?L"公式行展示排版预览，由上方公式排版开关控制；不代表可展开官能团。":L"按精确、包含、近似排序；搜索不会修改实际化学标签。");
    }
    void populate(){
        loading=true;for(auto& d:dirty)d=false;completionDirty=false;formulaDirty=false;
        CheckDlgButton(hwnd,917,chemcompletion::enabled()?BST_CHECKED:BST_UNCHECKED);
        groupsReady=false;completionReady=false;if(GetDlgItem(hwnd,969)){CheckDlgButton(hwnd,969,chemcompletion::enabled()?BST_CHECKED:BST_UNCHECKED);CheckDlgButton(hwnd,966,chemcompletion::formulaLayout()?BST_CHECKED:BST_UNCHECKED);}
        set(950,W(ai->preset));set(910,number(drawing->bondLength));set(911,number(drawing->lineWidth));set(914,drawing->fontName);set(915,number(drawing->fontSize));studio_i18n::sendDlgItemMessage(hwnd,916,CB_SELECTSTRING,WPARAM(-1),(LPARAM)drawing->preset.c_str());CheckDlgButton(hwnd,912,BST_CHECKED);
        set(920,W(ai->get(702)));set(921,W(ai->get(706)));CheckDlgButton(hwnd,922,IsDlgButtonChecked(ai->hwnd,707));set(923,W(ai->get(704)));refreshModels();set(926,W(ai->apiStatus));
        page=getPage();pageDocument=getDocument();set(942,number(page.width*25.4/72));set(943,number(page.height*25.4/72));set(944,std::to_wstring(page.across));set(945,std::to_wstring(page.down));
        studio_i18n::sendDlgItemMessage(hwnd,940,CB_SETCURSEL,6,0);studio_i18n::sendDlgItemMessage(hwnd,941,CB_SETCURSEL,page.width>page.height?1:0,0);
        CheckDlgButton(hwnd,946,pagedefaults::current().enabled?BST_CHECKED:BST_UNCHECKED);
        if(shortcutSummary)set(931,shortcutSummary());set(802,customgroups::readError().empty()?L"更改选项后点击“保存设置”":W(customgroups::readError()));loading=false;
    }
    void select(int index){
        int next=std::max(0,std::min(kCategories-1,index));
        if(next==category&&IsWindowVisible(hwnd))return;
        if(category==5)KillTimer(hwnd,2);
        int previous=category;category=next;
        HWND focus=GetFocus();if(focus&&IsChild(hwnd,focus)&&group(GetDlgCtrlID(focus))==previous)SetFocus(GetDlgItem(hwnd,810+category));
        // Fill expensive data only when its page is first visited.
        if(category==5&&!completionReady)fillCompletion();
        if(category==6&&!groupsReady)fillGroups();
        // Layout is independent of category; switching only changes visibility.
        // Freeze child redraws as one transaction to avoid intermediate frames.
        studio_i18n::sendMessage(hwnd,WM_SETREDRAW,FALSE,0);
        for(int id=910;id<=987;++id){int g=group(id);if(g<0)continue;auto c=GetDlgItem(hwnd,id);if(!c)continue;
            bool visible=(GetWindowLongPtrW(c,GWL_STYLE)&WS_VISIBLE)!=0;
            if(visible!=(g==category))ShowWindow(c,g==category?SW_SHOWNA:SW_HIDE);
        }
        studio_i18n::sendMessage(hwnd,WM_SETREDRAW,TRUE,0);
        if(previous>=0)InvalidateRect(GetDlgItem(hwnd,810+previous),nullptr,FALSE);
        InvalidateRect(GetDlgItem(hwnd,810+category),nullptr,FALSE);
        RECT content;GetClientRect(hwnd,&content);content.left=MulDiv(178,GetDpiForWindow(hwnd),96);InvalidateRect(hwnd,&content,FALSE);
        for(int id=910;id<=987;++id)if(group(id)==category)RedrawWindow(GetDlgItem(hwnd,id),nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_FRAME);
    }
    bool syncAPI(){
        if(ai->process){set(926,L"请等待当前 AI 任务完成后修改连接配置。");return false;}
        auto endpoint=text(920),model=text(923),key=text(921);
        if(endpoint.find(L"https://")!=0||endpoint.size()>2000){set(926,L"请输入有效的 HTTPS API 基础地址。");return false;}
        if(model.empty())model=L"auto";
        if(key.size()>512||model.size()>120){set(926,L"密钥或模型名称过长。");return false;}
        ai->set(702,U(endpoint));ai->set(706,U(key));ai->set(704,U(model));CheckDlgButton(ai->hwnd,707,IsDlgButtonChecked(hwnd,922));ai->set(723,"模型 · "+U(model));
        // Minimal hook for task 7: the AI pane's "not configured" banner follows these controls,
        // and WM_SETTEXT on an EDIT does not raise EN_CHANGE. One call, no layout change.
        if(IsWindow(ai->hwnd))ai->refreshConfiguration(true);return true;
    }
    bool save(){
        try{
            DrawingSettings draft=*drawing;auto draftPage=page;
            if(dirty[0]&&(!DrawingSettings::parse(text(910).c_str(),2,200,&draft.bondLength)||!DrawingSettings::parse(text(911).c_str(),.1,12,&draft.lineWidth)||!DrawingSettings::parse(text(915).c_str(),4,96,&draft.fontSize)))throw std::runtime_error("键长范围 2–200 pt；线宽范围 0.1–12 pt；字号范围 4–96 pt。");if(dirty[0]){draft.fontName=text(914);auto preset=text(916);if(!preset.empty())draft.preset=preset;if(draft.fontName.empty()||draft.fontName.size()>120)throw std::runtime_error("请选择或输入有效字体名称。");}
            if(dirty[2]){double width,height,across,down;if(!DrawingSettings::parse(text(942).c_str(),25.4,5080,&width)||!DrawingSettings::parse(text(943).c_str(),25.4,5080,&height)||!DrawingSettings::parse(text(944).c_str(),1,20,&across)||!DrawingSettings::parse(text(945).c_str(),1,20,&down)||across!=int(across)||down!=int(down))throw std::runtime_error("请检查纸张尺寸和张数：宽高 25.4–5080 mm；横向、纵向各 1–20 张。");draftPage.width=width*72/25.4;draftPage.height=height*72/25.4;draftPage.across=int(across);draftPage.down=int(down);if(getDocument()!=pageDocument)throw std::runtime_error("当前文档已切换，请重新打开设置后修改页面。");}
            if(dirty[1]&&!syncAPI()){select(1);return false;}
            if(completionDirty&&!chemcompletion::saveEnabled(IsDlgButtonChecked(hwnd,917)==BST_CHECKED))throw std::runtime_error("无法保存基团补齐设置，请检查程序目录是否可写。");
            if(formulaDirty&&!chemcompletion::saveFormulaLayout(IsDlgButtonChecked(hwnd,966)==BST_CHECKED))throw std::runtime_error("无法保存基团公式排版设置，请检查程序目录是否可写。");
            if(dirty[0]){if(!draft.save())throw std::runtime_error("无法保存绘图默认值，请检查程序目录是否可写。");*drawing=draft;applyDrawing(draft,IsDlgButtonChecked(hwnd,912)==BST_CHECKED);}
            if(dirty[4]){auto value=U(text(950));if(value.size()>24000)throw std::runtime_error("预设提示词过长，请控制在 6000 字以内。");ai->preset=value;}
            if(dirty[1]||dirty[4])ai->save();bool savedDefault=false;
            if(dirty[2]){applyPage(draftPage,pageDocument);page=draftPage;
                pagedefaults::Values defaults;defaults.width=draftPage.width;defaults.height=draftPage.height;defaults.across=draftPage.across;defaults.down=draftPage.down;defaults.enabled=IsDlgButtonChecked(hwnd,946)==BST_CHECKED;
                if(!pagedefaults::save(defaults))throw std::runtime_error("无法保存默认页面设置，请检查程序目录是否可写。");
                savedDefault=defaults.enabled;}
            for(auto& d:dirty)d=false;completionDirty=false;formulaDirty=false;set(802,savedDefault?L"已保存 · 当前文档已更新，并设为新建文档默认页面":L"已保存 · 设置已更新");return true;
        }catch(const std::exception& e){set(802,W(e.what()));return false;}
    }
    void poll(){
        if(!IsWindowVisible(hwnd)||category!=1)return;
        bool running=ai&&ai->process;for(int id:{920,921,922,923,924,925}){auto c=GetDlgItem(hwnd,id);if(bool(IsWindowEnabled(c))==running)EnableWindow(c,!running);}
        if(category==1){if(waiting&&!running){loading=true;refreshModels();loading=false;waiting=false;}set(926,W(ai->apiStatus));}
    }
    void layout(){
        RECT r;GetClientRect(hwnd,&r);double dpi=GetDpiForWindow(hwnd)/96.;auto px=[&](int n){return int(n*dpi);};int w=r.right,h=r.bottom,x=px(202),right=w-px(30),content=right-x;
        auto move=[&](int id,int a,int b,int c,int d){auto child=GetDlgItem(hwnd,id);if(!child)return;RECT old;GetWindowRect(child,&old);MapWindowPoints(0,hwnd,(POINT*)&old,2);if(old.left!=a||old.top!=b||old.right-old.left!=std::max(1,c)||old.bottom-old.top!=std::max(1,d))MoveWindow(child,a,b,std::max(1,c),std::max(1,d),FALSE);};
        move(980,x,px(145),std::min(content,px(350)),px(260));move(981,x,px(210),content,px(50));
        int order[]={0,1,4,2,3,5,6,7};for(int i=0;i<kCategories;i++)move(810+order[i],px(16),px(87+i*46),px(146),px(40));
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
        {int previewWidth=std::min(px(220),content/3);previewRect={right-previewWidth,px(100),right,px(212)};move(970,x,px(108),content-previewWidth-px(12),px(108));}move(971,x,px(224),content-px(150),px(30));move(972,right-px(138),px(224),px(138),px(30));move(974,x,px(270),content,std::max(px(100),h-px(420)));move(973,right-px(138),h-px(140),px(138),px(30));
        {int listTop=px(190),listHeight=std::max(px(96),h-listTop-px(274));move(961,x,listTop,content,listHeight);
         auto list=GetDlgItem(hwnd,961);if(list){
            int check=px(26),origin=px(86),nameWidth=std::max(px(120),content-check-origin-px(28));
            int widths[]={check,nameWidth,origin};for(int i=0;i<3;++i)if(int(studio_i18n::sendMessage(list,LVM_GETCOLUMNWIDTH,i,0))!=widths[i])studio_i18n::sendMessage(list,LVM_SETCOLUMNWIDTH,i,widths[i]);
         }}
        for(int i=0;i<3;++i)move(985+i,x+px(i*78),h-px(227),px(74),px(26));
        move(962,x,h-px(196),std::max(px(120),content-px(270)),px(34));
        if(auto edit=GetDlgItem(hwnd,962))RedrawWindow(edit,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_FRAME);
        move(963,right-px(250),h-px(196),px(108),px(30));move(964,right-px(134),h-px(196),px(134),px(30));
        move(965,x,h-px(156),px(150),px(30));move(967,x+px(164),h-px(156),std::max(px(120),content-px(164)),px(30));
        move(802,x,h-px(88),content,px(28));move(800,right-px(228),h-px(46),px(126),px(32));move(801,right-px(90),h-px(46),px(90),px(32));
    }
    void drawPage(HDC dc,RECT r){float dpi=GetDpiForWindow(hwnd)/96.f;Graphics g(dc);g.ScaleTransform(dpi,dpi);g.SetSmoothingMode(SmoothingModeAntiAlias);g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);float w=r.right/dpi,h=r.bottom/dpi;SolidBrush bg(Color(255,255,255,255));g.FillRectangle(&bg,0.f,0.f,w,h);SolidBrush nav(studio_ui::background());g.FillRectangle(&nav,0.f,0.f,178.f,h);
        studio_ui::label(g,L"设置",RectF(26,20,146,36),18,studio_ui::ink(),true);studio_ui::label(g,L"MOLECULE STUDIO",RectF(26,55,145,20),9,studio_ui::muted());
        const wchar_t* titles[]={L"结构绘图",L"AI 与 API",L"页面与排版",L"快捷键",L"AI 预设提示词",L"基团补齐",L"自定义官能团",L"语言"};const wchar_t* subtitles[]={L"设置新建结构的线宽与键长",L"连接服务、选择模型，配置只需一次",L"调整当前文档的纸张尺寸和页面网格",L"熟悉的操作，让绘图保持流畅",L"编辑反应条件分行、上下标和紧凑排版偏好",L"搜索缩写，设置自动补齐与公式排版",L"从选中的完整结构保存可展开的官能团",L"界面语言"};
        studio_ui::label(g,titles[category],RectF(202,23,w-232,36),22,studio_ui::ink(),true);studio_ui::label(g,subtitles[category],RectF(202,65,w-232,24),12,studio_ui::muted());
        auto label=[&](const wchar_t* text,int x,int y,int width=300){studio_ui::label(g,text,RectF(float(x),float(y),float(width),24),12,studio_ui::ink());};
        if(category==0){label(L"投稿标准 / 期刊样式",202,82,540);label(L"字体",202,144,220);label(L"字号（pt）",452,144,100);label(L"键长（pt）",202,218);label(L"线宽（pt）",406,218);label(L"选择 ACS Document 1996 或期刊标准会自动填入键长、线宽、字体和字号。",202,374,540);label(L"键长用于后续绘制；已有结构坐标不缩放，单独设置过样式的对象保留原格式。",202,400,540);}
        if(category==1){label(L"API 基础地址 · 兼容 Chat Completions",202,104,540);label(L"API Key",202,172);label(L"模型",202,278);}
        if(category==5){label(L"添加自定义缩写：输入 别名=标准写法，选中文字设置格式",202,h-264,620);}
        if(category==2){label(L"纸张尺寸",202,114);label(L"方向",422,114);label(L"宽度（mm）",202,191);label(L"高度（mm）",406,191);label(L"横向张数",202,274);label(L"纵向张数",406,274);label(L"页面设置应用到当前文档，可按 Ctrl+Z 撤销。",202,390,540);studio_ui::label(g,L"勾选后，新建文档沿用这套纸张与张数；取消勾选则新建文档回到 A4。",RectF(202,414,620,24),12,studio_ui::muted());}
    }
    void paint(){
        PAINTSTRUCT ps;HDC dc=BeginPaint(hwnd,&ps);RECT r;GetClientRect(hwnd,&r);
        if(category<0||category>=8||r.right<=0||r.bottom<=0){EndPaint(hwnd,&ps);return;}
        UINT dpi=GetDpiForWindow(hwnd);
        if(paintWidth!=r.right||paintHeight!=r.bottom||paintLanguage!=studio_i18n::current()||paintDpi!=dpi){clearPaintCache();paintWidth=r.right;paintHeight=r.bottom;paintLanguage=studio_i18n::current();paintDpi=dpi;}
        if(paintCacheDisabled){drawPage(dc,r);drawGroupPreview(dc);EndPaint(hwnd,&ps);return;}
        HDC memory=CreateCompatibleDC(dc);
        if(memory){bool fresh=!pagePaint[category];if(fresh)pagePaint[category]=CreateCompatibleBitmap(dc,r.right,r.bottom);
            if(pagePaint[category]){auto previous=SelectObject(memory,pagePaint[category]);if(fresh)drawPage(memory,r);BitBlt(dc,0,0,r.right,r.bottom,memory,0,0,SRCCOPY);SelectObject(memory,previous);}
            else drawPage(dc,r);DeleteDC(memory);
        }else drawPage(dc,r);
        drawGroupPreview(dc);EndPaint(hwnd,&ps);
    }
    static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){auto self=(StudioSettingsWindow*)GetWindowLongPtrW(h,GWLP_USERDATA);if(m==WM_NCCREATE){self=(StudioSettingsWindow*)((CREATESTRUCTW*)l)->lpCreateParams;self->hwnd=h;SetWindowLongPtrW(h,GWLP_USERDATA,(LONG_PTR)self);}if(!self)return DefWindowProcW(h,m,w,l);
        switch(m){case WM_SIZE:self->layout();RedrawWindow(h,0,0,RDW_INVALIDATE|RDW_ALLCHILDREN);return 0;case WM_GETMINMAXINFO:{auto p=(MINMAXINFO*)l;UINT dpi=GetDpiForWindow(h);p->ptMinTrackSize={MulDiv(740,dpi,96),MulDiv(540,dpi,96)};return 0;}
        case WM_DPICHANGED:{auto r=(RECT*)l;SetWindowPos(h,0,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER);self->updateFont();self->layout();return 0;}
        case WM_NOTIFY:{auto header=(NMHDR*)l;
            if(header&&header->idFrom==961&&header->code==NM_CUSTOMDRAW){auto d=(NMLVCUSTOMDRAW*)l;
                if(d->nmcd.dwDrawStage==CDDS_PREPAINT)return CDRF_NOTIFYITEMDRAW;
                if(d->nmcd.dwDrawStage==CDDS_ITEMPREPAINT)return CDRF_NOTIFYSUBITEMDRAW;
                if(d->nmcd.dwDrawStage==(CDDS_ITEMPREPAINT|CDDS_SUBITEM)&&d->iSubItem==1&&d->nmcd.dwItemSpec<self->completionNames.size()){
                    auto name=self->completionNames[d->nmcd.dwItemSpec];auto pieces=chemcompletion::segments(name,true);if(pieces.empty())pieces.push_back({name,0});
                    RECT r={};ListView_GetSubItemRect(header->hwndFrom,int(d->nmcd.dwItemSpec),1,LVIR_BOUNDS,&r);
                    bool selected=ListView_GetItemState(header->hwndFrom,int(d->nmcd.dwItemSpec),LVIS_SELECTED)!=0;FillRect(d->nmcd.hdc,&r,GetSysColorBrush(selected?COLOR_HIGHLIGHT:COLOR_WINDOW));
                    int saved=SaveDC(d->nmcd.hdc);IntersectClipRect(d->nmcd.hdc,r.left,r.top,r.right,r.bottom);SetBkMode(d->nmcd.hdc,TRANSPARENT);SetTextColor(d->nmcd.hdc,GetSysColor(selected?COLOR_HIGHLIGHTTEXT:COLOR_WINDOWTEXT));
                    LOGFONTW base={};GetObjectW(self->font,sizeof(base),&base);auto italic=chemitalic::maskUtf8(name);auto custom=chemcompletion::savedFormat(name);int x=r.left+4;size_t at=0;
                    for(auto piece:pieces)for(size_t k=0;k<piece.text.size();){size_t bytes=(unsigned char)piece.text[k]<128?1:((unsigned char)piece.text[k]<224?2:3);auto ch=W(piece.text.substr(k,bytes));LOGFONTW f=base;f.lfItalic=custom.size()==name.size()&&at<custom.size()?((custom[at]-'0')&4)!=0:at<italic.size()&&italic[at];if(piece.script)f.lfHeight=LONG(base.lfHeight*.72);auto font=CreateFontIndirectW(&f);auto old=SelectObject(d->nmcd.hdc,font);SIZE size={};GetTextExtentPoint32W(d->nmcd.hdc,ch.c_str(),int(ch.size()),&size);int y=r.top+(r.bottom-r.top-abs(base.lfHeight))/2+(piece.script<0?abs(base.lfHeight)/3:piece.script>0?-abs(base.lfHeight)/5:0);TextOutW(d->nmcd.hdc,x,y,ch.c_str(),int(ch.size()));x+=size.cx;SelectObject(d->nmcd.hdc,old);DeleteObject(font);at+=bytes;k+=bytes;}
                    RestoreDC(d->nmcd.hdc,saved);return CDRF_SKIPDEFAULT;
                }
            }
if(header&&header->idFrom==961&&header->code==LVN_ITEMCHANGED){
            auto view=(NMLISTVIEW*)l;
            if(!self->completionFilling&&view->iItem>=0&&size_t(view->iItem)<self->completionKeys.size()&&(view->uChanged&LVIF_STATE)){
                bool checked=ListView_GetCheckState(GetDlgItem(h,961),view->iItem)!=0;
                auto key=self->completionKeys[size_t(view->iItem)];
                if(key.empty()){self->completionFilling=true;LVITEMW state={};state.stateMask=LVIS_STATEIMAGEMASK;state.state=0;studio_i18n::sendDlgItemMessage(h,961,LVM_SETITEMSTATE,view->iItem,(LPARAM)&state);self->completionFilling=false;return 0;}
                if(chemcompletion::setEnabled(key,checked))self->set(802,checked?L"已启用该缩写 · 立即生效":L"已停用该缩写 · 不再自动补齐");
                else{ListView_SetCheckState(GetDlgItem(h,961),view->iItem,checked?FALSE:TRUE);self->set(802,L"无法保存基团补齐清单，请检查程序目录是否可写。");}
            }
            return 0;}return DefWindowProcW(h,m,w,l);}
        case WM_COMMAND:{int id=LOWORD(w),event=HIWORD(w);if(id>=810&&id<=817){self->select(id-810);return 0;}if(id==800){self->save();return 0;}if(id==801||id==IDCANCEL){ShowWindow(h,SW_HIDE);return 0;}
            if(id==980&&event==CBN_SELCHANGE){int language=int(studio_i18n::sendDlgItemMessage(h,980,CB_GETCURSEL,0,0));if(studio_i18n::save(language)){self->translate();studio_i18n::refreshAll();RedrawWindow(self->owner,0,0,RDW_INVALIDATE|RDW_ALLCHILDREN);}else{studio_i18n::sendDlgItemMessage(h,980,CB_SETCURSEL,studio_i18n::current(),0);self->set(802,L"无法保存语言设置，请检查程序目录是否可写。");}return 0;}
            if(id==968&&event==EN_CHANGE){if(!self->loading){KillTimer(h,2);SetTimer(h,2,120,nullptr);}return 0;}
            if(id==974&&HIWORD(w)==LBN_SELCHANGE){self->updateGroupPreview();return 0;}
            if(id==972){try{if(!self->saveGroup)throw std::runtime_error("画布未连接");self->saveGroup(U(self->text(971)));self->fillGroups();self->fillCompletion();self->set(971,L"");self->set(802,L"已保存官能团，可输入缩写使用");}catch(const std::exception& e){self->set(802,W(e.what()));}return 0;}
            if(id==973){int i=int(studio_i18n::sendDlgItemMessage(h,974,LB_GETCURSEL,0,0));if(i>=0){wchar_t n[64]={};studio_i18n::sendDlgItemMessage(h,974,LB_GETTEXT,i,(LPARAM)n);try{customgroups::remove(U(n));++chemcompletion::state().revision;self->fillGroups();self->fillCompletion();self->set(802,L"已移除库中定义，现有文档内的结构保留");}catch(const std::exception& e){self->set(802,W(e.what()));}}return 0;}
            if(id>=985&&id<=987){auto edit=GetDlgItem(h,962);CHARFORMAT2W f={};f.cbSize=sizeof(f);::SendMessageW(edit,EM_GETCHARFORMAT,SCF_SELECTION,(LPARAM)&f);DWORD effect=id==985?CFE_SUPERSCRIPT:id==986?CFE_SUBSCRIPT:CFE_ITALIC;bool on=(f.dwEffects&effect)!=0;f.dwMask=id==987?CFM_ITALIC:CFM_SUBSCRIPT|CFM_SUPERSCRIPT;f.dwEffects=on?0:effect;::SendMessageW(edit,EM_SETCHARFORMAT,SCF_SELECTION,(LPARAM)&f);SetFocus(edit);return 0;}
            if(id==963){auto value=U(self->text(962));size_t eq=value.find('=');if(eq==std::string::npos||eq==0||eq+1>=value.size()){self->set(802,L"请按“别名=标准写法”输入，例如 tb=t-Bu");return 0;}
                auto alias=value.substr(0,eq),canonical=value.substr(eq+1);
                std::string format;auto edit=GetDlgItem(h,962);for(size_t i=eq+1;i<value.size();++i){CHARRANGE r={LONG(i),LONG(i+1)};::SendMessageW(edit,EM_EXSETSEL,0,(LPARAM)&r);CHARFORMAT2W f={};f.cbSize=sizeof(f);::SendMessageW(edit,EM_GETCHARFORMAT,SCF_SELECTION,(LPARAM)&f);format+=char('0'+((f.dwEffects&CFE_SUPERSCRIPT)?2:(f.dwEffects&CFE_SUBSCRIPT)?1:0)+((f.dwEffects&CFE_ITALIC)?4:0));}
                if(chemcompletion::saveFormat(canonical,format)&&chemcompletion::addCustom(alias,canonical)){self->fillCompletion();self->set(962,L"");self->set(802,W("已添加 "+canonical+" · 立即生效"));}
                else self->set(802,L"无法添加：别名只能用字母数字，不能是元素符号，标准写法需为可见字符。");
                return 0;}
            if(id==964){int index=-1;for(int i=0;i<int(self->completionKeys.size());++i)if(ListView_GetItemState(GetDlgItem(h,961),i,LVIS_SELECTED))index=i;
                if(index<0){self->set(802,L"请先在清单里选中要删除的自定义缩写");return 0;}
                if(chemcompletion::removeCustom(self->completionKeys[size_t(index)])){self->fillCompletion();self->set(802,L"已删除该自定义缩写");}
                else self->set(802,L"内置缩写不能删除，可取消勾选停用");
                return 0;}
            if(id==965&&event==BN_CLICKED){chemcompletion::resetEntries();self->fillCompletion();self->set(802,L"已恢复补齐默认值；自定义官能团结构库保留");return 0;}
            if(id==969&&event==BN_CLICKED){bool on=IsDlgButtonChecked(h,969)==BST_CHECKED;CheckDlgButton(h,917,on?BST_CHECKED:BST_UNCHECKED);self->completionDirty=true;return 0;}
            if(id==966&&event==BN_CLICKED){self->formulaDirty=true;return 0;}
            if(id==951){self->set(950,W(AiWindow::defaultPreset()));self->dirty[4]=true;self->set(802,L"已恢复默认预设，点击保存设置后生效。");return 0;}
            if(id==913){self->set(910,L"14.40");self->set(911,L"1.19");self->dirty[0]=true;return 0;}
            if(id==930){if(self->editShortcuts)self->editShortcuts();if(self->shortcutSummary)self->set(931,self->shortcutSummary());return 0;}
            if(id==924||id==925){if(self->syncAPI()){self->ai->start(true,id==924);self->waiting=true;self->poll();}return 0;}
            if(self->loading)return 0;
            if(id==917&&event==BN_CLICKED){self->completionDirty=true;if(GetDlgItem(h,969))CheckDlgButton(h,969,IsDlgButtonChecked(h,917));return 0;}
            int g=group(id);if(g>=0&&g<5&&(event==EN_CHANGE||event==CBN_EDITCHANGE||event==CBN_SELCHANGE||event==BN_CLICKED))self->dirty[g]=true;
            if(id==916&&event==CBN_SELCHANGE){int index=int(studio_i18n::sendDlgItemMessage(h,916,CB_GETCURSEL,0,0));if(index>=0&&size_t(index)<journalStyles().size()){const auto& style=journalStyles()[size_t(index)];self->set(910,number(style.bondLength));self->set(911,number(style.lineWidth));self->set(914,style.font);self->set(915,number(style.fontSize));self->dirty[0]=true;}return 0;}
            if((id==940||id==941)&&event==CBN_SELCHANGE){int index=int(studio_i18n::sendDlgItemMessage(h,940,CB_GETCURSEL,0,0)),orientation=int(studio_i18n::sendDlgItemMessage(h,941,CB_GETCURSEL,0,0));double sizes[][2]={{210,297},{297,420},{148,210},{176,250},{215.9,279.4},{215.9,355.6}};
                if(index>=0&&index<6){self->set(942,number(sizes[index][orientation?1:0]));self->set(943,number(sizes[index][orientation?0:1]));}else{double width,height;if(DrawingSettings::parse(self->text(942).c_str(),25.4,5080,&width)&&DrawingSettings::parse(self->text(943).c_str(),25.4,5080,&height)&&(orientation==1)!=(width>height)){self->set(942,number(height));self->set(943,number(width));}}}
            return 0;}
        case WM_TIMER:if(w==2){KillTimer(h,2);self->fillCompletion();}else self->poll();return 0;case WM_CLOSE:KillTimer(h,2);ShowWindow(h,SW_HIDE);return 0;case WM_DESTROY:KillTimer(h,1);KillTimer(h,2);self->hwnd=0;return 0;
        case WM_ERASEBKGND:return 1;case WM_PAINT:self->paint();return 0;
        case WM_DRAWITEM:{auto item=(DRAWITEMSTRUCT*)l;return studio_ui::button(item,item->CtlID==800,item->CtlID==UINT(810+self->category),GetDpiForWindow(h)/96.f);}
        case WM_CTLCOLORSTATIC:case WM_CTLCOLORBTN:SetBkColor((HDC)w,RGB(255,255,255));SetTextColor((HDC)w,RGB(105,121,133));return (LRESULT)GetStockObject(WHITE_BRUSH);
        }return DefWindowProcW(h,m,w,l);
    }
    void updateFont(){auto old=font;font=studio_ui::font(hwnd);for(int id=800;id<=987;id++)if(auto control=GetDlgItem(hwnd,id))studio_i18n::sendMessage(control,WM_SETFONT,(WPARAM)font,TRUE);if(old)DeleteObject(old);}
    void show(HWND parent,int initial=0){owner=parent;if(!IsWindow(hwnd)){
        WNDCLASSW wc={};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(0);wc.hCursor=LoadCursorW(0,MAKEINTRESOURCEW(32512));wc.hIcon=LoadIconW(wc.hInstance,MAKEINTRESOURCEW(1));wc.lpszClassName=L"MoleculeStudioSettings";RegisterClassW(&wc);RECT r;GetWindowRect(parent,&r);UINT dpi=GetDpiForWindow(parent);
        MONITORINFO mi={};mi.cbSize=sizeof(mi);GetMonitorInfoW(MonitorFromWindow(parent,MONITOR_DEFAULTTONEAREST),&mi);int width=std::min(MulDiv(820,dpi,96),int(mi.rcWork.right-mi.rcWork.left)-20),height=std::min(MulDiv(620,dpi,96),int(mi.rcWork.bottom-mi.rcWork.top)-20);
        studio_i18n::createWindowEx(WS_EX_CONTROLPARENT,L"MoleculeStudioSettings",L"Molecule Studio · 设置",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,mi.rcWork.left+(mi.rcWork.right-mi.rcWork.left-width)/2,mi.rcWork.top+(mi.rcWork.bottom-mi.rcWork.top-height)/2,width,height,parent,nullptr,wc.hInstance,this);
        auto child=[&](const wchar_t* cls,const wchar_t* title,int id,DWORD style=0){auto control=studio_i18n::createWindowEx(0,cls,title,WS_CHILD|style,0,0,1,1,hwnd,(HMENU)(INT_PTR)id,wc.hInstance,nullptr);if(id==914||id==916||id==923||id==961||id==974||id==980)studio_i18n::protect(control);if(title&&*title&&wcscmp(cls,L"EDIT")!=0&&wcscmp(cls,L"COMBOBOX")!=0)captions[id]=title;return control;};
        auto button=[&](const wchar_t* title,int id){return child(L"BUTTON",title,id,WS_TABSTOP|BS_OWNERDRAW);};auto edit=[&](const wchar_t* title,int id,DWORD style=0){return child(L"EDIT",title,id,WS_TABSTOP|WS_BORDER|((style&ES_MULTILINE)?0:ES_AUTOHSCROLL)|style);};
        const wchar_t* categories[]={L"结构绘图",L"AI 与 API",L"页面与排版",L"快捷键",L"AI 预设",L"基团补齐",L"自定义官能团",L"语言"};for(int i=0;i<kCategories;i++){button(categories[i],810+i);ShowWindow(GetDlgItem(hwnd,810+i),SW_SHOW);}
        edit(L"",950,ES_MULTILINE|ES_AUTOVSCROLL|ES_WANTRETURN|WS_VSCROLL);studio_i18n::sendDlgItemMessage(hwnd,950,EM_SETLIMITTEXT,6000,0);button(L"恢复默认预设",951);child(L"STATIC",L"预设提示词",952);child(L"STATIC",L"试剂与当量在上，溶剂温度时间在下；保存后从下一次发送起生效。",953);
        child(L"COMBOBOX",L"",916,WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL);for(auto& style:journalStyles())studio_i18n::sendDlgItemMessage(hwnd,916,CB_ADDSTRING,0,(LPARAM)style.name);child(L"COMBOBOX",L"",914,WS_TABSTOP|CBS_DROPDOWN|CBS_AUTOHSCROLL|WS_VSCROLL);studio_i18n::sendDlgItemMessage(hwnd,914,CB_LIMITTEXT,120,0);for(auto name:{L"Arial",L"Helvetica",L"Times New Roman",L"Calibri",L"Courier New",L"Microsoft YaHei",L"Symbol"})studio_i18n::sendDlgItemMessage(hwnd,914,CB_ADDSTRING,0,(LPARAM)name);edit(L"10",915);edit(L"14.40",910);edit(L"0.60",911);child(L"BUTTON",L"同时应用到当前文档的默认格式",912,WS_TABSTOP|BS_AUTOCHECKBOX);button(L"恢复默认值",913);
        child(L"BUTTON",L"自动补齐基团大小写和连字符（如 tbu → t-Bu）",917,WS_TABSTOP|BS_AUTOCHECKBOX);
        child(L"BUTTON",L"启用基团自动补齐（按下面的清单识别官能团缩写）",969,WS_TABSTOP|BS_AUTOCHECKBOX);
        child(L"BUTTON",L"公式排版：原子数下标、同位素与电荷上标，支持自由基（SO4 → SO₄²⁻）",966,WS_TABSTOP|BS_AUTOCHECKBOX);
        child(L"STATIC",L"缩写清单",960);
        {auto list=child(WC_LISTVIEWW,L"",961,WS_BORDER|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS|WS_TABSTOP);
         studio_i18n::sendMessage(list,LVM_SETEXTENDEDLISTVIEWSTYLE,0,LPARAM(LVS_EX_FULLROWSELECT|LVS_EX_CHECKBOXES|LVS_EX_DOUBLEBUFFER));
         const wchar_t* headers[]={L"",L"缩写",L"来源"};const int widths[]={28,300,60};
         for(int i=0;i<3;i++){LVCOLUMNW column={};column.mask=LVCF_TEXT|LVCF_WIDTH;column.cx=widths[i];column.pszText=const_cast<wchar_t*>(headers[i]);studio_i18n::sendMessage(list,LVM_INSERTCOLUMNW,UINT(i),(LPARAM)&column);}}
        edit(L"",968);studio_i18n::sendDlgItemMessage(hwnd,968,EM_SETCUEBANNER,TRUE,(LPARAM)L"搜索缩写、别名或公式（如 SO4）…");
        child(L"STATIC",L"1  画出完整官能团，用一根单键连接末端 R 原子。\n2  框选整组结构（包括 R），在此输入缩写并保存。\n示例：Ns = 邻硝基苯磺酰基，R 连接硫原子。\nR 是外部连接占位，保存后不会作为真实原子展开。",970);edit(L"",971);studio_i18n::sendDlgItemMessage(hwnd,971,EM_SETLIMITTEXT,24,0);studio_i18n::sendDlgItemMessage(hwnd,971,EM_SETCUEBANNER,TRUE,(LPARAM)L"缩写名称，例如 Ns");button(L"保存画布选区",972);button(L"删除所选定义",973);child(L"LISTBOX",L"",974,WS_TABSTOP|WS_BORDER|WS_VSCROLL|LBS_NOTIFY);
        LoadLibraryW(L"Msftedit.dll");auto completionEdit=child(L"RICHEDIT50W",L"",962,WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL|ES_NOHIDESEL);
        ::SendMessageW(completionEdit,EM_SETBKGNDCOLOR,0,RGB(255,255,255));
        ::SendMessageW(completionEdit,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,MAKELPARAM(6,6));
        button(L"上标",985);button(L"下标",986);button(L"斜体",987);studio_i18n::sendDlgItemMessage(hwnd,962,EM_SETLIMITTEXT,120,0);
        button(L"添加",963);button(L"删除选中",964);button(L"恢复默认",965);child(L"STATIC",L"",967);
        edit(L"https://api.deepseek.com",920);edit(L"",921,ES_PASSWORD);studio_i18n::sendDlgItemMessage(hwnd,921,EM_SETLIMITTEXT,512,0);child(L"BUTTON",L"在这台电脑记住密钥（Windows 加密保存）",922,WS_TABSTOP|BS_AUTOCHECKBOX);
        child(L"COMBOBOX",L"",923,WS_TABSTOP|CBS_DROPDOWN|CBS_AUTOHSCROLL|WS_VSCROLL);studio_i18n::sendDlgItemMessage(hwnd,923,CB_LIMITTEXT,120,0);button(L"获取模型",924);button(L"测试连接",925);child(L"STATIC",L"尚未测试连接",926);
        button(L"编辑快捷键…",930);edit(L"",931,ES_MULTILINE|ES_READONLY|WS_VSCROLL);child(L"STATIC",L"支持修改、清除与恢复 默认快捷键。",932);
        child(L"COMBOBOX",L"",940,WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL);for(auto name:{L"A4",L"A3",L"A5",L"B5 (ISO)",L"US Letter",L"US Legal",L"自定义"})studio_i18n::sendDlgItemMessage(hwnd,940,CB_ADDSTRING,0,(LPARAM)name);
        child(L"COMBOBOX",L"",941,WS_TABSTOP|CBS_DROPDOWNLIST);for(auto name:{L"纵向",L"横向"})studio_i18n::sendDlgItemMessage(hwnd,941,CB_ADDSTRING,0,(LPARAM)name);
        edit(L"210",942);edit(L"297",943);edit(L"1",944);edit(L"1",945);
        child(L"BUTTON",L"同时设为新建文档的默认页面（宽高与张数）",946,WS_TABSTOP|BS_AUTOCHECKBOX);
        button(L"保存设置",800);button(L"关闭",801);child(L"STATIC",L"",802);for(int id:{800,801,802})ShowWindow(GetDlgItem(hwnd,id),SW_SHOW);
        child(L"COMBOBOX",L"",980,WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL);for(int i=0;i<15;++i)studio_i18n::sendDlgItemMessage(hwnd,980,CB_ADDSTRING,0,(LPARAM)studio_i18n::name(i));child(L"STATIC",L"选择语言后立即生效并自动保存。",981);
        SetWindowSubclass(GetDlgItem(hwnd,974),groupListKey,1,reinterpret_cast<DWORD_PTR>(this));updateFont();translate();layout();SetTimer(hwnd,1,350,nullptr);
        }
        if(!IsWindowVisible(hwnd)){populate();category=-1;}select(initial);ShowWindow(hwnd,SW_SHOWNORMAL);RedrawWindow(hwnd,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN);SetForegroundWindow(hwnd);
    }
};

