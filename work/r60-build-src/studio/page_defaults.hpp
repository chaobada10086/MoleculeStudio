#ifndef STUDIO_PAGE_DEFAULTS_HPP
#define STUDIO_PAGE_DEFAULTS_HPP
#include <windows.h>
#include <cmath>
#include <string>
// Paper size, orientation and page counts used by new documents. The choice is
// made in 设置 · 页面与排版 and stored in interface.ini next to the executable so
// it survives restarts and travels with the portable build. Geometry is kept in
// points because that is what the document model and Page use.
namespace pagedefaults {
struct Values {
    double width=595.2756,height=841.8898;int across=1,down=1;bool enabled=false;
};
inline std::wstring& pathOverride(){static std::wstring value;return value;}
inline std::wstring path(){
    if(!pathOverride().empty())return pathOverride();
    wchar_t exe[32768]={};GetModuleFileNameW(nullptr,exe,32768);
    std::wstring file=exe;return file.substr(0,file.find_last_of(L"\\/"))+L"\\interface.ini";
}
inline bool valid(const Values& value){
    return std::isfinite(value.width)&&std::isfinite(value.height)&&value.width>=72&&value.width<=14400&&value.height>=72&&value.height<=14400&&value.across>=1&&value.across<=20&&value.down>=1&&value.down<=20;
}
inline Values load(){
    Values value;std::wstring file=path();wchar_t text[128]={};
    auto read=[&](const wchar_t* key,double low,double high,double fallback){
        GetPrivateProfileStringW(L"Page",key,L"",text,128,file.c_str());
        wchar_t* end=nullptr;double number=wcstod(text,&end);
        return end!=text&&std::isfinite(number)&&number>=low&&number<=high?number:fallback;
    };
    value.enabled=GetPrivateProfileIntW(L"Page",L"Default",0,file.c_str())!=0;
    value.width=read(L"WidthMM",25.4,5080,210)*72.0/25.4;
    value.height=read(L"HeightMM",25.4,5080,297)*72.0/25.4;
    double across=read(L"Across",1,20,1),down=read(L"Down",1,20,1);
    value.across=across==int(across)?int(across):1;
    value.down=down==int(down)?int(down):1;
    if(!valid(value))return Values();
    return value;
}
inline Values& current(){static Values value=load();return value;}
inline void reload(){current()=load();}
inline bool save(const Values& value){
    if(!valid(value))return false;
    std::wstring file=path();wchar_t text[64];
    auto put=[&](const wchar_t* key,const std::wstring& text){return WritePrivateProfileStringW(L"Page",key,text.c_str(),file.c_str())!=0;};
    auto mm=[&](double points){swprintf(text,64,L"%.4f",points*25.4/72.0);return std::wstring(text);};
    bool ok=put(L"Default",value.enabled?L"1":L"0");
    ok=put(L"WidthMM",mm(value.width))&&ok;
    ok=put(L"HeightMM",mm(value.height))&&ok;
    ok=put(L"Across",std::to_wstring(value.across))&&ok;
    ok=put(L"Down",std::to_wstring(value.down))&&ok;
    if(!ok)return false;
    current()=value;return true;
}
}
#endif
