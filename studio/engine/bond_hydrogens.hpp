#pragma once
#include "valence_warnings.hpp"

namespace studio { namespace bond_hydrogens {
using namespace r62::core;

inline bool neutralOxygen(const AtomRecord& atom){
    return !atom.semantics.charge&&!atom.semantics.radical&&!atom.semantics.aromatic;
}
inline bool legacyHydroxyl(const AtomRecord& atom,const std::string& label){
    if(label!="OH"&&label!="HO")return false;
    const auto type=static_cast<NodeType>(atom.semantics.nodeType);
    if(type==NodeType::Element)return atom.element==6&&!atom.semantics.isotope;
    return hydride_labels::legacyHydroxyl(atom,label)||
           (type==NodeType::Query&&(atom.element==6||atom.element==8));
}

// Run after the bonds are available. Condensed H can be left behind by
// older editors, or by entering OH on an already bonded oxygen.
inline Error repairCondensedHydroxyl(Document& d,ObjectId id,bool& repaired){
    const auto* atom=d.atoms().alive(id);
    if(!atom||!neutralOxygen(*atom))return Error::success();
    const auto label=d.atomLabel(id);
    const bool legacy=legacyHydroxyl(*atom,label);
    if(!legacy&&(atom->element!=8||atom->semantics.nodeType!=uint8_t(NodeType::Element)||
       (label!="OH"&&label!="HO"&&
        (atom->semantics.implicitHydrogens!=1||(label!="O"&&!label.empty())))))return Error::success();

    const auto bonds=d.incidentBonds(id);
    if(bonds.size()!=2)return Error::success();
    for(auto bid:bonds){
        const auto* bond=d.bonds().alive(bid);
        if(!bond||bond->semantics!=BondSemantics::Normal||bond->order!=BondOrder::Single)return Error::success();
        const auto* neighbor=d.atoms().alive(bond->beginAtom==id?bond->endAtom:bond->beginAtom);
        if(!neighbor||neighbor->element==1)return Error::success();
    }

    auto sem=atom->semantics;
    sem.implicitHydrogens=0;
    if(legacy){
        auto e=d.setElement(id,8);
        if(!e.ok())return e;
        sem.nodeType=uint8_t(NodeType::Element);
        sem.showLabel=true;
    }
    auto e=d.setAtomSemantics(id,sem);
    if(!e.ok())return e;
    if(!label.empty()&&label!="O"){
        e=d.setAtomLabel(id,"O");
        if(!e.ok())return e;
    }
    auto& bag=d.extensions(ObjectRef(ObjectKind::Atom,id));
    if(!label.empty()&&label!="O"){
        bag.erase("studio.atom.runs");
        bag.erase("studio.label.text");
    }
    if(legacy){
        bag.erase("studio.nickname.fragment");
        bag.erase("cdxml.attr.r62ChemLabel");
    }
    bag.erase("studio.mechanism.ion.base");
    bag.erase("studio.mechanism.compact.valence");
    repaired=true;
    return Error::success();
}
inline Error repairCondensedHydroxyls(Document& d,bool& repaired){
    for(const auto& atom:d.atoms().records())if(atom.isAlive()){
        auto e=repairCondensedHydroxyl(d,atom.id,repaired);
        if(!e.ok())return e;
    }
    return Error::success();
}

inline int valence(const Document& d,ObjectId id){
    int sum=0;for(auto bid:d.incidentBonds(id)){auto b=d.bonds().alive(bid);if(b&&b->semantics==BondSemantics::Normal&&b->order>=BondOrder::Single&&b->order<=BondOrder::Triple)sum+=int(b->order);}return sum;
}
inline ObjectId terminalCarbonBond(const Document& d,ObjectId oxygen,BondOrder order){
    const auto bonds=d.incidentBonds(oxygen);
    if(bonds.size()!=1)return kInvalidObjectId;
    const auto* b=d.bonds().alive(bonds.front());
    if(!b||b->semantics!=BondSemantics::Normal||b->order!=order)return kInvalidObjectId;
    const auto* carbon=d.atoms().alive(b->beginAtom==oxygen?b->endAtom:b->beginAtom);
    return carbon&&carbon->element==6&&carbon->semantics.nodeType==uint8_t(NodeType::Element)?b->id:kInvalidObjectId;
}
struct Entry {ObjectId id;int element,h,valence;bool legacyHydroxyl,onlyRepair;std::string label;};
struct Carbonyl {ObjectId oxygen,bond;};
struct Before {
    std::vector<Entry> atoms;
    std::vector<Carbonyl> carbonyls;
    explicit Before(const Document& d){
        for(const auto& a:d.atoms().records())if(a.isAlive()){
            const auto label=d.atomLabel(a.id);
            const bool legacyOH=legacyHydroxyl(a,label);
            if(!legacyOH&&a.semantics.nodeType!=uint8_t(NodeType::Element))continue;
            const int element=legacyOH?8:a.element;
            const std::string symbol=elementSymbol(element);
            // Only exact elemental H labels are editable; COOH/OMe/Ns etc.
            // must remain structural abbreviations, not lose arbitrary text.
            const int textH=studio::hydride_labels::count(element,label);
            if(!label.empty()&&label!=symbol&&textH<0)continue;
            const bool literal=textH>=0&&d.extensions(ObjectRef(ObjectKind::Atom,a.id)).findValue("pst.attr.MoleculeStudioLiteralLabel");
            const bool hydroxyl=element==8&&neutralOxygen(a)&&(label=="OH"||label=="HO");
            if(literal&&!hydroxyl)continue;
            const int h=std::max(textH,int(a.semantics.implicitHydrogens));
            if(h<=0){
                if(a.element==8&&textH<0&&!a.semantics.charge&&!a.semantics.radical&&!a.semantics.aromatic){
                    auto bond=terminalCarbonBond(d,a.id,BondOrder::Double);
                    if(bond!=kInvalidObjectId)carbonyls.push_back({a.id,bond});
                }
                continue;
            }
            const bool onlyRepair=literal||(legacyOH&&!hydride_labels::legacyHydroxyl(a,label));
            atoms.push_back(Entry{a.id,element,std::min(h,8),valence(d,a.id),legacyOH,onlyRepair,label});
        }
    }
    Error apply(Document& d)const{
        for(const auto& old:atoms){
            const auto* a=d.atoms().alive(old.id);
            if(!a||(!old.legacyHydroxyl&&a->element!=old.element))continue;
            const int bonds=valence(d,old.id);
            const int used=bonds-old.valence;
            if(used<=0)continue;
            if(old.element==8){
                bool repaired=false;
                auto e=repairCondensedHydroxyl(d,old.id,repaired);
                if(!e.ok())return e;
                if(repaired)continue;
            }
            if(old.onlyRepair)continue;
            auto sem=a->semantics;
            int remaining=std::max(0,old.h-used);
            const int oxygenLimit=studio::validation::maximumValence(8,sem.charge);
            if(old.element==8&&oxygenLimit>=0)
                remaining=std::min(old.h,std::max(0,oxygenLimit-bonds));
            if(remaining==old.h&&!old.legacyHydroxyl)continue;
            sem.implicitHydrogens=remaining;
            if(old.legacyHydroxyl){
                sem.nodeType=uint8_t(NodeType::Element);
                auto e=d.setElement(old.id,old.element);if(!e.ok())return e;
            }
            auto e=d.setAtomSemantics(old.id,sem);if(!e.ok())return e;
            if(!old.label.empty()){e=d.setAtomLabel(old.id,elementSymbol(old.element));if(!e.ok())return e;}
            auto& bag=d.extensions(ObjectRef(ObjectKind::Atom,old.id));bag.erase("studio.atom.runs");bag.erase("studio.label.text");
            if(old.legacyHydroxyl){bag.erase("studio.nickname.fragment");bag.erase("cdxml.attr.r62ChemLabel");}
            if(remaining!=old.h){bag.erase("studio.mechanism.ion.base");bag.erase("studio.mechanism.compact.valence");}
        }
        for(const auto& old:carbonyls){
            const auto* oxygen=d.atoms().alive(old.oxygen);
            if(!oxygen||oxygen->element!=8||oxygen->semantics.nodeType!=uint8_t(NodeType::Element)||
               oxygen->semantics.charge||oxygen->semantics.radical||oxygen->semantics.implicitHydrogens>0||
               terminalCarbonBond(d,old.oxygen,BondOrder::Single)!=old.bond)continue;
            auto sem=oxygen->semantics;
            sem.implicitHydrogens=1;
            auto e=d.setAtomSemantics(old.oxygen,sem);
            if(!e.ok())return e;
        }
        return Error::success();
    }
};
inline bool editing(const std::string& tag){
    if(tag=="插入连烯")return true;
    return tag=="resolved bond"||tag=="bond.tool"||tag=="sprout bond"||tag=="click bond"||tag=="apply bond preset"||tag=="化学键级"||tag=="化学键属性"||tag=="hotspot bond order"||tag=="键显示"||tag=="rings_chains.ring.create"||tag=="rings_chains.chain.create"||tag=="稠合环"||tag=="添加结构片段";
}
} }
