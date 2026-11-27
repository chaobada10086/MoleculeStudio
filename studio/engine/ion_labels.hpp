#pragma once
#include "next/core/document.hpp"
#include <string>
#include <vector>
#include <algorithm>
namespace studioions {
struct Entry {const char* label;const char* aliases;int element,hydrogens,hidden;};
inline const std::vector<Entry>& entries(){
    static const std::vector<Entry> values={
        {"OH","oh ho",8,1,0},{"OMe","ome meo ch3o och3",8,0,1},
        {"OEt","oet eto c2h5o oc2h5",8,0,1},{"OtBu","otbu tbuo",8,0,1},
        {"OiPr","oipr ipro",8,0,1},{"OBn","obn bno",8,0,1},{"OPh","oph pho",8,0,1},
        {"OAc","oac aco ch3co2",8,0,1},{"OTs","ots tso",8,0,1},{"OTf","otf tfo",8,0,1},
        {"NH2","nh2 h2n",7,2,0},{"NHMe","nhme menh",7,1,1},
        {"NMe2","nme2 me2n",7,0,2},{"NEt2","net2 et2n",7,0,2},
        {"SH","sh hs",16,1,0},{"SMe","sme mes",16,0,1},{"SPh","sph phs",16,0,1},
        {"CN","cn",6,0,3},{"N3","n3",7,0,2},{"SCN","scn",16,0,1},
        {"H","h",1,0,0},{"F","f",9,0,0},{"Cl","cl",17,0,0},
        {"Br","br",35,0,0},{"I","i",53,0,0},
        {"Li","li",3,0,0},{"Na","na",11,0,0},{"K","k",19,0,0},
        {"NH4","nh4",7,4,0},{"H3O","h3o oh3",8,3,0},
        {"NMe4","nme4 me4n",7,0,4},{"NEt4","net4 et4n",7,0,4},
        {"NBu4","nbu4 bu4n",7,0,4},{"NHEt3","nhet3 et3nh",7,1,3},
        {"NHMe3","nhme3 me3nh",7,1,3}
    };return values;
}
inline std::string ascii(std::string value){
    for(const auto& pair:std::vector<std::pair<std::string,std::string>>{{"−","-"},{"⁻","-"},{"⁺","+"},{"₀","0"},{"₁","1"},{"₂","2"},{"₃","3"},{"₄","4"},{"₅","5"},{"₆","6"}}){
        size_t at=0;while((at=value.find(pair.first,at))!=std::string::npos){value.replace(at,pair.first.size(),pair.second);at+=pair.second.size();}
    }
    return value;
}
inline const Entry* find(const std::string& value,int* charge=nullptr,bool* explicitCharge=nullptr){
    auto text=ascii(value);int q=0;bool explicitQ=false;
    if(!text.empty()&&(text.back()=='+'||text.back()=='-')){q=text.back()=='+'?1:-1;explicitQ=true;text.pop_back();}
    // MeS has sulfur at the attachment point. Mes and MES denote other structures.
    if(text=="MeS")text="SMe";
    else if(text=="Mes"||text=="MES"||text=="mes")return nullptr;
    text.erase(std::remove(text.begin(),text.end(),'-'),text.end());
    for(auto& c:text)if(c>='A'&&c<='Z')c=char(c+32);
    for(const auto& entry:entries()){
        std::string words=std::string(" ")+entry.aliases+" ";
        if(!text.empty()&&words.find(" "+text+" ")!=std::string::npos){
            if(charge)*charge=q;if(explicitCharge)*explicitCharge=explicitQ;return &entry;
        }
    }
    return nullptr;
}
inline int hiddenValence(const r62::core::Document& doc,r62::core::ObjectId id){
    auto value=doc.extensions(r62::core::ObjectRef(r62::core::ObjectKind::Atom,id)).findValue("studio.mechanism.compact.valence");
    return value?std::max(0,std::atoi(value->c_str())):0;
}
inline bool compact(const r62::core::Document& doc,r62::core::ObjectId id){
    return doc.extensions(r62::core::ObjectRef(r62::core::ObjectKind::Atom,id)).findValue("studio.mechanism.ion.base")!=nullptr;
}
inline r62::core::Error apply(r62::core::Document& doc,r62::core::ObjectId id,const Entry& entry,int charge){
    using namespace r62::core;
    const auto* atom=doc.atoms().alive(id);if(!atom)return Error(ErrorCode::NotFound,"离子原子不存在");
    auto sem=atom->semantics;sem.nodeType=uint8_t(NodeType::Element);sem.charge=charge;
    sem.showLabel=true;sem.radical=0;sem.aromatic=false;sem.implicitHydrogens=entry.hydrogens;
    if(charge==-1&&!entry.hidden){
        int external=0;for(auto bid:doc.incidentBonds(id))external+=int(doc.bonds().alive(bid)->order);
        sem.implicitHydrogens=std::max(0,entry.hydrogens-external);
    }
    auto error=doc.setElement(id,entry.element);if(error.ok())error=doc.setAtomSemantics(id,sem);
    if(error.ok())error=doc.setAtomLabel(id,!entry.hidden&&entry.hydrogens>0&&sem.implicitHydrogens!=entry.hydrogens?elementSymbol(entry.element):entry.label);
    auto& bag=doc.extensions(ObjectRef(ObjectKind::Atom,id));
    bag.set("studio.mechanism.ion.base",entry.label);
    bag.set("studio.mechanism.compact.valence",std::to_string(entry.hidden));
    bag.erase("studio.atom.runs");bag.erase("studio.label.text");bag.erase("pst.attr.MoleculeStudioLiteralLabel");
    if(charge)bag.set("pst.attr.MoleculeStudioChargePosition","auto");
    else{bag.erase("pst.attr.MoleculeStudioChargePosition");bag.erase("pst.attr.MoleculeStudioChargeCircle");}
    return error;
}
}
