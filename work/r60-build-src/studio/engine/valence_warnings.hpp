#pragma once
#include "next/core/document.hpp"
#include "next/core/commands.hpp"
#include "hydride_labels.hpp"
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
struct Assessment {
    int element=0,used=0,limit=-1;
    bool exceeded()const{return limit>=0&&used>limit;}
};
inline Assessment assess(const Document& doc,const AtomRecord& atom) {
    Assessment result;
    if(!atom.isAlive())return result;
    const auto label=doc.atomLabel(atom.id);
    const bool legacyOH=hydride_labels::legacyHydroxyl(atom,label);
    if(!legacyOH&&atom.semantics.nodeType!=uint8_t(NodeType::Element))return result;
    const int element=legacyOH?8:atom.element;
    // Plain Ar is also the conventional aryl placeholder. Legacy/imported
    // drawings can encode it as element 18, so do not diagnose that ambiguous
    // label using argon's zero valence. Explicit isotope/charge stays elemental.
    if(atom.semantics.isotope==0&&atom.semantics.charge==0&&
       (label=="Ar"||(label.empty()&&atom.element==18)))return result;
    const int textH=hydride_labels::count(element,label);
    if(!label.empty()&&label!=elementSymbol(element)&&textH<0)return result;
    if(legacyOH&&doc.extensions(ObjectRef(ObjectKind::Atom,atom.id)).findValue("pst.attr.MoleculeStudioLiteralLabel"))return result;
    int limit=maximumValence(element,atom.semantics.charge);
    if(limit<0)return result;
    int sum=std::max(0,std::max(textH,int(atom.semantics.implicitHydrogens))),aromatic=0;
    // The neutral central halogen in a conventional oxyanion drawing can
    // have a bond-order sum up to seven, e.g. O=I(=O)(=O)O-.
    bool onlyOxygen=sum==0&&atom.semantics.charge==0&&(element==17||element==35||element==53);
    bool hasOxoBond=false;
    for(auto id:doc.incidentBonds(atom.id)) {
        const auto* bond=doc.bonds().alive(id);if(!bond)continue;
        // Count the known covalent valence even if some other edges have
        // noncovalent or indeterminate order. Their presence cannot undo an
        // excess already established by the known bonds.
        if(bond->semantics!=BondSemantics::Normal&&bond->semantics!=BondSemantics::Aromatic)continue;
        // Kekule single/double orders remain exact even when marked aromatic.
        if(bond->order==BondOrder::Aromatic){++sum;++aromatic;}
        else if(bond->order>=BondOrder::Single&&bond->order<=BondOrder::Quadruple)sum+=int(bond->order);
        if(onlyOxygen){
            const auto* neighbor=doc.atoms().alive(bond->beginAtom==atom.id?bond->endAtom:bond->beginAtom);
            onlyOxygen=neighbor&&neighbor->element==8&&neighbor->semantics.nodeType==uint8_t(NodeType::Element)&&
                bond->semantics==BondSemantics::Normal&&(bond->order==BondOrder::Single||bond->order==BondOrder::Double);
            hasOxoBond|=onlyOxygen&&bond->order==BondOrder::Double;
        }
    }
    if(onlyOxygen&&hasOxoBond)limit=7;
    // Aromatic carbon has one additional pi valence. For heteroatoms use
    // the lower bound: lone-pair donors must not be mistaken for excess bonds.
    if(aromatic>=2&&atom.element==6&&atom.semantics.charge==0)++sum;
    result.element=element;result.used=sum;result.limit=limit;
    return result;
}
inline bool overValent(const Document& doc,const AtomRecord& atom) {
    return assess(doc,atom).exceeded();
}
inline std::vector<ObjectId> overValentAtoms(const Document& doc) {
    std::vector<ObjectId> ids;
    for(const auto& atom:doc.atoms().records())if(!ignored(doc,atom.id)&&overValent(doc,atom))ids.push_back(atom.id);
    return ids;
}
} }
