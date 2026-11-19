// Included inside studioio after the document and XML APIs are available.
inline Error readArrows(const std::string& bytes,Document& doc){
    std::string normalized=bytes;persistence::repairUnreferencedArrowIds(normalized);tinyxml2::XMLDocument xml;if(xml.Parse(normalized.c_str())!=tinyxml2::XML_SUCCESS)return Error();auto root=xml.RootElement();if(!root)return Error();std::set<int> imported;
    struct Binding {ObjectId id;std::string source,target;};std::vector<Binding> bindings;
    std::function<Error(tinyxml2::XMLElement*)> visit=[&](tinyxml2::XMLElement* parent){for(auto node=parent->FirstChildElement();node;node=node->NextSiblingElement()){
        if((std::string(node->Name())=="arrow"||((std::string(node->Name())=="graphic"||std::string(node->Name())=="curve")&&node->Attribute("MoleculeStudioArrow")))&&node->Attribute("Tail3D")&&node->Attribute("Head3D")){
            auto point=[&](const char* name){Vec2 p;std::istringstream in(node->Attribute(name));in>>p.x>>p.y;return p;};Vec2 begin=point("Tail3D"),end=point("Head3D");if(core::length(end-begin)<1e-8)continue;
            double width=node->DoubleAttribute("LineWidth",doc.defaultLineWidth());if(!(width>0))width=.6;arrows::ArrowParams p=arrows::resolvePreset(0,begin,end,width);bool decoded=false;if(auto custom=node->Attribute("MoleculeStudioArrow"))p=arrows::decodeArrowParams(custom,&decoded);if(!decoded){p=arrows::resolvePreset(0,begin,end,width);auto attr=[&](const char* key){auto v=node->Attribute(key);return std::string(v?v:"");};std::string head=attr("ArrowheadHead"),tail=attr("ArrowheadTail"),type=attr("ArrowheadType");p.head.atEnd=!head.empty()&&head!="None";p.head.atBegin=!tail.empty()&&tail!="None";p.head.shape=type=="Hollow"?arrows::ArrowHeadShape::Hollow:type=="Angle"?arrows::ArrowHeadShape::Angled:arrows::ArrowHeadShape::Solid;p.head.sides=head=="HalfLeft"?arrows::HeadSides::Left:head=="HalfRight"?arrows::HeadSides::Right:arrows::HeadSides::Both;p.head.length=node->DoubleAttribute("HeadSize",2250)*width/100;p.head.width=node->DoubleAttribute("ArrowheadWidth",563)*width*2/100;if(node->Attribute("ArrowShaftSpacing")){p.shafts=arrows::ShaftCount::Two;p.presetId=30;p.separation=node->DoubleAttribute("ArrowShaftSpacing")*width/100;double ratio=node->DoubleAttribute("ArrowEquilibriumRatio",100);p.splitRatio=100/(100+ratio);p.originalSplitRatio=p.splitRatio;if(type=="Angle"&&((head=="Full"&&!p.head.atBegin)||(tail=="Full"&&!p.head.atEnd))){p.shafts=arrows::ShaftCount::Double;p.presetId=40;p.head.shape=arrows::ArrowHeadShape::HalfArrow;p.head.sides=arrows::HeadSides::Both;p.separation*=2;p.head.width=p.separation;}}
                if(attr("LineType").find("Dashed")!=std::string::npos)p.line=arrows::ArrowLine::Dashed;std::string noGo=attr("NoGo");if(!noGo.empty()&&noGo!="None"){p.head.shape=noGo=="Hash"?arrows::ArrowHeadShape::Hash:arrows::ArrowHeadShape::Cross;/* Explicit head/tail attributes remain independent of the no-go marker. */}
                if(node->Attribute("AngularSize")){double angle=node->DoubleAttribute("AngularSize");if(std::abs(angle)>1e-7){p.family=arrows::ArrowFamily::Arc;p.sweep=arrows::ArcSweep::Free;p.arcSagitta=core::length(end-begin)*.5*std::tan(angle*3.141592653589793/720);}}
            }if(p.mechanism||p.virtualBond){
                auto electronRef=[&](const char* key){core::ObjectRef ref;auto text=node->Attribute(key);if(!text)return ref;std::string value=text;auto colon=value.find(':');if(colon==std::string::npos)return ref;int source=0;std::istringstream in(value.substr(colon+1));if(!(in>>source)||source<=0)return ref;std::string kind=value.substr(0,colon);if(kind=="atom")for(const auto& a:doc.atoms().records())if(a.isAlive()&&a.sourceId==source){if(ref.valid())return core::ObjectRef();ref=core::ObjectRef(core::ObjectKind::Atom,a.id);}if(kind=="bond")for(const auto& b:doc.bonds().records())if(b.isAlive()&&b.sourceId==source){if(ref.valid())return core::ObjectRef();ref=core::ObjectRef(core::ObjectKind::Bond,b.id);}return ref;};
                p.electronSource=electronRef("MoleculeStudioElectronSource");p.electronTarget=electronRef("MoleculeStudioElectronTarget");
            }p.begin=begin;p.end=end;int source=node->IntAttribute("id");ObjectId id=kInvalidObjectId;if(std::string(node->Name())=="graphic")for(auto& r:doc.arrows().records())if(r.isAlive()&&r.sourceId==source){id=r.id;break;}if(id!=kInvalidObjectId){auto e=arrows::writeArrow(doc,id,p,doc.defaultLineWidth());if(!e.ok())return e;}else{AnnotationRecord record;auto e=arrows::toAnnotationRecord(p,doc.defaultLineWidth(),&record);if(!e.ok())return e;record.sourceId=source;e=doc.addAnnotation(ObjectKind::Arrow,record,&id);if(!e.ok())return e;doc.extensions(ObjectRef(ObjectKind::Arrow,id)).set(arrows::kArrowParamsKey,arrows::encodeArrowParams(p));}{auto& bag=doc.extensions(ObjectRef(ObjectKind::Arrow,id));auto v=doc.documentExtensions().findValue("studio.palette."+std::string(node->Attribute("color")?node->Attribute("color"):"0"));if(v)bag.set("studio.color",*v);if(node->Attribute("AngularSize")&&!node->Attribute("MoleculeStudioArrow")){tinyxml2::XMLPrinter xml;node->Accept(&xml);bag.set("studio.cdxml.graphic",xml.CStr());}if(auto z=node->Attribute("Z"))bag.set("studio.z",z);}if(auto mode=node->Attribute("MoleculeStudioCatalytic"))doc.extensions(ObjectRef(ObjectKind::Arrow,id)).set("studio.catalytic",mode);if(auto path=node->Attribute("MoleculeStudioCatalyticPath"))doc.extensions(ObjectRef(ObjectKind::Arrow,id)).set("pst.attr.MoleculeStudioCatalyticPath",path);if(p.mechanism||p.virtualBond||node->Attribute("MoleculeStudioCatalytic"))bindings.push_back({id,node->Attribute("MoleculeStudioElectronSource")?node->Attribute("MoleculeStudioElectronSource"):"",node->Attribute("MoleculeStudioElectronTarget")?node->Attribute("MoleculeStudioElectronTarget"):""});if(std::string(node->Name())=="curve"){std::vector<ObjectId> old;for(const auto& path:doc.paths().records())if(path.isAlive()&&path.sourceId==source)old.push_back(path.id);for(auto id:old)doc.removeObject(ObjectRef(ObjectKind::Path,id));}imported.insert(source);
        }auto e=visit(node);if(!e.ok())return e;
    }return Error();};auto e=visit(root);if(!e.ok())return e;
    auto resolve=[&](const std::string& value){
        auto colon=value.find(':');if(colon==std::string::npos)return ObjectRef();
        const auto kind=value.substr(0,colon);int file=0;std::istringstream in(value.substr(colon+1));if(!(in>>file)||file<=0)return ObjectRef();
        if(kind=="atom")for(const auto& a:doc.atoms().records())if(a.isAlive()&&a.sourceId==file)return ObjectRef(ObjectKind::Atom,a.id);
        if(kind=="bond")for(const auto& b:doc.bonds().records())if(b.isAlive()&&b.sourceId==file)return ObjectRef(ObjectKind::Bond,b.id);
        if(kind=="arrow")for(const auto& a:doc.arrows().records())if(a.isAlive()&&a.sourceId==file)return ObjectRef(ObjectKind::Arrow,a.id);
        return ObjectRef();
    };
    for(const auto& binding:bindings){
        arrows::ArrowParams p;bool valid=false;
        auto error=arrows::readArrow(doc,binding.id,&p,&valid);if(!error.ok())return error;
        p.electronSource=resolve(binding.source);p.electronTarget=resolve(binding.target);
        error=arrows::writeArrow(doc,binding.id,p,doc.defaultLineWidth());if(!error.ok())return error;
    }
    // Companion vector heads belong to a promoted mechanism arrow. Keep them
    // only when its metadata is absent (e.g. a ChemDraw/CDX external roundtrip).
    std::function<void(tinyxml2::XMLElement*)> removeParts=[&](tinyxml2::XMLElement* parent){for(auto n=parent->FirstChildElement();n;n=n->NextSiblingElement()){
        if(n->Attribute("MoleculeStudioArrowPart")&&imported.count(n->IntAttribute("MoleculeStudioArrowPart"))){
            std::vector<ObjectId> dead;for(const auto& path:doc.paths().records())if(path.isAlive()&&path.sourceId==n->IntAttribute("id"))dead.push_back(path.id);
            for(auto id:dead)doc.removeObject(ObjectRef(ObjectKind::Path,id));
        }removeParts(n);
    }};removeParts(root);
    // Native ChemDraw pairs a modern arrow with a superseded legacy graphic.
    // Replace that fallback instead of showing both or replaying stale raw XML.
    std::function<void(tinyxml2::XMLElement*)> legacy=[&](tinyxml2::XMLElement* parent){for(auto n=parent->FirstChildElement();n;n=n->NextSiblingElement()){if(n->Attribute("SupersededBy")&&imported.count(n->IntAttribute("SupersededBy"))){int id=n->IntAttribute("id");std::vector<ObjectId> dead;for(auto& r:doc.arrows().records())if(r.isAlive()&&r.sourceId==id)dead.push_back(r.id);for(auto a:dead)doc.removeObject(ObjectRef(ObjectKind::Arrow,a));}legacy(n);}};legacy(root);
    auto entries=doc.documentExtensions().entries();for(auto& entry:entries)if(entry.key.compare(0,8,"pst.raw.")==0){
        // The first page arrow can carry a leading </fragment> in its preserved
        // slice. Parsing that slice as a standalone XML document fails, leaving
        // both the raw arrow and the promoted arrow to be exported with one ID.
        // Retain the structural prefix, and remove only the promoted arrow.
        size_t at=entry.value.find("<arrow");if(at==std::string::npos)continue;
        tinyxml2::XMLDocument raw;if(raw.Parse(entry.value.c_str()+at)!=tinyxml2::XML_SUCCESS)continue;
        auto r=raw.RootElement();if(r&&!r->NextSiblingElement()&&std::string(r->Name())=="arrow"&&imported.count(r->IntAttribute("id"))){
            if(at)doc.documentExtensions().set(entry.key,entry.value.substr(0,at),entry.sourceId);
            else doc.documentExtensions().erase(entry.key,entry.sourceId);
        }
    }
    std::function<void(tinyxml2::XMLElement*)> joined=[&](tinyxml2::XMLElement* parent){for(auto n=parent->FirstChildElement();n;n=n->NextSiblingElement()){
        if(std::string(n->Name())=="group"){
            GroupRecord group;group.label="Joined arrows";bool onlyArrows=true;
            for(auto child=n->FirstChildElement();child;child=child->NextSiblingElement()){
                std::string kind=child->Name();bool arrow=kind=="arrow"||(kind=="curve"&&child->Attribute("MoleculeStudioArrow"));if(!arrow){onlyArrows=false;continue;}
                int source=child->IntAttribute("id");for(const auto& a:doc.arrows().records())if(a.isAlive()&&a.sourceId==source)group.members.push_back(ObjectRef(ObjectKind::Arrow,a.id));
            }
            if((onlyArrows||n->IntAttribute("MoleculeStudioArrowGroup",0))&&group.members.size()>1){ObjectId id;doc.addGroup(group,&id);}
        }joined(n);
    }};joined(root);
    studio_electron_graphics::importSymbols(doc);
    return Error();
}
