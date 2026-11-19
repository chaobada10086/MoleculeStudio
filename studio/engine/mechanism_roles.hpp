#pragma once
#include "next/core/document.hpp"

namespace r62 { namespace mechanism {
inline const char* genericRoleKey(){return "pst.attr.MoleculeStudioRole";}
inline std::string genericRole(const core::Document& doc,core::ObjectId id){
    auto a=doc.atoms().alive(id);if(!a||a->semantics.nodeType!=uint8_t(core::NodeType::Query))return "";
    auto value=doc.extensions(core::ObjectRef(core::ObjectKind::Atom,id)).findValue(genericRoleKey());
    if(!value)return "";
    if(*value=="base"&&doc.atomLabel(id)=="B")return *value;
    if((*value=="nucleophile"||*value=="nucleophile-neutral")&&(doc.atomLabel(id)=="Nu"||doc.atomLabel(id)=="Nu:"))return *value;
    return "";
}
inline int genericRoleValence(const std::string& role,int charge){
    if(role=="nucleophile-neutral")return charge==0?0:charge==1?1:-1;
    return (role=="base"||role=="nucleophile")?(charge==-1?0:charge==0?1:-1):-1;
}
// Explicit user action only. Typing elemental B never invokes this conversion.
inline core::Error setGenericRole(core::Document& doc,core::ObjectId id,const std::string& role){
    auto a=doc.atoms().alive(id);
    if(!a||(role!="base"&&role!="nucleophile"&&role!="nucleophile-neutral"))return core::Error(core::ErrorCode::InvalidArgument,"通用试剂类型无效。");
    if(!doc.incidentBonds(id).empty())return core::Error(core::ErrorCode::InvalidArgument,"请选中一个未成键的原子作为通用试剂，或取消选择后插入。");
    auto semantics=a->semantics;semantics.nodeType=uint8_t(core::NodeType::Query);semantics.charge=role=="nucleophile-neutral"?0:-1;semantics.implicitHydrogens=0;semantics.radical=0;semantics.isotope=0;semantics.aromatic=false;semantics.showLabel=true;
    // Core storage requires a real element number, but Query node type and an
    // explicit role ensure this placeholder is never a molecular formula atom.
    auto error=doc.setElement(id,6);if(!error.ok())return error;
    error=doc.setAtomSemantics(id,semantics);if(!error.ok())return error;
    error=doc.setAtomLabel(id,role=="base"?"B":role=="nucleophile-neutral"?"Nu:":"Nu");if(!error.ok())return error;
    auto& bag=doc.extensions(core::ObjectRef(core::ObjectKind::Atom,id));
    auto entries=bag.entries();for(const auto& e:entries)if(e.key=="studio.nickname.fragment"||e.key=="studio.atom.runs"||e.key=="studio.label.text"||e.key=="studio.hydrogens.manual"||e.key.find("pst.attr.")==0)bag.erase(e.key,e.sourceId);
    bag.set(genericRoleKey(),role);return core::Error();
}
} }
