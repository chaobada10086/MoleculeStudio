#pragma once
#include "next/integration/session.hpp"
#include "next/chemistry/valence.hpp"
#include <queue>
#include <map>
#include <cctype>
#include <sstream>
#include "nickname_catalog.hpp"
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
inline bool addFormulaExact(const std::string& text,double& exact){
    for(size_t i=0;i<text.size();){
        if(!std::isupper((unsigned char)text[i]))return false;
        std::string symbol(1,text[i++]);
        while(i<text.size()&&std::islower((unsigned char)text[i]))symbol+=text[i++];
        int n=0;while(i<text.size()&&std::isdigit((unsigned char)text[i]))n=n*10+(text[i++]-'0');
        if(!n)n=1;
        const chemistry::ElementInfo* info=chemistry::elementInfoBySymbol(symbol.c_str(),int(symbol.size()));
        if(!info||info->number==0)return false;
        bool ok=false;double one=monoisotopicMass(info->number,&ok);if(!ok)return false;
        exact+=one*n;
    }
    return true;
}
inline bool addFormulaText(const std::string& text,std::map<std::string,int>& counts,double& mass,size_t& heavy){
    for(size_t i=0;i<text.size();){
        if(!std::isupper((unsigned char)text[i]))return false;
        std::string symbol(1,text[i++]);
        while(i<text.size()&&std::islower((unsigned char)text[i]))symbol+=text[i++];
        int n=0;while(i<text.size()&&std::isdigit((unsigned char)text[i]))n=n*10+(text[i++]-'0');
        if(!n)n=1;
        int z=0;double w=0;
        if(symbol=="H"){z=1;w=1.008;}
        else if(symbol=="C"){z=6;w=12.011;}
        else if(symbol=="N"){z=7;w=14.007;}
        else if(symbol=="O"){z=8;w=15.999;}
        else if(symbol=="F"){z=9;w=18.998;}
        else if(symbol=="P"){z=15;w=30.974;}
        else if(symbol=="S"){z=16;w=32.06;}
        else if(symbol=="Cl"){z=17;w=35.45;}
        else if(symbol=="Br"){z=35;w=79.904;}
        else if(symbol=="I"){z=53;w=126.904;}
        else return false;
        if(symbol!="H")heavy+=size_t(n);
        counts[symbol]+=n;mass+=w*n;
    }
    return true;
}
// Evaluate whole connected structures touched by selection, not a chemically
// capped fragment. Never guess abbreviation expansion or unknown valence.
inline Formula analyze(const Document& d,const std::vector<ObjectRef>& refs){
    Formula r;std::set<ObjectId> seeds,seen;std::set<ObjectRef> groups;
    std::function<void(ObjectRef)> add=[&](ObjectRef x){
        if(x.kind==ObjectKind::Atom&&d.atoms().alive(x.id))seeds.insert(x.id);
        if(x.kind==ObjectKind::Bond){auto b=d.bonds().alive(x.id);if(b){seeds.insert(b->beginAtom);seeds.insert(b->endAtom);}}
        if(x.kind==ObjectKind::Group&&groups.insert(x).second){auto g=d.groups().alive(x.id);if(g)for(auto y:g->members)add(y);}
    };for(auto x:refs)add(x);
    if(seeds.empty()){r.note="选择一个结构，查看分子信息";return r;}
    for(auto seed:seeds){if(seen.count(seed))continue;++r.components;std::queue<ObjectId> q;q.push(seed);seen.insert(seed);
        while(!q.empty()){auto a=q.front();q.pop();for(auto b:d.incidentBonds(a)){auto br=d.bonds().alive(b);if(!br)continue;auto n=br->beginAtom==a?br->endAtom:br->beginAtom;if(seen.insert(n).second)q.push(n);}}}
    // Analyze the saved molecular graph, including attachment valence, instead
    // of trying to infer a custom definition from its display name.
    bool embedded=false;for(auto id:seen)if(d.extensions(ObjectRef(ObjectKind::Atom,id)).findValue("studio.nickname.fragment"))embedded=true;
    if(embedded){try{
        Document copy;copy.setDefaultBondLength(d.defaultBondLength());std::map<ObjectId,ObjectId> mapping;
        for(auto id:seen){auto atom=*d.atoms().alive(id);atom.sourceId=0;ObjectId fresh;auto e=copy.addAtomRecord(atom,&fresh);if(!e.ok())throw std::runtime_error(e.message);mapping[id]=fresh;copy.setAtomLabel(fresh,d.atomLabel(id));if(auto raw=d.extensions(ObjectRef(ObjectKind::Atom,id)).findValue("studio.nickname.fragment"))copy.extensions(ObjectRef(ObjectKind::Atom,fresh)).set("studio.nickname.fragment",*raw);}
        for(auto id:seen)for(auto bid:d.incidentBonds(id)){auto bond=*d.bonds().alive(bid);if(bond.beginAtom!=id||!seen.count(bond.endAtom))continue;bond.sourceId=0;bond.beginAtom=mapping[bond.beginAtom];bond.endAtom=mapping[bond.endAtom];ObjectId fresh;auto e=copy.addBondRecord(bond,&fresh);if(!e.ok())throw std::runtime_error(e.message);}
        std::ostringstream xml;auto e=persistence::writeCdxml(copy,xml);if(!e.ok())throw std::runtime_error(e.message);
        Document expanded;e=persistence::readCdxml(nicknameexpand::expand(xml.str()),expanded);if(!e.ok())throw std::runtime_error(e.message);
        std::vector<ObjectRef> all;for(const auto& a:expanded.atoms().records())if(a.isAlive())all.push_back(ObjectRef(ObjectKind::Atom,a.id));
        return analyze(expanded,all);
    }catch(const std::exception& e){r.note=e.what();return r;}}
    r.atoms=seen.size();std::map<std::string,int> counts;double mass=0,exact=0;bool exactOk=true;int charge=0;
    for(auto id:seen){const auto* a=d.atoms().alive(id);if(!a)continue;auto sem=a->semantics;const std::string label=d.atomLabel(id);
        if(auto alias=nickdata::findAlias(label)){size_t heavy=0;if(!addFormulaText(alias->formula,counts,mass,heavy)){r.note="缩写分子式数据不可用";return r;}if(!addFormulaExact(alias->formula,exact))exactOk=false;if(heavy>0)r.atoms+=heavy-1;continue;}
        if(sem.nodeType!=uint8_t(NodeType::Element)||(!label.empty()&&label!=core::elementSymbol(a->element))||sem.radical){r.note="含未解析标签或自由基，暂不计算";return r;}
        int sum=0,arom=0,degree=0;for(auto bid:d.incidentBonds(id)){auto b=d.bonds().alive(bid);if(!b)continue;++degree;
            // The ring tool uses Kekule orders, with Aromatic semantics only
            // on the double edges. Those still contribute explicit valence 2.
            if(b->semantics==BondSemantics::Aromatic&&b->order==BondOrder::Double){sum+=2;continue;}
            if(b->order==BondOrder::Aromatic||b->semantics==BondSemantics::Aromatic){++arom;continue;}
            if(b->semantics!=BondSemantics::Normal){r.note="含查询或特殊键，暂不计算";return r;}sum+=int(b->order);}
        int h=sem.implicitHydrogens;
        if(h<0){
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
