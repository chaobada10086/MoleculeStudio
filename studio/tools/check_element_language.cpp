// Standalone language regression, including live native caption refresh.
// g++ -std=c++17 -I studio studio/tools/check_element_language.cpp -lcomctl32 -lcomdlg32 -o work/check-element-language.exe
#define _WIN32_WINNT 0x0A00
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <objidl.h>
#include <gdiplus.h>
#include "ui_native_language.hpp"
#include <iostream>
#include <stdexcept>
int main(){
    using namespace studio_i18n;
    auto require=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};
    try{
        std::vector<std::vector<std::wstring>> rows;std::vector<std::wstring> fields;
        const wchar_t* first=elementTranslationBlob();
        for(const wchar_t* p=first;*p;++p)if(*p==0x1f||*p==0x1e){fields.emplace_back(first,p);first=p+1;if(*p==0x1e){rows.push_back(fields);fields.clear();}}
        require(rows.size()==14,"element translation row count");
        HWND caption=createWindowEx(0,L"STATIC",L"元素周期表",WS_POPUP,0,0,400,80,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        require(caption!=nullptr,"native caption fixture");
        for(int language=0;language<15;++language){
            current()=language;
            for(const auto& row:rows){require(row.size()==15&&!row[language].empty(),"missing translation");require(tr(row[0])==row[language],"catalog lookup differs from curated translation");if(language>=4)require(!hasHan(row[language]),"unexpected Chinese fallback");}
            auto status=tr(L"元素 %s：点击画布添加或替换原子，Esc 退出");require(status.find(L"%s")!=std::wstring::npos,"status substitution lost");
            refreshAll();wchar_t text[256]={};GetWindowTextW(caption,text,256);require(text==tr(L"元素周期表"),"native caption did not update on language switch");
            std::cout<<language<<": PASS\n";
        }
        DestroyWindow(caption);std::cout<<"PASS: 14 strings x 15 languages, status placeholders and live window captions\n";
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
