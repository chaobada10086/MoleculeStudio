#pragma once
#include "molecular_render.hpp"
#include <memory>
namespace studio_generated_spacing {
using namespace r62;
inline double visibleLength(const core::Document& doc,const core::BondRecord& bond,
                           const integration::RingGeometry& rings,const integration::molecular::Labels& labels){
    auto data=rings.build(bond);auto axis=data.axisEnd-data.axisBegin;double span=core::length(axis);if(span<1e-8)return 0.;axis=axis*(1./span);
    integration::molecular::clip(data,bond,labels,integration::molecular::setting(doc,"MarginWidth",1.6));
    // A hashed wedge is many transverse strokes. Its visible bond length is
    // the axial envelope of ALL strokes, not the length of one tiny dash.
    double low=1e100,high=-1e100;for(const auto& shape:data.shapes){const auto& points=shape.kind==bonds::BondShapeKind::FilledPolygon?shape.outline:shape.points;
        for(auto p:points){double t=core::dot(p,axis);low=std::min(low,t);high=std::max(high,t);}}
    return high>=low?high-low:0.;
}
// Resolve collisions by rotating entire bridge-side branches. Internal ring
// geometry and stereobond orientation within each branch remain rigid.
inline core::Error separateOverlaps(core::Document& doc,const std::set<core::ObjectId>& allowed,text::GlyphCache& glyphs){
    using namespace core;if(allowed.size()<2||allowed.size()>200)return Error();
    const double unit=doc.defaultBondLength(),gap=unit*.09;
    std::map<ObjectId,Vec2> points;for(auto id:allowed)points[id]=doc.atoms().alive(id)->position;
    std::vector<std::pair<ObjectId,ObjectId>> bonds;std::map<ObjectId,std::vector<ObjectId>> adjacent;
    for(const auto& b:doc.bonds().records())if(b.isAlive()&&allowed.count(b.beginAtom)&&allowed.count(b.endAtom)){bonds.push_back({b.beginAtom,b.endAtom});adjacent[b.beginAtom].push_back(b.endAtom);adjacent[b.endAtom].push_back(b.beginAtom);}
    integration::RingGeometry rings(doc);const auto labels=integration::molecular::labels(doc,glyphs,rings);
    std::map<ObjectId,Box> offsets;for(const auto& item:labels)if(allowed.count(item.first)){auto box=item.second.box;box.min=box.min-points.at(item.first);box.max=box.max-points.at(item.first);offsets[item.first]=box;}
    size_t work=0;const size_t budget=6000000;
    auto cost=[&](const std::map<ObjectId,Vec2>& p){double total=0;
        for(auto i=allowed.begin();i!=allowed.end();++i)for(auto j=allowed.upper_bound(*i);j!=allowed.end();++j){if(++work>budget)return -1.;if(doc.bondBetween(*i,*j)!=kInvalidObjectId)continue;
            const double distance=length(p.at(*i)-p.at(*j));if(distance<unit*.5)total+=100*std::pow((unit*.5-distance)/unit,2);
            auto a=offsets.find(*i),b=offsets.find(*j);if(a==offsets.end()||b==offsets.end())continue;
            double x=std::min(a->second.max.x+p.at(*i).x,b->second.max.x+p.at(*j).x)-std::max(a->second.min.x+p.at(*i).x,b->second.min.x+p.at(*j).x)+gap;
            double y=std::min(a->second.max.y+p.at(*i).y,b->second.max.y+p.at(*j).y)-std::max(a->second.min.y+p.at(*i).y,b->second.min.y+p.at(*j).y)+gap;
            if(x>0&&y>0)total+=20*x*y/(unit*unit);
        }
        // Keep unrelated bonds out of visible label boxes, including hydrogens.
        for(const auto& edge:bonds)for(const auto& label:offsets){if(label.first==edge.first||label.first==edge.second)continue;if(++work>budget)return -1.;
            const auto a=p.at(edge.first),z=p.at(edge.second),d=z-a;double dd=lengthSq(d);if(dd<1e-8)continue;
            auto box=label.second;box.min=box.min+p.at(label.first);box.max=box.max+p.at(label.first);auto c=(box.min+box.max)*.5;
            double t=std::max(0.,std::min(1.,dot(c-a,d)/dd));auto q=a+d*t;
            if(q.x>box.min.x-gap&&q.x<box.max.x+gap&&q.y>box.min.y-gap&&q.y<box.max.y+gap)total+=2;
        }return total;
    };
    double current=cost(points);if(current<=1e-8)return Error();
    for(int pass=0;pass<4&&current>1e-8;++pass){double best=current;auto winner=points;
        for(const auto& edge:bonds){for(int side=0;side<2;++side){ObjectId pivot=side?edge.second:edge.first,start=side?edge.first:edge.second;
            std::set<ObjectId> branch{start};std::vector<ObjectId> queue{start};bool cyclic=false;
            for(size_t i=0;i<queue.size()&&!cyclic;++i)for(auto next:adjacent[queue[i]]){if(queue[i]==start&&next==pivot)continue;if(next==pivot){cyclic=true;break;}if(branch.insert(next).second)queue.push_back(next);}
            if(cyclic||branch.size()>allowed.size()/2)continue;
            // Do not rotate wedges relative to a fixed stereocenter.
            auto connection=doc.bonds().alive(doc.bondBetween(pivot,start));if(connection&&(connection->display!=BondDisplay::Normal||connection->stereo!=BondStereo::None))continue;
            bool stereoPivot=false;for(auto bid:doc.incidentBonds(pivot)){auto b=doc.bonds().alive(bid);if(b&&(b->stereo!=BondStereo::None||b->display==BondDisplay::Wedge||b->display==BondDisplay::WedgeHash||b->display==BondDisplay::HollowWedge))stereoPivot=true;}if(stereoPivot)continue;
            for(int step:{-1,1,-2,2,-3,3,-4,4,-5,5,6}){double angle=step*3.141592653589793/6;double cs=std::cos(angle),sn=std::sin(angle);auto trial=points;
                for(auto id:branch){auto q=points.at(id)-points.at(pivot);trial[id]=points.at(pivot)+Vec2(q.x*cs-q.y*sn,q.x*sn+q.y*cs);}
                double score=cost(trial);if(score<0)return Error();if(score<best-1e-7){best=score;winner=std::move(trial);}
            }
        }}
        if(best>=current-1e-7)break;points=std::move(winner);current=best;
    }
    for(const auto& item:points){auto e=doc.setAtomPosition(item.first,item.second);if(!e.ok())return e;}return Error();
}
// Only generated atoms are eligible. Move an entire bridge-side branch rigidly,
// so rings, stereobonds and all geometry inside that branch stay intact.
inline core::Error improve(core::Document& doc,const std::vector<core::ObjectRef>& generated){
    std::set<core::ObjectId> allowed;for(auto ref:generated)if(ref.kind==core::ObjectKind::Atom&&doc.atoms().alive(ref.id))allowed.insert(ref.id);
    std::unique_ptr<text::GlyphProvider> provider(text::createGdiGlyphProvider("Arial","Microsoft YaHei"));text::GlyphCache glyphs(provider.get());
    const double unit=doc.defaultBondLength();std::map<core::ObjectId,double> original;
    for(const auto& b:doc.bonds().records())if(b.isAlive()&&allowed.count(b.beginAtom)&&allowed.count(b.endAtom))original[b.id]=core::length(doc.atoms().alive(b.endAtom)->position-doc.atoms().alive(b.beginAtom)->position);
    for(int pass=0;pass<10;++pass){bool changed=false;
        for(const auto& item:original){const auto bond=*doc.bonds().alive(item.first);auto a=doc.atoms().alive(bond.beginAtom)->position,z=doc.atoms().alive(bond.endAtom)->position;auto axis=z-a;double span=core::length(axis);if(span<1e-6)continue;
            integration::RingGeometry rings(doc);auto labels=integration::molecular::clippingLabels(doc,integration::molecular::labels(doc,glyphs,rings));double visible=visibleLength(doc,bond,rings,labels);
            if(span>=unit*.85&&visible>=unit*.35)continue;
            auto side=[&](core::ObjectId start){std::set<core::ObjectId> seen{start};std::vector<core::ObjectId> todo{start};for(size_t i=0;i<todo.size();++i)for(auto bid:doc.incidentBonds(todo[i]))if(bid!=bond.id){auto b=doc.bonds().alive(bid);auto next=b->beginAtom==todo[i]?b->endAtom:b->beginAtom;if(seen.insert(next).second)todo.push_back(next);}return seen;};
            auto left=side(bond.beginAtom);if(left.count(bond.endAtom))continue;auto right=side(bond.endAtom);
            auto eligible=[&](const std::set<core::ObjectId>& ids){for(auto id:ids)if(!allowed.count(id))return false;return true;};
            bool moveRight=eligible(right)&&(!eligible(left)||right.size()<=left.size());if(!moveRight&&!eligible(left))continue;
            const double room=item.second+unit*1.5-span;double extra=std::min(room,std::min(unit*.65,std::max(unit*.9-span,unit*.45-visible)));if(extra<.01)continue;
            auto delta=axis*(extra/span*(moveRight?1.:-1.));for(auto id:moveRight?right:left){auto e=doc.setAtomPosition(id,doc.atoms().alive(id)->position+delta);if(!e.ok())return e;}changed=true;
        }
        if(!changed)break;
    }
    return separateOverlaps(doc,allowed,glyphs);
}
}
