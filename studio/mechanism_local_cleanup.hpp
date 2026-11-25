#pragma once
#include "clean_component_spacing.hpp"

// Included after the mechanism preparation helpers. Work on a scratch product;
// the application commits the resulting geometry with the reaction itself.
namespace studio_local_cleanup {
using namespace r62;using namespace core;
struct Scope {std::set<ObjectId> atoms,movable,active;std::vector<std::vector<ObjectId>> newCycles;};
struct Report {bool attempted=false,changed=false,cancelled=false;size_t movable=0;std::string detail;};
struct Request {Scope scope;std::string xml;std::map<int,ObjectId> fileAtoms;};
inline bool isocyanateN(const Document& d,ObjectId id,ObjectId* center=nullptr,ObjectId* end=nullptr,ObjectId* parent=nullptr){
 auto a=d.atoms().alive(id);if(!a||a->element!=7||a->semantics.charge||a->semantics.radical||d.incidentBonds(id).size()!=2)return false;ObjectId c=kInvalidObjectId,r=kInvalidObjectId,o=kInvalidObjectId;
 for(auto bid:d.incidentBonds(id)){auto b=d.bonds().alive(bid);auto n=b->beginAtom==id?b->endAtom:b->beginAtom;if(b->order==BondOrder::Double&&d.atoms().alive(n)->element==6)c=n;else if(b->order==BondOrder::Single)r=n;}
 if(c==kInvalidObjectId||r==kInvalidObjectId||d.incidentBonds(c).size()!=2)return false;for(auto bid:d.incidentBonds(c)){auto b=d.bonds().alive(bid);auto n=b->beginAtom==c?b->endAtom:b->beginAtom;if(n!=id&&b->order==BondOrder::Double&&d.atoms().alive(n)->element==8&&d.incidentBonds(n).size()==1)o=n;}
 if(o==kInvalidObjectId)return false;if(center)*center=c;if(end)*end=o;if(parent)*parent=r;return true;
}
inline bool straightenIsocyanates(Document& d,const std::set<ObjectId>& movable){bool changed=false;for(const auto& a:d.atoms().records())if(a.isAlive()){ObjectId c,o,r;if(!isocyanateN(d,a.id,&c,&o,&r)||!movable.count(c)||!movable.count(o))continue;bool stereo=false;for(auto id:{a.id,c,o})for(auto bid:d.incidentBonds(id))stereo|=mechanism::step_detail::hasStereoMark(*d.bonds().alive(bid));if(stereo)continue;auto axis=a.position-d.atoms().alive(r)->position;double len=length(axis);if(len<1e-8)continue;axis=axis*(1/len);auto cp=a.position+axis*length(d.atoms().alive(c)->position-a.position);auto op=cp+axis*length(d.atoms().alive(o)->position-d.atoms().alive(c)->position);changed|=length(cp-d.atoms().alive(c)->position)>1e-6||length(op-d.atoms().alive(o)->position)>1e-6;d.setAtomPosition(c,cp);d.setAtomPosition(o,op);}return changed;}
inline Scope scope(const Document& doc,const Document& source,const std::vector<ObjectRef>& made){
    Scope out;std::set<ObjectId> allowed;for(auto ref:made)if(ref.kind==ObjectKind::Atom&&doc.atoms().alive(ref.id))allowed.insert(ref.id);
    if(allowed.size()>200)return out;
    std::map<ObjectId,std::vector<ObjectId>> adjacent;std::vector<const BondRecord*> bonds;
    for(const auto& b:doc.bonds().records())if(b.isAlive()&&allowed.count(b.beginAtom)&&allowed.count(b.endAtom)){adjacent[b.beginAtom].push_back(b.endAtom);adjacent[b.endAtom].push_back(b.beginAtom);bonds.push_back(&b);}
    std::set<ObjectId> ring,protectedAtoms;std::set<std::pair<ObjectId,ObjectId>> common;
    auto key=[](ObjectId a,ObjectId b){return std::make_pair(std::min(a,b),std::max(a,b));};
    for(auto b:bonds){auto a=studio_mechanism_products::sourceAtom(doc,b->beginAtom),z=studio_mechanism_products::sourceAtom(doc,b->endAtom);auto old=source.bonds().alive(source.bondBetween(a,z));if(old)common.insert(key(b->beginAtom,b->endAtom));}
    auto path=[&](ObjectId begin,ObjectId end,const std::pair<ObjectId,ObjectId>& omitted,bool retained){std::map<ObjectId,ObjectId> parent{{begin,begin}};std::vector<ObjectId> queue{begin};for(size_t i=0;i<queue.size()&&!parent.count(end);++i)for(auto n:adjacent[queue[i]])if(key(queue[i],n)!=omitted&&(!retained||common.count(key(queue[i],n)))&&!parent.count(n)){parent[n]=queue[i];queue.push_back(n);}std::vector<ObjectId> result;if(parent.count(end)){for(auto id=end;;id=parent.at(id)){result.push_back(id);if(id==begin)break;}std::reverse(result.begin(),result.end());}return result;};
    // Only cycles that survive in the unchanged graph are a fixed frame.
    // A new closure must remain free to form its polygon around that frame.
    for(auto b:bonds){auto edge=key(b->beginAtom,b->endAtom);auto cycle=path(b->beginAtom,b->endAtom,edge,false);if(common.count(edge)&&!path(b->beginAtom,b->endAtom,edge,true).empty()){ring.insert(b->beginAtom);ring.insert(b->endAtom);}else if(!common.count(edge)&&cycle.size()>=3&&cycle.size()<=8)out.newCycles.push_back(cycle);
        bool stereo=mechanism::step_detail::hasStereoMark(*b);
        bool alkene=cycle.empty()&&b->order==BondOrder::Double&&!isocyanateN(doc,b->beginAtom)&&!isocyanateN(doc,b->endAtom)&&adjacent[b->beginAtom].size()>1&&adjacent[b->endAtom].size()>1;
        if(stereo||alkene)for(auto id:{b->beginAtom,b->endAtom}){protectedAtoms.insert(id);for(auto n:adjacent[id])protectedAtoms.insert(n);}
    }
    for(auto id:allowed){auto original=studio_mechanism_products::sourceAtom(doc,id);auto old=source.atoms().alive(original);auto atom=doc.atoms().alive(id);std::map<ObjectId,int> before,after;
        if(old)for(auto bid:source.incidentBonds(original)){auto b=source.bonds().alive(bid);before[b->beginAtom==original?b->endAtom:b->beginAtom]=int(b->order);}
        for(auto bid:doc.incidentBonds(id)){auto b=doc.bonds().alive(bid);after[studio_mechanism_products::sourceAtom(doc,b->beginAtom==id?b->endAtom:b->beginAtom)]=int(b->order);}
        if(!old||before!=after||old->semantics.charge!=atom->semantics.charge||old->semantics.radical!=atom->semantics.radical||studio_mechanism_products::hydrogenCount(source,original)!=studio_mechanism_products::hydrogenCount(doc,id))out.active.insert(id);
    }
    std::map<ObjectId,int> distance;std::vector<ObjectId> queue;for(auto id:out.active){distance[id]=0;queue.push_back(id);}
    for(size_t i=0;i<queue.size();++i){auto id=queue[i];if(distance[id]>=3||ring.count(id))continue;for(auto n:adjacent[id])if(!distance.count(n)){distance[n]=distance[id]+1;queue.push_back(n);}}
    std::set<ObjectId> seen;
    for(auto root:allowed)if(seen.insert(root).second){std::vector<ObjectId> part{root};bool relevant=false;for(size_t i=0;i<part.size();++i){relevant|=out.active.count(part[i])!=0;for(auto n:adjacent[part[i]])if(seen.insert(n).second)part.push_back(n);}if(!relevant||part.size()<4)continue;
        std::set<ObjectId> moving;for(auto id:part){auto a=doc.atoms().alive(id);if(distance.count(id)&&!ring.count(id)&&!protectedAtoms.count(id)&&a->semantics.nodeType==uint8_t(NodeType::Element)&&mechanism::genericRole(doc,id).empty())moving.insert(id);}
        // Explicit hydrogens belong to the same local edit as their parent.
        for(auto id:part)if(doc.atoms().alive(id)->element==1&&adjacent[id].size()==1&&moving.count(adjacent[id][0])&&!protectedAtoms.count(id))moving.insert(id);
        if(moving.size()<2||moving.size()>32)continue;
        size_t fixed=part.size()-moving.size();if(fixed<2){const BondRecord* anchor=nullptr;double best=-1;for(auto b:bonds)if(std::find(part.begin(),part.end(),b->beginAtom)!=part.end()){double score=adjacent[b->beginAtom].size()+adjacent[b->endAtom].size()+(!out.active.count(b->beginAtom)?10:0)+(!out.active.count(b->endAtom)?10:0);if(b->order==BondOrder::Single&&(isocyanateN(doc,b->beginAtom)||isocyanateN(doc,b->endAtom)))score+=100;if(score>best){best=score;anchor=b;}}if(anchor){moving.erase(anchor->beginAtom);moving.erase(anchor->endAtom);}}
        if(moving.size()<2)continue;out.atoms.insert(part.begin(),part.end());out.movable.insert(moving.begin(),moving.end());
    }
    return out;
}
struct Quality {double crowd=0,shape=0;};
inline Quality quality(const Document& doc,const Scope& scope){
    Quality q;const double unit=doc.defaultBondLength();std::map<ObjectId,std::vector<ObjectId>> neighbors;std::vector<const BondRecord*> bonds;
    for(const auto& b:doc.bonds().records())if(b.isAlive()&&scope.atoms.count(b.beginAtom)&&scope.atoms.count(b.endAtom)){neighbors[b.beginAtom].push_back(b.endAtom);neighbors[b.endAtom].push_back(b.beginAtom);bonds.push_back(&b);if(scope.movable.count(b.beginAtom)||scope.movable.count(b.endAtom))q.shape+=.3*std::pow(length(doc.atoms().alive(b.beginAtom)->position-doc.atoms().alive(b.endAtom)->position)/unit-1,2);}
    std::map<ObjectId,ObjectId> component;for(auto root:scope.atoms)if(!component.count(root)){std::vector<ObjectId> queue{root};component[root]=root;for(size_t i=0;i<queue.size();++i)for(auto n:neighbors[queue[i]])if(!component.count(n)){component[n]=root;queue.push_back(n);}}
    for(auto id:scope.movable){auto ns=neighbors[id];if(ns.size()!=2&&ns.size()!=3)continue;const auto* a=doc.atoms().alive(id);if(a->semantics.radical||a->element==1)continue;bool linear=false;int doubles=0;for(auto n:ns){int order=int(doc.bonds().alive(doc.bondBetween(id,n))->order);if(order==3)linear=true;if(order==2)++doubles;}linear|=doubles==2||isocyanateN(doc,id);double ideal=linear?-1.:-.5;
        for(size_t i=0;i<ns.size();++i)for(size_t j=0;j<i;++j){auto x=doc.atoms().alive(ns[i])->position-a->position,y=doc.atoms().alive(ns[j])->position-a->position;double denom=length(x)*length(y);double target=ideal;for(const auto& cycle:scope.newCycles)if(std::find(cycle.begin(),cycle.end(),id)!=cycle.end()&&std::find(cycle.begin(),cycle.end(),ns[i])!=cycle.end()&&std::find(cycle.begin(),cycle.end(),ns[j])!=cycle.end()){target=std::cos(3.141592653589793-6.283185307179586/cycle.size());break;}if(denom>1e-8)q.shape+=4*std::pow(dot(x,y)/denom-target,2);}
    }
    auto provider = studio::createGlyphProvider();
    text::GlyphCache glyphs(provider.get());integration::RingGeometry rings(doc);auto labels=integration::molecular::labels(doc,glyphs,rings,&scope.atoms);
    for(auto i=scope.atoms.begin();i!=scope.atoms.end();++i)for(auto j=scope.atoms.upper_bound(*i);j!=scope.atoms.end();++j){if(component[*i]!=component[*j]||(!scope.movable.count(*i)&&!scope.movable.count(*j)))continue;auto a=doc.atoms().alive(*i)->position,b=doc.atoms().alive(*j)->position;if(doc.bondBetween(*i,*j)==kInvalidObjectId){double d=length(a-b)/unit;if(d<.5)q.crowd+=100*std::pow(.5-d,2);}auto x=labels.find(*i),y=labels.find(*j);if(x!=labels.end()&&y!=labels.end()){double w=std::min(x->second.box.max.x,y->second.box.max.x)-std::max(x->second.box.min.x,y->second.box.min.x),h=std::min(x->second.box.max.y,y->second.box.max.y)-std::max(x->second.box.min.y,y->second.box.min.y);if(w>0&&h>0)q.crowd+=20*w*h/(unit*unit);}}
    for(size_t i=0;i<bonds.size();++i){auto a=bonds[i];for(auto id:scope.atoms){if(component[id]!=component[a->beginAtom]||id==a->beginAtom||id==a->endAtom)continue;if(!scope.movable.count(id)&&!scope.movable.count(a->beginAtom)&&!scope.movable.count(a->endAtom))continue;auto p=doc.atoms().alive(id)->position,x=doc.atoms().alive(a->beginAtom)->position,y=doc.atoms().alive(a->endAtom)->position;double d=mechanism::step_detail::pointSegment(p,x,y)/unit;if(d<.25)q.crowd+=50*std::pow(.25-d,2);auto label=labels.find(id);if(label!=labels.end()){auto box=label->second.box;auto center=(box.min+box.max)*.5,v=y-x;double t=std::max(0.,std::min(1.,dot(center-x,v)/std::max(1e-8,lengthSq(v))));auto closest=x+v*t;if(closest.x>box.min.x&&closest.x<box.max.x&&closest.y>box.min.y&&closest.y<box.max.y)q.crowd+=2;}}
        for(size_t j=0;j<i;++j){auto b=bonds[j];if(component[a->beginAtom]!=component[b->beginAtom])continue;if(a->beginAtom==b->beginAtom||a->beginAtom==b->endAtom||a->endAtom==b->beginAtom||a->endAtom==b->endAtom)continue;if(!scope.movable.count(a->beginAtom)&&!scope.movable.count(a->endAtom)&&!scope.movable.count(b->beginAtom)&&!scope.movable.count(b->endAtom))continue;if(mechanism::step_detail::segmentsCross(doc.atoms().alive(a->beginAtom)->position,doc.atoms().alive(a->endAtom)->position,doc.atoms().alive(b->beginAtom)->position,doc.atoms().alive(b->endAtom)->position))q.crowd+=10;}}
    return q;
}
inline bool request(const Document& doc,const Scope& scope,Request& out){
    if(scope.movable.empty())return false;out.scope=scope;Document subset;subset.setDefaultBondLength(doc.defaultBondLength());std::vector<ObjectRef> refs,created;std::vector<ObjectId> originals(scope.atoms.begin(),scope.atoms.end());for(auto id:originals)refs.emplace_back(ObjectKind::Atom,id);for(const auto& b:doc.bonds().records())if(b.isAlive()&&scope.atoms.count(b.beginAtom)&&scope.atoms.count(b.endAtom))refs.emplace_back(ObjectKind::Bond,b.id);
    if(!studioio::append(doc,subset,refs,Vec2(),&created).ok())return false;size_t i=0;for(auto ref:created)if(ref.kind==ObjectKind::Atom){if(i>=originals.size())return false;subset.extensions(ref).set("pst.attr.MoleculeStudioCleanOriginal",std::to_string(originals[i++]));subset.extensions(ref).erase("studio.atom.z");subset.extensions(ref).erase("pst.attr.xyz");}if(i!=originals.size())return false;
    std::ostringstream xml;if(!persistence::writeCdxml(subset,xml).ok())return false;tinyxml2::XMLDocument dom;if(dom.Parse(xml.str().c_str())!=tinyxml2::XML_SUCCESS||!dom.RootElement())return false;auto page=dom.RootElement()->FirstChildElement("page");if(!page)return false;std::string fixed;
    for(auto f=page->FirstChildElement("fragment");f;f=f->NextSiblingElement("fragment"))for(auto n=f->FirstChildElement("n");n;n=n->NextSiblingElement("n"))if(auto value=n->Attribute("MoleculeStudioCleanOriginal")){auto id=ObjectId(std::strtoul(value,nullptr,10));int file=n->IntAttribute("id");if(!scope.atoms.count(id)||out.fileAtoms.count(file))return false;out.fileAtoms[file]=id;if(!scope.movable.count(id)){if(!fixed.empty())fixed+=' ';fixed+=std::to_string(file);}}
    if(out.fileAtoms.size()!=originals.size()||fixed.empty())return false;dom.RootElement()->SetAttribute("MoleculeStudioCleanFixedAtoms",fixed.c_str());tinyxml2::XMLPrinter printer;dom.Print(&printer);out.xml=printer.CStr();return true;
}
inline bool apply(Document& doc,const Request& request,const std::string& xml,std::string* reason=nullptr){
    auto reject=[&](const char* why){if(reason)*reason=why;return false;};
    tinyxml2::XMLDocument dom;if(dom.Parse(xml.c_str())!=tinyxml2::XML_SUCCESS||!dom.RootElement())return false;auto page=dom.RootElement()->FirstChildElement("page");if(!page)return false;std::map<ObjectId,Vec2> points;
    for(auto f=page->FirstChildElement("fragment");f;f=f->NextSiblingElement("fragment"))for(auto n=f->FirstChildElement("n");n;n=n->NextSiblingElement("n")){auto found=request.fileAtoms.find(n->IntAttribute("id"));if(found==request.fileAtoms.end()||points.count(found->second))return false;auto value=n->Attribute("p");if(!value)return false;std::istringstream in(value);Vec2 p;if(!(in>>p.x>>p.y)||!std::isfinite(p.x)||!std::isfinite(p.y))return false;points[found->second]=p;}
    if(points.size()!=request.scope.atoms.size())return false;Vec2 offset;bool anchored=false;for(const auto& p:points)if(!request.scope.movable.count(p.first)){auto shift=doc.atoms().alive(p.first)->position-p.second;if(anchored&&length(shift-offset)>1e-4)return reject("fixed frame mismatch");offset=shift;anchored=true;}if(!anchored)return false;
    Document candidate=doc;double motion=0;const double unit=doc.defaultBondLength();for(auto id:request.scope.movable){auto next=points.at(id)+offset;double distance=length(next-doc.atoms().alive(id)->position);if(distance>unit*3)return reject("motion limit");motion+=distance;if(!candidate.setAtomPosition(id,next).ok())return false;}if(motion<1e-5)return false;
    for(const auto& b:doc.bonds().records())if(b.isAlive()&&(request.scope.movable.count(b.beginAtom)||request.scope.movable.count(b.endAtom))){double old=length(doc.atoms().alive(b.beginAtom)->position-doc.atoms().alive(b.endAtom)->position),span=length(candidate.atoms().alive(b.beginAtom)->position-candidate.atoms().alive(b.endAtom)->position);if(span<unit*.6||span>std::max(old+1e-4,unit*1.8))return reject("bond length limit");}
    auto before=quality(doc,request.scope),after=quality(candidate,request.scope);if(after.crowd>before.crowd+1e-6){if(reason)*reason="crowd worse "+std::to_string(before.crowd)+" -> "+std::to_string(after.crowd);return false;};if(after.crowd>=before.crowd-1e-6&&after.shape>=before.shape-1e-3){if(reason)*reason="shape not improved "+std::to_string(before.shape)+" -> "+std::to_string(after.shape);return false;};
    for(auto id:request.scope.movable){doc.setAtomPosition(id,candidate.atoms().alive(id)->position);auto& bag=doc.extensions(ObjectRef(ObjectKind::Atom,id));bag.erase("studio.atom.z");bag.erase("pst.attr.xyz");}return true;
}
// A constrained depiction can be impossible with two separately anchored rings.
// Refine the remaining small tree branches by exact angular candidates instead;
// every candidate preserves all bond lengths and every fixed atom.
inline bool relax(Document& doc,const Scope& local){
    if(local.movable.empty())return false;std::map<ObjectId,std::vector<ObjectId>> adjacent;for(const auto& b:doc.bonds().records())if(b.isAlive()&&local.atoms.count(b.beginAtom)&&local.atoms.count(b.endAtom)){adjacent[b.beginAtom].push_back(b.endAtom);adjacent[b.endAtom].push_back(b.beginAtom);}
    auto current=quality(doc,local);bool changed=false;size_t evaluations=0;
    auto better=[](Quality a,Quality b){return a.crowd<b.crowd-1e-6||(std::abs(a.crowd-b.crowd)<1e-6&&a.shape<b.shape-1e-4);};
    for(int pass=0;pass<3&&evaluations<64;++pass){Quality best=current;Document winner=doc;bool found=false;
        for(auto pivot:local.movable){const auto& ns=adjacent[pivot];if(ns.size()!=2&&ns.size()!=3)continue;auto center=doc.atoms().alive(pivot)->position;auto atom=doc.atoms().alive(pivot);if(atom->semantics.radical||atom->element==1)continue;bool linear=false;int doubles=0;for(auto n:ns){int order=int(doc.bonds().alive(doc.bondBetween(pivot,n))->order);linear|=order==3;doubles+=order==2;}linear|=doubles==2||isocyanateN(doc,pivot);
            for(auto start:ns){std::set<ObjectId> branch{start};std::vector<ObjectId> queue{start};bool movable=true;for(size_t i=0;i<queue.size()&&movable;++i){if(!local.movable.count(queue[i])){movable=false;break;}for(auto n:adjacent[queue[i]]){if(queue[i]==start&&n==pivot)continue;if(n==pivot){movable=false;break;}if(branch.insert(n).second)queue.push_back(n);}}if(!movable)continue;auto from=doc.atoms().alive(start)->position-center;if(length(from)<1e-8)continue;
                for(auto other:ns)if(other!=start)for(double sense:{-1.,1.}){if(evaluations>=64)break;auto axis=doc.atoms().alive(other)->position-center;auto target=mechanism::step_detail::rotate(axis,sense*(linear?3.141592653589793:2.09439510239319549));double turn=std::atan2(mechanism::step_detail::cross(from,target),dot(from,target));if(std::abs(turn)<1e-5)continue;Document trial=doc;for(auto id:branch)trial.setAtomPosition(id,center+mechanism::step_detail::rotate(doc.atoms().alive(id)->position-center,turn));++evaluations;auto q=quality(trial,local);if(better(q,best)){best=q;winner=std::move(trial);found=true;}}
            }
        }
        if(!found)break;for(auto id:local.movable)doc.setAtomPosition(id,winner.atoms().alive(id)->position);current=best;changed=true;
    }
    if(changed)for(auto id:local.movable){auto& bag=doc.extensions(ObjectRef(ObjectKind::Atom,id));bag.erase("studio.atom.z");bag.erase("pst.attr.xyz");}return changed;
}
// Close a new small cycle around an unchanged fused edge. The pre-existing
// aromatic polygon stays exact; new cycle atoms and their small outer branches
// are laid out together, including carbonyl O/H labels.
inline bool rebuildNewCycles(Document& doc,const Scope& local){
    if(local.newCycles.empty())return false;std::map<ObjectId,std::vector<ObjectId>> adjacent;for(const auto& b:doc.bonds().records())if(b.isAlive()&&local.atoms.count(b.beginAtom)&&local.atoms.count(b.endAtom)){adjacent[b.beginAtom].push_back(b.endAtom);adjacent[b.endAtom].push_back(b.beginAtom);}bool changed=false;
    for(const auto& cycle:local.newCycles){std::set<ObjectId> members(cycle.begin(),cycle.end()),fixed;for(auto id:cycle)if(!local.movable.count(id))fixed.insert(id);if(fixed.size()!=2)continue;size_t start=cycle.size();for(size_t i=0;i<cycle.size();++i)if(fixed.count(cycle[i])&&fixed.count(cycle[(i+1)%cycle.size()])){start=i;break;}if(start==cycle.size())continue;
        struct Arm{std::vector<ObjectId> atoms;std::set<ObjectId> roots;};std::vector<Arm> arms;std::set<ObjectId> seen;for(auto root:cycle)for(auto n:adjacent[root])if(!members.count(n)&&seen.insert(n).second){Arm arm;arm.atoms.push_back(n);for(size_t i=0;i<arm.atoms.size();++i)for(auto next:adjacent[arm.atoms[i]]){if(members.count(next))arm.roots.insert(next);else if(seen.insert(next).second)arm.atoms.push_back(next);}arms.push_back(arm);}
        bool eligible=true;for(const auto& arm:arms)if(arm.roots.size()==1)for(auto id:arm.atoms)if(!local.movable.count(id))eligible=false;if(!eligible)continue;
        Quality best=quality(doc,local);Document winner=doc;bool found=false;auto better=[](Quality a,Quality b){return a.crowd<b.crowd-1e-6||(std::abs(a.crowd-b.crowd)<1e-6&&a.shape<b.shape-1e-4);};
        for(double sense:{-1.,1.}){Document trial=doc;auto edge=doc.atoms().alive(cycle[(start+1)%cycle.size()])->position-doc.atoms().alive(cycle[start])->position;auto cursor=doc.atoms().alive(cycle[(start+1)%cycle.size()])->position;for(size_t i=2;i<cycle.size();++i){edge=mechanism::step_detail::rotate(edge,sense*6.283185307179586/cycle.size());cursor=cursor+edge;trial.setAtomPosition(cycle[(start+i)%cycle.size()],cursor);}Vec2 center;for(auto id:cycle)center=center+trial.atoms().alive(id)->position;center=center*(1./cycle.size());
            for(const auto& arm:arms)if(arm.roots.size()==1){auto root=*arm.roots.begin();ObjectId first=kInvalidObjectId;for(auto id:adjacent[root])if(std::find(arm.atoms.begin(),arm.atoms.end(),id)!=arm.atoms.end()){first=id;break;}if(first==kInvalidObjectId)continue;auto from=doc.atoms().alive(first)->position-doc.atoms().alive(root)->position,to=trial.atoms().alive(root)->position-center;double turn=std::atan2(mechanism::step_detail::cross(from,to),dot(from,to));for(auto id:arm.atoms)trial.setAtomPosition(id,trial.atoms().alive(root)->position+mechanism::step_detail::rotate(doc.atoms().alive(id)->position-doc.atoms().alive(root)->position,turn));}
            auto q=quality(trial,local);if(better(q,best)){best=q;winner=std::move(trial);found=true;}
        }
        if(found){for(auto id:local.movable)doc.setAtomPosition(id,winner.atoms().alive(id)->position);changed=true;}
    }
    return changed;
}
inline bool packFragments(Document& doc,const Scope& local){
    std::vector<ObjectId> ids(local.atoms.begin(),local.atoms.end());std::map<ObjectId,Vec2> before;for(auto id:ids)before[id]=doc.atoms().alive(id)->position;auto e=studio::clean_spacing::separate(doc,ids);if(!e.ok())return false;for(auto id:ids)if(length(doc.atoms().alive(id)->position-before.at(id))>1e-6)return true;return false;
}
inline bool regularizeSmallProducts(Document& doc,const Document& source,const std::vector<ObjectRef>& made){
    std::set<ObjectId> allowed,seen;for(auto ref:made)if(ref.kind==ObjectKind::Atom&&doc.atoms().alive(ref.id))allowed.insert(ref.id);bool changed=false;const double unit=doc.defaultBondLength();
    for(auto root:allowed)if(seen.insert(root).second){std::vector<ObjectId> part{root};bool active=false;for(size_t i=0;i<part.size();++i){auto id=part[i],old=studio_mechanism_products::sourceAtom(doc,id);auto atom=doc.atoms().alive(id),before=source.atoms().alive(old);std::map<ObjectId,int> a,b;
        if(before)for(auto bid:source.incidentBonds(old)){auto edge=source.bonds().alive(bid);a[edge->beginAtom==old?edge->endAtom:edge->beginAtom]=int(edge->order);}
        for(auto bid:doc.incidentBonds(id)){auto edge=doc.bonds().alive(bid);auto other=edge->beginAtom==id?edge->endAtom:edge->beginAtom;b[studio_mechanism_products::sourceAtom(doc,other)]=int(edge->order);if(allowed.count(other)&&seen.insert(other).second)part.push_back(other);}
        active|=!before||a!=b||atom->semantics.charge!=before->semantics.charge||atom->semantics.radical!=before->semantics.radical;
    }if(!active||part.size()<2||part.size()>3)continue;
        bool formulaCondition=false;for(auto id:part)for(auto bid:doc.incidentBonds(id))if(auto visible=doc.extensions(ObjectRef(ObjectKind::Bond,bid)).findValue("pst.attr.Visible"))formulaCondition|=*visible=="no";if(formulaCondition)continue;
        bool stereo=false;for(auto id:part)for(auto bid:doc.incidentBonds(id))stereo|=mechanism::step_detail::hasStereoMark(*doc.bonds().alive(bid));if(stereo)continue;
        auto put=[&](ObjectId id,Vec2 point){if(length(point-doc.atoms().alive(id)->position)>1e-7){doc.setAtomPosition(id,point);changed=true;}};
        Vec2 center;for(auto id:part)center=center+doc.atoms().alive(id)->position;center=center*(1./part.size());
        if(part.size()==2){auto v=doc.atoms().alive(part[1])->position-doc.atoms().alive(part[0])->position;double n=length(v);v=n>1e-8?v*(unit/n):Vec2(unit,0);put(part[0],center-v*.5);put(part[1],center+v*.5);}
        else {bool triangle=true;for(auto id:part)triangle&=doc.incidentBonds(id).size()==2;
            if(triangle){double angle=std::atan2(doc.atoms().alive(part[0])->position.y-center.y,doc.atoms().alive(part[0])->position.x-center.x);for(size_t i=0;i<3;++i)put(part[i],center+Vec2(std::cos(angle+i*6.283185307179586/3),std::sin(angle+i*6.283185307179586/3))*(unit/std::sqrt(3.)));}
            else {ObjectId middle=kInvalidObjectId;for(auto id:part)if(doc.incidentBonds(id).size()==2)middle=id;if(middle==kInvalidObjectId)continue;std::vector<ObjectId> ends;int doubleCount=0;bool triple=false;for(auto bid:doc.incidentBonds(middle)){auto edge=doc.bonds().alive(bid);ends.push_back(edge->beginAtom==middle?edge->endAtom:edge->beginAtom);doubleCount+=edge->order==BondOrder::Double;triple|=edge->order==BondOrder::Triple;}
                auto origin=doc.atoms().alive(middle)->position;double first=core::angleOf(doc.atoms().alive(ends[0])->position-origin),gap=(doubleCount==2||triple)?180.:120.;auto old=doc.atoms().alive(ends[1])->position-origin;auto positive=core::direction(first+gap),negative=core::direction(first-gap);put(ends[0],origin+core::direction(first)*unit);put(ends[1],origin+(dot(old,positive)>=dot(old,negative)?positive:negative)*unit);
            }
        }
    }return changed;
}
inline bool regularizeCarbeneTriangles(Document& doc,const Document& source,const std::vector<ObjectRef>& made){
    bool changed=false;const double unit=doc.defaultBondLength();
    for(auto ref:made)if(ref.kind==ObjectKind::Atom){auto atom=doc.atoms().alive(ref.id);if(!atom)continue;auto oldId=studio_mechanism_products::sourceAtom(doc,ref.id);auto old=source.atoms().alive(oldId);if(!old||old->element!=6||old->semantics.radical!=uint8_t(Radical::Singlet)||atom->semantics.radical)continue;
        std::vector<ObjectId> added,retained;for(auto bid:doc.incidentBonds(ref.id)){auto edge=doc.bonds().alive(bid);auto other=edge->beginAtom==ref.id?edge->endAtom:edge->beginAtom;auto previous=studio_mechanism_products::sourceAtom(doc,other);(source.bondBetween(oldId,previous)==kInvalidObjectId?added:retained).push_back(other);}
        if(added.size()!=2||retained.empty()||doc.bondBetween(added[0],added[1])==kInvalidObjectId)continue;
        // Only unbranched alkene partners are regularized here. Substituted or
        // cyclic partners remain in the general constrained cleanup path.
        if(doc.incidentBonds(added[0]).size()!=2||doc.incidentBonds(added[1]).size()!=2)continue;
        bool stereo=false;for(auto id:added)for(auto bid:doc.incidentBonds(id))stereo|=mechanism::step_detail::hasStereoMark(*doc.bonds().alive(bid));if(stereo)continue;
        Vec2 away;for(auto id:retained){auto v=doc.atoms().alive(id)->position-atom->position;double n=length(v);if(n>1e-8)away=away-v*(1/n);}if(length(away)<1e-8)away=Vec2(1,0);double angle=core::angleOf(away);
        Vec2 a=atom->position+core::direction(angle-30)*unit,b=atom->position+core::direction(angle+30)*unit;
        if(length(doc.atoms().alive(added[0])->position-a)+length(doc.atoms().alive(added[1])->position-b)>length(doc.atoms().alive(added[0])->position-b)+length(doc.atoms().alive(added[1])->position-a))std::swap(a,b);
        changed|=length(a-doc.atoms().alive(added[0])->position)>1e-7||length(b-doc.atoms().alive(added[1])->position)>1e-7;doc.setAtomPosition(added[0],a);doc.setAtomPosition(added[1],b);
    }return changed;
}
using Service = std::function<bool(const std::string&,std::string&)>;
inline Report run(Document& doc,const Document& source,const std::vector<ObjectRef>& made,const Service& service){
    Report result;result.changed=regularizeSmallProducts(doc,source,made);result.changed=regularizeCarbeneTriangles(doc,source,made)||result.changed;auto local=scope(doc,source,made);straightenIsocyanates(doc,local.movable);result.movable=local.movable.size();if(local.movable.empty())return result;auto q=quality(doc,local);result.detail="shape="+std::to_string(q.shape)+" crowd="+std::to_string(q.crowd);if(q.crowd<1e-6&&q.shape<.2){result.changed=packFragments(doc,local)||result.changed;return result;}Request input;if(!request(doc,local,input)){result.detail+=" request mapping failed";return result;}
    result.attempted=true;std::string output;if(!service(input.xml,output)){result.cancelled=output=="已取消化学服务";result.detail=output;if(!result.cancelled){result.changed=rebuildNewCycles(doc,local);result.changed=relax(doc,local)||result.changed;result.changed=packFragments(doc,local)||result.changed;}return result;}std::string reason;result.changed=apply(doc,input,output,&reason);if(!result.changed){result.changed=rebuildNewCycles(doc,local);result.changed=relax(doc,local)||result.changed;}result.changed=packFragments(doc,local)||result.changed;result.detail+=" "+reason;return result;
}
}
