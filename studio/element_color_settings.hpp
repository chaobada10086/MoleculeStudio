#pragma once
#include <cstdint>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "app_paths.hpp"
#include <algorithm>
#include <vector>
#include <map>
#include <sstream>
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
}
