from pathlib import Path
p=Path('studio/structure_perspective_ui.hpp');s=p.read_text(encoding='utf-8').replace('perspectiveYaw=delta.x*.024;perspectivePitch=delta.y*.024;','perspectiveYaw=-delta.x*.020;perspectivePitch=delta.y*.020;');p.write_text(s,encoding='utf-8')
p=Path('studio/structure_perspective_checks.hpp');s=p.read_text(encoding='utf-8').replace('1.05/(.024*','1.05/(.020*');p.write_text(s,encoding='utf-8')
p=Path('studio/selection_transform.hpp');s=p.read_text(encoding='utf-8').replace('bool App::beginSelectionTransform(Vec2 pixel,uint32_t modifiers){','bool App::beginSelectionTransform(Vec2 pixel,uint32_t modifiers){\n    if(perspectiveTool)return false;');p.write_text(s,encoding='utf-8')
p=Path('studio/engine/session.cpp');s=p.read_text(encoding='utf-8');s=s.replace('for(const auto& b:doc.bonds().records())if(b.isAlive()&&wanted(ObjectKind::Bond,b.id)){','''for(const auto& storedBond:doc.bonds().records())if(storedBond.isAlive()&&wanted(ObjectKind::Bond,storedBond.id)){
        auto b=storedBond;
        auto isBold=[&](const BondRecord& edge){return edge.display==BondDisplay::Bold||static_cast<const Document&>(doc).extensions(ObjectRef(ObjectKind::Bond,edge.id)).findValue("pst.attr.MoleculeStudioPerspectiveFront");};
        if(b.display==BondDisplay::Normal&&isBold(b))b.display=BondDisplay::Bold;''');s=s.replace('return edge.display==BondDisplay::Bold?(value?', 'return isBold(edge)?(value?');s=s.replace('molecular::setting(doc,"BoldWidth",2)):doc.defaultLineWidth();','(bag.findValue("pst.attr.MoleculeStudioPerspectiveFront")?doc.defaultLineWidth()*2.22:molecular::setting(doc,"BoldWidth",2))):doc.defaultLineWidth();');s=s.replace('edge->display!=BondDisplay::Bold','!isBold(*edge)');p.write_text(s,encoding='utf-8')
p=Path('studio/engine/cdxml_writer.cpp');s=p.read_text(encoding='utf-8');s=s.replace('    std::set<std::string> written;','    std::set<std::string> written;written.insert("MoleculeStudioPerspectiveFront");written.insert("MoleculeStudioPerspectiveZ");',1);s=s.replace('if(auto z=bag.findValue("studio.atom.z"))custom("xyz",','auto z=bag.findValue("studio.atom.z");if(!z)z=bag.findValue("pst.attr.MoleculeStudioPerspectiveZ");if(z)custom("xyz",');s=s.replace('    if (bond.display != core::BondDisplay::Normal)\n        attribute(out, "Display", displayToCdxml(bond.display));','''    auto display=bond.display;
    const auto& bondBag=writer.document.extensions(ObjectRef(ObjectKind::Bond,bond.id));
    if(display==core::BondDisplay::Normal&&bondBag.findValue("pst.attr.MoleculeStudioPerspectiveFront")){
        display=core::BondDisplay::Bold;
        if(!bondBag.findValue("pst.attr.BoldWidth"))attributeNumber(out,"BoldWidth",writer.document.defaultLineWidth()*2.22);
    }
    if (display != core::BondDisplay::Normal)attribute(out, "Display", displayToCdxml(display));''');p.write_text(s,encoding='utf-8')
