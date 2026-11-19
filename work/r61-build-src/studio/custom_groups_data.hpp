#pragma once
namespace customgroups {
struct Entry {std::string name,fragment;};
inline std::wstring path(){if(auto p=std::getenv("MOLECULE_STUDIO_GROUP_LIBRARY")){int n=MultiByteToWideChar(CP_UTF8,0,p,-1,0,0);std::wstring w(n,0);MultiByteToWideChar(CP_UTF8,0,p,-1,&w[0],n);w.resize(n-1);return w;}wchar_t p[32768]={};GetModuleFileNameW(0,p,32768);std::wstring w=p;return w.substr(0,w.find_last_of(L"\\/"))+L"\\custom_groups.xml";}
inline std::string fold(std::string s){for(auto& c:s)if(c>='A'&&c<='Z')c+=32;return s;}
inline std::vector<Entry>& entries(){static std::vector<Entry> v;return v;}
inline bool& loaded(){static bool b=false;return b;}
inline std::string& readError(){static std::string e;return e;}
inline void loadChecked(){if(loaded())return;std::ifstream f(path().c_str(),std::ios::binary);if(!f){if(GetFileAttributesW(path().c_str())!=INVALID_FILE_ATTRIBUTES)throw std::runtime_error("无法读取已有官能团库，已保留原文件");loaded()=true;return;}std::string b((std::istreambuf_iterator<char>(f)),{});tinyxml2::XMLDocument d;if(b.size()>8*1024*1024||d.Parse(b.c_str())!=tinyxml2::XML_SUCCESS||!d.RootElement()||std::string(d.RootElement()->Name())!="groups")throw std::runtime_error("官能团库损坏，已保留原文件，请从备份恢复");std::vector<Entry> parsed;for(auto e=d.RootElement()->FirstChildElement("group");e;e=e->NextSiblingElement("group")){auto n=e->Attribute("name");auto t=e->GetText();if(!n||!t)throw std::runtime_error("官能团库条目损坏，已保留原文件");parsed.push_back({n,t});}entries()=parsed;loaded()=true;}
inline void load(){try{loadChecked();}catch(const std::exception& e){entries().clear();readError()=e.what();loaded()=true;}}
inline const Entry* find(const std::string& name){load();for(auto& e:entries())if(fold(e.name)==fold(name))return &e;return nullptr;}
inline void persist(const std::vector<Entry>& draft){load();if(!readError().empty())throw std::runtime_error(readError());tinyxml2::XMLDocument d;auto r=d.NewElement("groups");d.InsertEndChild(r);r->SetAttribute("version",1);for(auto& e:draft){auto g=d.NewElement("group");g->SetAttribute("name",e.name.c_str());g->SetText(e.fragment.c_str());r->InsertEndChild(g);}tinyxml2::XMLPrinter p;d.Print(&p);auto tmp=path()+L".tmp";{std::ofstream f(tmp.c_str(),std::ios::binary|std::ios::trunc);f.write(p.CStr(),p.CStrSize()-1);if(!f)throw std::runtime_error("无法写入官能团库");}if(!MoveFileExW(tmp.c_str(),path().c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("无法保存官能团库");entries()=draft;}
inline void remove(const std::string& name){load();auto v=entries();v.erase(std::remove_if(v.begin(),v.end(),[&](const Entry& e){return fold(e.name)==fold(name);}),v.end());persist(v);}
}
