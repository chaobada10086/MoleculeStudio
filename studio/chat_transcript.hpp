#pragma once
// The transcript keeps the last rendered viewport in a DIB section and only re-measures the
// messages when their count or the available width changes. Repainting used to allocate a
// full-size 32bpp GDI+ surface, re-measure and re-rasterise every message and then alpha-blend
// the whole viewport on each WM_PAINT, so a window move/uncover/resize cost hundreds of ms.
struct ChatTranscript {
    struct Message {std::wstring role,text;bool localizable=false;};
    struct Item {RectF bounds,body;size_t index;};
    HWND hwnd=nullptr;float scale=1;int selected=-1;int scroll=0,total=0;std::vector<Message> messages;std::vector<Item> items;
    HDC mem=nullptr;HBITMAP dib=nullptr;HGDIOBJ old=nullptr;int cacheW=0,cacheH=0,cacheScroll=-1;float cacheScale=0;bool cacheValid=false;
    int displayLanguage=-1;int layoutWidth=-1;size_t layoutCount=0;float layoutScale=0;bool liveResize=false;
    ~ChatTranscript(){releaseCache();}
    ChatTranscript()=default;
    ChatTranscript(const ChatTranscript&)=delete;ChatTranscript& operator=(const ChatTranscript&)=delete;
    void releaseCache(){if(mem){if(old)SelectObject(mem,old);DeleteDC(mem);}if(dib)DeleteObject(dib);mem=nullptr;dib=nullptr;old=nullptr;cacheW=cacheH=0;cacheValid=false;}
    void ensureCache(int w,int h){
        if(mem&&cacheW==w&&cacheH==h)return;releaseCache();
        BITMAPINFO info={};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=w;info.bmiHeader.biHeight=-h;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
        HDC screen=GetDC(nullptr);mem=CreateCompatibleDC(screen);ReleaseDC(nullptr,screen);
        void* bits=nullptr;dib=CreateDIBSection(mem,&info,DIB_RGB_COLORS,&bits,nullptr,0);
        if(!dib){if(mem)DeleteDC(mem);mem=nullptr;return;}
        old=SelectObject(mem,dib);cacheW=w;cacheH=h;
    }
    void clear(){messages.clear();scroll=0;reflow(true);}
    void append(const std::string& raw){
        std::string body=raw;std::wstring role=L"状态";
        if(body.compare(0,strlen("你："),"你：")==0){role=L"你";body.erase(0,strlen("你："));}
        else if(body.compare(0,strlen("AI："),"AI：")==0){role=L"AI 助手";body.erase(0,strlen("AI："));}
        else if(messages.empty())role=L"AI 助手";
        Message next;next.role=role;next.text=W(body);next.localizable=raw.compare(0,strlen("你："),"你：")!=0&&raw.compare(0,strlen("AI："),"AI：")!=0;messages.push_back(next);if(selected>=int(messages.size()))selected=-1;if(messages.size()>300)messages.erase(messages.begin());reflow(true);
    }
    // Measure once per (width, scale, message count); scrolling and pure height changes reuse it.
    void measure(){
        if(!IsWindow(hwnd))return;RECT r;GetClientRect(hwnd,&r);Bitmap measureSurface(1,1,PixelFormat32bppARGB);Graphics g(&measureSurface);g.ScaleTransform(scale,scale);
        float width=r.right/scale,y=12;items.clear();Font font(L"Microsoft YaHei UI",13,FontStyleRegular,UnitPixel);StringFormat format;format.SetFormatFlags(StringFormatFlagsLineLimit);format.SetTrimming(StringTrimmingNone);
        for(size_t i=0;i<messages.size();i++){
            const auto& message=messages[i];auto shown=message.localizable?studio_i18n::tr(message.text):message.text;bool user=message.role==L"你";float bubble=std::max(110.f,width-44),textWidth=bubble-24;
            RectF measured;g.MeasureString(shown.c_str(),int(shown.size()),&font,RectF(0,0,textWidth,1000000),&format,&measured);
            float height=std::max(20.f,measured.Height)+46;float x=user?width-12-bubble:12;
            items.push_back({RectF(x,y,bubble,height),RectF(x+12,y+31,textWidth,height-40),i});y+=height+12;
        }
        total=int((y+4)*scale);layoutWidth=r.right;layoutCount=messages.size();layoutScale=scale;
    }
    void reflow(bool bottom=false){
        if(!IsWindow(hwnd))return;RECT r;GetClientRect(hwnd,&r);
        // During an interactive resize the width changes on every WM_SIZE; re-wrapping the whole
        // conversation there is what made dragging the window edge crawl. Keep the last layout and
        // stretch the cached frame until the drag ends.
        if(!liveResize&&(layoutWidth!=r.right||layoutCount!=messages.size()||layoutScale!=scale))measure();
        int maximum=std::max(0,total-int(r.bottom));scroll=bottom?maximum:std::max(0,std::min(scroll,maximum));
        SCROLLINFO info={};info.cbSize=sizeof(info);info.fMask=SIF_RANGE|SIF_PAGE|SIF_POS;info.nMin=0;info.nMax=std::max(0,total-1);info.nPage=r.bottom;info.nPos=scroll;SetScrollInfo(hwnd,SB_VERT,&info,TRUE);
        cacheValid=false;InvalidateRect(hwnd,nullptr,FALSE);
    }
    void render(const RECT& r){
        ensureCache(r.right,r.bottom);if(!mem)return;
        {Graphics g(mem);g.Clear(studio_ui::background());g.ScaleTransform(scale,scale);g.TranslateTransform(0,-scroll/scale);g.SetSmoothingMode(SmoothingModeAntiAlias);g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
        Font font(L"Microsoft YaHei UI",13,FontStyleRegular,UnitPixel);StringFormat format;format.SetFormatFlags(StringFormatFlagsLineLimit);
        for(const auto& item:items){if(item.bounds.GetBottom()*scale<scroll||item.bounds.Y*scale>scroll+r.bottom)continue;const auto& message=messages[item.index];bool user=message.role==L"你",status=message.role==L"状态";
            bool chosen=int(item.index)==selected;studio_ui::round(g,item.bounds,10,chosen?Color(255,224,244,240):user?Color(255,220,239,234):status?Color(255,235,240,242):Color(255,255,255,255));
            studio_ui::label(g,message.role,RectF(item.bounds.X+12,item.bounds.Y+5,item.bounds.Width-24,23),11,user?studio_ui::teal():studio_ui::muted(),true);
            auto shown=message.localizable?studio_i18n::tr(message.text):message.text;SolidBrush text(status?studio_ui::muted():studio_ui::ink());g.DrawString(shown.c_str(),int(shown.size()),&font,item.body,&format,&text);
        }}
        GdiFlush();cacheScroll=scroll;cacheScale=scale;cacheValid=true;
    }
    void paint(){
        if(displayLanguage!=studio_i18n::current()){displayLanguage=studio_i18n::current();layoutCount=0;reflow();}
        PAINTSTRUCT ps;HDC dc=BeginPaint(hwnd,&ps);RECT r;GetClientRect(hwnd,&r);if(r.right<=0||r.bottom<=0){EndPaint(hwnd,&ps);return;}
        if(liveResize&&mem&&cacheW>0&&cacheH>0){
            SetStretchBltMode(dc,COLORONCOLOR);StretchBlt(dc,0,0,r.right,r.bottom,mem,0,0,cacheW,cacheH,SRCCOPY);EndPaint(hwnd,&ps);return;
        }
        if(!cacheValid||!mem||cacheW!=r.right||cacheH!=r.bottom||cacheScroll!=scroll||cacheScale!=scale)render(r);
        RECT d=ps.rcPaint;int w=d.right-d.left,h=d.bottom-d.top;
        if(mem&&w>0&&h>0)BitBlt(dc,d.left,d.top,w,h,mem,d.left,d.top,SRCCOPY);
        EndPaint(hwnd,&ps);
    }
    int messageAt(int x,int y) const{PointF p(x/scale,(y+scroll)/scale);for(const auto& item:items)if(item.bounds.Contains(p))return int(item.index);return -1;}

