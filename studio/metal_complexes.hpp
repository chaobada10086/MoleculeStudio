#pragma once
#include "engine/metal_labels.hpp"
namespace metal_complexes {
using namespace r62;using namespace core;
struct Complex{int element=0;std::vector<std::string> ligands;bool valid()const{return element&& !ligands.empty();}};
inline Complex parse(std::string label){
    const char* digits[]={"₀","₁","₂","₃","₄","₅","₆","₇","₈","₉"};for(int n=0;n<10;++n){size_t p;while((p=label.find(digits[n]))!=std::string::npos)label.replace(p,std::string(digits[n]).size(),1,char('0'+n));}size_t sub;while((sub=label.find("ₙ"))!=std::string::npos)label.replace(sub,3,"n");
    auto span=studio::metal_labels::find(label);Complex result;if(!span.valid())return result;result.element=elementNumber(label.substr(span.start,span.size));
    const std::string rest=label.substr(0,span.start)+label.substr(span.start+span.size);
    static const char* tokens[]={"P(tBu)3","PPh3","PPh2R","PCy3","PMe3","PEt3","MeCN","NCMe","H2O","NH3","OTf","OAc","THF","acac","phen","bpy","COD","dba","Cp*","Cp","Cl","Br","CO","CN","OH","Ph","Me","Et","Ar","Ln","py","F","I","H","R","L"};
    for(size_t p=0;p<rest.size();){if(rest[p]==' '){++p;continue;}std::string ligand;
        if(rest[p]=='('){size_t start=++p;int depth=1;while(p<rest.size()&&depth){if(rest[p]=='(')++depth;if(rest[p]==')')--depth;if(depth)++p;}if(depth)return Complex();ligand=rest.substr(start,p-start);++p;bool known=false;for(auto token:tokens)known|=ligand==token;if(!known)return Complex();}
        else {for(auto token:tokens)if(rest.compare(p,std::string(token).size(),token)==0){ligand=token;break;}if(ligand.empty())return Complex();p+=ligand.size();}
        int count=0;while(p<rest.size()&&rest[p]>='0'&&rest[p]<='9'){count=count*10+rest[p++]-'0';if(count>8)return Complex();}if(!count)count=1;for(int i=0;i<count;++i)result.ligands.push_back(ligand);if(result.ligands.size()>12)return Complex();
    }return result;
}
inline bool eligible(const Document& doc,ObjectId id){
    const auto& ext=doc.extensions(ObjectRef(ObjectKind::Atom,id));if(ext.findValue("studio.compound.fragment")||ext.findValue("studio.nickname.fragment"))return false;
    return parse(doc.atomLabel(id)).valid();
}
inline Error graph(const Document& source,ObjectId target,Document& graph,ObjectId* anchor){
    auto complex=parse(source.atomLabel(target));if(!complex.valid())return Error(ErrorCode::InvalidArgument,"配体组成无法明确识别，已保留原标签。");
    graph.setDefaultBondLength(source.defaultBondLength());const auto origin=source.atoms().alive(target)->position;
    auto error=graph.addAtom(origin,complex.element,anchor);if(!error.ok())return error;
    auto sem=graph.atoms().alive(*anchor)->semantics;sem.implicitHydrogens=0;sem.charge=source.atoms().alive(target)->semantics.charge;sem.showLabel=true;graph.setAtomSemantics(*anchor,sem);graph.setAtomLabel(*anchor,elementSymbol(complex.element));
    std::vector<double> neighbours;for(auto bid:source.incidentBonds(target)){auto bond=source.bonds().alive(bid);neighbours.push_back(core::angleOf(source.atoms().alive(bond->beginAtom==target?bond->endAtom:bond->beginAtom)->position-origin));}
    std::vector<double> angles;const size_t total=complex.ligands.size()+neighbours.size();double rotation=neighbours.empty()?-90.:neighbours.front();
    for(size_t i=neighbours.empty()?0:1;angles.size()<complex.ligands.size();++i)angles.push_back(rotation+i*360./std::max(size_t(3),total));
    for(size_t i=0;i<complex.ligands.size();++i){const auto& ligand=complex.ligands[i];int element=elementNumber(ligand);ObjectId atom,bond;
        error=graph.addAtom(origin+core::direction(angles[i])*source.defaultBondLength(),element?element:6,&atom);if(!error.ok())return error;
        if(element){auto value=graph.atoms().alive(atom)->semantics;value.implicitHydrogens=0;value.showLabel=true;graph.setAtomSemantics(atom,value);graph.setAtomLabel(atom,ligand);}
        else {CommandContext context(graph);auto applied=nicknames::labelCommand(atom,ligand).execute(context);if(!applied.ok())return applied.error;graph.setAtomLabel(atom,ligand);if(ligand=="L"||ligand=="Ln"||ligand=="R"||ligand=="Ar"){auto value=graph.atoms().alive(atom)->semantics;value.nodeType=uint8_t(NodeType::Query);value.showLabel=true;value.implicitHydrogens=0;graph.setAtomSemantics(atom,value);}TextRun run;run.utf8=ligand;run.flags=kFormula;studioatom::set(graph,atom,studio::metal_labels::scripted({run}));}
        error=graph.addBond(*anchor,atom,&bond);if(!error.ok())return error;
    }return Error();
}
}
