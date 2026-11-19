#pragma once
#include "next/arrows_curves/model.hpp"
namespace studio { namespace arrow_crossings {
using namespace r62;using namespace core;
inline double cross(Vec2 a,Vec2 b){return a.x*b.y-a.y*b.x;}
inline std::vector<Vec2> points(const arrows::ArrowParams& p){auto v=arrows::routedPoints(p);if(v.size()<2)arrows::flattenArc(p.begin,p.end,p.arcSagitta,arrows::Tessellation(),&v);return v;}
inline Vec2 unit(Vec2 v){return v*(1/std::max(1e-9,length(v)));}
inline std::vector<Vec2> cubic(Vec2 a,Vec2 b,Vec2 c,Vec2 d,int samples){std::vector<Vec2> out;for(int i=0;i<=samples;++i){double t=double(i)/samples,s=1-t;out.push_back(a*(s*s*s)+b*(3*s*s*t)+c*(3*s*t*t)+d*(t*t*t));}return out;}
struct Intersection {Vec2 point,direction,other;double at;};
inline std::vector<Intersection> intersections(const std::vector<Vec2>& a,const std::vector<Vec2>& b){
 std::vector<Intersection> out;for(size_t i=1;i<a.size();++i)for(size_t j=1;j<b.size();++j){auto u=a[i]-a[i-1],v=b[j]-b[j-1];double det=cross(u,v);if(std::abs(det)<1e-9)continue;auto delta=b[j-1]-a[i-1];double t=cross(delta,v)/det,s=cross(delta,u)/det;if(t<0||t>1||s<0||s>1)continue;auto q=a[i-1]+u*t;bool duplicate=false;for(auto hit:out)if(length(hit.point-q)<1e-5)duplicate=true;if(!duplicate)out.push_back({q,unit(u),unit(v),(i-1+t)/(a.size()-1)});
 }return out;
}
// A single cubic distributes the turn across the entire arrow. The old crossing
// is only the trigger; it is never used as a required interpolation point.
inline bool balancedCurve(const arrows::ArrowParams& arrow,const std::vector<Vec2>& other,std::vector<Vec2>* result){
 auto axis=arrow.end-arrow.begin;double span=length(axis);if(span<1)return false;auto normal=Vec2(-axis.y,axis.x)*(1/span);
 auto originalHits=intersections(points(arrow),other);double best=1e100,bestF=.7,bestH=.6,bestK=0;int bestSign=1;
 auto evaluate=[&](double f,double h,double k,int sign){
    if(f<.25||f>.8||h<.12||h>1.5||std::abs(k)>h*.15)return;
    auto c1=arrow.begin+axis*f+normal*(span*(sign*h+k));auto c2=arrow.end-axis*f+normal*(span*(-sign*h+k));auto curve=cubic(arrow.begin,c1,c2,arrow.end,40);auto hits=intersections(curve,other);if(hits.empty())return;
    double angle=0,travel=0;for(auto hit:hits){if(hit.at<.08||hit.at>.92)return;double cosine=dot(hit.direction,hit.other);angle+=1-cosine*cosine;double nearest=span;for(auto old:originalHits)nearest=std::min(nearest,length(old.point-hit.point));travel+=nearest*nearest/(span*span);}
    // Integral of squared curvature penalizes abrupt turns everywhere, including
    // the two lobes. A soft balance term permits the crossing to migrate.
    double bending=0,left=0,right=0;for(int i=1;i<40;++i){double t=i/40.,s=1-t;auto velocity=(c1-arrow.begin)*(3*s*s)+(c2-c1)*(6*s*t)+(arrow.end-c2)*(3*t*t);auto acceleration=(c2-c1*2+arrow.begin)*(6*s)+(arrow.end-c2*2+c1)*(6*t);double speed=length(velocity);if(speed<span*.04)return;double turn=cross(velocity,acceleration);double energy=span*turn*turn/std::pow(speed,5)/40.;bending+=energy;if(i<20)left+=energy;else right+=energy;}
    double imbalance=(left-right)/std::max(1.,left+right);double score=travel+12*angle+3*(hits.size()-1)+.2*bending+3*imbalance*imbalance+2*(k/h)*(k/h)+.1*h*h;
    if(score<best){best=score;bestF=f;bestH=h;bestK=k;bestSign=sign;}
 };
 for(int sign:{-1,1})for(double f:{.35,.45,.55,.65,.75})for(double h:{.25,.4,.6,.9,1.2})for(double k:{-.25,-.125,0.,.125,.25})evaluate(f,h,k,sign);
 if(best==1e100)return false;
 for(int round=0;round<3;++round){double f=bestF,h=bestH,k=bestK,step=.075/std::pow(2.,round);int sign=bestSign;for(int a=-1;a<=1;++a)for(int b=-1;b<=1;++b)for(int c=-1;c<=1;++c)evaluate(f+a*step,h+b*step*2,k+c*step,sign);}
 auto c1=arrow.begin+axis*bestF+normal*(span*(bestSign*bestH+bestK));auto c2=arrow.end-axis*bestF+normal*(span*(-bestSign*bestH+bestK));*result=cubic(arrow.begin,c1,c2,arrow.end,128);return true;
}
inline Error update(Document& d,const std::set<ObjectId>* candidates=nullptr){
 std::vector<ObjectId> ids;for(const auto& a:d.arrows().records())if(a.isAlive())ids.push_back(a.id);
 for(size_t k=1;k<ids.size();++k){if(candidates&&!candidates->count(ids[k]))continue;arrows::ArrowParams later;bool valid=false;arrows::readArrow(d,ids[k],&later,&valid);
  if(!valid||later.mechanism||later.virtualBond||later.family!=arrows::ArrowFamily::Arc||later.shafts!=arrows::ShaftCount::One||!later.route.empty())continue;
  auto v=points(later);std::vector<Vec2> curve;
  for(size_t h=0;h<k&&curve.empty();++h){arrows::ArrowParams first;bool ok=false;arrows::readArrow(d,ids[h],&first,&ok);if(!ok||first.mechanism||first.virtualBond||first.family!=arrows::ArrowFamily::Arc)continue;auto w=points(first);
   bool needs=false;for(auto hit:intersections(v,w)){
    if(length(hit.point-later.begin)<later.head.length*2||length(hit.point-later.end)<later.head.length*2||length(hit.point-first.begin)<first.head.length*2||length(hit.point-first.end)<first.head.length*2)continue;
    if(std::abs(dot(hit.direction,hit.other))<.94){needs=true;break;}
   }
   if(needs)balancedCurve(later,w,&curve);
  }
  if(curve.empty())continue;
  later.route.clear();auto axis=later.end-later.begin,n=Vec2(-axis.y,axis.x);double dd=dot(axis,axis);for(auto q:curve){q=q-later.begin;later.route.emplace_back(dot(q,axis)/dd,dot(q,n)/dd);}
  auto e=arrows::writeArrow(d,ids[k],later,d.defaultLineWidth());if(!e.ok())return e;
 }return Error();
}
} }





