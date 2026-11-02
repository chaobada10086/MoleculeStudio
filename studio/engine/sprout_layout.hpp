#pragma once
#include "next/rings_chains/rings_chains.hpp"
#include "alkyne_sprout.hpp"
#include <map>
#include <set>
#include <algorithm>
#include <cmath>
#include <limits>

// Automatic atom growth only. Explicit drags and intentional ring closures
// keep their pointer direction. All lengths below are in bond-length units.
namespace r62 { namespace sprout_layout {
using namespace core;
inline double gap(double a,double b){double d=std::fmod(std::abs(a-b),360.);return std::min(d,360.-d);}
inline double pointSegmentSq(Vec2 p,Vec2 a,Vec2 b){Vec2 v=b-a;double n=lengthSq(v);double t=n?std::max(0.,std::min(1.,dot(p-a,v)/n)):0.;return lengthSq(p-a-v*t);}
inline double cross(Vec2 a,Vec2 b){return a.x*b.y-a.y*b.x;}
inline double segmentSq(Vec2 a,Vec2 b,Vec2 c,Vec2 d){
    double x=cross(b-a,c-a),y=cross(b-a,d-a),z=cross(d-c,a-c),w=cross(d-c,b-c);
    if(((x>1e-10&&y< -1e-10)||(x< -1e-10&&y>1e-10))&&((z>1e-10&&w< -1e-10)||(z< -1e-10&&w>1e-10)))return 0.;
    return std::min(std::min(pointSegmentSq(a,c,d),pointSegmentSq(b,c,d)),std::min(pointSegmentSq(c,a,b),pointSegmentSq(d,a,b)));
}
inline bool inside(Vec2 p,const std::vector<Vec2>& polygon){
    bool in=false;for(size_t i=0,j=polygon.size()-1;i<polygon.size();j=i++){
        Vec2 a=polygon[i],b=polygon[j];if(pointSegmentSq(p,a,b)<1e-12)return false;
        if((a.y>p.y)!=(b.y>p.y)&&p.x<(b.x-a.x)*(p.y-a.y)/(b.y-a.y)+a.x)in=!in;
    }return in;
}
struct AtomObstacle{Vec2 p;double radius;};
struct Edge{Vec2 a,b;bool incident;};
struct Environment{
    std::vector<AtomObstacle> atoms;std::vector<Edge> edges;
    std::vector<std::vector<Vec2>> faces;
    Environment(const Document& doc,ObjectId source){
        const Vec2 origin=doc.atoms().get(source)->position;const double unit=std::max(1e-6,doc.defaultBondLength());
        std::map<ObjectId,Vec2> positions;std::map<ObjectId,std::vector<ObjectId>> adjacent;
        for(const auto& a:doc.atoms().records())if(a.isAlive()){
            Vec2 p=(a.position-origin)*(1./unit);
            if(a.id!=source&&lengthSq(p)<16.)atoms.push_back({p,(a.element!=6||!doc.atomLabel(a.id).empty())?.42:.28});
        }
        // Scan records once, then evaluate only nearby obstacles. Include long
        // bonds whose endpoints are outside the neighbourhood but cross it.
        for(const auto& b:doc.bonds().records())if(b.isAlive()){
            const auto* a=doc.atoms().alive(b.beginAtom);const auto* z=doc.atoms().alive(b.endAtom);if(!a||!z)continue;
            Vec2 p=(a->position-origin)*(1./unit),q=(z->position-origin)*(1./unit);
            if(pointSegmentSq(Vec2(),p,q)>16.)continue;
            edges.push_back({p,q,b.beginAtom==source||b.endAtom==source});
            positions[a->id]=p;positions[z->id]=q;adjacent[a->id].push_back(z->id);adjacent[z->id].push_back(a->id);
        }
        // Walk the bounded faces of the local embedded bond graph. This also
        // prevents growth INTO a ring when a short segment crosses no edge.
        for(auto& entry:adjacent){auto center=positions[entry.first];auto& ns=entry.second;
            std::sort(ns.begin(),ns.end(),[&](ObjectId a,ObjectId b){Vec2 p=positions[a]-center,q=positions[b]-center;double x=std::atan2(p.y,p.x),y=std::atan2(q.y,q.x);return x==y?a<b:x<y;});
        }
        std::set<std::pair<ObjectId,ObjectId>> visited;
        for(const auto& entry:adjacent)for(auto next:entry.second){
            auto start=std::make_pair(entry.first,next);if(visited.count(start))continue;
            auto edge=start;std::vector<Vec2> polygon;bool closed=false;
            for(size_t step=0;step<=edges.size()*2;++step){
                if(visited.count(edge)){closed=edge==start;break;}visited.insert(edge);polygon.push_back(positions[edge.first]);
                const auto& ns=adjacent[edge.second];auto found=std::find(ns.begin(),ns.end(),edge.first);if(found==ns.end())break;
                size_t index=size_t(found-ns.begin());edge=std::make_pair(edge.second,ns[(index+ns.size()-1)%ns.size()]);
            }
            double area=0;for(size_t i=0;i<polygon.size();++i)area+=cross(polygon[i],polygon[(i+1)%polygon.size()]);
            if(closed&&polygon.size()>=3&&area>1e-8)faces.push_back(polygon);
        }
    }
    bool score(double angle,const std::vector<double>& neighbours,double* crowding)const{
        for(double n:neighbours)if(gap(n,angle)<25.-1e-7)return false;
        Vec2 tip=direction(angle);double score=0;
        for(const auto& a:atoms){double end=lengthSq(tip-a.p),along=pointSegmentSq(a.p,Vec2(),tip);
            if(end<a.radius*a.radius||along<a.radius*a.radius)return false;
            score+=.08/((end+.2)*(end+.2))+.025/((along+.25)*(along+.25));
        }
        for(const auto& e:edges)if(!e.incident){
            double d=segmentSq(Vec2(),tip,e.a,e.b);if(d<.18*.18)return false;
            score+=.025/((d+.2)*(d+.2));
        }
        for(const auto& polygon:faces)for(double t:{.25,.55,.9})if(inside(tip*t,polygon))return false;
        *crowding=score;return true;
    }
};
inline rings_chains::SproutDirection resolve(const Document& doc,ObjectId atom,double preferred=-30.){
    auto env=rings_chains::buildSproutEnvironment(doc,atom,25.);
    auto result=rings_chains::resolveSproutDirection(env,preferred);
    if(!doc.atoms().alive(atom)){result.ok=false;result.rejectReason="sprout anchor is missing";return result;}
    if(!result.ok)return result;
    Environment obstacles(doc,atom);std::vector<double> canonical;
    double linear=0;bool sp=spContinuationAngle(doc,atom,&linear);
    if(sp)canonical.push_back(linear);
    else if(env.neighbourDirectionsDeg.empty())canonical.push_back(preferred);
    else if(env.neighbourDirectionsDeg.size()<=2)for(double n:env.neighbourDirectionsDeg){canonical.push_back(n+120.);canonical.push_back(n-120.);}
    else canonical.push_back(result.directionDeg);
    std::vector<double> candidates=canonical;
    if(!sp){
        candidates.push_back(result.directionDeg);
        auto ns=env.neighbourDirectionsDeg;
        for(size_t i=0;i<ns.size();++i){double width=std::fmod(ns[(i+1)%ns.size()]-ns[i]+360.,360.);if(ns.size()==1)width=360.;candidates.push_back(ns[i]+width*.5);}
        double frame=ns.empty()?preferred:ns.front();for(int i=0;i<24;++i)candidates.push_back(frame+i*15.);
    }
    bool found=false;double best=1e300;
    for(double angle:candidates){double crowding=0;if(!obstacles.score(angle,env.neighbourDirectionsDeg,&crowding))continue;
        double deviation=180.;for(double standard:canonical)deviation=std::min(deviation,gap(angle,standard));
        double value=crowding+2.*std::pow(deviation/60.,2)+.0001*gap(angle,result.directionDeg);
        if(value<best-1e-9){best=value;linear=angle;found=true;}
    }
    result.ok=found;
    if(found)result.directionDeg=std::fmod(linear+720.,360.);
    else result.rejectReason="附近没有可安全自动添加化学键的方向，请拖动指定方向";
    return result;
}
} }
