#pragma once
#include "compound_catalog.hpp"
#include "third_party/tinyxml2/tinyxml2.h"
#include <cctype>
namespace studio_terminal_drag {
struct End {std::string label,base,token,fragment;int element=0,host=6;size_t offset=0;};
inline const std::map<std::string,End>& entries(){static const auto data=[](){std::map<std::string,End> out;
    for(const auto& compound:compounddata::entries){std::string aliases=compound.aliases;size_t start=0;while(start<aliases.size()){
        size_t stop=aliases.find('|',start);if(stop==std::string::npos)stop=aliases.size();auto label=aliases.substr(start,stop-start);start=stop+1;
        for(auto symbol:std::vector<std::pair<std::string,int>>{{"Cl",17},{"Br",35},{"I",53},{"F",9}}){size_t tail=label.size();while(tail&&std::isdigit(static_cast<unsigned char>(label[tail-1])))--tail;
            if(tail<=symbol.first.size()||label.substr(tail-symbol.first.size(),symbol.first.size())!=symbol.first)continue;
            tinyxml2::XMLDocument xml;if(xml.Parse(compound.fragment)!=tinyxml2::XML_SUCCESS)continue;auto f=xml.RootElement();if(!f)continue;
            tinyxml2::XMLElement* terminal=nullptr;int count=0;for(auto n=f->FirstChildElement("n");n;n=n->NextSiblingElement("n"))if(n->IntAttribute("Element")==symbol.second){terminal=n;++count;}
            int written=tail<label.size()?std::atoi(label.substr(tail).c_str()):1;if(!terminal||count!=written)continue;
            auto id=terminal->IntAttribute("id");int hostId=-1,degree=0;for(auto b=f->FirstChildElement("b");b;b=b->NextSiblingElement("b"))if(b->IntAttribute("B")==id||b->IntAttribute("E")==id){if(b->IntAttribute("Order",1)!=1){degree=99;break;}hostId=b->IntAttribute("B")==id?b->IntAttribute("E"):b->IntAttribute("B");++degree;}
            if(degree!=1)continue;auto host=f->FirstChildElement("n");while(host&&host->IntAttribute("id")!=hostId)host=host->NextSiblingElement("n");if(!host)continue;
            End e;e.label=label;e.token=symbol.first;e.element=symbol.second;e.host=host->IntAttribute("Element",6);e.offset=tail-symbol.first.size();e.base=label.substr(0,e.offset);if(count>1)e.base+=symbol.first+(count>2?std::to_string(count-1):"");
            terminal->SetAttribute("NodeType","ExternalConnectionPoint");terminal->DeleteAttribute("Element");terminal->DeleteAttribute("NumHydrogens");terminal->DeleteChildren();tinyxml2::XMLPrinter printer;f->Accept(&printer);e.fragment=printer.CStr();out[compounddata::key(label)]=e;
        }
    }}return out;}();return data;}
inline const End* find(const std::string& label){auto it=entries().find(compounddata::key(label));return it==entries().end()?nullptr:&it->second;}
}
