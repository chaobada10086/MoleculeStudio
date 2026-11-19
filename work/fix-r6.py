from pathlib import Path
p=Path('studio/studio.cpp');s=p.read_text(encoding='utf-8');s=s.replace('bool editProjectionEndpoint(Vec2 point);','bool editProjectionEndpoint(Vec2 point,bool probe=false,Vec2* capture=nullptr);bool projectionCapture(Vec2 pixel,Vec2* capture=nullptr);void appendProjectionCapture(render::OverlayScene& overlay);bool projectionWasHot=false;',1)
s=s.replace('if(selection){int hit=selectionHandleAt(pixel);','if(projectionCapture(pixel))return 32513;if(selection){int hit=selectionHandleAt(pixel);',1)
s=s.replace('appendElectronMarkerSelection(o);','appendElectronMarkerSelection(o);appendProjectionCapture(o);',1).replace('appendElectronMarkerSelection(overlay);render::RenderStats','appendElectronMarkerSelection(overlay);appendProjectionCapture(overlay);render::RenderStats',1)
s=s.replace('a->updateCanvasCursor();a->invalidate(a->pan||a->down||changed);return 0;', 'bool projectionHot=a->projectionCapture(pixel);changed=changed||projectionHot||a->projectionWasHot;a->projectionWasHot=projectionHot;a->updateCanvasCursor();a->invalidate(a->pan||a->down||changed);return 0;',1)
p.write_text(s,encoding='utf-8')
p=Path('studio/projection_actions.hpp');s=p.read_text(encoding='utf-8').replace('bool App::editProjectionEndpoint(Vec2 point){','bool App::editProjectionEndpoint(Vec2 point,bool probe,Vec2* capture){',1)
s=s.replace('{openText(ref.id);return true;}','{if(capture)*capture=(box.min+box.max)*.5;if(!probe)openText(ref.id);return true;}',1)
s=s.replace('    // Editing an occupied endpoint', '    if(capture)*capture=endpoint;if(probe)return true;\n    // Editing an occupied endpoint',1)
s+='''
// Use the editing resolver for capture feedback: preview and click cannot disagree.
bool App::projectionCapture(Vec2 pixel,Vec2* capture){
    if(!pointerOverCanvas||textEditor.active||down||pan||cloneDragging||transformDragging||wrapDragging||tableDragPending||perspectiveTool||bioPlacing||ringPlacing||patternPlacing||radicalPlacing||virtualBondTool||phenalenePlacing||equivalencePlacement||partialChargePlacement)return false;
    const auto& tool=s.entries[s.activeIndex];
    if(tool.command.find("Eraser")!=std::string::npos)return false;
    return editProjectionEndpoint(s.view().toDocument(pixel),true,capture);
}
void App::appendProjectionCapture(render::OverlayScene& overlay){
    Vec2 point;if(!projectionCapture(pointerPixel,&point))return;
    render::Primitive ring;ring.kind=render::PrimKind::Polyline;ring.style.stroke=render::Paint(0x2563EBFF,true);ring.style.fill.enabled=false;ring.style.strokeWidth=s.view().deviceRadiusToDocument(2.5);ring.layer=render::LayerId(900);ring.z=100;
    double radius=s.view().deviceRadiusToDocument(9);
    for(int i=0;i<=40;++i)ring.points.push_back(point+Vec2(std::cos(i*6.283185307/40),std::sin(i*6.283185307/40))*radius);
    ring.bounds=render::geom::computeBounds(ring,.6);auto halo=ring;halo.style.stroke.rgba=0xFFFFFFFF;halo.style.strokeWidth=s.view().deviceRadiusToDocument(5);halo.bounds=render::geom::computeBounds(halo,.6);overlay.scene().add(halo);overlay.scene().add(ring);
}
''';p.write_text(s,encoding='utf-8')
p=Path('studio/tlc_cdxml_writer.inc');s=p.read_text(encoding='utf-8').replace('attribute(out,"ShowBorders",figure.showLaneBorders?"yes":"no");','// CDX ShowBorders controls the outer plate frame, not internal lane guides.\n        attribute(out,"ShowBorders","yes");',1);p.write_text(s,encoding='utf-8')
p=Path('studio/engine/cdxml_reader.cpp');s=p.read_text(encoding='utf-8').replace('figure.showLaneBorders=yes("ShowBorders",false);','figure.showLaneBorders=false; // Native ShowBorders is the outer frame, not lane guides.',1);p.write_text(s,encoding='utf-8')
p=Path('studio/projection_checks.hpp');s=p.read_text(encoding='utf-8');needle='        app.s.activateCommand("chooseEdittingTool_Text");\n        click(tip,false);';new='''        auto captureDigest=app.s.doc.digest();auto captureView=app.s.view();
        for(double zoom:{1.,2.,4.}){auto view=captureView;view.scale=zoom;app.s.setView(view);auto pixel=app.s.view().toDevice(tip)+Vec2(7,0);studio_i18n::sendMessage(app.canvas,WM_MOUSEMOVE,0,MAKELPARAM(int(std::lround(pixel.x)),int(std::lround(pixel.y))));Vec2 captured;
            require(app.projectionCapture(app.pointerPixel,&captured)&&length(captured-tip)<.001,"projection endpoint captures within device radius at every zoom");
            require(app.canvasCursorId(app.pointerPixel)==32513,"captured endpoint has editing cursor");render::OverlayScene overlay;app.appendProjectionCapture(overlay);require(overlay.scene().size()==2,"capture ring and halo are rendered");
            require(!app.projectionCapture(app.s.view().toDevice(Vec2(10000,10000))),"far pointer releases capture");
        }
        require(app.s.doc.digest()==captureDigest&&!app.textEditor.active,"hover capture never edits the document");
        studio_i18n::sendMessage(app.canvas,WM_MOUSELEAVE,0,0);render::OverlayScene gone;app.appendProjectionCapture(gone);require(gone.scene().size()==0,"leaving canvas clears capture");
        app.s.setView(captureView);
        app.s.activateCommand("chooseEdittingTool_Text");
        click(tip,false);''';assert needle in s;s=s.replace(needle,new,1);p.write_text(s,encoding='utf-8')
