#pragma once
#include <algorithm>
#include "next/arrows_curves/model.hpp"
namespace studio { namespace arrow_crossings {
using namespace r62;using namespace core;
inline double cross(Vec2 a,Vec2 b){return a.x*b.y-a.y*b.x;}
inline std::vector<Vec2> points(const arrows::ArrowParams& p){auto v=arrows::routedPoints(p);if(v.size()<2)arrows::flattenArc(p.begin,p.end,p.arcSagitta,arrows::Tessellation(),&v);return v;}
inline Vec2 unit(Vec2 v){return v*(1/std::max(1e-9,length(v)));}
struct Intersection {Vec2 point,direction,other;double at;};
inline std::vector<Intersection> intersections(const std::vector<Vec2>& a,const std::vector<Vec2>& b){
 std::vector<Intersection> out;for(size_t i=1;i<a.size();++i)for(size_t j=1;j<b.size();++j){auto u=a[i]-a[i-1],v=b[j]-b[j-1];double det=cross(u,v);if(std::abs(det)<1e-9)continue;auto delta=b[j-1]-a[i-1];double t=cross(delta,v)/det,s=cross(delta,u)/det;if(t<0||t>1||s<0||s>1)continue;auto q=a[i-1]+u*t;bool duplicate=false;for(auto hit:out)if(length(hit.point-q)<1e-5)duplicate=true;if(!duplicate)out.push_back({q,unit(u),unit(v),(i-1+t)/(a.size()-1)});
 }return out;
}
// Cubic approximation of a circular arc, starting at the common tangent.
inline Vec2 arcSample(Vec2 join, Vec2 end, Vec2 tangent, double t) {
    Vec2 chord = end - join;
    Vec2 direction = unit(chord);
    double cosine = dot(direction,tangent);
    Vec2 endTangent = direction * (2*cosine) - tangent;
    double handle = 2*length(chord)/(3*(1+cosine));
    double u = 1-t;
    return join + chord*(t*t*(3-2*t))
                + (tangent*u - endTangent*t)*(3*handle*u*t);
}

inline bool fitJunction(const arrows::ArrowParams& arrow, const std::vector<Vec2>& mainArc,
                        const Intersection& original, std::vector<Vec2>* result) {
    Vec2 chord = arrow.end-arrow.begin;
    double span = length(chord);
    if (span<1) return false;
    Vec2 axis=unit(chord);
    // Bias the junction slightly toward the drawn arc so the two shoulders
    // can differ in depth, while retaining the user's overall direction.
    Vec2 middle=(arrow.begin+arrow.end)*.5;
    Vec2 bias=original.point-middle;
    Vec2 target=middle+bias*std::min(.30,.065*span/std::max(length(bias),1e-9));
    double best=std::numeric_limits<double>::infinity();
    for(size_t i=1;i<mainArc.size();++i){
        Vec2 edge=mainArc[i]-mainArc[i-1];
        double squared=dot(edge,edge);
        if(squared<1e-10)continue;
        double t=dot(target-mainArc[i-1],edge)/squared;
        Vec2 join=mainArc[i-1]+edge*std::max(0.,std::min(1.,t));
        if(length(join-target)>.55*span)continue;
        Vec2 forward=unit(edge);
        if(dot(forward,axis)<0)forward=forward*-1;
        double angle=std::atan2(cross(axis,forward),dot(axis,forward));
        Vec2 normal(-axis.y,axis.x);
        for(double blend : {.96,.83,.65}){
            double turn=std::max(-1.12,std::min(1.12,angle*blend));
            Vec2 tangent=axis*std::cos(turn)+normal*std::sin(turn);
            if(dot(unit(join-arrow.begin),tangent)<.10 || dot(unit(arrow.end-join),tangent)<.10)continue;
            double clearance=std::min({length(join-arrow.begin),length(join-arrow.end),
                                       length(join-mainArc.front()),length(join-mainArc.back())});
            if(clearance<2*arrow.head.length)continue;
            double score=length(join-target)/span+.11*(1-blend);
            if(score>=best)continue;
            std::vector<Vec2> path;path.reserve(129);
            for(int k=64;k>=0;--k)path.push_back(arcSample(join,arrow.begin,tangent*-1,k/64.));
            for(int k=1;k<=64;++k)path.push_back(arcSample(join,arrow.end,tangent,k/64.));
            if(intersections(path,mainArc).size()!=1)continue;
            best=score;
            *result=std::move(path);
        }
    }
    return !result->empty();
}
inline Error update(Document& d,const std::set<ObjectId>* candidates=nullptr){
 std::vector<ObjectId> ids;for(const auto& a:d.arrows().records())if(a.isAlive())ids.push_back(a.id);
 for(size_t k=1;k<ids.size();++k){if(candidates&&!candidates->count(ids[k]))continue;arrows::ArrowParams later;bool valid=false;arrows::readArrow(d,ids[k],&later,&valid);
  if(!valid||later.mechanism||later.virtualBond||later.family!=arrows::ArrowFamily::Arc||later.shafts!=arrows::ShaftCount::One||!later.route.empty())continue;
  auto v=points(later);std::vector<Vec2> curve;
  for(size_t h=0;h<k&&curve.empty();++h){arrows::ArrowParams first;bool ok=false;arrows::readArrow(d,ids[h],&first,&ok);if(!ok||first.mechanism||first.virtualBond||first.family!=arrows::ArrowFamily::Arc)continue;auto w=points(first);
   for(auto hit:intersections(v,w)){
    if(length(hit.point-later.begin)<later.head.length*2||length(hit.point-later.end)<later.head.length*2||length(hit.point-first.begin)<first.head.length*2||length(hit.point-first.end)<first.head.length*2)continue;
    if(std::abs(dot(hit.direction,hit.other))<.94){if(fitJunction(later,w,hit,&curve))break;}
   }
  }
  if(curve.empty())continue;
  later.route.clear();auto axis=later.end-later.begin,n=Vec2(-axis.y,axis.x);double dd=dot(axis,axis);for(auto q:curve){q=q-later.begin;later.route.emplace_back(dot(q,axis)/dd,dot(q,n)/dd);}
  auto e=arrows::writeArrow(d,ids[k],later,d.defaultLineWidth());if(!e.ok())return e;
 }return Error();
}
} }





