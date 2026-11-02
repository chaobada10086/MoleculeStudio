#pragma once
namespace functional_groups {
using namespace r62;using namespace core;
inline void checked(Error e){if(!e.ok())throw std::runtime_error(e.message);}
inline std::string definition(const Document& d,ObjectId id){
    if(auto raw=d.extensions(ObjectRef(ObjectKind::Atom,id)).findValue("studio.nickname.fragment"))return *raw;
    auto name=d.atomLabel(id);if(auto n=nickdata::find(name))return n->fragment;if(auto n=customgroups::find(name))return n->fragment;return "";
}
inline std::set<ObjectId> selection(const Session& s){std::set<ObjectId> ids;for(auto r:studioio::expanded(s.doc,s.selected)){if(r.kind==ObjectKind::Atom&&s.doc.atoms().alive(r.id))ids.insert(r.id);else if(r.kind==ObjectKind::Bond)if(auto b=s.doc.bonds().alive(r.id)){ids.insert(b->beginAtom);ids.insert(b->endAtom);}}return ids;}
inline void copyAtomData(const Document& from,ObjectId old,Document& to,ObjectId fresh){
    if(!from.atomLabel(old).empty()||!to.atomLabel(fresh).empty()){checked(to.setAtomLabel(fresh,from.atomLabel(old)));if(from.atomLabel(old).empty())to.setAtomLabelQuiet(fresh,"");}for(const auto& e:from.extensions(ObjectRef(ObjectKind::Atom,old)).entries())if(e.key.compare(0,4,"pst.")!=0)to.extensions(ObjectRef(ObjectKind::Atom,fresh)).set(e.key,e.value);
    auto runs=studioatom::runs(from,old);if(!runs.empty())studioatom::set(to,fresh,runs);else to.extensions(ObjectRef(ObjectKind::Atom,fresh)).erase("studio.atom.runs");
}
struct Expansion {ObjectId target,anchor,dummy=kInvalidObjectId;std::shared_ptr<Document> graph;};
inline Expansion prepareExpansion(const Document& d,ObjectId target){
    std::string raw=definition(d,target);if(raw.empty())throw std::runtime_error("缩写缺少结构定义，已保留原结构");
    Document local;local.setDefaultBondLength(d.defaultBondLength());local.setDefaultLineWidth(d.defaultLineWidth());auto atom=*d.atoms().alive(target);atom.sourceId=0;atom.semantics.nodeType=uint8_t(NodeType::Nickname);ObjectId root;checked(local.addAtomRecord(atom,&root));checked(local.setAtomLabel(root,d.atomLabel(target)));local.extensions(ObjectRef(ObjectKind::Atom,root)).set("studio.nickname.fragment",raw);
    auto incident=d.incidentBonds(target);if(incident.size()>1)throw std::runtime_error("单连接点官能团不能连接多根外部键");
    if(!incident.empty()){auto link=*d.bonds().alive(incident.front());ObjectId other=link.beginAtom==target?link.endAtom:link.beginAtom,dummy;checked(local.addAtom(d.atoms().alive(other)->position,6,&dummy));link.sourceId=0;link.beginAtom=link.beginAtom==target?root:dummy;link.endAtom=link.endAtom==target?root:dummy;ObjectId id;checked(local.addBondRecord(link,&id));}
    std::string xml=persistence::writeCdxmlToString(local);tinyxml2::XMLDocument source;source.Parse(xml.c_str());auto fragment=source.RootElement()->FirstChildElement("page")->FirstChildElement("fragment");int rootSource=-1,dummySource=-1;
    for(auto n=fragment->FirstChildElement("n");n;n=n->NextSiblingElement("n")){if(n->FirstChildElement("fragment")||n->FirstChildElement("nickname"))rootSource=n->IntAttribute("id");else dummySource=n->IntAttribute("id");}
    Expansion result;result.target=target;result.anchor=kInvalidObjectId;result.graph=std::make_shared<Document>();checked(persistence::readCdxml(nicknameexpand::expand(xml),*result.graph));
    for(const auto& a:result.graph->atoms().records())if(a.isAlive()){if(a.sourceId==rootSource)result.anchor=a.id;if(a.sourceId==dummySource)result.dummy=a.id;}
    if(result.anchor==kInvalidObjectId){for(const auto& a:result.graph->atoms().records())if(a.isAlive()&&a.id!=result.dummy){result.anchor=a.id;break;}}
    if(result.anchor==kInvalidObjectId)throw std::runtime_error("官能团没有可展开的原子");if(!studio::validation::overValentAtoms(*result.graph).empty())throw std::runtime_error("官能团展开后价态异常，已保留原结构");return result;
}
inline void applyExpansion(Document& d,const Expansion& p,std::vector<ObjectRef>& inserted){
    size_t begin=inserted.size();
    const auto& graph=*p.graph;std::map<ObjectId,ObjectId> ids;ids[p.anchor]=p.target;
    auto anchor=*graph.atoms().alive(p.anchor);anchor.id=p.target;anchor.sourceId=d.atoms().alive(p.target)->sourceId;checked(d.writeAtom(anchor));
    auto& ext=d.extensions(ObjectRef(ObjectKind::Atom,p.target));ext.erase("studio.nickname.fragment");ext.erase("studio.hydrogens.manual");ext.erase("studio.query.description");copyAtomData(graph,p.anchor,d,p.target);inserted.push_back(ObjectRef(ObjectKind::Atom,p.target));
    for(const auto& a:graph.atoms().records())if(a.isAlive()&&a.id!=p.dummy&&a.id!=p.anchor){auto c=a;c.sourceId=0;ObjectId id;checked(d.addAtomRecord(c,&id));ids[a.id]=id;copyAtomData(graph,a.id,d,id);inserted.push_back(ObjectRef(ObjectKind::Atom,id));}
    for(const auto& b:graph.bonds().records())if(b.isAlive()&&ids.count(b.beginAtom)&&ids.count(b.endAtom)){auto c=b;c.sourceId=0;c.beginAtom=ids[b.beginAtom];c.endAtom=ids[b.endAtom];ObjectId id;checked(d.addBondRecord(c,&id));for(const auto& e:graph.extensions(ObjectRef(ObjectKind::Bond,b.id)).entries())if(e.key.compare(0,4,"pst.")!=0)d.extensions(ObjectRef(ObjectKind::Bond,id)).set(e.key,e.value);inserted.push_back(ObjectRef(ObjectKind::Bond,id));}
    for(const auto& g:d.groups().records())if(g.isAlive()&&std::find(g.members.begin(),g.members.end(),ObjectRef(ObjectKind::Atom,p.target))!=g.members.end()){auto group=d.groups().mutableGet(g.id);for(size_t i=begin;i<inserted.size();++i)if(std::find(group->members.begin(),group->members.end(),inserted[i])==group->members.end())group->members.push_back(inserted[i]);d.noteTouchedGroup(g.id);}
}
struct Pattern {std::string name,raw;std::shared_ptr<Document> graph;ObjectId anchor,marker;std::vector<ObjectId> atoms;bool custom=false;};
inline bool pattern(const std::string& name,const std::string& raw,bool custom,Pattern& p){
    tinyxml2::XMLDocument xml;if(xml.Parse(raw.c_str())!=tinyxml2::XML_SUCCESS||!xml.RootElement()||std::string(xml.RootElement()->Name())!="fragment")return false;
    p.name=name;p.raw=raw;p.custom=custom;p.graph=std::make_shared<Document>();if(!persistence::readCdxml("<CDXML><page id=\"2000000000\">"+raw+"</page></CDXML>",*p.graph).ok())return false;
    p.marker=kInvalidObjectId;for(const auto& a:p.graph->atoms().records())if(a.isAlive()){if(a.semantics.nodeType==uint8_t(NodeType::ExternalConnection)){if(p.marker!=kInvalidObjectId)return false;p.marker=a.id;}else{if(a.semantics.nodeType!=uint8_t(NodeType::Element))return false;p.atoms.push_back(a.id);}}
    if(p.marker==kInvalidObjectId||p.atoms.empty())return false;auto links=p.graph->incidentBonds(p.marker);if(links.size()!=1)return false;auto b=p.graph->bonds().alive(links[0]);if(b->order!=BondOrder::Single)return false;p.anchor=b->beginAtom==p.marker?b->endAtom:b->beginAtom;
    // Explicit stereo-specific library names need stereo-aware matching. Do not
    // assign one based on connectivity alone.
    for(const auto& b:p.graph->bonds().records())if(b.isAlive()&&(b.stereo!=BondStereo::None||b.display==BondDisplay::Wedge||b.display==BondDisplay::WedgeHash))return false;
    return true;
}
inline std::vector<Pattern> patterns(){
    static std::vector<Pattern> builtins;static bool ready=false;
    if(!ready){for(const auto& n:nickdata::nicknames){Pattern p;if(pattern(n.name,n.fragment,false,p))builtins.push_back(std::move(p));}ready=true;}
    auto result=builtins;customgroups::load();for(const auto& n:customgroups::entries()){Pattern p;if(pattern(n.name,n.fragment,true,p))result.push_back(std::move(p));}
    std::stable_sort(result.begin(),result.end(),[](const Pattern& a,const Pattern& b){if(a.custom!=b.custom)return a.custom>b.custom;return a.atoms.size()>b.atoms.size();});return result;
}
struct Match {std::string name,fragment;ObjectId anchor;std::set<ObjectId> atoms;};
inline const BondRecord* bond(const Document& d,ObjectId a,ObjectId b){for(auto id:d.incidentBonds(a)){auto r=d.bonds().alive(id);if((r->beginAtom==a&&r->endAtom==b)||(r->beginAtom==b&&r->endAtom==a))return r;}return nullptr;}
inline int hydrogens(const Document& d,ObjectId id){auto a=d.atoms().alive(id);if(a->semantics.implicitHydrogens>=0)return a->semantics.implicitHydrogens;int sum=0;for(auto bid:d.incidentBonds(id)){auto b=d.bonds().alive(bid);if(b->order==BondOrder::Aromatic)return -1;sum+=int(b->order);}auto r=chemistry::implicitHydrogensSimple(a->element,a->semantics.charge,sum,false);return r.valid?r.count:-1;}
inline bool sameAtom(const Document& d,ObjectId id,const Pattern& p,ObjectId tid){
    auto a=d.atoms().alive(id),b=p.graph->atoms().alive(tid);if(!a||a->semantics.nodeType!=uint8_t(NodeType::Element)||a->element!=b->element||a->semantics.charge!=b->semantics.charge||a->semantics.isotope!=b->semantics.isotope||a->semantics.radical!=b->semantics.radical)return false;
    if(!d.atomLabel(id).empty()&&d.atomLabel(id)!=core::elementSymbol(a->element))return false;
    if(d.incidentBonds(id).size()!=p.graph->incidentBonds(tid).size())return false;
    int expected=hydrogens(*p.graph,tid),actual=hydrogens(d,id);return expected<0||actual<0||expected==actual;
}
inline std::vector<Match> findMatches(const Document& d,const std::set<ObjectId>& selected){
    std::vector<Match> result;std::set<ObjectId> used;size_t budget=2000000;
    for(const auto& p:patterns()){
        if(p.atoms.size()>selected.size())continue;
        for(auto candidate:selected){if(!budget||used.count(candidate)||!sameAtom(d,candidate,p,p.anchor))continue;
            std::map<ObjectId,ObjectId> mapping;std::set<ObjectId> occupied;mapping[p.anchor]=candidate;occupied.insert(candidate);
            std::function<bool()> search=[&](){if(!budget)return false;--budget;if(mapping.size()==p.atoms.size())return true;
                ObjectId next=kInvalidObjectId,parent=kInvalidObjectId;for(auto tid:p.atoms)if(!mapping.count(tid)){for(auto bid:p.graph->incidentBonds(tid)){auto b=p.graph->bonds().alive(bid);ObjectId other=b->beginAtom==tid?b->endAtom:b->beginAtom;if(mapping.count(other)){next=tid;parent=other;break;}}if(next!=kInvalidObjectId)break;}
                if(next==kInvalidObjectId)return false;
                for(auto bid:d.incidentBonds(mapping[parent])){auto b=d.bonds().alive(bid);ObjectId id=b->beginAtom==mapping[parent]?b->endAtom:b->beginAtom;if(!selected.count(id)||used.count(id)||occupied.count(id)||!sameAtom(d,id,p,next))continue;bool compatible=true;
                    for(const auto& kv:mapping){auto a=bond(*p.graph,next,kv.first),b=bond(d,id,kv.second);if(bool(a)!=bool(b)||(a&&a->order!=b->order)){compatible=false;break;}if(b&&b->semantics!=BondSemantics::Normal&&b->semantics!=BondSemantics::Aromatic){compatible=false;break;}}
                    if(!compatible)continue;mapping[next]=id;occupied.insert(id);if(search())return true;occupied.erase(id);mapping.erase(next);
                }return false;
            };
            if(!search())continue;ObjectId outside=kInvalidObjectId,boundary=kInvalidObjectId;bool safe=true;for(auto id:occupied)for(auto bid:d.incidentBonds(id)){auto b=d.bonds().alive(bid);ObjectId other=b->beginAtom==id?b->endAtom:b->beginAtom;if(!occupied.count(other)){if(id!=candidate||outside!=kInvalidObjectId){safe=false;break;}outside=other;boundary=bid;}}
            if(!safe||outside==kInvalidObjectId||d.bonds().alive(boundary)->order!=BondOrder::Single)continue;
            for(const auto* slab:{&d.arrows(),&d.brackets(),&d.symbols(),&d.orbitals(),&d.bioShapes()})for(const auto& a:slab->records())if(a.isAlive())for(auto id:a.anchors)if(occupied.count(id)&&id!=candidate)safe=false;
            if(!safe)continue;
            // Embed the actual matched atoms, coordinates and stereo, rather
            // than replacing them with an idealized library depiction.
            Document fragment;fragment.setDefaultBondLength(d.defaultBondLength());std::vector<ObjectRef> refs;for(auto id:occupied)refs.push_back(ObjectRef(ObjectKind::Atom,id));checked(studioio::append(d,fragment,refs,Vec2(),nullptr));ObjectId anchor=ObjectId(std::distance(occupied.begin(),occupied.find(candidate))),marker;
            checked(fragment.addAtom(d.atoms().alive(outside)->position,6,&marker));auto sem=fragment.atoms().alive(marker)->semantics;sem.nodeType=uint8_t(NodeType::ExternalConnection);checked(fragment.setAtomSemantics(marker,sem));auto link=*d.bonds().alive(boundary);link.sourceId=0;link.beginAtom=link.beginAtom==candidate?anchor:marker;link.endAtom=link.endAtom==candidate?anchor:marker;ObjectId linkId;checked(fragment.addBondRecord(link,&linkId));
            std::string xml=persistence::writeCdxmlToString(fragment);tinyxml2::XMLDocument saved;saved.Parse(xml.c_str());auto f=saved.RootElement()->FirstChildElement("page")->FirstChildElement("fragment");tinyxml2::XMLPrinter printer;f->Accept(&printer);
            result.push_back({p.name,printer.CStr(),candidate,occupied});used.insert(occupied.begin(),occupied.end());
        }
    }return result;
}
inline void collapse(Document& d,const Match& m){
    std::set<ObjectId> removedBonds;for(const auto& b:d.bonds().records())if(b.isAlive()&&m.atoms.count(b.beginAtom)&&m.atoms.count(b.endAtom))removedBonds.insert(b.id);
    for(auto id:removedBonds)checked(d.removeObject(ObjectRef(ObjectKind::Bond,id)));for(auto id:m.atoms)if(id!=m.anchor)checked(d.removeAtom(id));
    auto sem=d.atoms().alive(m.anchor)->semantics;sem.nodeType=uint8_t(NodeType::Nickname);sem.showLabel=true;sem.charge=sem.radical=sem.isotope=0;sem.aromatic=false;sem.implicitHydrogens=-1;checked(d.setElement(m.anchor,6));checked(d.setAtomSemantics(m.anchor,sem));checked(d.setAtomLabel(m.anchor,m.name));auto& ext=d.extensions(ObjectRef(ObjectKind::Atom,m.anchor));ext.set("studio.nickname.fragment",m.fragment);ext.erase("studio.hydrogens.manual");ext.erase("studio.query.description");
    for(const auto& group:d.groups().records())if(group.isAlive()){std::vector<ObjectRef> members;bool changed=false;for(auto ref:group.members){if(ref.kind==ObjectKind::Atom&&m.atoms.count(ref.id)){ref.id=m.anchor;changed=true;}if(ref.kind==ObjectKind::Bond&&removedBonds.count(ref.id)){changed=true;continue;}if(std::find(members.begin(),members.end(),ref)==members.end())members.push_back(ref);}if(changed){auto mutableGroup=d.groups().mutableGet(group.id);mutableGroup->members=members;d.noteTouchedGroup(group.id);}}
}
}
