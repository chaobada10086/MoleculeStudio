#pragma once
#include "molecular_render.hpp"
#include "mechanism_electrons.hpp"
#include "compound_catalog.hpp"
#include "terminal_atom_drag.hpp"
#include <memory>
namespace r62 { namespace arrows {
inline double mechanismRadicalRadius(const core::Document& doc,core::ObjectId atom=core::kInvalidObjectId){double em=integration::molecular::setting(doc,"LabelSize",10);if(atom!=core::kInvalidObjectId)for(const auto& run:studioatom::runs(doc,atom))if(run.size>0&&!(run.flags&(core::kSubscript|core::kSuperscript))){em=run.size;break;}return std::max(.45,em*.09);}
inline core::Vec2 avoidRadicalBonds(const core::Document& doc,const core::AtomRecord& atom,core::Vec2 point,double em,double radius,const text::RichLayout* label,bool automatic=false){
    using namespace core;constexpr double pi=3.14159265358979323846;
    std::vector<Vec2> bonds;std::vector<double> angles;std::vector<ObjectId> neighbors;double nearest=1e100;Vec2 away(0,-1);
    const double pad=radius+std::max(.3,integration::molecular::setting(doc,"LineWidth",.6)*.5)+em*.03;
    auto distance=[](Vec2 p,Vec2 v){double t=std::max(0.,std::min(1.,dot(p,v)/std::max(1e-12,dot(v,v))));return length(p-v*t);};
    for(auto id:doc.incidentBonds(atom.id))if(auto bond=doc.bonds().alive(id)){
        if(bond->semantics==BondSemantics::Hydrogen)continue;auto other=doc.atoms().alive(bond->beginAtom==atom.id?bond->endAtom:bond->beginAtom);if(!other)continue;
        auto v=other->position-atom.position;if(length(v)<1e-8)continue;bonds.push_back(v);neighbors.push_back(other->id);double angle=std::atan2(v.y,v.x);if(angle<0)angle+=2*pi;angles.push_back(angle);
        auto gap=distance(point-atom.position,v);if(gap<nearest){nearest=gap;away=v*(-1./length(v));}
    }
    if((!automatic&&nearest>=pad)||angles.empty())return point;
    if(nearest>=pad)away=Vec2(0,-1);
    // A bridgehead has equally wide sectors inside both rings and outside.
    // Find local cycles through this atom, then prefer sectors outside them.
    std::vector<std::vector<Vec2>> cycles;
    for(size_t a=0;a<neighbors.size();++a)for(size_t b=a+1;b<neighbors.size();++b){
        std::vector<ObjectId> queue{neighbors[a]};std::map<ObjectId,ObjectId> parent;parent[neighbors[a]]=atom.id;
        for(size_t k=0;k<queue.size()&&!parent.count(neighbors[b]);++k)for(auto next:doc.neighbours(queue[k]))if(next!=atom.id&&!parent.count(next)){parent[next]=queue[k];queue.push_back(next);}
        if(!parent.count(neighbors[b]))continue;std::vector<Vec2> polygon{atom.position};
        for(auto id=neighbors[b];id!=atom.id;id=parent.at(id))polygon.push_back(doc.atoms().alive(id)->position);cycles.push_back(polygon);
    }
    auto inRing=[&](Vec2 p){for(const auto& polygon:cycles){bool inside=false;for(size_t i=0,j=polygon.size()-1;i<polygon.size();j=i++){auto a=polygon[i],b=polygon[j];if((a.y>p.y)!=(b.y>p.y)&&p.x<(b.x-a.x)*(p.y-a.y)/(b.y-a.y)+a.x)inside=!inside;}if(inside)return true;}return false;};
    std::sort(angles.begin(),angles.end());double best=-1,bestFacing=-2;int bestRing=2;Vec2 placed=point;
    for(size_t i=0;i<angles.size();++i){double next=i+1<angles.size()?angles[i+1]:angles.front()+2*pi;double angle=(angles[i]+next)*.5;Vec2 ray(std::cos(angle),std::sin(angle));
        double span=std::max(em*(automatic?.55:.8),length(point-atom.position));
        if(label&&!label->metrics.inkBox.empty()){
            const auto& ink=label->metrics.inkBox;double exit=1e100;
            if(std::abs(ray.x)>1e-8)exit=std::min(exit,((ray.x>0?ink.max.x+radius*1.8:ink.min.x-radius*1.8)-atom.position.x)/ray.x);
            if(std::abs(ray.y)>1e-8)exit=std::min(exit,((ray.y>0?ink.max.y+radius*1.8:ink.min.y-radius*1.8)-atom.position.y)/ray.y);
            if(exit>0&&exit<em*1.4)span=std::max(span,exit);
        }
        double clearance=1e100;for(auto v:bonds)clearance=std::min(clearance,distance(ray*span,v));
        if(clearance<pad)continue;double opening=next-angles[i],facing=dot(ray,away);
        int ring=inRing(atom.position+ray*span)?1:0;
        if(ring<bestRing||(ring==bestRing&&(opening>best+1e-7||(std::abs(opening-best)<1e-7&&facing>bestFacing)))){bestRing=ring;best=opening;bestFacing=facing;placed=atom.position+ray*span;}
    }
    return placed;
}
inline std::vector<core::Vec2> mechanismRadicalPositions(const core::Document& doc,const core::AtomRecord& atom,double em,const text::RichLayout* label){
    std::vector<core::Vec2> result;if(!atom.semantics.radical)return result;double radius=mechanismRadicalRadius(doc,atom.id);
    auto center=atom.position+core::Vec2(em*.55,-em*.55);
    if(label&&!label->metrics.inkBox.empty()){const auto& ink=label->metrics.inkBox;center=core::Vec2(ink.max.x+radius*1.4,ink.min.y+radius*.2);}
    if(atom.semantics.radical==uint8_t(core::Radical::Doublet)){
        center=atom.position+core::Vec2(em*.7,0);
        if(label&&!label->metrics.inkBox.empty()){
            double low=1e100,high=-1e100;
            for(const auto& glyph:label->glyphs)if(glyph.size>=em*.9)for(const auto& contour:glyph.contours)for(auto point:contour){double y=point.y+label->anchor.y;low=std::min(low,y);high=std::max(high,y);}
            const auto& ink=label->metrics.inkBox;center=core::Vec2(ink.max.x+radius*1.4,high>=low?(low+high)*.5:(ink.min.y+ink.max.y)*.5);
        }
    }
    if(!label&&atom.element==6&&atom.semantics.radical==uint8_t(core::Radical::Singlet)){
        std::vector<double> occupied;
        for(auto bid:doc.incidentBonds(atom.id)){
            const auto& bond=*doc.bonds().alive(bid);
            auto delta=doc.atoms().alive(bond.beginAtom==atom.id?bond.endAtom:bond.beginAtom)->position-atom.position;
            occupied.push_back(std::atan2(delta.y,delta.x));
        }
        double best=-1;
        for(int k=0;k<48;++k){
            const double angle=k*6.283185307179586/48;double clearance=3.141592653589793;
            for(double used:occupied)clearance=std::min(clearance,std::acos(std::cos(angle-used)));
            if(clearance>best+1e-8){best=clearance;center=atom.position+core::Vec2(std::cos(angle),std::sin(angle))*(em*.64);}
        }
    }
    const auto* placement=doc.extensions(core::ObjectRef(core::ObjectKind::Atom,atom.id)).findValue(integration::molecular::electronMarkerKey(true));
    const bool automatic=atom.element==6&&!doc.incidentBonds(atom.id).empty()&&(!placement||*placement=="auto");
    if(automatic&&atom.semantics.radical==uint8_t(core::Radical::Doublet)){
        center=atom.position+core::Vec2(0,-em*.55);
        if(label&&!label->metrics.inkBox.empty())center.y=std::min(center.y,label->metrics.inkBox.min.y-radius*1.8);
    }
    center=integration::molecular::electronMarkerCenter(doc,atom,true,em,center);
    if(atom.semantics.radical==uint8_t(core::Radical::Doublet))center=avoidRadicalBonds(doc,atom,center,em,radius,label,automatic);
    bool pair=atom.semantics.radical==uint8_t(core::Radical::Triplet)||atom.semantics.radical==uint8_t(core::Radical::Singlet);
    auto radial=center-atom.position;double distance=core::length(radial);
    const auto tangent=!label&&distance>1e-8?core::Vec2(-radial.y/distance,radial.x/distance):core::Vec2(1,0);
    for(int dot=0;dot<(pair?2:1);++dot){auto point=center;if(pair)point=point+tangent*(dot==0?-radius*1.65:radius*1.65);result.push_back(point);}return result;
}
inline bool drawChargeMarker(const core::Document& doc,const core::AtomRecord& atom){
    const auto label=doc.atomLabel(atom.id);
    return std::abs(int(atom.semantics.charge))==1&&
        (integration::molecular::attachedElectronMarker(doc,atom.id,false)||
         (atom.element==6&&!doc.incidentBonds(atom.id).empty()&&(label.empty()||label=="C")&&!atom.semantics.showLabel&&!atom.semantics.isotope));
}
// Recognize the exact three-offset azide preset emitted by earlier versions.
// A changed position breaks the pattern, so individually positioned charges
// retain their authored offsets. This is a display-only compatibility repair.
inline bool legacyAzideChargeOffset(const core::Document& doc,const core::AtomRecord& atom,core::Vec2* offset){
    if(atom.element!=7)return false;std::set<core::ObjectId> seen{atom.id};std::vector<core::ObjectId> ids{atom.id};
    for(size_t i=0;i<ids.size();++i)for(auto bid:doc.incidentBonds(ids[i])){auto b=doc.bonds().alive(bid);if(b->order!=core::BondOrder::Double)return false;auto n=b->beginAtom==ids[i]?b->endAtom:b->beginAtom;if(seen.insert(n).second)ids.push_back(n);if(ids.size()>3)return false;}
    if(ids.size()!=3)return false;int negatives=0,positives=0;core::Vec2 common;bool first=true;
    for(auto id:ids){auto a=doc.atoms().alive(id);if(a->element!=7||a->semantics.radical||a->semantics.isotope)return false;int q=a->semantics.charge;if(q==-1&&doc.incidentBonds(id).size()==1)++negatives;else if(q==1&&doc.incidentBonds(id).size()==2)++positives;else return false;
        auto raw=doc.extensions(core::ObjectRef(core::ObjectKind::Atom,id)).findValue(integration::molecular::electronMarkerKey(false));if(!raw)return false;std::istringstream in(*raw);core::Vec2 p;if(!(in>>p.x>>p.y)||std::abs(core::length(p)-1.05)>1e-7)return false;auto oriented=p*double(-q);if(!first&&core::length(oriented-common)>1e-7)return false;common=oriented;first=false;if(id==atom.id)*offset=p*(.80/1.05);
    }return negatives==2&&positives==1;
}
inline core::Vec2 chargeMarkerCenter(const core::Document& doc,const core::AtomRecord& atom,double em,const text::RichLayout* label){
    core::Vec2 legacy;if(legacyAzideChargeOffset(doc,atom,&legacy))return atom.position+legacy*em;
    const bool left=integration::molecular::automaticChargeOnLeft(doc,atom);
    auto center=atom.position+core::Vec2((left?-1.:1.)*em*.48,-em*.48);
    if(label&&!label->metrics.inkBox.empty()){
        const auto& ink=label->metrics.inkBox;
        center=core::Vec2(left?ink.min.x-em*.25:ink.max.x+em*.25,ink.min.y);
    }else if(atom.element==6&&!integration::molecular::attachedElectronMarker(doc,atom.id,false)){
        std::vector<core::Vec2> directions;
        for(auto id:doc.incidentBonds(atom.id))if(auto bond=doc.bonds().alive(id)){
            auto other=doc.atoms().alive(bond->beginAtom==atom.id?bond->endAtom:bond->beginAtom);
            if(!other)continue;
            const auto delta=other->position-atom.position;
            const double distance=core::length(delta);
            if(distance>1e-8)directions.push_back(delta*(1./distance));
        }
        if(directions.size()>=3){
            double best=-1;
            constexpr double pi=3.14159265358979323846;
            for(int i=0;i<24;++i){
                const double angle=-pi/4+i*pi/12;
                const core::Vec2 direction(std::cos(angle),std::sin(angle));
                double clearance=2;
                for(auto occupied:directions)clearance=std::min(clearance,1-core::dot(direction,occupied));
                if(clearance>best+1e-6){best=clearance;center=atom.position+direction*(em*.70);}
            }
        }
    }
    return integration::molecular::electronMarkerCenter(doc,atom,false,em,center);
}
inline int attachedHydrogenCount(const core::Document& doc,const core::AtomRecord& atom){
    if(atom.semantics.radical||atom.semantics.aromatic)return 0;
    auto label=doc.atomLabel(atom.id);
    if(!atom.semantics.charge&&doc.incidentBonds(atom.id).empty()&&compounddata::hydride(label))return 1;
    // All exact elemental hydrides, not only OH and positively charged atoms.
    // This includes neutral amide/imide NH, amines, thiols, silanes and CHn.
    const int written=studio::hydride_labels::count(atom.element,label);
    if(written>0)return written;
    if(!label.empty()&&label!=core::elementSymbol(atom.element)){
        const int terminal=studio::hydride_labels::terminal(label);
        if(terminal>0)return terminal;
    }
    const bool neutralHydroxyl=atom.semantics.charge==0&&
        (atom.element==8||((label=="OH"||label=="HO")&&atom.element==6&&
            atom.semantics.nodeType!=uint8_t(core::NodeType::Element)));
    if(neutralHydroxyl){
        const auto& links=doc.incidentBonds(atom.id);
        if(links.size()!=1||(label!=""&&label!="O"&&label!="OH"&&label!="HO"))return 0;
        const auto* bond=doc.bonds().alive(links.front());
        if(!bond||bond->order!=core::BondOrder::Single||bond->semantics!=core::BondSemantics::Normal)return 0;
        const auto* other=doc.atoms().alive(bond->beginAtom==atom.id?bond->endAtom:bond->beginAtom);
        if(!other||other->element==1)return 0;
        if(label=="OH"||label=="HO")return atom.element==6||atom.semantics.implicitHydrogens!=0?1:0;
        if(atom.semantics.nodeType!=uint8_t(core::NodeType::Element))return 0;
        return integration::molecular::labelHydrogenCount(doc,atom)==1?1:0;
    }
    if(atom.element<=1||atom.semantics.nodeType!=uint8_t(core::NodeType::Element)||atom.semantics.implicitHydrogens<=0)return 0;
    const auto& attrs=doc.extensions(core::ObjectRef(core::ObjectKind::Atom,atom.id));
    if(attrs.findValue("pst.attr.MoleculeStudioLiteralLabel"))return 0;
    const std::string element=core::elementSymbol(atom.element);
    const int count=atom.semantics.implicitHydrogens;
    if(label.empty()||label==element)return count;
    if(atom.semantics.charge==1&&label.back()=='+')label.pop_back();
    const std::string hydrogens=count==1?"H":"H"+std::to_string(count);
    return label==element+hydrogens||label==hydrogens+element?count:0;
}
struct MechanismVisualBoxes {
    core::Box body,charge,attachedProton,attachedProtonFirst;
    bool hasBody=false,hasCharge=false,hasAttachedProton=false,hasAttachedProtonFirst=false;
    std::vector<core::Box> labelGlyphs,lonePairs;
    std::vector<core::Vec2> radicalPoints;
    core::Box terminal;bool hasTerminal=false;int terminalElement=0;
    core::Vec2 lonePairHost;
};
inline const std::map<core::ObjectId,MechanismVisualBoxes>& mechanismVisualBoxes(const core::Document& doc){
    struct Cache {const core::Document* owner=nullptr;core::DocumentId id=0;uint64_t revision=~uint64_t(0);std::unique_ptr<text::GlyphProvider> provider;std::unique_ptr<text::GlyphCache> glyphs;std::map<core::ObjectId,MechanismVisualBoxes> boxes;
        Cache():provider(text::createGdiGlyphProvider("Arial","Microsoft YaHei")),glyphs(new text::GlyphCache(provider.get())){} };
    static Cache cache;
    if(cache.owner==&doc&&cache.id==doc.id()&&cache.revision==doc.revision())return cache.boxes;
    cache.owner=&doc;cache.id=doc.id();cache.revision=doc.revision();cache.boxes.clear();
    integration::RingGeometry rings(doc);auto labels=integration::molecular::labels(doc,*cache.glyphs,rings);
    for(const auto& entry:labels){auto& result=cache.boxes[entry.first];const auto& layout=entry.second.layout;std::set<uint16_t> chargeRuns;
        for(const auto& g:layout.glyphs)if(g.codepoint=='+'||g.codepoint=='-'||g.codepoint==0x2212)chargeRuns.insert(g.runIndex);
        auto atom=doc.atoms().alive(entry.first);
        const text::GlyphPlacement* mainGroup=nullptr;
        if(atom){
            const auto* compact=mechanism::lonePairGroup(doc.atomLabel(atom->id));
            if(compact&&(compact->element==8||compact->element==7||compact->element==16)){
                double best=1e100;
                for(const auto& g:layout.glyphs)if(g.codepoint==uint32_t(core::elementSymbol(compact->element)[0])){
                    const auto p=layout.anchor+core::Vec2(g.x+g.advance*.5,g.y);
                    const double distance=core::lengthSq(p-atom->position);
                    if(distance<best){best=distance;mainGroup=&g;}
                }
            }
        }
        if(atom&&(atom->element==50||atom->element==14))for(size_t i=0;i+1<layout.glyphs.size();++i)if(layout.glyphs[i].codepoint=='S'&&layout.glyphs[i+1].codepoint==(atom->element==50?'n':'i')){mainGroup=&layout.glyphs[i];break;}
        const auto* phosphorus=atom?mechanism::triphenylphosphineGlyph(doc.atomLabel(atom->id),layout):nullptr;
        const bool printedPair=mechanism::hasPrintedLonePair(doc,entry.first);
        if(atom)if(auto end=studio_terminal_drag::find(doc.atomLabel(atom->id))){
            for(size_t i=end->offset;i<end->offset+end->token.size()&&i<layout.glyphs.size();++i)for(const auto& contour:layout.glyphs[i].contours)for(auto p:contour){p=p+layout.anchor;if(result.hasTerminal)result.terminal.add(p);else{result.terminal=core::Box(p,p);result.hasTerminal=true;}}
            result.terminalElement=end->element;
        }
        const bool attachedProton=atom&&attachedHydrogenCount(doc,*atom)>0;
        for(const auto& g:layout.glyphs){bool sign=g.codepoint=='+'||g.codepoint=='-'||g.codepoint==0x2212;
            bool digit=g.codepoint>='0'&&g.codepoint<='9';bool charge=atom&&atom->semantics.charge&&(sign||(!phosphorus&&digit&&chargeRuns.count(g.runIndex)&&g.size<doc.defaultTextSize()*.9));
            core::Box glyph;bool hasInk=false;
            for(const auto& contour:g.contours)for(auto point:contour){
                point=point+layout.anchor;
                if(hasInk)glyph.add(point);
                else{glyph=core::Box(point,point);hasInk=true;}
            }
            if(!hasInk)continue;
            if(printedPair&&g.codepoint==':'){result.lonePairs.push_back(glyph);continue;}
            if(!charge)result.labelGlyphs.push_back(glyph);
            if(attachedProton&&!charge&&g.codepoint=='H'){
                result.attachedProton=glyph;
                result.hasAttachedProton=true;
                if(atom&&!doc.atomLabel(atom->id).empty()&&doc.atomLabel(atom->id).front()=='H'&&&g==&layout.glyphs.front()){result.attachedProtonFirst=glyph;result.hasAttachedProtonFirst=true;}
            }
            // The H and count glyphs are clickable, while the arrow is still
            // routed towards the element glyph at the chemical attachment.
            if(phosphorus&&!charge&&(&g!=phosphorus))continue;
            if(mainGroup&&!charge&&&g!=mainGroup&&!(atom&&(atom->element==50||atom->element==14)&&&g==mainGroup+1))continue;
            if(!charge&&(digit||(atom&&atom->element!=1&&g.codepoint=='H'&&layout.glyphs.size()>1)))continue;
            auto& box=charge?result.charge:result.body;auto& exists=charge?result.hasCharge:result.hasBody;
            if(exists){box.add(glyph.min);box.add(glyph.max);}
            else {box=glyph;exists=true;}
        }
    }
    double em=integration::molecular::setting(doc,"LabelSize",10);
    for(const auto& atom:doc.atoms().records())if(atom.isAlive()&&drawChargeMarker(doc,atom)){
        auto label=labels.find(atom.id);
        auto center=chargeMarkerCenter(doc,atom,em,label==labels.end()?nullptr:&label->second.layout);double half=em*.19,stroke=std::max(.6,em*.08)*.5;auto& result=cache.boxes[atom.id];
        const bool circled=integration::molecular::circledElectronCharge(doc,atom.id);
        if(circled)half=integration::molecular::electronChargeCircleRadius(em);
        const auto extent=core::Vec2(half+stroke,(circled||atom.semantics.charge>0)?half+stroke:stroke);
        result.charge=core::Box(center-extent,center+extent);result.hasCharge=true;}
    for(const auto& atom:doc.atoms().records())if(atom.isAlive()&&atom.semantics.radical){auto label=labels.find(atom.id);
        cache.boxes[atom.id].radicalPoints=mechanismRadicalPositions(doc,atom,integration::molecular::setting(doc,"LabelSize",10),label==labels.end()?nullptr:&label->second.layout);}
    for(const auto& atom:doc.atoms().records())if(atom.isAlive()&&!mechanism::hasPrintedLonePair(doc,atom.id)){
        const double radius=mechanism::lonePairDotRadius(em);
        const core::Vec2 pad(radius,radius);
        for(int i=0;i<mechanism::lonePairCount(doc,atom.id);++i){
            const auto label=labels.find(atom.id);
            const auto* layout=label==labels.end()?nullptr:&label->second.layout;
            cache.boxes[atom.id].lonePairHost=mechanism::lonePairDragHost(doc,atom,layout);
            const auto dots=mechanism::lonePairDots(doc,atom,i,em,layout);
            core::Box area(dots[0]-pad,dots[0]+pad);
            area.add(dots[1]-pad);area.add(dots[1]+pad);
            cache.boxes[atom.id].lonePairs.push_back(area);
        }
    }
    return cache.boxes;
}
inline double mechanismBoxDistance(const core::Box& box,core::Vec2 point){
    return std::hypot(std::max(0.,std::max(box.min.x-point.x,point.x-box.max.x)),
                      std::max(0.,std::max(box.min.y-point.y,point.y-box.max.y)));
}
inline core::Box mechanismLabelEnvelope(const MechanismVisualBoxes& visual){
    if(visual.labelGlyphs.empty())return visual.body;
    auto box=visual.labelGlyphs.front();
    for(const auto& glyph:visual.labelGlyphs){box.add(glyph.min);box.add(glyph.max);}
    return box;
}
inline const core::Box* mechanismClosestLonePair(const core::Document& doc,core::ObjectId atom,core::Vec2 point){
    const auto& boxes=mechanismVisualBoxes(doc);
    auto found=boxes.find(atom);
    const core::Box* best=nullptr;
    double distance=1e100;
    if(found!=boxes.end())for(const auto& box:found->second.lonePairs){
        const auto current=mechanismBoxDistance(box,point);
        if(current<distance){best=&box;distance=current;}
    }
    return best;
}
inline std::vector<core::Vec2> mechanismRadicalPoints(const core::Document& doc,core::ObjectId atom){const auto& boxes=mechanismVisualBoxes(doc);auto found=boxes.find(atom);return found==boxes.end()?std::vector<core::Vec2>():found->second.radicalPoints;}
inline core::Box mechanismAtomBodyBox(const core::Document& doc,const core::AtomRecord& a,double pad=0){
    const auto& boxes=mechanismVisualBoxes(doc);auto found=boxes.find(a.id);core::Box box(a.position,a.position);
    if(found!=boxes.end()&&found->second.hasBody)box=found->second.body;
    box.min=box.min-core::Vec2(pad,pad);box.max=box.max+core::Vec2(pad,pad);return box;
}
inline core::Box mechanismChargeBox(const core::Document& doc,const core::AtomRecord& a,double pad=0){
    const auto& boxes=mechanismVisualBoxes(doc);auto found=boxes.find(a.id);core::Box box(a.position,a.position);
    if(found!=boxes.end()&&found->second.hasCharge)box=found->second.charge;
    box.min=box.min-core::Vec2(pad,pad);box.max=box.max+core::Vec2(pad,pad);return box;
}
} }
