#pragma once
#include "next/chemistry/valence.hpp"
#include "compound_catalog.hpp"
#include <algorithm>
#include <map>
#include <set>
#include <vector>

namespace studio_hydrogen {
using namespace r62;
using namespace r62::core;
// Compare rooted environments in the fragment before choosing between two H sites.
// In acyclic components each signature describes the entire labelled tree.
inline std::string branchSignature(const core::Document& d,ObjectId id,ObjectId parent,std::set<ObjectId>& seen){
    if(!seen.insert(id).second)return {};
    auto a=d.atoms().alive(id);
    std::string key=std::to_string(a->element)+"/"+std::to_string(a->semantics.implicitHydrogens)+"/"+
        std::to_string(a->semantics.charge)+"/"+std::to_string(a->semantics.isotope)+"/"+std::to_string(a->semantics.radical);
    std::vector<std::string> children;
    for(auto bid:d.incidentBonds(id)){
        auto b=d.bonds().alive(bid);auto other=b->beginAtom==id?b->endAtom:b->beginAtom;
        if(other==parent)continue;
        if(b->stereo!=BondStereo::None||b->display!=BondDisplay::Normal)return {};
        auto part=branchSignature(d,other,id,seen);if(part.empty())return {};
        children.push_back(std::to_string(int(b->order))+":"+part);
    }
    std::sort(children.begin(),children.end());for(const auto& child:children)key+="("+child+")";return key;
}
inline ObjectId fragmentDonor(const Document& graph,ObjectId dummy,const std::string& base,bool first,int written,const std::string& label){
    int totalH=0;
    std::set<ObjectId> explicitDonors;
    const auto* known=compounddata::find(label);
    const std::string canonical=known?known->name:label;
    bool terminalAlkyne=canonical=="PhCCH"||base.find("#C")!=std::string::npos;
    bool terminalAlkyl=canonical=="nHexH"||canonical=="nPentH"||canonical=="nHeptH";
    bool cyclicAlkane=canonical=="cHexH";
    std::map<ObjectId,int> hydrogens;
    for(const auto& a:graph.atoms().records())if(a.isAlive()&&a.element!=1&&a.id!=dummy){
        int count=a.semantics.implicitHydrogens;
        if(count<0)count=std::max(0,chemistry::refreshAtomDerived(graph,a.id,{}).newHydrogenCount);
        for(auto bid:graph.incidentBonds(a.id)){
            auto b=graph.bonds().alive(bid);auto other=graph.atoms().alive(b->beginAtom==a.id?b->endAtom:b->beginAtom);
            if(other->element==1&&!other->semantics.isotope){++count;explicitDonors.insert(a.id);}
        }
        if(count){hydrogens[a.id]=count;totalH+=count;}
    }
    // Several hydride aliases (DIBAL-H, TIPS-H, 9-BBN-H) omit the donor
    // element from the label. The authored H bond identifies it unambiguously.
    if(explicitDonors.size()==1){
        auto id=*explicitDonors.begin();int z=graph.atoms().alive(id)->element;
        if(z==5||z==13||z==14||z==50)return id;
    }
    bool oxyacid=first && (base.compare(0,2,"SO")==0||base.compare(0,2,"PO")==0||base.compare(0,2,"NO")==0||base.compare(0,2,"IO")==0||base.compare(0,2,"CO")==0);
    std::vector<ObjectId> sites;
    for(auto item:hydrogens){
        auto a=graph.atoms().alive(item.first);std::string symbol=elementSymbol(a->element);
        bool matches=base.size()>=symbol.size()&&!base.compare(first?0:base.size()-symbol.size(),symbol.size(),symbol);
        if(canonical=="HFIP")matches=a->element==8;
        if(canonical=="HMDS")matches=a->element==7;
        if(canonical=="Hacac"){
            int carbonyls=0;
            for(auto bid:graph.incidentBonds(a->id)){
                auto b=graph.bonds().alive(bid);auto neighbor=b->beginAtom==a->id?b->endAtom:b->beginAtom;
                if(graph.atoms().alive(neighbor)->element!=6)continue;
                for(auto id:graph.incidentBonds(neighbor)){
                    auto link=graph.bonds().alive(id);auto other=link->beginAtom==neighbor?link->endAtom:link->beginAtom;
                    if(link->order==BondOrder::Double&&graph.atoms().alive(other)->element==8)++carbonyls;
                }
            }
            matches=a->element==6&&carbonyls==2;
        }
        if(terminalAlkyne){
            bool triple=false;for(auto bid:graph.incidentBonds(a->id))if(graph.bonds().alive(bid)->order==BondOrder::Triple)triple=true;
            if(!triple)continue;
        }
        if(terminalAlkyl){if(a->element!=6||item.second!=3)continue;matches=true;}
        if(cyclicAlkane&&a->element==6)matches=true;
        if(!matches&&!(oxyacid&&a->element==8&&totalH==written))continue;
        if(item.second<written&&totalH!=written)continue;
        sites.push_back(a->id);
    }
    if(sites.size()==1)return sites[0];
    // In an unsubstituted uniform cycle all vertices have the same rooted
    // graph (including the two possible Kekule orientations).
    if(dummy==kInvalidObjectId && sites.size()>=3 && sites.size()==graph.stats().aliveAtoms){
        bool uniform=true;auto firstAtom=graph.atoms().alive(sites.front());
        std::vector<int> orders;
        for(auto id:sites){
            auto a=graph.atoms().alive(id);auto links=graph.incidentBonds(id);
            if(links.size()!=2||a->element!=firstAtom->element||a->semantics.charge!=firstAtom->semantics.charge||
               a->semantics.isotope||a->semantics.radical||hydrogens[id]!=hydrogens[sites.front()]){uniform=false;break;}
            std::vector<int> current;
            for(auto bid:links){auto b=graph.bonds().alive(bid);if(b->stereo!=BondStereo::None)uniform=false;current.push_back(int(b->order));}
            std::sort(current.begin(),current.end());if(orders.empty())orders=current;else if(orders!=current)uniform=false;
        }
        if(uniform){
            std::vector<ObjectId> queue{sites.front()};std::set<ObjectId> visited{sites.front()};
            for(size_t i=0;i<queue.size();++i)for(auto bid:graph.incidentBonds(queue[i])){
                auto b=graph.bonds().alive(bid);auto other=b->beginAtom==queue[i]?b->endAtom:b->beginAtom;
                if(visited.insert(other).second)queue.push_back(other);
            }
            if(visited.size()==sites.size())return sites.front();
        }
    }
    std::string signature;
    for(auto id:sites){
        std::set<ObjectId> seen;auto k=branchSignature(graph,id,kInvalidObjectId,seen);
        if(k.empty()||(!signature.empty()&&k!=signature))return kInvalidObjectId;signature=k;
    }
    return sites.empty()?kInvalidObjectId:sites.front();
}
}
