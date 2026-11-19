#ifndef STUDIO_PAGE_DIALOG_HPP
#define STUDIO_PAGE_DIALOG_HPP
#include "interop.hpp"
namespace studioio {
struct PageDialog {
    Page page;bool updating=false;HFONT font=0;double dpi=1;
    static double mm(double pt){return pt*25.4/72.;}
    static double pt(double mm){return mm*72./25.4;}
    void values(HWND h){updating=true;wchar_t b[48];swprintf(b,48,L"%.2f",mm(page.width));studio_i18n::setDlgItemText(h,203,b);swprintf(b,48,L"%.2f",mm(page.height));studio_i18n::setDlgItemText(h,204,b);SetDlgItemInt(h,205,page.across,FALSE);SetDlgItemInt(h,206,page.down,FALSE);updating=false;InvalidateRect(h,0,TRUE);}
    bool read(HWND h){wchar_t b[64];studio_i18n::getDlgItemText(h,203,b,64);wchar_t* end;double w=wcstod(b,&end);if(*end)return false;studio_i18n::getDlgItemText(h,204,b,64);double v=wcstod(b,&end);if(*end)return false;Page p=page;p.width=pt(w);p.height=pt(v);BOOL ok1=FALSE,ok2=FALSE;p.across=GetDlgItemInt(h,205,&ok1,FALSE);p.down=GetDlgItemInt(h,206,&ok2,FALSE);if(!ok1||!ok2||!p.valid())return false;page=p;return true;}
    static INT_PTR CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){auto d=reinterpret_cast<PageDialog*>(GetWindowLongPtrW(h,DWLP_USER));
        if(m==WM_INITDIALOG){d=reinterpret_cast<PageDialog*>(l);SetWindowLongPtrW(h,DWLP_USER,(LONG_PTR)d);studio_i18n::setWindowText(h,L"页面设置 — 纸张与页面布局");d->dpi=GetDpiForWindow(h)/96.;auto px=[&](int n){return int(n*d->dpi);};
            SetWindowPos(h,0,0,0,px(640),px(420),SWP_NOMOVE|SWP_NOZORDER);
            RECT owner,rect;GetWindowRect(GetParent(h),&owner);GetWindowRect(h,&rect);SetWindowPos(h,0,owner.left+(owner.right-owner.left-(rect.right-rect.left))/2,owner.top+(owner.bottom-owner.top-(rect.bottom-rect.top))/2,0,0,SWP_NOSIZE|SWP_NOZORDER);
            d->font=CreateFontW(-px(14),0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");
            auto control=[&](const wchar_t* cls,const wchar_t* title,int id,int x,int y,int width,int height,DWORD style=0){HWND c=studio_i18n::createWindowEx(0,cls,title,WS_CHILD|WS_VISIBLE|style,px(x),px(y),px(width),px(height),h,(HMENU)(INT_PTR)id,0,0);studio_i18n::sendMessage(c,WM_SETFONT,(WPARAM)d->font,TRUE);return c;};
            control(L"STATIC",L"纸张",0,22,18,280,24);control(L"STATIC",L"尺寸",0,24,56,80,22);
            HWND paper=control(L"COMBOBOX",L"",201,110,52,210,240,CBS_DROPDOWNLIST|WS_TABSTOP|WS_VSCROLL);
            for(auto s:{L"A4",L"A3",L"A5",L"B5 (ISO)",L"US Letter",L"US Legal",L"自定义"})studio_i18n::sendMessage(paper,CB_ADDSTRING,0,(LPARAM)s);
            int index=6;double small=mm(std::min(d->page.width,d->page.height)),large=mm(std::max(d->page.width,d->page.height));double sizes[][2]={{210,297},{297,420},{148,210},{176,250},{215.9,279.4},{215.9,355.6}};for(int i=0;i<6;i++)if(std::abs(small-sizes[i][0])<.1&&std::abs(large-sizes[i][1])<.1)index=i;studio_i18n::sendMessage(paper,CB_SETCURSEL,index,0);
            control(L"STATIC",L"方向",0,24,96,80,22);HWND orient=control(L"COMBOBOX",L"",202,110,92,210,120,CBS_DROPDOWNLIST|WS_TABSTOP);studio_i18n::sendMessage(orient,CB_ADDSTRING,0,(LPARAM)L"纵向");studio_i18n::sendMessage(orient,CB_ADDSTRING,0,(LPARAM)L"横向");studio_i18n::sendMessage(orient,CB_SETCURSEL,d->page.width>d->page.height?1:0,0);
            control(L"STATIC",L"宽度 (mm)",0,24,136,86,22);control(L"EDIT",L"",203,110,132,85,26,WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL);control(L"STATIC",L"高度 (mm)",0,210,136,86,22);control(L"EDIT",L"",204,290,132,85,26,WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL);
            control(L"STATIC",L"页面布局 · Pages",0,22,184,300,24);control(L"STATIC",L"横向张数",0,24,224,84,22);control(L"EDIT",L"1",205,110,220,85,26,WS_BORDER|WS_TABSTOP|ES_NUMBER);control(L"STATIC",L"纵向张数",0,210,224,84,22);control(L"EDIT",L"1",206,290,220,85,26,WS_BORDER|WS_TABSTOP|ES_NUMBER);
            control(L"STATIC",L"每个方向 1–20 张；绘图按纸张网格排列。",0,24,266,360,24);
            control(L"STATIC",L"预览",0,412,18,170,24);control(L"BUTTON",L"确定",IDOK,414,332,88,30,BS_DEFPUSHBUTTON|WS_TABSTOP);control(L"BUTTON",L"取消",IDCANCEL,516,332,88,30,WS_TABSTOP);
            d->values(h);return TRUE;
        }
        if(!d)return FALSE;
        if(m==WM_COMMAND){int id=LOWORD(w),notification=HIWORD(w);
            if(id==IDCANCEL){EndDialog(h,0);return TRUE;}
            if(id==IDOK){if(!d->read(h)){studio_i18n::messageBox(h,L"请输入有效数值：纸张宽高 25.4–5080 mm，横向和纵向各 1–20 张。",L"页面设置",MB_OK|MB_ICONINFORMATION);return TRUE;}EndDialog(h,1);return TRUE;}
            if((id==201||id==202)&&notification==CBN_SELCHANGE){int paper=int(studio_i18n::sendDlgItemMessage(h,201,CB_GETCURSEL,0,0)),orient=int(studio_i18n::sendDlgItemMessage(h,202,CB_GETCURSEL,0,0));double sizes[][2]={{210,297},{297,420},{148,210},{176,250},{215.9,279.4},{215.9,355.6}};
                if(paper<6){d->page.width=pt(sizes[paper][orient?1:0]);d->page.height=pt(sizes[paper][orient?0:1]);}else if((orient==1)!=(d->page.width>d->page.height))std::swap(d->page.width,d->page.height);d->values(h);return TRUE;}
            if((id>=203&&id<=206)&&notification==EN_CHANGE&&!d->updating){if(id<=204)studio_i18n::sendDlgItemMessage(h,201,CB_SETCURSEL,6,0);if(d->read(h))InvalidateRect(h,0,TRUE);return TRUE;}
        }
        if(m==WM_PAINT){PAINTSTRUCT ps;HDC dc=BeginPaint(h,&ps);int x=int(410*d->dpi),y=int(60*d->dpi);double fit=std::min(180.*d->dpi/(d->page.width*d->page.across),240.*d->dpi/(d->page.height*d->page.down));HPEN pen=CreatePen(PS_SOLID,1,RGB(165,176,184));auto old=SelectObject(dc,pen);auto brush=SelectObject(dc,GetStockObject(WHITE_BRUSH));for(int r=0;r<d->page.down;r++)for(int c=0;c<d->page.across;c++)Rectangle(dc,x+int(c*d->page.width*fit),y+int(r*d->page.height*fit),x+int((c+1)*d->page.width*fit),y+int((r+1)*d->page.height*fit));SelectObject(dc,brush);SelectObject(dc,old);DeleteObject(pen);EndPaint(h,&ps);return TRUE;}
        if(m==WM_DESTROY){if(d->font)DeleteObject(d->font);return TRUE;}return FALSE;
    }
    static bool show(HWND owner,Page* p){PageDialog d;d.page=*p;struct Template{DLGTEMPLATE t;WORD menu,cls,title;} t={};t.t.style=WS_POPUP|WS_CAPTION|WS_SYSMENU|DS_MODALFRAME;t.t.cx=430;t.t.cy=260;
        // Title is set separately because the in-memory template has no resources.
        auto result=DialogBoxIndirectParamW(GetModuleHandleW(0),&t.t,owner,proc,(LPARAM)&d);if(result==1)*p=d.page;return result==1;
    }
};
}
#endif
