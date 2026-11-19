#pragma once
#include "next/arrows_curves/model.hpp"
namespace studio { namespace arrow_crossings {
using namespace r62;using namespace core;
inline double cross(Vec2 a,Vec2 b){return a.x*b.y-a.y*b.x;}
inline std::vector<Vec2> points(const arrows::ArrowParams& p){auto v=arrows::routedPoints(p);if(v.size()<2)arrows::flattenArc(p.begin,p.end,p.arcSagitta,arrows::Tessellation(),&v);return v;}
inline Vec2 unit(Vec2 v){return v*(1/std::max(1e-9,length(v)));}
inline Error update(Document& d,const std::set<ObjectId>* candidates=nullptr){
 std::vector<ObjectId> ids;for(const auto& a:d.arrows().records())if(a.isAlive())ids.push_back(a.id);
 for(size_t k=1;k<ids.size();++k){if(candidates&&!candidates->count(ids[k]))continue;arrows::ArrowParams later;bool valid=false;arrows::readArrow(d,ids[k],&later,&valid);
  if(!valid||later.mechanism||later.virtualBond||later.family!=arrows::ArrowFamily::Arc||later.shafts!=arrows::ShaftCount::One||!later.route.empty())continue;
  auto v=points(later);bool found=false;Vec2 hit,tangent;size_t index=0;
  for(size_t h=0;h<k&&!found;++h){arrows::ArrowParams first;bool ok=false;arrows::readArrow(d,ids[h],&first,&ok);if(!ok||first.mechanism||first.virtualBond||first.family!=arrows::ArrowFamily::Arc)continue;auto w=points(first);
   for(size_t i=1;i<v.size()&&!found;++i)for(size_t j=1;j<w.size();++j){auto a=v[i-1],b=w[j-1],u=v[i]-a,z=w[j]-b;double det=cross(u,z);if(std::abs(det)<1e-9)continue;double t=cross(b-a,z)/det,s=cross(b-a,u)/det;if(t<0||t>1||s<0||s>1)continue;auto q=a+u*t;
    if(length(q-later.begin)<later.head.length*2||length(q-later.end)<later.head.length*2||length(q-first.begin)<first.head.length*2||length(q-first.end)<first.head.length*2)continue;
    // A shallow crossing already has the requested tangent transition.
    auto x=unit(u),y=unit(z);double dot=x.x*y.x+x.y*y.y;if(std::abs(dot)>.94)continue;if(dot<0)y=y*(-1);hit=q;tangent=y;index=i;found=true;break;
   }
  }
  if(!found)continue;
  auto begin=later.begin,end=later.end,startT=unit(v[1]-v[0]),endT=unit(v.back()-v[v.size()-2]);double a=length(hit-begin),b=length(end-hit);if(a<1||b<1)continue;
  std::vector<Vec2> curve;auto cubic=[&](Vec2 p0,Vec2 p1,Vec2 p2,Vec2 p3){for(int i=curve.empty()?0:1;i<=64;++i){double t=i/64.,s=1-t;curve.push_back(p0*(s*s*s)+p1*(3*s*s*t)+p2*(3*s*t*t)+p3*(t*t*t));}};
  cubic(begin,begin+startT*(a*.4),hit-tangent*(a*.35),hit);cubic(hit,hit+tangent*(b*.35),end-endT*(b*.4),end);
  later.route.clear();auto axis=end-begin,n=Vec2(-axis.y,axis.x);double dd=axis.x*axis.x+axis.y*axis.y;for(auto q:curve){q=q-begin;later.route.emplace_back((q.x*axis.x+q.y*axis.y)/dd,(q.x*n.x+q.y*n.y)/dd);}
  auto e=arrows::writeArrow(d,ids[k],later,d.defaultLineWidth());if(!e.ok())return e;
 }return Error();
} } }
