#pragma once
#include "molecular_render.hpp"
#include <memory>
namespace r62 { namespace arrows {
struct MechanismVisibleBondStroke {core::Vec2 begin,end;double halfWidth=0;};
struct MechanismBondEnvelope {core::Vec2 origin,normal;double low=0,high=0;bool valid=false;std::vector<MechanismVisibleBondStroke> strokes;};
inline const std::map<core::ObjectId,MechanismBondEnvelope>& mechanismBondEnvelopes(const core::Document& doc){
    struct Cache {const core::Document* owner=nullptr;core::DocumentId id=0;uint64_t revision=~uint64_t(0);std::unique_ptr<text::GlyphProvider> provider;std::unique_ptr<text::GlyphCache> glyphs;std::map<core::ObjectId,MechanismBondEnvelope> envelopes;
        Cache():provider(text::createGdiGlyphProvider("Arial","Microsoft YaHei")),glyphs(new text::GlyphCache(provider.get())){} };
    static Cache cache;if(cache.owner==&doc&&cache.id==doc.id()&&cache.revision==doc.revision())return cache.envelopes;
    cache.owner=&doc;cache.id=doc.id();cache.revision=doc.revision();cache.envelopes.clear();
    integration::RingGeometry rings(doc);auto labels=integration::molecular::clippingLabels(doc,integration::molecular::labels(doc,*cache.glyphs,rings));
    for(const auto& bond:doc.bonds().records())if(bond.isAlive()){
        auto data=rings.build(bond);integration::molecular::clip(data,bond,labels,integration::molecular::setting(doc,"MarginWidth",1.6));
        auto delta=data.axisEnd-data.axisBegin;double length=core::length(delta);if(length<1e-8)continue;
        auto& envelope=cache.envelopes[bond.id];envelope.origin=data.axisBegin;envelope.normal=core::Vec2(-delta.y/length,delta.x/length);
        for(const auto& shape:data.shapes){double half=shape.kind==bonds::BondShapeKind::FilledPolygon?0:(shape.strokeWidth>0?shape.strokeWidth:doc.defaultLineWidth())*.5;
            const auto& points=shape.kind==bonds::BondShapeKind::FilledPolygon?shape.outline:shape.points;
            for(size_t i=1;i<points.size();++i){MechanismVisibleBondStroke stroke;stroke.begin=points[i-1];stroke.end=points[i];stroke.halfWidth=half;envelope.strokes.push_back(stroke);}
            for(auto q:points){auto d=q-envelope.origin;double t=d.x*envelope.normal.x+d.y*envelope.normal.y;
                if(!envelope.valid){envelope.low=t-half;envelope.high=t+half;envelope.valid=true;}else{envelope.low=std::min(envelope.low,t-half);envelope.high=std::max(envelope.high,t+half);}}
        }
    }
    return cache.envelopes;
}
inline bool mechanismBondContact(const core::Document& doc,core::ObjectId bond,core::Vec2 point,core::Vec2* nearest,double* inkDistance=nullptr){
    if(inkDistance)*inkDistance=1e100;
    const auto& data=mechanismBondEnvelopes(doc);auto found=data.find(bond);if(found==data.end())return false;double best=1e100;bool valid=false;
    for(const auto& stroke:found->second.strokes){auto delta=stroke.end-stroke.begin;double square=delta.x*delta.x+delta.y*delta.y;if(square<1e-12)continue;auto rel=point-stroke.begin;double t=std::max(0.,std::min(1.,(rel.x*delta.x+rel.y*delta.y)/square));auto contact=stroke.begin+delta*t;double distance=core::length(contact-point);
        if(inkDistance)*inkDistance=std::min(*inkDistance,std::max(0.,distance-stroke.halfWidth));
        if(distance<best){best=distance;if(nearest)*nearest=contact;valid=true;}}
    return valid;
}
inline core::Vec2 mechanismBondOutwardContact(const core::Document& doc,core::ObjectId bond,core::Vec2 point,core::Vec2 hint,bool useHint=false){
    auto b=doc.bonds().alive(bond);if(b&&projection_molecule::axis(doc,*b)&&projection_molecule::kind(doc,b->beginAtom)==2){auto center=doc.atoms().alive(b->beginAtom)->position;auto ray=useHint?hint:point-center;double length=core::length(ray);if(length>1e-8)return center+ray*((projection_molecule::radius(doc)+1.1)/length);}
    const auto& data=mechanismBondEnvelopes(doc);auto found=data.find(bond);if(found==data.end()||!found->second.valid)return point;
    const auto& e=found->second;auto rel=point-e.origin;double t=rel.x*e.normal.x+rel.y*e.normal.y,mid=(e.low+e.high)*.5;
    const double hinted=core::dot(hint,e.normal)<0?-1.:1.;
    const double direction=useHint?hinted:t<mid-1e-6?-1.:t>mid+1e-6?1.:hinted;
    double outside=direction<0?e.low-1.1:e.high+1.1;return point+e.normal*(outside-t);
}
inline bool mechanismOutsideBondInk(const core::Document& doc,core::ObjectId bond,core::Vec2 point,double gap=1.1){
    const auto& envelopes=mechanismBondEnvelopes(doc);auto found=envelopes.find(bond);if(found==envelopes.end()||!found->second.valid)return true;
    const auto& e=found->second;auto d=point-e.origin;double t=d.x*e.normal.x+d.y*e.normal.y;
    return t<=e.low-gap||t>=e.high+gap;
}
} }
