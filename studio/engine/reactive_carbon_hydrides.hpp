#pragma once
#include "hydride_labels.hpp"
#include "atom_runs.hpp"
#include "third_party/tinyxml2/tinyxml2.h"
namespace studio_carbon_hydrides {
// Convert only explicit elemental carbon hydrides at the drawn reaction sites.
// A stored fragment must prove that it contains no other hidden heavy atoms.
inline std::set<r62::core::ObjectId> normalize(r62::core::Document& doc,const std::vector<r62::arrows::ArrowParams>& inputs){
    using namespace r62::core;std::set<ObjectId> sites,changed;
    for(const auto& p:inputs)for(auto ref:{p.electronSource,p.electronTarget}){
        if(ref.kind==ObjectKind::Atom)sites.insert(ref.id);
        else if(ref.kind==ObjectKind::Bond)if(auto b=doc.bonds().alive(ref.id)){sites.insert(b->beginAtom);sites.insert(b->endAtom);}
    }
    for(auto id:sites){auto a=doc.atoms().alive(id);if(!a||a->element!=6)continue;int h=studio::hydride_labels::count(6,doc.atomLabel(id));if(h<1||h>4)continue;
        auto& attrs=doc.extensions(ObjectRef(ObjectKind::Atom,id));bool valid=true;
        for(auto key:{"studio.nickname.fragment","studio.compound.fragment"})if(auto raw=attrs.findValue(key)){
            tinyxml2::XMLDocument xml;if(xml.Parse(raw->c_str())!=tinyxml2::XML_SUCCESS||!xml.RootElement()){valid=false;break;}
            int carbon=0,ports=0;for(auto n=xml.RootElement()->FirstChildElement("n");n;n=n->NextSiblingElement("n")){
                if(std::string(n->Attribute("NodeType")?n->Attribute("NodeType"):"")=="ExternalConnectionPoint"){++ports;continue;}
                if(n->IntAttribute("Element",6)!=6||n->IntAttribute("NumHydrogens",h)!=h){valid=false;break;}++carbon;
            }
            if(carbon!=1||ports>1)valid=false;
        }
        if(!valid)continue;auto sem=a->semantics;sem.nodeType=uint8_t(NodeType::Element);sem.implicitHydrogens=h;sem.showLabel=true;sem.aromatic=false;
        doc.setAtomLabel(id,"C");doc.setAtomSemantics(id,sem);studioatom::set(doc,id,{});
        for(auto key:{"studio.nickname.fragment","studio.compound.fragment","studio.label.text","pst.attr.MoleculeStudioLiteralLabel"})doc.extensions(ObjectRef(ObjectKind::Atom,id)).erase(key);
        changed.insert(id);
    }return changed;
}
}
