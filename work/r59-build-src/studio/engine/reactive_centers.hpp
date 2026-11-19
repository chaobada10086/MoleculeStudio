#pragma once
#include "next/core/document.hpp"
namespace r62 { namespace mechanism {
inline bool cumuleneCenter(const core::Document& doc,const core::AtomRecord& atom){
    if(atom.element!=6||atom.semantics.showLabel||atom.semantics.charge||atom.semantics.radical||
       atom.semantics.nodeType!=uint8_t(core::NodeType::Element))return false;
    const auto& bonds=doc.incidentBonds(atom.id);
    if(bonds.size()!=2)return false;
    for(auto id:bonds){
        const auto* bond=doc.bonds().alive(id);
        if(!bond||bond->order!=core::BondOrder::Double||bond->semantics!=core::BondSemantics::Normal)return false;
    }
    return true;
}
inline bool singletCarbene(const core::AtomRecord& atom){
    return atom.element==6&&!atom.semantics.charge&&atom.semantics.radical==uint8_t(core::Radical::Singlet);
}
} }
