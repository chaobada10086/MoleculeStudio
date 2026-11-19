#pragma once
#include "next/arrows_curves/model.hpp"
#include <algorithm>
#include <cmath>
#include <set>

namespace r62 { namespace arrows {
// All measurements use document points. Acquisition tolerance comes from the view.
struct ArcJunction {
    core::ObjectId arrow=core::kInvalidObjectId;
    core::Vec2 point,tangent;
    double distance=0;
    bool valid()const{return arrow!=core::kInvalidObjectId;}
};
inline ArcJunction arcJunction(const core::Document& doc,core::Vec2 query,double radius,core::ObjectId exclude=core::kInvalidObjectId){
    ArcJunction best;best.distance=radius;
    for(const auto& a:doc.arrows().records())if(a.isAlive()&&a.id!=exclude){
        ArrowParams p;bool ok=false;if(!readArrow(doc,a.id,&p,&ok).ok()||isMechanismArrow(p)||p.shafts!=ShaftCount::One)continue;
        ArrowGeometry g;Tessellation tess;tess.tolerance=std::max(.01,radius*.025);
        if(!buildArrowGeometry(p,doc.defaultLineWidth(),tess,&g).ok()||g.shafts.empty())continue;
        const auto& line=g.shafts.front();
        for(size_t i=1;i<line.size();++i){auto d=line[i]-line[i-1];double len=core::length(d);if(len<1e-8)continue;
            double t=std::max(0.,std::min(1.,core::dot(query-line[i-1],d)/(len*len)));auto q=line[i-1]+d*t;double gap=core::length(query-q);
            if(gap<best.distance){best.arrow=a.id;best.point=q;best.distance=gap;best.tangent=d*(1/len);if(p.head.atBegin&&!p.head.atEnd)best.tangent=best.tangent*(-1);}
        }
    }return best;
}
inline double tracedArcSagitta(core::Vec2 begin,core::Vec2 end,const std::vector<core::Vec2>& trace,double tolerance){
    auto d=end-begin;double chord=core::length(d);if(chord<1e-8)return 0;
    double bow=0,x=chord*.5;
    for(auto sample:trace){auto q=sample-begin;double y=(d.x*q.y-d.y*q.x)/chord;if(std::abs(y)>std::abs(bow)){bow=y;x=core::dot(q,d)/chord;}}
    if(std::abs(bow)<tolerance)return chord*.2071067811865475;
    const double cy=(x*x+bow*bow-chord*x)/(2*bow),radius=std::hypot(chord*.5,cy);
    const double sag=cy+std::copysign(radius,bow),limit=chord*3.797877056;
    return std::max(-limit,std::min(limit,sag));
}
inline void tangentArc(ArrowParams& p,core::Vec2 tangent,bool atBegin){
    auto d=p.end-p.begin;double len=core::length(d);if(len<1e-8)return;auto u=d*(1/len);
    double cross=u.x*tangent.y-u.y*tangent.x;
    double halfAngle=std::atan2(atBegin?cross:-cross,core::dot(u,tangent));
    halfAngle=std::max(-2.8797932658,std::min(2.8797932658,halfAngle));
    p.arcSagitta=len*.5*std::tan(halfAngle*.5);p.sweep=ArcSweep::Free;p.route.clear();
}
inline void junctionCurve(ArrowParams& p,const ArcJunction& start,const ArcJunction& finish){
    if(start.valid())p.begin=start.point;if(finish.valid())p.end=finish.point;
    p.route.clear();
    if(start.valid()&&finish.valid()){
        auto d=p.end-p.begin;double len=core::length(d);if(len<1e-8)return;auto normal=core::Vec2(-d.y,d.x);
        auto a=p.begin+start.tangent*(len*.35),b=p.end-finish.tangent*(len*.35);
        for(int i=0;i<=64;++i){double t=i/64.,s=1-t;auto q=p.begin*(s*s*s)+a*(3*s*s*t)+b*(3*s*t*t)+p.end*(t*t*t)-p.begin;p.route.push_back(core::Vec2(core::dot(q,d)/(len*len),core::dot(q,normal)/(len*len)));}
    }else if(start.valid())tangentArc(p,start.tangent,true);else if(finish.valid())tangentArc(p,finish.tangent,false);
    if(start.valid())p.head.atBegin=false;if(finish.valid())p.head.atEnd=false;
}
inline core::Error mergeArrowGroup(core::Document& doc,core::ObjectId child,const std::vector<core::ObjectId>& targets){
    std::set<core::ObjectRef> members;members.insert(core::ObjectRef(core::ObjectKind::Arrow,child));
    for(auto id:targets)if(id!=child&&doc.arrows().alive(id))members.insert(core::ObjectRef(core::ObjectKind::Arrow,id));
    if(members.size()<2)return core::Error::success();
    bool changed=true;std::set<core::ObjectId> old;
    while(changed){changed=false;for(const auto& g:doc.groups().records())if(g.isAlive()&&g.label=="Joined arrows"&&!old.count(g.id)){
        bool overlaps=false;for(auto ref:g.members)overlaps|=members.count(ref)!=0;if(overlaps){members.insert(g.members.begin(),g.members.end());old.insert(g.id);changed=true;}
    }}
    // Removing an object group also deletes its children. Here we dissolve only
    // the old containers while preserving every arrow in the union.
    for(auto id:old){doc.groups().kill(id);doc.noteTouchedGroup(id);}
    core::GroupRecord group;group.label="Joined arrows";group.members.assign(members.begin(),members.end());core::ObjectId id;return doc.addGroup(group,&id);
}
inline void appendJunctionPreview(core::PreviewScene& scene,const ArcJunction& snap,const core::ViewTransform& view){
    if(!snap.valid())return;core::PreviewItem mark;mark.primitive=core::PreviewPrimitive::Polyline;mark.style.width=view.deviceRadiusToDocument(1.5);mark.style.colorRGBA=0x168D87FF;
    double r=view.deviceRadiusToDocument(5);for(int i=0;i<=32;++i){double a=i*6.28318530718/32;mark.points.push_back(snap.point+core::Vec2(std::cos(a),std::sin(a))*r);}scene.add(mark);
}
} }
