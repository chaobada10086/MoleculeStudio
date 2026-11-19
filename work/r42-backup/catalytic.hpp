#pragma once
#include "next/arrows_curves/model.hpp"
#include "next/selection/bounds.hpp"
#include "mechanism_visual_anchors.hpp"
namespace r62 { namespace integration {core::Error translateDocumentObjects(core::Document&,const std::vector<core::ObjectRef>&,core::Vec2);} }
namespace studio { namespace catalytic {
using namespace r62;using namespace core;
inline const char* key(){return "studio.catalytic";}
inline std::vector<ObjectRef> component(const Document& d,ObjectId root){
    std::set<ObjectId> ids;std::vector<ObjectId> q;if(d.atoms().alive(root)){q.push_back(root);ids.insert(root);}for(size_t i=0;i<q.size();++i)for(auto n:d.neighbours(q[i]))if(ids.insert(n).second)q.push_back(n);
    std::vector<ObjectRef> refs;for(auto id:ids)refs.emplace_back(ObjectKind::Atom,id);for(const auto& b:d.bonds().records())if(b.isAlive()&&ids.count(b.beginAtom)&&ids.count(b.endAtom))refs.emplace_back(ObjectKind::Bond,b.id);return refs;
}
inline Vec2 center(const Document& d,ObjectId id){if(d.atoms().alive(id)&&d.extensions(ObjectRef(ObjectKind::Atom,id)).findValue("pst.attr.MoleculeStudioCatalyticMetal"))return d.atoms().alive(id)->position;auto refs=component(d,id);auto box=selection::selectionBounds(d,refs);return (box.min+box.max)*.5;}
inline double extent(const Document& d,ObjectId id){auto refs=component(d,id);auto box=selection::selectionBounds(d,refs);const auto& labels=arrows::mechanismVisualBoxes(d);for(auto ref:refs)if(ref.kind==ObjectKind::Atom){auto i=labels.find(ref.id);if(i!=labels.end()){if(i->second.hasBody){box.add(i->second.body.min);box.add(i->second.body.max);}if(i->second.hasCharge){box.add(i->second.charge.min);box.add(i->second.charge.max);}}}auto c=center(d,id);return std::hypot(std::max(std::abs(box.min.x-c.x),std::abs(box.max.x-c.x)),std::max(std::abs(box.min.y-c.y),std::abs(box.max.y-c.y)))+8;}
inline Vec2 point(const Document& d,ObjectRef ref){if(ref.kind==ObjectKind::Atom)return center(d,ref.id);if(ref.kind==ObjectKind::Arrow){arrows::ArrowParams p;bool ok=false;arrows::readArrow(d,ref.id,&p,&ok);if(ok){auto pts=arrows::routedPoints(p);return pts.size()>2?pts[pts.size()/2]:arrows::arcMidpoint(p.begin,p.end,p.arcSagitta);}}return Vec2();}
inline Vec2 local(const arrows::ArrowParams& p,Vec2 q){auto v=p.end-p.begin,n=Vec2(-v.y,v.x);double dd=std::max(1e-12,v.x*v.x+v.y*v.y);auto x=q-p.begin;return Vec2((x.x*v.x+x.y*v.y)/dd,(x.x*n.x+x.y*n.y)/dd);}
inline Vec2 world(const arrows::ArrowParams& p,Vec2 q){auto v=p.end-p.begin;return p.begin+v*q.x+Vec2(-v.y,v.x)*q.y;}
inline Error update(Document& d){
 for(int pass=0;pass<2;++pass)for(const auto& a:d.arrows().records())if(a.isAlive()){
    auto mode=d.extensions(ObjectRef(ObjectKind::Arrow,a.id)).findValue(key());if(!mode||((*mode=="cycle")!=(pass==0)))continue;
    arrows::ArrowParams p;bool ok=false;arrows::readArrow(d,a.id,&p,&ok);if(!ok)continue;
    auto alive=[&](ObjectRef r){return r.kind==ObjectKind::Atom?d.atoms().alive(r.id)!=nullptr:r.kind==ObjectKind::Arrow?d.arrows().alive(r.id)!=nullptr:false;};
    if(!alive(p.electronSource)||!alive(p.electronTarget))continue;
    auto from=point(d,p.electronSource),to=point(d,p.electronTarget);
    if(*mode=="cycle"&&p.route.size()>2){auto ds=from-world(p,p.mechanismDeparture),dt=to-world(p,p.mechanismArrival);auto pts=arrows::routedPoints(p);
        if(length(ds)<1e-6&&length(dt)<1e-6){size_t first=0,last=pts.size()-1;double ga=extent(d,p.electronSource.id),gb=extent(d,p.electronTarget.id);while(last-first>8&&length(pts[first]-from)<ga)++first;while(last-first>8&&length(pts[last]-to)<gb)--last;if(first==0&&last+1==pts.size())continue;pts=std::vector<Vec2>(pts.begin()+first,pts.begin()+last+1);}
        else for(size_t i=0;i<pts.size();++i){double t=double(i)/(pts.size()-1);pts[i]=pts[i]+ds*(1-t)+dt*t;}
        p.begin=pts.front();p.end=pts.back();p.route.clear();for(auto q:pts)p.route.push_back(local(p,q));p.mechanismDeparture=local(p,from);p.mechanismArrival=local(p,to);auto e=arrows::writeArrow(d,a.id,p,d.defaultLineWidth());if(!e.ok())return e;continue;}
    auto delta=to-from;double span=length(delta);if(span<1e-6)continue;auto u=delta*(1/span);
    double gapA=p.electronSource.kind==ObjectKind::Atom?extent(d,p.electronSource.id):0,gapB=p.electronTarget.kind==ObjectKind::Atom?extent(d,p.electronTarget.id):0;
    if(gapA+gapB>span*.8){double scale=span*.8/(gapA+gapB);gapA*=scale;gapB*=scale;}
    auto begin=from+u*gapA,end=to-u*gapB;
    // Bounds-based docking remains independent of the chemical electron anchors.
    if(length(begin-p.begin)<1e-6&&length(end-p.end)<1e-6)continue;
    double old=length(p.end-p.begin);double ratio=old>1e-6?p.arcSagitta/old:.2;
    p.begin=begin;p.end=end;p.arcSagitta=length(end-begin)*ratio;p.route.clear();
    auto e=arrows::writeArrow(d,a.id,p,d.defaultLineWidth());if(!e.ok())return e;
 }return Error();
}
inline Error link(Document& d,ObjectRef from,ObjectRef to,const std::string& mode,double bend,ObjectId* out){
    auto a=point(d,from),b=point(d,to);if(length(a-b)<1)return Error(ErrorCode::InvalidGeometry,"连接的两个对象太近");
    auto p=arrows::withHeadTier(arrows::resolvePreset(58,a,b,d.defaultLineWidth()),1);p.arcSagitta=length(b-a)*bend;p.sweep=arrows::ArcSweep::Free;p.electronSource=from;p.electronTarget=to;
    auto e=arrows::addArrow(d,p,d.defaultLineWidth(),out);if(!e.ok())return e;d.extensions(ObjectRef(ObjectKind::Arrow,*out)).set(key(),mode);return update(d);
}
inline Error arrange(Document& d,std::vector<ObjectId> nodes,Vec2 origin,double rx,double ry,bool clockwise,std::vector<ObjectRef>* made,bool fixedSize=false,double rotation=0){
    const double pi=3.141592653589793;size_t n=nodes.size();if(n<3||n>12)return Error(ErrorCode::InvalidArgument,"请选择 3–12 个独立中间体");
    double need=0;for(auto id:nodes)need=std::max(need,extent(d,id));double minimum=(need+10)/std::sin(pi/n);if(!fixedSize){rx=std::max(rx,minimum);ry=std::max(ry,minimum);}auto rotate=[&](Vec2 v){return Vec2(v.x*std::cos(rotation)-v.y*std::sin(rotation),v.x*std::sin(rotation)+v.y*std::cos(rotation));};
    for(size_t i=0;i<n;++i){double a=-pi*.5+(clockwise?1:-1)*2*pi*i/n;auto refs=component(d,nodes[i]);auto delta=origin+rotate(Vec2(rx*std::cos(a),ry*std::sin(a)))-center(d,nodes[i]);auto e=integration::translateDocumentObjects(d,refs,delta);if(!e.ok())return e;}
    for(size_t i=0;i<n;++i){ObjectId id=kInvalidObjectId;auto from=ObjectRef(ObjectKind::Atom,nodes[i]),to=ObjectRef(ObjectKind::Atom,nodes[(i+1)%n]);
        auto contains=[&](ObjectId root,ObjectId atom){auto refs=component(d,root);return std::find(refs.begin(),refs.end(),ObjectRef(ObjectKind::Atom,atom))!=refs.end();};
        for(const auto& edge:d.arrows().records())if(edge.isAlive()){auto mode=d.extensions(ObjectRef(ObjectKind::Arrow,edge.id)).findValue(key());if(!mode||*mode!="cycle")continue;arrows::ArrowParams p;bool valid=false;arrows::readArrow(d,edge.id,&p,&valid);if(valid&&p.electronSource.kind==ObjectKind::Atom&&p.electronTarget.kind==ObjectKind::Atom&&contains(nodes[i],p.electronSource.id)&&contains(nodes[(i+1)%n],p.electronTarget.id)){id=edge.id;p.begin=point(d,from);p.end=point(d,to);p.arcSagitta=length(p.end-p.begin)*(clockwise?-.20:.20);arrows::writeArrow(d,id,p,d.defaultLineWidth());break;}}
        auto e=id==kInvalidObjectId?link(d,from,to,"cycle",clockwise?-.20:.20,&id):update(d);if(!e.ok())return e;
        // Every span samples the same ellipse. Angular clipping leaves gaps for
        // labels without moving the curve away from that ellipse.
        arrows::ArrowParams p;bool valid=false;arrows::readArrow(d,id,&p,&valid);double sign=clockwise?1.:-1.,step=2*pi/n;
        double a=-pi*.5+sign*step*i,b=a+sign*step;auto speed=[&](double t){return std::hypot(rx*std::sin(t),ry*std::cos(t));};
        double ga=std::min(step*.38,extent(d,nodes[i])/speed(a)),gb=std::min(step*.38,extent(d,nodes[(i+1)%n])/speed(b));a+=sign*ga;b-=sign*gb;
        auto pt=[&](double t){return origin+rotate(Vec2(rx*std::cos(t),ry*std::sin(t)));};p.begin=pt(a);p.end=pt(b);p.route.clear();for(int j=0;j<=96;++j)p.route.push_back(local(p,pt(a+(b-a)*j/96.)));p.mechanismDeparture=local(p,point(d,from));p.mechanismArrival=local(p,point(d,to));p.arcSagitta=length(p.end-p.begin)*(clockwise?-.2:.2);e=arrows::writeArrow(d,id,p,d.defaultLineWidth());if(!e.ok())return e;if(made)made->emplace_back(ObjectKind::Arrow,id);}
    return Error();
}
} }
