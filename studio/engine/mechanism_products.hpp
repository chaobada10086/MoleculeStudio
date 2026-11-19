#pragma once
#include "mechanism_step.hpp"
#include "diazo.hpp"
#include "generated_bond_spacing.hpp"
#include <functional>
namespace studio_mechanism_products {
using namespace r62::mechanism;
inline Error cloneStage(const Document& source,const std::vector<ObjectRef>& selected,Document& prepared,std::vector<ObjectRef>& made){
    std::set<ObjectId> ids;
    for(auto ref:selected)if(ref.kind==ObjectKind::Atom&&source.atoms().alive(ref.id))ids.insert(ref.id);
    std::vector<ObjectId> queue(ids.begin(),ids.end());
    for(size_t i=0;i<queue.size();++i)for(auto id:source.incidentBonds(queue[i])){
        const auto& bond=*source.bonds().alive(id);
        const auto other=bond.beginAtom==queue[i]?bond.endAtom:bond.beginAtom;
        if(ids.insert(other).second)queue.push_back(other);
    }
    if(ids.empty())return step_detail::fail("请选择要转移质子或脱质子的结构。");
    std::vector<ObjectRef> refs;double left=1e100,right=-1e100,top=1e100,bottom=-1e100;
    for(auto id:ids){
        refs.emplace_back(ObjectKind::Atom,id);auto point=source.atoms().alive(id)->position;
        left=std::min(left,point.x);right=std::max(right,point.x);top=std::min(top,point.y);bottom=std::max(bottom,point.y);
    }
    for(const auto& bond:source.bonds().records())if(bond.isAlive()&&ids.count(bond.beginAtom)&&ids.count(bond.endAtom))refs.emplace_back(ObjectKind::Bond,bond.id);
    prepared=source;
    auto e=studioio::append(source,prepared,refs,Vec2(right-left+120,0),&made);if(!e.ok())return e;
    std::vector<ObjectId> products;
    for(auto ref:made)if(ref.kind==ObjectKind::Atom)products.push_back(ref.id);
    std::sort(products.begin(),products.end());size_t i=0;
    for(auto id:ids)prepared.extensions(ObjectRef(ObjectKind::Atom,products.at(i++))).set("studio.mechanism.source.atom",std::to_string(id));
    auto arrow=arrows::resolvePreset(8,Vec2(right+18,(top+bottom)*.5),Vec2(right+100,(top+bottom)*.5),source.defaultLineWidth());
    ObjectId aid;e=arrows::addArrow(prepared,arrow,source.defaultLineWidth(),&aid);
    if(e.ok())made.emplace_back(ObjectKind::Arrow,aid);return e;
}
struct Loss {const char* name;std::vector<int> elements,hydrogens;std::vector<std::array<int,3>> bonds;};
inline const std::vector<Loss>& smallLosses(){
    static const std::vector<Loss> list={
        {"H2O",{8},{2},{}},{"HF",{9},{1},{}},{"HCl",{17},{1},{}},{"HBr",{35},{1},{}},{"HI",{53},{1},{}},
        {"NH3",{7},{3},{}},{"MeOH",{6,8},{3,1},{{{0,1,1}}}},
        {"EtOH",{6,6,8},{3,2,1},{{{0,1,1}},{{1,2,1}}}},
        {"tBuOH",{6,6,6,6,8},{3,0,3,3,1},{{{0,1,1}},{{1,2,1}},{{1,3,1}},{{1,4,1}}}},
        {"CO2",{8,6,8},{0,0,0},{{{0,1,2}},{{1,2,2}}}},
        {"CO",{6,8},{0,0},{{{0,1,3}}}},
        {"SO2",{8,16,8},{0,0,0},{{{0,1,2}},{{1,2,2}}}},
        {"N2",{7,7},{0,0},{{{0,1,3}}}},
        {"HCO2H",{6,8,8},{1,0,1},{{{0,1,2}},{{0,2,1}}}},
        {"AcOH",{6,6,8,8},{3,0,0,1},{{{0,1,1}},{{1,2,2}},{{1,3,1}}}},
        {"Me2CO",{6,6,6,8},{3,0,3,0},{{{0,1,1}},{{1,2,1}},{{1,3,2}}}},
        {"C2H4",{6,6},{2,2},{{{0,1,2}}}},
        {"C4H8",{6,6,6,6},{2,0,3,3},{{{0,1,2}},{{1,2,1}},{{1,3,1}}}},
        {"Me2S",{6,16,6},{3,0,3},{{{0,1,1}},{{1,2,1}}}}
    };return list;
}
inline std::vector<std::vector<ObjectId>> components(const Document& doc,const std::vector<ObjectRef>& refs){
    std::set<ObjectId> atoms,seen;
    for(auto ref:refs)if(ref.kind==ObjectKind::Atom&&doc.atoms().alive(ref.id))atoms.insert(ref.id);
    std::vector<std::vector<ObjectId>> parts;
    for(auto atom:atoms)if(seen.insert(atom).second){
        std::vector<ObjectId> part{atom};
        for(size_t i=0;i<part.size();++i)for(auto id:doc.incidentBonds(part[i])){
            const auto& bond=*doc.bonds().alive(id);
            const auto other=bond.beginAtom==part[i]?bond.endAtom:bond.beginAtom;
            if(atoms.count(other)&&seen.insert(other).second)part.push_back(other);
        }
        parts.push_back(part);
    }
    return parts;
}
inline int hydrogenCount(const Document& doc,ObjectId id){
    const auto* atom=doc.atoms().alive(id);if(!atom||atom->element==1)return 0;
    return std::max(0,chemistry::refreshAtomDerived(doc,id,{}).newHydrogenCount);
}
inline bool matchesLoss(const Document& doc,const std::vector<ObjectId>& part,const Loss& loss){
    std::vector<ObjectId> atoms;
    std::map<ObjectId,int> h;
    int totalCharge=0;for(auto id:part)totalCharge+=doc.atoms().alive(id)->semantics.charge;
    const bool carbonMonoxide=std::string(loss.name)=="CO"&&part.size()==2&&!totalCharge;
    for(auto id:part){
        const auto& atom=*doc.atoms().alive(id);
        if((atom.semantics.charge&&!(carbonMonoxide&&((atom.element==6&&atom.semantics.charge==-1)||(atom.element==8&&atom.semantics.charge==1))))||atom.semantics.radical||atom.semantics.isotope||
           atom.semantics.nodeType!=uint8_t(NodeType::Element))return false;
        if(atom.element!=1){atoms.push_back(id);h[id]=hydrogenCount(doc,id);}
    }
    if(atoms.size()!=loss.elements.size())return false;
    for(auto id:part)if(doc.atoms().alive(id)->element==1){
        const auto& bonds=doc.incidentBonds(id);if(bonds.size()!=1)return false;
        const auto* bond=doc.bonds().alive(bonds.front());
        ++h[bond->beginAtom==id?bond->endAtom:bond->beginAtom];
    }
    std::map<std::pair<int,int>,int> orders;
    for(const auto& bond:loss.bonds)orders[std::minmax(bond[0],bond[1])]=bond[2];
    std::vector<ObjectId> map(atoms.size(),kInvalidObjectId);std::set<ObjectId> used;
    std::function<bool(size_t)> search=[&](size_t index){
        if(index==atoms.size())return true;
        for(auto id:atoms)if(!used.count(id)&&doc.atoms().alive(id)->element==loss.elements[index]&&h[id]==loss.hydrogens[index]){
            bool fit=true;
            for(size_t j=0;j<index;++j){
                const auto* bond=doc.bonds().alive(doc.bondBetween(id,map[j]));
                const int expected=orders.count(std::minmax(int(index),int(j)))?orders.at(std::minmax(int(index),int(j))):0;
                if((bond?int(bond->order):0)!=expected){fit=false;break;}
            }
            if(!fit)continue;
            used.insert(id);map[index]=id;if(search(index+1))return true;used.erase(id);
        }
        return false;
    };
    return search(0);
}
inline ObjectId sourceAtom(const Document& doc,ObjectId id){
    auto value=doc.extensions(ObjectRef(ObjectKind::Atom,id)).findValue("studio.mechanism.source.atom");
    return value?ObjectId(std::strtoul(value->c_str(),nullptr,10)):kInvalidObjectId;
}
inline Error changeHydrogen(Document& doc,ObjectId donor,ObjectId acceptor=kInvalidObjectId){
    const auto* atom=doc.atoms().alive(donor);if(!atom)return step_detail::fail("供氢原子不存在。");
    auto sem=atom->semantics;
    int count=hydrogenCount(doc,donor);
    if(count>0)sem.implicitHydrogens=count-1;
    else{
        ObjectId hydrogen=kInvalidObjectId;
        for(auto id:doc.incidentBonds(donor)){
            const auto& bond=*doc.bonds().alive(id);auto other=bond.beginAtom==donor?bond.endAtom:bond.beginAtom;
            const auto* candidate=doc.atoms().alive(other);
            if(candidate->element==1&&!candidate->semantics.isotope&&!candidate->semantics.charge){hydrogen=other;break;}
        }
        if(hydrogen==kInvalidObjectId)return step_detail::fail("该位点没有可转移的氢。");
        auto error=doc.removeObject(ObjectRef(ObjectKind::Atom,hydrogen));if(!error.ok())return error;
    }
    auto refreshLabel=[&](ObjectId id){
        bool first=false;size_t letters=0;const auto* a=doc.atoms().alive(id);
        if(integration::molecular::hydrideLabel(a->element,doc.atomLabel(id),&first,&letters)){
            doc.setAtomLabel(id,elementSymbol(a->element));studioatom::set(doc,id,{});
        }
    };
    --sem.charge;auto error=doc.setAtomSemantics(donor,sem);if(!error.ok())return error;
    refreshLabel(donor);
    if(acceptor!=kInvalidObjectId){
        auto next=doc.atoms().alive(acceptor)->semantics;next.implicitHydrogens=hydrogenCount(doc,acceptor)+1;++next.charge;
        error=doc.setAtomSemantics(acceptor,next);
        refreshLabel(acceptor);
    }
    return error;
}
inline bool hasHydrogen(const Document& doc,ObjectId id){
    if(hydrogenCount(doc,id)>0)return true;
    for(auto bid:doc.incidentBonds(id)){
        const auto& b=*doc.bonds().alive(bid);
        const auto* other=doc.atoms().alive(b.beginAtom==id?b.endAtom:b.beginAtom);
        if(other->element==1&&!other->semantics.charge&&!other->semantics.isotope)return true;
    }
    return false;
}
inline Error finish(Document& doc,const Document& source,std::vector<ObjectRef>& made,int mode,bool keepLossStructures=false){
    std::vector<ObjectId> acids,bases,carbonBases;ObjectId arrowId=kInvalidObjectId;
    for(auto ref:made){
        if(ref.kind==ObjectKind::Arrow)arrowId=ref.id;
        if(ref.kind!=ObjectKind::Atom)continue;
        const auto* atom=doc.atoms().alive(ref.id);if(!atom)continue;
        const int z=atom->element;
        if((z==7||z==8||z==15||z==16)&&atom->semantics.charge>0&&hasHydrogen(doc,ref.id))acids.push_back(ref.id);
        if((z==7||z==8||z==16)&&atom->semantics.charge<0)bases.push_back(ref.id);
        if(z==6&&atom->semantics.charge==-1&&!atom->semantics.radical)carbonBases.push_back(ref.id);
    }
    std::vector<std::string> above,below;
    std::vector<std::vector<ObjectId>> aboveFragments;
    if(mode==1){
        // Protonate a carbanion before competing heteroatom anions. Preserve
        // ambiguity between two carbon sites instead of choosing by atom ID.
        if(!carbonBases.empty())bases=carbonBases;
        if(acids.empty()&&!carbonBases.empty()){
            for(auto ref:made)if(ref.kind==ObjectKind::Atom){
                const auto* atom=doc.atoms().alive(ref.id);
                if(atom&&(atom->element==8||atom->element==16)&&!atom->semantics.charge&&!atom->semantics.radical&&hasHydrogen(doc,ref.id))acids.push_back(ref.id);
            }
        }
        if(acids.size()==1&&bases.size()==1){
            auto e=changeHydrogen(doc,acids.front(),bases.front());if(!e.ok())return e;
            above.push_back("-H+");below.push_back("+H+");
        }else if(acids.size()==1&&bases.empty()){
            std::vector<ObjectId> neutralN;
            for(auto ref:made)if(ref.kind==ObjectKind::Atom){
                const auto* atom=doc.atoms().alive(ref.id);
                if(atom&&atom->element==7&&!atom->semantics.charge&&!atom->semantics.radical&&lonePairCapacity(doc,ref.id)>0)neutralN.push_back(ref.id);
            }
            if(neutralN.size()!=1)return step_detail::fail("请明确唯一的受质子原子；当前没有找到唯一的质子转移组合。");
            auto e=changeHydrogen(doc,acids.front(),neutralN.front());if(!e.ok())return e;
            above.push_back("-H+");below.push_back("+H+");
        }else if(acids.size()>1||bases.size()>1)return step_detail::fail("存在多个质子转移位点，请拆成单独一步或补画质子箭头。");
    }
    if(mode==2){
        if(acids.size()>1)return step_detail::fail("存在多个可脱质子的带正电原子，请分别处理。");
        if(acids.size()==1){
            auto e=changeHydrogen(doc,acids.front());if(!e.ok())return e;above.push_back("-H+");
        }else{
            struct Site {ObjectId carbon,donor,bond;bool aromatic;};
            std::vector<Site> sites;
            for(auto ref:made)if(ref.kind==ObjectKind::Atom){
                const auto* c=doc.atoms().alive(ref.id);
                if(!c||c->element!=6||c->semantics.charge!=1)continue;
                for(auto bid:doc.incidentBonds(c->id)){
                    const auto& b=*doc.bonds().alive(bid);const auto donor=b.beginAtom==c->id?b.endAtom:b.beginAtom;
                    if(b.order!=BondOrder::Single||doc.atoms().alive(donor)->element!=6||!hasHydrogen(doc,donor))continue;
                    std::vector<ObjectId> queue{donor};std::map<ObjectId,ObjectId> parent{{donor,donor}};
                    for(size_t i=0;i<queue.size()&&queue.size()<32&&!parent.count(c->id);++i)
                        for(auto id:doc.incidentBonds(queue[i])){
                            if(id==bid)continue;const auto& edge=*doc.bonds().alive(id);
                            auto next=edge.beginAtom==queue[i]?edge.endAtom:edge.beginAtom;
                            if(parent.emplace(next,queue[i]).second)queue.push_back(next);
                        }
                    int length=1,doubles=1;
                    if(parent.count(c->id))for(auto id=c->id;id!=donor;id=parent.at(id)){
                        ++length;const auto* e=doc.bonds().alive(doc.bondBetween(id,parent.at(id)));doubles+=e->order==BondOrder::Double;
                    }
                    sites.push_back({c->id,donor,bid,parent.count(c->id)&&length==6&&doubles==3});
                }
            }
            if(std::any_of(sites.begin(),sites.end(),[](const Site& site){return site.aromatic;}))
                sites.erase(std::remove_if(sites.begin(),sites.end(),[](const Site& site){return !site.aromatic;}),sites.end());
            if(sites.size()!=1)return step_detail::fail("未找到唯一可脱质子的位点，请明确 C–H 和成键位置。");
            const auto site=sites.front();
            auto e=changeHydrogen(doc,site.donor);if(!e.ok())return e;
            auto donor=doc.atoms().alive(site.donor)->semantics;++donor.charge;doc.setAtomSemantics(site.donor,donor);
            auto carbon=doc.atoms().alive(site.carbon)->semantics;--carbon.charge;doc.setAtomSemantics(site.carbon,carbon);
            doc.setBondOrder(site.bond,BondOrder::Double);above.push_back("-H+");
        }
    }
    const auto parts=components(doc,made);
    for(const auto& part:parts)if(part.size()==2){bool changed=false;auto e=studio::diazo::compactNitrogenBond(doc,part[0],part[1],changed);if(!e.ok())return e;}
    for(const auto& part:parts){
        std::set<ObjectId> originals;bool detached=false;
        for(auto id:part)originals.insert(sourceAtom(doc,id));
        for(auto original:originals)if(source.atoms().alive(original))for(auto bid:source.incidentBonds(original)){
            const auto& b=*source.bonds().alive(bid);
            if(!originals.count(b.beginAtom)||!originals.count(b.endAtom))detached=true;
        }
        // B is the explicit generic-base role, never elemental boron. A new
        // conjugate acid is a byproduct even when its H was implicit originally.
        bool capturedProton=false;
        if(arrowId!=kInvalidObjectId&&parts.size()>1)for(auto id:part){
            const auto* a=doc.atoms().alive(id);const auto oldId=sourceAtom(doc,id);const auto* old=source.atoms().alive(oldId);
            if(genericRole(doc,id)!="base"||genericRole(source,oldId)!="base"||!old||old->semantics.charge!=-1||a->semantics.charge!=0||a->semantics.radical)continue;
            if(part.size()==1&&hydrogenCount(doc,id)==1)capturedProton=true;
            if(part.size()==2)for(auto other:part)if(other!=id){const auto* h=doc.atoms().alive(other);const auto* b=doc.bonds().alive(doc.bondBetween(id,other));
                capturedProton=h&&h->element==1&&!h->semantics.charge&&!h->semantics.radical&&b&&b->order==BondOrder::Single;
            }
        }
        if(capturedProton){aboveFragments.push_back(part);continue;}
        if(keepLossStructures||!detached||parts.size()<2)continue;
        // Only a newly detached halide belongs over the reaction arrow. Keep
        // its atom (including charge/source identity) editable, and leave free
        // spectator ions and radical fragments in the product area.
        if(arrowId!=kInvalidObjectId&&part.size()==1){
            const auto* ion=doc.atoms().alive(part.front());
            if((ion->element==9||ion->element==17||ion->element==35||ion->element==53)&&
               ion->semantics.charge==-1&&!ion->semantics.radical){
                aboveFragments.push_back(part);continue;
            }
        }
        std::string loss;
        if(part.size()==1){
            const auto* a=doc.atoms().alive(part.front());
            if(studioions::compact(doc,a->id)&&!a->semantics.charge&&!a->semantics.radical&&!a->semantics.isotope&&a->semantics.implicitHydrogens==1){
                const auto label=doc.atomLabel(a->id);
                if(label=="OMe")loss="MeOH";
                if(label=="OEt")loss="EtOH";
                if(label=="OtBu")loss="tBuOH";
            }
        }
        if(part.size()==1){
            const auto* atom=doc.atoms().alive(part.front());
            if(atom->element==1&&atom->semantics.charge==1&&!atom->semantics.isotope&&!atom->semantics.radical)loss="H+";
        }
        if(part.size()==2&&doc.atoms().alive(part[0])->element==1&&doc.atoms().alive(part[1])->element==1&&
           !doc.atoms().alive(part[0])->semantics.charge&&!doc.atoms().alive(part[1])->semantics.charge&&
           !doc.atoms().alive(part[0])->semantics.isotope&&!doc.atoms().alive(part[1])->semantics.isotope&&
           !doc.atoms().alive(part[0])->semantics.radical&&!doc.atoms().alive(part[1])->semantics.radical&&
           doc.bondBetween(part[0],part[1])!=kInvalidObjectId)loss="H2";
        if(loss.empty())for(const auto& candidate:smallLosses())if(matchesLoss(doc,part,candidate)){loss=candidate.name;break;}
        if(loss.empty())continue;
        above.push_back("-"+loss);
        for(auto id:part){auto e=doc.removeObject(ObjectRef(ObjectKind::Atom,id));if(!e.ok())return e;}
    }
    // Consumed external protons are represented below the reaction arrow.
    if(mode==1)for(auto ref:made)if(ref.kind==ObjectKind::Atom&&doc.atoms().alive(ref.id)){
        const auto* old=source.atoms().alive(sourceAtom(doc,ref.id));
        if(old&&old->element==1&&old->semantics.charge==1&&source.incidentBonds(old->id).empty()&&
           !doc.incidentBonds(ref.id).empty())below.push_back("+H+");
    }
    if(mode==1&&above.empty()&&below.empty())return step_detail::fail("没有找到可执行的质子转移，请指定供氢和受氢位点。");
    made.erase(std::remove_if(made.begin(),made.end(),[&](ObjectRef ref){return
        ref.kind==ObjectKind::Atom?!doc.atoms().alive(ref.id):
        ref.kind==ObjectKind::Bond?!doc.bonds().alive(ref.id):false;}),made.end());
    {
        std::map<ObjectId,AtomRecord> atoms;std::map<ObjectId,Vec2> positions;
        std::map<step_detail::Edge,int> bonds;
        for(auto ref:made)if(ref.kind==ObjectKind::Atom){atoms[ref.id]=*doc.atoms().alive(ref.id);positions[ref.id]=atoms.at(ref.id).position;}
        for(const auto& bond:doc.bonds().records())if(bond.isAlive()&&atoms.count(bond.beginAtom)&&atoms.count(bond.endAtom))
            bonds[step_detail::edge(bond.beginAtom,bond.endAtom)]=int(bond.order);
        size_t budget=30000000;
        std::set<ObjectId> active;
        for(const auto& item:atoms){auto id=item.first,original=sourceAtom(doc,id);auto old=source.atoms().alive(original);if(!old){active.insert(id);continue;}
            std::map<ObjectId,int> before,after;
            for(auto bid:source.incidentBonds(original)){auto b=source.bonds().alive(bid);before[b->beginAtom==original?b->endAtom:b->beginAtom]=int(b->order);}
            for(auto bid:doc.incidentBonds(id)){auto b=doc.bonds().alive(bid);after[sourceAtom(doc,b->beginAtom==id?b->endAtom:b->beginAtom)]=int(b->order);}
            if(before!=after||old->semantics.charge!=item.second.semantics.charge||old->semantics.radical!=item.second.semantics.radical||hydrogenCount(source,original)!=hydrogenCount(doc,id))active.insert(id);
        }
        const auto masses=studio_fragment_mass::weights(doc);
        step_detail::alignTrigonalBranches(bonds,atoms,{},doc.defaultBondLength(),budget,positions,&active,&masses);
        for(const auto& item:positions)doc.setAtomPosition(item.first,item.second);
    }
    {auto e=studio_generated_spacing::improve(doc,made);if(!e.ok())return e;}
    // Newly generated monoradicals prefer above, then an unoccupied bisector.
    // Existing
    // radicals retain their authored positions, including compact Bu3Sn dots.
    for(auto ref:made)if(ref.kind==ObjectKind::Atom){auto atom=doc.atoms().alive(ref.id);if(!atom||atom->semantics.radical!=uint8_t(Radical::Doublet))continue;
        auto old=source.atoms().alive(sourceAtom(doc,ref.id));if(old&&old->semantics.radical==uint8_t(Radical::Doublet))continue;
        if(atom->element==6){doc.extensions(ref).set(integration::molecular::electronMarkerKey(true),"auto");continue;}
        double em=integration::molecular::setting(doc,"LabelSize",10);Vec2 point=atom->position+Vec2(0,-em*.8);
        const auto& visuals=arrows::mechanismVisualBoxes(doc);auto visual=visuals.find(atom->id);
        if(visual!=visuals.end()&&visual->second.hasBody){auto body=visual->second.body;point=Vec2((body.min.x+body.max.x)*.5,body.min.y-arrows::mechanismRadicalRadius(doc,atom->id)*1.8);}
        // Skeletal/labelled carbon uses the atom coordinate as its vertical
        // axis; asymmetric C glyph ink must not introduce a sideways offset.
        if(atom->element==6){point.x=atom->position.x;point.y=std::min(point.y,atom->position.y-em*.8);}
        text::RichLayout bodyLayout;if(visual!=visuals.end()&&visual->second.hasBody)bodyLayout.metrics.inkBox=visual->second.body;
        point=arrows::avoidRadicalBonds(doc,*atom,point,em,arrows::mechanismRadicalRadius(doc,atom->id),bodyLayout.metrics.inkBox.empty()?nullptr:&bodyLayout);
        std::ostringstream offset;offset.precision(12);offset<<(point.x-atom->position.x)/em<<' '<<(point.y-atom->position.y)/em;doc.extensions(ref).set(integration::molecular::electronMarkerKey(true),offset.str());
    }
    if(arrowId!=kInvalidObjectId&&!aboveFragments.empty()){
        arrows::ArrowParams arrow;bool ok=false;arrows::readArrow(doc,arrowId,&arrow,&ok);
        if(!ok)return step_detail::fail("无法读取反应箭头。");
        const auto middle=(arrow.begin+arrow.end)*.5;const double size=doc.defaultTextSize();
        std::vector<Box> boxes;double height=0;const double gap=size*.6;
        // A loss sign is a separate baseline caption, never a bond or charge.
        const double prefixWidth=size*1.1;
        for(const auto& fragment:aboveFragments){
            // Conditions use compact formula typography: BH, never B--H.
            // Retain the underlying connectivity for electron/charge accounting.
            if(fragment.size()==2){ObjectId base=kInvalidObjectId,hydrogen=kInvalidObjectId;
                for(auto id:fragment){if(genericRole(doc,id)=="base")base=id;else if(doc.atoms().alive(id)->element==1)hydrogen=id;}
                if(base!=kInvalidObjectId&&hydrogen!=kInvalidObjectId){auto bond=doc.bondBetween(base,hydrogen);
                    if(bond!=kInvalidObjectId){
                        // B/H share one typographic baseline. Capture boxes are
                        // deliberately asymmetric and are not font baselines.
                        auto hp=doc.atoms().alive(hydrogen)->position;hp.y=doc.atoms().alive(base)->position.y;auto e=doc.setAtomPosition(hydrogen,hp);if(!e.ok())return e;
                        for(auto id:{base,hydrogen}){auto ref=ObjectRef(ObjectKind::Atom,id);auto& bag=doc.extensions(ref);for(auto key:{"studio.label.offset","studio.label.text","studio.text.LabelAlignment","studio.text.LabelJustification","studio.text.Justification"})bag.erase(key);
                            core::TextRun run;run.utf8=id==base?"B":"H";run.fontName=doc.defaultFont();if(auto font=doc.documentExtensions().findValue("studio.style.LabelFontName"))run.fontName=*font;run.size=size;run.flags=0;studioatom::set(doc,id,{run});}
                        doc.extensions(ObjectRef(ObjectKind::Bond,bond)).set("pst.attr.Visible","no");
                        // Match a normal, single-run "BH" in the actual font.
                        // A providerless glyph cache uses fallback advances;
                        // those leave a visible word-space with Arial/GDI.
                        text::LayoutOptions typography;typography.defaultFont=doc.defaultFont();if(auto font=doc.documentExtensions().findValue("studio.style.LabelFontName"))typography.defaultFont=*font;typography.defaultSize=size;
                        std::unique_ptr<text::GlyphProvider> provider(text::createGdiGlyphProvider("Arial","Microsoft YaHei"));text::GlyphCache glyphs(provider.get());core::TextRun run;run.utf8="BH";run.fontName=typography.defaultFont;run.size=size;text::RichLayout pair;
                        auto inkCenter=[](const text::RichLayout& layout,size_t index){double left=1e100,right=-1e100;for(const auto& contour:layout.glyphs.at(index).contours)for(auto point:contour){left=std::min(left,point.x);right=std::max(right,point.x);}return layout.anchor.x+(left+right)*.5;};
                        auto labels=integration::molecular::labels(doc,glyphs,integration::RingGeometry(doc));auto bl=labels.find(base),hl=labels.find(hydrogen);
                        if(text::layoutRichText({run},Vec2(),typography,&glyphs,&pair).ok()&&pair.glyphs.size()==2&&bl!=labels.end()&&hl!=labels.end()){
                            double wanted=inkCenter(pair,1)-inkCenter(pair,0),actual=inkCenter(hl->second.layout,0)-inkCenter(bl->second.layout,0);
                            hp=doc.atoms().alive(hydrogen)->position;hp.x+=wanted-actual;e=doc.setAtomPosition(hydrogen,hp);if(!e.ok())return e;
                        }
                    }
                }
            }
            Box box;bool first=true;
            for(auto id:fragment){const auto& atom=*doc.atoms().alive(id);auto body=arrows::mechanismAtomBodyBox(doc,atom);
                if(first){box=body;first=false;}else{box.add(body.min);box.add(body.max);}
                if(atom.semantics.charge){const auto charge=arrows::mechanismChargeBox(doc,atom);if(!charge.empty()){box.add(charge.min);box.add(charge.max);}}
            }
            boxes.push_back(box);height+=box.max.y-box.min.y;
        }
        height+=gap*(aboveFragments.size()-1);
        std::set<std::string> captionRows(above.begin(),above.end());
        const double bottom=middle.y-size*(above.empty()?1.0:2.8+1.6*(captionRows.size()-1));
        double top=bottom-height;
        for(size_t i=0;i<aboveFragments.size();++i){
            auto box=boxes[i];const double rowBottom=top+box.max.y-box.min.y;
            const double x=middle.x-(prefixWidth+box.max.x-box.min.x)*.5;
            auto delta=Vec2(x+prefixWidth-box.min.x,rowBottom-box.max.y);
            ObjectId lossSign;auto signResult=doc.addText(Vec2(x,rowBottom-(box.max.y-box.min.y)*.5+size*.32),size,"\xE2\x88\x92",&lossSign);
            if(!signResult.ok())return signResult;
            doc.extensions(ObjectRef(ObjectKind::Text,lossSign)).set("studio.mechanism.loss-sign","1");
            made.emplace_back(ObjectKind::Text,lossSign);
            for(auto id:aboveFragments[i]){auto e=doc.setAtomPosition(id,doc.atoms().alive(id)->position+delta);if(!e.ok())return e;}
            top=rowBottom+gap;
        }
    }
    if(arrowId!=kInvalidObjectId&&(!above.empty()||!below.empty())){
        arrows::ArrowParams arrow;bool ok=false;arrows::readArrow(doc,arrowId,&arrow,&ok);
        if(!ok)return step_detail::fail("无法读取反应箭头。");
        const auto middle=(arrow.begin+arrow.end)*.5;const double size=doc.defaultTextSize();
        auto caption=[&](const std::vector<std::string>& entries,double y,bool stack){
            if(entries.empty())return Error();
            std::map<std::string,int> counts;for(const auto& entry:entries)++counts[entry];
            std::vector<std::string> rows;std::string joined;
            for(const auto& entry:counts){auto row=(entry.first[0]=='-'?std::string("\xE2\x88\x92 "):entry.first.substr(0,1))+(entry.second>1?std::to_string(entry.second):"")+entry.first.substr(1);
                if(stack)rows.push_back(row);else{if(!joined.empty())joined+="  ";joined+=row;}}
            if(!stack)rows.push_back(joined);
            for(size_t row=0;row<rows.size();++row){const auto& text=rows[row];
            const double baseline=y-(stack?(rows.size()-1-row)*size*1.6:0);
            size_t characters=0;for(unsigned char c:text)if((c&0xC0)!=0x80)++characters;
            ObjectId id;auto e=doc.addText(Vec2(middle.x-characters*size*.23,baseline),size,text,&id);
            if(e.ok()){
                std::vector<TextRun> runs;std::string base;
                auto addRun=[&](const std::string& value,uint16_t flags){if(value.empty())return;TextRun run;run.utf8=value;run.size=size;run.fontName=doc.defaultFont();run.flags=flags;runs.push_back(run);};
                for(char character:text){
                    if(character=='+'){addRun(base,kFormula);base.clear();addRun("+",kSuperscript);}
                    else base+=character;
                }
                addRun(base,kFormula);e=doc.setTextRuns(id,runs);
            }
            if(!e.ok())return e;made.emplace_back(ObjectKind::Text,id);
            }return Error();
        };
        auto e=caption(above,middle.y-size*.9,true);if(!e.ok())return e;
        e=caption(below,middle.y+size*1.8,false);if(!e.ok())return e;
    }
    return Error();
}
}
