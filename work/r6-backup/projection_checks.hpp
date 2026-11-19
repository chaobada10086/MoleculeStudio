#pragma once
static void checkProjectionTemplates(App& app,const std::string& dir){
    auto require=[](bool ok,const std::string& why){if(!ok)throw std::runtime_error("projection: "+why);};
    auto groupCount=[](const Document& d){size_t n=0;for(const auto& g:d.groups().records())n+=g.isAlive();return n;};
    auto templateTools=makeTemplateTools();auto graphicTools=makeExpandedTools(false);int buttons=0;for(auto group:templateTools)for(auto b:group.items)if(b.cmd=="@projection"){require(b.arg>=0&&b.arg<3&&!b.icon.empty(),"projection ribbon routes");++buttons;}require(buttons==3,"three projection tools on template page");for(auto group:graphicTools)for(auto b:group.items)require(b.cmd!="@projection","graphics page has no separate projections");require(templateTools.size()>3&&templateTools[2].name=="立体投影"&&templateTools[3].name=="自定义官能团","projection group precedes custom groups");
    Document style;style.setDefaultFont("Arial");style.setDefaultBondLength(20);style.setDefaultTextSize(10);
    int cases=0;
    for(int kind=0;kind<3;++kind)for(int variant=kind==0?1:0;variant<(kind==0?13:2);++variant){
        auto source=stereo_projection::make(style,kind,variant);
        const size_t texts=0;
        require(source->doc.stats().aliveTexts==texts&&groupCount(source->doc)==1,"editable labels and one group");
        const size_t members=kind==0?size_t(2*variant+1):kind==1?7:8;
        require(source->doc.groups().records().front().members.size()==members,"all lines, circles and captions belong to group");
        require(source->doc.stats().aliveAtoms==0,"diagram placeholders do not invent chemical atoms");
        require(source->scene.primitives().size()>texts,"all diagram ink renders");
        std::string xml;require(studioio::exportXml(source->doc,&xml).ok(),"standard projection export");
        for(int round=0;round<3;++round){Session back;require(persistence::readCdxml(xml,back.doc).ok(),"grouped diagram repeated reopen");back.refresh();require(back.doc.stats().aliveTexts==texts&&groupCount(back.doc)==1,"no label or group loss");require(studioio::exportXml(back.doc,&xml).ok(),"re-export diagram");}
        std::string binary,decoded,why;require(cdxbinary::fromXml(xml,binary,why)&&cdxbinary::toXml(binary,decoded,why),"projection CDX roundtrip");
        Document back;require(persistence::readCdxml(decoded,back).ok()&&back.stats().aliveTexts==texts&&groupCount(back)==1,"CDX retains editable grouped labels");
        if(kind==2){int circles=0,filled=0;tinyxml2::XMLDocument tree;tree.Parse(decoded.c_str());std::function<void(tinyxml2::XMLElement*)> visit=[&](tinyxml2::XMLElement* e){if(e->Attribute("GraphicType")&&std::string(e->Attribute("GraphicType"))=="Oval"){++circles;filled+=e->Attribute("FillType")&&std::string(e->Attribute("FillType"))=="Solid";}for(auto c=e->FirstChildElement();c;c=c->NextSiblingElement())visit(c);};visit(tree.RootElement());require(circles==2&&filled==1&&decoded.find("Center3D=")!=std::string::npos&&decoded.find("MajorAxisEnd3D=")!=std::string::npos,"Newman circle, dot and explicit axes survive CDX");}
        ++cases;
    }
    if(const char* path=std::getenv("MOLECULE_STUDIO_PROJECTION_NATIVE_SOURCE")){
        Session native;require(native.open(path).ok(),"open actual ChemDraw-saved projection fixture");
        require(native.doc.stats().aliveAtoms==0&&native.doc.stats().aliveTexts==30&&groupCount(native.doc)==5,"native roundtrip retains diagram captions and groups");
        size_t paths=0,arrows=0,members=0,fills=0;for(const auto& p:native.doc.paths().records())paths+=p.isAlive();for(const auto& a:native.doc.arrows().records())arrows+=a.isAlive();for(const auto& g:native.doc.groups().records())if(g.isAlive())members+=g.members.size();for(const auto& p:native.scene.primitives())fills+=(p.source.kind==ObjectKind::Path&&p.style.fill.enabled);
        require(paths==32&&arrows==0&&members==62&&fills==2,"native vector lines do not duplicate; filled front dots retained");
        require(native.save(dir+"/projections-after-chemdraw.cdxml",true).ok(),"resave after ChemDraw");
    }
    app.s.cancel();app.s.doc=style;app.s.history.clear();app.s.selected.clear();app.s.input.bindDocument(app.s.doc.id());app.s.refresh();
    auto before=app.s.doc.digest();app.armProjection(2,0);require(app.bioPlacing&&app.customPlacement&&app.s.doc.digest()==before,"arm projection without edits");
    app.cancelBioPlacement();require(!app.bioPlacing&&app.s.doc.digest()==before,"Esc/cancel does not insert");
    Session gallery;gallery.doc=style;studioio::Page page;page.width=460;page.height=320;page.x=page.y=0;page.store(gallery.doc);
    const int kinds[]={0,1,2,1,2},variants[]={2,0,0,1,1};
    for(int i=0;i<5;++i){
        app.armProjection(kinds[i],variants[i]);auto source=app.bioSource;require(source!=nullptr,"prepared source");
        app.placeBioComponent(Vec2(90,90));require(!app.bioPlacing&&groupCount(app.s.doc)==1,"actual insertion selects editable group");
        require(app.s.undo().ok()&&app.s.doc.digest()==before,"one insertion undo");require(app.s.undo(true).ok(),"insertion redo");
        // Empty endpoints are editable without preprinted R/H placeholders.
        app.s.fit();Vec2 tip;bool found=false;
        for(const auto& path:app.s.doc.paths().records())if(path.isAlive()&&!path.closed&&path.points.size()==2){tip=path.points.back();found=true;}
        require(found,"editable endpoint exists");
        auto click=[&](Vec2 point,bool twice){auto pixel=app.s.view().toDevice(point);auto pos=MAKELPARAM(int(std::lround(pixel.x)),int(std::lround(pixel.y)));studio_i18n::sendMessage(app.canvas,WM_MOUSEMOVE,0,pos);studio_i18n::sendMessage(app.canvas,WM_LBUTTONDOWN,MK_LBUTTON,pos);studio_i18n::sendMessage(app.canvas,WM_LBUTTONUP,0,pos);if(twice){studio_i18n::sendMessage(app.canvas,WM_LBUTTONDBLCLK,MK_LBUTTON,pos);studio_i18n::sendMessage(app.canvas,WM_LBUTTONUP,0,pos);}};
        app.s.activateCommand("chooseEdittingTool_Text");
        click(tip,false);
        require(app.textEditor.active,"text-tool click on grouped endpoint opens editor");
        auto label=app.textEditor.text;
        require(label!=kInvalidObjectId,"endpoint creates a grouped caption");
        studio_i18n::setWindowText(app.textEditor.hwnd,L"CH3");require(app.finishText(true),"commit substituent");
        require(fullText(app.s.doc,label)=="CH3"&&app.s.doc.stats().aliveTexts==1,"one substituent saved");
        app.s.activateCommand("chooseSelectionTool_Marquee");click(tip,true);
        require(app.textEditor.active&&app.textEditor.text==label,"real double-click reuses occupied endpoint");
        studio_i18n::setWindowText(app.textEditor.hwnd,L"CH2CH2OH");require(app.finishText(true),"change substituent");
        require(app.s.undo().ok()&&fullText(app.s.doc,label)=="CH3","undo label change");
        require(app.s.undo(true).ok()&&fullText(app.s.doc,label)=="CH2CH2OH","redo label change");
        Box labelBox;for(const auto& primitive:app.s.scene.primitives())if(primitive.source==ObjectRef(ObjectKind::Text,label)){labelBox.add(primitive.bounds.min);labelBox.add(primitive.bounds.max);}
        require(labelBox.max.x-labelBox.min.x>20,"long caption has measured visible width");
        auto labelPoint=Vec2(labelBox.max.x-1,(labelBox.min.y+labelBox.max.y)*.5);
        app.s.activateCommand("chooseEdittingTool_Text");click(labelPoint,false);
        require(app.textEditor.active&&app.textEditor.text==label,"text tool edits long existing group label");app.finishText(false);
        app.s.activateCommand("chooseSelectionTool_Marquee");
        auto pixel=app.s.view().toDevice(tip);studio_i18n::sendMessage(app.canvas,WM_MOUSEMOVE,0,MAKELPARAM(int(std::lround(pixel.x)),int(std::lround(pixel.y))));
        studio_i18n::sendMessage(app.canvas,WM_KEYDOWN,'O',0);studio_i18n::sendMessage(app.canvas,WM_CHAR,'O',0);studio_i18n::sendMessage(app.canvas,WM_KEYDOWN,'H',0);studio_i18n::sendMessage(app.canvas,WM_CHAR,'H',0);
        require(app.textEditor.active&&app.textEditor.text==label,"hover typing opens existing substituent");require(app.finishText(true)&&fullText(app.s.doc,label)=="OH","hover typing replaces label");
        require(app.s.doc.stats().aliveTexts==1&&app.s.doc.stats().aliveAtoms==0,"editing does not stack labels or create stray atoms");
        for(bool binary:{false,true}){
            auto file=dir+"/edited-projection-"+std::to_string(i)+(binary?".cdx":".cdxml");
            Session reopened;
            if(binary){std::string xml,bytes,decoded,why;require(studioio::exportXml(app.s.doc,&xml).ok()&&cdxbinary::fromXml(xml,bytes,why)&&cdxbinary::toXml(bytes,decoded,why),"edited CDX roundtrip");std::ofstream(file,std::ios::binary).write(bytes.data(),bytes.size());require(persistence::readCdxml(decoded,reopened.doc).ok(),"read edited CDX");}
            else{require(app.s.save(file,true).ok(),"save edited projection");require(reopened.open(file).ok(),"reopen edited projection");}
            app.s.doc=reopened.doc;app.s.selected.clear();app.s.history.clear();app.s.input.bindDocument(app.s.doc.id());app.s.refresh();
            // CDXML export translates drawing coordinates onto its page.
            for(const auto& path:app.s.doc.paths().records())if(path.isAlive()&&!path.closed&&path.points.size()==2)tip=path.points.back();app.s.fit();
            click(tip,true);require(app.textEditor.active&&fullText(app.s.doc,app.textEditor.text)=="OH","reopened substituent remains editable");app.finishText(false);
        }
        const Vec2 center(75+(i%3)*150,80+(i/3)*150);auto bounds=source->scene.bounds();auto delta=center-(bounds.min+bounds.max)*.5;
        require(studioio::append(source->doc,gallery.doc,source->all(),delta,nullptr).ok(),"gallery component");
        for(const auto& t:gallery.doc.texts().records())if(t.isAlive()){auto flag=gallery.doc.extensions(ObjectRef(ObjectKind::Text,t.id)).findValue("pst.attr.InterpretChemically");require(flag&&*flag=="no","inserted projection labels remain nonchemical captions");}
        app.s.cancel();app.s.doc=style;app.s.history.clear();app.s.selected.clear();app.s.input.bindDocument(app.s.doc.id());app.s.refresh();
    }
    Session sugarGallery;sugarGallery.doc=style;studioio::Page sugarPage;sugarPage.width=650;sugarPage.height=650;sugarPage.x=sugarPage.y=0;sugarPage.store(sugarGallery.doc);
    for(int mirror=0;mirror<2;++mirror)for(size_t i=0;i<stereo_projection::sugars().size();++i){
        auto source=stereo_projection::sugar(style,int(i),mirror);require(source->doc.stats().aliveAtoms==0&&groupCount(source->doc)==1,"sugar is a grouped editable projection");
        auto file=dir+"/sugar-"+std::to_string(i)+(mirror?"-L":"-D")+".cdxml";require(source->save(file,true).ok(),"sugar saved for independent CIP checks");
        app.s.doc=source->doc;app.s.refresh();app.s.selected=app.s.all();std::string answer;require(app.prepareStereoLabels(&answer)&&answer.find("\nprojection\t")!=std::string::npos,"every sugar can be annotated");
        if(i==9&&!mirror){app.execute(Button("testStereo","标注构型","","@stereoLabels"));app.s.selected.clear();app.s.fit();app.invalidate();app.screenshot(dir+"/glucose-annotated.png");app.s.selected=app.s.all();auto texts=app.s.doc.stats().aliveTexts;require(texts==source->doc.stats().aliveTexts+4,"glucose four CIP labels");app.execute(Button("testStereo","标注构型","","@stereoLabels"));require(app.s.doc.stats().aliveTexts==texts,"projection annotation is idempotent");require(app.s.undo().ok(),"annotation undo");}
        if((i==9||i==13||i==3||i==17)&&!mirror){int col=i==9?0:i==13?1:i==3?2:3;auto bounds=source->scene.bounds();auto delta=Vec2(80+col*150,160)-(bounds.min+bounds.max)*.5;require(studioio::append(source->doc,sugarGallery.doc,source->all(),delta,nullptr).ok(),"sugar gallery");}
    }
    sugarGallery.refresh();require(sugarGallery.save(dir+"/fischer-sugars.cdxml",true).ok(),"sugar gallery save");app.s.doc=sugarGallery.doc;app.s.refresh();app.s.selected.clear();app.s.fit();app.invalidate();app.screenshot(dir+"/fischer-sugars.png");
    gallery.refresh();require(gallery.save(dir+"/stereochemical-projections.cdxml",true).ok(),"gallery CDXML");
    std::string xml,binary,why;require(studioio::exportXml(gallery.doc,&xml).ok()&&cdxbinary::fromXml(xml,binary,why),"gallery CDX");std::ofstream(dir+"/stereochemical-projections.cdx",std::ios::binary).write(binary.data(),binary.size());
    app.s.doc=gallery.doc;app.s.refresh();app.s.fit();app.s.selected.clear();app.invalidate();app.screenshot(dir+"/stereochemical-projections.png");
    std::ofstream(dir+"/projection-checks.json")<<"{\"variants\":"<<cases<<",\"ribbon_buttons\":3,\"placement_cancel\":true,\"one_step_undo_redo\":true,\"editable_grouped_labels\":true,\"three_cdxml_roundtrips\":true,\"cdx_circle_dot_labels\":true}";
}
