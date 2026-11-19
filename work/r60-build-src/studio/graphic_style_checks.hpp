#pragma once
void App::graphicStyleCheck(const std::string& dir){
    auto require=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
    s.dirty=false;execute(Button("","新建","","@new"));s.graphicsWidth=0;s.graphicsColor=0x000000FFu;
    auto draw=[&](const char* command,Vec2 from,Vec2 to){s.selected.clear();require(s.activateCommand(command),"activate graphics fixture tool");require(s.gesture(PointerPhase::Down,from).ok(),"graphics pointer down");require(s.gesture(PointerPhase::Move,to).ok(),"graphics pointer move");require(s.gesture(PointerPhase::Up,to).ok(),"graphics pointer up");};
    auto checkPaint=[&](Session& session,ObjectRef ref,double width,uint32_t color){bool found=false;for(const auto& p:session.scene.primitives())if(p.source==ref&&p.style.stroke.enabled){found=true;if(std::abs(p.style.strokeWidth-width)>=1e-7){std::ostringstream message;message<<"stroke width "<<int(ref.kind)<<":"<<ref.id<<" expected "<<width<<" got "<<p.style.strokeWidth<<" in "<<session.sourcePath;throw std::runtime_error(message.str());}require(render::resolvePaint(p.style.stroke.rgba)==color,"graphic stroke has chosen color");}require(found,"fixture has visible stroke");};
    draw("chooseDrawingTool_Rectangle_Hollow",Vec2(0,0),Vec2(60,40));ObjectRef rectangle(ObjectKind::Arrow,ObjectId(s.doc.arrows().size()-1));checkPaint(s,rectangle,.6,0x000000FFu);
    s.selected.clear();require(applyGraphicsStyle(2,0x0046DCFFu,true,true).ok(),"set shared future style");checkPaint(s,rectangle,.6,0x000000FFu);
    draw("chooseDrawingTool_Circle_Hollow",Vec2(80,0),Vec2(130,50));ObjectRef circle(ObjectKind::Arrow,ObjectId(s.doc.arrows().size()-1));checkPaint(s,circle,2,0x0046DCFFu);
    draw("chooseArrowTool_Large_Retrosynthetic",Vec2(0,80),Vec2(100,80));ObjectRef arrow(ObjectKind::Arrow,ObjectId(s.doc.arrows().size()-1));checkPaint(s,arrow,2,0x0046DCFFu);
    arrows::ArrowParams params;bool ok=false;require(arrows::readArrow(s.doc,arrow.id,&params,&ok).ok()&&ok&&std::abs(arrows::arrowStrokeWidth(params,.6)-2)<1e-7,"arrow geometry and committed width agree");
    auto head=params.head;auto begin=params.begin,end=params.end;
    draw("choosePenTool_DrawCurve",Vec2(0,120),Vec2(100,140));ObjectRef curve(ObjectKind::Path,ObjectId(s.doc.paths().size()-1));checkPaint(s,curve,2,0x0046DCFFu);
    draw("chooseBracketTool_SquareBrackets",Vec2(160,0),Vec2(210,60));ObjectRef bracket(ObjectKind::Bracket,ObjectId(s.doc.brackets().size()-1));checkPaint(s,bracket,2,0x0046DCFFu);
    draw("chooseTableTool",Vec2(240,0),Vec2(300,60));ObjectRef table(ObjectKind::Table,ObjectId(s.doc.tables().size()-1));checkPaint(s,table,2,0x0046DCFFu);
    draw("chooseTLCPlateTool",Vec2(320,0),Vec2(380,60));ObjectRef plate(ObjectKind::Symbol,ObjectId(s.doc.symbols().size()-1));checkPaint(s,plate,2,0x0046DCFFu);
    ObjectId atom;s.doc.addAtom(Vec2(180,100),8,&atom);s.refresh();
    s.selected={rectangle,circle,arrow,curve,bracket,table,plate,ObjectRef(ObjectKind::Atom,atom)};auto before=s.doc.digest();
    require(applyGraphicsStyle(1.5,0xDC1E1EFFu,true,true).ok(),"mixed graphics shared style");auto after=s.doc.digest();for(auto ref:{rectangle,circle,arrow,curve,bracket,table,plate})checkPaint(s,ref,1.5,0xDC1E1EFFu);
    require(*static_cast<const Document&>(s.doc).extensions(ObjectRef(ObjectKind::Atom,atom)).findValue("studio.color.override")==std::to_string(0xDC1E1EFFu),"mixed selection recolors chemical labels too");
    require(arrows::readArrow(s.doc,arrow.id,&params,&ok).ok()&&params.begin==begin&&params.end==end&&params.head.length==head.length&&params.head.width==head.width,"line width does not resize arrow geometry or head");
    require(s.undo().ok()&&s.doc.digest()==before,"graphics style is one undo step");require(s.undo(true).ok()&&s.doc.digest()==after,"graphics style redo");
    require(s.save(dir+"/graphics-style.r62doc").ok(),"save graphics style native");Session native;require(native.open(dir+"/graphics-style.r62doc").ok(),"reload graphics native");for(auto ref:{rectangle,circle,arrow,curve,bracket,table,plate})checkPaint(native,ref,1.5,0xDC1E1EFFu);
    require(s.save(dir+"/graphics-style.cdxml",true).ok(),"export graphics CDXML");Session xml;require(xml.open(dir+"/graphics-style.cdxml").ok(),"reload graphics CDXML");bool exportedArrow=false;for(const auto& record:xml.doc.arrows().records())if(record.isAlive()){bool rich=false;arrows::ArrowParams p;if(arrows::readArrow(xml.doc,record.id,&p,&rich).ok()&&rich&&p.head.shape==arrows::ArrowHeadShape::HalfArrow){require(std::abs(arrows::arrowStrokeWidth(p,.6)-1.5)<1e-7,"CDXML retains explicit arrow width");exportedArrow=true;}}require(exportedArrow,"CDXML retains custom arrow style");
    // Chemical width comes from drawing settings; graphics color remains shared.
    s.dirty=false;execute(Button("","新建","","@new"));s.graphicsWidth=0;s.graphicsColor=0x000000FFu;
    std::vector<ObjectRef> chemical;std::vector<ObjectId> chemicalBonds;
    for(int i=0;i<7;++i){ObjectId a,b,id;Vec2 origin(15+(i%4)*75,20+(i/4)*55);require(s.doc.addAtom(origin,8,&a).ok()&&s.doc.addAtom(origin+Vec2(40,0),7,&b).ok(),"chemical fixture atoms");require(s.doc.addBond(a,b,&id).ok(),"chemical fixture bond");if(i==1)s.doc.setBondOrder(id,BondOrder::Double);if(i==2)s.doc.setBondOrder(id,BondOrder::Triple);if(i==3)s.doc.setBondDisplay(id,BondDisplay::Dashed);if(i==4)s.doc.setBondDisplay(id,BondDisplay::Bold);if(i==5){s.doc.setBondDisplay(id,BondDisplay::Wedge);s.doc.setBondStereo(id,BondStereo::Up);}if(i==6){s.doc.setBondDisplay(id,BondDisplay::WedgeHash);s.doc.setBondStereo(id,BondStereo::Down);}chemical.push_back(ObjectRef(ObjectKind::Bond,id));chemicalBonds.push_back(id);}
    ObjectId caption;require(s.doc.addText(Vec2(15,145),12,"Structure color",&caption).ok(),"chemical caption");chemical.push_back(ObjectRef(ObjectKind::Text,caption));
    ObjectId a,b,unselected;require(s.doc.addAtom(Vec2(240,140),6,&a).ok()&&s.doc.addAtom(Vec2(280,140),6,&b).ok()&&s.doc.addBond(a,b,&unselected).ok(),"unselected chemical fixture");s.refresh();s.selected=chemical;before=s.doc.digest();
    require(applyGraphicsStyle(1.5,0x0046DCFFu,true,true).ok(),"apply color while preserving molecular widths");after=s.doc.digest();
    auto chemicalPaint=[&](Session& session){for(size_t i=0;i<5;++i)checkPaint(session,ObjectRef(ObjectKind::Bond,chemicalBonds[i]),i==4?2.:.6,0x0046DCFFu);for(size_t i=5;i<7;++i){bool filled=false;for(const auto& p:session.scene.primitives())if(p.source==ObjectRef(ObjectKind::Bond,chemicalBonds[i])&&(p.style.fill.enabled||p.style.stroke.enabled)){filled=true;require(render::resolvePaint(p.style.fill.enabled?p.style.fill.rgba:p.style.stroke.rgba)==0x0046DCFFu,"stereo wedges have chosen color");}require(filled,"stereo wedge visible");r62::integration::RingGeometry geometry(session.doc);auto data=geometry.build(*session.doc.bonds().get(chemicalBonds[i]));Box bounds;for(const auto& shape:data.shapes){for(auto point:shape.outline)bounds.add(point);for(auto point:shape.points)bounds.add(point);}require(!bounds.empty()&&bounds.max.y-bounds.min.y>0,"stereo wedge remains visible");}checkPaint(session,ObjectRef(ObjectKind::Bond,unselected),.6,0x000000FFu);bool atom=false,text=false;for(const auto& p:session.scene.primitives())if(p.source.kind==ObjectKind::Atom||p.source==ObjectRef(ObjectKind::Text,caption)){if(p.style.fill.enabled){require(render::resolvePaint(p.style.fill.rgba)==0x0046DCFFu,"molecular label and caption use chosen color");atom|=p.source.kind==ObjectKind::Atom;text|=p.source.kind==ObjectKind::Text;}}require(atom&&text,"molecular labels and caption visible");require(session.doc.bonds().get(chemicalBonds[1])->order==BondOrder::Double&&session.doc.bonds().get(chemicalBonds[5])->stereo==BondStereo::Up&&session.doc.bonds().get(chemicalBonds[6])->stereo==BondStereo::Down,"appearance preserves chemical semantics");};
    chemicalPaint(s);require(s.undo().ok()&&s.doc.digest()==before,"molecular appearance single undo");require(s.undo(true).ok()&&s.doc.digest()==after,"molecular appearance redo");chemicalPaint(s);
    chemicalPaint(s);
    before=s.doc.digest();s.dirty=false;
    require(applyGraphicsStyle(4,s.graphicsColor,true,false).ok(),"change graphic width with only chemistry selected");
    require(s.doc.digest()==before&&!s.dirty,"width-only chemical selection leaves document unchanged");chemicalPaint(s);
    // Old native documents may contain a graphics override; it must no longer affect chemical rendering or export.
    for(auto id:chemicalBonds)s.doc.extensions(ObjectRef(ObjectKind::Bond,id)).set("studio.graphics.width","4");
    s.refresh();chemicalPaint(s);
    require(s.save(dir+"/chemical-style.r62doc").ok(),"save molecular native");Session chemicalNative;require(chemicalNative.open(dir+"/chemical-style.r62doc").ok(),"reload molecular native");chemicalPaint(chemicalNative);
    require(s.save(dir+"/chemical-style.cdxml",true).ok(),"save molecular CDXML");Session chemicalXml;require(chemicalXml.open(dir+"/chemical-style.cdxml").ok(),"reload molecular CDXML");chemicalPaint(chemicalXml);
    // The real inline editor must inherit overrides when reopening a label.
    textEditor.begin(kInvalidObjectId,s.doc.bonds().get(chemicalBonds[0])->beginAtom,Vec2());require(textEditor.format.colorRGBA==0x0046DCFFu,"inline atom edit inherits chosen color");textEditor.finish(false);
    s.selected.clear();require(applyGraphicsStyle(.75,0x008C46FFu,true,true).ok(),"new chemical drawing defaults");
    size_t firstBond=s.doc.bonds().size(),firstAtom=s.doc.atoms().size();auto oldAtomColor=*static_cast<const Document&>(s.doc).extensions(ObjectRef(ObjectKind::Atom,0)).findValue("studio.color.override");
    draw("chooseBondTool_Solid",Vec2(350,100),Vec2(385,100));checkPaint(s,ObjectRef(ObjectKind::Bond,ObjectId(firstBond)),.6,0x008C46FFu);
    require(*static_cast<const Document&>(s.doc).extensions(ObjectRef(ObjectKind::Atom,ObjectId(firstAtom))).findValue("studio.color.override")==std::to_string(0x008C46FFu),"new atoms inherit color");
    firstBond=s.doc.bonds().size();draw("chooseRingTool_Benzene",Vec2(350,180),Vec2(380,180));require(s.doc.bonds().size()>firstBond,"ring fixture created");for(size_t i=firstBond;i<s.doc.bonds().size();++i)checkPaint(s,ObjectRef(ObjectKind::Bond,ObjectId(i)),.6,0x008C46FFu);
    firstBond=s.doc.bonds().size();draw("chooseAlkaneTool_Acyclic_Chain",Vec2(15,230),Vec2(130,230));require(s.doc.bonds().size()>firstBond,"chain fixture created");for(size_t i=firstBond;i<s.doc.bonds().size();++i)checkPaint(s,ObjectRef(ObjectKind::Bond,ObjectId(i)),.6,0x008C46FFu);
    require(*static_cast<const Document&>(s.doc).extensions(ObjectRef(ObjectKind::Atom,0)).findValue("studio.color.override")==oldAtomColor,"future format preserves existing structure");
    draw("chooseOrbitalTool_s_Open",Vec2(170,230),Vec2(200,260));ObjectRef orbital(ObjectKind::Orbital,ObjectId(s.doc.orbitals().size()-1));checkPaint(s,orbital,.75,0x008C46FFu);
    draw("chooseBioTool_Receptor",Vec2(240,230),Vec2(280,260));ObjectRef bio(ObjectKind::BioShape,ObjectId(s.doc.bioShapes().size()-1));checkPaint(s,bio,.75,0x008C46FFu);
    s.selected={orbital,bio};require(applyGraphicsStyle(1.,0xE68200FFu,true,true).ok(),"selected orbital and bio format");checkPaint(s,orbital,1.,0xE68200FFu);checkPaint(s,bio,1.,0xE68200FFu);
    s.selected.clear();require(applyGraphicsStyle(.75,0x008C46FFu,true,true).ok(),"restore future green");ObjectId nextText=ObjectId(s.doc.texts().size());textEditor.begin(kInvalidObjectId,kInvalidObjectId,Vec2(15,295));require(textEditor.format.colorRGBA==0x008C46FFu,"new inline text inherits current color");studio_i18n::sendMessage(textEditor.hwnd,EM_REPLACESEL,FALSE,(LPARAM)L"New text");require(textEditor.finish(true).ok(),"commit colored new text");bool newText=false;for(const auto& p:s.scene.primitives())if(p.source==ObjectRef(ObjectKind::Text,nextText)&&p.style.fill.enabled){newText=true;require(render::resolvePaint(p.style.fill.rgba)==0x008C46FFu,"committed text retains chosen color");}require(newText,"new colored text visible");
    auto coordinates=s.doc.atoms().records();
    require(s.submit(Command("drawing width settings",kInvalidateAppearance|kInvalidateGeometry,[](CommandContext& c){c.document.noteTouchedDefaults();c.document.setDefaultLineWidth(.9);return CommandResult::success();}),"drawing width settings").ok(),"settings change molecular width");
    checkPaint(s,ObjectRef(ObjectKind::Bond,unselected),.9,0x000000FFu);
    checkPaint(s,orbital,1.,0xE68200FFu);
    for(size_t i=0;i<coordinates.size();++i)require(coordinates[i].position==s.doc.atoms().records()[i].position,"width settings preserve atom positions");
    s.selected.clear();switchTab(1);layout();s.fit();screenshot(dir+"/chemical-style.png");
    std::ofstream(dir+"/chemical-style-checks.json")<<"{\"chemical_bonds_labels_text\":true,\"single_double_triple_dashed_bold_wedge_hash\":true,\"semantics_preserved\":true,\"unselected_preserved\":true,\"undo_redo\":true,\"native_and_cdxml\":true,\"new_bonds_rings_chains\":true,\"inline_color\":true}";
    s.selected.clear();require(applyGraphicsStyle(.6,0x000000FFu,true,true).ok(),"restore future default graphic format");switchTab(1);layout();s.fit();screenshot(dir+"/compact-graphics-format.png");
    require(applyGraphicsStyle(.75,0x0046DCFFu,true,true).ok(),"visible format fixture");
    require(graphicWidthText(s.graphicsWidth)=="0.75 磅"&&graphicColorText(s.graphicsColor)=="蓝色"&&graphicColorText(0x123456FFu)=="#123456","persistent values show current width and named or custom color");
    {Bitmap strip(width,top,PixelFormat32bppARGB);Graphics g(&strip);paint(g,true,false);UINT count=0,bytes=0;GetImageEncodersSize(&count,&bytes);std::vector<uint8_t> storage(bytes);auto encoders=(ImageCodecInfo*)storage.data();GetImageEncoders(count,bytes,encoders);for(UINT i=0;i<count;++i)if(wcscmp(encoders[i].MimeType,L"image/png")==0)strip.Save(W(dir+"/visible-format-controls.png").c_str(),&encoders[i].Clsid,0);}
    std::ofstream(dir+"/visible-format-checks.json")<<"{\"independent_width_color_controls\":true,\"width_value\":\"0.75 磅\",\"color_name\":\"蓝色\",\"custom_hex\":true}";
    std::ofstream(dir+"/graphic-style-checks.json")<<"{\"default_black\":true,\"future_objects\":true,\"arrow_shape_curve_bracket\":true,\"mixed_selection\":true,\"atoms_recolored\":true,\"undo_redo\":true,\"native_roundtrip\":true,\"cdxml_arrow_width\":true}";
    s.selected.clear();s.graphicsColor=0x000000FFu;
    ObjectId colored=ObjectId(s.doc.texts().size());textEditor.begin(kInvalidObjectId,kInvalidObjectId,Vec2(400,300));
    studio_i18n::sendMessage(edit,EM_REPLACESEL,FALSE,(LPARAM)L"ABCD");textEditor.select(0,2);
    require(applyTextColor(0xDC1E1EFFu).ok(),"ribbon color applies to selected characters");
    require(textEditor.finish(true).ok(),"commit colored new text");
    auto textColors=[&](Session& session,ObjectRef ref,uint32_t first,uint32_t second){bool a=false,b=false;for(const auto& p:session.scene.primitives())if(p.source==ref&&p.style.fill.enabled){auto color=render::resolvePaint(p.style.fill.rgba);a|=color==first;b|=color==second;}require(a&&b,"render retains chosen text colors");};
    ObjectRef coloredRef(ObjectKind::Text,colored);textColors(s,coloredRef,0xDC1E1EFFu,0x000000FFu);
    s.selected={coloredRef};auto colorBefore=s.doc.digest();require(applyTextColor(0x0046DCFFu).ok(),"color selected text object");textColors(s,coloredRef,0x0046DCFFu,0x0046DCFFu);
    require(s.undo().ok()&&s.doc.digest()==colorBefore,"text object color undo");textColors(s,coloredRef,0xDC1E1EFFu,0x000000FFu);require(s.undo(true).ok(),"text object color redo");
    s.selected={ObjectRef(ObjectKind::Atom,atom)};require(applyTextColor(0xB400B4FFu).ok(),"color atom label");textColors(s,ObjectRef(ObjectKind::Atom,atom),0xB400B4FFu,0xB400B4FFu);
    require(s.save(dir+"/text-color.r62doc").ok(),"save text colors native");Session textNative;require(textNative.open(dir+"/text-color.r62doc").ok(),"open text colors native");textColors(textNative,coloredRef,0x0046DCFFu,0x0046DCFFu);
    require(s.save(dir+"/text-color.cdxml",true).ok(),"save text colors CDXML");Session textXml;require(textXml.open(dir+"/text-color.cdxml").ok(),"open text colors CDXML");textColors(textXml,coloredRef,0x0046DCFFu,0x0046DCFFu);
    s.selected.clear();require(applyTextColor(0x008C46FFu).ok(),"color without selection sets future text");require(s.graphicsColor==0x008C46FFu,"future text color recorded");
    std::ofstream(dir+"/text-color-checks.json")<<"{\"partial_new_text\":true,\"object_color\":true,\"atom_label_refresh\":true,\"undo_redo\":true,\"native_cdxml\":true,\"future_color\":true}";
    s.dirty=false;
}
