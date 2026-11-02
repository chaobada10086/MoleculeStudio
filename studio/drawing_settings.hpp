#ifndef STUDIO_DRAWING_SETTINGS_HPP
#define STUDIO_DRAWING_SETTINGS_HPP
// Project journal-style presets: bond length, stroke width and label typography.
// Historical preset values and their origins are recorded in PROVENANCE.md.
struct JournalStyle {
    const wchar_t* name;double bondLength;double lineWidth;const wchar_t* font;double fontSize;
};
inline const std::vector<JournalStyle>& journalStyles(){
    static const std::vector<JournalStyle> styles={
        {L"ACS Document 1996",14.40,0.60,L"Arial",10},
        {L"Nature Research",10.80,0.60,L"Arial",6},
        {L"RSC (1 Column)",12.19,0.45,L"Arial",7},
        {L"RSC (2 Column)",12.19,0.45,L"Arial",7},
        {L"Wiley",17.01,0.74,L"Arial",12},
        {L"Adv. Synth. Catal.",17.01,0.99,L"Arial",10},
        {L"SYNTHESIS / SYNLETT",17.01,0.79,L"Arial",10},
        {L"Science of Synthesis",17.01,0.79,L"Arial",10},
        {L"J. Chin. Chem. Soc.",18.00,0.99,L"Arial",12},
        {L"J. Het. Chem.",10.80,0.60,L"Times New Roman",7},
        {L"J. Mol. Mod. (1 Column)",14.40,0.60,L"Times New Roman",10},
        {L"J. Mol. Mod. (2 Column)",14.40,0.60,L"Times New Roman",10},
        {L"Verlag Helv. Chim. Acta",17.01,0.60,L"Arial",10},
        {L"Phytomedicine",20.01,0.99,L"Arial",12}
    };return styles;
}
inline const JournalStyle* journalStyle(const std::wstring& name){for(const auto& s:journalStyles())if(name==s.name)return &s;return nullptr;}
struct DrawingSettings {
    double bondLength=14.40,lineWidth=0.60;
    std::wstring fontName=L"Arial";double fontSize=10.0;
    std::wstring preset=L"ACS Document 1996";
    std::wstring path;
    static bool parse(const wchar_t* text,double low,double high,double* value){
        wchar_t* end=nullptr;double n=wcstod(text,&end);
        if(end==text)return false;
        while(*end==L' '||*end==L'\t'||*end==L'\r'||*end==L'\n')++end;
        if(*end||!std::isfinite(n)||n<low||n>high)return false;
        *value=n;return true;
    }
    DrawingSettings(){wchar_t exe[32768]={};GetModuleFileNameW(nullptr,exe,32768);path=exe;path=path.substr(0,path.find_last_of(L"\\/"))+L"\\drawing.ini";load();}
    void acsDefaults(){bondLength=14.40;lineWidth=0.60;fontName=L"Arial";fontSize=10.0;preset=L"ACS Document 1996";}
    void load(){
        acsDefaults();wchar_t text[256]={};
        GetPrivateProfileStringW(L"Drawing",L"Preset",L"",text,256,path.c_str());
        if(!text[0])return; // existing installs migrate to ACS Document 1996
        preset=text;
        GetPrivateProfileStringW(L"Drawing",L"BondLength",L"14.40",text,256,path.c_str());parse(text,2,200,&bondLength);
        GetPrivateProfileStringW(L"Drawing",L"LineWidth",L"0.60",text,256,path.c_str());parse(text,.1,12,&lineWidth);
        GetPrivateProfileStringW(L"Drawing",L"Font",L"Arial",text,256,path.c_str());fontName=text;
        GetPrivateProfileStringW(L"Drawing",L"FontSize",L"10",text,256,path.c_str());parse(text,4,96,&fontSize);
    }
    bool save()const{
        if(!std::isfinite(bondLength)||!std::isfinite(lineWidth)||!std::isfinite(fontSize)||bondLength<2||bondLength>200||lineWidth<.1||lineWidth>12||fontSize<4||fontSize>96||fontName.empty())return false;
        std::wstring temp=path+L".tmp-"+std::to_wstring(GetCurrentProcessId());
        std::wstring bytes=L"\xFEFF[Drawing]\r\nPreset="+preset+L"\r\nBondLength="+std::to_wstring(bondLength)+L"\r\nLineWidth="+std::to_wstring(lineWidth)+L"\r\nFont="+fontName+L"\r\nFontSize="+std::to_wstring(fontSize)+L"\r\n";
        HANDLE f=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(f==INVALID_HANDLE_VALUE)return false;DWORD written=0,wanted=DWORD(bytes.size()*sizeof(wchar_t));
        bool ok=WriteFile(f,bytes.data(),wanted,&written,nullptr)&&written==wanted;
        if(ok)ok=FlushFileBuffers(f)!=0;CloseHandle(f);
        if(ok)ok=MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
        if(!ok)DeleteFileW(temp.c_str());return ok;
    }
    void apply(r62::core::Document& doc)const{doc.noteTouchedDefaults();doc.setDefaultBondLength(bondLength);doc.setDefaultLineWidth(lineWidth);doc.setDefaultFont(U(fontName));doc.setDefaultTextSize(fontSize);}
};

