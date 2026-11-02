#pragma once
// Browser-style document tabs.
//
// One frame window owns one complete editor window per tab.  Each editor keeps
// its own document, undo history, selection, canvas and renderer, so switching
// tabs never copies or rebuilds document state; it only shows/hides a window.
//
// The tab strip is drawn by the active editor as a 36 px row under the title
// row, which keeps every piece of chrome inside the editor window and lets the
// shell double-buffer it together with the rest of the header.
#include <functional>
struct TabHost {
    HWND hwnd=0;HINSTANCE inst=0;
    std::vector<App*> tabs,retired;
    int active=-1;float scale=1.f;int width=1280,height=820;
    float bandH=40.f;
    float scroll=0.f,tabsLeft=8.f,tabsRight=0.f,totalWidth=0.f;
    std::string hover;
    bool closing=false,liveSizing=false,resizePending=false;
    std::function<App*(HWND,const std::string&,bool)> factory;
    std::vector<RectF> tabRects;RectF plusRect,listRect;
    HWND tip=0;

    struct Hit {std::string kind;int index;Hit():index(-1){}Hit(const char* k,int i):kind(k),index(i){}};

    static float labelWidth(const std::string& text,float size){
        float total=0;
        for(size_t i=0;i<text.size();){
            unsigned char c=(unsigned char)text[i];
            if(c<0x80){total+=size*.56f;i+=1;}
            else if((c>>5)==0x6){total+=size*.56f;i+=2;}
            else if((c>>4)==0xE){total+=size*1.f;i+=3;}
            else{total+=size*.56f;i+=4;}
        }
        return total;
    }
    std::wstring tabTitle(int index)const{
        if(index<0||index>=int(tabs.size()))return L"";
        wchar_t buf[512]={};studio_i18n::getWindowText(tabs[index]->h,buf,512);
        std::wstring text(buf);
        size_t dot=text.find(L'\u00b7');
        if(dot!=std::wstring::npos){
            text=text.substr(dot+1);
            size_t first=text.find_first_not_of(L" \t");
            text=first==std::wstring::npos?L"":text.substr(first);
        }
        while(!text.empty()&&(text.back()==L' '||text.back()==L'*'||text.back()==L'\t'))text.pop_back();
        if(text.empty())text=L"\u672a\u547d\u540d\u7ed3\u6784";
        if(text==L"未命名结构")text=studio_i18n::tr(text);
        return text;
    }
    // Tab labels drop the document extension; the tooltip keeps the full name.
    std::wstring displayName(int index)const{
        std::wstring text=tabTitle(index);
        size_t dot=text.find_last_of(L'.');
        if(dot!=std::wstring::npos&&dot>0&&text.size()-dot<=6)text=text.substr(0,dot);
        return text;
    }
    std::wstring tooltipText(int index)const{
        if(index<0||index>=int(tabs.size()))return L"";
        std::wstring text=tabTitle(index);
        if(tabs[index]->nmrPage)return text+studio_i18n::tr(L"\n基于打开时的结构快照；可以切回结构标签页继续编辑");
        if(tabs[index]->s.dirty)text+=studio_i18n::tr(L"  \u2022  \u672a\u4fdd\u5b58");
        const std::string& path=tabs[index]->s.sourcePath;
        if(!path.empty())text+=L"\n"+W(path);
        else text+=studio_i18n::tr(L"\n\u5c1a\u672a\u4fdd\u5b58\u5230\u6587\u4ef6");
        return text;
    }
    float tabWidth(int index)const{
        float text=labelWidth(U(displayName(index)),13.f);
        float box=text+52.f;
        if(box<120.f)box=120.f;
        if(box>220.f)box=220.f;
        return box;
    }
    void layoutTabs(float w){
        tabRects.clear();
        tabsLeft=210.f;tabsRight=std::max(tabsLeft+80.f,w-344.f);
        float right=tabsRight;
        listRect=RectF(right-24.f,8.f,24.f,24.f);right-=26.f;
        plusRect=RectF(right-28.f,8.f,28.f,24.f);right-=34.f;
        std::vector<float> widths;
        float total=0;
        for(int i=0;i<int(tabs.size());++i){widths.push_back(tabWidth(i));total+=widths[i];}
        totalWidth=total;
        float visible=std::max(10.f,right-tabsLeft);
        float maxScroll=std::max(0.f,total-visible);
        if(scroll>maxScroll)scroll=maxScroll;
        if(scroll<0.f)scroll=0.f;
        float x=tabsLeft-scroll;
        for(size_t i=0;i<widths.size();++i){
            tabRects.push_back(RectF(x,0.f,widths[i],bandH));
            x+=widths[i];
        }
    }
    void ensureVisible(int index){
        if(index<0||index>=int(tabRects.size()))return;
        float left=tabRects[index].X,right=tabRects[index].GetRight();
        if(left<tabsLeft)scroll-=tabsLeft-left;
        else if(right>tabsRight)scroll+=right-tabsRight;
        if(scroll<0.f)scroll=0.f;
    }
    void scrollBy(float delta){
        float visible=std::max(10.f,tabsRight-tabsLeft);
        float maxScroll=std::max(0.f,totalWidth-visible);
        scroll+=delta;
        if(scroll<0.f)scroll=0.f;
        if(scroll>maxScroll)scroll=maxScroll;
    }
    void paintBand(Graphics& g,float w){
        SolidBrush bg(Color(255,241,243,241));
        g.FillRectangle(&bg,0.f,0.f,w,bandH);
        auto clip=g.Save();
        g.SetClip(RectF(tabsLeft,0.f,std::max(0.f,tabsRight-tabsLeft),bandH));
        for(int i=0;i<int(tabRects.size());++i){
            RectF r=tabRects[i];
            if(r.GetRight()<tabsLeft||r.X>tabsRight)continue;
            bool act=i==active;
            std::string id=std::to_string(i);
            if(act){
                SolidBrush white(Color(255,255,255,255));
                g.FillRectangle(&white,r.X,0.f,r.Width,bandH);
                SolidBrush accent(teal);
                g.FillRectangle(&accent,r.X,0.f,r.Width,2.f);
            }else{
                SolidBrush sep(Color(255,214,220,216));
                g.FillRectangle(&sep,r.GetRight()-1.f,11.f,1.f,bandH-22.f);
            }
            rawTxt(g,U(displayName(i)),RectF(r.X+13.f,0.f,r.Width-44.f,bandH),13.f,act?ink:muted,act);
            bool showClose=act||hover=="tab:"+id||hover=="close:"+id;
            float cx=r.GetRight()-16.f,cy=bandH*.5f;
            if(showClose){
                bool hot=hover=="close:"+id;
                Pen cross(hot?Color(255,190,60,60):(act?Color(255,70,82,82):muted),hot?1.7f:1.4f);
                g.DrawLine(&cross,cx-4.f,cy-4.f,cx+4.f,cy+4.f);
                g.DrawLine(&cross,cx+4.f,cy-4.f,cx-4.f,cy+4.f);
            }else if(tabs[i]->s.dirty){
                SolidBrush dot(Color(255,150,160,156));
                g.FillEllipse(&dot,cx-3.f,cy-3.f,6.f,6.f);
            }
        }
        g.Restore(clip);
        bool plusHot=hover=="plus";
        if(plusHot){SolidBrush hot(Color(255,228,234,229));g.FillRectangle(&hot,plusRect.X,plusRect.Y,plusRect.Width,plusRect.Height);}
        Pen plus(ink,1.5f);
        float px=plusRect.X+plusRect.Width*.5f,py=plusRect.Y+plusRect.Height*.5f;
        g.DrawLine(&plus,px-6.f,py,px+6.f,py);
        g.DrawLine(&plus,px,py-6.f,px,py+6.f);
        bool listHot=hover=="list";
        if(listHot){SolidBrush hot(Color(255,228,234,229));g.FillRectangle(&hot,listRect.X,listRect.Y,listRect.Width,listRect.Height);}
        Pen chevron(ink,1.5f);
        float lx=listRect.X+listRect.Width*.5f,ly=listRect.Y+listRect.Height*.5f;
        g.DrawLine(&chevron,lx-4.f,ly-2.f,lx,ly+2.f);
        g.DrawLine(&chevron,lx,ly+2.f,lx+4.f,ly-2.f);
        Pen bottom(Color(255,221,227,222));
        g.DrawLine(&bottom,0.f,bandH-.5f,w,bandH-.5f);
    }
    Hit hitBand(float x,float y){
        if(plusRect.Contains(x,y))return Hit("plus",-1);
        if(listRect.Contains(x,y))return Hit("list",-1);
        if(x<tabsLeft||x>tabsRight)return Hit();
        for(int i=0;i<int(tabRects.size());++i){
            RectF r=tabRects[i];
            if(r.X>tabsRight||r.GetRight()<tabsLeft)continue;
            if(!r.Contains(x,y))continue;
            if(x>r.GetRight()-26.f)return Hit("close",i);
            return Hit("tab",i);
        }
        return Hit();
    }
    void bandClick(const Hit& hit){
        if(hit.kind=="tab")selectTab(hit.index);
        else if(hit.kind=="close")closeTab(hit.index);
        else if(hit.kind=="plus")newTab();
        else if(hit.kind=="list")showTabList();
    }
    void showTabList(){
        HMENU menu=CreatePopupMenu();
        for(int i=0;i<int(tabs.size());++i){
            std::wstring label=displayName(i);
            if(tabs[i]->s.dirty)label+=L"  *";
            studio_i18n::appendMenu(menu,MF_STRING|(i==active?MF_CHECKED:0),100+i,label.c_str());
        }
        POINT point;GetCursorPos(&point);
        int id=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY|TPM_LEFTALIGN,point.x,point.y,0,hwnd,0);
        DestroyMenu(menu);
        if(id>=100)selectTab(id-100);
    }
    static LRESULT CALLBACK tipProc(HWND h,UINT m,WPARAM wp,LPARAM lp){
        if(m==WM_PAINT){
            PAINTSTRUCT ps;HDC dc=BeginPaint(h,&ps);
            RECT rc;GetClientRect(h,&rc);
            HBRUSH bg=CreateSolidBrush(RGB(250,251,248));FillRect(dc,&rc,bg);DeleteObject(bg);
            FrameRect(dc,&rc,(HBRUSH)GetStockObject(GRAY_BRUSH));
            wchar_t text[2048]={};studio_i18n::getWindowText(h,text,2048);
            SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(40,52,53));
            HFONT font=(HFONT)GetStockObject(DEFAULT_GUI_FONT);
            HGDIOBJ old=SelectObject(dc,font);
            std::wstring all(text);
            int y=4;
            size_t start=0;
            while(start<=all.size()){
                size_t end=all.find(L'\n',start);
                std::wstring line=all.substr(start,end==std::wstring::npos?std::wstring::npos:end-start);
                RECT inner={6,y,rc.right-6,y+18};
                ::DrawTextW(dc,line.c_str(),-1,&inner,DT_LEFT|DT_TOP|DT_SINGLELINE|DT_NOPREFIX|DT_END_ELLIPSIS);
                y+=18;
                if(end==std::wstring::npos)break;
                start=end+1;
            }
            SelectObject(dc,old);
            EndPaint(h,&ps);return 0;
        }
        if(m==WM_ERASEBKGND)return 1;
        return DefWindowProcW(h,m,wp,lp);
    }
    void ensureTip(){
        if(tip)return;
        static bool registered=false;
        if(!registered){
            WNDCLASSW wc={};wc.lpfnWndProc=TabHost::tipProc;wc.hInstance=inst;wc.hCursor=LoadCursorW(0,MAKEINTRESOURCEW(32512));wc.lpszClassName=L"MoleculeStudioTip";RegisterClassW(&wc);registered=true;
        }
        tip=studio_i18n::createWindowEx(WS_EX_TOPMOST|WS_EX_TOOLWINDOW,L"MoleculeStudioTip",L"",WS_POPUP,0,0,10,10,0,0,inst,0);
    }
    void showTip(){
        if(hover.rfind("tab:",0)!=0&&hover.rfind("close:",0)!=0)return;
        int index=atoi(hover.substr(hover.find(':')+1).c_str());
        std::wstring text=tooltipText(index);
        if(text.empty())return;
        ensureTip();
        if(!tip)return;
        ::SetWindowTextW(tip,text.c_str());
        HDC dc=GetDC(tip);
        int width=120,height=0;
        {HDC measure=CreateCompatibleDC(dc);HGDIOBJ old=SelectObject(measure,GetStockObject(DEFAULT_GUI_FONT));
         size_t start=0;int lines=0;
         while(start<=text.size()){
             size_t end=text.find(L'\n',start);
             std::wstring line=text.substr(start,end==std::wstring::npos?std::wstring::npos:end-start);
             SIZE size={};GetTextExtentPoint32W(measure,line.c_str(),int(line.size()),&size);
             if(size.cx+16>width)width=size.cx+16;
             ++lines;
             if(end==std::wstring::npos)break;
             start=end+1;
         }
         if(width>760)width=760;
         height=lines*18+8;
         SelectObject(measure,old);DeleteDC(measure);}
        ReleaseDC(tip,dc);
        POINT point;GetCursorPos(&point);
        SetWindowPos(tip,HWND_TOPMOST,point.x+12,point.y+22,width,height,SWP_NOACTIVATE|SWP_SHOWWINDOW);
        InvalidateRect(tip,0,TRUE);
    }
    void hideTip(){
        if(tip)ShowWindow(tip,SW_HIDE);
    }
    void selectTab(int index){
        if(index<0||index>=int(tabs.size()))return;
        active=index;
        for(int i=0;i<int(tabs.size());++i)ShowWindow(tabs[i]->h,i==index?SW_SHOW:SW_HIDE);
        g_currentApp=tabs[index];
        layout();
        layoutTabs(float(width)/scale);
        ensureVisible(index);
        layoutTabs(float(width)/scale);
        studio_i18n::setWindowText(hwnd,(L"Molecule Studio  \u00b7  "+tabTitle(index)).c_str());
        SetFocus(tabs[index]->focusSurface());
    }
    void selectIndex(int index){if(index>=0&&index<int(tabs.size()))selectTab(index);}
    void cycle(int delta){
        int n=int(tabs.size());
        if(n<2)return;
        selectTab(((active+delta)%n+n)%n);
    }
    void newTab(const std::string& path="",bool demo=false){
        if(!factory)return;
        App* app=factory(hwnd,path,demo);
        if(!app)return;
        app->host=this;
        tabs.push_back(app);
        scroll=1e9f;
        selectTab(int(tabs.size())-1);
    }
    void newWindow(){
        wchar_t exe[32768]={};GetModuleFileNameW(nullptr,exe,32768);
        std::wstring command=L"\""+std::wstring(exe)+L"\"";
        STARTUPINFOW si={};si.cb=sizeof(si);
        PROCESS_INFORMATION pi={};
        if(CreateProcessW(exe,&command[0],0,0,FALSE,0,0,0,&si,&pi)){
            if(pi.hProcess)CloseHandle(pi.hProcess);
            if(pi.hThread)CloseHandle(pi.hThread);
        }
    }
    void closeTab(int index){
        if(index<0||index>=int(tabs.size()))return;
        studio_i18n::sendMessage(tabs[index]->h,WM_CLOSE,0,0);
    }
    void closeTab(App* app){
        for(int i=0;i<int(tabs.size());++i)if(tabs[i]==app){closeTab(i);return;}
    }
    void tabDestroyed(App* app){
        size_t index=size_t(-1);
        for(size_t i=0;i<tabs.size();++i)if(tabs[i]==app){index=i;break;}
        if(index==size_t(-1))return;
        tabs.erase(tabs.begin()+index);
        retired.push_back(app);
        if(active>int(index))--active;
        if(active>=int(tabs.size()))active=int(tabs.size())-1;
        if(active<0)active=tabs.empty()?-1:0;
        if(!closing)PostMessageW(hwnd,WM_APP+50,0,0);
    }
    void titleChanged(App* app){
        if(active>=0&&active<int(tabs.size())&&tabs[active]==app)
            studio_i18n::setWindowText(hwnd,(L"Molecule Studio  \u00b7  "+tabTitle(active)).c_str());
    }
    void reap(){
        for(size_t i=0;i<retired.size();++i)delete retired[i];
        retired.clear();
    }
    void closeAll(){
        closing=true;
        while(!tabs.empty()){
            App* app=tabs.front();
            HWND target=app->h;
            studio_i18n::sendMessage(target,WM_CLOSE,0,0);
            if(IsWindow(target)){closing=false;return;}
            if(!tabs.empty()&&tabs.front()==app){tabs.erase(tabs.begin());retired.push_back(app);}
        }
        DestroyWindow(hwnd);
        closing=false;
    }
    void plusMenu(){
        POINT point;GetCursorPos(&point);
        HMENU menu=CreatePopupMenu();
        studio_i18n::appendMenu(menu,MF_STRING,1,L"\u65b0\u5efa\u6807\u7b7e\u9875\tCtrl+T");
        studio_i18n::appendMenu(menu,MF_STRING,2,L"\u65b0\u5efa\u7a97\u53e3\tCtrl+Shift+N");
        studio_i18n::appendMenu(menu,MF_SEPARATOR,0,0);
        studio_i18n::appendMenu(menu,MF_STRING,3,L"\u5173\u95ed\u5f53\u524d\u6807\u7b7e\u9875\tCtrl+W");
        int id=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY|TPM_LEFTALIGN,point.x,point.y,0,hwnd,0);
        DestroyMenu(menu);
        if(id==1)newTab();
        else if(id==2)newWindow();
        else if(id==3)closeTab(active);
    }
    void dropFiles(HDROP drop){
        UINT count=DragQueryFileW(drop,0xFFFFFFFF,0,0);
        for(UINT i=0;i<count;++i){
            wchar_t file[32768]={};
            if(DragQueryFileW(drop,i,file,32768))newTab(U(file));
        }
        DragFinish(drop);
    }
    void layout(){
        RECT rc;GetClientRect(hwnd,&rc);
        width=rc.right;height=rc.bottom;
        if(active>=0&&active<int(tabs.size()))MoveWindow(tabs[active]->h,0,0,std::max(1,width),std::max(1,height),FALSE);
    }
    static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM wp,LPARAM lp){
        TabHost* t=(TabHost*)GetWindowLongPtrW(h,GWLP_USERDATA);
        if(m==WM_NCCREATE){t=(TabHost*)((CREATESTRUCTW*)lp)->lpCreateParams;t->hwnd=h;SetWindowLongPtrW(h,GWLP_USERDATA,(LONG_PTR)t);}
        if(!t)return DefWindowProcW(h,m,wp,lp);
        switch(m){
        case WM_NCCALCSIZE:
            if(wp){
                if(IsZoomed(h)){MONITORINFO monitor={};monitor.cbSize=sizeof(monitor);if(GetMonitorInfoW(MonitorFromWindow(h,MONITOR_DEFAULTTONEAREST),&monitor))((NCCALCSIZE_PARAMS*)lp)->rgrc[0]=monitor.rcWork;}
                return 0;
            }
            break;
        case WM_NCHITTEST:{
            POINT point={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
            ScreenToClient(h,&point);
            RECT client;GetClientRect(h,&client);
            int edge=int(7*t->scale);
            if(!IsZoomed(h)){
                bool left=point.x<edge,right=point.x>=client.right-edge,top=point.y<edge,bottom=point.y>=client.bottom-edge;
                if(top&&left)return HTTOPLEFT;if(top&&right)return HTTOPRIGHT;
                if(bottom&&left)return HTBOTTOMLEFT;if(bottom&&right)return HTBOTTOMRIGHT;
                if(left)return HTLEFT;if(right)return HTRIGHT;if(top)return HTTOP;if(bottom)return HTBOTTOM;
            }
            // The editor shell passes its empty caption band through as
            // HTTRANSPARENT; report the band above the tab strip as a caption so
            // dragging moves the frame and double-click maximizes/restores it.
            if(point.y<40*t->scale)return HTCAPTION;
            return HTCLIENT;}
        case WM_ENTERSIZEMOVE:t->liveSizing=true;if(t->active>=0&&t->active<int(t->tabs.size()))studio_i18n::sendMessage(t->tabs[t->active]->h,WM_ENTERSIZEMOVE,0,0);return 0;
        case WM_EXITSIZEMOVE:KillTimer(h,71);t->resizePending=false;t->liveSizing=false;t->layout();if(t->active>=0&&t->active<int(t->tabs.size()))studio_i18n::sendMessage(t->tabs[t->active]->h,WM_EXITSIZEMOVE,0,0);InvalidateRect(h,0,FALSE);return 0;
        case WM_SIZE:if(t->liveSizing){if(!t->resizePending){t->resizePending=true;SetTimer(h,71,33,0);}}else{t->layout();InvalidateRect(h,0,FALSE);}return 0;
        case WM_TIMER:if(wp==71){KillTimer(h,71);t->resizePending=false;t->layout();if(t->active>=0&&t->active<int(t->tabs.size()))studio_i18n::sendMessage(t->tabs[t->active]->h,WM_TIMER,71,0);return 0;}break;
        case WM_ERASEBKGND:return 1;
        case WM_PAINT:{
            PAINTSTRUCT ps;HDC dc=BeginPaint(h,&ps);
            RECT rc;GetClientRect(h,&rc);
            {Graphics g(dc);g.SetPageUnit(UnitPixel);SolidBrush bg(paper);g.FillRectangle(&bg,0.f,0.f,float(rc.right),float(rc.bottom));}
            EndPaint(h,&ps);
            return 0;}
        case WM_DPICHANGED:{
            t->scale=float(HIWORD(wp))/96.f;
            RECT* r=(RECT*)lp;
            SetWindowPos(h,0,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER);
            t->layout();return 0;}
        case WM_GETMINMAXINFO:{
            MINMAXINFO* info=(MINMAXINFO*)lp;
            MONITORINFO monitor={};monitor.cbSize=sizeof(monitor);
            if(GetMonitorInfoW(MonitorFromWindow(h,MONITOR_DEFAULTTONEAREST),&monitor)){
                info->ptMaxPosition.x=monitor.rcWork.left-monitor.rcMonitor.left;
                info->ptMaxPosition.y=monitor.rcWork.top-monitor.rcMonitor.top;
                info->ptMaxSize.x=monitor.rcWork.right-monitor.rcWork.left;
                info->ptMaxSize.y=monitor.rcWork.bottom-monitor.rcWork.top;
            }
            // The scaled minimum (880x560 at 100%) can exceed a small high-DPI
            // work area (1760x1120 here at 200% on a 1663x921 screen), and a
            // minimum larger than the maximum tracking size freezes every edge
            // drag.  Keep a quarter of the work area shrinkable instead.
            int minW=int(880*t->scale),minH=int(560*t->scale);
            if(info->ptMaxSize.x>0)minW=std::min(minW,int(info->ptMaxSize.x*3/4));
            if(info->ptMaxSize.y>0)minH=std::min(minH,int(info->ptMaxSize.y*3/4));
            info->ptMinTrackSize.x=minW;
            info->ptMinTrackSize.y=minH;
            return 0;}
        case WM_SETFOCUS:
            if(t->active>=0&&t->active<int(t->tabs.size()))SetFocus(t->tabs[t->active]->focusSurface());
            return 0;
        case WM_KEYDOWN:
            if(t->active>=0&&t->active<int(t->tabs.size()))studio_i18n::sendMessage(t->tabs[t->active]->focusSurface(),WM_KEYDOWN,wp,lp);
            return 0;
        case WM_DROPFILES:t->dropFiles((HDROP)wp);return 0;
        case WM_CLOSE:t->closeAll();return 0;
        case WM_DESTROY:PostQuitMessage(0);return 0;
        case WM_APP+50:
            t->reap();
            if(t->tabs.empty()){PostMessageW(h,WM_APP+51,0,0);return 0;}
            t->selectTab(t->active);
            return 0;
        case WM_APP+51:DestroyWindow(h);return 0;
        }
        return DefWindowProcW(h,m,wp,lp);
    }
    void create(){
        static bool registered=false;
        if(!registered){
            WNDCLASSW wc={};
            wc.lpfnWndProc=TabHost::proc;
            wc.hInstance=inst;
            wc.hCursor=LoadCursorW(0,MAKEINTRESOURCEW(32512));
            wc.hIcon=LoadIconW(inst,MAKEINTRESOURCEW(1));
            wc.lpszClassName=L"MoleculeStudioTabs";
            RegisterClassW(&wc);
            registered=true;
        }
        hwnd=studio_i18n::createWindowEx(0,L"MoleculeStudioTabs",L"Molecule Studio 0.95",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,1560,950,0,0,inst,this);
        scale=GetDpiForWindow(hwnd)/96.f;
        DragAcceptFiles(hwnd,TRUE);
    }
};