    void copyText(const std::wstring& value){if(value.empty()||!OpenClipboard(hwnd))return;HGLOBAL memory=GlobalAlloc(GMEM_MOVEABLE,(value.size()+1)*sizeof(wchar_t));if(memory){auto out=GlobalLock(memory);if(out){memcpy(out,value.c_str(),(value.size()+1)*sizeof(wchar_t));GlobalUnlock(memory);if(!EmptyClipboard()||!SetClipboardData(CF_UNICODETEXT,memory))GlobalFree(memory);}else GlobalFree(memory);}CloseClipboard();}

    void copyAll(){std::wstring all;for(const auto& message:messages){all+=message.role+L"："+message.text+L"\r\n\r\n";}copyText(all);}

    void copyMessage(int x,int y){int index=messageAt(x,y);if(index>=0)selected=index;HMENU menu=CreatePopupMenu();studio_i18n::appendMenu(menu,MF_STRING,1,L"复制这条消息");studio_i18n::appendMenu(menu,MF_STRING,2,L"复制全部对话");POINT at={x,y};ClientToScreen(hwnd,&at);int command=TrackPopupMenu(menu,TPM_RETURNCMD,at.x,at.y,0,hwnd,nullptr);DestroyMenu(menu);if(command==1&&index>=0)copyText(messages[size_t(index)].text);else if(command==2)copyAll();InvalidateRect(hwnd,nullptr,FALSE);}

