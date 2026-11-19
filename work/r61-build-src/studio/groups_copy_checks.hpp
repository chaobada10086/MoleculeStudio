#pragma once
static void groupsCopyCheck(App& app,const std::string& dir){
    using namespace r62::integration;
    auto require=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    int cases=0;
    for(auto label:{"Ph","i-Pr","Br","NO2","no2","UnknownLongGroup","Xyz123","NMe2","OTES"})for(int angle=0;angle<360;angle+=30){
        Session s;ObjectId c,a,b;double rad=angle*3.141592653589793/180;
        s.doc.addAtom(Vec2(),6,&c);s.doc.addAtom(Vec2(14.4*cos(rad),14.4*sin(rad)),6,&a);s.doc.addBond(c,a,&b);
        auto name=chemcompletion::normalize(label,true);require(s.submit(nicknames::labelCommand(a,name),"test").ok(),"apply group");
        RingGeometry rings(s.doc);auto labels=molecular::labels(s.doc,s.glyphs,rings);auto data=rings.build(*s.doc.bonds().get(b));molecular::clip(data,*s.doc.bonds().get(b),labels,1.6);
        require(!data.shapes.empty()&&data.shapes.front().points.size()==2,"label hides bond");
        double visible=core::length(data.shapes.front().points[1]-data.shapes.front().points[0]);
        std::ofstream(dir+"/visible-lengths.csv",std::ios::app)<<name<<","<<angle<<","<<visible<<"\n";
        require(visible>8,"terminal label shortens bond excessively");
        require(std::abs(core::length(s.doc.atoms().get(a)->position)-14.4)<1e-8,"label moves atom");
        if(name=="NO2"&&angle==150){const auto& gs=labels.at(a).layout.glyphs;require(gs[0].x>gs[1].x&&gs[0].x>gs[2].x,"left nitro connects through nitrogen");}
        if(angle==150){s.refresh();dragpreview::writeSvg(s.scene,dir+"/"+std::string(label)+".svg");}
        ++cases;
    }
    require(chemcompletion::normalize("no2",true)=="NO2","nitro completion");
    require(chemcompletion::normalize("otes",true)=="OTES","silylether completion");
    require(chemcompletion::normalize("Co",true)=="Co","element case preserved");
    require(chemcompletion::normalize("no2",false)=="no2","completion toggle");
    for(auto label:{"NO2","NH2","N3","OH","SH","OTES","OTBS","OCF3","B(OH)2"})require(nickdata::find(label)!=nullptr,"common group missing");
    std::string fixture="<CDXML BondLength=\"45\" LineWidth=\"3\"><page><fragment><n id=\"1\" p=\"0 0\"/><n id=\"2\" p=\"45 0\" Element=\"8\"/><b id=\"3\" B=\"1\" E=\"2\" Order=\"1\" Display=\"WedgeBegin\"/></fragment></page></CDXML>";
    auto file=dir+"/recognition.cdxml";std::ofstream(file)<<fixture;
    app.s.doc.setDefaultBondLength(21);app.s.doc.setDefaultLineWidth(.8);app.s.graphicsWidth=1.2;app.s.graphicsColor=0xBF3020FF;
    std::wstring error;require(app.insertOcsrStructure(file,"",error),"insert recognition");
    auto verify=[&](Session& s){require(s.doc.bonds().size()==1,"editable bond missing");auto b=s.doc.bonds().get(0);require(std::abs(core::length(s.doc.atoms().get(b->endAtom)->position-s.doc.atoms().get(b->beginAtom)->position)-21)<.01,"canvas bond length");
        auto& bag=static_cast<const Document&>(s.doc).extensions(ObjectRef(ObjectKind::Bond,b->id));require(bag.findValue("studio.graphics.width")&&std::abs(std::atof(bag.findValue("studio.graphics.width")->c_str())-1.2)<.001,"canvas width");require(bag.findValue("studio.color.override")&&*bag.findValue("studio.color.override")==std::to_string(0xBF3020FFu),"canvas color");require(b->display==BondDisplay::Wedge,"wedge preserved");};
    verify(app.s);require(app.s.undo().ok()&&app.s.doc.bonds().alive(0)==nullptr,"insertion undo");
    app.h=CreateWindowExW(0,L"STATIC",L"Recognition clipboard regression",WS_POPUP,0,0,1,1,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    IDataObject* old=nullptr;OleGetClipboard(&old);
    require(app.copyOcsrStructure(file,error),"copy recognition");Session pasted;require(studioio::paste(pasted,app.h).ok(),"paste clipboard into editable canvas");verify(pasted);
    if(old){OleSetClipboard(old);old->Release();}
    std::ofstream(dir+"/checks.json")<<"{\"direction_cases\":"<<cases<<",\"completion\":true,\"editable_insert\":true,\"undo\":true,\"clipboard_roundtrip\":true,\"canvas_bond_length_width_color\":true,\"stereo_preserved\":true}";
}
