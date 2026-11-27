#pragma once
#include "compound_catalog.hpp"
#include "engine/hydride_labels.hpp"
#include "engine/ion_labels.hpp"
#include "third_party/tinyxml2/tinyxml2.h"
#include <sstream>

// Keep the attachment atom and the remaining hydrogens inside a condensed label.
namespace studio_hydrogen {
using namespace r62::core;
struct Residue {std::string label,fragment;int element=0,remaining=0,hidden=0;};
inline bool resolve(const Document& doc,ObjectId host,bool front,Residue& out){
    const std::string label=doc.atomLabel(host);
    if(auto preset=compounddata::hydride(label)){
        out.label=preset->base;out.fragment=preset->fragment;out.element=preset->element;
        out.hidden=out.element==8?1:3;return true;
    }
    const auto& bag=doc.extensions(ObjectRef(ObjectKind::Atom,host));
    const std::string* stored=bag.findValue("studio.nickname.fragment");
    const auto* compound=compounddata::find(label);
    if(!stored&&!compound)return false;
    bool first=false;int count=studio::hydride_labels::terminal(label,&out.label,front,&first);
    if(!count)return false;
    tinyxml2::XMLDocument xml;
    if(xml.Parse(stored?stored->c_str():compound->fragment)!=tinyxml2::XML_SUCCESS)return false;
    auto root=xml.RootElement();if(!root)return false;
    tinyxml2::XMLElement *anchor=nullptr,*external=nullptr;
    int maxId=root->IntAttribute("id"),externalAnchor=-1;
    for(auto n=root->FirstChildElement();n;n=n->NextSiblingElement()){
        maxId=std::max(maxId,n->IntAttribute("id"));
        if(n->Attribute("NodeType","ExternalConnectionPoint")){if(external)return false;external=n;}
    }
    if(external)for(auto b=root->FirstChildElement("b");b;b=b->NextSiblingElement("b")){
        int a=b->IntAttribute("B"),z=b->IntAttribute("E"),id=external->IntAttribute("id");
        if(a==id||z==id)externalAnchor=a==id?z:a;
    }
    for(auto n=root->FirstChildElement("n");n;n=n->NextSiblingElement("n")){
        if(n==external||n->IntAttribute("NumHydrogens")!=count)continue;
        if(external && n->IntAttribute("id")!=externalAnchor)continue;
        const int z=n->IntAttribute("Element",6);const std::string symbol=elementSymbol(z);
        auto base=out.label;if(!first)while(!base.empty()&&base.back()>='0'&&base.back()<='9')base.pop_back();
        if(base.size()<symbol.size()||base.compare(first?0:base.size()-symbol.size(),symbol.size(),symbol))continue;
        if(anchor)return false;
        anchor=n;out.element=z;
    }
    if(!anchor)return false;
    const int anchorId=anchor->IntAttribute("id");out.hidden=0;
    for(auto b=root->FirstChildElement("b");b;b=b->NextSiblingElement("b")){
        int a=b->IntAttribute("B"),z=b->IntAttribute("E");
        if(a!=anchorId&&z!=anchorId)continue;
        if(external&&(a==external->IntAttribute("id")||z==external->IntAttribute("id")))continue;
        out.hidden+=b->IntAttribute("Order",1);
    }
    if(!external){
        double x=0,y=0;std::istringstream(anchor->Attribute("p")?anchor->Attribute("p"):"")>>x>>y;
        std::ostringstream pos;pos<<x+20<<" "<<y;
        auto port=xml.NewElement("n");port->SetAttribute("id",++maxId);port->SetAttribute("p",pos.str().c_str());
        port->SetAttribute("NodeType","ExternalConnectionPoint");root->InsertEndChild(port);
        auto b=xml.NewElement("b");b->SetAttribute("B",anchorId);b->SetAttribute("E",maxId);
        b->SetAttribute("id",++maxId);b->SetAttribute("Order",1);root->InsertEndChild(b);
    }
    out.remaining=count-1;anchor->SetAttribute("NumHydrogens",out.remaining);
    if(out.remaining){std::string h="H";if(out.remaining>1)h+=std::to_string(out.remaining);out.label=first?h+out.label:out.label+h;}
    tinyxml2::XMLPrinter printer;root->Accept(&printer);out.fragment=printer.CStr();return true;
}
inline Error apply(Document& doc,ObjectId host,const Residue& residue,int charge){
    studioions::Entry group={residue.label.c_str(),"",residue.element,residue.remaining,residue.hidden};
    auto e=studioions::apply(doc,host,group,charge);if(!e.ok())return e;
    auto sem=doc.atoms().alive(host)->semantics;sem.nodeType=uint8_t(NodeType::Nickname);
    e=doc.setAtomSemantics(host,sem);if(!e.ok())return e;
    auto& bag=doc.extensions(ObjectRef(ObjectKind::Atom,host));
    bag.erase("studio.compound.fragment");bag.set("studio.nickname.fragment",residue.fragment);
    return e;
}
}
