#pragma once

#include "engine/mechanism_visual_anchors.hpp"
#include "nicknames.hpp"
#include "next/arrows_curves/model.hpp"
#include "next/chemistry/valence.hpp"
#ifndef _WIN32
#include <limits>
#include <optional>
#endif

namespace studio::electron_marker {
using namespace r62;
using namespace r62::core;

struct Hit {
    ObjectId atom;
    int site;
    int pair;
    Vec2 center;

    Hit(ObjectId atomId = kInvalidObjectId, int anchor = 0, int pairIndex = -1, Vec2 position = Vec2())
        : atom(atomId), site(anchor), pair(pairIndex), center(position) {}
};

#ifndef _WIN32
inline Hit at(const Document& doc, Vec2 point, double tolerance, bool includeLonePairs) {
    Hit hit;
    double bestGap = tolerance;
    double bestDistance = std::numeric_limits<double>::max();
    const auto consider = [&](ObjectId atom, int site, int pair, Vec2 center, double gap) {
        const auto distance = length(point - center);
        if (gap > bestGap || (gap == bestGap && distance >= bestDistance)) return;
        bestGap = gap;
        bestDistance = distance;
        hit = {atom, site, pair, center};
    };
    for (const auto& item : arrows::mechanismVisualBoxes(doc)) {
        const auto& visual = item.second;
        const auto* atom = doc.atoms().alive(item.first);
        if (!atom) continue;
        const auto box = [&](const Box& bounds, int site, int pair) {
            consider(item.first, site, pair, (bounds.min + bounds.max) * .5,
                arrows::mechanismBoxDistance(bounds, point));
        };
        if (visual.hasCharge && atom->semantics.charge) box(visual.charge, 2, -1);
        if (includeLonePairs)
            for (size_t index = 0; index < visual.lonePairs.size(); ++index)
                box(visual.lonePairs[index], 3, static_cast<int>(index));
        if (visual.radicalPoints.empty()) continue;
        const auto center = (visual.radicalPoints.front() + visual.radicalPoints.back()) * .5;
        const auto radius = arrows::mechanismRadicalRadius(doc, item.first);
        for (auto dot : visual.radicalPoints)
            consider(item.first, 4, -1, center, std::max(0., length(point - dot) - radius));
    }
    return hit;
}

inline std::optional<Box> bounds(const Document& doc, const Hit& hit) {
    const auto& entries = arrows::mechanismVisualBoxes(doc);
    const auto item = entries.find(hit.atom);
    if (item == entries.end()) return {};
    const auto& visual = item->second;
    if (hit.site == 2 && visual.hasCharge) return visual.charge;
    if (hit.site == 3 && hit.pair >= 0 && static_cast<size_t>(hit.pair) < visual.lonePairs.size())
        return visual.lonePairs[hit.pair];
    if (hit.site != 4 || visual.radicalPoints.empty()) return {};
    const auto radius = arrows::mechanismRadicalRadius(doc, hit.atom);
    Box box(visual.radicalPoints.front(), visual.radicalPoints.front());
    for (auto dot : visual.radicalPoints) box.add(dot);
    return Box(box.min - Vec2(radius, radius), box.max + Vec2(radius, radius));
}
#endif

inline bool exists(const Document& doc, const Hit& hit) {
    const auto* atom = doc.atoms().alive(hit.atom);
    return atom && ((hit.site == 2 && atom->semantics.charge) ||
        (hit.site == 4 && atom->semantics.radical) ||
        (hit.site == 3 && hit.pair >= 0 && hit.pair < mechanism::lonePairCount(doc, hit.atom)));
}

inline Command remove(Hit hit) {
    const bool radical = hit.site == 4, lonePair = hit.site == 3;
    return Command(lonePair?"删除孤电子对并补氢":radical?"删除自由基或卡宾并补氢":"删除电荷并补氢",
        kInvalidateSemantics|kInvalidateAppearance|kInvalidateGeometry,[hit,radical,lonePair](CommandContext& c){
        using namespace integration::molecular;
        auto& doc=c.document;
        const auto* atom=doc.atoms().alive(hit.atom);
        if(!atom||(lonePair?(hit.pair<0||hit.pair>=mechanism::lonePairCount(doc,hit.atom)):
                   radical?!atom->semantics.radical:!atom->semantics.charge))return CommandResult::noChange();
        const auto ref=ObjectRef(ObjectKind::Atom,hit.atom);
        auto sem=atom->semantics;
        const auto element=atom->element;
        const std::string symbol=elementSymbol(element);
        const auto label=doc.atomLabel(hit.atom);
        auto base=label;
        auto& attrs=doc.extensions(ref);
        const bool literal=attrs.findValue("pst.attr.MoleculeStudioLiteralLabel")!=nullptr;
        auto isHydride=[&](const std::string& text){
            bool hydrogenFirst=false;
            size_t elementChars=0;
            return hydrideLabel(element,text,&hydrogenFirst,&elementChars);
        };
        if(lonePair){
            const int count=mechanism::lonePairCount(doc,hit.atom);
            const auto offsets=mechanism::lonePairOffsets(doc,hit.atom);
            attrs.set(mechanism::lonePairKey(),std::to_string(count-1));
            attrs.erase(mechanism::lonePairPositionsKey());
            for(const auto& entry:offsets)if(entry.first!=hit.pair&&entry.first<count)
                mechanism::setLonePairOffset(doc,hit.atom,entry.first-(entry.first>hit.pair?1:0),entry.second);
            if(mechanism::hasPrintedLonePair(doc,hit.atom))base="Nu";
        }else if(radical)sem.radical=0;
        else{
            const int charge=sem.charge;
            sem.charge=0;
            if(!literal)for(const auto& sign:charge>0?std::vector<std::string>{"+","⁺"}:
                                                             std::vector<std::string>{"-","−","⁻"}){
                const auto suffix=(std::abs(charge)>1?std::to_string(std::abs(charge)):"")+sign;
                if(base.size()<=suffix.size()||base.compare(base.size()-suffix.size(),suffix.size(),suffix)!=0)continue;
                auto plain=base.substr(0,base.size()-suffix.size());
                if(plain==symbol||isHydride(plain)||mechanism::triphenylphosphineLabel(plain))
                    base=plain;
                break;
            }
        }
        if(radical&&studioions::compact(doc,hit.atom)&&doc.incidentBonds(hit.atom).empty())for(const auto& h:compounddata::hydrides)if(base==h.base){
            auto e=nicknames::apply(doc,hit.atom,h.name);auto& bag=doc.extensions(ref);bag.erase("studio.mechanism.ion.base");bag.erase("studio.mechanism.compact.valence");bag.erase(electronMarkerKey(true));std::vector<TextRun> runs;for(const auto& part:chemcompletion::segments(h.name,true)){TextRun run;run.utf8=part.text;run.flags=part.script<0?kSubscript:part.script>0?kSuperscript:0;runs.push_back(run);}studioatom::set(doc,hit.atom,runs);return e.ok()?CommandResult::success():CommandResult::fail(e.code,e.message);
        }
        const bool hydride=isHydride(base);
        if(!literal&&(base.empty()||base==symbol||hydride)&&!sem.aromatic&&
           sem.nodeType==uint8_t(NodeType::Element)&&!attrs.findValue("studio.nickname.fragment")){
            chemistry::AtomEnvironment env;
            env.element=element;env.charge=sem.charge;env.radical=sem.radical;
            bool knownBonds=true;
            for(auto id:doc.incidentBonds(hit.atom)){
                const auto* bond=doc.bonds().alive(id);
                if(!bond||bond->semantics!=BondSemantics::Normal||bond->order<BondOrder::Single||bond->order>BondOrder::Quadruple){
                    knownBonds=false;break;
                }
                env.bondOrderSum+=int(bond->order);++env.bondCount;
            }
            const auto hydrogen=chemistry::implicitHydrogens(env);
            if(knownBonds&&hydrogen.valid){
                int count=element==1?0:hydrogen.count;
                if(element!=6&&sem.radical)
                    count-=sem.radical==uint8_t(Radical::Doublet)?1:2;
                sem.implicitHydrogens=static_cast<int8_t>(std::max(0,std::min(8,count)));
                if(hydride)base=symbol;
            }
        }
        auto error=doc.setAtomSemantics(hit.atom,sem);
        if(error.ok()&&base!=label){
            error=doc.setAtomLabel(hit.atom,base);
            if(error.ok())studioatom::set(doc,hit.atom,{});
        }
        if(!error.ok())return CommandResult::fail(error.code,error.message);
        auto& changed=doc.extensions(ref);
        if(!lonePair){
            changed.erase(electronMarkerKey(radical));
            if(!radical)changed.erase("pst.attr.MoleculeStudioChargeCircle");
        }
        return CommandResult::success();
    });
}

inline Command move(const Document& document, Hit hit, Vec2 host, Vec2 offset, double em) {
    const auto id = hit.atom;
    const auto site = hit.site, pair = hit.pair;
    const bool radical = site == 4, lonePair = site == 3;
    const auto center = host + offset;
    auto shift=center-hit.center;
    const auto oldDots=radical?arrows::mechanismRadicalPoints(document,id):std::vector<Vec2>{};
    std::vector<Box> oldPairs;
    if(lonePair)oldPairs=arrows::mechanismVisualBoxes(document).at(id).lonePairs;
    return Command("移动电子标记",kInvalidateGeometry|kInvalidateAppearance,[id,site,pair,radical,lonePair,offset,em,shift,oldDots,oldPairs](CommandContext& c){
        auto& doc=c.document;
        if (!exists(doc, {id, site, pair, {}})) return CommandResult::noChange();
        const auto host=ObjectRef(ObjectKind::Atom,id);
        if(lonePair){
            if(mechanism::hasPrintedLonePair(doc,id)){
                auto error=doc.setAtomLabel(id,"Nu");
                if(!error.ok())return CommandResult::fail(error.code,error.message);
            }
            mechanism::setLonePairOffset(doc,id,pair,offset*(1./em));
        }else doc.extensions(host).set(integration::molecular::electronMarkerKey(radical),
                                       std::to_string(offset.x/em)+" "+std::to_string(offset.y/em));
        auto follows=[&](ObjectRef atom,uint8_t visual,Vec2 position){
            if(atom!=host||visual!=site)return false;
            if(!lonePair)return true;
            if(pair<0||size_t(pair)>=oldPairs.size())return false;
            const auto distance=arrows::mechanismBoxDistance(oldPairs[pair],position);
            for(size_t index=0;index<oldPairs.size();++index)
                if(index!=size_t(pair)&&arrows::mechanismBoxDistance(oldPairs[index],position)<distance)return false;
            return true;
        };
        for(const auto& arrow:doc.arrows().records())if(arrow.isAlive()){
            arrows::ArrowParams params;bool present=false;
            auto result=arrows::readArrow(doc,arrow.id,&params,&present);
            if(!result.ok()||!present||!params.mechanism)continue;
            const bool source=follows(params.electronSource,params.sourceVisualAnchor,params.begin);
            const bool target=follows(params.electronTarget,params.targetVisualAnchor,params.end);
            if(!source&&!target)continue;
            auto moveEndpoint=[&](Vec2 endpoint){
                if(oldDots.empty())return endpoint+shift;
                auto closest=oldDots.front();
                for(auto dot:oldDots)if(length(endpoint-dot)<length(endpoint-closest))closest=dot;
                auto gap=endpoint-closest;
                const double distance=length(gap),clearance=arrows::mechanismRadicalRadius(doc,id)+1.;
                // Copies made before the marker offset was preserved can
                // have an arrow still pointing at its old dot location.
                if(distance>clearance)gap=gap*(clearance/distance);
                return closest+shift+gap;
            };
            if(source)params.begin=moveEndpoint(params.begin);
            if(target)params.end=moveEndpoint(params.end);
            result=arrows::writeArrow(doc,arrow.id,params,doc.defaultLineWidth());
            if(!result.ok())return CommandResult::fail(result.code,result.message);
        }
        return CommandResult::success();
    });
}
}
