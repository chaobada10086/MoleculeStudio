#pragma once
#include "mechanism_layout.hpp"
#include "catalytic.hpp"
#include "arrow_crossings.hpp"
namespace studio { namespace mechanism_follow {
using namespace r62::core;
struct Item {ObjectId id;r62::arrows::ArrowParams arrow;Vec2 source,target;bool hasSource=false,hasTarget=false,hadOverlap=false;};
struct Before {
    std::vector<Item> items;std::set<ObjectId> originalArrows;std::map<ObjectId,std::string> originalParams;
    explicit Before(const Document& doc){for(const auto& record:doc.arrows().records())if(record.isAlive()){
        originalArrows.insert(record.id);Item item;item.id=record.id;bool ok=false;r62::arrows::readArrow(doc,record.id,&item.arrow,&ok);
        if(ok)originalParams[record.id]=r62::arrows::encodeArrowParams(item.arrow);if(!ok||!item.arrow.mechanism)continue;
        item.hasSource=r62::arrows::mechanismAnchorPoint(doc,item.arrow.electronSource,&item.source);
        item.hasTarget=r62::arrows::mechanismAnchorPoint(doc,item.arrow.electronTarget,&item.target);
        item.hadOverlap=r62::arrows::mechanismOverlapsGlyphs(doc,item.arrow);
        if(item.hasSource||item.hasTarget)items.push_back(item);
    }}
    Error apply(Document& doc)const{for(const auto& item:items){
        if(!doc.arrows().alive(item.id))continue;
        Vec2 source,target,ds,dt;
        if(item.hasSource&&r62::arrows::mechanismAnchorPoint(doc,item.arrow.electronSource,&source))ds=source-item.source;
        if(item.hasTarget&&r62::arrows::mechanismAnchorPoint(doc,item.arrow.electronTarget,&target))dt=target-item.target;
        if(r62::core::length(ds)<1e-9&&r62::core::length(dt)<1e-9)continue;
        auto p=item.arrow;p.begin=p.begin+ds;p.end=p.end+dt;
        if(r62::core::length(ds-dt)>1e-9||(!item.hadOverlap&&r62::arrows::mechanismOverlapsGlyphs(doc,p))){
            // Rebuild from chemical anchors, never from previously clipped ink.
            // Special charge/pair/radical sites retain their translated hint.
            if(item.hasSource&&(p.sourceVisualAnchor<2||p.sourceVisualAnchor==5))p.begin=source;
            if(item.hasTarget&&(p.targetVisualAnchor<2||p.targetVisualAnchor==5))p.end=target;
            if(!p.mechanismSide){double bow=0;for(auto q:p.route)bow+=q.y;if(std::abs(bow)<1e-8)bow=p.arcSagitta;p.mechanismSide=bow>=0?1:-1;}
            p.route.clear();p.arcSagitta=r62::core::length(p.end-p.begin)*.22*p.mechanismSide;
            Document routing=doc;routing.removeObject(ObjectRef(ObjectKind::Arrow,item.id));
            r62::arrows::routeMechanism(routing,p);r62::arrows::clearMechanismLabels(routing,p);
        }
        auto e=r62::arrows::writeArrow(doc,item.id,p,doc.defaultLineWidth());if(!e.ok())return e;
    }auto e=studio::catalytic::update(doc);std::set<ObjectId> created;for(const auto& a:doc.arrows().records())if(a.isAlive()){r62::arrows::ArrowParams p;bool ok=false;r62::arrows::readArrow(doc,a.id,&p,&ok);if(ok&&(!originalArrows.count(a.id)||(originalParams.count(a.id)&&originalParams.at(a.id)!=r62::arrows::encodeArrowParams(p))))created.insert(a.id);}return e.ok()?studio::arrow_crossings::update(doc,&created):e;}
};
} }
