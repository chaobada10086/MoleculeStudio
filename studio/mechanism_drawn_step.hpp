#pragma once
#include "mechanism_step_prepare.hpp"
// A drawing operation may intentionally describe a nonstandard or incomplete
// mechanism. Replay the user's arrows without a chemical-validity gate.
namespace studio_drawn_step {
using namespace r62;using namespace core;
inline ObjectRef contact(const Document& doc,const arrows::ArrowParams& arrow,bool target){
    const auto point=target?arrow.end:arrow.begin;
    const double unit=mechanism::infer_detail::localUnit(doc,point);
    const auto bound=target?arrow.electronTarget:arrow.electronSource;
    if(bound.kind==ObjectKind::Atom&&doc.atoms().alive(bound.id))return bound;
    if(bound.kind==ObjectKind::Arrow)return target&&virtual_bonds::isGuide(doc,bound)?bound:ObjectRef();
    if(bound.kind==ObjectKind::Bond&&doc.bonds().alive(bound.id)){
        const auto& b=*doc.bonds().alive(bound.id);
        // An atom donating to its own bond is a bond-order change. Keep
        // that target even when the arrow tip is near a shared endpoint.
        const bool ownBond=arrow.electronSource==ObjectRef(ObjectKind::Atom,b.beginAtom)||
                           arrow.electronSource==ObjectRef(ObjectKind::Atom,b.endAtom);
        if(target&&arrow.electronSource.kind==ObjectKind::Bond){
            const auto* giving=doc.bonds().alive(arrow.electronSource.id);
            if(giving&&giving->id!=b.id){
                for(auto shared:{giving->beginAtom,giving->endAtom}){
                    if(shared!=b.beginAtom&&shared!=b.endAtom)continue;
                    const auto opposite=shared==b.beginAtom?b.endAtom:b.beginAtom;
                    // A tip on the far half of a bound adjacent bond increases
                    // that bond's order. Its inset from the vertex is cosmetic.
                    if(length(doc.atoms().alive(opposite)->position-point)<unit*.36)return bound;
                }
            }
        }
        if(target&&!ownBond)for(auto id:{b.beginAtom,b.endAtom})
            if(length(doc.atoms().alive(id)->position-point)<unit*.36)return ObjectRef(ObjectKind::Atom,id);
        return bound;
    }
    if(bound.valid())return ObjectRef();
    ObjectRef best;double score=1e100;
    for(const auto& atom:doc.atoms().records())if(atom.isAlive()){
        ObjectRef ref(ObjectKind::Atom,atom.id);double value=0;
        if(mechanism::infer_detail::nearRef(doc,ref,point,Vec2(),unit,&value)){
            const double gap=length(atom.position-point)/unit;
            value+=gap<.25?-.2:.04;
            if(value<score){score=value;best=ref;}
        }
    }
    for(const auto& bond:doc.bonds().records())if(bond.isAlive()){
        ObjectRef ref(ObjectKind::Bond,bond.id);double value=0;
        if(mechanism::infer_detail::nearRef(doc,ref,point,Vec2(),unit,&value)&&value<score){score=value;best=ref;}
    }
    if(target)for(const auto& guide:doc.arrows().records())if(guide.isAlive()){
        const ObjectRef ref(ObjectKind::Arrow,guide.id);double value=0;
        if(virtual_bonds::isGuide(doc,ref)&&mechanism::infer_detail::nearRef(doc,ref,point,Vec2(),unit,&value)&&value<score){score=value;best=ref;}
    }
    return best;
}
inline std::set<ObjectId> participants(const Document& doc,const arrows::ArrowParams& arrow){
    std::set<ObjectId> atoms;
    for(bool target:{false,true}){
        const auto ref=contact(doc,arrow,target);
        if(ref.kind==ObjectKind::Atom)atoms.insert(ref.id);
        else if(ref.kind==ObjectKind::Bond){const auto& b=*doc.bonds().alive(ref.id);atoms.insert(b.beginAtom);atoms.insert(b.endAtom);}
        else{ObjectId a,b;if(virtual_bonds::endpoints(doc,ref,a,b)){atoms.insert(a);atoms.insert(b);}}
    }
    std::vector<ObjectId> queue(atoms.begin(),atoms.end());
    for(size_t i=0;i<queue.size();++i)for(auto bid:doc.incidentBonds(queue[i])){
        const auto& b=*doc.bonds().alive(bid);auto next=b.beginAtom==queue[i]?b.endAtom:b.beginAtom;
        if(atoms.insert(next).second)queue.push_back(next);
    }
    return atoms;
}
// In a polar carbon-metal addition, the accompanying pi-to-heteroatom
// arrow identifies the acceptor. Rotating the drawing cannot change the
// nucleophilic end of the sigma bond.
inline ObjectId polarCarbonEnd(const Document& doc,const BondRecord& sigma,ObjectId acceptor,
                               const std::vector<mechanism::ElectronMove>& moves){
    if(sigma.order!=BondOrder::Single||doc.atoms().alive(acceptor)->element!=6)return kInvalidObjectId;
    for(auto carbon:{sigma.beginAtom,sigma.endAtom}){
        if(doc.atoms().alive(carbon)->element!=6)continue;
        const auto metal=doc.atoms().alive(carbon==sigma.beginAtom?sigma.endAtom:sigma.beginAtom)->element;
        if(metal!=3&&metal!=12&&metal!=13&&metal!=30&&metal!=49)continue;
        for(const auto& shift:moves){
            if(shift.electronCount!=2||shift.source.kind!=ObjectKind::Bond||shift.target.kind!=ObjectKind::Atom)continue;
            const auto& pi=*doc.bonds().alive(shift.source.id);
            if(pi.order!=BondOrder::Double)continue;
            const auto hetero=shift.target.id;
            if(!((pi.beginAtom==acceptor&&pi.endAtom==hetero)||(pi.endAtom==acceptor&&pi.beginAtom==hetero)))continue;
            const auto element=doc.atoms().alive(hetero)->element;
            if(element==8||element==7)return carbon;
        }
    }
    return kInvalidObjectId;
}
// A pi bond and a C-metal sigma bond donating into each other specify
// both ends of a three-membered ring. The halide transfer completes
// the charge balance and distinguishes it from two unrelated additions.
inline std::map<ObjectId,ObjectId> concertedAdditionEnds(const Document& doc,const std::vector<mechanism::ElectronMove>& moves){
    std::map<ObjectId,ObjectId> ends;
    for(const auto& attack:moves){
        if(attack.electronCount!=2||attack.source.kind!=ObjectKind::Bond||attack.target.kind!=ObjectKind::Atom)continue;
        const auto& pi=*doc.bonds().alive(attack.source.id);
        auto carbon=attack.target.id;
        if(pi.order!=BondOrder::Double||doc.atoms().alive(pi.beginAtom)->element!=6||
           doc.atoms().alive(pi.endAtom)->element!=6||doc.atoms().alive(carbon)->element!=6)continue;
        for(const auto& close:moves){
            if(close.electronCount!=2||close.source.kind!=ObjectKind::Bond||close.target.kind!=ObjectKind::Atom)continue;
            auto tip=close.target.id;
            if(tip!=pi.beginAtom&&tip!=pi.endAtom)continue;
            const auto& sigma=*doc.bonds().alive(close.source.id);
            if(sigma.order!=BondOrder::Single||(sigma.beginAtom!=carbon&&sigma.endAtom!=carbon))continue;
            auto metal=sigma.beginAtom==carbon?sigma.endAtom:sigma.beginAtom;
            auto z=doc.atoms().alive(metal)->element;
            if(z!=30&&z!=12&&z!=3)continue;
            for(const auto& loss:moves){
                if(loss.electronCount!=2||loss.source.kind!=ObjectKind::Bond||loss.target.kind!=ObjectKind::Atom)continue;
                const auto& cx=*doc.bonds().alive(loss.source.id);
                if(cx.order!=BondOrder::Single||(cx.beginAtom!=carbon&&cx.endAtom!=carbon))continue;
                auto x=cx.beginAtom==carbon?cx.endAtom:cx.beginAtom;
                auto element=doc.atoms().alive(x)->element;
                if((element!=9&&element!=17&&element!=35&&element!=53)||(loss.target.id!=metal&&loss.target.id!=x))continue;
                ends[pi.id]=tip==pi.beginAtom?pi.endAtom:pi.beginAtom;
                ends[sigma.id]=carbon;
                if(loss.target.id==metal)ends[cx.id]=x;
            }
        }
    }
    return ends;
}
inline Error prepare(const Document& sourceInput,const std::vector<ObjectRef>& selected,
                     const std::vector<arrows::ArrowParams>& arrowsIn,Document& doc,
                     std::vector<mechanism::ElectronMove>& moves,std::vector<ObjectRef>& made){
    using namespace mechanism::step_detail;
    Document normalized=sourceInput;studio_mechanism::resolveHaloformCenter(normalized,arrowsIn);studio_carbon_hydrides::normalize(normalized,arrowsIn);const Document& source=normalized;
    moves.clear();made.clear();std::vector<ObjectRef> refs=selected;
    std::set<std::pair<ObjectRef,ObjectRef>> used;
    std::map<ObjectRef,int> electronsFrom;
    for(const auto& arrow:arrowsIn){
        const auto from=contact(source,arrow,false),to=contact(source,arrow,true);
        if(!from.valid()||!to.valid())return fail("箭头端点附近没有可关联的原子或化学键，请把端点靠近要编辑的位置。");
        const bool single=arrow.head.sides==arrows::HeadSides::Left||arrow.head.sides==arrows::HeadSides::Right;
        if(!used.insert({from,to}).second)return fail("有重复的机理箭头，请只保留一次电子转移。");
        const int electrons=single?1:2;
        electronsFrom[from]+=electrons;
        const mechanism::ElectronMove next(from,to,electrons);
        bool split=electronsFrom[from]==4 && std::any_of(moves.begin(),moves.end(),[&](const mechanism::ElectronMove& old){return separatesDoubleBond(source,old,next);});
        if(electronsFrom[from]>2&&!split)return fail("同一电子来源超过两个电子，请检查重复的箭头。");
        moves.emplace_back(from,to,electrons);
        for(auto id:participants(source,arrow))refs.emplace_back(ObjectKind::Atom,id);
    }
    auto error=studio_mechanism_products::cloneStage(source,refs,doc,made);if(!error.ok())return error;
    std::map<ObjectId,ObjectId> copied;
    for(auto ref:made)if(ref.kind==ObjectKind::Atom)copied[studio_mechanism_products::sourceAtom(doc,ref.id)]=ref.id;
    std::map<Edge,int> changes;std::map<ObjectId,int> charges,radicals;std::set<ObjectId> donated;
    std::map<Edge,int> halfBonds;std::map<ObjectRef,int> singleUses;
    for(const auto& move:moves)if(move.electronCount==1)++singleUses[move.source];
    auto radicalDelta=[&](ObjectId id,int delta){if(!radicals.count(id))radicals[id]=source.atoms().alive(id)->semantics.radical==uint8_t(Radical::Doublet)?1:0;radicals[id]+=delta;};
    std::set<ObjectId> explicitBondSources;
    for(const auto& move:moves)if(move.source.kind==ObjectKind::Bond)explicitBondSources.insert(move.source.id);
    std::vector<Edge> additions;
    auto add=[&](ObjectId a,ObjectId b){if(a==b)return;const auto link=edge(a,b);++changes[link];additions.push_back({a,b});};
    auto transfer=[&](ObjectId donor,ObjectId acceptor){++charges[donor];--charges[acceptor];};
    auto closest=[&](const BondRecord& bond,Vec2 point){
        return length(source.atoms().alive(bond.beginAtom)->position-point)<=length(source.atoms().alive(bond.endAtom)->position-point)?bond.beginAtom:bond.endAtom;
    };
    const auto concerted=concertedAdditionEnds(source,moves);
    for(size_t index=0;index<moves.size();++index){
        const auto& move=moves[index];
        if(move.source==move.target)return fail("机理箭头起点和终点捕捉到了同一个对象，请重新指定端点。");
        if(move.electronCount==1&&move.target.kind!=ObjectKind::Arrow){
            if(move.source.kind==ObjectKind::Atom){auto donor=move.source.id,acceptor=move.target.id;
                if(move.target.kind==ObjectKind::Bond){const auto& b=*source.bonds().alive(move.target.id);acceptor=b.beginAtom==donor?b.endAtom:b.endAtom==donor?b.beginAtom:closest(b,arrowsIn[index].end);}
                ++halfBonds[edge(donor,acceptor)];radicalDelta(donor,-1);
            }else{
                const auto& b=*source.bonds().alive(move.source.id);auto broken=edge(b.beginAtom,b.endAtom);
                if(move.target.kind==ObjectKind::Bond){const auto& t=*source.bonds().alive(move.target.id);ObjectId shared=kInvalidObjectId;for(auto id:{b.beginAtom,b.endAtom})if(id==t.beginAtom||id==t.endAtom)shared=id;if(shared==kInvalidObjectId)return fail("鱼钩的键到键转移需要共享原子。");
                    --halfBonds[broken];++halfBonds[edge(t.beginAtom,t.endAtom)];if(singleUses[move.source]==1){--halfBonds[broken];radicalDelta(b.beginAtom==shared?b.endAtom:b.beginAtom,1);}
                }else{auto target=move.target.id;if(target!=b.beginAtom&&target!=b.endAtom)return fail("请把断键鱼钩指向该键的端点，或明确完整的成键电子来源。");--halfBonds[broken];radicalDelta(target,1);if(singleUses[move.source]==1){--halfBonds[broken];radicalDelta(b.beginAtom==target?b.endAtom:b.beginAtom,1);}}
            }
            continue;
        }
        ObjectId guideBegin,guideEnd;
        if(virtual_bonds::endpoints(source,move.target,guideBegin,guideEnd)){
            if(move.electronCount!=2)return fail("请用双电子机理箭头指定待成键，或画完整的一对自由基鱼钩箭头。");
            ObjectId donor=move.source.id,other=kInvalidObjectId;
            if(move.source.kind==ObjectKind::Bond){
                const auto& sigma=*source.bonds().alive(move.source.id);
                const bool begin=sigma.beginAtom==guideBegin||sigma.beginAtom==guideEnd;
                const bool end=sigma.endAtom==guideBegin||sigma.endAtom==guideEnd;
                if(begin==end)return fail("来源键与待成键需要共用一个原子。");
                donor=begin?sigma.beginAtom:sigma.endAtom;other=begin?sigma.endAtom:sigma.beginAtom;
                --changes[edge(donor,other)];
            }else if(move.source.kind!=ObjectKind::Atom|| (donor!=guideBegin&&donor!=guideEnd))
                return fail("电子来源需要与待成键共用一个原子。");
            add(guideBegin,guideEnd);
            transfer(other==kInvalidObjectId?donor:other,donor==guideBegin?guideEnd:guideBegin);
            if(other==kInvalidObjectId)donated.insert(donor);
            continue;
        }
        if(virtual_bonds::isGuide(source,move.source))return fail("待成键是成键目标，请从原子或已有化学键开始画电子箭头。");
        if(move.source.kind==ObjectKind::Atom){
            const auto donor=move.source.id;ObjectId acceptor=move.target.id;
            if(move.target.kind==ObjectKind::Bond){
                const auto& b=*source.bonds().alive(move.target.id);
                if(b.beginAtom==donor||b.endAtom==donor)acceptor=b.beginAtom==donor?b.endAtom:b.beginAtom;
                else{
                    const auto point=arrowsIn[index].end;
                    const auto midpoint=(source.atoms().alive(b.beginAtom)->position+source.atoms().alive(b.endAtom)->position)*.5;
                    acceptor=closest(b,length(point-midpoint)<source.defaultBondLength()*.1?source.atoms().alive(donor)->position:point);
                    // An arrow directed into a pi bond requests addition there.
                    // Supply its local pi shift when no separate arrow uses it.
                    if(int(b.order)>1&&!explicitBondSources.count(b.id)){
                        --changes[edge(b.beginAtom,b.endAtom)];
                        transfer(acceptor,b.beginAtom==acceptor?b.endAtom:b.beginAtom);
                    }
                }
            }
            add(donor,acceptor);donated.insert(donor);
            if(move.electronCount==2)transfer(donor,acceptor);
            else{radicals[donor]=0;radicals[acceptor]=source.atoms().alive(acceptor)->semantics.radical?0:int(Radical::Doublet);}
        }else{
            const auto& bond=*source.bonds().alive(move.source.id);const auto original=edge(bond.beginAtom,bond.endAtom);
            --changes[original];ObjectId target=move.target.id;
            if(move.target.kind==ObjectKind::Bond){
                const auto& receiving=*source.bonds().alive(move.target.id);ObjectId shared=kInvalidObjectId;
                for(auto id:{bond.beginAtom,bond.endAtom})if(id==receiving.beginAtom||id==receiving.endAtom)shared=id;
                if(shared!=kInvalidObjectId){
                    add(receiving.beginAtom,receiving.endAtom);
                    transfer(bond.beginAtom==shared?bond.endAtom:bond.beginAtom,receiving.beginAtom==shared?receiving.endAtom:receiving.beginAtom);continue;
                }
                if(bond.order==BondOrder::Double&&receiving.order==BondOrder::Double)
                    return fail("两根 π 键之间的成键端点无法确定，请把箭头指向受攻原子，或补齐受体 π 键的电子去向。");
                target=closest(receiving,arrowsIn[index].end);
            }
            if(target==bond.beginAtom||target==bond.endAtom){
                const auto other=target==bond.beginAtom?bond.endAtom:bond.beginAtom;
                if(move.electronCount==2)transfer(other,target);
                else{radicals[other]=int(Radical::Doublet);radicals[target]=int(Radical::Doublet);}
            }else{
                const auto migrating=ringMigrationEndpoint(source,mechanism::ElectronMove(move.source,ObjectRef(ObjectKind::Atom,target),move.electronCount),moves);
                auto resolved=concerted.find(bond.id);
                auto donor=resolved==concerted.end()?migrating:resolved->second;
                if(donor==kInvalidObjectId&&move.electronCount==2)
                    donor=polarCarbonEnd(source,bond,target,moves);
                if(donor==kInvalidObjectId&&bond.order==BondOrder::Double){
                    // A simultaneous donation into an adjacent sigma bond uses
                    // the pi endpoint on that bond, leaving the other end free.
                    std::set<ObjectId> restored;
                    for(const auto& incoming:moves){
                        if(incoming.electronCount!=2||incoming.target.kind!=ObjectKind::Bond)continue;
                        const auto& receiving=*source.bonds().alive(incoming.target.id);
                        if(receiving.order!=BondOrder::Single)continue;
                        ObjectId shared=kInvalidObjectId;
                        if(incoming.source.kind==ObjectKind::Atom)shared=incoming.source.id;
                        else if(incoming.source.kind==ObjectKind::Bond){
                            const auto& giving=*source.bonds().alive(incoming.source.id);
                            if(giving.id==receiving.id)continue;
                            for(auto id:{giving.beginAtom,giving.endAtom})
                                if(id==receiving.beginAtom||id==receiving.endAtom)shared=id;
                        }
                        if(shared!=receiving.beginAtom&&shared!=receiving.endAtom)continue;
                        const auto partner=shared==receiving.beginAtom?receiving.endAtom:receiving.beginAtom;
                        if(partner==bond.beginAtom||partner==bond.endAtom)restored.insert(partner);
                    }
                    if(restored.size()==1)donor=*restored.begin()==bond.beginAtom?bond.endAtom:bond.beginAtom;
                }
                if(donor==kInvalidObjectId)donor=closest(bond,source.atoms().alive(target)->position);
                add(donor,target);transfer(donor==bond.beginAtom?bond.endAtom:bond.beginAtom,target);
            }
        }
    }
    for(const auto& item:halfBonds){if(item.second%2)return fail("鱼钩成键的单电子尚未配对，请补齐对应的电子来源。");changes[item.first]+=item.second/2;}
    for(const auto& item:radicals)if(item.second<0||item.second>1)return fail("鱼钩电子来源或自由基计数不完整，请检查箭头起点及重叠的重复箭头。");
    bool changed=!radicals.empty();
    for(const auto& bond:changes)changed|=bond.second!=0;
    for(const auto& charge:charges)changed|=charge.second!=0;
    if(!changed)
        return fail("这些箭头互相抵消，没有产生下一步结构。");
    std::set<ObjectId> edited;for(const auto& item:changes)if(item.second){edited.insert(item.first.first);edited.insert(item.first.second);}for(const auto& item:charges)if(item.second)edited.insert(item.first);for(const auto& item:radicals)edited.insert(item.first);
    for(const auto& item:copied){
        const auto* old=source.atoms().alive(item.first);
        const auto label=source.atomLabel(item.first);int typedCharge=0;bool explicitCharge=false;
        const auto* ion=studioions::find(label,&typedCharge,&explicitCharge);
        if(!ion && donated.count(item.first))ion=mechanism::compactLonePairGroup(label);
        if(ion)if(explicitCharge||ion->hidden||label!=elementSymbol(old->element)){
            error=studioions::apply(doc,item.second,*ion,explicitCharge?typedCharge:old->semantics.charge);if(!error.ok())return error;
        }
        auto sem=doc.atoms().alive(item.second)->semantics;
        if(edited.count(item.first)&&sem.implicitHydrogens<0&&old->semantics.nodeType==uint8_t(NodeType::Element))
            sem.implicitHydrogens=std::max(0,chemistry::refreshAtomDerived(source,item.first,{}).newHydrogenCount);
        if(edited.count(item.first)&&mechanism::singletCarbene(*old)){
            int bonded=0;for(auto bid:source.incidentBonds(old->id))bonded+=int(source.bonds().alive(bid)->order);
            sem.implicitHydrogens=std::max(0,2-bonded);
        }
        sem.charge=int8_t(std::max(-8,std::min(8,int(sem.charge)+charges[item.first])));
        if(radicals.count(item.first))sem.radical=radicals[item.first]?uint8_t(Radical::Doublet):uint8_t(Radical::None);
        if((donated.count(item.first)||sem.charge)&&sem.radical==uint8_t(Radical::Singlet))sem.radical=0;
        error=doc.setAtomSemantics(item.second,sem);if(!error.ok())return error;
        if(donated.count(item.first)){
            auto& bag=doc.extensions(ObjectRef(ObjectKind::Atom,item.second));
            const int count=mechanism::lonePairCount(source,item.first);
            if(count>1)bag.set(mechanism::lonePairKey(),std::to_string(count-1));else bag.erase(mechanism::lonePairKey());
        }
    }
    for(const auto& item:changes){
        const auto a=copied.at(item.first.first),b=copied.at(item.first.second);
        auto bid=doc.bondBetween(a,b);const auto* old=doc.bonds().alive(bid);
        const int order=(old?int(old->order):0)+item.second;
        if(order<0||order>3)return fail("这些电子转移产生了不支持的键级，请检查该键上的成键和断键箭头。");
        if(!order){if(old){error=doc.removeObject(ObjectRef(ObjectKind::Bond,bid));if(!error.ok())return error;}continue;}
        if(!old){error=doc.addBond(a,b,&bid);if(!error.ok())return error;made.emplace_back(ObjectKind::Bond,bid);}
        error=doc.setBondOrder(bid,BondOrder(order));if(!error.ok())return error;
        if(item.second){doc.setBondDisplay(bid,BondDisplay::Normal);doc.setBondStereo(bid,BondStereo::None);}
    }
    // A neutral, divalent carbon left by alpha elimination is a carbene, not
    // an ordinary skeletal carbon with an invisible valence deficit.
    for(const auto& item:copied){
        const auto* old=source.atoms().alive(item.first);const auto* a=doc.atoms().alive(item.second);
        if(edited.count(item.first)&&a->semantics.nodeType==uint8_t(NodeType::Element)&&(a->element==1||a->element==6||a->element==7||a->element==8)){
            int sum=std::max(0,int(a->semantics.implicitHydrogens));for(auto bid:doc.incidentBonds(a->id))sum+=int(doc.bonds().alive(bid)->order);
            int maximum=valence(a->element,a->semantics.charge)-(a->semantics.radical==uint8_t(Radical::Doublet)?1:0);
            if(maximum>=0&&sum>maximum)return fail(std::string(elementSymbol(a->element))+" ("+std::to_string(sum)+" > "+std::to_string(maximum)+") "+
                "生成产物出现超价：该原子的键级之和（含氢）超过允许值，请核对成键、断键箭头和电荷。");
        }
        if(old->element!=6||a->semantics.charge||a->semantics.radical||a->semantics.nodeType!=uint8_t(NodeType::Element))continue;
        int valence=std::max(0,int(a->semantics.implicitHydrogens));for(auto bid:doc.incidentBonds(a->id))valence+=int(doc.bonds().alive(bid)->order);
        if(valence!=2)continue;bool eliminated=false;
        for(auto bid:source.incidentBonds(old->id)){auto b=source.bonds().alive(bid);auto other=b->beginAtom==old->id?b->endAtom:b->beginAtom;auto leaving=source.atoms().alive(other);
            if(leaving->element!=1&&leaving->element!=6&&copied.count(other)&&doc.bondBetween(a->id,copied.at(other))==kInvalidObjectId)eliminated=true;
        }
        if(eliminated){auto sem=a->semantics;sem.radical=uint8_t(Radical::Singlet);auto e=doc.setAtomSemantics(a->id,sem);if(!e.ok())return e;}
    }
    // Bring separate partners together, keeping the heavier side stationary.
    const auto layoutMasses=studio_fragment_mass::weights(doc);
    for(const auto& link:additions){
        auto a=copied.at(link.first),b=copied.at(link.second);const auto excluded=doc.bondBetween(a,b);
        std::vector<ObjectId> branch{a};std::set<ObjectId> seen{a};
        for(size_t i=0;i<branch.size();++i)for(auto bid:doc.incidentBonds(branch[i]))if(bid!=excluded){
            const auto& edge=*doc.bonds().alive(bid);auto next=edge.beginAtom==branch[i]?edge.endAtom:edge.beginAtom;
            if(seen.insert(next).second)branch.push_back(next);
        }
        if(seen.count(b))continue;
        std::vector<ObjectId> other{b};std::set<ObjectId> otherSeen{b};
        for(size_t i=0;i<other.size();++i)for(auto bid:doc.incidentBonds(other[i]))if(bid!=excluded){const auto& edge=*doc.bonds().alive(bid);auto next=edge.beginAtom==other[i]?edge.endAtom:edge.beginAtom;if(otherSeen.insert(next).second)other.push_back(next);}
        double firstMass=0,secondMass=0;for(auto id:branch)firstMass+=layoutMasses.at(id);for(auto id:other)secondMass+=layoutMasses.at(id);
        if(firstMass>secondMass+1e-6){std::swap(a,b);branch.swap(other);}
        const auto delta=doc.atoms().alive(a)->position-doc.atoms().alive(b)->position;
        const double distance=length(delta);if(distance<1e-6)continue;
        const auto shift=delta*(source.defaultBondLength()/distance-1);
        for(auto id:branch)doc.setAtomPosition(id,doc.atoms().alive(id)->position+shift);
    }
    return Error();
}
}
