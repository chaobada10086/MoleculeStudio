from pathlib import Path
p=Path('studio/engine/projection_export.hpp');s=p.read_text(encoding='utf-8');s=s.replace('if(bytes.find("MoleculeStudioProjectionInk")==std::string::npos&&bytes.find("MoleculeStudioProjectionHidden")==std::string::npos)return bytes;','if(bytes.find("MoleculeStudioProjection")==std::string::npos)return bytes;')
needle='    std::function<void(tinyxml2::XMLElement*)> walk=';assert needle in s
s=s.replace(needle,'''    // ChemDraw retains standard object tags while discarding private attributes.
    // Consume those tags before normal parsing (native files may reuse tag IDs).
    std::function<void(tinyxml2::XMLElement*)> tags=[&](tinyxml2::XMLElement* parent){for(auto e=parent->FirstChildElement();e;){auto next=e->NextSiblingElement();std::string label=e->Attribute("Name")?e->Attribute("Name"):"";auto equal=label.find('=');if(std::string(e->Name())=="objecttag"&&label.compare(0,24,"MoleculeStudioProjection")==0&&equal!=std::string::npos){parent->SetAttribute(label.substr(0,equal).c_str(),label.substr(equal+1).c_str());parent->DeleteChild(e);}else tags(e);e=next;}};tags(xml.RootElement());
'''+needle,1)
needle='    tinyxml2::XMLPrinter out;xml.Print(&out);return out.CStr();\n}\n}';assert needle in s
s=s.replace(needle,'''    std::function<void(tinyxml2::XMLElement*)> metadata=[&](tinyxml2::XMLElement* e){for(auto a=e->FirstAttribute();a;a=a->Next())if(std::string(a->Name()).compare(0,24,"MoleculeStudioProjection")==0){auto tag=xml.NewElement("objecttag");tag->SetAttribute("id",next++);tag->SetAttribute("Name",(std::string(a->Name())+"="+a->Value()).c_str());e->InsertEndChild(tag);}for(auto c=e->FirstChildElement();c;c=c->NextSiblingElement())metadata(c);};metadata(root);
    tinyxml2::XMLPrinter out;xml.Print(&out);return out.CStr();
}
}''')
p.write_text(s,encoding='utf-8')
p=Path('studio/cdx_binary.hpp');s=p.read_text(encoding='utf-8');needle='for(auto* c=e->FirstChildElement();c;c=c->NextSiblingElement())if(objectTag(c->Name()))object(c,out,depth+1);';assert needle in s
s=s.replace(needle,'''for(auto* c=e->FirstChildElement();c;c=c->NextSiblingElement())if(objectTag(c->Name())){std::string label=c->Attribute("Name")?c->Attribute("Name"):"";auto equal=label.find('=');if(std::string(c->Name())=="objecttag"&&label.compare(0,24,"MoleculeStudioProjection")==0&&equal!=std::string::npos&&e->Attribute(label.substr(0,equal).c_str()))continue;object(c,out,depth+1);}''');p.write_text(s,encoding='utf-8')
p=Path('studio/projection_checks.hpp');s=p.read_text(encoding='utf-8');needle='        for(const auto& bytes:{xml,decoded})';assert needle in s
s=s.replace(needle,'''        tinyxml2::XMLDocument nativeTree;nativeTree.Parse(xml.c_str());std::function<void(tinyxml2::XMLElement*)> nativeSave=[&](tinyxml2::XMLElement* e){std::vector<std::string> keys;for(auto a=e->FirstAttribute();a;a=a->Next())if(std::string(a->Name()).compare(0,24,"MoleculeStudioProjection")==0)keys.push_back(a->Name());for(auto key:keys)e->DeleteAttribute(key.c_str());for(auto c=e->FirstChildElement();c;c=c->NextSiblingElement())nativeSave(c);};nativeSave(nativeTree.RootElement());tinyxml2::XMLPrinter nativePrinter;nativeTree.Print(&nativePrinter);std::string native=nativePrinter.CStr();
        for(const auto& bytes:{xml,decoded,native})''')
needle='    int variants=0,conversions=0;';assert needle in s
s=s.replace(needle,'''    if(const char* path=std::getenv("MOLECULE_STUDIO_PROJECTION_NATIVE_SOURCE")){Session native;require(native.open(path).ok(),"actual ChemDraw projection reopen");int cores=0,paths=0,arrows=0;for(const auto& a:native.doc.atoms().records())if(a.isAlive())cores+=projection_molecule::kind(native.doc,a.id)==2;for(const auto& p:native.doc.paths().records())paths+=p.isAlive();for(const auto& a:native.doc.arrows().records())arrows+=a.isAlive();auto formula=studio::analyze(native.doc,native.all());require(cores==2&&paths==0&&arrows==0&&formula.valid&&formula.plain=="C8H18O","ChemDraw object tags restore projection without duplicate ink");roundtrip(native.doc,"after-chemdraw");}
'''+needle);p.write_text(s,encoding='utf-8')
