#pragma once
#include "app_paths.hpp"
#include "next/core/contracts.hpp"
#include <functional>
#include <map>
#include <sstream>

// A shared, persisted palette; the selected drawing element belongs to each tab.
namespace elementpalette {
struct Settings {
    std::vector<int> pins{6,1,8,7,15,9,17,35,53,14,5};
    std::map<int,uint32_t> colors;
    static std::wstring path(){return apppaths::dataFile(L"interface.ini");}
    Settings(){
        wchar_t raw[2048]={};GetPrivateProfileStringW(L"ElementPalette",L"Pins",L"",raw,2048,path().c_str());
        if(*raw){std::wistringstream in(raw);std::vector<int> values;int z;while(in>>z)if(z>=1&&z<=118&&std::find(values.begin(),values.end(),z)==values.end())values.push_back(z);pins=values;}
        for(int z=1;z<=118;++z){auto key=L"Color"+std::to_wstring(z);GetPrivateProfileStringW(L"ElementPalette",key.c_str(),L"",raw,2048,path().c_str());if(*raw){wchar_t* end=nullptr;auto color=wcstoul(raw,&end,16);if(end&&!*end&&color<=0xFFFFFF)colors[z]=(uint32_t(color)<<8)|255;}}
    }
    bool save(){
        std::wstring value=L"0";for(int z:pins)value+=L" "+std::to_wstring(z);
        bool ok=WritePrivateProfileStringW(L"ElementPalette",L"Pins",value.c_str(),path().c_str())!=FALSE;
        for(int z=1;z<=118;++z){auto key=L"Color"+std::to_wstring(z);auto found=colors.find(z);wchar_t hex[16];swprintf(hex,16,L"%06X",found==colors.end()?0:unsigned(found->second>>8));ok=(WritePrivateProfileStringW(L"ElementPalette",key.c_str(),found==colors.end()?nullptr:hex,path().c_str())!=FALSE)&&ok;}
        return ok;
    }
    uint32_t color(int z)const{auto i=colors.find(z);return i==colors.end()?0x000000FFu:i->second;}
    bool pinned(int z)const{return std::find(pins.begin(),pins.end(),z)!=pins.end();}
};
inline Settings& settings(){static Settings value;return value;}
inline Gdiplus::Color rgba(uint32_t color){return Gdiplus::Color(BYTE(color),BYTE(color>>24),BYTE(color>>16),BYTE(color>>8));}
struct Palette {
    HWND bar=nullptr,table=nullptr,owner=nullptr;float scale=1,tableScale=1;int active=0,selected=6,hover=-1,offset=0;
    std::function<void(int)> choose;
    std::function<void(int)> recolor;
    static constexpr int width=36;
    static const char* symbol(int z){return r62::core::elementSymbol(z);}
    static POINT cell(int z){
        if(z==1)return {0,0};if(z==2)return {17,0};
        if(z<=10)return {z<=4?z-3:z+7,1};
        if(z<=18)return {z<=12?z-11:z-1,2};
        if(z<=36)return {z-19,3};if(z<=54)return {z-37,4};
        if(z<=56)return {z-55,5};if(z<=71)return {z-55,8};
        if(z<=86)return {z-69,5};if(z<=88)return {z-87,6};
        if(z<=103)return {z-87,9};return {z-101,6};
    }
    int hit(float x,float y)const{for(int z=1;z<=118;++z){auto p=cell(z);if(Gdiplus::RectF(22.f+p.x*42.f,76.f+p.y*42.f,39,39).Contains(x,y))return z;}return 0;}
    static void label(Gdiplus::Graphics& g,const std::string& value,Gdiplus::RectF box,float size,Gdiplus::Color color,bool bold=false,bool translate=true){
        Gdiplus::Font font(L"Microsoft YaHei UI",size,bold?Gdiplus::FontStyleBold:Gdiplus::FontStyleRegular,Gdiplus::UnitPixel);Gdiplus::SolidBrush brush(color);Gdiplus::StringFormat fmt;fmt.SetAlignment(Gdiplus::StringAlignmentCenter);fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);int n=MultiByteToWideChar(CP_UTF8,0,value.c_str(),-1,nullptr,0);std::wstring wide(n,L'\0');MultiByteToWideChar(CP_UTF8,0,value.c_str(),-1,&wide[0],n);wide.resize(n-1);auto text=translate?studio_i18n::tr(wide):wide;if(translate&&studio_i18n::rtl())fmt.SetFormatFlags(Gdiplus::StringFormatFlagsDirectionRightToLeft);Gdiplus::RectF measured;g.MeasureString(text.c_str(),int(text.size()),&font,Gdiplus::PointF(0,0),&measured);if(measured.Width>box.Width-6){float fit=std::max(7.5f,size*(box.Width-6)/measured.Width);Gdiplus::Font fitted(L"Microsoft YaHei UI",fit,bold?Gdiplus::FontStyleBold:Gdiplus::FontStyleRegular,Gdiplus::UnitPixel);g.DrawString(text.c_str(),int(text.size()),&fitted,box,&fmt,&brush);return;}g.DrawString(text.c_str(),int(text.size()),&font,box,&fmt,&brush);
    }
    static void box(Gdiplus::Graphics& g,Gdiplus::RectF r,Gdiplus::Color color){Gdiplus::SolidBrush b(color);g.FillRectangle(&b,r);}
    void refresh(){if(bar)InvalidateRect(bar,nullptr,FALSE);if(table)InvalidateRect(table,nullptr,FALSE);}
    void save(){if(!settings().save())studio_i18n::messageBox(table?table:owner,L"无法保存元素栏设置，请检查设置文件是否可写。",L"MoleculeStudio",MB_OK|MB_ICONWARNING);refresh();}
    int capacity()const{RECT rc;GetClientRect(bar,&rc);return std::max(1,int((rc.bottom/scale-66)/30));}
    void paint(HWND hwnd,HDC dc,bool full){
        using namespace Gdiplus;RECT rc;GetClientRect(hwnd,&rc);Bitmap buffer(std::max(1,int(rc.right)),std::max(1,int(rc.bottom)),PixelFormat32bppARGB);Graphics g(&buffer);g.SetPageUnit(UnitPixel);float dpi=full?tableScale:scale;g.ScaleTransform(dpi,dpi);g.SetSmoothingMode(SmoothingModeAntiAlias);g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);auto skin=studio_ui::skinColors();g.Clear(full?Color(255,249,250,252):Color(255,247,249,252));
        if(!full){
            Pen edge(skin.border,1);g.DrawLine(&edge,0.f,0.f,0.f,rc.bottom/dpi);
            RectF icon(4,5,28,28);box(g,icon,hover==0?skin.selected:skin.hover);
            for(int row=0;row<3;++row)for(int col=0;col<5;++col)if(row==2||col==0||col==4)box(g,RectF(9.f+col*4,11.f+row*5,3,4),skin.accent);
            int cap=capacity();offset=std::max(0,std::min(offset,std::max(0,int(settings().pins.size())-cap)));
            for(int i=0;i<cap&&offset+i<int(settings().pins.size());++i){int z=settings().pins[offset+i];RectF r(4,40.f+i*30,28,28);if(active==z||hover==i+1)box(g,r,active==z?skin.selected:skin.hover);label(g,symbol(z),r,14,rgba(settings().color(z)),active==z,false);if(settings().colors.count(z))box(g,RectF(10,r.GetBottom()-3,16,2),rgba(settings().color(z)));}
            if(int(settings().pins.size())>cap){label(g,"‹",RectF(2,rc.bottom/dpi-25,16,23),17,skin.accent,false,false);label(g,"›",RectF(18,rc.bottom/dpi-25,16,23),17,skin.accent,false,false);}
        }else{
            label(g,"元素周期表",RectF(22,14,756,26),20,skin.strong,true);
            label(g,"选择元素 · 固定常用元素到侧栏 · 为元素保存颜色",RectF(22,43,756,22),11,Color(255,102,112,128));
            for(int z=1;z<=118;++z){auto p=cell(z);RectF r(22.f+p.x*42,76.f+p.y*42,39,39);Color fill=Color(255,238,243,250);if(p.y>=8)fill=Color(255,244,239,249);else if(p.x>=12)fill=Color(255,233,246,240);if(z==selected)fill=skin.selected;box(g,r,fill);Pen edge(z==selected?skin.accent:Color(255,219,225,232),z==selected?2.f:.6f);g.DrawRectangle(&edge,r);label(g,std::to_string(z),RectF(r.X+2,r.Y+1,25,11),7.5f,Color(255,109,120,135),false,false);label(g,symbol(z),RectF(r.X,r.Y+9,39,26),15,rgba(settings().color(z)),true,false);if(settings().pinned(z))box(g,RectF(r.GetRight()-6,r.Y+4,3,3),skin.accent);}
            label(g,"57–71",RectF(106,286,39,39),9,skin.strong,false,false);label(g,"89–103",RectF(106,328,39,39),9,skin.strong,false,false);
            label(g,std::string(symbol(selected))+"  ·  "+std::to_string(selected),RectF(22,508,112,36),17,rgba(settings().color(selected)),true,false);
            const char* names[]={"使用元素",settings().pinned(selected)?"取消固定":"固定到侧栏","元素颜色…","恢复默认颜色"};
            for(int i=0;i<4;++i){RectF r(148.f+i*158,510,148,32);box(g,r,i==0?skin.accent:Color::White);Pen border(skin.border,1);g.DrawRectangle(&border,r);label(g,names[i],r,12,i==0?Color::White:skin.strong,i==0);}
            label(g,"点击元素后选择操作；Enter 使用，Space 固定 / 取消固定，Esc 关闭",RectF(22,555,756,25),11,Color(255,109,120,135));
        }
        // Copy physical pixels 1:1. The natural-size overload applies image DPI again on a scaled display.
        Graphics dest(dc);
        dest.SetPageUnit(UnitPixel);
        dest.DrawImage(&buffer,Rect(0,0,rc.right,rc.bottom),0,0,rc.right,rc.bottom,UnitPixel);
    }
    void action(int command){
        if(command==0){active=selected;if(choose)choose(selected);ShowWindow(table,SW_HIDE);SetFocus(owner);}
        if(command==1){auto& pins=settings().pins;auto it=std::find(pins.begin(),pins.end(),selected);if(it==pins.end())pins.push_back(selected);else pins.erase(it);save();}
        if(command==2){static COLORREF custom[16]={};auto color=settings().color(selected);CHOOSECOLORW cc={};cc.lStructSize=sizeof(cc);cc.hwndOwner=table;cc.rgbResult=RGB(color>>24,(color>>16)&255,(color>>8)&255);cc.lpCustColors=custom;cc.Flags=CC_RGBINIT|CC_FULLOPEN;if(studio_i18n::chooseColor(&cc)){settings().colors[selected]=(uint32_t(GetRValue(cc.rgbResult))<<24)|(uint32_t(GetGValue(cc.rgbResult))<<16)|(uint32_t(GetBValue(cc.rgbResult))<<8)|255;save();if(recolor)recolor(selected);}}
        if(command==3){settings().colors.erase(selected);save();if(recolor)recolor(selected);}
    }
    static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){
        Palette* self=(Palette*)GetWindowLongPtrW(h,GWLP_USERDATA);if(m==WM_NCCREATE){self=(Palette*)((CREATESTRUCTW*)l)->lpCreateParams;SetWindowLongPtrW(h,GWLP_USERDATA,(LONG_PTR)self);}if(!self)return DefWindowProcW(h,m,w,l);bool full=h==self->table;float dpi=full?self->tableScale:self->scale;
        switch(m){
        case WM_ERASEBKGND:return 1;
        case WM_PAINT:{PAINTSTRUCT ps;auto dc=BeginPaint(h,&ps);self->paint(h,dc,full);EndPaint(h,&ps);return 0;}
        case WM_CLOSE:ShowWindow(h,SW_HIDE);return 0;
        case WM_MOUSEMOVE:if(!full){int y=int(GET_Y_LPARAM(l)/dpi);self->hover=y<36?0:y>=40?(y-40)/30+1:-1;TRACKMOUSEEVENT t={sizeof(t),TME_LEAVE,h,0};TrackMouseEvent(&t);self->refresh();}return 0;
        case WM_MOUSELEAVE:self->hover=-1;self->refresh();return 0;
        case WM_MOUSEWHEEL:if(!full){self->offset+=GET_WHEEL_DELTA_WPARAM(w)>0?-3:3;self->refresh();}return 0;
        case WM_LBUTTONDOWN:{float x=GET_X_LPARAM(l)/dpi,y=GET_Y_LPARAM(l)/dpi;
            if(full){if(int z=self->hit(x,y)){self->selected=z;self->refresh();}else if(y>=510&&y<=542&&x>=148&&x<780)self->action(int((x-148)/158));}
            else if(y<36)self->show();else {RECT rc;GetClientRect(h,&rc);if(y>rc.bottom/dpi-25&&int(settings().pins.size())>self->capacity()){self->offset+=x<18?-self->capacity():self->capacity();self->refresh();}else if(y>=40){int index=self->offset+int((y-40)/30);if(index<int(settings().pins.size())&&int((y-40)/30)<self->capacity()){self->selected=self->active=settings().pins[index];if(self->choose)self->choose(self->active);self->refresh();}}}return 0;}
        case WM_RBUTTONUP:if(!full){int row=int((GET_Y_LPARAM(l)/dpi-40)/30);if(GET_Y_LPARAM(l)/dpi>=40&&row>=0&&row<self->capacity()&&row+self->offset<int(settings().pins.size()))self->selected=settings().pins[row+self->offset];self->show();}return 0;
        case WM_KEYDOWN:if(w==VK_ESCAPE){if(full)ShowWindow(h,SW_HIDE);SetFocus(self->owner);return 0;}if(full){if(w==VK_RETURN)self->action(0);else if(w==VK_SPACE)self->action(1);else if(w==VK_LEFT||w==VK_UP)self->selected=std::max(1,self->selected-1);else if(w==VK_RIGHT||w==VK_DOWN)self->selected=std::min(118,self->selected+1);self->refresh();return 0;}break;
        }return DefWindowProcW(h,m,w,l);
    }
    static void registerClass(){static bool registered=false;if(registered)return;WNDCLASSW wc={};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.hCursor=LoadCursorW(nullptr,MAKEINTRESOURCEW(32512));wc.lpszClassName=L"MoleculeStudioElementPalette";RegisterClassW(&wc);registered=true;}
    void attach(HWND parent){owner=parent;registerClass();bar=studio_i18n::createWindowEx(0,L"MoleculeStudioElementPalette",L"元素快捷栏",WS_CHILD|WS_VISIBLE,0,0,36,300,parent,nullptr,GetModuleHandleW(nullptr),this);}
    void show(){
        if(!table){MONITORINFO monitor={sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(owner,MONITOR_DEFAULTTONEAREST),&monitor);tableScale=std::min(GetDpiForWindow(owner)/96.f,std::min((monitor.rcWork.right-monitor.rcWork.left-40)/816.f,(monitor.rcWork.bottom-monitor.rcWork.top-80)/600.f));registerClass();table=studio_i18n::createWindowEx(WS_EX_TOOLWINDOW,L"MoleculeStudioElementPalette",L"元素周期表",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,int(monitor.rcWork.left+30),int(monitor.rcWork.top+30),int(804*tableScale),int(600*tableScale)+GetSystemMetrics(SM_CYCAPTION),owner,nullptr,GetModuleHandleW(nullptr),this);}
        ShowWindow(table,SW_SHOW);SetForegroundWindow(table);SetFocus(table);refresh();
    }
};
}
