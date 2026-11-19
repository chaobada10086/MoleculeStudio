#ifndef STUDIO_DRAWING_SETTINGS_HPP
#define STUDIO_DRAWING_SETTINGS_HPP
#include "app_paths.hpp"
// Numeric style facts are audited in docs/JOURNAL_DRAWING_STYLE_AUDIT.txt.
struct JournalStyle {
 const wchar_t* name;double bondLength,lineWidth,boldWidth,marginWidth,hashSpacing,bondSpacing,chainAngle;
 const wchar_t* font;double fontSize;const wchar_t* captionFont;double captionSize;const wchar_t* source;
};
inline const std::vector<JournalStyle>& journalStyles(){static const std::vector<JournalStyle> styles={
        {L"ACS Document 1996",14.4,0.6,2,1.6,2.5,18,120,L"Arial",10,L"Arial",10,L"ACS 1996；已对照 ACS 作者指南与 ChemDraw 手册"},
        {L"Nature Research",10.8,0.595275591,1.55905512,1.19055118,1.7007874,18,120,L"Arial",6,L"Arial",6,L"Nature 官方化学结构指南（Arial 6 pt）"},
        {L"RSC (1 Column)",12.2,0.5,1.6,1.25,1.8,20,120,L"Arial",7,L"Arial",7,L"RSC 官方参数；留白沿用模板；单栏/双栏结构样式相同"},
        {L"RSC (2 Column)",12.2,0.5,1.6,1.25,1.8,20,120,L"Arial",7,L"Arial",7,L"RSC 官方参数；留白沿用模板；单栏/双栏结构样式相同"},
        {L"Wiley",17.0078735,0.736999512,2.60786438,2.0125885,2.60786438,18,120,L"Arial",12,L"Arial",12,L"ChemDraw 25.1 内置历史样式；请对照目标期刊现行要求"},
        {L"Adv. Synth. Catal.",17,0.75,2.6,2,2.6,18,120,L"Arial",12,L"Arial",12,L"Advanced Synthesis & Catalysis 官方作者指南"},
        {L"SYNTHESIS / SYNLETT",10.119685,0.566929134,1.38897638,0.907086614,1.75748031,18,120,L"Arial",6,L"Arial",6,L"Thieme SYNLETT 2026 / SYNTHESIS 2024 作者指南"},
        {L"Science of Synthesis",17.0078735,0.793685913,2.0125885,2.23936462,2.49447632,18,120,L"Arial",10,L"Arial",10,L"Thieme 推荐 Science of Synthesis 模板；数值对照本机内置样式"},
        {L"J. Chin. Chem. Soc.",14.4,0.6,2,1.6,2.5,18,120,L"Times New Roman",10,L"Times New Roman",10,L"Journal of the Chinese Chemical Society 官方作者指南"},
        {L"J. Het. Chem.",17,0.75,2.6,2,2.6,18,120,L"Arial",12,L"Arial",12,L"Journal of Heterocyclic Chemistry 官方作者指南"},
        {L"J. Mol. Mod. (1 Column)",14.3999939,0.599990845,2.0125885,1.58738708,2.49447632,18,120,L"Times New Roman",10,L"Times New Roman",10,L"ChemDraw 25.1 内置历史样式；请对照目标期刊现行要求"},
        {L"J. Mol. Mod. (2 Column)",14.3999939,0.599990845,2.0125885,1.58738708,2.49447632,18,120,L"Times New Roman",10,L"Times New Roman",10,L"ChemDraw 25.1 内置历史样式；请对照目标期刊现行要求"},
        {L"Verlag Helv. Chim. Acta",11.9,0.42,1.4,1.4,1.4,15,120,L"Arial",7,L"Arial",8.4,L"Helvetica Chimica Acta 官方作者指南；标签 7 pt / 文字 8.4 pt"},
        {L"Phytomedicine",20.0125885,0.992111206,1.33332825,1.24723816,3,8,120,L"Arial",12,L"Arial",12,L"ChemDraw 25.1 内置历史样式；请对照目标期刊现行要求"},
};return styles;}
inline const JournalStyle* journalStyle(const std::wstring& name){for(const auto& s:journalStyles())if(name==s.name)return &s;return nullptr;}
struct DrawingSettings {
 double bondLength=14.4,lineWidth=.6,boldWidth=2,marginWidth=1.6,hashSpacing=2.5,bondSpacing=18,chainAngle=120;
 std::wstring fontName=L"Arial",captionFontName=L"Arial";double fontSize=10,captionFontSize=10;
 std::wstring preset=L"ACS Document 1996",path;
 static bool parse(const wchar_t* text,double low,double high,double* value){wchar_t* end=nullptr;double n=wcstod(text,&end);if(end==text)return false;while(*end==L' '||*end==L'\t'||*end==L'\r'||*end==L'\n')++end;if(*end||!std::isfinite(n)||n<low||n>high)return false;*value=n;return true;}
 static bool safe(const std::wstring& s,size_t max){return !s.empty()&&s.size()<=max&&s.find_first_of(L"\r\n[]=\0")==std::wstring::npos;}
 void use(const JournalStyle& s){preset=s.name;bondLength=s.bondLength;lineWidth=s.lineWidth;boldWidth=s.boldWidth;marginWidth=s.marginWidth;hashSpacing=s.hashSpacing;bondSpacing=s.bondSpacing;chainAngle=s.chainAngle;fontName=s.font;fontSize=s.fontSize;captionFontName=s.captionFont;captionFontSize=s.captionSize;}
 void acsDefaults(){use(journalStyles().front());}
 // User state: portable next to the EXE, %LOCALAPPDATA%\\MoleculeStudio when packaged (read-only install).
 DrawingSettings(){path=apppaths::dataFile(L"drawing.ini");load();}
 bool valid()const{auto ok=[](double v,double lo,double hi){return std::isfinite(v)&&v>=lo&&v<=hi;};return ok(bondLength,2,200)&&ok(lineWidth,.1,12)&&ok(boldWidth,.1,24)&&ok(marginWidth,0,20)&&ok(hashSpacing,.1,20)&&ok(bondSpacing,1,100)&&ok(chainAngle,30,180)&&ok(fontSize,4,96)&&ok(captionFontSize,4,96)&&safe(fontName,120)&&safe(captionFontName,120)&&safe(preset,80);}
 void read(const std::wstring& file,const wchar_t* section){wchar_t text[256]={};
  auto number=[&](const wchar_t* key,double lo,double hi,double& v){GetPrivateProfileStringW(section,key,std::to_wstring(v).c_str(),text,256,file.c_str());parse(text,lo,hi,&v);};
  number(L"BondLength",2,200,bondLength);number(L"LineWidth",.1,12,lineWidth);number(L"BoldWidth",.1,24,boldWidth);number(L"MarginWidth",0,20,marginWidth);number(L"HashSpacing",.1,20,hashSpacing);number(L"BondSpacing",1,100,bondSpacing);number(L"ChainAngle",30,180,chainAngle);number(L"FontSize",4,96,fontSize);
  GetPrivateProfileStringW(section,L"Font",fontName.c_str(),text,256,file.c_str());fontName=text;
  captionFontName=fontName;captionFontSize=fontSize;number(L"CaptionFontSize",4,96,captionFontSize);GetPrivateProfileStringW(section,L"CaptionFont",captionFontName.c_str(),text,256,file.c_str());captionFontName=text;
 }
 void load(){acsDefaults();wchar_t text[256]={};GetPrivateProfileStringW(L"Drawing",L"Preset",L"",text,256,path.c_str());if(!text[0])return;if(auto style=journalStyle(text))use(*style);else preset=text;read(path,L"Drawing");}
 std::wstring values()const{return L"BondLength="+std::to_wstring(bondLength)+L"\r\nLineWidth="+std::to_wstring(lineWidth)+L"\r\nBoldWidth="+std::to_wstring(boldWidth)+L"\r\nMarginWidth="+std::to_wstring(marginWidth)+L"\r\nHashSpacing="+std::to_wstring(hashSpacing)+L"\r\nBondSpacing="+std::to_wstring(bondSpacing)+L"\r\nChainAngle="+std::to_wstring(chainAngle)+L"\r\nFont="+fontName+L"\r\nFontSize="+std::to_wstring(fontSize)+L"\r\nCaptionFont="+captionFontName+L"\r\nCaptionFontSize="+std::to_wstring(captionFontSize)+L"\r\n";}
 static bool write(const std::wstring& path,const std::wstring& contents){auto temp=path+L".tmp-"+std::to_wstring(GetCurrentProcessId());auto bytes=L"\xFEFF"+contents;HANDLE f=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);if(f==INVALID_HANDLE_VALUE)return false;DWORD written=0,wanted=DWORD(bytes.size()*sizeof(wchar_t));bool ok=WriteFile(f,bytes.data(),wanted,&written,nullptr)&&written==wanted;if(ok)ok=FlushFileBuffers(f)!=0;CloseHandle(f);if(ok)ok=MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;if(!ok)DeleteFileW(temp.c_str());return ok;}
 bool save()const{return valid()&&write(path,L"[Drawing]\r\nPreset="+preset+L"\r\n"+values());}
 std::wstring templatesPath()const{return path+L".templates.ini";}
 std::vector<DrawingSettings> templates()const{std::vector<DrawingSettings> result;auto file=templatesPath();int count=GetPrivateProfileIntW(L"Templates",L"Count",0,file.c_str());if(count<0||count>100)return result;for(int i=0;i<count;++i){auto section=L"Template"+std::to_wstring(i);wchar_t name[256]={};GetPrivateProfileStringW(section.c_str(),L"Name",L"",name,256,file.c_str());auto v=*this;v.acsDefaults();v.preset=name;v.read(file,section.c_str());if(v.valid()&&!journalStyle(v.preset))result.push_back(v);}return result;}
 bool saveTemplates(const std::vector<DrawingSettings>& rows)const{if(rows.size()>100)return false;std::wstring contents=L"[Templates]\r\nCount="+std::to_wstring(rows.size())+L"\r\n";for(size_t i=0;i<rows.size();++i){if(!rows[i].valid()||journalStyle(rows[i].preset))return false;contents+=L"[Template"+std::to_wstring(i)+L"]\r\nName="+rows[i].preset+L"\r\n"+rows[i].values();}return write(templatesPath(),contents);}
 void apply(r62::core::Document& doc)const{doc.noteTouchedDefaults();doc.noteTouchedExtensions(r62::core::ObjectRef(),true);doc.setDefaultBondLength(bondLength);doc.setDefaultLineWidth(lineWidth);doc.setDefaultFont(U(captionFontName));doc.setDefaultTextSize(captionFontSize);auto& ext=doc.documentExtensions();ext.set("studio.style.LabelSize",std::to_string(fontSize));ext.set("studio.style.LabelFontName",U(fontName));for(const auto& v:std::vector<std::pair<std::string,double>>{{"ChainAngle",chainAngle},{"BondSpacing",bondSpacing},{"BoldWidth",boldWidth},{"MarginWidth",marginWidth},{"HashSpacing",hashSpacing}}){ext.set("studio.style."+v.first,std::to_string(v.second));ext.set("pst.attr."+v.first,std::to_string(v.second));}}
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
