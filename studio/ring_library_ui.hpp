#pragma once
#include "template_thumbnail.hpp"
#include "ring_library_template.hpp"
static TemplateLibraryModel ringLibraryModel(){
    TemplateLibraryModel model;
    for(auto category:ring_library::categories)model.categories.push_back(category);
    for(const auto& p:ring_library::parents)model.parents.push_back({p.title,p.english,std::string(p.key)+" "+p.aliases,p.category,p.firstVariant,p.variantCount});
    for(const auto& v:ring_library::variants)model.variants.push_back({v.parent,v.title,v.formula});
    model.load=[](int index){auto source=std::make_shared<Session>();auto created=std::make_shared<std::vector<ObjectRef>>();auto e=source->submit(ringTemplateCommand(index,Vec2(),source->doc.defaultBondLength(),created),"缩略图");if(!e.ok())throw std::runtime_error(e.message);return source;};
    return model;
}
static void paintRingThumbnail(Graphics& g,int index,RectF rect){
    static TemplateLibraryModel model=ringLibraryModel();const wchar_t* actionLabel=L"选择并放置";model.paint(g,index,rect);
}
struct RingLibraryPicker{
    TemplateLibraryModel model=ringLibraryModel();const wchar_t* actionLabel=L"选择并放置";
    HWND hwnd=0,owner=0,search=0,category=0,variant=0,place=0,prev=0,next=0;HFONT font=0;
    float dpi=1;int page=0,selected=10,result=-1,columns=4,rows=3;std::vector<int> filtered,recent;
    std::wstring query;std::vector<std::pair<int,RectF>> cards;
    static std::wstring lower(std::wstring text){for(auto& c:text)c=std::towlower(c);return text;}
    static bool matches(int parent,const std::wstring& text){return ringLibraryModel().matches(parent,text);}

