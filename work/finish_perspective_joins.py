from pathlib import Path
p=Path('studio/engine/session.cpp');s=p.read_text(encoding='utf-8');needle='        bonds::toPrimitives(data,ObjectRef(ObjectKind::Bond,b.id),opts,&bondPrimitives);';s=s.replace(needle,needle+'''
        if(static_cast<const Document&>(doc).extensions(ObjectRef(ObjectKind::Bond,b.id)).findValue("pst.attr.MoleculeStudioPerspectiveFront")&&b.display==BondDisplay::Normal&&b.stereo==BondStereo::None&&!bondPrimitives.empty())bondPrimitives.front().style.cap=render::CapStyle::Round;''');p.write_text(s,encoding='utf-8')
