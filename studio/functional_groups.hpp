#pragma once
#include "metal_complexes.hpp"
namespace functional_groups {
using namespace r62;using namespace core;
inline void checked(Error e){if(!e.ok())throw std::runtime_error(e.message);}
inline std::string definition(const Document& d,ObjectId id){
    const auto atom=d.atoms().alive(id);
    if(atom&&atom->semantics.nodeType==uint8_t(NodeType::Element)&&d.atomLabel(id)==elementSymbol(atom->element)&&!studioions::compact(d,id)&&!d.extensions(ObjectRef(ObjectKind::Atom,id)).findValue("studio.compound.fragment"))return "";
    if(d.incidentBonds(id).empty())if(auto ion=compounddata::ion(d.atomLabel(id),d.atoms().alive(id)->semantics.charge))return ion->fragment;
    const auto label=d.atomLabel(id);
    if(auto b=bridgedata::find(label))return b->fragment;
    const auto group=nickdata::find(label=="MeS"?"SMe":label);
    if(auto raw=d.extensions(ObjectRef(ObjectKind::Atom,id)).findValue("studio.compound.fragment")){
        // Recover the known stale library fragment while preserving imported custom definitions.
        const auto wrong=compounddata::find(label=="SMe"?"MeS":label);
        if(group&&wrong&&*raw==wrong->fragment&&(std::string("|")+wrong->aliases+"|").find("|"+label+"|")==std::string::npos)return group->fragment;
        return *raw;
    }
    if(auto raw=d.extensions(ObjectRef(ObjectKind::Atom,id)).findValue("studio.nickname.fragment"))return *raw;
    if(metal_complexes::eligible(d,id))return "<metal-ligands/>";
    auto name=d.atomLabel(id);if(group)return group->fragment;if(auto n=customgroups::find(name))return n->fragment;if(auto n=compounddata::find(name))if((std::string("|")+n->aliases+"|").find("|"+name+"|")!=std::string::npos)return n->fragment;return "";
}
inline std::set<ObjectId> selection(const Session& s){std::set<ObjectId> ids;for(auto r:studioio::expanded(s.doc,s.selected)){if(r.kind==ObjectKind::Atom&&s.doc.atoms().alive(r.id))ids.insert(r.id);else if(r.kind==ObjectKind::Bond)if(auto b=s.doc.bonds().alive(r.id)){ids.insert(b->beginAtom);ids.insert(b->endAtom);}}return ids;}
inline void copyAtomData(const Document& from,ObjectId old,Document& to,ObjectId fresh){
    if(!from.atomLabel(old).empty()||!to.atomLabel(fresh).empty()){checked(to.setAtomLabel(fresh,from.atomLabel(old)));if(from.atomLabel(old).empty())to.setAtomLabelQuiet(fresh,"");}for(const auto& e:from.extensions(ObjectRef(ObjectKind::Atom,old)).entries())if(e.key.compare(0,4,"pst.")!=0)to.extensions(ObjectRef(ObjectKind::Atom,fresh)).set(e.key,e.value);
    auto runs=studioatom::runs(from,old);if(!runs.empty())studioatom::set(to,fresh,runs);else to.extensions(ObjectRef(ObjectKind::Atom,fresh)).erase("studio.atom.runs");
}
struct Expansion {ObjectId secondBond=kInvalidObjectId,secondAnchor=kInvalidObjectId,target=kInvalidObjectId,anchor=kInvalidObjectId,dummy=kInvalidObjectId,text=kInvalidObjectId;std::shared_ptr<Document> graph;bool intermediate=false,metalLigands=false;};
inline void esterStage(Expansion& p,const std::string& label){
    std::string suffix;if(label.compare(0,3,"COO")==0)suffix=label.substr(3);else if(label.compare(0,3,"CO2")==0)suffix=label.substr(3);else return;
    if(suffix=="H")return;if(suffix=="CH3")suffix="Me";const auto name="O"+suffix;if(suffix.empty()||!nickdata::find(name))return;
    auto& g=*p.graph;auto carbon=g.atoms().alive(p.anchor);if(!carbon||carbon->element!=6)return;
    ObjectId oxygen=kInvalidObjectId;bool carbonyl=false;
    for(auto bid:g.incidentBonds(p.anchor)){auto b=g.bonds().alive(bid);auto id=b->beginAtom==p.anchor?b->endAtom:b->beginAtom;auto a=g.atoms().alive(id);if(a&&a->element==8){if(b->order==BondOrder::Double)carbonyl=true;else if(b->order==BondOrder::Single)oxygen=id;}}
    if(!carbonyl||oxygen==kInvalidObjectId)return;
    std::set<ObjectId> seen{p.anchor,oxygen};std::vector<ObjectId> branch{oxygen};for(size_t i=0;i<branch.size();++i)for(auto bid:g.incidentBonds(branch[i])){auto b=g.bonds().alive(bid);auto id=b->beginAtom==branch[i]?b->endAtom:b->beginAtom;if(seen.insert(id).second)branch.push_back(id);}
    if(seen.count(p.dummy))return;
    for(auto id:branch)if(id!=oxygen)checked(g.removeObject(ObjectRef(ObjectKind::Atom,id)));
    checked(nicknames::apply(g,oxygen,name));p.intermediate=true;
}
inline Expansion prepareCompound(const Document& d,const std::string& raw,Vec2 position,ObjectId target=kInvalidObjectId,ObjectId text=kInvalidObjectId){
    if(target!=kInvalidObjectId&&!d.incidentBonds(target).empty()&&raw==compounddata::find("MES")->fragment&&!d.extensions(ObjectRef(ObjectKind::Atom,target)).findValue("studio.compound.anchor"))
        throw std::runtime_error("MES 是完整的缓冲剂分子，没有取代基连接点。请单独放置，或选择 Mes、MeS。");
    Expansion p;p.target=target;p.text=text;p.graph=std::make_shared<Document>();
    checked(persistence::readCdxml("<CDXML BondLength=\"22\"><page id=\"2000000000\">"+raw+"</page></CDXML>",*p.graph));
    Box bounds;bool found=false;for(const auto& a:p.graph->atoms().records())if(a.isAlive()){if(found)bounds.add(a.position);else{bounds=Box(a.position,a.position);found=true;}}
    if(!found)throw std::runtime_error("化合物缺少结构");
    const auto center=(bounds.min+bounds.max)*.5;double distance=1e100;
    for(const auto& a:p.graph->atoms().records())if(a.isAlive()){double gap=length(a.position-center);if(gap<distance){p.anchor=a.id;distance=gap;}}
    bool saved=false;
    if(target!=kInvalidObjectId){auto& bag=d.extensions(ObjectRef(ObjectKind::Atom,target));saved=bag.findValue("studio.compound.saved")!=nullptr;
        if(auto index=bag.findValue("studio.compound.anchor")){auto id=ObjectId(std::strtoul(index->c_str(),nullptr,10));if(p.graph->atoms().alive(id))p.anchor=id;}}
    // Alkoxide templates attach through their central element. Aluminium and
    // boron are deliberately included here even though they are not transition
    // metals, and the nearest atom to the drawing centre can be a ligand.
    if(!saved&&target!=kInvalidObjectId){
        const auto label=d.atomLabel(target);const auto ligand=label.find("(O");
        if(ligand!=std::string::npos){const auto symbol=label.substr(0,ligand);
            if(symbol=="Al"||symbol=="B"||symbol=="Ti"||symbol=="Zr"){
                const int element=elementNumber(symbol);
                for(const auto& a:p.graph->atoms().records())if(a.isAlive()&&a.element==element){
                    size_t oxygenNeighbors=0;for(auto bid:p.graph->incidentBonds(a.id)){
                        const auto b=p.graph->bonds().alive(bid);const auto other=b->beginAtom==a.id?b->endAtom:b->beginAtom;
                        if(const auto adjacent=p.graph->atoms().alive(other);adjacent&&adjacent->element==8)++oxygenNeighbors;
                    }
                    if(oxygenNeighbors>=3){p.anchor=a.id;break;}
                }
            }
        }
    }
    // A compact transition-metal formula attaches through its metal, even when the
    // template's geometric centre happens to be a carbon in a bulky ligand.
    if(!saved&&target!=kInvalidObjectId){auto span=studio::metal_labels::find(d.atomLabel(target));if(span.valid()){
        int element=elementNumber(d.atomLabel(target).substr(span.start,span.size));ObjectId metal=kInvalidObjectId;int count=0;bool stereo=false;
        for(const auto& a:p.graph->atoms().records())if(a.isAlive()&&studio::metal_labels::find(elementSymbol(a.element)).valid()){++count;if(a.element==element)metal=a.id;}
        for(const auto& b:p.graph->bonds().records())if(b.isAlive())stereo|=mechanism::step_detail::hasStereoMark(b);
        if(count==1&&metal!=kInvalidObjectId){p.anchor=metal;p.metalLigands=metal_complexes::parse(d.atomLabel(target)).valid()&&!stereo;}
    }}
    if(target!=kInvalidObjectId&&mechanism::lonePairCount(d,target)>0){
        int hidden=0;const auto donor=mechanism::lonePairHost(d,*d.atoms().alive(target),&hidden);
        for(const auto& a:p.graph->atoms().records())if(a.isAlive()&&a.element==donor.element&&a.semantics.charge==donor.semantics.charge){p.anchor=a.id;break;}
    }
    const auto anchor=p.graph->atoms().alive(p.anchor)->position;
    const double factor=saved?1.:d.defaultBondLength()/22.;
    for(const auto& a:p.graph->atoms().records())if(a.isAlive())checked(p.graph->setAtomPosition(a.id,position+(a.position-anchor)*factor));
    p.graph->setDefaultBondLength(d.defaultBondLength());return p;
}
inline Expansion prepareExpansion(const Document& d,ObjectId target){
    if(auto bridge=bridgedata::find(d.atomLabel(target))){
        auto links=d.incidentBonds(target);
        if(links.size()!=2||d.bonds().alive(links[0])->order!=BondOrder::Single||d.bonds().alive(links[1])->order!=BondOrder::Single)
            throw std::runtime_error("双端连接基需要两条外部单键");
        auto neighbor=[&](ObjectId bid){auto b=d.bonds().alive(bid);return d.atoms().alive(b->beginAtom==target?b->endAtom:b->beginAtom)->position;};
        Expansion p;p.graph=std::make_shared<Document>();p.target=target;p.secondBond=links[1];
        checked(persistence::readCdxml(std::string("<CDXML><page id=\"2000000000\">")+bridge->fragment+"</page></CDXML>",*p.graph));
        std::vector<ObjectId> ports,anchors;
        for(const auto& a:p.graph->atoms().records())if(a.isAlive()&&a.semantics.nodeType==uint8_t(NodeType::ExternalConnection)){
            ports.push_back(a.id);auto edge=p.graph->bonds().alive(p.graph->incidentBonds(a.id).at(0));anchors.push_back(edge->beginAtom==a.id?edge->endAtom:edge->beginAtom);
        }
        if(ports.size()!=2)throw std::runtime_error("连接基模板的端点数量异常");
        auto start=p.graph->atoms().alive(ports[0])->position,axis=p.graph->atoms().alive(ports[1])->position-start;
        auto origin=neighbor(links[0]),dest=neighbor(links[1])-origin;
        if(length(axis)<1e-6||length(dest)<1e-6)throw std::runtime_error("连接基端点重合");
        double norm=axis.x*axis.x+axis.y*axis.y;
        double c=(dest.x*axis.x+dest.y*axis.y)/norm,s=(dest.y*axis.x-dest.x*axis.y)/norm;
        for(auto port:ports)checked(p.graph->removeAtom(port));
        for(const auto& a:p.graph->atoms().records())if(a.isAlive()){auto v=a.position-start;checked(p.graph->setAtomPosition(a.id,origin+Vec2(c*v.x-s*v.y,s*v.x+c*v.y)));}
        p.anchor=anchors[0];p.secondAnchor=anchors[1];return p;
    }
    if(metal_complexes::eligible(d,target)){Expansion p;p.target=target;p.graph=std::make_shared<Document>();checked(metal_complexes::graph(d,target,*p.graph,&p.anchor));p.intermediate=true;p.metalLigands=true;return p;}
    if(studio::diazo::requested(d.atomLabel(target))&&studio::diazo::eligible(d,target)){
        Expansion p;p.target=target;p.graph=std::make_shared<Document>();p.graph->setDefaultBondLength(d.defaultBondLength());p.graph->setDefaultTextSize(d.defaultTextSize());
        checked(p.graph->addAtom(d.atoms().alive(target)->position,6,&p.anchor));
        if(studio::diazo::externalValence(d,target)==1&&d.incidentBonds(target).size()==1)checked(studio::diazo::terminal(*p.graph,p.anchor,studio::diazo::axis(d,target)));
        else checked(studio::diazo::append(*p.graph,p.anchor,studio::diazo::axis(d,target),studio::diazo::externalValence(d,target)));return p;
    }
    std::string raw=definition(d,target);if(raw.empty())throw std::runtime_error("缩写缺少结构定义，已保留原结构");
    if(raw.find("ExternalConnectionPoint")==std::string::npos)
        return prepareCompound(d,raw,d.atoms().alive(target)->position,target);
    Document local;local.setDefaultBondLength(d.defaultBondLength());local.setDefaultLineWidth(d.defaultLineWidth());auto atom=*d.atoms().alive(target);atom.sourceId=0;atom.semantics.nodeType=uint8_t(NodeType::Nickname);ObjectId root;checked(local.addAtomRecord(atom,&root));checked(local.setAtomLabel(root,d.atomLabel(target)));local.extensions(ObjectRef(ObjectKind::Atom,root)).set("studio.nickname.fragment",raw);
    auto incident=d.incidentBonds(target);if(incident.size()>1)throw std::runtime_error("单连接点官能团不能连接多根外部键");
    if(!incident.empty()){auto link=*d.bonds().alive(incident.front());ObjectId other=link.beginAtom==target?link.endAtom:link.beginAtom,dummy;checked(local.addAtom(d.atoms().alive(other)->position,6,&dummy));link.sourceId=0;link.beginAtom=link.beginAtom==target?root:dummy;link.endAtom=link.endAtom==target?root:dummy;ObjectId id;checked(local.addBondRecord(link,&id));}
    std::string xml=persistence::writeCdxmlToString(local);tinyxml2::XMLDocument source;source.Parse(xml.c_str());auto fragment=source.RootElement()->FirstChildElement("page")->FirstChildElement("fragment");int rootSource=-1,dummySource=-1;
    for(auto n=fragment->FirstChildElement("n");n;n=n->NextSiblingElement("n")){if(n->FirstChildElement("fragment")||n->FirstChildElement("nickname"))rootSource=n->IntAttribute("id");else dummySource=n->IntAttribute("id");}
    Expansion result;result.target=target;result.anchor=kInvalidObjectId;result.graph=std::make_shared<Document>();checked(persistence::readCdxml(nicknameexpand::expand(xml),*result.graph));
    for(const auto& a:result.graph->atoms().records())if(a.isAlive()){if(a.sourceId==rootSource)result.anchor=a.id;if(a.sourceId==dummySource)result.dummy=a.id;}
    if(result.anchor==kInvalidObjectId){for(const auto& a:result.graph->atoms().records())if(a.isAlive()&&a.id!=result.dummy){result.anchor=a.id;break;}}
    if(result.anchor==kInvalidObjectId)throw std::runtime_error("官能团没有可展开的原子");esterStage(result,d.atomLabel(target));if(!studio::validation::overValentAtoms(*result.graph).empty())throw std::runtime_error("官能团展开后价态异常，已保留原结构");return result;
}
inline void applyExpansion(Document& d,const Expansion& p,std::vector<ObjectRef>& inserted){
    size_t begin=inserted.size();
    ObjectId target=p.target;const auto& graph=*p.graph;
    if(target==kInvalidObjectId){auto a=*graph.atoms().alive(p.anchor);a.sourceId=0;checked(d.addAtomRecord(a,&target));if(p.text!=kInvalidObjectId)checked(d.removeObject(ObjectRef(ObjectKind::Text,p.text)));}
    std::map<ObjectId,ObjectId> ids;ids[p.anchor]=target;
    auto anchor=*graph.atoms().alive(p.anchor);anchor.id=target;anchor.sourceId=d.atoms().alive(target)->sourceId;checked(d.writeAtom(anchor));
    auto& ext=d.extensions(ObjectRef(ObjectKind::Atom,target));for(const char* key:{"studio.nickname.fragment","studio.compound.fragment","studio.compound.saved","studio.compound.anchor","studio.hydrogens.manual","studio.query.description","studio.text.tidy.original","studio.mechanism.ion.base","studio.mechanism.compact.valence","studio.label.offset","studio.label.text"})ext.erase(key);copyAtomData(graph,p.anchor,d,target);inserted.push_back(ObjectRef(ObjectKind::Atom,target));
    for(const auto& a:graph.atoms().records())if(a.isAlive()&&a.id!=p.dummy&&a.id!=p.anchor){auto c=a;c.sourceId=0;ObjectId id;checked(d.addAtomRecord(c,&id));ids[a.id]=id;copyAtomData(graph,a.id,d,id);inserted.push_back(ObjectRef(ObjectKind::Atom,id));}
    for(const auto& b:graph.bonds().records())if(b.isAlive()&&ids.count(b.beginAtom)&&ids.count(b.endAtom)){auto c=b;c.sourceId=0;c.beginAtom=ids[b.beginAtom];c.endAtom=ids[b.endAtom];ObjectId id;checked(d.addBondRecord(c,&id));for(const auto& e:graph.extensions(ObjectRef(ObjectKind::Bond,b.id)).entries())if(e.key.compare(0,4,"pst.")!=0)d.extensions(ObjectRef(ObjectKind::Bond,id)).set(e.key,e.value);inserted.push_back(ObjectRef(ObjectKind::Bond,id));}
    if(p.secondBond!=kInvalidObjectId){
        auto edge=d.bonds().alive(p.secondBond);auto second=ids.at(p.secondAnchor);
        checked(d.setBondEndpoints(p.secondBond,edge->beginAtom==target?second:edge->beginAtom,edge->endAtom==target?second:edge->endAtom));
    }
    // A free azide is drawn N-=N+=N-. Put its plain charges above/below
    // the chain so superscript charge glyphs cannot erase either double bond.
    if(graph.stats().aliveAtoms==3&&graph.stats().aliveBonds==2){
        std::vector<ObjectId> negative;ObjectId positive=kInvalidObjectId;bool azide=true;
        for(const auto& a:graph.atoms().records())if(a.isAlive()){
            azide&=a.element==7&&a.semantics.implicitHydrogens==0;
            if(a.semantics.charge==-1)negative.push_back(a.id);else if(a.semantics.charge==1)positive=a.id;else azide=false;
        }
        for(const auto& b:graph.bonds().records())if(b.isAlive())azide&=b.order==BondOrder::Double;
        if(azide&&negative.size()==2&&positive!=kInvalidObjectId){
            auto axis=graph.atoms().alive(negative[1])->position-graph.atoms().alive(negative[0])->position;
            auto normal=Vec2(-axis.y,axis.x);double span=length(normal);if(span>1e-8){normal=normal*(.80/span);if(normal.y>0)normal=normal*-1;
                for(const auto& item:ids){const auto q=graph.atoms().alive(item.first)->semantics.charge;const auto offset=normal*(q<0?1.:-1.);std::ostringstream out;out<<offset.x<<' '<<offset.y;auto& bag=d.extensions(ObjectRef(ObjectKind::Atom,item.second));bag.set("pst.attr.MoleculeStudioChargePosition",out.str());bag.erase("pst.attr.MoleculeStudioChargeCircle");}
            }
        }
    }
    for(const auto& g:d.groups().records())if(g.isAlive()&&std::find(g.members.begin(),g.members.end(),ObjectRef(ObjectKind::Atom,p.target))!=g.members.end()){auto group=d.groups().mutableGet(g.id);for(size_t i=begin;i<inserted.size();++i)if(std::find(group->members.begin(),group->members.end(),inserted[i])==group->members.end())group->members.push_back(inserted[i]);d.noteTouchedGroup(g.id);}
}
struct Pattern {std::string name,raw;std::shared_ptr<Document> graph;ObjectId anchor,marker;std::vector<ObjectId> atoms;bool custom=false,whole=false;};
inline bool pattern(const std::string& name,const std::string& raw,bool custom,Pattern& p){
    tinyxml2::XMLDocument xml;if(xml.Parse(raw.c_str())!=tinyxml2::XML_SUCCESS||!xml.RootElement()||std::string(xml.RootElement()->Name())!="fragment")return false;
    p.name=name;p.raw=raw;p.custom=custom;p.graph=std::make_shared<Document>();if(!persistence::readCdxml("<CDXML><page id=\"2000000000\">"+raw+"</page></CDXML>",*p.graph).ok())return false;
    p.marker=kInvalidObjectId;for(const auto& a:p.graph->atoms().records())if(a.isAlive()){if(a.semantics.nodeType==uint8_t(NodeType::ExternalConnection)){if(p.marker!=kInvalidObjectId)return false;p.marker=a.id;}else{if(a.semantics.nodeType!=uint8_t(NodeType::Element))return false;p.atoms.push_back(a.id);}}
    if(p.atoms.empty())return false;
    if(p.marker==kInvalidObjectId){p.whole=true;p.anchor=p.atoms.front();Box bounds(p.graph->atoms().alive(p.anchor)->position,p.graph->atoms().alive(p.anchor)->position);for(auto id:p.atoms)bounds.add(p.graph->atoms().alive(id)->position);const auto center=(bounds.min+bounds.max)*.5;for(auto id:p.atoms)if(length(p.graph->atoms().alive(id)->position-center)<length(p.graph->atoms().alive(p.anchor)->position-center))p.anchor=id;return true;}
    auto links=p.graph->incidentBonds(p.marker);if(links.size()!=1)return false;auto b=p.graph->bonds().alive(links[0]);if(b->order!=BondOrder::Single)return false;p.anchor=b->beginAtom==p.marker?b->endAtom:b->beginAtom;
    // Explicit stereo-specific library names need stereo-aware matching. Do not
    // assign one based on connectivity alone.
    for(const auto& b:p.graph->bonds().records())if(b.isAlive()&&(b.stereo!=BondStereo::None||b.display==BondDisplay::Wedge||b.display==BondDisplay::WedgeHash))return false;
    return true;
}
inline std::vector<Pattern> patterns(){
    static std::vector<Pattern> builtins;static bool ready=false;
    if(!ready){for(const auto& n:nickdata::nicknames){Pattern p;if(pattern(n.name,n.fragment,false,p))builtins.push_back(std::move(p));}for(const auto& n:compounddata::entries){Pattern p;if(pattern(n.name,n.fragment,false,p))builtins.push_back(std::move(p));}ready=true;}
    auto result=builtins;customgroups::load();for(const auto& n:customgroups::entries()){Pattern p;if(pattern(n.name,n.fragment,true,p))result.push_back(std::move(p));}
    std::stable_sort(result.begin(),result.end(),[](const Pattern& a,const Pattern& b){if(a.custom!=b.custom)return a.custom>b.custom;return a.atoms.size()>b.atoms.size();});return result;
}
struct Match {std::string name,fragment;ObjectId anchor;std::set<ObjectId> atoms;bool whole;
    Match(std::string n,std::string f,ObjectId a,std::set<ObjectId> ids,bool w=false):name(n),fragment(f),anchor(a),atoms(ids),whole(w){} };
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
                if(next==kInvalidObjectId){if(!p.whole)return false;for(auto tid:p.atoms)if(!mapping.count(tid)){next=tid;break;}}
                std::vector<ObjectId> candidates;
                if(parent==kInvalidObjectId)candidates.assign(selected.begin(),selected.end());else for(auto bid:d.incidentBonds(mapping[parent])){auto b=d.bonds().alive(bid);candidates.push_back(b->beginAtom==mapping[parent]?b->endAtom:b->beginAtom);}
                for(auto id:candidates){if(!selected.count(id)||used.count(id)||occupied.count(id)||!sameAtom(d,id,p,next))continue;bool compatible=true;
                    for(const auto& kv:mapping){auto a=bond(*p.graph,next,kv.first),b=bond(d,id,kv.second);if(bool(a)!=bool(b)||(a&&a->order!=b->order)){compatible=false;break;}
                        if(b&&(p.whole?(a->semantics!=b->semantics||a->stereo!=b->stereo||a->display!=b->display):(b->semantics!=BondSemantics::Normal&&b->semantics!=BondSemantics::Aromatic))){compatible=false;break;}}
                    if(!compatible)continue;mapping[next]=id;occupied.insert(id);if(search())return true;occupied.erase(id);mapping.erase(next);
                }return false;
            };
            if(!search())continue;ObjectId outside=kInvalidObjectId,boundary=kInvalidObjectId;bool safe=true;for(auto id:occupied)for(auto bid:d.incidentBonds(id)){auto b=d.bonds().alive(bid);ObjectId other=b->beginAtom==id?b->endAtom:b->beginAtom;if(!occupied.count(other)){if(id!=candidate||outside!=kInvalidObjectId){safe=false;break;}outside=other;boundary=bid;}}
            if(!safe||(p.whole?outside!=kInvalidObjectId:(outside==kInvalidObjectId||d.bonds().alive(boundary)->order!=BondOrder::Single)))continue;
            for(const auto* slab:{&d.arrows(),&d.brackets(),&d.symbols(),&d.orbitals(),&d.bioShapes()})for(const auto& a:slab->records())if(a.isAlive())for(auto id:a.anchors)if(occupied.count(id)&&id!=candidate)safe=false;
            if(!safe)continue;
            // Embed the actual matched atoms, coordinates and stereo, rather
            // than replacing them with an idealized library depiction.
            Document fragment;fragment.setDefaultBondLength(d.defaultBondLength());std::vector<ObjectRef> refs;for(auto id:occupied)refs.push_back(ObjectRef(ObjectKind::Atom,id));checked(studioio::append(d,fragment,refs,Vec2(),nullptr));ObjectId anchor=ObjectId(std::distance(occupied.begin(),occupied.find(candidate))),marker;
            if(!p.whole){checked(fragment.addAtom(d.atoms().alive(outside)->position,6,&marker));auto sem=fragment.atoms().alive(marker)->semantics;sem.nodeType=uint8_t(NodeType::ExternalConnection);checked(fragment.setAtomSemantics(marker,sem));auto link=*d.bonds().alive(boundary);link.sourceId=0;link.beginAtom=link.beginAtom==candidate?anchor:marker;link.endAtom=link.endAtom==candidate?anchor:marker;ObjectId linkId;checked(fragment.addBondRecord(link,&linkId));}
            std::string xml=persistence::writeCdxmlToString(fragment);tinyxml2::XMLDocument saved;saved.Parse(xml.c_str());auto f=saved.RootElement()->FirstChildElement("page")->FirstChildElement("fragment");tinyxml2::XMLPrinter printer;f->Accept(&printer);
            result.push_back({p.name,printer.CStr(),candidate,occupied,p.whole});used.insert(occupied.begin(),occupied.end());
        }
    }return result;
}
inline void collapse(Document& d,const Match& m){
    std::set<ObjectId> removedBonds;for(const auto& b:d.bonds().records())if(b.isAlive()&&m.atoms.count(b.beginAtom)&&m.atoms.count(b.endAtom))removedBonds.insert(b.id);
    for(auto id:removedBonds)checked(d.removeObject(ObjectRef(ObjectKind::Bond,id)));for(auto id:m.atoms)if(id!=m.anchor)checked(d.removeAtom(id));
    auto sem=d.atoms().alive(m.anchor)->semantics;sem.nodeType=uint8_t(NodeType::Nickname);sem.showLabel=true;sem.charge=sem.radical=sem.isotope=0;sem.aromatic=false;sem.implicitHydrogens=-1;checked(d.setElement(m.anchor,6));checked(d.setAtomSemantics(m.anchor,sem));checked(d.setAtomLabel(m.anchor,m.name));auto& ext=d.extensions(ObjectRef(ObjectKind::Atom,m.anchor));ext.erase("studio.nickname.fragment");ext.erase("studio.compound.fragment");ext.set(m.whole?"studio.compound.fragment":"studio.nickname.fragment",m.fragment);
    if(m.whole){ext.set("studio.compound.saved","1");ext.set("studio.compound.anchor",std::to_string(std::distance(m.atoms.begin(),m.atoms.find(m.anchor))));}
    if(m.whole){auto entry=compounddata::find(m.name);if(entry&&std::string(entry->formula)=="C18H15P"){auto phosphine=d.atoms().alive(m.anchor)->semantics;phosphine.nodeType=uint8_t(NodeType::Element);phosphine.implicitHydrogens=0;checked(d.setElement(m.anchor,15));checked(d.setAtomSemantics(m.anchor,phosphine));}}
    if(m.whole)if(const auto* hydride=compounddata::hydride(m.name)){auto sem=d.atoms().alive(m.anchor)->semantics;sem.implicitHydrogens=0;checked(d.setElement(m.anchor,hydride->element));checked(d.setAtomSemantics(m.anchor,sem));}
    ext.erase("studio.hydrogens.manual");ext.erase("studio.query.description");studioatom::set(d,m.anchor,{});
    if(m.whole&&m.name.find("·")!=std::string::npos){std::vector<TextRun> runs;for(const auto& part:chemcompletion::segments(m.name,true)){TextRun run;run.utf8=part.text;run.flags=part.script<0?kSubscript:part.script>0?kSuperscript:0;runs.push_back(run);}studioatom::set(d,m.anchor,runs);}
    if(m.whole&&studio::compact_radicals::find(m.name))checked(studio::compact_radicals::apply(d,m.anchor,m.name));
    for(const auto& group:d.groups().records())if(group.isAlive()){std::vector<ObjectRef> members;bool changed=false;for(auto ref:group.members){if(ref.kind==ObjectKind::Atom&&m.atoms.count(ref.id)){ref.id=m.anchor;changed=true;}if(ref.kind==ObjectKind::Bond&&removedBonds.count(ref.id)){changed=true;continue;}if(std::find(members.begin(),members.end(),ref)==members.end())members.push_back(ref);}if(changed){auto mutableGroup=d.groups().mutableGet(group.id);mutableGroup->members=members;d.noteTouchedGroup(group.id);}}
}
}