struct DrawingSettingsDialog {
    DrawingSettings draft;bool applyCurrent=true;HFONT font=nullptr;
    static INT_PTR CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){
        auto d=(DrawingSettingsDialog*)GetWindowLongPtrW(h,DWLP_USER);
        if(m==WM_INITDIALOG){
            d=(DrawingSettingsDialog*)l;SetWindowLongPtrW(h,DWLP_USER,(LONG_PTR)d);studio_i18n::setWindowText(h,L"设置 · 结构绘图");
            double dpi=GetDpiForWindow(h)/96.;auto px=[&](int n){return int(n*dpi);};
            SetWindowPos(h,nullptr,0,0,px(470),px(340),SWP_NOMOVE|SWP_NOZORDER);
            RECT r,o;GetWindowRect(h,&r);GetWindowRect(GetParent(h),&o);SetWindowPos(h,nullptr,o.left+(o.right-o.left-r.right+r.left)/2,o.top+(o.bottom-o.top-r.bottom+r.top)/2,0,0,SWP_NOSIZE|SWP_NOZORDER);
            d->font=CreateFontW(-px(13),0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");
            auto control=[&](const wchar_t* cls,const wchar_t* text,int id,int x,int y,int width,int height,DWORD style=0){auto c=studio_i18n::createWindowEx(0,cls,text,WS_CHILD|WS_VISIBLE|style,px(x),px(y),px(width),px(height),h,(HMENU)(INT_PTR)id,nullptr,nullptr);studio_i18n::sendMessage(c,WM_SETFONT,(WPARAM)d->font,TRUE);return c;};
            control(L"STATIC",L"新建结构的默认格式（保存后重启仍生效）",0,20,18,420,24);
            control(L"STATIC",L"键长（pt）",0,20,58,130,24);
            control(L"EDIT",std::to_wstring(d->draft.bondLength).c_str(),621,160,54,135,28,WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL);
            control(L"STATIC",L"2–200 pt",0,312,58,120,24);
            control(L"STATIC",L"线宽（pt）",0,20,100,130,24);
            control(L"EDIT",std::to_wstring(d->draft.lineWidth).c_str(),622,160,96,135,28,WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL);
            control(L"STATIC",L"0.1–12 pt",0,312,100,120,24);
            control(L"BUTTON",L"同时应用到当前文档的默认格式",623,20,140,420,26,BS_AUTOCHECKBOX|WS_TABSTOP);CheckDlgButton(h,623,BST_CHECKED);
            control(L"STATIC",L"键长用于后续绘制，不缩放已有结构。\n线宽影响继承默认值的对象；单独设置过的线宽保留。",0,20,176,425,44);
            control(L"BUTTON",L"恢复默认",624,20,246,100,30,WS_TABSTOP);
            control(L"BUTTON",L"保存",IDOK,246,246,88,30,BS_DEFPUSHBUTTON|WS_TABSTOP);
            control(L"BUTTON",L"取消",IDCANCEL,346,246,88,30,WS_TABSTOP);return TRUE;
        }
        if(!d)return FALSE;
        if(m==WM_COMMAND){int id=LOWORD(w);
            if(id==IDCANCEL){EndDialog(h,0);return TRUE;}
            if(id==624){studio_i18n::setDlgItemText(h,621,L"14.40");studio_i18n::setDlgItemText(h,622,L"0.60");return TRUE;}
            if(id==IDOK){wchar_t value[128];studio_i18n::getDlgItemText(h,621,value,128);
                if(!DrawingSettings::parse(value,2,200,&d->draft.bondLength)){studio_i18n::messageBox(h,L"键长请输入 2–200 pt 的有效数值。",L"结构绘图",MB_OK|MB_ICONINFORMATION);SetFocus(GetDlgItem(h,621));return TRUE;}
                studio_i18n::getDlgItemText(h,622,value,128);
                if(!DrawingSettings::parse(value,.1,12,&d->draft.lineWidth)){studio_i18n::messageBox(h,L"线宽请输入 0.1–12 pt 的有效数值。",L"结构绘图",MB_OK|MB_ICONINFORMATION);SetFocus(GetDlgItem(h,622));return TRUE;}
                if(!d->draft.save()){studio_i18n::messageBox(h,L"无法保存绘图设置，请检查程序目录是否可写。",L"结构绘图",MB_OK|MB_ICONERROR);return TRUE;}
                d->applyCurrent=IsDlgButtonChecked(h,623)==BST_CHECKED;EndDialog(h,1);return TRUE;
            }
        }
        if(m==WM_DESTROY){DeleteObject(d->font);return TRUE;}return FALSE;
    }
    static bool show(HWND owner,DrawingSettings& settings,bool* apply){DrawingSettingsDialog d;d.draft=settings;
        struct Template{DLGTEMPLATE t;WORD menu,cls,title;}t={};t.t.style=WS_POPUP|WS_CAPTION|WS_SYSMENU|DS_MODALFRAME;t.t.cx=310;t.t.cy=220;
        if(DialogBoxIndirectParamW(GetModuleHandleW(nullptr),&t.t,owner,proc,(LPARAM)&d)!=1)return false;
        settings=d.draft;*apply=d.applyCurrent;return true;
    }
};
#endif
