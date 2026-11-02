#pragma once
#include "third_party/tinyxml2/tinyxml2.h"
#include "nickname_catalog.hpp"
#include <functional>
#include <map>
#include <vector>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <stdexcept>
// Work on an XML copy. Existing external bonds keep their IDs, endpoints,
// wedge direction and ordering; the real attachment atom reuses the label ID.
namespace nicknameexpand {
using tinyxml2::XMLElement;
inline void walk(XMLElement* e,const std::function<void(XMLElement*)>& fn){fn(e);for(auto c=e->FirstChildElement();c;c=c->NextSiblingElement())walk(c,fn);}
inline std::string label(XMLElement* n){std::string s;if(auto t=n->FirstChildElement("t"))for(auto r=t->FirstChildElement("s");r;r=r->NextSiblingElement("s"))if(r->GetText())s+=r->GetText();return s;}
inline std::pair<double,double> point(XMLElement* n){double x=0,y=0;if(auto p=n->Attribute("p")){std::istringstream in(p);in>>x>>y;}return {x,y};}
inline void remap(XMLElement* root,const std::map<int,int>& ids){walk(root,[&](XMLElement* e){for(auto key:{"id","B","E","ConnectionOrder","BondOrdering","BondCircularOrdering","CrossingBonds","object"})if(auto text=e->Attribute(key)){std::istringstream in(text);std::ostringstream out;int id;bool first=true;while(in>>id){if(!first)out<<' ';auto it=ids.find(id);out<<(it==ids.end()?id:it->second);first=false;}e->SetAttribute(key,out.str().c_str());}});}
inline std::string expand(const std::string& source){
    tinyxml2::XMLDocument xml;
    if(xml.Parse(source.c_str())!=tinyxml2::XML_SUCCESS||!xml.RootElement())throw std::runtime_error("官能团结构数据损坏");
    auto root=xml.RootElement();int next=1;walk(root,[&](XMLElement* e){next=std::max(next,e->IntAttribute("id")+1);});
    for(int iteration=0;iteration<1024;++iteration){
        walk(root,[&](XMLElement* e){if(std::string(e->Name())!="n")return;if(auto wrapper=e->FirstChildElement("nickname")){for(auto f=wrapper->FirstChildElement("fragment");f;f=f->NextSiblingElement("fragment"))e->InsertEndChild(f->DeepClone(&xml));e->DeleteChild(wrapper);}});
        XMLElement* node=nullptr;
        walk(root,[&](XMLElement* e){if(std::string(e->Name())=="n"&&e->FirstChildElement("fragment"))node=e;});
        if(!node){
            // Imported shorthand may omit its embedded definition.
            walk(root,[&](XMLElement* e){if(node||std::string(e->Name())!="n")return;auto name=label(e);auto n=nickdata::find(name);auto a=nickdata::findAlias(name);auto type=e->Attribute("NodeType");if(!n&&!a)return;if(!type&&e->IntAttribute("Element",6)!=6)return;
                tinyxml2::XMLDocument def;if(def.Parse(n?n->fragment:a->fragment)!=tinyxml2::XML_SUCCESS)return;auto f=def.RootElement();if(!f||(std::string(f->Name())!="fragment"&&std::string(f->Name())!="nickname"))return;
                auto copy=f->DeepClone(&xml)->ToElement();std::map<int,int> ids;walk(copy,[&](XMLElement* c){if(c->Attribute("id"))ids[c->IntAttribute("id")]=next++;});remap(copy,ids);e->InsertEndChild(copy);e->SetAttribute("NodeType","Nickname");node=e;
            });
            if(node&&node->FirstChildElement("nickname"))continue;
        }
        if(!node){bool unknown=false;walk(root,[&](XMLElement* e){if(std::string(e->Name())!="n")return;auto type=e->Attribute("NodeType");if(type&&(std::string(type)=="Nickname"||std::string(type)=="GenericNickname"||std::string(type)=="ExternalConnectionPoint"))unknown=true;});if(unknown)throw std::runtime_error("结构包含未定义官能团或连接点，已保留原结构");tinyxml2::XMLPrinter p;xml.Print(&p);return p.CStr();}
        auto f=node->FirstChildElement("fragment");auto parent=node->Parent()->ToElement();
        std::vector<XMLElement*> external,incident,connections;std::map<int,XMLElement*> nodes;
        for(auto a=f->FirstChildElement("n");a;a=a->NextSiblingElement("n")){nodes[a->IntAttribute("id")]=a;if(a->Attribute("NodeType")&&std::string(a->Attribute("NodeType"))=="ExternalConnectionPoint")external.push_back(a);}
        int nodeId=node->IntAttribute("id");for(auto b=parent->FirstChildElement("b");b;b=b->NextSiblingElement("b"))if(b->IntAttribute("B")==nodeId||b->IntAttribute("E")==nodeId)incident.push_back(b);
        if(external.empty()&&incident.empty()){
            for(auto def=node->FirstChildElement("fragment");def;def=def->NextSiblingElement("fragment"))for(auto child=def->FirstChildElement();child;child=child->NextSiblingElement())parent->InsertEndChild(child->DeepClone(&xml));
            parent->DeleteChild(node);continue;
        }
        if(external.size()!=1||incident.size()>1)throw std::runtime_error("该官能团须有一个连接点，且最多连接一根外部键");
        auto ext=external[0];int extId=ext->IntAttribute("id");
        for(auto b=f->FirstChildElement("b");b;b=b->NextSiblingElement("b"))if(b->IntAttribute("B")==extId||b->IntAttribute("E")==extId)connections.push_back(b);
        if(connections.size()!=1)throw std::runtime_error("官能团连接点定义不完整");auto link=connections[0];
        int anchorId=link->IntAttribute("B")==extId?link->IntAttribute("E"):link->IntAttribute("B");
        if(!nodes.count(anchorId))throw std::runtime_error("官能团缺少连接原子");
        auto origin=point(node),anchor=point(nodes[anchorId]),endpoint=point(ext);double angle=0,scale=1;
        std::vector<double> lengths;for(auto b=f->FirstChildElement("b");b;b=b->NextSiblingElement("b")){auto a=nodes.find(b->IntAttribute("B")),z=nodes.find(b->IntAttribute("E"));if(a==nodes.end()||z==nodes.end())throw std::runtime_error("官能团化学键端点缺失");auto p=point(a->second),q=point(z->second);double d=std::hypot(p.first-q.first,p.second-q.second);if(d>1e-6)lengths.push_back(d);}
        if(!lengths.empty()){std::sort(lengths.begin(),lengths.end());scale=root->DoubleAttribute("BondLength",14.4)/lengths[lengths.size()/2];}
        if(!incident.empty()){int other=incident[0]->IntAttribute("B")==nodeId?incident[0]->IntAttribute("E"):incident[0]->IntAttribute("B");XMLElement* neighbor=nullptr;for(auto n=parent->FirstChildElement("n");n;n=n->NextSiblingElement("n"))if(n->IntAttribute("id")==other)neighbor=n;if(!neighbor)throw std::runtime_error("外部化学键端点缺失");auto p=point(neighbor);angle=std::atan2(origin.second-p.second,origin.first-p.first)-std::atan2(anchor.second-endpoint.second,anchor.first-endpoint.first);}
        std::map<int,int> ids;walk(f,[&](XMLElement* e){if(e->Attribute("id"))ids[e->IntAttribute("id")]=e->IntAttribute("id")==anchorId?nodeId:next++;});
        if(!incident.empty())ids[link->IntAttribute("id")]=incident[0]->IntAttribute("id");
        f->DeleteChild(ext);f->DeleteChild(link);remap(f,ids);
        walk(f,[&](XMLElement* e){if(std::string(e->Name())=="n"&&e->Attribute("p")){auto p=point(e);double x=(p.first-anchor.first)*scale,y=(p.second-anchor.second)*scale;std::ostringstream out;out.precision(17);out<<origin.first+x*std::cos(angle)-y*std::sin(angle)<<' '<<origin.second+x*std::sin(angle)+y*std::cos(angle);e->SetAttribute("p",out.str().c_str());e->DeleteAttribute("xyz");}});
        // Clone before deleting the placeholder, which owns the fragment.
        for(auto child=f->FirstChildElement();child;child=child->NextSiblingElement())parent->InsertEndChild(child->DeepClone(&xml));
        parent->DeleteChild(node);
    }
    throw std::runtime_error("官能团定义循环或嵌套过深");
}
}
