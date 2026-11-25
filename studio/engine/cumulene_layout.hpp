#pragma once
#include "next/bonds/edit.hpp"
#include <map>
#include <set>
#include <cmath>

namespace studio { namespace cumulene_layout {
using namespace r62::core;
inline std::vector<ObjectId> pair(const Document& d,const AtomRecord& a){
    if((a.element!=6&&a.element!=7)||a.semantics.nodeType!=uint8_t(NodeType::Element)||a.semantics.aromatic)return {};
    auto ids=d.incidentBonds(a.id);if(ids.size()!=2)return {};
    for(auto id:ids){auto b=d.bonds().alive(id);if(!b||b->order!=BondOrder::Double||b->semantics!=BondSemantics::Normal||b->display!=BondDisplay::Normal||b->stereo!=BondStereo::None)return {};}
    return ids;
}
inline std::set<ObjectId> branch(const Document& d,ObjectId start,ObjectId excluded){
    std::set<ObjectId> found{start};std::vector<ObjectId> pending{start};
    for(size_t i=0;i<pending.size();++i)for(auto id:d.incidentBonds(pending[i])){
        auto b=d.bonds().alive(id);if(!b)continue;auto other=b->beginAtom==pending[i]?b->endAtom:b->beginAtom;
        if(other!=excluded&&found.insert(other).second)pending.push_back(other);
    }return found;
}
struct Before {
    std::map<ObjectId,std::vector<ObjectId>> pairs;
    Before(const Document& d, bool capture){
        if (!capture) return;
        for(const auto& a:d.atoms().records())if(a.isAlive()){auto p=pair(d,a);if(!p.empty())pairs[a.id]=p;}}
    Error apply(Document& d)const{
        for(const auto& a:d.atoms().records())if(a.isAlive()){
            auto ids=pair(d,a);if(ids.empty())continue;
            auto old=pairs.find(a.id);if(old!=pairs.end()&&old->second==ids)continue;
            auto endpoint=[&](ObjectId id){auto b=d.bonds().alive(id);return b->beginAtom==a.id?b->endAtom:b->beginAtom;};
            auto left=endpoint(ids[0]),right=endpoint(ids[1]);
            auto l=branch(d,left,a.id),r=branch(d,right,a.id);
            if(l.count(right))continue; // An endocyclic pair cannot be straightened without opening the ring.
            if(l.size()<r.size()){std::swap(left,right);std::swap(l,r);}
            auto origin=a.position,u=d.atoms().alive(left)->position-origin,v=d.atoms().alive(right)->position-origin;
            const double lu=length(u),lv=length(v);if(lu<1e-8||lv<1e-8)continue;
            const Vec2 destination=origin-u;
            const double angle=std::atan2(-u.y,-u.x)-std::atan2(v.y,v.x),cs=std::cos(angle),sn=std::sin(angle);
            const Vec2 previous=d.atoms().alive(right)->position;
            for(auto id:r){auto q=d.atoms().alive(id)->position-previous;
                auto e=d.setAtomPosition(id,destination+Vec2(q.x*cs-q.y*sn,q.x*sn+q.y*cs));if(!e.ok())return e;
            }
            for(auto id:ids)d.extensions(ObjectRef(ObjectKind::Bond,id)).erase("user.bonds.doubleSide");
        }return Error::success();
    }
};
} }
