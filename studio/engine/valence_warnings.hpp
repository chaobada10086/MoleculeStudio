#pragma once
#include "next/core/document.hpp"
#include "next/core/commands.hpp"
#include <algorithm>
#include <vector>

namespace studio { namespace validation {
using namespace r62::core;
inline bool ignored(const Document& doc,ObjectId id) {
    const auto* value=doc.extensions(ObjectRef(ObjectKind::Atom,id)).findValue("studio.validation.ignore-valence");
    return value&&*value=="1";
}
inline Command setIgnored(ObjectId id,bool ignore) {
    return Command(ignore?"忽略原子价态错误":"恢复原子价态检查",kInvalidateAppearance,[id,ignore](CommandContext& c){
        if(!c.document.atoms().alive(id))return CommandResult::fail(ErrorCode::NotAlive,"Atom is deleted");
        auto& bag=c.document.extensions(ObjectRef(ObjectKind::Atom,id));
        if(ignore)bag.set("studio.validation.ignore-valence","1");else bag.erase("studio.validation.ignore-valence");
        return CommandResult::success();
    });
}
// Document-space dimensions: square and stroke scale with the molecule.
inline Box warningSquare(const Box& bounds) {
    Vec2 center=(bounds.min+bounds.max)*.5;
    double radius=std::max(3.5,std::max(bounds.max.x-bounds.min.x,bounds.max.y-bounds.min.y)*.5+1.25);
    return Box(center-Vec2(radius,radius),center+Vec2(radius,radius));
}
// Conservative main-group maxima, following RDKit Book's allowed-valence table:
// https://www.rdkit.org/docs/RDKit_Book.html#valence-calculation-and-allowed-valences
// Unknown elements/charge states and non-element labels are not diagnosed.
inline int maximumValence(int element,int charge) {
    if(charge) {
        if(element==6 && (charge==1||charge==-1))return 3;
        if((element==15||element==33)&&charge==-2)return 5;
        if((element==16||element==34)&&charge==-1)return 5;
        if(charge < -1 || charge > 1)return -1;
        switch(element) {
        case 5:case 7:case 8:case 9:case 14:case 15:case 16:case 17:
        case 33:case 34:case 35:case 53:break;
        default:return -1;
        }
        element-=charge;
    }
    switch(element) {
    case 1:case 9:case 17:case 35:return 1;
    case 5:return 3;
    case 6:case 14:case 32:return 4;
    case 7:return 3;
    case 8:return 2;
    case 15:case 33:case 51:case 53:case 85:return 5;
    case 16:case 34:case 52:case 54:return 6;
    case 2:case 10:case 18:case 36:case 86:return 0;
    default:return -1;
    }
}
inline bool overValent(const Document& doc,const AtomRecord& atom) {
    if(!atom.isAlive()||atom.semantics.nodeType!=uint8_t(NodeType::Element))return false;
    const auto label=doc.atomLabel(atom.id);
    // Plain Ar is also the conventional aryl placeholder. Legacy/imported
    // drawings can encode it as element 18, so do not diagnose that ambiguous
    // label using argon's zero valence. Explicit isotope/charge stays elemental.
    if(atom.semantics.isotope==0&&atom.semantics.charge==0&&
       (label=="Ar"||(label.empty()&&atom.element==18)))return false;
    if(!label.empty()&&label!=elementSymbol(atom.element))return false;
    int limit=maximumValence(atom.element,atom.semantics.charge);
    if(limit<0)return false;
    int sum=std::max(0,int(atom.semantics.implicitHydrogens)),aromatic=0;
    for(auto id:doc.incidentBonds(atom.id)) {
        const auto* bond=doc.bonds().alive(id);if(!bond)continue;
        if(bond->semantics==BondSemantics::Hydrogen)continue;
        if(bond->semantics!=BondSemantics::Normal&&bond->semantics!=BondSemantics::Aromatic)return false;
        // Kekule single/double orders remain exact even when marked aromatic.
        if(bond->order==BondOrder::Aromatic){++sum;++aromatic;}
        else if(bond->order>=BondOrder::Single&&bond->order<=BondOrder::Quadruple)sum+=int(bond->order);
        else return false;
    }
    // Aromatic carbon has one additional pi valence. For heteroatoms use
    // the lower bound: lone-pair donors must not be mistaken for excess bonds.
    if(aromatic>=2&&atom.element==6&&atom.semantics.charge==0)++sum;
    return sum>limit;
}
inline std::vector<ObjectId> overValentAtoms(const Document& doc) {
    std::vector<ObjectId> ids;
    for(const auto& atom:doc.atoms().records())if(!ignored(doc,atom.id)&&overValent(doc,atom))ids.push_back(atom.id);
    return ids;
}
} }
