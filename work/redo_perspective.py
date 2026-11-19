from pathlib import Path
p=Path('studio/structure_perspective.hpp');s=p.read_text(encoding='utf-8');s=s.replace('auto v=d.extensions(ObjectRef(ObjectKind::Atom,id)).findValue("pst.attr.MoleculeStudioPerspectiveZ");','auto v=d.extensions(ObjectRef(ObjectKind::Atom,id)).findValue("studio.atom.z");if(!v)v=d.extensions(ObjectRef(ObjectKind::Atom,id)).findValue("pst.attr.MoleculeStudioPerspectiveZ");')
s=s.replace('.set("pst.attr.MoleculeStudioPerspectiveZ",out.str());','.set("studio.atom.z",out.str());d.extensions(ObjectRef(ObjectKind::Atom,id)).erase("pst.attr.MoleculeStudioPerspectiveZ");')
start=s.index('        auto& ext=d.extensions(ObjectRef(ObjectKind::Bond,b.id));');end=s.index('\n    }',start)
s=s[:start]+'''        auto& ext=d.extensions(ObjectRef(ObjectKind::Bond,b.id));ext.erase("pst.attr.MoleculeStudioPerspectiveFront");
        if((b.display==BondDisplay::Normal||b.display==BondDisplay::Bold)&&b.stereo==BondStereo::None&&cyclic(d,b)){
            bool front=hi-lo>d.defaultBondLength()*.03&&(depth(d,b.beginAtom)+depth(d,b.endAtom))*.5<lo+(hi-lo)*.38;
            d.setBondDisplay(b.id,front?BondDisplay::Bold:BondDisplay::Normal);
            if(front)ext.set("pst.attr.BoldWidth",std::to_string(d.defaultLineWidth()*2.22));
        }'''+s[end:];p.write_text(s,encoding='utf-8')
p=Path('studio/structure_perspective_ui.hpp');s=p.read_text(encoding='utf-8');start=s.index('    if(perspectiveAtoms.empty()){');end=s.index('    auto first=',start)
s=s[:start]+'''    // Like the native tool, the first gesture makes a selection. Rotation
    // starts only inside that selection; outside gestures remain marquee input.
    if(perspectiveAtoms.size()<3)return false;
    if(!studioio::inside(studioio::selectionBounds(s),s.view().toDocument(pixel),s.view().deviceRadiusToDocument(4)))return false;
'''+s[end:]
s=s.replace('Vec2 delta=(pixel-perspectivePress)*(1./scale);','Vec2 delta=(pixel-perspectivePress)*s.view().docPerDevice();')
s=s.replace('perspectiveYaw=std::max(-1.35,std::min(1.35,delta.x*.012));perspectivePitch=std::max(-1.35,std::min(1.35,delta.y*.012));','perspectiveYaw=delta.x*.024;perspectivePitch=delta.y*.024;')
s=s.replace('transformHandle=8;SetFocus','transformHandle=-1;SetFocus');p.write_text(s,encoding='utf-8')
p=Path('studio/studio.cpp');s=p.read_text(encoding='utf-8').replace('if(perspectiveTool)return 32649;','if(perspectiveTool)return perspectiveDragging||(!s.selected.empty()&&studioio::inside(studioio::selectionBounds(s),s.view().toDocument(pixel)))?32649:32512;');s=s.replace('return id==32649?rotationPointer','if(perspectiveTool)return LoadCursorW(0,MAKEINTRESOURCEW(id));return id==32649?rotationPointer');s=s.replace('bool hot=!transformDragging&&canvasCursorId','bool hot=!perspectiveTool&&!transformDragging&&canvasCursorId');p.write_text(s,encoding='utf-8')
p=Path('studio/engine/session.cpp');s=p.read_text(encoding='utf-8');start=s.index('        auto data=rings.build(b);');end=s.index('        ps.insert(ps.end(),bondPrimitives.begin()',start)
s=s[:start]+'''        // Standard CDXML Bold doubles retain their inner pi stroke.
        auto plain=b;if(plain.display==BondDisplay::Bold)plain.display=BondDisplay::Normal;
        auto data=rings.build(plain);
        molecular::clip(data,b,bondLabels,molecular::setting(doc,"MarginWidth",1.6));
        clipBridgeCrossing(doc,b,data);
        auto widthOf=[&](const BondRecord& edge){const auto& bag=static_cast<const Document&>(doc).extensions(ObjectRef(ObjectKind::Bond,edge.id));auto value=bag.findValue("pst.attr.BoldWidth");return edge.display==BondDisplay::Bold?(value?std::atof(value->c_str()):molecular::setting(doc,"BoldWidth",2)):doc.defaultLineWidth();};
        if(b.display==BondDisplay::Bold&&!data.shapes.empty())data.shapes.front().strokeWidth=widthOf(b);
        bonds::RenderAdapterOptions opts;opts.style.width=opts.viewDefaultWidth=doc.defaultLineWidth();
        if(auto c=static_cast<const Document&>(doc).extensions(ObjectRef(ObjectKind::Bond,b.id)).findValue("studio.color"))opts.color=uint32_t(std::strtoull(c->c_str(),0,10));
        std::vector<render::Primitive> bondPrimitives;
        bonds::toPrimitives(data,ObjectRef(ObjectKind::Bond,b.id),opts,&bondPrimitives);
        if(b.display==BondDisplay::Bold&&!bondPrimitives.empty()&&bondPrimitives.front().points.size()==2){
            auto& ink=bondPrimitives.front();Vec2 p=ink.points[0],q=ink.points[1];double half=widthOf(b)*.5;
            auto offset=[&](ObjectId id,ObjectId other,Vec2 origin,Vec2 toward){Vec2 u=toward-origin;double len=length(u);if(len<1e-8)return Vec2();u=u*(1/len);Vec2 n(-u.y,u.x),answer=n*half;
                for(auto bid:doc.incidentBonds(id)){const auto* edge=doc.bonds().alive(bid);if(edge->id==b.id||edge->display!=BondDisplay::Bold)continue;auto adjacent=edge->beginAtom==id?edge->endAtom:edge->beginAtom;Vec2 v=doc.atoms().alive(adjacent)->position-doc.atoms().alive(id)->position;double vl=length(v);if(vl<1e-8)continue;v=v*(1/vl);Vec2 m(-v.y,v.x);double det=n.x*m.y-n.y*m.x;if(std::abs(det)<.05)continue;double h2=-widthOf(*edge)*.5;Vec2 join((half*m.y-n.y*h2)/det,(n.x*h2-half*m.x)/det);if(length(join)<half*4)answer=join;break;}return answer;};
            auto l=offset(b.beginAtom,b.endAtom,p,q),r=offset(b.endAtom,b.beginAtom,q,p);
            ink.kind=render::PrimKind::Polygon;ink.points={p+l,q-r,q+r,p-l};ink.style.fill=ink.style.stroke;ink.style.stroke.enabled=false;ink.bounds=render::geom::computeBounds(ink,.6);
        }
'''+s[end:];p.write_text(s,encoding='utf-8')
