#pragma once
#include "mechanism_step.hpp"
#include "mechanism_layout.hpp"
#include <sstream>

namespace r62 { namespace mechanism {
namespace infer_detail {
struct Candidate { core::ObjectRef ref; double score; };
inline double localUnit(const core::Document& doc,core::Vec2 point){
    std::vector<std::pair<double,double>> nearbyLengths;
    for(const auto& b:doc.bonds().records())if(b.isAlive()){
        auto a=doc.atoms().alive(b.beginAtom),z=doc.atoms().alive(b.endAtom);if(!a||!z)continue;
        double len=core::length(a->position-z->position);if(len>1)nearbyLengths.push_back({core::length((a->position+z->position)*.5-point),len});
    }
    std::sort(nearbyLengths.begin(),nearbyLengths.end());std::vector<double> lengths;
    for(size_t i=0;i<std::min(size_t(5),nearbyLengths.size());++i)lengths.push_back(nearbyLengths[i].second);
    std::sort(lengths.begin(),lengths.end());
    return std::max(6.,std::min(100.,lengths.empty()?doc.defaultBondLength():lengths[lengths.size()/2]));
}
inline bool nearRef(const core::Document& doc,core::ObjectRef ref,core::Vec2 point,core::Vec2 direction,double unit,double* score,bool directed=false){
    core::Vec2 contact;double radius=0;
    if(ref.kind==core::ObjectKind::Atom){auto a=doc.atoms().alive(ref.id);if(!a)return false;contact=a->position;
        if(a->semantics.radical){
            for(auto dot:arrows::mechanismRadicalPoints(doc,ref.id)){
                const double distance=core::length(point-dot);
                if(distance<=arrows::mechanismRadicalRadius(doc)+2.5){*score=distance/unit;return true;}
            }
        }
        if(a->semantics.charge){
            const auto& boxes=arrows::mechanismVisualBoxes(doc);
            auto found=boxes.find(ref.id);
            if(found!=boxes.end()&&found->second.hasCharge){
                const double gap=arrows::mechanismBoxDistance(found->second.charge,point);
                if(gap<=2.5){*score=gap/unit;return true;}
            }
        }
        bool label=a->element!=6||!doc.atomLabel(a->id).empty();radius=unit*(!genericRole(doc,ref.id).empty()?.85:label?.60:.40);
    }else if(ref.kind==core::ObjectKind::Bond){auto b=doc.bonds().alive(ref.id);if(!b)return false;auto a=doc.atoms().alive(b->beginAtom),z=doc.atoms().alive(b->endAtom);if(!a||!z)return false;
        auto d=z->position-a->position;double len2=d.x*d.x+d.y*d.y;if(len2<1e-8)return false;
        double t=((point.x-a->position.x)*d.x+(point.y-a->position.y)*d.y)/len2;
        contact=a->position+d*std::max(0.,std::min(1.,t));radius=unit*.56;
    }else if(virtual_bonds::isGuide(doc,ref)){
        if(!virtual_bonds::contact(doc,ref,point,&contact))return false;
        radius=unit*.56;
    }else return false;
    auto delta=contact-point;double distance=core::length(delta);
    if(ref.kind==core::ObjectKind::Atom)if(auto pair=arrows::mechanismClosestLonePair(doc,ref.id,point)){
        double gap=arrows::mechanismBoxDistance(*pair,point);
        if(gap<distance&&gap<unit*.22){contact=(pair->min+pair->max)*.5;delta=contact-point;distance=gap;}
    }
    if(distance>radius)return false;
    double alignment=0,dl=core::length(direction);if(distance>1e-6&&dl>1e-6)alignment=(delta.x*direction.x+delta.y*direction.y)/(distance*dl);
    // A separated endpoint must approach its anchor within the 45 degree
    // tangent cone. Inside the immediate capture disk the tangent is unstable
    // (especially at a bond midpoint), so retain every nearby candidate.
    // This excludes a neighbouring bond sideways below a labelled donor.
    if(directed&&dl>1e-6&&distance>unit*.18&&alignment<std::sqrt(.5))return false;
    // Tangent direction orders remaining candidates; it never chooses between two
    // chemically valid outcomes. Small handwritten curves often have loose tangents.
    *score=distance/unit+.12*(1-alignment);return true;
}
inline std::string resultKey(const core::Document& doc,const std::vector<core::ObjectRef>& created){
    std::ostringstream out;std::map<core::ObjectId,size_t> atom;
    for(auto ref:created)if(ref.kind==core::ObjectKind::Atom){auto a=doc.atoms().alive(ref.id);atom[ref.id]=atom.size();
        out<<a->element<<':'<<int(a->semantics.charge)<<':'<<int(a->semantics.implicitHydrogens)<<':'<<a->semantics.isotope<<':'<<int(a->semantics.radical)<<';';}
    out<<'|';std::vector<std::string> edges;
    for(auto ref:created)if(ref.kind==core::ObjectKind::Bond){
        auto b=doc.bonds().alive(ref.id);
        size_t a=atom.at(b->beginAtom),z=atom.at(b->endAtom);
        const auto depth=step_detail::wedgeDepth(*b);
        if(!depth&&z<a)std::swap(a,z);
        edges.push_back(std::to_string(a)+","+std::to_string(z)+":"+std::to_string(int(b->order))+":"+std::to_string(depth));
    }
    std::sort(edges.begin(),edges.end());for(auto edge:edges)out<<edge<<';';return out.str();
}
}

// Resolve all visual electron arrows together. Existing references are never
// retargeted. Unbound endpoints use local geometry, and only a unique chemical
// outcome is accepted. Input document and arrow coordinates remain untouched.
inline core::Error inferElectronMoves(const core::Document& graph,const std::vector<arrows::ArrowParams>& params,
                                      std::vector<ElectronMove>* moves,const core::Document* drawing=nullptr){
    const auto& ownerDocument=drawing?*drawing:graph;
    auto owner=[&](core::ObjectRef ref){const core::AnnotationRecord* marker=nullptr;if(ref.kind==core::ObjectKind::Symbol)marker=ownerDocument.symbols().alive(ref.id);else if(ref.kind==core::ObjectKind::Orbital)marker=ownerDocument.orbitals().alive(ref.id);if(!marker)return ref;core::ObjectId atom=core::kInvalidObjectId;for(auto id:marker->anchors)if(ownerDocument.atoms().alive(id)){if(atom!=core::kInvalidObjectId&&atom!=id)return ref;atom=id;}return atom==core::kInvalidObjectId?ref:core::ObjectRef(core::ObjectKind::Atom,atom);};
    auto normalized=params;bool owned=false;for(auto& p:normalized){auto from=owner(p.electronSource),to=owner(p.electronTarget);owned|=from!=p.electronSource||to!=p.electronTarget;p.electronSource=from;p.electronTarget=to;}if(owned)return inferElectronMoves(graph,normalized,moves,drawing);
    bool bidirectional=false;std::vector<arrows::ArrowParams> expanded;
    for(auto p:params){if(p.mechanism&&p.head.atBegin&&p.head.atEnd&&p.head.sides==arrows::HeadSides::Both&&p.shafts==arrows::ShaftCount::One){bidirectional=true;p.head.atBegin=false;expanded.push_back(p);expanded.push_back(arrows::reversedArrow(p));}else expanded.push_back(p);}
    if(bidirectional)return inferElectronMoves(graph,expanded,moves,drawing);
    using namespace infer_detail;
    // Expanding a nickname can move the attachment label and add nearby
    // atoms. Interpret arrow geometry against the drawing the user saw.
    const auto& doc=drawing?*drawing:graph;
    if(moves)moves->clear();
    auto fail=[](const std::string& text){return core::Error(core::ErrorCode::InvalidArgument,text);};
    if(params.empty()||params.size()>32)return fail("请选中同一步的 1–32 根机理箭头。");
    struct Contacts {std::vector<Candidate> sources,targets;};
    std::vector<Contacts> contacts;
    for(const auto& p:params){
        if(!p.mechanism||p.head.atBegin||!p.head.atEnd||p.shafts!=arrows::ShaftCount::One)return fail("当前支持单向机理箭头。");
        std::vector<core::Vec2> points;arrows::Tessellation tess;tess.tolerance=.1;
        if(p.route.size()>1)points=arrows::routedPoints(p);else arrows::flattenArc(p.begin,p.end,p.arcSagitta,tess,&points);
        core::Vec2 directions[2]={p.begin-p.end,p.end-p.begin};
        if(points.size()>1){directions[0]=points[0]-points[1];directions[1]=points.back()-points[points.size()-2];}
        std::vector<Candidate> endpoints[2];
        for(int side=0;side<2;++side){core::Vec2 point=side?p.end:p.begin;core::ObjectRef bound=side?p.electronTarget:p.electronSource;double unit=localUnit(doc,point);
            if(bound.valid()){
                double score=0;bool boundIsNear=nearRef(doc,bound,point,directions[side],unit,&score);
                if(!boundIsNear&&bound.kind==core::ObjectKind::Atom&&projection_molecule::rear(doc,bound.id))boundIsNear=core::length(point-doc.atoms().alive(bound.id)->position)<=projection_molecule::radius(doc)+3.;
                if(!boundIsNear&&bound.kind==core::ObjectKind::Bond){auto b=doc.bonds().alive(bound.id);
                    if(b&&projection_molecule::axis(doc,*b)&&projection_molecule::kind(doc,b->beginAtom)==2){auto center=doc.atoms().alive(b->beginAtom)->position;boundIsNear=core::length(point-center)<=projection_molecule::radius(doc)+3.;}
                }
                // Explicit semantic anchors precede glyph clipping. A charged
                // carbon's downward H can legitimately move its visible arrow
                // endpoint beyond the ordinary unbound-atom search radius.
                // Accept only a narrow strip at that same atom's label edge.
                if(!boundIsNear&&bound.kind==core::ObjectKind::Atom){auto a=doc.atoms().alive(bound.id);if(a&&arrows::mechanismHasLabel(doc,*a)){
                    auto box=arrows::mechanismLabelBox(doc,*a);double dx=std::max(0.,std::max(box.min.x-point.x,point.x-box.max.x)),dy=std::max(0.,std::max(box.min.y-point.y,point.y-box.max.y));
                    double edge=std::min(std::min(std::fabs(point.x-box.min.x),std::fabs(point.x-box.max.x)),std::min(std::fabs(point.y-box.min.y),std::fabs(point.y-box.max.y)));
                    boundIsNear=(dx>0||dy>0)?std::sqrt(dx*dx+dy*dy)<=1.5:edge<=1.;
                    if(!boundIsNear&&a->semantics.charge){
                        // A charge-click source may start inside the actual
                        // superscript capture area rather than at a label edge.
                        // Reuse the drawing tool envelope; never retarget it.
                        auto ink=arrows::mechanismCaptureLabelBox(doc,*a);double x=std::max(0.,std::max(ink.min.x-point.x,point.x-ink.max.x)),y=std::max(0.,std::max(ink.min.y-point.y,point.y-ink.max.y));
                        double em=doc.defaultTextSize();if(auto value=doc.documentExtensions().findValue("studio.style.LabelSize")){double size=std::atof(value->c_str());if(size>0&&size<100)em=size;}
                        boundIsNear=std::sqrt(x*x+y*y)<=std::min(unit*.12,em*.15);
                    }
                }}
                if(!boundIsNear&&bound.kind==core::ObjectKind::Atom){
                    const auto visual=side?p.targetVisualAnchor:p.sourceVisualAnchor;
                    auto atom=doc.atoms().alive(bound.id);
                    if(atom){
                        const auto& boxes=arrows::mechanismVisualBoxes(doc);
                        auto found=boxes.find(bound.id);
                        if(found!=boxes.end()){
                            if(visual==1&&!found->second.labelGlyphs.empty())
                                boundIsNear=arrows::mechanismBoxDistance(arrows::mechanismLabelEnvelope(found->second),point)<=2.;
                            if((visual==2||visual==0)&&found->second.hasCharge)
                                boundIsNear=arrows::mechanismBoxDistance(found->second.charge,point)<=2.5;
                            // Older Nu arrows were stored as atom-label anchors.
                            if(visual==3||hasPrintedLonePair(doc,bound.id))
                                if(auto pair=arrows::mechanismClosestLonePair(doc,bound.id,point))
                                    boundIsNear=boundIsNear||arrows::mechanismBoxDistance(*pair,point)<=2.5;
                        }
                    }
                }
                if(!boundIsNear&&bound.kind==core::ObjectKind::Atom){
                    // A captured atom and every electron/charge glyph owned by
                    // it denote the same chemical centre, regardless of site.
                    auto atom=doc.atoms().alive(bound.id);const auto& boxes=arrows::mechanismVisualBoxes(doc);auto found=boxes.find(bound.id);
                    if(atom&&core::length(point-atom->position)<=unit*.22)boundIsNear=true;
                    if(found!=boxes.end()){const auto& visual=found->second;
                        if(visual.hasBody&&arrows::mechanismBoxDistance(visual.body,point)<=2.5)boundIsNear=true;
                        if(visual.hasCharge&&arrows::mechanismBoxDistance(visual.charge,point)<=2.5)boundIsNear=true;
                        for(const auto& pair:visual.lonePairs)if(arrows::mechanismBoxDistance(pair,point)<=2.5)boundIsNear=true;
                        for(auto dot:visual.radicalPoints)if(core::length(dot-point)<=arrows::mechanismRadicalRadius(doc)+2.5)boundIsNear=true;
                    }
                }
                if(!boundIsNear)return fail("箭头原有的原子或键关联已失效或离端点太远，请重新指定该端点。");
                endpoints[side].push_back({bound,score});
            }else{
                auto add=[&](core::ObjectRef ref){double score;if(nearRef(doc,ref,point,directions[side],unit,&score))endpoints[side].push_back({ref,score});};
                for(const auto& a:doc.atoms().records())if(a.isAlive())add(core::ObjectRef(core::ObjectKind::Atom,a.id));
                for(const auto& b:doc.bonds().records())if(b.isAlive())add(core::ObjectRef(core::ObjectKind::Bond,b.id));
                if(side)for(const auto& guide:doc.arrows().records())if(guide.isAlive()){
                    const core::ObjectRef ref(core::ObjectKind::Arrow,guide.id);
                    if(virtual_bonds::isGuide(doc,ref))add(ref);
                }
                // A label-directed endpoint must not also capture the side of
                // a bond attached to that label. Do not impose a tangent cone
                // on isolated candidates: routed label avoidance can legitimately
                // turn just before the endpoint.
                auto captured=endpoints[side];
                endpoints[side].erase(std::remove_if(endpoints[side].begin(),endpoints[side].end(),[&](const Candidate& candidate){
                    if(candidate.ref.kind!=core::ObjectKind::Bond)return false;
                    double ignored;bool directedBond=nearRef(doc,candidate.ref,point,directions[side],unit,&ignored,true);
                    auto b=doc.bonds().alive(candidate.ref.id);if(!b)return false;
                    auto first=doc.atoms().alive(b->beginAtom),last=doc.atoms().alive(b->endAtom);if(!first||!last)return false;
                    auto axis=last->position-first->position;double len2=axis.x*axis.x+axis.y*axis.y;
                    double t=len2>1e-8?((point.x-first->position.x)*axis.x+(point.y-first->position.y)*axis.y)/len2:.5;
                    for(const auto& other:captured)if(other.ref.kind==core::ObjectKind::Atom&&(other.ref.id==b->beginAtom||other.ref.id==b->endAtom)){
                        auto a=doc.atoms().alive(other.ref.id);if(!a||!arrows::mechanismHasLabel(doc,*a))continue;
                        // The end fifth lies under the atom label/capture area,
                        // not in the exposed interior used to identify a bond.
                        if((other.ref.id==b->beginAtom&&t<.2)||(other.ref.id==b->endAtom&&t>.8))return true;
                        if(!directedBond&&nearRef(doc,other.ref,point,directions[side],unit,&ignored,true))return true;
                    }
                    return false;
                }),endpoints[side].end());
                std::sort(endpoints[side].begin(),endpoints[side].end(),[](const Candidate& a,const Candidate& b){return a.score<b.score;});
                if(!side&&p.head.sides==arrows::HeadSides::Both){
                    // A legacy free tail near an alkene vertex can match both
                    // its carbon and its pi bond. Prefer the electron-bearing
                    // bond when both give the same product. The search below
                    // still rejects chemically distinct outcomes.
                    std::stable_partition(endpoints[side].begin(),endpoints[side].end(),[&](const Candidate& c){
                        if(c.ref.kind!=core::ObjectKind::Atom)return true;
                        auto atom=doc.atoms().alive(c.ref.id);
                        if(atom->element!=6||atom->semantics.charge||atom->semantics.radical||lonePairCapacity(doc,atom->id)>0)return true;
                        for(const auto& match:captured)if(match.ref.kind==core::ObjectKind::Bond){
                            auto bond=doc.bonds().alive(match.ref.id);
                            if(bond->order==core::BondOrder::Double&&bond->semantics==core::BondSemantics::Normal&&
                               (bond->beginAtom==atom->id||bond->endAtom==atom->id))return false;
                        }
                        return true;
                    });
                }
                // Never truncate possible candidates and then pretend uniqueness.
                if(endpoints[side].size()>6)return fail("这个箭头端点附近的原子和键过多，请把端点稍微靠近希望关联的对象。");
            }
            if(side==0&&endpoints[side].empty())return fail("有箭头起点离结构太远，请从电子点、原子或化学键开始。");
        }
        contacts.push_back({endpoints[0],endpoints[1]});
    }

    auto isDoublet=[&](core::ObjectRef ref){
        auto atom=ref.kind==core::ObjectKind::Atom?doc.atoms().alive(ref.id):nullptr;
        return atom&&atom->semantics.radical==uint8_t(core::Radical::Doublet)&&!atom->semantics.charge;
    };
    auto hook=[&](size_t index){
        return params[index].head.sides==arrows::HeadSides::Left||params[index].head.sides==arrows::HeadSides::Right;
    };
    // A free target can describe a prospective bond if exactly two hooks meet.
    // Both sources must be known. The endpoints stay free in the drawing.
    std::vector<std::vector<size_t>> partners(params.size());
    for(size_t i=0;i<params.size();++i){
        if(!hook(i)||!contacts[i].targets.empty()||contacts[i].sources.size()!=1)continue;
        auto a=contacts[i].sources.front().ref;
        for(size_t j=i+1;j<params.size();++j){
            if(!hook(j)||!contacts[j].targets.empty()||contacts[j].sources.size()!=1)continue;
            auto b=contacts[j].sources.front().ref;
            if(a==b||!(isDoublet(a)&&(isDoublet(b)||b.kind==core::ObjectKind::Bond)||
                       isDoublet(b)&&a.kind==core::ObjectKind::Bond))continue;
            const double unit=localUnit(doc,(params[i].end+params[j].end)*.5);
            if(core::length(params[i].end-params[j].end)>std::max(4.,unit*.55))continue;
            partners[i].push_back(j);partners[j].push_back(i);
        }
    }
    std::vector<std::vector<ElectronMove>> options;
    size_t combinations=1;
    for(size_t i=0;i<params.size();++i){
        const auto& p=params[i];const auto& contact=contacts[i];
        std::vector<ElectronMove> choices;
        if(contact.targets.empty()){
            if(!hook(i))return fail("双电子机理箭头的终点未捕捉到原子或键。质子化时请用「插入 H⁺」创建质子，再把箭头指向 H 或正号。");
            if(partners[i].size()!=1||partners[partners[i].front()].size()!=1)
                return fail("空白处的鱼钩终点需与另一支鱼钩明确会合，或直接指向原子或键。");
            auto source=contact.sources.front().ref;
            auto other=contacts[partners[i].front()].sources.front().ref;
            if(other.kind==core::ObjectKind::Bond){
                auto b=doc.bonds().alive(other.id);
                const auto meeting=(params[i].end+params[partners[i].front()].end)*.5;
                const double first=core::length(doc.atoms().alive(b->beginAtom)->position-meeting),last=core::length(doc.atoms().alive(b->endAtom)->position-meeting);
                const double margin=localUnit(doc,meeting)*.12;
                for(auto id:{b->beginAtom,b->endAtom})if(id!=source.id){
                    if(std::abs(first-last)>margin&&id!=(first<last?b->beginAtom:b->endAtom))continue;
                    choices.emplace_back(source,core::ObjectRef(core::ObjectKind::Atom,id),1,true);
                }
            }else choices.emplace_back(source,other,1,true);
        }else for(auto s:contact.sources)for(auto t:contact.targets)
            if(s.ref!=t.ref){
                if(!hook(i)&&s.ref.kind==core::ObjectKind::Atom&&t.ref.kind==core::ObjectKind::Bond){
                    const auto atom=doc.atoms().alive(s.ref.id);const auto bond=doc.bonds().alive(t.ref.id);
                    if(atom&&bond&&atom->element==6&&atom->semantics.radical==uint8_t(core::Radical::Singlet)&&bond->order==core::BondOrder::Double&&bond->beginAtom!=s.ref.id&&bond->endAtom!=s.ref.id){
                        // A carbene attacks the pi bond as a whole. Evaluate
                        // both ends together with the complementary pi arrow;
                        // the shared cyclopropane product resolves the symmetry.
                        choices.emplace_back(s.ref,core::ObjectRef(core::ObjectKind::Atom,bond->beginAtom),2);
                        choices.emplace_back(s.ref,core::ObjectRef(core::ObjectKind::Atom,bond->endAtom),2);
                        continue;
                    }
                }
                choices.emplace_back(s.ref,t.ref,hook(i)?1:2);
                if(!hook(i)&&s.ref.kind==core::ObjectKind::Atom&&p.sourceVisualAnchor==3){
                    const auto& boxes=arrows::mechanismVisualBoxes(doc);
                    auto host=boxes.find(s.ref.id);
                    if(host!=boxes.end()){
                        const auto* pair=arrows::mechanismClosestLonePair(doc,s.ref.id,p.begin);
                        if(pair)choices.back().sourceLonePair=int(pair-host->second.lonePairs.data());
                    }
                }
            }
        if(choices.empty())return fail("箭头起点和终点指向同一个对象，请检查箭头方向。");
        if(combinations>1024/choices.size())return fail("选区中的端点存在过多组合，请缩小选区或将端点靠近目标。");
        combinations*=choices.size();options.push_back(choices);
    }
    std::vector<ElectronMove> current,solution;std::string key,lastError;bool ambiguous=false;
    std::function<void(size_t)> search=[&](size_t index){
        if(ambiguous)return;
        if(index==options.size()){
            core::Document scratch=graph;std::vector<core::ObjectRef> created;auto error=generateNextStep(scratch,current,&created);
            if(!error.ok()){if(lastError.empty())lastError=error.message;return;}
            // Also identify the original participant components: two identical
            // molecules at different locations are different endpoint choices.
            std::set<core::ObjectId> participants;for(auto m:current)for(auto ref:{m.source,m.target}){if(ref.kind==core::ObjectKind::Atom)participants.insert(ref.id);else if(ref.kind==core::ObjectKind::Bond){auto b=doc.bonds().alive(ref.id);participants.insert(b->beginAtom);participants.insert(b->endAtom);}else{core::ObjectId a,b;if(virtual_bonds::endpoints(doc,ref,a,b)){participants.insert(a);participants.insert(b);}}}
            std::vector<core::ObjectId> queue(participants.begin(),participants.end());for(size_t i=0;i<queue.size();++i)for(auto id:doc.incidentBonds(queue[i])){auto b=doc.bonds().alive(id);if(!b)continue;auto other=b->beginAtom==queue[i]?b->endAtom:b->beginAtom;if(participants.insert(other).second)queue.push_back(other);}
            std::string candidate=resultKey(scratch,created);for(auto id:participants)candidate+="/"+std::to_string(id);
            if(solution.empty()){solution=current;key=candidate;}else if(key!=candidate)ambiguous=true;
            return;
        }
        for(auto move:options[index]){bool duplicate=false;for(auto old:current)if(old.source==move.source&&(old.electronCount+move.electronCount>2)){duplicate=true;break;}if(duplicate)continue;current.push_back(move);search(index+1);current.pop_back();if(ambiguous)return;}
    };
    search(0);
    if(ambiguous)return core::Error(core::ErrorCode::InvalidSemantics,"附近存在两种不同且价态合理的结果，暂不猜测。请把有歧义的箭头端点靠近目标原子或键。");
    if(solution.empty())return fail(lastError.empty()?"这些箭头无法组成同一个合理的电子转移步骤。":"未能从附近的原子和键确定下一步："+lastError);
    if(moves)*moves=solution;return core::Error();
}
} }
