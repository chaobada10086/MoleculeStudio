from pathlib import Path
p=Path('studio/projection_templates.hpp');s=p.read_text(encoding='utf-8');s=s.replace('if(!label.empty()){e=nicknames::apply(s->doc,id,label);if(!e.ok())throw std::runtime_error(e.message);}', 'if(!label.empty()){auto element=chemistry::elementInfoBySymbol(label.c_str(),int(label.size()));if(element&&element->number){e=s->doc.setElement(id,element->number);}else e=nicknames::apply(s->doc,id,label);if(!e.ok())throw std::runtime_error(e.message);}')
p.write_text(s,encoding='utf-8')
p=Path('studio/chemistry_open.py');s=p.read_text(encoding='utf-8').replace("removeHs=not preserve_hydrogens))", "removeHs=not preserve_hydrogens and not any('MoleculeStudioProjection' in n.attrib for n in root.iter('n'))))");p.write_text(s,encoding='utf-8')
p=Path('studio/cdx_binary.hpp');s=p.read_text(encoding='utf-8');needle='parent->InsertEndChild(child);contents(r,child,depth+1);';assert needle in s
s=s.replace(needle,needle+'''if(tag==0x8011){auto label=child->Attribute("Name");std::string metadata=label?label:"";if(metadata.compare(0,22,"MoleculeStudioProjecti")==0){}auto equal=metadata.find('=');if(equal!=std::string::npos&&metadata.compare(0,24,"MoleculeStudioProjection")==0){parent->SetAttribute(metadata.substr(0,equal).c_str(),metadata.substr(equal+1).c_str());parent->DeleteChild(child);}}''')
s=s.replace('if(metadata.compare(0,22,"MoleculeStudioProjecti")==0){}','')
# Standard ObjectTag name carries the application-owned metadata; other software can preserve it without needing custom CDX properties.
needle='        for(auto* c=e->FirstChildElement();c;c=c->NextSiblingElement())if(objectTag(c->Name()))object(c,out,depth+1);';assert needle in s
s=s.replace(needle,'''        for(auto* a=e->FirstAttribute();a;a=a->Next())if(std::string(a->Name()).compare(0,24,"MoleculeStudioProjection")==0){put(out,0x8011,2);put(out,nextId++,4);field(out,8,std::string(a->Name())+"="+a->Value());put(out,0,2);}
'''+needle)
# Prefix lengths must match exactly.
s=s.replace('compare(0,24,"MoleculeStudioProjection")','compare(0,23,"MoleculeStudioProjection")')
p.write_text(s,encoding='utf-8')
p=Path('studio/projection_actions.hpp');s=p.read_text(encoding='utf-8').replace('// Empty templates contain real editable vector endpoints, not visible R labels.','// Projection terminals use the ordinary atom editor and molecular semantics.').replace('s.nearestAtom(point,s.view().deviceRadiusToDocument(12)/s.view().docPerDevice())','s.nearestAtom(point,12)')
a=s.index('ObjectId App::projectionAxis');b=s.index('void App::convertProjection',a)
s=s[:a]+'''ObjectId App::projectionAxis(Vec2 point){
    auto eligible=[&](const BondRecord& b){return b.order==BondOrder::Single&&s.doc.atoms().alive(b.beginAtom)->element==6&&s.doc.atoms().alive(b.endAtom)->element==6&&(projection_molecule::marked(s.doc,b.beginAtom)||projection_molecule::marked(s.doc,b.endAtom));};
    auto pick=s.pick(point);if(pick.kind==ObjectKind::Bond){auto b=s.doc.bonds().alive(pick.id);if(b&&eligible(*b))return b->id;}
    std::set<ObjectId> scope;auto refs=studioio::expanded(s.doc,s.selected);for(auto ref:refs)if(ref.kind==ObjectKind::Atom)scope.insert(ref.id);else if(ref.kind==ObjectKind::Bond){auto b=s.doc.bonds().alive(ref.id);if(b){scope.insert(b->beginAtom);scope.insert(b->endAtom);}}
    if(pick.kind==ObjectKind::Atom){for(auto id:projection_molecule::component(s.doc,pick.id))scope.insert(id);}
    ObjectId fallback=kInvalidObjectId;double distance=1e100;
    for(const auto& b:s.doc.bonds().records())if(b.isAlive()&&eligible(b)&&(scope.count(b.beginAtom)||scope.count(b.endAtom))){auto p=(s.doc.atoms().alive(b.beginAtom)->position+s.doc.atoms().alive(b.endAtom)->position)*.5;double score=length(p-point)+(projection_molecule::axis(s.doc,b)?0:10000);if(score<distance){distance=score;fallback=b.id;}}
    return fallback;
}
'''+s[b:];s=s.replace('kInvalidateGeometry|kInvalidateAppearance,[axis','kInvalidateStructure|kInvalidateGeometry|kInvalidateAppearance|kInvalidateSemantics,[axis');p.write_text(s,encoding='utf-8')
p=Path('studio/studio.cpp');s=p.read_text(encoding='utf-8');needle='            if(s.selected.empty())return 0;\n            auto refs=studioio::expanded(s.doc,s.selected);';assert needle in s
s=s.replace(needle,'''            if(s.selected.empty()){auto axis=a->projectionAxis(point);if(axis!=kInvalidObjectId){auto edge=s.doc.bonds().alive(axis);for(auto id:projection_molecule::component(s.doc,edge->beginAtom))s.selected.emplace_back(ObjectKind::Atom,id);}else return 0;}
            auto refs=studioio::expanded(s.doc,s.selected);''',1);p.write_text(s,encoding='utf-8')
