#pragma once
static void checkNativeCompatibility(App& app,const std::string& dir){
    auto require=[](bool value,const std::string& why){if(!value)throw std::runtime_error("native compatibility: "+why);};
    // Regression dimensions measured in ChemDraw's SVG output: a 13.95 pt
    // CirclePlus handle with 0.6 pt LineWidth draws a 6.44 pt radius circle.
    {
        std::string binary,decoded,why;
        const std::string source="<CDXML InterpretChemically=\"yes\"><page id=\"1\"><t id=\"25\" InterpretChemically=\"no\"><s>δ−</s></t></page></CDXML>";
        require(cdxbinary::fromXml(source,binary,why)&&cdxbinary::toXml(binary,decoded,why),"explicit false CDX boolean roundtrip");
        // Native ChemDraw stores a one-byte zero, rather than omitting this
        // property and inheriting the document's InterpretChemically=yes.
        require(binary.find(std::string("\x08\x07\x01\x00\x00",5))!=std::string::npos,"native explicit false boolean encoding");
        tinyxml2::XMLDocument xml;require(xml.Parse(decoded.c_str())==tinyxml2::XML_SUCCESS,"CDX caption XML");
        auto t=xml.RootElement()->FirstChildElement("page")->FirstChildElement("t");
        require(t&&std::string(t->Attribute("InterpretChemically"))=="no","CDX retains caption interpretation override");
    }
    int circleCases=0;
    for(double em:{6.,10.,18.})for(double line:{.3,.6,1.2})for(int sign:{-1,1}){
        Document d;d.documentExtensions().set("studio.style.LabelSize",std::to_string(em));d.setDefaultLineWidth(line);
        ObjectId id;require(d.addAtom(Vec2(20,20),6,&id).ok(),"circle atom");
        auto sem=d.atoms().alive(id)->semantics;sem.charge=sign;sem.implicitHydrogens=0;d.setAtomSemantics(id,sem);
        auto& bag=d.extensions({ObjectKind::Atom,id});bag.set("pst.attr.MoleculeStudioChargePosition","auto");bag.set("pst.attr.MoleculeStudioChargeCircle","1");
        auto bytes=persistence::writeCdxmlToString(d);tinyxml2::XMLDocument xml;require(xml.Parse(bytes.c_str())==tinyxml2::XML_SUCCESS,"circle XML");
        auto g=xml.RootElement()->FirstChildElement("page")->FirstChildElement("graphic");
        require(g&&std::string(g->Attribute("SymbolType"))==(sign>0?"CirclePlus":"CircleMinus"),"native circle symbol and sign");
        Vec2 a,b;std::istringstream box(g->Attribute("BoundingBox"));box>>a.x>>a.y>>b.x>>b.y;
        const double stroke=g->DoubleAttribute("LineWidth")*.8;
        require(std::abs(core::length(b-a)*4./9.+stroke*.5-em*.34)<1e-8,"native circle radius matches editor across styles");
        require(std::abs(stroke-std::max(.6,em*.08))<1e-8,"native circle stroke matches editor");
        auto rep=g->FirstChildElement("represent");require(rep&&std::string(rep->Attribute("attribute"))=="Charge","circle remains attached chemical charge");++circleCases;
    }
    {
        Session symbol;
        const std::string file=dir+"/standalone-circles.cdxml";
        std::ofstream(file)<<"<CDXML LineWidth=\"0.6\"><page id=\"1\"><graphic id=\"2\" GraphicType=\"Symbol\" SymbolType=\"CirclePlus\" BoundingBox=\"20 20 33.95 20\"/><graphic id=\"3\" GraphicType=\"Symbol\" SymbolType=\"CircleMinus\" BoundingBox=\"50 20 63.95 20\"/></page></CDXML>";
        require(symbol.open(file).ok(),"native standalone circles import");int outlines=0,signs=0;
        for(const auto& p:symbol.scene.primitives())if(p.source.kind==ObjectKind::Symbol){
            if(p.points.size()==96){double lo=1e10,hi=-1e10;for(auto v:p.points){lo=std::min(lo,v.x);hi=std::max(hi,v.x);}require(std::abs(hi-lo-12.88)<1e-6,"native SVG circle diameter");require(p.style.stroke.enabled&&!p.style.fill.enabled,"native circle hollow outline");++outlines;}
            else if(p.points.size()==2)++signs;
        }
        require(outlines==2&&signs==3,"native standalone plus/minus retain their circle and correct sign");
    }
    bool circleUser=false;
    if(auto source=std::getenv("MOLECULE_STUDIO_CIRCLE_COMPAT_SOURCE"))if(*source){
        Session original;require(original.open(source).ok(),"current circled user drawing opens");
        auto expected=original.doc.stats();std::map<int,std::pair<int,int>> electrons;
        for(const auto& a:original.doc.atoms().records())if(a.isAlive())electrons[a.sourceId]={int(a.semantics.charge),int(a.semantics.radical)};
        require(original.save(dir+"/circle-compatible.cdxml",true).ok(),"repair current circled drawing");
        for(int i=0;i<3;++i){Session back;require(back.open(dir+"/circle-compatible.cdxml").ok(),"circle file reopens");
            require(back.doc.stats().aliveAtoms==expected.aliveAtoms&&back.doc.stats().aliveBonds==expected.aliveBonds,"circle drawing graph stable");
            int circles=0;for(const auto& a:back.doc.atoms().records())if(a.isAlive()){
                require(electrons.at(a.sourceId)==std::make_pair(int(a.semantics.charge),int(a.semantics.radical)),"all charges and radicals stable");
                circles+=integration::molecular::circledElectronCharge(back.doc,a.id);
            }require(circles==2,"both circle styles survive");
            for(const auto& g:back.doc.symbols().records())require(!g.isAlive(),"circle graphics do not duplicate atom markers");
            back.sourcePath.clear();require(back.save(dir+"/circle-compatible.cdxml",true).ok(),"repeat circle save");
        }
        std::string bytes;persistence::readFileBytes(dir+"/circle-compatible.cdxml",&bytes);tinyxml2::XMLDocument xml;xml.Parse(bytes.c_str());int captions=0;
        std::function<void(tinyxml2::XMLElement*)> visit=[&](tinyxml2::XMLElement* e){if(std::string(e->Name())=="t"&&e->Attribute("InterpretChemically")&&std::string(e->Attribute("InterpretChemically"))=="no")++captions;for(auto c=e->FirstChildElement();c;c=c->NextSiblingElement())visit(c);};visit(xml.RootElement());require(captions==2,"both partial-charge captions excluded from formula parsing");std::string cdx,why;require(cdxbinary::fromXml(bytes,cdx,why),"encode fixed drawing as CDX");std::ofstream(dir+"/circle-compatible.cdx",std::ios::binary).write(cdx.data(),cdx.size());circleUser=true;
    }
    std::ofstream(dir+"/circle-compatibility-checks.json")<<"{\"circle_style_cases\":"<<circleCases<<",\"native_svg_dimensions\":true,\"native_standalone_symbols\":true,\"user_file\":"<<(circleUser?"true":"false")<<"}";
    int buttons=0;for(const auto& group:app.mechanismGroups)for(const auto& b:group.items)if(b.id=="mechanism.base"||b.id=="mechanism.nu"||b.id=="mechanism.proton"){require(b.iconOnly&&b.icon.find("generic")==0,"three formula icons without captions");++buttons;}require(buttons==3,"all three role buttons");
    // Imported hidden bonds must remain chemical connections, without changing
    // letter spacing or becoming visible when the object is regenerated.
    Session hidden;const std::string fixture="<CDXML LabelSize=\"10\" LabelFont=\"3\"><fonttable><font id=\"3\" name=\"Arial\"/></fonttable><page id=\"1\"><fragment id=\"2\"><n id=\"3\" p=\"0 0\" NodeType=\"GenericNickname\" MoleculeStudioRole=\"base\" NumHydrogens=\"0\"><t><s font=\"3\" size=\"10\">B</s></t></n><n id=\"4\" p=\"6.9458 0\" Element=\"1\" NumHydrogens=\"0\"/><b id=\"5\" B=\"3\" E=\"4\" Visible=\"no\"/></fragment></page></CDXML>";
    std::ofstream(dir+"/hidden-bh.cdxml")<<fixture;require(hidden.open(dir+"/hidden-bh.cdxml").ok(),"import hidden BH");auto edge=hidden.doc.bonds().alive(0);require(edge&&hidden.doc.extensions({ObjectKind::Bond,0}).findValue("pst.attr.Visible"),"hidden bond is promoted to owner");
    auto labels=integration::molecular::labels(hidden.doc,hidden.glyphs,integration::RingGeometry(hidden.doc));auto center=[](const text::RichLayout& l,size_t index){double a=1e100,b=-1e100;for(const auto& c:l.glyphs.at(index).contours)for(auto p:c){a=std::min(a,p.x);b=std::max(b,p.x);}return l.anchor.x+(a+b)*.5;};TextRun run;run.utf8="BH";run.fontName="Arial";run.size=10;text::LayoutOptions options;options.defaultFont="Arial";options.defaultSize=10;text::RichLayout pair;require(text::layoutRichText({run},Vec2(),options,&hidden.glyphs,&pair).ok(),"BH reference");require(std::abs(center(labels.at(1).layout,0)-center(labels.at(0).layout,0)-(center(pair,1)-center(pair,0)))<.04,"imported BH retains continuous typography");hidden.doc.setAtomPosition(0,Vec2(1,0));require(hidden.save(dir+"/hidden-bh-resaved.cdxml",true).ok(),"regenerate hidden BH");std::string bytes;persistence::readFileBytes(dir+"/hidden-bh-resaved.cdxml",&bytes);require(bytes.find("Visible=\"no\"")!=std::string::npos,"hidden bond survives regeneration");
    int ions=0;for(int element:{9,17,35,53,8,7}){Document d;ObjectId atom;d.addAtom(Vec2(),element,&atom);auto sem=d.atoms().alive(atom)->semantics;sem.charge=element==7?1:-1;sem.implicitHydrogens=element==7?4:element==8?1:0;d.setAtomSemantics(atom,sem);auto xml=persistence::writeCdxmlToString(d);tinyxml2::XMLDocument parsed;parsed.Parse(xml.c_str());auto n=parsed.RootElement()->FirstChildElement("page")->FirstChildElement("fragment")->FirstChildElement("n");require(n&&n->FirstChildElement("t"),"charged heteroatom has native explicit label");auto text=n->FirstChildElement("t");std::string label;for(auto part=text->FirstChildElement("s");part;part=part->NextSiblingElement("s"))if(part->GetText())label+=part->GetText();require(label.find(sem.charge>0?"+":"-")!=std::string::npos,"native label includes its formal charge");++ions;}
    bool user=false;auto source=std::getenv("MOLECULE_STUDIO_NATIVE_COMPAT_SOURCE");if(source&&*source){
        Session document;require(document.open(source).ok(),"open user example");int count=0;for(const auto& a:document.doc.symbols().records())if(a.isAlive())++count;require(count==0,"owned charge and radical symbols are not duplicated");require(document.save(dir+"/native-compatible.cdxml",true).ok(),"export fixed user drawing");
        for(int i=0;i<3;++i){Session back;require(back.open(dir+"/native-compatible.cdxml").ok(),"reopen user drawing");int bromides=0,singlets=0;for(const auto& a:back.doc.atoms().records())if(a.isAlive()){bromides+=a.element==35&&a.semantics.charge==-1;singlets+=a.semantics.radical==uint8_t(Radical::Singlet);}require(bromides==1&&singlets==1,"bromide and carbene semantics preserved");for(const auto& a:back.doc.symbols().records())require(!a.isAlive(),"repeated opens never duplicate electron glyphs");back.sourcePath.clear();require(back.save(dir+"/native-compatible.cdxml",true).ok(),"repeated save");}
        require(app.s.open(dir+"/native-compatible.cdxml").ok(),"render fixed drawing");app.s.selected.clear();app.s.fit();auto tab=app.ribbonTab;app.ribbonTab=6;app.layout();app.screenshot(dir+"/native-compatible.png");app.ribbonTab=tab;app.layout();user=true;
    }
    std::ofstream(dir+"/native-compatibility-checks.json")<<"{\"icon_only_roles\":3,\"labelled_ions\":"<<ions<<",\"hidden_bond_roundtrip\":true,\"bh_import_typography\":true,\"user_file\":"<<(user?"true":"false")<<",\"repeated_saves_no_duplicate_symbols\":true}";
}
