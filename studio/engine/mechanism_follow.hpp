#pragma once
#include "mechanism_layout.hpp"
#include "catalytic.hpp"
#include "arrow_crossings.hpp"
namespace studio { namespace mechanism_follow {
using namespace r62::core;
struct Item {ObjectId id;r62::arrows::ArrowParams arrow;Vec2 source,target;bool hasSource=false,hasTarget=false,hadOverlap=false;};
// Arrow writes do not affect atom or bond glyph geometry. Keep that geometry
// stable for the batch, while later routes still see every earlier arrow edit.
class RoutingObstacles {
    using Obstacle = r62::arrows::MechanismObstacle;
    std::vector<Obstacle> fixed_, combined_;
    std::map<ObjectId,std::vector<Obstacle>> arrows_;
    std::vector<ObjectId> order_;
public:
    const std::vector<r62::core::Box> routingLabels;
    explicit RoutingObstacles(const Document& doc):fixed_(r62::arrows::mechanismFixedObstacles(doc)), routingLabels(r62::arrows::mechanismRoutingLabels(doc)) {
        for (const auto& record:doc.arrows().records()) if (record.isAlive()) {
            r62::arrows::ArrowParams p;
            bool valid=false;
            r62::arrows::readArrow(doc,record.id,&p,&valid);
            order_.push_back(record.id);
            if (valid) update(record.id,p);
        }
    }
    void update(ObjectId id,const r62::arrows::ArrowParams& p) {
        auto& segments=arrows_[id];
        segments.clear();
        r62::arrows::appendArrowObstacles(p,segments);
    }
    const std::vector<Obstacle>& excluding(ObjectId id) {
        combined_=fixed_;
        for (auto other:order_) if (other!=id) {
            const auto found=arrows_.find(other);
            if (found!=arrows_.end()) combined_.insert(combined_.end(),found->second.begin(),found->second.end());
        }
        return combined_;
    }
};
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
    Error apply(Document& doc)const{
        std::unique_ptr<RoutingObstacles> obstacles;
        std::vector<std::pair<ObjectId,r62::arrows::ArrowParams>> updates;
        for(const auto& item:items){
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
            if (!obstacles) {
                obstacles.reset(new RoutingObstacles(doc));
                for (const auto& prior:updates) obstacles->update(prior.first,prior.second);
            }
            r62::arrows::routeMechanism(doc,p,true,&obstacles->excluding(item.id),&obstacles->routingLabels);
            r62::arrows::clearMechanismLabels(doc,p);
        }
        if (obstacles) obstacles->update(item.id,p);
        updates.emplace_back(item.id,std::move(p));
    }
        for (const auto& edit:updates) {
            auto result=r62::arrows::writeArrow(doc,edit.first,edit.second,doc.defaultLineWidth());
            if (!result.ok()) return result;
        }
        auto e=studio::catalytic::update(doc);std::set<ObjectId> created;for(const auto& a:doc.arrows().records())if(a.isAlive()){r62::arrows::ArrowParams p;bool ok=false;r62::arrows::readArrow(doc,a.id,&p,&ok);if(ok&&(!originalArrows.count(a.id)||(originalParams.count(a.id)&&originalParams.at(a.id)!=r62::arrows::encodeArrowParams(p))))created.insert(a.id);}return e.ok()?studio::arrow_crossings::update(doc,&created):e;}
};
} }
