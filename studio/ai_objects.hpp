#pragma once
namespace aiobjects {
using Refs=std::vector<ObjectRef>;
using Registry=std::map<std::string,Refs>;
inline std::string hexColor(uint32_t color){char text[16];std::snprintf(text,sizeof(text),"#%06X",unsigned(color>>8));return text;}
inline std::string kind(ObjectKind k){
    switch(k){case ObjectKind::Atom:return "atom";case ObjectKind::Bond:return "bond";case ObjectKind::Text:return "text";case ObjectKind::Arrow:return "arrow";case ObjectKind::Path:return "path";case ObjectKind::Bracket:return "bracket";case ObjectKind::Orbital:return "orbital";case ObjectKind::Symbol:return "symbol";case ObjectKind::BioShape:return "bio";case ObjectKind::Table:return "table";case ObjectKind::Group:return "group";default:return "unknown";}
}
inline std::string key(ObjectRef r){return kind(r.kind)+":"+std::to_string(r.id);}
inline std::string textPreview(const std::string& text,size_t limit=500){
    if(text.size()<=limit)return text;size_t end=limit;
    while(end>0&&(static_cast<unsigned char>(text[end])&0xC0)==0x80)--end;
    return text.substr(0,end);
}
inline Refs all(const Document& d){
    Refs out;
#define AI_SLAB(s,k) for(const auto& r:d.s().records())if(r.isAlive())out.push_back(ObjectRef(ObjectKind::k,r.id));
    AI_SLAB(atoms,Atom) AI_SLAB(bonds,Bond) AI_SLAB(texts,Text) AI_SLAB(arrows,Arrow) AI_SLAB(paths,Path) AI_SLAB(brackets,Bracket) AI_SLAB(orbitals,Orbital) AI_SLAB(symbols,Symbol) AI_SLAB(bioShapes,BioShape) AI_SLAB(tables,Table) AI_SLAB(groups,Group)
#undef AI_SLAB
    return out;
}
inline Registry registry(const Document& d,const Refs& selection){
    Registry r;r["document"]=all(d);auto expanded=studioio::expanded(d,selection);r["selection"]=Refs(expanded.begin(),expanded.end());r["created"]={};
    for(auto ref:r["document"]){r[key(ref)]={ref};if(ref.kind==ObjectKind::Group){auto members=studioio::expanded(d,{ref});r[key(ref)]=Refs(members.begin(),members.end());}}
    std::map<ObjectId,ObjectId> component;
    for(const auto& a:d.atoms().records())if(a.isAlive()&&!component.count(a.id)){
        std::vector<ObjectId> queue(1,a.id);component[a.id]=a.id;
        for(size_t i=0;i<queue.size();++i)for(auto neighbor:d.neighbours(queue[i]))if(!component.count(neighbor)){component[neighbor]=a.id;queue.push_back(neighbor);}
    }
    for(const auto& a:component)r["molecule:"+std::to_string(a.second)].push_back(ObjectRef(ObjectKind::Atom,a.first));
    for(const auto& b:d.bonds().records())if(b.isAlive())r["molecule:"+std::to_string(component[b.beginAtom])].push_back(ObjectRef(ObjectKind::Bond,b.id));
    return r;
}
inline void snapshot(const Document& d,const Refs& selection,Vec2 origin,tinyxml2::XMLDocument& xml,tinyxml2::XMLElement* root){
    auto r=registry(d,selection);std::set<ObjectRef> selected(r["selection"].begin(),r["selection"].end());std::map<ObjectRef,std::string> groups;
    for(const auto& group:r)if(group.first.compare(0,9,"molecule:")==0)for(auto ref:group.second)groups[ref]=group.first;
    auto scene=xml.NewElement("scene");root->InsertEndChild(scene);scene->SetAttribute("total",int(r["document"].size()));int count=0;
    // Show the active selection first, then its surrounding molecules. An
    // atom near the end of a large file must not disappear behind the cap.
    Refs ordered;std::set<ObjectRef> queued;auto queue=[&](ObjectRef ref){if(queued.insert(ref).second)ordered.push_back(ref);};
    for(auto ref:r["selection"])queue(ref);
    std::set<std::string> nearbyGroups;for(auto ref:r["selection"])if(groups.count(ref))nearbyGroups.insert(groups[ref]);
    for(const auto& group:nearbyGroups)for(auto ref:r[group])queue(ref);
    for(auto ref:r["document"])queue(ref);
    scene->SetAttribute("selection_total",int(r["selection"].size()));scene->SetAttribute("selection_complete",r["selection"].size()<=1500);
    for(auto ref:ordered){if(count++>=1500)break;auto item=xml.NewElement("object");scene->InsertEndChild(item);item->SetAttribute("ref",key(ref).c_str());item->SetAttribute("kind",kind(ref.kind).c_str());item->SetAttribute("selected",selected.count(ref)!=0);if(groups.count(ref))item->SetAttribute("molecule",groups[ref].c_str());
        auto b=selection::objectBounds(d,ref);if(!b.empty()){item->SetAttribute("x",b.min.x-origin.x);item->SetAttribute("y",b.min.y-origin.y);item->SetAttribute("x2",b.max.x-origin.x);item->SetAttribute("y2",b.max.y-origin.y);}
        if(ref.kind==ObjectKind::Atom){auto a=d.atoms().get(ref.id);item->SetAttribute("element",core::elementSymbol(a->element));item->SetAttribute("charge",int(a->semantics.charge));item->SetAttribute("is_element",a->semantics.nodeType==uint8_t(NodeType::Element));item->SetAttribute("label",d.atomLabel(ref.id).c_str());}
        if(ref.kind==ObjectKind::Bond){auto b=d.bonds().get(ref.id);item->SetAttribute("order",int(b->order));item->SetAttribute("begin",key(ObjectRef(ObjectKind::Atom,b->beginAtom)).c_str());item->SetAttribute("end",key(ObjectRef(ObjectKind::Atom,b->endAtom)).c_str());}
        if(ref.kind==ObjectKind::Text){auto text=d.texts().get(ref.id);item->SetAttribute("text",textPreview(fullText(d,ref.id)).c_str());item->SetAttribute("size",text->size);item->SetAttribute("font",(text->format.fontName.empty()?d.defaultFont():text->format.fontName).c_str());item->SetAttribute("bold",(text->format.flags&kBold)!=0);item->SetAttribute("italic",(text->format.flags&kItalic)!=0);}
        if(auto color=d.extensions(ref).findValue("studio.color.override"))item->SetAttribute("color",hexColor(uint32_t(std::strtoul(color->c_str(),nullptr,10))).c_str());
    }
    scene->SetAttribute("truncated",r["document"].size()>1500);
}
struct Selector{std::string target="auto",kind="all",element,contains;std::vector<std::string> refs;};
inline Selector selector(const tinyxml2::XMLElement* e){
    Selector s;if(auto v=e->Attribute("target"))s.target=v;if(auto v=e->Attribute("kind"))s.kind=v;if(auto v=e->Attribute("element"))s.element=v;if(auto v=e->Attribute("contains"))s.contains=v;
    for(auto ref=e->FirstChildElement("ref");ref;ref=ref->NextSiblingElement("ref")){auto v=ref->Attribute("id");if(!v||s.refs.size()>=500)throw std::runtime_error("AI 对象引用无效");s.refs.push_back(v);}
    if(s.target.size()>100||s.contains.size()>2000||(!s.element.empty()&&core::elementNumber(s.element)<1))throw std::runtime_error("AI 筛选条件无效");
    static const std::set<std::string> kinds={"all","atom","bond","text","arrow","path","bracket","orbital","symbol","bio","table","group","graphics"};if(!kinds.count(s.kind))throw std::runtime_error("AI 对象类型无效");return s;
}
inline Refs resolve(const Document& d,const Registry& registry,const Selector& s){
    std::set<ObjectRef> candidates,live;for(auto ref:all(d))live.insert(ref);
    auto add=[&](std::string target){if(target=="auto")target=registry.at("selection").empty()?"document":"selection";auto found=registry.find(target);if(found==registry.end())throw std::runtime_error("AI 对象引用不存在："+target);for(auto ref:found->second)if(live.count(ref))candidates.insert(ref);};
    if(s.refs.empty())add(s.target);else for(const auto& ref:s.refs)add(ref);
    Refs out;for(auto ref:candidates){auto k=kind(ref.kind);if(s.kind!="all"&&s.kind!=k&&!(s.kind=="graphics"&&k!="atom"&&k!="bond"&&k!="text"&&k!="group"))continue;
        if(!s.element.empty()){if(ref.kind!=ObjectKind::Atom)continue;auto a=d.atoms().get(ref.id);if(a->element!=core::elementNumber(s.element)||a->semantics.nodeType!=uint8_t(NodeType::Element))continue;}
        if(!s.contains.empty()&&(ref.kind!=ObjectKind::Text||fullText(d,ref.id).find(s.contains)==std::string::npos))continue;out.push_back(ref);
    }return out;
}
inline void created(Registry& registry,const std::string& handle,const Refs& refs,const Document* source=nullptr){
    if(handle.empty())return;registry[handle]=refs;
    for(size_t i=0;i<refs.size();++i){auto ref=refs[i];registry["created"].push_back(ref);registry["document"].push_back(ref);registry[handle+"/object:"+std::to_string(i)]={ref};}
    if(source){size_t i=0;for(const auto& a:source->atoms().records())if(a.isAlive()&&i<refs.size())registry[handle+"/atom:"+std::to_string(a.sourceId)]={refs[i++]};for(const auto& b:source->bonds().records())if(b.isAlive()&&i<refs.size())registry[handle+"/bond:"+std::to_string(b.sourceId)]={refs[i++]};}
}
}
