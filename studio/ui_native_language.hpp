#pragma once
#include "ui_language.hpp"
#include <commctrl.h>
#include <vector>
#include <map>
#include <string>
namespace studio_i18n {
const UINT languageMessage=WM_APP+0x3d1;
inline bool& refreshing(){static bool b=false;return b;}
struct ControlText {
    std::wstring caption,cue;
    std::vector<std::wstring> options;
    std::map<std::pair<int,int>,std::wstring> cells;
    std::map<int,std::wstring> columns,tabs;
};
inline std::map<HWND,ControlText>& controls(){static std::map<HWND,ControlText> c;return c;}
inline std::wstring className(HWND h){wchar_t c[128]={};GetClassNameW(h,c,128);return c;}
inline bool editable(HWND h){auto c=className(h);return c==L"Edit"||c==L"EDIT"||c.find(L"RichEdit")!=std::wstring::npos||c.find(L"RICHEDIT")!=std::wstring::npos;}
inline bool isCombo(HWND h){return className(h)==L"ComboBox";}
inline bool protectedText(HWND h){return GetPropW(h,L"MoleculeStudioRawText")!=nullptr;}
inline void protect(HWND h){SetPropW(h,L"MoleculeStudioRawText",(HANDLE)1);controls().erase(h);}
inline LRESULT CALLBACK cleanup(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR){if(m==WM_NCDESTROY){controls().erase(h);RemoveWindowSubclass(h,cleanup,0x3d1);}return DefSubclassProc(h,m,w,l);}
inline void track(HWND h){if(h)SetWindowSubclass(h,cleanup,0x3d1,0);}
inline BOOL setWindowText(HWND h,LPCWSTR value){
    if(!h||!value||editable(h)||isCombo(h)||protectedText(h)||className(h)==L"MoleculeStudioShell"||className(h)==L"MoleculeStudioTabs")return ::SetWindowTextW(h,value);
    if(!refreshing()){controls()[h].caption=value;track(h);}auto text=tr(value);return ::SetWindowTextW(h,text.c_str());
}
inline BOOL setDlgItemText(HWND h,int id,LPCWSTR text){return setWindowText(GetDlgItem(h,id),text);}
inline HWND createWindowEx(DWORD ex,LPCWSTR cls,LPCWSTR title,DWORD style,int x,int y,int width,int height,HWND owner,HMENU menu,HINSTANCE instance,LPVOID data){
    bool input=cls&&ULONG_PTR(cls)>65535&&(wcscmp(cls,L"EDIT")==0||wcsstr(cls,L"RichEdit")||wcsstr(cls,L"RICHEDIT"));
    auto text=title&&!input?tr(title):std::wstring(title?title:L"");
    auto h=::CreateWindowExW(ex,cls,title?text.c_str():nullptr,style,x,y,width,height,owner,menu,instance,data);
    if(h&&!input&&title&&className(h)!=L"MoleculeStudioShell"&&className(h)!=L"MoleculeStudioTabs"){controls()[h].caption=title;track(h);}return h;
}
inline HWND createWindow(DWORD exDummy,LPCWSTR cls,LPCWSTR title,DWORD style,int x,int y,int width,int height,HWND owner,HMENU menu,HINSTANCE instance,LPVOID data)=delete;
inline HWND createWindow(LPCWSTR cls,LPCWSTR title,DWORD style,int x,int y,int width,int height,HWND owner,HMENU menu,HINSTANCE instance,LPVOID data){return createWindowEx(0,cls,title,style,x,y,width,height,owner,menu,instance,data);}
inline std::wstring canonicalCombo(HWND h){
    auto found=controls().find(h);if(found==controls().end()||!isCombo(h)||protectedText(h))return L"";
    int index=int(::SendMessageW(h,CB_GETCURSEL,0,0));if(index<0||size_t(index)>=found->second.options.size())return L"";
    wchar_t buf[2048]={};::GetWindowTextW(h,buf,2048);auto original=found->second.options[size_t(index)];
    return buf==tr(original)?original:L"";
}
inline int getWindowText(HWND h,LPWSTR out,int count){auto source=canonicalCombo(h);if(!source.empty()&&out&&count>0){int n=std::min(int(source.size()),count-1);wmemcpy(out,source.c_str(),n);out[n]=0;return n;}return ::GetWindowTextW(h,out,count);}
inline int getWindowTextLength(HWND h){auto source=canonicalCombo(h);return source.empty()?::GetWindowTextLengthW(h):int(source.size());}
inline UINT getDlgItemText(HWND h,int id,LPWSTR text,int count){return UINT(getWindowText(GetDlgItem(h,id),text,count));}
inline LRESULT sendMessage(HWND h,UINT m,WPARAM w=0,LPARAM l=0){
    if(!h||protectedText(h))return ::SendMessageW(h,m,w,l);
    if(m==CB_RESETCONTENT||m==LB_RESETCONTENT){controls()[h].options.clear();return ::SendMessageW(h,m,w,l);}
    if((m==CB_ADDSTRING||m==CB_INSERTSTRING||m==LB_ADDSTRING||m==LB_INSERTSTRING)&&l){
        const wchar_t* source=(const wchar_t*)l;auto text=tr(source);auto result=::SendMessageW(h,m,w,(LPARAM)text.c_str());
        if(result>=0){auto& v=controls()[h].options;if(size_t(result)>v.size())v.resize(size_t(result));v.insert(v.begin()+result,source);track(h);}return result;
    }
    if(m==CB_DELETESTRING||m==LB_DELETESTRING){auto& v=controls()[h].options;if(size_t(w)<v.size())v.erase(v.begin()+w);return ::SendMessageW(h,m,w,l);}
    if(m==CB_FINDSTRING||m==CB_FINDSTRINGEXACT||m==CB_SELECTSTRING){if(l){auto value=tr((const wchar_t*)l);return ::SendMessageW(h,m,w,(LPARAM)value.c_str());}}
    if(m==CB_GETLBTEXTLEN||m==LB_GETTEXTLEN||m==CB_GETLBTEXT||m==LB_GETTEXT){auto it=controls().find(h);if(it!=controls().end()&&size_t(w)<it->second.options.size()){auto& s=it->second.options[size_t(w)];if((m==CB_GETLBTEXT||m==LB_GETTEXT)&&l)wcscpy((wchar_t*)l,s.c_str());return s.size();}}
    if((m==EM_SETCUEBANNER||m==CB_SETCUEBANNER)&&l){controls()[h].cue=(const wchar_t*)l;track(h);auto text=tr((const wchar_t*)l);return ::SendMessageW(h,m,w,(LPARAM)text.c_str());}
    if((m==TCM_INSERTITEMW||m==TCM_SETITEMW)&&l){auto c=*(TCITEMW*)l;if((c.mask&TCIF_TEXT)&&c.pszText&&ULONG_PTR(c.pszText)>65535){auto original=std::wstring(c.pszText),text=tr(original);c.pszText=&text[0];auto result=::SendMessageW(h,m,w,(LPARAM)&c);int index=m==TCM_INSERTITEMW?int(result):int(w);if(index>=0){controls()[h].tabs[index]=original;track(h);}return result;}}
    if((m==LVM_INSERTCOLUMNW||m==LVM_SETCOLUMNW)&&l){auto c=*(LVCOLUMNW*)l;if((c.mask&LVCF_TEXT)&&c.pszText&&ULONG_PTR(c.pszText)>65535){controls()[h].columns[int(w)]=c.pszText;track(h);auto text=tr(c.pszText);c.pszText=&text[0];return ::SendMessageW(h,m,w,(LPARAM)&c);}}
    if((m==LVM_INSERTITEMW||m==LVM_SETITEMW||m==LVM_SETITEMTEXTW)&&l){auto c=*(LVITEMW*)l;if((m==LVM_SETITEMTEXTW||(c.mask&LVIF_TEXT))&&c.pszText&&ULONG_PTR(c.pszText)>65535){auto text=tr(c.pszText);std::wstring original=c.pszText;c.pszText=&text[0];auto result=::SendMessageW(h,m,w,(LPARAM)&c);int row=m==LVM_INSERTITEMW?int(result):m==LVM_SETITEMTEXTW?int(w):c.iItem;if(row>=0){controls()[h].cells[{row,c.iSubItem}]=original;track(h);}return result;}}
    if(m==LVM_DELETEALLITEMS)controls()[h].cells.clear();
    if((m==TTM_ADDTOOLW||m==TTM_UPDATETIPTEXTW)&&l){auto tip=*(TOOLINFOW*)l;if(tip.lpszText&&ULONG_PTR(tip.lpszText)>65535){auto text=tr(tip.lpszText);tip.lpszText=&text[0];return ::SendMessageW(h,m,w,(LPARAM)&tip);}}
    if(m==WM_SETTEXT&&l&&!editable(h))return setWindowText(h,(LPCWSTR)l);
    return ::SendMessageW(h,m,w,l);
}
inline LRESULT sendDlgItemMessage(HWND h,int id,UINT m,WPARAM w,LPARAM l){return sendMessage(GetDlgItem(h,id),m,w,l);}
inline BOOL appendMenu(HMENU h,UINT flags,UINT_PTR id,LPCWSTR text){if(!text||(flags&(MF_OWNERDRAW|MF_BITMAP|MF_SEPARATOR)))return ::AppendMenuW(h,flags,id,text);auto value=tr(text);return ::AppendMenuW(h,flags,id,value.c_str());}
inline int messageBox(HWND h,LPCWSTR message,LPCWSTR title,UINT flags){
    auto text=tr(message?message:L""),heading=tr(title?title:L"");
    std::vector<int> ids;switch(flags&15){case MB_OKCANCEL:ids={IDOK,IDCANCEL};break;case MB_YESNO:ids={IDYES,IDNO};break;case MB_YESNOCANCEL:ids={IDYES,IDNO,IDCANCEL};break;case MB_RETRYCANCEL:ids={IDRETRY,IDCANCEL};break;case MB_ABORTRETRYIGNORE:ids={IDABORT,IDRETRY,IDIGNORE};break;default:ids={IDOK};}
    std::vector<std::wstring> labels;for(auto id:ids)labels.push_back(tr(id==IDOK?L"确定":id==IDCANCEL?L"取消":id==IDYES?L"是":id==IDNO?L"否":id==IDRETRY?L"重试":id==IDABORT?L"中止":L"忽略"));
    std::vector<TASKDIALOG_BUTTON> buttons;for(size_t i=0;i<ids.size();++i){TASKDIALOG_BUTTON b={ids[i],labels[i].c_str()};buttons.push_back(b);}
    auto module=GetModuleHandleW(L"comctl32.dll");typedef HRESULT (WINAPI *TaskFn)(const TASKDIALOGCONFIG*,int*,int*,BOOL*);auto fn=module?(TaskFn)GetProcAddress(module,"TaskDialogIndirect"):nullptr;
    if(fn){TASKDIALOGCONFIG c={};c.cbSize=sizeof(c);c.hwndParent=h;c.dwFlags=TDF_ALLOW_DIALOG_CANCELLATION|TDF_SIZE_TO_CONTENT;if(rtl())c.dwFlags|=TDF_RTL_LAYOUT;c.pszWindowTitle=heading.c_str();c.pszContent=text.c_str();c.cButtons=UINT(buttons.size());c.pButtons=buttons.data();unsigned def=(flags>>8)&3;c.nDefaultButton=ids[std::min(size_t(def),ids.size()-1)];c.pszMainIcon=((flags&MB_ICONMASK)==MB_ICONERROR)?TD_ERROR_ICON:((flags&MB_ICONMASK)==MB_ICONWARNING)?TD_WARNING_ICON:nullptr;int result=IDCANCEL;if(SUCCEEDED(fn(&c,&result,nullptr,nullptr)))return result;}
    return ::MessageBoxW(h,text.c_str(),heading.c_str(),flags);
}
inline std::wstring fileFilter(LPCWSTR original){std::wstring result;if(!original)return result;for(auto text=original;*text;text+=wcslen(text)+1){auto part=tr(text);result+=part;result.push_back(0);}result.push_back(0);return result;}
inline std::wstring systemCaption(std::wstring value){auto original=value;size_t at=0;while((at=value.find(L'&',at))!=std::wstring::npos)value.erase(at,1);if(value.size()>3&&value.back()==L')'&&value[value.size()-3]==L'(')value.erase(value.size()-3);bool colon=!value.empty()&&(value.back()==L':'||value.back()==L'：');if(colon)value.pop_back();
 static const std::map<std::wstring,std::wstring> aliases={{L"OK",L"确定"},{L"Cancel",L"取消"},{L"Open",L"打开"},{L"Save",L"保存"},{L"Save As",L"另存为…"},{L"Yes",L"是"},{L"No",L"否"},{L"File name",L"文件名"},{L"Files of type",L"文件类型"},{L"File type",L"文件类型"},{L"Help",L"帮助"},{L"Color",L"颜色"},{L"Basic colors",L"基本颜色"},{L"Custom colors",L"自定义颜色"},{L"Add to Custom Colors",L"添加到自定义颜色"},{L"Define Custom Colors >>",L"自定义颜色"},{L"Red",L"红色"},{L"Green",L"绿色"},{L"Blue",L"蓝色"}};
 auto known=aliases.find(value);if(known!=aliases.end())value=known->second;
 auto exact=catalog().find(value);if(exact!=catalog().end())return std::wstring(exact->second.text[current()])+(colon?L":" : L"");
 auto reverse=reverseCatalog().find(value);if(reverse!=reverseCatalog().end())return std::wstring(reverse->second->text[current()])+(colon?L":" : L"");return original;
}
inline void translateSystemDialog(HWND root){wchar_t title[1024]={};::GetWindowTextW(root,title,1024);auto localizedTitle=systemCaption(title);if(localizedTitle!=title)::SetWindowTextW(root,localizedTitle.c_str());EnumChildWindows(root,[](HWND h,LPARAM)->BOOL{auto cls=className(h);if(cls!=L"Button"&&cls!=L"Static")return TRUE;wchar_t text[2048]={};::GetWindowTextW(h,text,2048);auto localized=systemCaption(text);if(localized!=text)::SetWindowTextW(h,localized.c_str());return TRUE;},0);}
inline LPOFNHOOKPROC& previousFileHook(){static LPOFNHOOKPROC hook=nullptr;return hook;}
inline UINT_PTR CALLBACK fileHook(HWND h,UINT m,WPARAM w,LPARAM l){if(m==WM_INITDIALOG||m==WM_NOTIFY)translateSystemDialog(GetParent(h));return previousFileHook()?previousFileHook()(h,m,w,l):0;}
inline BOOL fileDialog(OPENFILENAMEW* value,bool save){if(!value)return FALSE;auto originalFilter=value->lpstrFilter,originalTitle=value->lpstrTitle;auto filter=fileFilter(originalFilter),title=tr(originalTitle?originalTitle:L"");if(originalFilter)value->lpstrFilter=filter.c_str();if(originalTitle)value->lpstrTitle=title.c_str();LANGID previous=GetThreadUILanguage();if(current()!=0&&current()!=1)SetThreadUILanguage(MAKELANGID(LANG_ENGLISH,SUBLANG_ENGLISH_US));auto hook=value->lpfnHook;DWORD flags=value->Flags;previousFileHook()=hook;value->lpfnHook=fileHook;value->Flags|=OFN_ENABLEHOOK|OFN_EXPLORER;BOOL ok=save?::GetSaveFileNameW(value) : ::GetOpenFileNameW(value);SetThreadUILanguage(previous);value->lpstrFilter=originalFilter;value->lpstrTitle=originalTitle;value->lpfnHook=hook;value->Flags=flags;previousFileHook()=nullptr;return ok;}
inline UINT_PTR CALLBACK colorHook(HWND h,UINT m,WPARAM,LPARAM){if(m==WM_INITDIALOG)translateSystemDialog(h);return 0;}
inline BOOL chooseColor(CHOOSECOLORW* value){if(!value)return FALSE;auto hook=value->lpfnHook;auto flags=value->Flags;LANGID previous=GetThreadUILanguage();if(current()!=0&&current()!=1)SetThreadUILanguage(MAKELANGID(LANG_ENGLISH,SUBLANG_ENGLISH_US));if(!hook){value->lpfnHook=colorHook;value->Flags|=CC_ENABLEHOOK;}BOOL ok=::ChooseColorW(value);SetThreadUILanguage(previous);value->lpfnHook=hook;value->Flags=flags;return ok;}
inline BOOL openFileName(OPENFILENAMEW* value){return fileDialog(value,false);}
inline BOOL saveFileName(OPENFILENAMEW* value){return fileDialog(value,true);}
inline int drawText(HDC dc,LPCWSTR text,int count,LPRECT r,UINT flags){auto value=tr(text?std::wstring(text,count<0?wcslen(text):size_t(count)):L"");return ::DrawTextW(dc,value.c_str(),int(value.size()),r,flags);}
inline BOOL textOut(HDC dc,int x,int y,LPCWSTR text,int count){auto value=tr(std::wstring(text,size_t(count)));return ::TextOutW(dc,x,y,value.c_str(),int(value.size()));}
inline Gdiplus::Status drawString(Gdiplus::Graphics& g,const wchar_t* v,INT n,const Gdiplus::Font* f,const Gdiplus::PointF& p,const Gdiplus::Brush* b){auto t=tr(v?std::wstring(v,n<0?wcslen(v):size_t(n)):L"");return g.DrawString(t.c_str(),int(t.size()),f,p,b);}
inline Gdiplus::Status drawString(Gdiplus::Graphics& g,const wchar_t* v,INT n,const Gdiplus::Font* f,const Gdiplus::RectF& p,const Gdiplus::StringFormat* fmt,const Gdiplus::Brush* b){auto t=tr(v?std::wstring(v,n<0?wcslen(v):size_t(n)):L"");return g.DrawString(t.c_str(),int(t.size()),f,p,fmt,b);}
inline Gdiplus::Status drawString(Gdiplus::Graphics& g,const wchar_t* v,INT n,const Gdiplus::Font* f,const Gdiplus::PointF& p,const Gdiplus::StringFormat* fmt,const Gdiplus::Brush* b){auto t=tr(v?std::wstring(v,n<0?wcslen(v):size_t(n)):L"");return g.DrawString(t.c_str(),int(t.size()),f,p,fmt,b);}
inline void refreshAll(){
    refreshing()=true;auto snapshot=controls();
    for(auto& entry:snapshot){HWND h=entry.first;if(!IsWindow(h)||protectedText(h))continue;auto& c=entry.second;
        if(!c.caption.empty()&&!editable(h)&&!isCombo(h)&&className(h)!=L"MoleculeStudioShell"&&className(h)!=L"MoleculeStudioTabs"){auto text=tr(c.caption);::SetWindowTextW(h,text.c_str());}
        if(!c.cue.empty()){auto text=tr(c.cue);::SendMessageW(h,isCombo(h)?CB_SETCUEBANNER:EM_SETCUEBANNER,TRUE,(LPARAM)text.c_str());}
        if(!c.options.empty()){auto combo=isCombo(h);int selected=int(::SendMessageW(h,combo?CB_GETCURSEL:LB_GETCURSEL,0,0));bool visible=(GetWindowLongPtrW(h,GWL_STYLE)&WS_VISIBLE)!=0;if(visible)::SendMessageW(h,WM_SETREDRAW,FALSE,0);::SendMessageW(h,combo?CB_RESETCONTENT:LB_RESETCONTENT,0,0);for(auto& v:c.options){auto text=tr(v);::SendMessageW(h,combo?CB_ADDSTRING:LB_ADDSTRING,0,(LPARAM)text.c_str());}::SendMessageW(h,combo?CB_SETCURSEL:LB_SETCURSEL,selected,0);if(visible)::SendMessageW(h,WM_SETREDRAW,TRUE,0);}
        for(auto& tab:c.tabs){auto text=tr(tab.second);TCITEMW v={};v.mask=TCIF_TEXT;v.pszText=&text[0];::SendMessageW(h,TCM_SETITEMW,tab.first,(LPARAM)&v);}
        for(auto& column:c.columns){auto text=tr(column.second);LVCOLUMNW v={};v.mask=LVCF_TEXT;v.pszText=&text[0];::SendMessageW(h,LVM_SETCOLUMNW,column.first,(LPARAM)&v);}
        for(auto& cell:c.cells){auto text=tr(cell.second);LVITEMW v={};v.iSubItem=cell.first.second;v.pszText=&text[0];::SendMessageW(h,LVM_SETITEMTEXTW,cell.first.first,(LPARAM)&v);}
        RedrawWindow(h,0,0,RDW_INVALIDATE);
    }
    refreshing()=false;
    auto notify=[](HWND h,LPARAM)->BOOL{if(className(h)==L"MoleculeStudioShell")PostMessageW(h,languageMessage,0,0);RedrawWindow(h,0,0,RDW_INVALIDATE|RDW_ALLCHILDREN);return TRUE;};
    EnumThreadWindows(GetCurrentThreadId(),[](HWND h,LPARAM data)->BOOL{auto callback=(WNDENUMPROC)data;callback(h,0);EnumChildWindows(h,callback,0);return TRUE;},(LPARAM)(WNDENUMPROC)notify);
}
}
