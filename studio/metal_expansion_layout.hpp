#pragma once
#include "platform_font.hpp"
// Coordination labels need a local drawing layout, not an organic-valence
// cleanup. Keep the metal and the user's scaffold fixed; move new ligands as
// rigid branches so their internal bonds, rings and stereochemistry survive.
namespace metal_expansion_layout {
using namespace r62;using namespace core;
struct Branch {ObjectId root;std::set<ObjectId> atoms;std::map<ObjectId,Vec2> original;double extent=0;};
inline double visibleBond(const Document& doc,ObjectId metal,ObjectId ligand,text::GlyphCache& glyphs){
    auto edge=doc.bonds().alive(doc.bondBetween(metal,ligand));if(!edge)return 0;
    integration::RingGeometry rings(doc);auto labels=integration::molecular::labels(doc,glyphs,rings);auto data=rings.build(*edge);
    integration::molecular::clip(data,*edge,labels,integration::molecular::setting(doc,"MarginWidth",1.6));double longest=0;
    for(const auto& shape:data.shapes)if(shape.kind==bonds::BondShapeKind::Stroke&&shape.points.size()==2)longest=std::max(longest,length(shape.points[1]-shape.points[0]));return longest;
}
inline void place(Document& doc,ObjectId metal,const Branch& branch,double angle,double distance){
    const auto origin=doc.atoms().alive(metal)->position,old=branch.original.at(branch.root)-origin;
    auto u=direction(angle);double oldLength=length(old);Vec2 v=oldLength>1e-8?old*(1/oldLength):Vec2(1,0);double c=dot(v,u),s=v.x*u.y-v.y*u.x;
    for(const auto& entry:branch.original){auto p=entry.second-branch.original.at(branch.root);doc.setAtomPosition(entry.first,origin+u*distance+Vec2(c*p.x-s*p.y,s*p.x+c*p.y));}
}
inline double crowd(const Document& doc,const Branch& branch,const std::set<ObjectId>& ignore,text::GlyphCache& glyphs){
    integration::RingGeometry rings(doc);auto labels=integration::molecular::labels(doc,glyphs,rings);double cost=0,unit=doc.defaultBondLength();
    for(auto id:branch.atoms){auto p=doc.atoms().alive(id)->position;auto a=labels.find(id);
        for(const auto& other:doc.atoms().records())if(other.isAlive()&&!branch.atoms.count(other.id)&&!ignore.count(other.id)){
            if(doc.bondBetween(id,other.id)==kInvalidObjectId){double d=length(p-other.position)/unit;if(d<.45)cost+=100*std::pow(.45-d,2);}
            auto b=labels.find(other.id);if(a!=labels.end()&&b!=labels.end()){double x=std::min(a->second.box.max.x,b->second.box.max.x)-std::max(a->second.box.min.x,b->second.box.min.x),y=std::min(a->second.box.max.y,b->second.box.max.y)-std::max(a->second.box.min.y,b->second.box.min.y);if(x>0&&y>0)cost+=100*x*y/(unit*unit);}
        }
        for(const auto& edge:doc.bonds().records())if(edge.isAlive()&&edge.beginAtom!=id&&edge.endAtom!=id&&!branch.atoms.count(edge.beginAtom)&&!branch.atoms.count(edge.endAtom)&&!ignore.count(edge.beginAtom)&&!ignore.count(edge.endAtom)){
            auto x=doc.atoms().alive(edge.beginAtom)->position,y=doc.atoms().alive(edge.endAtom)->position;
            double d=mechanism::step_detail::pointSegment(p,x,y)/unit;if(d<.25)cost+=80*std::pow(.25-d,2);
            if(a!=labels.end()){auto box=a->second.box;auto center=(box.min+box.max)*.5,v=y-x;double t=std::max(0.,std::min(1.,dot(center-x,v)/std::max(1e-8,lengthSq(v))));auto q=x+v*t;if(q.x>box.min.x&&q.x<box.max.x&&q.y>box.min.y&&q.y<box.max.y)cost+=5;}
        }
    }
    for(const auto& edge:doc.bonds().records())if(edge.isAlive()&&(branch.atoms.count(edge.beginAtom)||branch.atoms.count(edge.endAtom)))for(const auto& other:doc.bonds().records()){
        if(!other.isAlive()||branch.atoms.count(other.beginAtom)||branch.atoms.count(other.endAtom)||ignore.count(other.beginAtom)||ignore.count(other.endAtom)||edge.beginAtom==other.beginAtom||edge.beginAtom==other.endAtom||edge.endAtom==other.beginAtom||edge.endAtom==other.endAtom)continue;
        if(mechanism::step_detail::segmentsCross(doc.atoms().alive(edge.beginAtom)->position,doc.atoms().alive(edge.endAtom)->position,doc.atoms().alive(other.beginAtom)->position,doc.atoms().alive(other.endAtom)->position))cost+=20;
    }return cost;
}
inline std::set<ObjectId> run(Document& doc,const std::vector<ObjectRef>& expanded,const std::set<ObjectId>& centers,bool* changed){
    std::set<ObjectId> made,protectedAtoms;for(auto ref:expanded)if(ref.kind==ObjectKind::Atom)made.insert(ref.id);
    if(centers.empty())return protectedAtoms;
    auto provider = studio::createGlyphProvider();
    text::GlyphCache glyphs(provider.get());
    for(auto metal:centers){if(!made.count(metal)||!doc.atoms().alive(metal))continue;auto origin=doc.atoms().alive(metal)->position;
        std::vector<Branch> branches;std::vector<double> fixed;std::set<ObjectId> seen;bool chelate=false,stereo=false;
        for(auto bid:doc.incidentBonds(metal)){auto edge=doc.bonds().alive(bid);auto root=edge->beginAtom==metal?edge->endAtom:edge->beginAtom;
            if(seen.count(root)){chelate=true;continue;}Branch branch;branch.root=root;branch.atoms.insert(root);std::vector<ObjectId> queue{root};bool movable=true;
            for(size_t i=0;i<queue.size();++i){auto id=queue[i];movable&=made.count(id)!=0;for(auto link:doc.incidentBonds(id)){auto b=doc.bonds().alive(link);stereo|=made.count(b->beginAtom)&&made.count(b->endAtom)&&mechanism::step_detail::hasStereoMark(*b);auto n=b->beginAtom==id?b->endAtom:b->beginAtom;if(n!=metal&&branch.atoms.insert(n).second)queue.push_back(n);}}
            seen.insert(branch.atoms.begin(),branch.atoms.end());
            if(movable){for(auto id:branch.atoms){branch.original[id]=doc.atoms().alive(id)->position;branch.extent=std::max(branch.extent,length(branch.original[id]-origin));protectedAtoms.insert(id);}branches.push_back(branch);}
            else fixed.push_back(angleOf(doc.atoms().alive(root)->position-origin));
        }
        protectedAtoms.insert(metal);
        // Chelates and authored wedge/dash arrangements keep their geometry;
        // their multiple attachment points cannot be rotated independently.
        if(chelate||stereo||branches.empty())continue;
        const size_t total=doc.incidentBonds(metal).size();std::vector<double> slots;
        if(fixed.size()<2){double rotation=fixed.empty()?(total==2?0.:-90.):fixed.front();for(size_t i=fixed.empty()?0:1;slots.size()<branches.size();++i)slots.push_back(rotation+i*360./std::max(size_t(2),total));}
        else {auto occupied=fixed;for(auto& angle:occupied)angle=std::fmod(angle+720.,360.);while(slots.size()<branches.size()){std::sort(occupied.begin(),occupied.end());double widest=-1,angle=0;for(size_t i=0;i<occupied.size();++i){double next=i+1<occupied.size()?occupied[i+1]:occupied.front()+360,gap=next-occupied[i];if(gap>widest){widest=gap;angle=occupied[i]+gap*.5;}}angle=std::fmod(angle+720.,360.);slots.push_back(angle);occupied.push_back(angle);}}
        std::stable_sort(branches.begin(),branches.end(),[](const Branch& a,const Branch& b){return a.extent>b.extent;});std::set<ObjectId> future;for(const auto& branch:branches)future.insert(branch.atoms.begin(),branch.atoms.end());
        for(const auto& branch:branches){for(auto id:branch.atoms)future.erase(id);double best=1e100,bestAngle=slots.front(),bestDistance=doc.defaultBondLength();size_t bestIndex=0;
            for(size_t index=0;index<slots.size();++index){double distance=doc.defaultBondLength();place(doc,metal,branch,slots[index],distance);
                // Use the actual clipped bond, including font ink and document
                // margin. A nominal ACS bond can otherwise become a tiny dash
                // between wide Pd and Cl labels.
                const double minimum=std::max(doc.defaultBondLength()*.42,doc.defaultLineWidth()*4);
                for(int attempt=0;attempt<5;++attempt){double visible=visibleBond(doc,metal,branch.root,glyphs);if(visible>=minimum-1e-6)break;distance+=minimum-visible+doc.defaultBondLength()*.04;place(doc,metal,branch,slots[index],distance);}
                double score=crowd(doc,branch,future,glyphs)+(distance/doc.defaultBondLength()-1)*.001;
                if(score<best-1e-8){best=score;bestAngle=slots[index];bestDistance=distance;bestIndex=index;}
            }
            place(doc,metal,branch,bestAngle,bestDistance);slots.erase(slots.begin()+bestIndex);
            for(const auto& entry:branch.original)if(length(doc.atoms().alive(entry.first)->position-entry.second)>1e-6){if(changed)*changed=true;break;}
        }
    }return protectedAtoms;
}
}
