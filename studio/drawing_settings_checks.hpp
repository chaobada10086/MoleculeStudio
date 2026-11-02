#pragma once
static void drawingSettingsCheck(App& app,const std::string& dir){
    auto require=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    DrawingSettings config;config.path=W(dir+"/test-drawing.ini");
    config.bondLength=14.4;config.lineWidth=1.19;
    require(config.save(),"save drawing defaults");
    DrawingSettings loaded;loaded.path=config.path;loaded.load();
    require(std::abs(loaded.bondLength-14.4)<1e-8&&std::abs(loaded.lineWidth-1.19)<1e-8,"reference defaults reload");
    double value=0;
    for(auto invalid:{L"",L"nan",L"inf",L"0",L"-1",L"1.19abc",L"13"})require(!DrawingSettings::parse(invalid,.1,12,&value),"reject invalid line width");
    require(DrawingSettings::parse(L" 1.19 ",.1,12,&value)&&std::abs(value-1.19)<1e-8,"parse decimal width");
    loaded.bondLength=22.5;loaded.lineWidth=.85;require(loaded.save(),"save custom values");
    DrawingSettings restarted;restarted.path=loaded.path;restarted.load();
    require(restarted.bondLength==22.5&&restarted.lineWidth==.85,"custom values persist");
    app.drawingSettings=restarted;app.applyDrawingDefaults();auto& s=app.s;
    auto before=s.doc.digest();
    s.activateCommand("chooseRingTool_Benzene");s.gesture(PointerPhase::Down,Vec2(),0);s.gesture(PointerPhase::Up,Vec2(),0);
    require(s.doc.stats().aliveAtoms==6&&s.doc.stats().aliveBonds==6,"draw editable ring with settings");
    for(const auto& b:s.doc.bonds().records())if(b.isAlive()){
        auto a=s.doc.atoms().get(b.beginAtom),z=s.doc.atoms().get(b.endAtom);
        require(std::abs(core::length(a->position-z->position)-22.5)<.01,"ring uses configured bond length");
    }
    {
        // The benzene tool's default orientation has a vertex straight up:
        // vertices at 30 + 60k degrees instead of 60k (which would put vertices
        // on the horizontal axis).
        std::vector<double> angles;
        for(const auto& a:s.doc.atoms().records())if(a.isAlive()){
            double degrees=std::atan2(-a.position.y,a.position.x)*180.0/3.141592653589793;
            if(degrees<0)degrees+=360.0;
            angles.push_back(degrees);
        }
        std::sort(angles.begin(),angles.end());
        require(angles.size()==6,"benzene has six vertices");
        for(int i=0;i<6;++i)require(std::abs(angles[size_t(i)]-(30.0+60.0*i))<1e-6,"default benzene has an upright vertex (30 degree default)");
        // The two 30-degree turns give the same hexagon but different Kekule
        // layouts. The editor's rotated default has the double bond on the LEFT
        // vertical edge (top-right and bottom-right slants carry the others).
        bool leftVerticalDouble=false,rightVerticalDouble=false;
        for(const auto& b:s.doc.bonds().records())if(b.isAlive()&&b.order==BondOrder::Double){
            const auto* a=s.doc.atoms().alive(b.beginAtom);const auto* z=s.doc.atoms().alive(b.endAtom);
            if(!a||!z||std::abs(a->position.x-z->position.x)>1e-6)continue;
            if(a->position.x<0)leftVerticalDouble=true;else rightVerticalDouble=true;
        }
        require(leftVerticalDouble&&!rightVerticalDouble,"default benzene Kekule matches the 30 degree rotated layout");
    }
    auto positions=s.doc.atoms().records();auto oldDefault=s.doc.defaultLineWidth();
    auto e=s.submit(Command("settings test",kInvalidateAppearance|kInvalidateGeometry,[](CommandContext& c){c.document.noteTouchedDefaults();c.document.setDefaultLineWidth(1.19);c.document.setDefaultBondLength(14.4);return CommandResult::success();}),"settings test");
    require(e.ok()&&s.doc.defaultLineWidth()==1.19,"apply defaults to document");
    for(size_t i=0;i<positions.size();++i)require(positions[i].position==s.doc.atoms().records()[i].position,"changing defaults preserves existing coordinates");
    require(s.undo().ok()&&s.doc.defaultLineWidth()==oldDefault,"document defaults undo");
    require(s.undo(true).ok()&&s.doc.defaultLineWidth()==1.19,"document defaults redo");
    require(s.save(dir+"/drawing-defaults.cdxml",true).ok(),"export defaults");
    Session roundtrip;require(roundtrip.open(dir+"/drawing-defaults.cdxml").ok(),"reopen export");
    require(roundtrip.doc.defaultBondLength()==14.4&&roundtrip.doc.defaultLineWidth()==1.19,"CDXML keeps defaults");
    app.aiContext();require(std::abs(app.aiWidth-1.19)<1e-8,"AI inherits document width");
    std::string xml="<result ok='true'><actions><molecule><![CDATA[<CDXML BondLength='30' LineWidth='0.6'><page id='1'><fragment id='2'><n id='3' p='0 0'/><n id='4' p='30 0'/><b id='5' B='3' E='4' Order='1'/></fragment></page></CDXML>]]></molecule></actions></result>";
    app.applyAI(xml);require(s.save(dir+"/ai-width.cdxml",true).ok(),"AI structure export");
    // Explicit imported document styles must not be overwritten by app defaults.
    Session imported;require(persistence::readCdxml("<CDXML BondLength='20' LineWidth='0.45'><page id='1'><fragment id='2'><n id='3' p='0 0'/><n id='4' p='20 0'/><b id='5' B='3' E='4' Order='1'/></fragment></page></CDXML>",imported.doc).ok(),"import style fixture");
    require(imported.doc.defaultBondLength()==20&&imported.doc.defaultLineWidth()==.45,"imports retain styles");
    std::ofstream(dir+"/checks.json")<<"{\"reference_defaults\":true,\"persistence\":true,\"validation\":true,\"configured_ring_length\":true,\"preserve_coordinates\":true,\"undo_redo\":true,\"cdxml_roundtrip\":true,\"ai_width\":true,\"import_styles\":true}";
    (void)before;
}
