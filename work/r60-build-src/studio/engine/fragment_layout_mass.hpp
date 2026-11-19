#pragma once
#include "next/chemistry/edit.hpp"
#include "next/chemistry/element_data.hpp"
#include "nickname_catalog.hpp"
#include "compound_catalog.hpp"
#include "hydride_labels.hpp"
#include "third_party/tinyxml2/tinyxml2.h"
#include <map>
#include <set>
#include <vector>
#include <cmath>
namespace studio_fragment_mass {
using namespace r62;using namespace core;
inline double elemental(int element,int isotope=0){
    if(isotope){chemistry::IsotopeInfo info;if(chemistry::knownIsotope(element,isotope,&info))return info.exactMass;return double(isotope);}
    return std::max(0.,chemistry::elementInfo(element).weight);
}
inline int inferredHydrogens(int element,int charge,int radical,int sum,int degree,int aromatic){
    if(element==1)return 0;
    if(aromatic>=2&&element==6)return std::max(0,3-degree);
    int target=element==6?(charge?3:4):element==7?3+charge:element==8?2+charge:element==16?(sum<=2?2:sum<=4?4:6):element==15?(sum<=3?3:5):(element==9||element==17||element==35||element==53)?1:-1;
    if(target<0)return 0;
    target-=radical==int(Radical::Doublet)?1:radical==int(Radical::Singlet)?2:0;
    return std::max(0,target-sum);
}
inline double fragmentMass(tinyxml2::XMLElement* fragment,int depth=0){
    if(!fragment||depth>8)return 0;
    std::map<int,int> sums,degrees,aromatic;
    for(auto b=fragment->FirstChildElement("b");b;b=b->NextSiblingElement("b")){
        const auto order=b->Attribute("Order");const bool ar=order&&std::string(order)=="1.5";int val=ar?1:b->IntAttribute("Order",1);
        for(int id:{b->IntAttribute("B"),b->IntAttribute("E")}){sums[id]+=val;++degrees[id];aromatic[id]+=ar;}
    }
    double mass=0;
    for(auto n=fragment->FirstChildElement("n");n;n=n->NextSiblingElement("n")){
        if(n->Attribute("NodeType")&&std::string(n->Attribute("NodeType"))=="ExternalConnectionPoint")continue;
        if(auto nested=n->FirstChildElement("fragment")){mass+=fragmentMass(nested,depth+1);continue;}
        const int id=n->IntAttribute("id"),element=n->IntAttribute("Element",6);int h=n->IntAttribute("NumHydrogens",-1);
        if(h<0)h=inferredHydrogens(element,n->IntAttribute("Charge"),n->IntAttribute("Radical"),sums[id],degrees[id],aromatic[id]);
        mass+=elemental(element,n->IntAttribute("Isotope"))+std::max(0,h)*elemental(1);
    }return mass;
}
inline double embedded(const std::string& raw){
    static std::map<std::string,double> cache;auto found=cache.find(raw);if(found!=cache.end())return found->second;
    tinyxml2::XMLDocument xml;double mass=xml.Parse(raw.c_str())==tinyxml2::XML_SUCCESS?fragmentMass(xml.RootElement()):0;
    if(cache.size()>256)cache.clear();cache.emplace(raw,mass);return mass;
}
inline std::map<ObjectId,double> weights(const Document& doc){
    std::map<ObjectId,double> result;
    for(const auto& a:doc.atoms().records())if(a.isAlive()){
        const auto ref=ObjectRef(ObjectKind::Atom,a.id);const auto& bag=doc.extensions(ref);double mass=0;
        const auto label=doc.atomLabel(a.id);
        const bool hydride=studio::hydride_labels::count(a.element,label)>=0;
        const bool abbreviation=bag.findValue("studio.nickname.fragment")||bag.findValue("studio.compound.fragment")||
            (a.semantics.nodeType!=uint8_t(NodeType::Element))||
            (!label.empty()&&label!=elementSymbol(a.element)&&!hydride&&(nickdata::findAlias(label)||compounddata::find(label)));
        // Layout priority follows visible chemistry, never the hidden mass of Ph,
        // protecting groups or a collapsed compound. Formula/mass reporting uses
        // its independent chemical-composition implementation.
        if(abbreviation){result[a.id]=0;continue;}
        if(mass<=0){
            int h=a.semantics.implicitHydrogens;
            if(h<0){int sum=0,degree=0,aromatic=0;for(auto bid:doc.incidentBonds(a.id))if(auto b=doc.bonds().alive(bid)){++degree;if(b->order==BondOrder::Aromatic){++aromatic;++sum;}else sum+=int(b->order);}h=inferredHydrogens(a.element,a.semantics.charge,a.semantics.radical,sum,degree,aromatic);}
            mass=elemental(a.element,a.semantics.isotope)+(a.element==1?0:std::max(0,h))*elemental(1);
        }
        result[a.id]=mass;
    }return result;
}
inline double weight(const std::map<ObjectId,AtomRecord>& atoms,ObjectId id,const std::map<ObjectId,double>* values=nullptr){
    if(values){auto found=values->find(id);if(found!=values->end())return found->second;}
    const auto& a=atoms.at(id);if(a.semantics.nodeType!=uint8_t(NodeType::Element))return 0;return elemental(a.element,a.semantics.isotope)+(a.element==1?0:std::max(0,int(a.semantics.implicitHydrogens)))*elemental(1);
}
inline double total(const std::vector<ObjectId>& ids,const std::map<ObjectId,AtomRecord>& atoms,const std::map<ObjectId,double>* values=nullptr){
    double mass=0;for(auto id:ids)mass+=weight(atoms,id,values);return mass;
}
}