    static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){auto self=(ChatTranscript*)GetWindowLongPtrW(h,GWLP_USERDATA);if(m==WM_NCCREATE){self=(ChatTranscript*)((CREATESTRUCTW*)l)->lpCreateParams;self->hwnd=h;SetWindowLongPtrW(h,GWLP_USERDATA,(LONG_PTR)self);}if(!self)return DefWindowProcW(h,m,w,l);
        switch(m){case WM_SIZE:self->reflow();return 0;case WM_PAINT:self->paint();return 0;case WM_ERASEBKGND:return 1;
        case WM_LBUTTONDOWN:self->selected=self->messageAt(GET_X_LPARAM(l),GET_Y_LPARAM(l));SetFocus(h);InvalidateRect(h,nullptr,FALSE);return 0;

        case WM_KEYDOWN:if((w=='C'||w=='c')&&(GetKeyState(VK_CONTROL)&0x8000)){if(self->selected>=0&&size_t(self->selected)<self->messages.size())self->copyText(self->messages[size_t(self->selected)].text);else self->copyAll();return 0;}if(w==VK_ESCAPE){self->selected=-1;InvalidateRect(h,nullptr,FALSE);return 0;}break;

        case WM_MOUSEWHEEL:self->scroll-=GET_WHEEL_DELTA_WPARAM(w)*3/2;self->reflow();return 0;
        case WM_VSCROLL:{SCROLLINFO info={};info.cbSize=sizeof(info);info.fMask=SIF_TRACKPOS;GetScrollInfo(h,SB_VERT,&info);int code=LOWORD(w);if(code==SB_THUMBTRACK)self->scroll=info.nTrackPos;else if(code==SB_LINEUP)self->scroll-=30;else if(code==SB_LINEDOWN)self->scroll+=30;else if(code==SB_PAGEUP)self->scroll-=180;else if(code==SB_PAGEDOWN)self->scroll+=180;self->reflow();return 0;}
        case WM_RBUTTONUP:self->copyMessage(GET_X_LPARAM(l),GET_Y_LPARAM(l));return 0;case WM_DESTROY:self->releaseCache();self->hwnd=nullptr;return 0;
        }return DefWindowProcW(h,m,w,l);
    }
    void create(HWND parent,int id){WNDCLASSW wc={};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.hCursor=LoadCursorW(nullptr,MAKEINTRESOURCEW(32512));wc.lpszClassName=L"MoleculeStudioChatTranscript";RegisterClassW(&wc);studio_i18n::createWindowEx(0,wc.lpszClassName,L"对话消息",WS_CHILD|WS_VISIBLE|WS_VSCROLL|WS_TABSTOP,0,0,1,1,parent,(HMENU)(INT_PTR)id,wc.hInstance,this);}
};
