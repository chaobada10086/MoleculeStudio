#pragma once
#include "engine/mechanism_infer.hpp"
#include "nickname_forms.hpp"
#include "engine/reactive_carbon_hydrides.hpp"
// Resolve catalogue aliases on a scratch document before inferring the step.
namespace studio_mechanism {
using namespace r62;using namespace core;

// Short carbonyl arrows often start or end at the edge of a labelled atom
// or at a vertex shared with a different bond. Resolve them together only
// when the other arrows identify an addition, a collapse or a reciprocal pair.
inline void resolveCarbonylContacts(const Document& doc,std::vector<arrows::ArrowParams>& inputs){
    if(inputs.size()<2)return;
    const auto atomRef=[](ObjectId id){return ObjectRef(ObjectKind::Atom,id);};
    const auto bondRef=[](ObjectId id){return ObjectRef(ObjectKind::Bond,id);};
    const auto touches=[](const BondRecord& b,ObjectId id){return b.beginAtom==id||b.endAtom==id;};
    auto resolved=inputs;
    for(const auto& co:doc.bonds().records()){
        if(!co.isAlive()||co.semantics!=BondSemantics::Normal||
           (co.order!=BondOrder::Single&&co.order!=BondOrder::Double))continue;
        auto* oxygen=doc.atoms().alive(co.beginAtom);
        auto* carbon=doc.atoms().alive(co.endAtom);
        if(oxygen&&oxygen->element==6)std::swap(oxygen,carbon);
        if(!oxygen||!carbon||oxygen->element!=8||carbon->element!=6||carbon->semantics.charge||
           carbon->semantics.radical||oxygen->semantics.radical)continue;
        const bool doubleBond=co.order==BondOrder::Double;
        if(doubleBond?oxygen->semantics.charge!=0:
           oxygen->semantics.charge!=-1||doc.incidentBonds(oxygen->id).size()!=1)continue;
        const double span=length(carbon->position-oxygen->position);
        if(span<1e-6)continue;
        const auto c=atomRef(carbon->id),o=atomRef(oxygen->id),pi=bondRef(co.id);
        auto nearAtom=[&](ObjectRef ref,Vec2 point,const AtomRecord& atom){
            if(ref==atomRef(atom.id))return true;
            if(ref.valid()){
                const auto* bond=ref.kind==ObjectKind::Bond?doc.bonds().alive(ref.id):nullptr;
                if(!bond||!touches(*bond,atom.id))return false;
            }
            if(length(point-atom.position)<=span*.28)return true;
            return atom.element!=6&&arrows::mechanismBoxDistance(arrows::mechanismAtomBodyBox(doc,atom),point)<=span*.12;
        };
        auto nearBond=[&](ObjectRef ref,Vec2 point,const BondRecord& bond){
            if(ref==bondRef(bond.id))return true;
            if(ref.valid()&&(ref.kind!=ObjectKind::Atom||!touches(bond,ref.id)))return false;
            const auto begin=doc.atoms().alive(bond.beginAtom)->position;
            const auto axis=doc.atoms().alive(bond.endAtom)->position-begin;
            const double norm=dot(axis,axis);
            if(norm<1e-8)return false;
            const double t=dot(point-begin,axis)/norm;
            return t>=-.04&&t<=1.04&&length(point-(begin+axis*t))<=span*.24;
        };
        struct Hit {size_t index;ObjectRef source,target;};
        std::vector<Hit> opening,closing,attack,departure;
        for(size_t i=0;i<inputs.size();++i){
            const auto& a=inputs[i];
            if(a.head.sides!=arrows::HeadSides::Both)continue;
            const auto from=a.electronSource,to=a.electronTarget;
            if(doubleBond&&from!=o&&nearBond(from,a.begin,co)&&nearAtom(to,a.end,*oxygen))
                opening.push_back({i,pi,o});
            if(from==o&&(nearBond(to,a.end,co)||nearAtom(to,a.end,*carbon)))
                closing.push_back({i,o,pi});
            if(doubleBond&&from.kind==ObjectKind::Atom&&from!=o&&from!=c&&
               doc.bondBetween(from.id,carbon->id)==kInvalidObjectId&&nearAtom(to,a.end,*carbon))
                attack.push_back({i,from,c});
            for(auto id:doc.incidentBonds(carbon->id)){
                const auto* sigma=doc.bonds().alive(id);
                if(!sigma||id==co.id||sigma->order!=BondOrder::Single||sigma->semantics!=BondSemantics::Normal)continue;
                const auto* leaving=doc.atoms().alive(sigma->beginAtom==carbon->id?sigma->endAtom:sigma->beginAtom);
                if(!leaving||leaving->element==6||leaving->element==1)continue;
                if(from==bondRef(id)&&nearAtom(to,a.end,*leaving))
                    departure.push_back({i,bondRef(id),atomRef(leaving->id)});
            }
        }
        auto bind=[&](const Hit& hit){
            auto& a=resolved[hit.index];
            if(a.electronSource!=hit.source)a.sourceVisualAnchor=hit.source.kind==ObjectKind::Atom?1:5;
            if(a.electronTarget!=hit.target)a.targetVisualAnchor=hit.target.kind==ObjectKind::Atom?1:5;
            a.electronSource=hit.source;a.electronTarget=hit.target;
        };
        if(doubleBond&&opening.size()==1&&(attack.size()==1||closing.size()==1)){
            bind(opening.front());
            if(attack.size()==1&&attack.front().index!=opening.front().index)bind(attack.front());
            if(closing.size()==1)bind(closing.front());
            if(closing.size()==1&&attack.size()==1&&departure.size()==1)bind(departure.front());
        }else if(!doubleBond&&closing.size()==1&&departure.size()==1){
            bind(closing.front());bind(departure.front());
        }
    }
    inputs.swap(resolved);
}

// Recover a carbonyl radical addition whose incoming tip snapped to an
// incident line close to the carbon. The caller must validate the resulting
// step and ask before changing a two-electron arrow into a one-electron hook.
inline bool correctCarbonylRadicalArrows(const Document& doc,std::vector<arrows::ArrowParams>& inputs,bool& changesElectronCount){
    changesElectronCount=false;
    if(inputs.size()!=2)return false;
    for(size_t i=0;i<inputs.size();++i){
        auto& attack=inputs[i];
        auto& pi=inputs[1-i];
        if(!attack.mechanism||!pi.mechanism||
           (attack.head.sides!=arrows::HeadSides::Left&&attack.head.sides!=arrows::HeadSides::Right)||
           attack.electronSource.kind!=ObjectKind::Atom||pi.electronSource.kind!=ObjectKind::Bond||
           pi.electronTarget.kind!=ObjectKind::Atom)continue;
        auto radical=doc.atoms().alive(attack.electronSource.id);
        auto bond=doc.bonds().alive(pi.electronSource.id);
        auto oxygen=doc.atoms().alive(pi.electronTarget.id);
        if(!radical||radical->semantics.radical!=uint8_t(Radical::Doublet)||radical->semantics.charge||
           !oxygen||oxygen->element!=8||oxygen->semantics.charge||oxygen->semantics.radical||
           !bond||bond->semantics!=BondSemantics::Normal||bond->order!=BondOrder::Double||
           (bond->beginAtom!=oxygen->id&&bond->endAtom!=oxygen->id))continue;
        auto carbon=doc.atoms().alive(bond->beginAtom==oxygen->id?bond->endAtom:bond->beginAtom);
        if(!carbon||carbon->element!=6||carbon->semantics.charge||carbon->semantics.radical||carbon->id==radical->id)continue;
        const auto carbonRef=ObjectRef(ObjectKind::Atom,carbon->id);
        const bool needsAnchor=attack.electronTarget!=carbonRef;
        const bool needsHook=pi.head.sides==arrows::HeadSides::Both;
        if(!needsAnchor&&!needsHook)continue;
        if(needsAnchor){
            if(attack.electronTarget.kind==ObjectKind::Bond){
                auto hit=doc.bonds().alive(attack.electronTarget.id);
                if(!hit||(hit->beginAtom!=carbon->id&&hit->endAtom!=carbon->id))continue;
            }else if(attack.electronTarget.valid())continue;
            const double carbonylLength=length(carbon->position-oxygen->position);
            if(length(attack.end-carbon->position)>carbonylLength*.23)continue;
        }
        attack.electronTarget=carbonRef;
        attack.targetVisualAnchor=1;
        if(needsHook){
            pi.presetId=57;
            pi.head.sides=arrows::HeadSides::Left;
        }
        changesElectronCount=needsHook;
        return true;
    }
    return false;
}
// An attack drawn a few points off a saturated carbon may snap to one
// of its adjacent bonds. Only repair it when the other arrow identifies
// the departing C–X bond and its leaving atom.
inline bool correctSubstitutionTarget(const Document& doc,std::vector<arrows::ArrowParams>& inputs){
    if(inputs.size()!=2)return false;
    for(size_t i=0;i<inputs.size();++i){
        auto& attack=inputs[i];
        const auto& departure=inputs[1-i];
        if(attack.head.sides!=arrows::HeadSides::Both||departure.head.sides!=arrows::HeadSides::Both||
           attack.electronSource.kind!=ObjectKind::Atom)continue;
        const auto center=mechanism::step_detail::substitutionCarbon(doc,{departure.electronSource,departure.electronTarget});
        if(center==kInvalidObjectId||attack.electronSource.id==center||attack.electronSource==departure.electronTarget)continue;
        const auto target=ObjectRef(ObjectKind::Atom,center);
        if(attack.electronTarget==target)continue;
        const auto origin=doc.atoms().alive(center)->position;
        const auto leaving=doc.atoms().alive(departure.electronTarget.id)->position;
        if(length(attack.end-origin)>length(leaving-origin)*.24)continue;
        if(attack.electronTarget.valid()){
            bool adjacent=false;
            for(auto id:doc.incidentBonds(center)){
                const auto& bond=*doc.bonds().alive(id);
                const auto neighbor=bond.beginAtom==center?bond.endAtom:bond.beginAtom;
                if(attack.electronTarget==ObjectRef(ObjectKind::Bond,id)||
                   (attack.electronTarget==ObjectRef(ObjectKind::Atom,neighbor)&&!doc.atomLabel(neighbor).empty())){
                    adjacent=true;
                    break;
                }
            }
            if(!adjacent)continue;
        }
        attack.electronTarget=target;
        attack.targetVisualAnchor=1;
        return true;
    }
    return false;
}
inline bool correctRingMigrationTarget(const Document& doc,std::vector<arrows::ArrowParams>& inputs){
    std::vector<mechanism::ElectronMove> moves;
    for(const auto& arrow:inputs)if(arrow.head.sides==arrows::HeadSides::Both)
        moves.emplace_back(arrow.electronSource,arrow.electronTarget);
    size_t correction=inputs.size();
    ObjectRef target;
    for(size_t i=0;i<inputs.size();++i){
        const auto& arrow=inputs[i];
        if(arrow.head.sides!=arrows::HeadSides::Both||arrow.electronSource.kind!=ObjectKind::Bond||
           arrow.electronTarget.kind!=ObjectKind::Bond)continue;
        const auto* source=doc.bonds().alive(arrow.electronSource.id);
        const auto* snapped=doc.bonds().alive(arrow.electronTarget.id);
        if(!source||source->order!=BondOrder::Single||!snapped)continue;
        auto span=[&](const BondRecord& bond){
            return length(doc.atoms().alive(bond.beginAtom)->position-doc.atoms().alive(bond.endAtom)->position);
        };
        const auto lengthLimit=std::min(span(*source),span(*snapped))*.24;
        for(auto id:{snapped->beginAtom,snapped->endAtom}){
            if(length(arrow.end-doc.atoms().alive(id)->position)>lengthLimit)continue;
            const ObjectRef candidate(ObjectKind::Atom,id);
            if(mechanism::step_detail::ringMigrationEndpoint(doc,{arrow.electronSource,candidate},moves)==kInvalidObjectId)continue;
            if(correction!=inputs.size())return false;
            correction=i;target=candidate;
        }
    }
    if(correction==inputs.size())return false;
    inputs[correction].electronTarget=target;
    inputs[correction].targetVisualAnchor=1;
    return true;
}
inline size_t includeDrawnHalogenDepartures(const Document& doc,std::vector<arrows::ArrowParams>& inputs,std::vector<ObjectId>& ids){
    const auto original=inputs.size();
    for(size_t i=0;i<original;++i){auto attack=inputs[i];if(attack.head.sides!=arrows::HeadSides::Both||attack.electronSource.kind!=ObjectKind::Atom||attack.electronTarget.kind!=ObjectKind::Atom)continue;
        auto center=doc.atoms().alive(attack.electronTarget.id);if(!center||center->element!=6||doc.bondBetween(attack.electronSource.id,center->id)!=kInvalidObjectId)continue;
        std::vector<std::pair<ObjectId,arrows::ArrowParams>> candidates;bool included=false;
        for(const auto& record:doc.arrows().records())if(record.isAlive()){
            arrows::ArrowParams p;bool valid=false;if(!arrows::readArrow(doc,record.id,&p,&valid).ok()||!valid||!p.mechanism||p.head.sides!=arrows::HeadSides::Both||p.head.atBegin||!p.head.atEnd||p.electronSource.kind!=ObjectKind::Bond||p.electronTarget.kind!=ObjectKind::Atom)continue;
            auto b=doc.bonds().alive(p.electronSource.id);auto x=doc.atoms().alive(p.electronTarget.id);if(!b||!x||b->order!=BondOrder::Single||b->semantics!=BondSemantics::Normal||
                (x->element!=9&&x->element!=17&&x->element!=35&&x->element!=53)||!((b->beginAtom==center->id&&b->endAtom==x->id)||(b->endAtom==center->id&&b->beginAtom==x->id)))continue;
            // Ignore stale bindings whose visible stroke has been dragged away.
            Vec2 bondPoint;arrows::mechanismAnchorPoint(doc,p.electronSource,&bondPoint);double unit=length(center->position-x->position);
            if(length(p.begin-bondPoint)>std::max(8.,unit*.65)||arrows::mechanismBoxDistance(arrows::mechanismAtomBodyBox(doc,*x),p.end)>3.)continue;
            if(std::find(ids.begin(),ids.end(),record.id)!=ids.end())included=true;
            else candidates.emplace_back(record.id,p);
        }
        if(!included&&candidates.size()==1){ids.push_back(candidates[0].first);inputs.push_back(candidates[0].second);}
    }return inputs.size()-original;
}
inline Error prepareProduct(const Document& source,const std::vector<ObjectRef>& selected,
                            const std::vector<arrows::ArrowParams>& inputs,Document* prepared,
                            std::vector<mechanism::ElectronMove>* moves,
                            std::vector<ObjectRef>* products,size_t* preservedCount=nullptr){
    if(!prepared||!moves||!products)return Error(ErrorCode::InvalidArgument,"缺少机理生成输出容器。");
    *prepared=source;moves->clear();products->clear();if(preservedCount)*preservedCount=0;
    const auto carbonHydrides=studio_carbon_hydrides::normalize(*prepared,inputs);
    std::set<ObjectId> seeds;
    auto add=[&](ObjectRef ref){if(ref.kind==ObjectKind::Atom&&source.atoms().alive(ref.id))seeds.insert(ref.id);else if(ref.kind==ObjectKind::Bond){if(auto b=source.bonds().alive(ref.id)){seeds.insert(b->beginAtom);seeds.insert(b->endAtom);}}else{ObjectId a,b;if(virtual_bonds::endpoints(source,ref,a,b)){seeds.insert(a);seeds.insert(b);}}};
    for(auto ref:selected)add(ref);
    for(const auto& p:inputs)for(int side=0;side<2;++side){
        auto bound=side?p.electronTarget:p.electronSource;
        if(bound.valid()){add(bound);continue;}
        Vec2 point=side?p.end:p.begin;double unit=mechanism::infer_detail::localUnit(source,point);
        for(const auto& a:source.atoms().records())if(a.isAlive()){double score;ObjectRef ref(ObjectKind::Atom,a.id);if(mechanism::infer_detail::nearRef(source,ref,point,Vec2(),unit,&score))add(ref);}
        for(const auto& b:source.bonds().records())if(b.isAlive()){double score;ObjectRef ref(ObjectKind::Bond,b.id);if(mechanism::infer_detail::nearRef(source,ref,point,Vec2(),unit,&score))add(ref);}
    }
    std::vector<ObjectId> queue(seeds.begin(),seeds.end());
    for(size_t i=0;i<queue.size();++i){
        if(queue.size()>500)return Error(ErrorCode::InvalidArgument,"所选反应结构过大，请缩小选区。");
        for(auto id:source.incidentBonds(queue[i]))if(auto b=source.bonds().alive(id)){auto other=b->beginAtom==queue[i]?b->endAtom:b->beginAtom;if(seeds.insert(other).second)queue.push_back(other);}
    }
    for(auto id:seeds){
        if(carbonHydrides.count(id))continue;
        const auto label=source.atomLabel(id);
        if(label.empty())continue;
        int typedCharge=0;bool explicitCharge=false;
        if(const auto* ion=studioions::find(label,&typedCharge,&explicitCharge))if(explicitCharge||ion->hidden||label!=elementSymbol(source.atoms().alive(id)->element)){
            auto e=studioions::apply(*prepared,id,*ion,explicitCharge?typedCharge:source.atoms().alive(id)->semantics.charge);
            if(!e.ok())return e;
            continue;
        }
        auto fragment=functional_groups::definition(source,id);
        if(fragment.empty()){
            const auto canonical=nicknameform::oriented(label,false);
            if(auto alias=nickdata::findAlias(canonical))fragment=alias->fragment;
        }
        if(!fragment.empty())
            prepared->extensions(ObjectRef(ObjectKind::Atom,id)).set(
                fragment.find("ExternalConnectionPoint")==std::string::npos?"studio.compound.fragment":"studio.nickname.fragment",fragment);
    }
    auto corrected=inputs;
    // A near-vertex sigma arrow can be saved as pointing to the incident
    // bond. Its ring-opening interpretation can also satisfy valence, so
    // resolve the concerted ring migration before evaluating that outcome.
    const bool migration=correctRingMigrationTarget(source,corrected);
    auto error=mechanism::inferElectronMoves(*prepared,migration?corrected:inputs,moves,&source);
    if(!error.ok()&&!migration){
        if(correctSubstitutionTarget(source,corrected)){
            auto retry=mechanism::inferElectronMoves(*prepared,corrected,moves,&source);
            if(retry.ok())error=retry;
        }
    }
    if(!error.ok())return error;
    error=mechanism::generateNextStep(*prepared,*moves,products);
    if(!error.ok())return error;
    if(preservedCount)for(auto ref:*products)
        if(ref.kind==ObjectKind::Atom&&static_cast<const Document&>(*prepared).extensions(ref).findValue("studio.nickname.fragment"))
            ++*preservedCount;
    return Error();
}
}
