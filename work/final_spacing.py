from pathlib import Path
p=Path('studio/engine/ring_geometry.cpp');s=p.read_text(encoding='utf-8');needle='    const auto* first=doc.atoms().alive(b.beginAtom);const auto* last=doc.atoms().alive(b.endAtom);';s=s.replace(needle,needle+'''
    auto hasDepth=[&](ObjectId id){const auto& bag=doc.extensions(ObjectRef(ObjectKind::Atom,id));return bag.findValue("studio.atom.z")||bag.findValue("pst.attr.MoleculeStudioPerspectiveZ");};
    if((hasDepth(b.beginAtom)||hasDepth(b.endAtom))&&length(z-a)>1e-8)opts.spacingPercent*=doc.defaultBondLength()/length(z-a);
''');p.write_text(s,encoding='utf-8')
p=Path('studio/interop.hpp');s=p.read_text(encoding='utf-8').replace('    root->SetAttribute("CreationProgram","Molecule Studio");','    root->SetAttribute("CreationProgram","Molecule Studio");\n    if(!root->Attribute("BondSpacing"))root->SetAttribute("BondSpacing",18);');p.write_text(s,encoding='utf-8')
