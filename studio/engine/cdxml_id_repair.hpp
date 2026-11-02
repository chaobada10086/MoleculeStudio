#pragma once
#include "third_party/tinyxml2/tinyxml2.h"
#include <map>
#include <string>
#include <set>
#include <vector>
#include <functional>
#include <cstring>
#include <cctype>
#include <limits>
namespace r62 { namespace persistence {
// Recover only unreferenced, childless arrows duplicated within one container.
// Never guess which atom/bond a reference intended, and never delete objects.
inline size_t repairUnreferencedArrowIds(std::string& bytes){
    tinyxml2::XMLDocument xml;if(xml.Parse(bytes.c_str())!=tinyxml2::XML_SUCCESS||!xml.RootElement())return 0;
    std::set<int> used,mentioned;std::vector<tinyxml2::XMLElement*> candidates;
    std::function<void(tinyxml2::XMLElement*)> scan=[&](tinyxml2::XMLElement* parent){
        if(parent->IntAttribute("id")>0)used.insert(parent->IntAttribute("id"));
        for(auto attr=parent->FirstAttribute();attr;attr=attr->Next())if(std::strcmp(attr->Name(),"id")){
            // Be conservative even about unknown reference attributes: any
            // matching integer token prevents automatic recovery of that ID.
            const char* p=attr->Value();while(*p){if(!std::isdigit((unsigned char)*p)){++p;continue;}const char* begin=p;long long value=0;bool overflow=false;
                while(std::isdigit((unsigned char)*p)){if(value>(std::numeric_limits<int>::max()-(*p-'0'))/10)overflow=true;if(!overflow)value=value*10+(*p-'0');++p;}
                bool decimal=(begin>attr->Value()&&begin[-1]=='.')||*p=='.';if(!overflow&&!decimal&&value>0)mentioned.insert(int(value));}
        }
        std::map<int,std::vector<tinyxml2::XMLElement*>> siblings;
        for(auto child=parent->FirstChildElement();child;child=child->NextSiblingElement()){
            int id=child->IntAttribute("id");if(id>0)siblings[id].push_back(child);scan(child);
        }
        for(auto& entry:siblings)if(entry.second.size()>1){bool arrows=true;
            for(auto node:entry.second)if(std::strcmp(node->Name(),"arrow")||node->FirstChildElement())arrows=false;
            if(arrows)for(size_t i=1;i<entry.second.size();++i)candidates.push_back(entry.second[i]);
        }
    };scan(xml.RootElement());
    size_t count=0;int next=1;
    for(auto node:candidates){int old=node->IntAttribute("id");if(mentioned.count(old))continue;
        while(used.count(next)&&next<std::numeric_limits<int>::max())++next;
        if(used.count(next))break;node->SetAttribute("id",next);used.insert(next);++count;
    }
    if(count){tinyxml2::XMLPrinter printer;xml.Print(&printer);bytes=printer.CStr();}
    return count;
}
}}


