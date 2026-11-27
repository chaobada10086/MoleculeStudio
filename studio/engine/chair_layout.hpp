#pragma once
#include "next/rings_chains/rings_chains.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

// Recognize the drawn six-cycle, rather than storing a screen-space direction
// on its atoms. The frame therefore follows rotations, copies and CDXML imports.
namespace r62 { namespace chair_layout {
using namespace core;
struct Frame {
    bool valid=false;
    std::vector<ObjectId> ring;
    Vec2 axial,equatorial;
};
inline Frame frame(const Document& doc,ObjectId anchor){
    Frame best;double bestError=.0064; // squared, relative to one ring bond
    if(!doc.atoms().alive(anchor))return best;
    std::vector<ObjectId> path(1,anchor);size_t visits=0;
    auto fit=[&](){
        Vec2 center;for(auto id:path)center=center+doc.atoms().alive(id)->position;center=center*(1./6);
        for(auto preset:{rings_chains::RingPresetId::Chair1,rings_chains::RingPresetId::Chair2}){
            rings_chains::RingGeometry g;rings_chains::buildRingGeometry(preset,1,&g);
            for(int first=0;first<6;++first){
                double norm=0,a=0,b=0;
                for(int i=0;i<6;++i){auto q=g.vertices[(i+first)%6].local,p=doc.atoms().alive(path[i])->position-center;norm+=lengthSq(q);a+=dot(q,p);b+=q.x*p.y-q.y*p.x;}
                a/=norm;b/=norm;double scale=std::hypot(a,b);if(scale<1e-6)continue;
                auto transform=[&](Vec2 q){return Vec2(a*q.x-b*q.y,b*q.x+a*q.y);};
                double error=0;for(int i=0;i<6;++i)error+=lengthSq(doc.atoms().alive(path[i])->position-center-transform(g.vertices[(i+first)%6].local));
                error/=6*scale*scale;if(error>=bestError)continue;
                const int k=first;const double flip=preset==rings_chains::RingPresetId::Chair2?-1.:1.;
                Vec2 axis(0,(k%2?1.:-1.)*flip);
                // Projected equatorial direction complements the alternating
                // axial direction. Do not bisect the two visible ring edges.
                auto q=g.vertices[k].local,prev=g.vertices[(k+5)%6].local,next=g.vertices[(k+1)%6].local;
                Vec2 eq=(q*2-prev-next)-axis*(1.6/std::sqrt(2.34));
                best.valid=true;best.ring=path;best.axial=transform(axis)*(1./scale);
                best.equatorial=transform(eq)*(1./(scale*length(eq)));bestError=error;
            }
        }
    };
    std::function<void(ObjectId)> walk=[&](ObjectId id){
        if(++visits>512)return;
        for(auto edge:doc.incidentBonds(id)){
            auto bond=doc.bonds().alive(edge);if(!bond||bond->order!=BondOrder::Single||bond->semantics!=BondSemantics::Normal)continue;
            auto other=bond->beginAtom==id?bond->endAtom:bond->beginAtom;
            if(other==anchor){if(path.size()==6)fit();continue;}
            if(path.size()>=6||std::find(path.begin(),path.end(),other)!=path.end())continue;
            path.push_back(other);walk(other);path.pop_back();
        }
    };walk(anchor);return best;
}
struct Target {bool recognized=false,available=false,axial=false;Vec2 end;};
inline Target target(const Document& doc,ObjectId id,Vec2 pointer,bool automatic){
    Target result;const auto f=frame(doc,id);if(!f.valid)return result;result.recognized=true;
    const Vec2 origin=doc.atoms().alive(id)->position;double unit=doc.defaultBondLength();
    const Vec2 rays[]={f.equatorial,f.axial};double best=1e100;
    auto onRing=[&](ObjectId a){return std::find(f.ring.begin(),f.ring.end(),a)!=f.ring.end();};
    for(int k=0;k<2;++k){
        const Vec2 ray=rays[k];double reach=unit*(k?.5:.65);
        // Leave a visible gap when an axial substituent ends at a projected ring edge.
        if(k)for(size_t i=0;i<f.ring.size();++i){
            auto a=f.ring[i],b=f.ring[(i+1)%f.ring.size()];if(a==id||b==id)continue;
            auto start=doc.atoms().alive(a)->position-origin;
            auto span=doc.atoms().alive(b)->position-origin-start;
            double det=ray.x*span.y-ray.y*span.x;if(std::abs(det)<1e-9)continue;
            double t=(start.x*span.y-start.y*span.x)/det;
            double u=(start.x*ray.y-start.y*ray.x)/det;
            if(u>=0&&u<=1&&t>unit*.20&&std::abs(t-reach)<unit*.15)reach=t-unit*.10;
        }
        const Vec2 end=origin+ray*reach;bool occupied=false;
        for(auto edge:doc.incidentBonds(id)){auto bond=doc.bonds().alive(edge);auto other=bond->beginAtom==id?bond->endAtom:bond->beginAtom;if(onRing(other))continue;auto v=doc.atoms().alive(other)->position-origin;double n=length(v);if(n>1e-8&&dot(v,ray)/n>.90)occupied=true;}
        if(occupied)continue;
        // A projected axial bond may cross the ring outline in a chair diagram.
        // It remains a separate edge; external atoms are still obstacles.
        for(const auto& atom:doc.atoms().records())if(atom.isAlive()&&!onRing(atom.id)){
            auto v=end-origin;double t=std::max(0.,std::min(1.,dot(atom.position-origin,v)/lengthSq(v)));
            if(length(atom.position-origin-v*t)<unit*.18)occupied=true;
        }
        if(occupied)continue;
        auto v=pointer-origin;double score=automatic?double(k):1-dot(v,ray)/std::max(1e-8,length(v));
        if(score<best){best=score;result.available=true;result.axial=k==1;result.end=end;}
    }
    return result;
}
} }
