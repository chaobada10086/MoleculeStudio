#pragma once
#include "engine/molecular_render.hpp"
#include "engine/mechanism_visual_anchors.hpp"
#include "engine/name_link.hpp"
#include "platform_font.hpp"
#include "next/selection/bounds.hpp"
#include <memory>
#include <set>
#include <vector>
#include <algorithm>
namespace studio { namespace clean_spacing {
using namespace r62;
using namespace r62::core;
struct Component {std::vector<ObjectId> atoms,texts;Box box;bool chosen=false,movable=false;};
inline Box shifted(Box b,Vec2 delta){return Box(b.min+delta,b.max+delta);}
inline bool overlaps(Box a,Box b,double gap){return a.max.x+gap>b.min.x&&b.max.x+gap>a.min.x&&a.max.y+gap>b.min.y&&b.max.y+gap>a.min.y;}
// Bounds use the same glyph placement as the renderer (including H and charges).
inline std::vector<Component> components(const Document& doc,const std::vector<ObjectId>& ids,const std::set<ObjectId>* movable){
    std::set<ObjectId> chosen(ids.begin(),ids.end()),seen;std::vector<Component> result;
    for(const auto& atom:doc.atoms().records())if(atom.isAlive()&&seen.insert(atom.id).second){
        Component c;c.atoms.push_back(atom.id);c.box=Box(atom.position,atom.position);c.movable=true;
        for(size_t k=0;k<c.atoms.size();++k){auto id=c.atoms[k];c.chosen|=chosen.count(id)!=0;c.movable&=chosen.count(id)!=0&&(!movable||movable->count(id)!=0);
            c.box.add(doc.atoms().alive(id)->position);
            for(auto bid:doc.incidentBonds(id)){auto b=doc.bonds().alive(bid);auto other=b->beginAtom==id?b->endAtom:b->beginAtom;if(seen.insert(other).second)c.atoms.push_back(other);}
        }
        result.push_back(c);
    }
    size_t chosenCount=0;for(const auto& c:result)chosenCount+=c.chosen;if(chosenCount<2)return result;
    auto provider = studio::createGlyphProvider();
    text::GlyphCache glyphs(provider.get());
    integration::RingGeometry rings(doc);auto labels=integration::molecular::labels(doc,glyphs,rings);
    const double em=integration::molecular::setting(doc,"LabelSize",10);
    for(auto& c:result)for(auto id:c.atoms){auto label=labels.find(id);const auto* layout=label==labels.end()?nullptr:&label->second.layout;
        if(label!=labels.end()){c.box.add(label->second.box.min);c.box.add(label->second.box.max);}
        auto atom=doc.atoms().alive(id);auto area=[&](Vec2 center,Vec2 pad){c.box.add(center-pad);c.box.add(center+pad);};
        if(arrows::drawChargeMarker(doc,*atom)){const bool circle=integration::molecular::circledElectronCharge(doc,id);double half=circle?integration::molecular::electronChargeCircleRadius(em):em*.19,stroke=std::max(.6,em*.08)*.5;area(arrows::chargeMarkerCenter(doc,*atom,em,layout),Vec2(half+stroke,(circle||atom->semantics.charge>0)?half+stroke:stroke));}
        if(atom->semantics.radical){double radius=arrows::mechanismRadicalRadius(doc,id);for(auto dot:arrows::mechanismRadicalPositions(doc,*atom,em,layout))area(dot,Vec2(radius,radius));}
        if(!mechanism::hasPrintedLonePair(doc,id)){double radius=mechanism::lonePairDotRadius(em);for(int i=0;i<mechanism::lonePairCount(doc,id);++i)for(auto dot:mechanism::lonePairDots(doc,*atom,i,em,layout))area(dot,Vec2(radius,radius));}
    }
    // A caption follows only when all linked atoms belong to this component.
    std::map<ObjectId,size_t> owner;for(size_t i=0;i<result.size();++i)for(auto id:result[i].atoms)owner[id]=i;
    for(const auto& t:doc.texts().records())if(t.isAlive()){
        auto refs=namelink::linkRefs(doc,t.id);size_t index=result.size();bool mixed=false;
        for(auto ref:refs){std::vector<ObjectId> atoms;if(ref.kind==ObjectKind::Atom)atoms.push_back(ref.id);else if(ref.kind==ObjectKind::Bond)if(auto b=doc.bonds().alive(ref.id)){atoms.push_back(b->beginAtom);atoms.push_back(b->endAtom);}
            for(auto id:atoms){auto it=owner.find(id);if(it==owner.end()){mixed=true;continue;}if(index==result.size())index=it->second;else if(index!=it->second)mixed=true;}}
        if(index<result.size()&&!mixed){auto& c=result[index];c.texts.push_back(t.id);auto b=selection::objectBounds(doc,ObjectRef(ObjectKind::Text,t.id));if(!b.empty()){c.box.add(b.min);c.box.add(b.max);}}
    }
    return result;
}
inline Error separate(Document& doc,const std::vector<ObjectId>& ids,const std::set<ObjectId>* movable=nullptr){
    if(ids.size()<2)return Error();
    auto parts=components(doc,ids,movable);size_t chosenCount=0;for(const auto& p:parts)chosenCount+=p.chosen;if(chosenCount<2)return Error();
    std::vector<size_t> moving;std::vector<Box> placed;std::set<ObjectId> captions;
    for(size_t i=0;i<parts.size();++i){auto& p=parts[i];for(auto id:p.texts)captions.insert(id);if(p.chosen&&p.movable)moving.push_back(i);else placed.push_back(p.box);}
    for(const auto& t:doc.texts().records())if(t.isAlive()&&!captions.count(t.id)){auto b=selection::objectBounds(doc,ObjectRef(ObjectKind::Text,t.id));if(!b.empty())placed.push_back(b);}
    // Keep the principal scaffold in place; translate smaller components rigidly.
    std::stable_sort(moving.begin(),moving.end(),[&](size_t a,size_t b){return parts[a].atoms.size()>parts[b].atoms.size();});
    double gap=std::max(doc.defaultBondLength()*.65,doc.defaultTextSize()*1.2);
    for(auto index:moving){auto& p=parts[index];auto clear=[&](Vec2 delta){auto b=shifted(p.box,delta);for(auto obstacle:placed)if(overlaps(b,obstacle,gap-1e-7))return false;return true;};Vec2 delta;
        if(!clear(delta)){
            std::vector<Vec2> candidates;Box all=placed.front();for(auto b:placed){all.add(b.min);all.add(b.max);}
            auto contacts=[&](Box b){double xs[]={b.min.x-gap-p.box.max.x,b.max.x+gap-p.box.min.x};double ys[]={b.min.y-gap-p.box.max.y,b.max.y+gap-p.box.min.y};for(auto x:xs)candidates.push_back(Vec2(x,0));for(auto y:ys)candidates.push_back(Vec2(0,y));for(auto x:xs)for(auto y:ys)candidates.push_back(Vec2(x,y));};
            contacts(all);if(placed.size()<128)for(auto b:placed)contacts(b);
            double best=1e300;for(auto candidate:candidates){double cost=dot(candidate,candidate);if(cost<best&&clear(candidate)){best=cost;delta=candidate;}}
            for(auto id:p.atoms){auto e=doc.setAtomPosition(id,doc.atoms().alive(id)->position+delta);if(!e.ok())return e;}
            for(auto id:p.texts){auto e=doc.setTextPosition(id,doc.texts().alive(id)->position+delta);if(!e.ok())return e;}
        }
        placed.push_back(shifted(p.box,delta));
    }
    return Error();
}
} }
