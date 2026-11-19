#ifndef STUDIO_MASS_DIALOG_HPP
#define STUDIO_MASS_DIALOG_HPP
// Common mass-spectrometry calculator: average and monoisotopic mass, DBE,
// and the usual positive/negative adducts with their theoretical m/z.
struct MassCalculatorDialog {
    struct Adduct {
        const wchar_t* name;
        int charge;
        double monoDelta;   // exact adduct mass relative to M
        double avgDelta;    // average adduct mass relative to M
    };
    Formula formula;
    HWND list=0, theoretical=0, filterBox=0, copy=0;bool embedded=false,showAll=false,expanded[2]={false,false};
    HFONT font=0, bigFont=0, monoFont=0,tableFont=0;
    double dpi=1,hostDpi=0;
    int filter=1;                 // 0 all, 1 positive, 2 negative
    int selected=-1;              // index into adducts()
    std::vector<size_t> visible;
    static const std::vector<Adduct>& adducts(){
        static const std::vector<Adduct> table={
            {L"[M]+•",             1, -0.000548579909, -0.000548579909},
            {L"[M+H]+",            1,  1.007276466621,  1.00794},
            {L"[M+Na]+",           1, 22.989220702,     22.98977},
            {L"[M+K]+",            1, 38.963157906,     39.0983},
            {L"[M+NH4]+",          1, 18.033825554,     18.03846},
            {L"[M+H-H2O]+",        1,-17.003288231,    -17.00734},
            {L"[M+2H]2+",          2,  2.014552933,      2.01588},
            {L"[M+3H]3+",          3,  3.021829400,      3.02382},
            {L"[M+ACN+H]+",        1, 42.033825554,     42.06102},
            {L"[M+MeOH+H]+",       1, 33.033491208,     33.05010},
            {L"[M]-•",            -1,  0.000548579909,  0.000548579909},
            {L"[M-H]-",           -1, -1.007276466621, -1.00794},
            {L"[M+Cl]-",          -1, 34.969401262,     35.453},
            {L"[M+HCOO]-",        -1, 44.998202715,     45.01744},
            {L"[M+CH3COO]-",      -1, 59.013853563,     59.04454},
            {L"[M+Br]-",          -1, 78.918886180,     79.904},
            {L"[M+NO3]-",         -1, 61.988366190,     62.00490},
            {L"[M-2H]2-",         -2, -2.014552933,     -2.01588}
        };
        return table;
    }
    static std::wstring number(double value,int decimals){
        wchar_t buffer[64];swprintf(buffer,64,L"%.*f",decimals,value);return buffer;
    }
    static std::wstring chargeText(int charge){
        std::wstring out=charge>0?L"+":L"−";
        int magnitude=std::abs(charge);
        if(magnitude!=1)out+=std::to_wstring(magnitude);
        return out;
    }
    double mz(const Adduct& a,bool average)const{
        double base=average?formula.averageMass:formula.exactMass;
        return (base+(average?a.avgDelta:a.monoDelta))/std::abs(a.charge);
    }
    int selectedAdduct()const{
        if(selected<0||size_t(selected)>=adducts().size())return -1;
        return selected;
    }
    void populate(HWND h){
        studio_i18n::sendMessage(list,LVM_DELETEALLITEMS,0,0);visible.clear();
        if(!formula.valid){selected=-1;update(h);return;}
        for(size_t i=0;i<adducts().size();++i){
            const Adduct& a=adducts()[i];
            if(filter==1&&a.charge<0)continue;
            if(filter==2&&a.charge>0)continue;
            const bool common=i==1||i==2||i==3||i==4||i==11||i==12||i==13||i==14;
            if(!showAll&&!common)continue;
            int row=int(visible.size());visible.push_back(i);
            std::wstring name=a.name;
            LVITEMW item={};item.mask=LVIF_TEXT;item.iItem=row;item.pszText=&name[0];
            studio_i18n::sendMessage(list,LVM_INSERTITEMW,0,(LPARAM)&item);
            auto set=[&](int col,const std::wstring& value){
                LVITEMW v={};v.iSubItem=col;v.pszText=(wchar_t*)value.c_str();
                studio_i18n::sendMessage(list,LVM_SETITEMTEXTW,row,(LPARAM)&v);
            };
            set(1,chargeText(a.charge));
            set(2,formula.exactValid?number(mz(a,false),4):L"—");
            set(3,a.charge>0?L"正离子":L"负离子");
        }
        int row=0;
        if(!visible.empty()){
            for(size_t i=0;i<visible.size();++i)if(wcscmp(adducts()[visible[i]].name,L"[M+H]+")==0){row=int(i);break;}
            selected=int(visible[size_t(row)]);
            LVITEMW state={};state.stateMask=LVIS_SELECTED|LVIS_FOCUSED;state.state=state.stateMask;
            studio_i18n::sendMessage(list,LVM_SETITEMSTATE,row,(LPARAM)&state);
            studio_i18n::sendMessage(list,LVM_ENSUREVISIBLE,row,FALSE);
        }else selected=-1;
        update(h);
    }
    void update(HWND h){
        int adduct=selectedAdduct();
        if(adduct<0||!formula.exactValid){studio_i18n::setWindowText(theoretical,L"—");EnableWindow(copy,FALSE);return;}
        const Adduct& a=adducts()[size_t(adduct)];
        double calculated=mz(a,false);
        studio_i18n::setWindowText(theoretical,number(calculated,4).c_str());
        EnableWindow(copy,TRUE);
    }
    std::wstring copyValue(int id)const{
        if(id==507)return formula.valid?number(formula.averageMass,4):L"";
        if(id==505)return formula.exactValid?number(formula.exactMass,4):L"";
        int index=selectedAdduct();return index>=0&&formula.exactValid?number(mz(adducts()[size_t(index)],false),4):L"";
    }
    static void copyText(HWND h,const std::wstring& text){
        if(!OpenClipboard(h))return;EmptyClipboard();
        HGLOBAL memory=GlobalAlloc(GMEM_MOVEABLE,(text.size()+1)*sizeof(wchar_t));
        if(memory){auto p=(wchar_t*)GlobalLock(memory);if(p){wcscpy(p,text.c_str());GlobalUnlock(memory);if(!SetClipboardData(CF_UNICODETEXT,memory))GlobalFree(memory);}else GlobalFree(memory);}CloseClipboard();
    }
    void copyResult(HWND h,int id=504){auto value=copyValue(id);if(!value.empty())copyText(h,value);}
    static INT_PTR CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){
        auto d=(MassCalculatorDialog*)GetWindowLongPtrW(h,DWLP_USER);
        if(m==WM_INITDIALOG){
            d=(MassCalculatorDialog*)l;SetWindowLongPtrW(h,DWLP_USER,(LONG_PTR)d);
            if(!d->embedded)studio_i18n::setWindowText(h,L"计算器");
            d->dpi=d->embedded&&d->hostDpi>0?d->hostDpi:GetDpiForWindow(h)/96.;auto px=[&](int n){return int(n*d->dpi);};
            if(!d->embedded){SetWindowPos(h,0,0,0,px(750),px(550),SWP_NOMOVE|SWP_NOZORDER);
            RECT r,o;GetWindowRect(h,&r);GetWindowRect(GetParent(h),&o);
            SetWindowPos(h,0,o.left+(o.right-o.left-(r.right-r.left))/2,o.top+(o.bottom-o.top-(r.bottom-r.top))/2,0,0,SWP_NOSIZE|SWP_NOZORDER);}
            d->font=CreateFontW(-px(15),0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");
            d->bigFont=CreateFontW(-px(22),0,0,0,FW_BOLD,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");
            d->monoFont=CreateFontW(-px(15),0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Consolas");
            d->tableFont=CreateFontW(-px(18),0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");
            auto control=[&](const wchar_t* cls,const wchar_t* title,int id,int x,int y,int width,int height,DWORD style=0,HFONT f=0){
                HWND c=studio_i18n::createWindowEx(0,cls,title,WS_CHILD|WS_VISIBLE|style,px(x),px(y),px(width),px(height),h,(HMENU)(INT_PTR)id,0,0);
                studio_i18n::sendMessage(c,WM_SETFONT,(WPARAM)(f?f:d->font),TRUE);return c;
            };
            std::wstring formulaText=d->formula.plain.empty()?L"未选择结构":W(displayFormula(d->formula.plain));
            control(L"STATIC",formulaText.c_str(),0,20,10,680,32,0,d->bigFont);
            control(L"STATIC",L"平均分子量",0,20,46,90,22);
            std::wstring avg=number(d->formula.averageMass,4)+L" g/mol";
            control(L"EDIT",avg.c_str(),508,116,46,126,24,ES_READONLY|ES_AUTOHSCROLL|WS_TABSTOP,d->monoFont);
            auto averageCopy=control(L"BUTTON",L"复制",507,248,43,54,28,WS_TABSTOP);EnableWindow(averageCopy,d->formula.valid);
            control(L"STATIC",L"单同位素质量",0,322,46,105,22);
            std::wstring exact=d->formula.exactValid?number(d->formula.exactMass,4)+L" u":L"—";
            control(L"EDIT",exact.c_str(),509,430,46,130,24,ES_READONLY|ES_AUTOHSCROLL|WS_TABSTOP,d->monoFont);
            auto exactCopy=control(L"BUTTON",L"复制",505,568,43,54,28,WS_TABSTOP);EnableWindow(exactCopy,d->formula.exactValid);
            control(L"STATIC",L"DBE",0,632,46,40,22);
            control(L"STATIC",number(d->formula.dbe,1).c_str(),0,678,46,42,22,0,d->monoFont);
            control(L"STATIC",L"",0,20,74,680,2,SS_ETCHEDHORZ);
            
            d->filterBox=control(WC_TABCONTROLW,L"",501,20,82,440,28,WS_TABSTOP);
            for(int i=0;i<2;++i){TCITEMW item={};item.mask=TCIF_TEXT;item.pszText=(LPWSTR)(i?L"负离子":L"正离子");SendMessageW(d->filterBox,TCM_INSERTITEMW,i,(LPARAM)&item);}
            SendMessageW(d->filterBox,TCM_SETCURSEL,d->filter-1,0);
            control(L"BUTTON",d->showAll?L"只显示常用四项":L"显示全部",506,568,80,132,28,WS_TABSTOP);
            d->list=control(WC_LISTVIEWW,L"",502,20,110,680,250,WS_BORDER|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS|WS_TABSTOP);
            SendMessageW(d->list,WM_SETFONT,(WPARAM)d->tableFont,TRUE);
            studio_i18n::sendMessage(d->list,LVM_SETEXTENDEDLISTVIEWSTYLE,0,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER|LVS_EX_GRIDLINES);
            const wchar_t* cols[]={L"加合物",L"电荷",L"精确 m/z",L"模式"};
            int widths[]={210,65,225,140};
            for(int i=0;i<4;i++){LVCOLUMNW c={};c.mask=LVCF_TEXT|LVCF_WIDTH;c.pszText=(wchar_t*)cols[i];c.cx=px(widths[i]);studio_i18n::sendMessage(d->list,LVM_INSERTCOLUMNW,i,(LPARAM)&c);}
            control(L"STATIC",L"所选加合物精确 m/z",0,20,372,154,24);
            d->theoretical=control(L"EDIT",L"—",510,180,372,140,24,ES_READONLY|ES_AUTOHSCROLL|WS_TABSTOP,d->monoFont);
            d->copy=control(L"BUTTON",L"复制",504,338,369,62,30,WS_TABSTOP);
            if(!d->embedded)control(L"BUTTON",L"关闭",IDCANCEL,590,414,110,32,WS_TABSTOP|BS_DEFPUSHBUTTON);
            
            d->populate(h);return TRUE;
        }
        if(!d)return FALSE;
        if(m==WM_COMMAND){
            int id=LOWORD(w),notification=HIWORD(w);
            if(id==IDCANCEL||id==IDOK){if(!d->embedded)EndDialog(h,0);return TRUE;}
            if(id==505||id==507){d->copyResult(h,id);return TRUE;}
            if(id==506){d->showAll=!d->showAll;d->expanded[d->filter-1]=d->showAll;studio_i18n::setWindowText(GetDlgItem(h,506),d->showAll?L"只显示常用四项":L"显示全部");d->populate(h);return TRUE;}
            if(id==504){d->copyResult(h);return TRUE;}
        }
        if(m==WM_NOTIFY&&((NMHDR*)l)->hwndFrom==d->filterBox&&((NMHDR*)l)->code==TCN_SELCHANGE){d->filter=int(SendMessageW(d->filterBox,TCM_GETCURSEL,0,0))+1;d->showAll=d->expanded[d->filter-1];studio_i18n::setWindowText(GetDlgItem(h,506),d->showAll?L"只显示常用四项":L"显示全部");d->populate(h);return TRUE;}
        if(m==WM_NOTIFY&&((NMHDR*)l)->hwndFrom==d->list&&((NMHDR*)l)->code==LVN_ITEMCHANGED){
            int row=int(studio_i18n::sendMessage(d->list,LVM_GETNEXTITEM,-1,LVNI_SELECTED));
            d->selected=row>=0&&size_t(row)<d->visible.size()?int(d->visible[size_t(row)]):-1;
            d->update(h);return TRUE;
        }
        if(m==WM_CLOSE){if(!d->embedded)EndDialog(h,0);return TRUE;}
        if(m==WM_DESTROY){if(d->font)DeleteObject(d->font);if(d->bigFont)DeleteObject(d->bigFont);if(d->monoFont)DeleteObject(d->monoFont);if(d->tableFont)DeleteObject(d->tableFont);d->font=d->bigFont=d->monoFont=d->tableFont=0;return TRUE;}
        return FALSE;
    }
    HWND createPage(HWND owner){
        embedded=true;struct Template{DLGTEMPLATE t;WORD menu,cls,title;}t={};
        t.t.style=WS_CHILD|DS_CONTROL;t.t.cx=720;t.t.cy=500;
        return CreateDialogIndirectParamW(GetModuleHandleW(0),&t.t,owner,proc,(LPARAM)this);
    }
    static void show(HWND owner,const Formula& value){
        MassCalculatorDialog d;d.formula=value;
        struct Template{DLGTEMPLATE t;WORD menu,cls,title;}t={};
        t.t.style=WS_POPUP|WS_CAPTION|WS_SYSMENU|DS_MODALFRAME;t.t.cx=720;t.t.cy=520;
        DialogBoxIndirectParamW(GetModuleHandleW(0),&t.t,owner,proc,(LPARAM)&d);
    }
};
#endif
