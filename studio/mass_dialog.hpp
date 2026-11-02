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
    HWND list=0, theoretical=0, filterBox=0, copy=0;
    HFONT font=0, bigFont=0, monoFont=0;
    double dpi=1;
    int filter=0;                 // 0 all, 1 positive, 2 negative
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
        for(size_t i=0;i<adducts().size();++i){
            const Adduct& a=adducts()[i];
            if(filter==1&&a.charge<0)continue;
            if(filter==2&&a.charge>0)continue;
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
            set(3,number(mz(a,true),4));
            set(4,a.charge>0?L"正离子":L"负离子");
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
        if(adduct<0){studio_i18n::setWindowText(theoretical,L"—");EnableWindow(copy,FALSE);return;}
        const Adduct& a=adducts()[size_t(adduct)];
        double calculated=formula.exactValid?mz(a,false):mz(a,true);
        studio_i18n::setWindowText(theoretical,number(calculated,4).c_str());
        EnableWindow(copy,TRUE);
    }
    std::wstring summary(HWND h)const{
        (void)h;
        std::wostringstream out;out<<std::fixed<<std::setprecision(4);
        out<<L"分子式: "<<W(displayFormula(formula.plain))<<L"\r\n";
        out<<L"平均分子量: "<<formula.averageMass<<L" g/mol\r\n";
        out<<L"单同位素质量: "<<(formula.exactValid?number(formula.exactMass,4):L"—")<<L" u\r\n";
        out<<L"不饱和度 (DBE): "<<number(formula.dbe,1)<<L"\r\n";
        int adduct=selectedAdduct();
        if(adduct>=0){
            const Adduct& a=adducts()[size_t(adduct)];
            out<<L"加合物: "<<a.name<<L"  ("<<chargeText(a.charge)<<L")\r\n";
            out<<L"理论 m/z: "<<number(formula.exactValid?mz(a,false):mz(a,true),4)<<L"\r\n";
        }
        return out.str();
    }
    void copyResult(HWND h){
        if(!OpenClipboard(h))return;
        EmptyClipboard();
        std::wstring text=summary(h);
        HGLOBAL memory=GlobalAlloc(GMEM_MOVEABLE,(text.size()+1)*sizeof(wchar_t));
        if(memory){
            auto* p=(wchar_t*)GlobalLock(memory);
            if(p){wcscpy(p,text.c_str());GlobalUnlock(memory);SetClipboardData(CF_UNICODETEXT,memory);}
            else GlobalFree(memory);
        }
        CloseClipboard();
    }
    static INT_PTR CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){
        auto d=(MassCalculatorDialog*)GetWindowLongPtrW(h,DWLP_USER);
        if(m==WM_INITDIALOG){
            d=(MassCalculatorDialog*)l;SetWindowLongPtrW(h,DWLP_USER,(LONG_PTR)d);
            studio_i18n::setWindowText(h,L"质谱计算器 — Mass Calculator");
            d->dpi=GetDpiForWindow(h)/96.;auto px=[&](int n){return int(n*d->dpi);};
            SetWindowPos(h,0,0,0,px(720),px(520),SWP_NOMOVE|SWP_NOZORDER);
            RECT r,o;GetWindowRect(h,&r);GetWindowRect(GetParent(h),&o);
            SetWindowPos(h,0,o.left+(o.right-o.left-(r.right-r.left))/2,o.top+(o.bottom-o.top-(r.bottom-r.top))/2,0,0,SWP_NOSIZE|SWP_NOZORDER);
            d->font=CreateFontW(-px(13),0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");
            d->bigFont=CreateFontW(-px(19),0,0,0,FW_BOLD,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");
            d->monoFont=CreateFontW(-px(13),0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Consolas");
            auto control=[&](const wchar_t* cls,const wchar_t* title,int id,int x,int y,int width,int height,DWORD style=0,HFONT f=0){
                HWND c=studio_i18n::createWindowEx(0,cls,title,WS_CHILD|WS_VISIBLE|style,px(x),px(y),px(width),px(height),h,(HMENU)(INT_PTR)id,0,0);
                studio_i18n::sendMessage(c,WM_SETFONT,(WPARAM)(f?f:d->font),TRUE);return c;
            };
            std::wstring formulaText=d->formula.plain.empty()?L"未选择结构":W(displayFormula(d->formula.plain));
            control(L"STATIC",formulaText.c_str(),0,20,10,680,32,0,d->bigFont);
            control(L"STATIC",L"平均分子量",0,20,46,90,22);
            std::wstring avg=number(d->formula.averageMass,4)+L" g/mol";
            control(L"STATIC",avg.c_str(),0,112,46,170,22,0,d->monoFont);
            control(L"STATIC",L"单同位素质量",0,300,46,105,22);
            std::wstring exact=d->formula.exactValid?number(d->formula.exactMass,4)+L" u":L"—";
            control(L"STATIC",exact.c_str(),0,410,46,150,22,0,d->monoFont);
            control(L"STATIC",L"不饱和度 DBE",0,570,46,100,22);
            control(L"STATIC",number(d->formula.dbe,1).c_str(),0,672,46,48,22,0,d->monoFont);
            control(L"STATIC",L"",0,20,74,680,2,SS_ETCHEDHORZ);
            control(L"STATIC",L"常见加合物（选择一行查看理论 m/z）",0,20,84,300,22);
            d->filterBox=control(L"COMBOBOX",L"",501,560,82,140,200,CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP);
            for(auto s:{L"全部离子",L"正离子",L"负离子"})studio_i18n::sendMessage(d->filterBox,CB_ADDSTRING,0,(LPARAM)s);
            studio_i18n::sendMessage(d->filterBox,CB_SETCURSEL,0,0);
            d->list=control(WC_LISTVIEWW,L"",502,20,110,680,250,WS_BORDER|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS|WS_TABSTOP);
            studio_i18n::sendMessage(d->list,LVM_SETEXTENDEDLISTVIEWSTYLE,0,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER|LVS_EX_GRIDLINES);
            const wchar_t* cols[]={L"加合物",L"电荷",L"单同位素 m/z",L"平均 m/z",L"模式"};
            int widths[]={180,55,135,135,75};
            for(int i=0;i<5;i++){LVCOLUMNW c={};c.mask=LVCF_TEXT|LVCF_WIDTH;c.pszText=(wchar_t*)cols[i];c.cx=px(widths[i]);studio_i18n::sendMessage(d->list,LVM_INSERTCOLUMNW,i,(LPARAM)&c);}
            control(L"STATIC",L"所选加合物理论 m/z",0,20,372,130,22);
            d->theoretical=control(L"STATIC",L"—",0,154,372,160,22,0,d->monoFont);
            d->copy=control(L"BUTTON",L"复制结果",504,20,414,110,32,WS_TABSTOP);
            control(L"BUTTON",L"关闭",IDCANCEL,590,414,110,32,WS_TABSTOP|BS_DEFPUSHBUTTON);
            control(L"STATIC",L"单同位素 m/z 用于高分辨质谱；平均 m/z 用于低分辨或分子量核对。",0,20,462,680,22);
            d->populate(h);return TRUE;
        }
        if(!d)return FALSE;
        if(m==WM_COMMAND){
            int id=LOWORD(w),notification=HIWORD(w);
            if(id==IDCANCEL||id==IDOK){EndDialog(h,0);return TRUE;}
            if(id==501&&notification==CBN_SELCHANGE){d->filter=int(studio_i18n::sendMessage(d->filterBox,CB_GETCURSEL,0,0));d->populate(h);return TRUE;}
            if(id==504){d->copyResult(h);return TRUE;}
        }
        if(m==WM_NOTIFY&&((NMHDR*)l)->hwndFrom==d->list&&((NMHDR*)l)->code==LVN_ITEMCHANGED){
            int row=int(studio_i18n::sendMessage(d->list,LVM_GETNEXTITEM,-1,LVNI_SELECTED));
            d->selected=row>=0&&size_t(row)<d->visible.size()?int(d->visible[size_t(row)]):-1;
            d->update(h);return TRUE;
        }
        if(m==WM_CLOSE){EndDialog(h,0);return TRUE;}
        if(m==WM_DESTROY){if(d->font)DeleteObject(d->font);if(d->bigFont)DeleteObject(d->bigFont);if(d->monoFont)DeleteObject(d->monoFont);return TRUE;}
        return FALSE;
    }
    static void show(HWND owner,const Formula& value){
        MassCalculatorDialog d;d.formula=value;
        struct Template{DLGTEMPLATE t;WORD menu,cls,title;}t={};
        t.t.style=WS_POPUP|WS_CAPTION|WS_SYSMENU|DS_MODALFRAME;t.t.cx=720;t.t.cy=520;
        DialogBoxIndirectParamW(GetModuleHandleW(0),&t.t,owner,proc,(LPARAM)&d);
    }
};
#endif
