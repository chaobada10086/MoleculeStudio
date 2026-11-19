from pathlib import Path
p=Path('studio/engine/projection_export.hpp');s=p.read_text(encoding='utf-8')
s=s.replace('else{if(e->Attribute("MoleculeStudioProjectionHidden"))', 'else{if(e->Attribute("MoleculeStudioProjectionWarning")){e->DeleteAttribute("IgnoreWarnings");e->DeleteAttribute("MoleculeStudioProjectionWarning");}if(e->Attribute("MoleculeStudioProjectionHidden"))')
needle='    for(auto pair:nodes){auto n=pair.second;';assert needle in s
s=s.replace(needle,'''    auto ignoreOverlap=[](tinyxml2::XMLElement* e){if(!e->Attribute("IgnoreWarnings")){e->SetAttribute("IgnoreWarnings","yes");e->SetAttribute("MoleculeStudioProjectionWarning","1");}};
'''+needle)
s=s.replace('bool rear=n->IntAttribute("MoleculeStudioProjection")==21;', 'if(n->IntAttribute("Element",6)==6)ignoreOverlap(n);bool rear=n->IntAttribute("MoleculeStudioProjection")==21;')
s=s.replace('if(other->IntAttribute("MoleculeStudioProjection",-1)/10==2)continue;', 'if(other->IntAttribute("MoleculeStudioProjection",-1)/10==2){ignoreOverlap(b);continue;}')
p.write_text(s,encoding='utf-8')