    int selectedParent()const{return model.variants[selected].parent;}
    void selectParent(int p){auto& parent=model.parents[p];if(selectedParent()!=p){selected=parent.firstVariant;if(studio_i18n::sendMessage(category,LB_GETCURSEL,0,0)==1)for(int id:recent)if(model.variants[id].parent==p){selected=id;break;}}studio_i18n::sendMessage(variant,CB_RESETCONTENT,0,0);for(int i=0;i<parent.variantCount;++i)studio_i18n::sendMessage(variant,CB_ADDSTRING,0,(LPARAM)W(model.variants[parent.firstVariant+i].title).c_str());studio_i18n::sendMessage(variant,CB_SETCURSEL,selected-parent.firstVariant,0);EnableWindow(variant,parent.variantCount>1);InvalidateRect(hwnd,0,FALSE);}
    void filter(){filtered.clear();wchar_t value[512]={};studio_i18n::getWindowText(search,value,512);query=value;int group=int(studio_i18n::sendMessage(category,LB_GETCURSEL,0,0));
        if(group==1){for(int id:recent){int p=model.variants[id].parent;if(model.matches(p,query)&&std::find(filtered.begin(),filtered.end(),p)==filtered.end())filtered.push_back(p);}}
        else for(int i=0;i<int(model.parents.size());++i)if((group<2||std::string(model.parents[i].category)==model.categories[group-2])&&model.matches(i,query))filtered.push_back(i);
        page=0;if(!filtered.empty()){selectParent(std::find(filtered.begin(),filtered.end(),selectedParent())!=filtered.end()?selectedParent():filtered[0]);page=int(std::find(filtered.begin(),filtered.end(),selectedParent())-filtered.begin())/(rows*columns);}EnableWindow(place,!filtered.empty());EnableWindow(variant,!filtered.empty()&&model.parents[selectedParent()].variantCount>1);InvalidateRect(hwnd,0,FALSE);
    }
    void layout(){RECT rc;GetClientRect(hwnd,&rc);float w=rc.right/dpi,h=rc.bottom/dpi;rows=std::max(1,int((h-170)/model.cardHeight));
        auto position=[&](HWND control,float x,float y,float width,float height){SetWindowPos(control,0,int(x*dpi),int(y*dpi),int(width*dpi),int(height*dpi),SWP_NOZORDER|SWP_NOACTIVATE);};
        position(search,214,16,w-232,31);position(category,16,58,180,h-122);position(variant,214,h-80,210,180);position(place,w-137,h-45,119,29);position(prev,w-220,h-81,89,27);position(next,w-117,h-81,99,27);InvalidateRect(hwnd,0,FALSE);
    }
    RectF cardRect(int slot)const{RECT rc;GetClientRect(hwnd,&rc);float width=(rc.right/dpi-232)/columns;return RectF(214+(slot%columns)*width,58+(slot/columns)*model.cardHeight,width-8,float(model.cardHeight-8));}
    void paint(Graphics& g){RECT rc;GetClientRect(hwnd,&rc);float w=rc.right/dpi,h=rc.bottom/dpi;g.SetPageUnit(UnitPixel);g.Clear(Color(255,247,249,248));g.ScaleTransform(dpi,dpi);g.SetSmoothingMode(SmoothingModeAntiAlias);g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
        txt(g,model.title,RectF(18,15,175,27),18,ink,true);cards.clear();int perPage=rows*columns,total=std::max(1,int((filtered.size()+perPage-1)/perPage));page=std::max(0,std::min(page,total-1));
        for(int slot=0;slot<perPage&&page*perPage+slot<int(filtered.size());++slot){int id=filtered[page*perPage+slot];const auto& p=model.parents[id];auto box=cardRect(slot);cards.push_back({id,box});roundbox(g,box,6,white);Pen border(id==selectedParent()?teal:line,id==selectedParent()?1.6f:1.f);g.DrawRectangle(&border,box);
            int vi=id==selectedParent()?selected:p.firstVariant;model.paint(g,vi,RectF(box.X+5,box.Y+5,box.Width-10,float(model.thumbnailHeight)));(std::wstring(model.windowClass)==L"MoleculeStudioCustomTemplates"?rawTxt:txt)(g,p.title,RectF(box.X+7,box.Y+model.thumbnailHeight+8,box.Width-14,21),11,ink,true,true);(std::wstring(model.windowClass)==L"MoleculeStudioCustomTemplates"?rawTxt:txt)(g,p.english,RectF(box.X+5,box.Y+model.thumbnailHeight+30,box.Width-10,16),9,muted,false,true);
        }
        if(filtered.empty())txt(g,query.empty()?"最近还没有使用记录":("没有找到匹配的"+model.noun),RectF(220,120,w-250,60),15,muted,false,true);
        std::string count=std::to_string(filtered.size())+(" 个"+model.noun+" · ")+std::to_string(page+1)+" / "+std::to_string(total)+" 页";txt(g,count,RectF(16,h-55,185,22),10,muted);
        if(!filtered.empty()){const auto& p=model.parents[selectedParent()];const auto& v=model.variants[selected];(std::wstring(model.windowClass)==L"MoleculeStudioCustomTemplates"?rawTxt:txt)(g,std::string(p.title)+" · "+p.english+" · "+v.formula,RectF(214,h-116,w-234,25),12,ink,true,false);}
        txt(g,"单击选择 · 双击放置 · Enter 确定 · Esc 取消",RectF(214,h-43,w-365,26),10,muted);EnableWindow(prev,page>0);EnableWindow(next,page+1<total);
    }
    void accept(){if(filtered.empty())return;result=selected;DestroyWindow(hwnd);}
    static LRESULT CALLBACK editProc(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR data){auto self=(RingLibraryPicker*)data;if(m==WM_KEYDOWN&&w==VK_RETURN){self->accept();return 0;}if(m==WM_KEYDOWN&&w==VK_ESCAPE){DestroyWindow(self->hwnd);return 0;}return DefSubclassProc(h,m,w,l);}
    static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){auto self=(RingLibraryPicker*)GetWindowLongPtrW(h,GWLP_USERDATA);if(m==WM_NCCREATE){self=(RingLibraryPicker*)((CREATESTRUCTW*)l)->lpCreateParams;self->hwnd=h;SetWindowLongPtrW(h,GWLP_USERDATA,(LONG_PTR)self);}if(!self)return DefWindowProcW(h,m,w,l);
        if(m==WM_CREATE){
            self->font=CreateFontW(-int(12*self->dpi),0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");
            auto child=[&](const wchar_t* type,const wchar_t* title,DWORD style,int id){auto control=studio_i18n::createWindowEx(0,type,title,WS_CHILD|WS_VISIBLE|WS_TABSTOP|style,0,0,10,10,h,(HMENU)UINT_PTR(id),GetModuleHandleW(0),0);studio_i18n::sendMessage(control,WM_SETFONT,(WPARAM)self->font,TRUE);return control;};
            self->search=child(L"EDIT",L"",ES_AUTOHSCROLL|WS_BORDER,101);studio_i18n::sendMessage(self->search,EM_SETCUEBANNER,TRUE,(LPARAM)W(self->model.cue).c_str());SetWindowSubclass(self->search,editProc,1,(DWORD_PTR)self);
            self->category=child(L"LISTBOX",L"",LBS_NOTIFY|WS_VSCROLL|LBS_NOINTEGRALHEIGHT,102);if(std::wstring(self->model.windowClass)==L"MoleculeStudioCustomTemplates")studio_i18n::protect(self->category);studio_i18n::sendMessage(self->category,LB_ADDSTRING,0,(LPARAM)studio_i18n::tr(W("全部 · "+std::to_string(self->model.parents.size())+" 个"+self->model.noun)).c_str());studio_i18n::sendMessage(self->category,LB_ADDSTRING,0,(LPARAM)studio_i18n::tr(L"最近使用").c_str());for(const auto& c:self->model.categories)studio_i18n::sendMessage(self->category,LB_ADDSTRING,0,(LPARAM)W(c).c_str());studio_i18n::sendMessage(self->category,LB_SETCURSEL,0,0);
            self->variant=child(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL,103);self->place=child(L"BUTTON",self->actionLabel,BS_DEFPUSHBUTTON,104);self->prev=child(L"BUTTON",L"上一页",BS_PUSHBUTTON,105);self->next=child(L"BUTTON",L"下一页",BS_PUSHBUTTON,106);self->layout();self->filter();return 0;
        }
        if(m==WM_SIZE){if(self->search)self->layout();return 0;}
        if(m==WM_PAINT){PAINTSTRUCT ps;HDC dc=BeginPaint(h,&ps);RECT rc;GetClientRect(h,&rc);Bitmap buffer(rc.right,rc.bottom,PixelFormat32bppARGB);{Graphics g(&buffer);self->paint(g);}Graphics dest(dc);dest.SetPageUnit(UnitPixel);dest.DrawImage(&buffer,Rect(0,0,rc.right,rc.bottom),0,0,rc.right,rc.bottom,UnitPixel);EndPaint(h,&ps);return 0;}
        if(m==WM_ERASEBKGND)return 1;
        if(m==WM_COMMAND){int id=LOWORD(w),code=HIWORD(w);if(id==IDCANCEL){DestroyWindow(h);return 0;}if(id==IDOK){self->accept();return 0;}if(id==101&&code==EN_CHANGE)self->filter();else if(id==102&&code==LBN_SELCHANGE)self->filter();else if(id==103&&code==CBN_SELCHANGE){self->selected=self->model.parents[self->selectedParent()].firstVariant+int(studio_i18n::sendMessage(self->variant,CB_GETCURSEL,0,0));InvalidateRect(h,0,FALSE);}else if(id==104)self->accept();else if(id==105||id==106){self->page+=id==105?-1:1;InvalidateRect(h,0,FALSE);}return 0;}
        if(m==WM_LBUTTONUP||m==WM_LBUTTONDBLCLK){float x=GET_X_LPARAM(l)/self->dpi,y=GET_Y_LPARAM(l)/self->dpi;for(auto card:self->cards)if(card.second.Contains(x,y)){self->selectParent(card.first);if(m==WM_LBUTTONDBLCLK)self->accept();break;}return 0;}
        if(m==WM_MOUSEWHEEL){self->page+=GET_WHEEL_DELTA_WPARAM(w)>0?-1:1;InvalidateRect(h,0,FALSE);return 0;}
        if(m==WM_KEYDOWN){if(w==VK_ESCAPE)DestroyWindow(h);else if(w==VK_RETURN)self->accept();else if(w==VK_NEXT||w==VK_PRIOR){self->page+=w==VK_NEXT?1:-1;InvalidateRect(h,0,FALSE);}return 0;}
        if(m==WM_CLOSE){DestroyWindow(h);return 0;}return DefWindowProcW(h,m,w,l);
    }
    int show(HWND parent,int initial,const std::vector<int>& history){owner=GetAncestor(parent,GA_ROOT);selected=std::max(0,std::min(initial,int(model.variants.size())-1));recent=history;dpi=GetDpiForWindow(owner)/96.f;
        WNDCLASSW wc={};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(0);wc.hCursor=LoadCursorW(0,MAKEINTRESOURCEW(32512));wc.style=CS_DBLCLKS;wc.lpszClassName=model.windowClass;RegisterClassW(&wc);
        MONITORINFO monitor={sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(owner,MONITOR_DEFAULTTONEAREST),&monitor);dpi=std::min(dpi,std::min((monitor.rcWork.right-monitor.rcWork.left-30)/970.f,(monitor.rcWork.bottom-monitor.rcWork.top-35)/700.f));dpi=std::max(.6f,dpi);
        RECT windowRect={0,0,int(950*dpi),int(650*dpi)};AdjustWindowRectEx(&windowRect,WS_CAPTION|WS_SYSMENU,0,WS_EX_DLGMODALFRAME);int width=windowRect.right-windowRect.left,height=windowRect.bottom-windowRect.top;RECT host;GetWindowRect(owner,&host);int x=std::max(int(monitor.rcWork.left),std::min(int((host.left+host.right-width)/2),int(monitor.rcWork.right)-width)),y=std::max(int(monitor.rcWork.top),std::min(int((host.top+host.bottom-height)/2),int(monitor.rcWork.bottom)-height));
        studio_i18n::createWindowEx(WS_EX_DLGMODALFRAME,wc.lpszClassName,W(model.title+" · 结构库").c_str(),WS_POPUP|WS_CAPTION|WS_SYSMENU,x,y,width,height,owner,0,wc.hInstance,this);if(!hwnd){if(font)DeleteObject(font);return -1;}EnableWindow(owner,FALSE);ShowWindow(hwnd,SW_SHOW);SetFocus(search);MSG msg={};
        while(IsWindow(hwnd)&&GetMessageW(&msg,0,0,0)>0){if(!IsDialogMessageW(hwnd,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
        if(msg.message==WM_QUIT)PostQuitMessage(int(msg.wParam));EnableWindow(owner,TRUE);SetForegroundWindow(owner);if(font)DeleteObject(font);return result;
    }
};

