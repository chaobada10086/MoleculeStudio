#pragma once
#include "engine/hydride_labels.hpp"
#include "next/integration/session.hpp"
#include "next/chemistry/valence.hpp"
#include <queue>
#include <map>
#include <cctype>
#include <sstream>
#include "nickname_catalog.hpp"
#include "bridge_catalog.hpp"
#include "compound_catalog.hpp"
#include "nickname_expand.hpp"
#include "next/persistence/persistence.hpp"
#include <iomanip>
namespace studio {
using namespace r62; using namespace core;
struct Formula {
    std::string plain, mass, note;
    size_t atoms=0, components=0;
    bool valid=false;
    double averageMass=0.0;   // natural-abundance average, u
    double exactMass=0.0;     // monoisotopic mass, u
    bool exactValid=false;    // every element/isotope resolved
    double dbe=0.0;           // double-bond equivalent (C/H/N/halogen)
};
// Monoisotopic mass: the exact mass of the most abundant isotope.  A missing
// isotope row is reported through ok=false instead of silently reusing the
// average weight, because those are different quantities.
inline double monoisotopicMass(int element,bool* ok){
    int count=0;const chemistry::IsotopeInfo* list=chemistry::isotopes(element,&count);
    if(list&&count>0){int best=0;for(int i=1;i<count;++i)if(list[i].abundance>list[best].abundance)best=i;if(ok)*ok=true;return list[best].exactMass;}
    if(ok)*ok=false;return chemistry::elementInfo(element).weight;
}
inline bool formulaPart(const std::string& text,size_t& i,std::string& symbol,int& z,int& isotope,int& count){
    isotope=0;symbol.clear();bool bracket=i<text.size()&&text[i]=='[';
    if(bracket){++i;while(i<text.size()&&std::isdigit((unsigned char)text[i]))isotope=isotope*10+(text[i++]-'0');}
    if(i>=text.size()||!std::isupper((unsigned char)text[i]))return false;
    symbol+=text[i++];while(i<text.size()&&std::islower((unsigned char)text[i]))symbol+=text[i++];
    if(bracket){if(i>=text.size()||text[i++]!=']')return false;}
    if(symbol=="D"||symbol=="T"){isotope=symbol=="D"?2:3;symbol="H";}
    const auto* info=chemistry::elementInfoBySymbol(symbol.c_str(),int(symbol.size()));if(!info||info->number==0)return false;z=info->number;
    count=0;while(i<text.size()&&std::isdigit((unsigned char)text[i]))count=count*10+(text[i++]-'0');if(!count)count=1;
    if(isotope)symbol="["+std::to_string(isotope)+symbol+"]";return true;
}
inline std::string formulaComposition(const std::string& text,int* charge=nullptr){
    if(charge)*charge=0;auto at=text.find_last_of("+-");if(at==std::string::npos)return text;
    for(size_t i=at+1;i<text.size();++i)if(!std::isdigit((unsigned char)text[i]))return text;
    int magnitude=at+1==text.size()?1:std::atoi(text.substr(at+1).c_str());if(magnitude<1)return text;
    if(charge)*charge=(text[at]=='+'?1:-1)*magnitude;return text.substr(0,at);
}
inline bool addFormulaExact(const std::string& formula,double& exact){
    const auto text=formulaComposition(formula);
    for(size_t i=0;i<text.size();){
        std::string symbol;int z,isotope,n;if(!formulaPart(text,i,symbol,z,isotope,n))return false;
        bool ok=false;double one=0;
        if(isotope){chemistry::IsotopeInfo iso;ok=chemistry::knownIsotope(z,isotope,&iso);one=iso.exactMass;}
        else one=monoisotopicMass(z,&ok);if(!ok)return false;
        exact+=one*n;
    }
    return true;
}
inline bool addFormulaText(const std::string& formula,std::map<std::string,int>& counts,double& mass,size_t& heavy){
    const auto text=formulaComposition(formula);
    for(size_t i=0;i<text.size();){
        std::string symbol;int z,isotope,n;if(!formulaPart(text,i,symbol,z,isotope,n))return false;
        double w=chemistry::elementInfo(z).weight;
        if(isotope){chemistry::IsotopeInfo iso;if(!chemistry::knownIsotope(z,isotope,&iso))return false;w=iso.exactMass;}
        if(w<=0)return false;if(z!=1)heavy+=size_t(n);
        counts[symbol]+=n;mass+=w*n;
    }
    return true;
}
// Evaluate whole connected structures touched by selection, not a chemically
// capped fragment. Never guess abbreviation expansion or unknown valence.
inline Formula analyze(const Document& d,const std::vector<ObjectRef>& refs){
    Formula r;std::set<ObjectId> seeds,seen,texts;std::set<ObjectRef> groups;
    std::function<void(ObjectRef)> add=[&](ObjectRef x){
        if(x.kind==ObjectKind::Atom&&d.atoms().alive(x.id))seeds.insert(x.id);
        if(x.kind==ObjectKind::Text&&d.texts().alive(x.id)&&compounddata::find(d.plainText(x.id)))texts.insert(x.id);
        if(x.kind==ObjectKind::Bond){auto b=d.bonds().alive(x.id);if(b){seeds.insert(b->beginAtom);seeds.insert(b->endAtom);}}
        if(x.kind==ObjectKind::Group&&groups.insert(x).second){auto g=d.groups().alive(x.id);if(g)for(auto y:g->members)add(y);}
    };for(auto x:refs)add(x);
    if(seeds.empty()&&texts.empty()){r.note="选择结构或库内化合物文字，查看分子信息";return r;}
    r.components=texts.size();
    for(auto seed:seeds){if(seen.count(seed))continue;++r.components;std::queue<ObjectId> q;q.push(seed);seen.insert(seed);
        while(!q.empty()){auto a=q.front();q.pop();for(auto b:d.incidentBonds(a)){auto br=d.bonds().alive(b);if(!br)continue;auto n=br->beginAtom==a?br->endAtom:br->beginAtom;if(seen.insert(n).second)q.push(n);}}}
    // Analyze the saved molecular graph, including attachment valence, instead
    // of trying to infer a custom definition from its display name.
    auto fragmentFor=[&](ObjectId id){
        const auto& bag=d.extensions(ObjectRef(ObjectKind::Atom,id));
        if(auto raw=bag.findValue("studio.nickname.fragment"))return *raw;
        if(bag.findValue("studio.mechanism.ion.base"))if(auto group=nickdata::find(d.atomLabel(id)))return std::string(group->fragment);
        return std::string();
    };
    bool embedded=false;for(auto id:seen)if(!fragmentFor(id).empty()&&!(d.incidentBonds(id).empty()&&compounddata::ion(d.atomLabel(id),d.atoms().alive(id)->semantics.charge)))embedded=true;
    if(embedded){try{
        Document copy;copy.setDefaultBondLength(d.defaultBondLength());std::map<ObjectId,ObjectId> mapping;
        for(auto id:seen){auto atom=*d.atoms().alive(id);atom.sourceId=0;ObjectId fresh;auto e=copy.addAtomRecord(atom,&fresh);if(!e.ok())throw std::runtime_error(e.message);mapping[id]=fresh;copy.setAtomLabel(fresh,d.atomLabel(id));auto raw=fragmentFor(id);if(!raw.empty()){auto sem=copy.atoms().alive(fresh)->semantics;sem.nodeType=uint8_t(NodeType::Nickname);copy.setAtomSemantics(fresh,sem);copy.extensions(ObjectRef(ObjectKind::Atom,fresh)).set("studio.nickname.fragment",raw);}}
        for(auto id:seen)for(auto bid:d.incidentBonds(id)){auto bond=*d.bonds().alive(bid);if(bond.beginAtom!=id||!seen.count(bond.endAtom))continue;bond.sourceId=0;bond.beginAtom=mapping[bond.beginAtom];bond.endAtom=mapping[bond.endAtom];ObjectId fresh;auto e=copy.addBondRecord(bond,&fresh);if(!e.ok())throw std::runtime_error(e.message);}
        std::ostringstream xml;auto e=persistence::writeCdxml(copy,xml);if(!e.ok())throw std::runtime_error(e.message);
        Document expanded;e=persistence::readCdxml(nicknameexpand::expand(xml.str()),expanded);if(!e.ok())throw std::runtime_error(e.message);
        std::vector<ObjectRef> all;for(const auto& a:expanded.atoms().records())if(a.isAlive())all.push_back(ObjectRef(ObjectKind::Atom,a.id));
        for(auto id:texts){ObjectId fresh;auto text=*d.texts().alive(id);text.sourceId=0;auto error=expanded.addText(text.position,text.size,d.plainText(id),&fresh);if(!error.ok())throw std::runtime_error(error.message);all.push_back(ObjectRef(ObjectKind::Text,fresh));}
        return analyze(expanded,all);
    }catch(const std::exception& e){r.note=e.what();return r;}}
    r.atoms=seen.size();std::map<std::string,int> counts;double mass=0,exact=0;bool exactOk=true;int charge=0;
    for(auto id:texts){const auto* compound=compounddata::find(d.plainText(id));size_t heavy=0;if(!compound||!addFormulaText(compound->formula,counts,mass,heavy)){r.note="化合物分子式数据不可用";return r;}if(!addFormulaExact(compound->formula,exact))exactOk=false;int intrinsicCharge=0;formulaComposition(compound->formula,&intrinsicCharge);charge+=intrinsicCharge;r.atoms+=heavy;}
    for(auto id:seen){const auto* a=d.atoms().alive(id);if(!a)continue;auto sem=a->semantics;const std::string label=d.atomLabel(id);
        if(d.incidentBonds(id).empty())if(const auto* ion=compounddata::ion(label,sem.charge)){
            size_t heavy=0;if(!addFormulaText(ion->formula,counts,mass,heavy)){r.note="离子分子式数据不可用";return r;}
            if(!addFormulaExact(ion->formula,exact))exactOk=false;if(heavy)r.atoms+=heavy-1;
            charge+=compounddata::ionCharge(ion);continue;
        }
        if(auto bridge=bridgedata::find(label)){
            if(d.incidentBonds(id).size()!=2){r.note="双端基团需要两个连接点";return r;}
            size_t heavy=0;if(!addFormulaText(bridge->formula,counts,mass,heavy))return r;
            if(!addFormulaExact(bridge->formula,exact))exactOk=false;if(heavy)r.atoms+=heavy-1;continue;
        }
        const int elementalH=sem.nodeType==uint8_t(NodeType::Element)?hydride_labels::count(a->element,label):-1;
        if(elementalH<0&&!d.extensions(ObjectRef(ObjectKind::Atom,id)).findValue("studio.nickname.fragment")&&
           !label.empty()&&label!=core::elementSymbol(a->element)&&(!(nickdata::find(label)||label=="MeS")||compounddata::canonical(label)==label&&d.extensions(ObjectRef(ObjectKind::Atom,id)).findValue("studio.compound.fragment"))){
            if(const auto* compound=compounddata::find(label)){size_t heavy=0;if(!addFormulaText(compound->formula,counts,mass,heavy)){r.note="化合物分子式数据不可用";return r;}if(!addFormulaExact(compound->formula,exact))exactOk=false;if(heavy)r.atoms+=heavy-1;int intrinsicCharge=0;formulaComposition(compound->formula,&intrinsicCharge);charge+=sem.charge+intrinsicCharge;continue;}
        }
        if(auto alias=elementalH<0&&!(sem.nodeType==uint8_t(NodeType::Element)&&label==elementSymbol(a->element))?nickdata::findAlias(label=="MeS"?"SMe":label):nullptr){size_t heavy=0;if(!addFormulaText(alias->formula,counts,mass,heavy)){r.note="缩写分子式数据不可用";return r;}if(!addFormulaExact(alias->formula,exact))exactOk=false;if(heavy>0)r.atoms+=heavy-1;continue;}
        bool countedCarbonRadical=sem.implicitHydrogens>=0&&sem.radical>=uint8_t(Radical::Singlet)&&sem.radical<=uint8_t(Radical::Triplet);
        if(sem.nodeType!=uint8_t(NodeType::Element)||(!label.empty()&&label!=core::elementSymbol(a->element)&&elementalH<0)||(sem.radical&&!countedCarbonRadical&&elementalH<0)){r.note="含未解析标签或未明确氢数的自由基，暂不计算";return r;}
        int sum=0,arom=0,degree=0;for(auto bid:d.incidentBonds(id)){auto b=d.bonds().alive(bid);if(!b)continue;++degree;
            // The ring tool uses Kekule orders, with Aromatic semantics only
            // on the double edges. Those still contribute explicit valence 2.
            if(b->semantics==BondSemantics::Aromatic&&b->order==BondOrder::Double){sum+=2;continue;}
            if(b->order==BondOrder::Aromatic||b->semantics==BondSemantics::Aromatic){++arom;continue;}
            if(b->semantics==BondSemantics::Dative&&sem.implicitHydrogens>=0){sum+=int(b->order);continue;}
            if(b->semantics!=BondSemantics::Normal){r.note="含查询或特殊键，暂不计算";return r;}sum+=int(b->order);}
        int h=elementalH>=0?elementalH:sem.implicitHydrogens;
        if(h<0){
            // A charged atom with no bonds has no valence context: the label
            // alone cannot tell whether O- is an oxide (no H) or a hydroxide
            // (one H). Report it as unresolved instead of inventing hydrogens.
            if(sem.charge&&degree==0){r.note="带电结构需明确氢数";return r;}
            if(arom){if(a->element!=6||arom<2||degree>3){r.note="芳香杂原子的氢数待确认";return r;}h=3-degree;}
            else {int val=a->element==6?4:a->element==7?3:a->element==8?2:a->element==16?(sum<=2?2:sum<=4?4:6):a->element==15?(sum<=3?3:5):(a->element==1||a->element==9||a->element==17||a->element==35||a->element==53)?1:-1;
                if(sem.charge){if((a->element==7&&std::abs(sem.charge)==1)||(a->element==8&&std::abs(sem.charge)==1))val+=sem.charge;else{r.note="带电结构需明确氢数";return r;}}
                if(val<0||sum>val){r.note="该元素的价态需确认";return r;}h=val-sum;
                // Explicit hydrogen nodes never receive additional hydrogens.
                if(a->element==1)h=0;
            }
        }
        auto ei=chemistry::elementInfo(a->element);double am=ei.weight;std::string symbol=ei.symbol;double atomExact=0;
        if(sem.isotope){chemistry::IsotopeInfo iso;if(!chemistry::knownIsotope(a->element,sem.isotope,&iso)){r.note="同位素质量数据不可用";return r;}am=iso.exactMass;atomExact=iso.exactMass;symbol="["+std::to_string(sem.isotope)+symbol+"]";}
        else {bool ok=false;atomExact=monoisotopicMass(a->element,&ok);if(!ok)exactOk=false;}
        if(am<=0){r.note="原子量数据不可用";return r;}
        bool hOk=false;double hm=monoisotopicMass(1,&hOk);if(!hOk)exactOk=false;
        counts[symbol]++;counts["H"]+=h;mass+=am+h*chemistry::elementInfo(1).weight;exact+=atomExact+h*hm;charge+=sem.charge;
    }
    // DBE = C + 1 - (H + X)/2 + N/2.  Oxygen and sulfur do not enter the count.
    int carbon=counts["C"],hydrogen=counts["H"],nitrogen=counts["N"];
    int halogen=counts["F"]+counts["Cl"]+counts["Br"]+counts["I"];
    r.dbe=carbon+1-(hydrogen+halogen)/2.0+nitrogen/2.0;
    auto emit=[&](const std::string& s){int n=counts[s];if(n>0){r.plain+=s;if(n!=1)r.plain+=std::to_string(n);}counts.erase(s);};
    if(counts.count("C")){emit("C");emit("H");}while(!counts.empty())emit(counts.begin()->first);
    if(charge)r.plain+=" ("+std::to_string(std::abs(charge))+(charge>0?"+":"−")+")";
    std::ostringstream ss;ss<<std::fixed<<std::setprecision(1)<<mass;r.mass=ss.str();
    r.averageMass=mass;r.exactMass=exact;r.exactValid=exactOk;r.valid=true;
    r.note=r.components>1?std::to_string(r.components)+" 个结构 · 合计":"所选完整结构 · 平均分子量";return r;
}
}