p=Path('studio/tlc_checks.inc');s=p.read_text(encoding='utf-8');needle='    s.selected.clear();auto saved=s.save(dir+"/tlc.r62doc",false);';new='''    // Lane guides must never control ChemDraw's outer TLC border.
    for(bool guides:{false,true}){auto checkFigure=candidate;checkFigure.showLaneBorders=guides;require(writeFigure(s.doc,plate,checkFigure).ok(),"set lane guide fixture");s.refresh();std::string xml;require(studioio::exportXml(s.doc,&xml).ok(),"export TLC border fixture");tinyxml2::XMLDocument tree;tree.Parse(xml.c_str());auto node=tree.RootElement()->FirstChildElement("page")->FirstChildElement("tlcplate");require(node&&node->Attribute("ShowBorders")&&std::string(node->Attribute("ShowBorders"))=="yes","native outer border enabled independently of lane guides");std::string bytes,decoded,why;require(cdxbinary::fromXml(xml,bytes,why)&&cdxbinary::toXml(bytes,decoded,why)&&decoded.find("ShowBorders=\\"yes\\"")!=std::string::npos,"CDX retains outer TLC border");std::ofstream(dir+(guides?"/tlc-guides-on.cdxml":"/tlc-guides-off.cdxml"))<<xml;std::ofstream(dir+(guides?"/tlc-guides-on.cdx":"/tlc-guides-off.cdx"),std::ios::binary).write(bytes.data(),bytes.size());}
    require(writeFigure(s.doc,plate,candidate).ok(),"restore TLC fixture");s.refresh();
'''+needle;assert needle in s;s=s.replace(needle,new,1);p.write_text(s,encoding='utf-8')
