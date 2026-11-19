#pragma once
// Export contracts that specifically exercise the external ChemDraw boundary,
// including binary clipboard conversion (which cannot retain custom XML attrs).
static void chemdrawCompatibilityCheck(App& app,const std::string& dir){
    auto require=[](bool ok,const char* why){if(!ok)throw std::runtime_error(std::string("ChemDraw compatibility: ")+why);};
    Session sample;
    ObjectId base,nu,bh,h,boron,nitrogen;
    sample.doc.addAtom(Vec2(45,45),6,&base);mechanism::setGenericRole(sample.doc,base,"base");
    sample.doc.addAtom(Vec2(145,45),6,&nu);mechanism::setGenericRole(sample.doc,nu,"nucleophile");
    sample.doc.addAtom(Vec2(245,45),6,&bh);mechanism::setGenericRole(sample.doc,bh,"base");
    auto neutral=sample.doc.atoms().alive(bh)->semantics;neutral.charge=0;sample.doc.setAtomSemantics(bh,neutral);
    sample.doc.addAtom(Vec2(275,45),1,&h);sample.doc.addBond(bh,h,nullptr);
    sample.doc.addAtom(Vec2(345,45),5,&boron);sample.doc.setAtomLabel(boron,"B");
    sample.doc.addAtom(Vec2(445,45),7,&nitrogen);sample.doc.setAtomLabel(nitrogen,"N");
    auto cation=sample.doc.atoms().alive(nitrogen)->semantics;cation.charge=1;cation.implicitHydrogens=0;sample.doc.setAtomSemantics(nitrogen,cation);
    for(int k=0;k<3;++k){
        auto p=arrows::resolvePreset(49,Vec2(50+k*150,150),Vec2(110+k*150,220),.6);
        p.mechanism=true;p.route={Vec2(0,0),Vec2(.15,-.3),Vec2(.5,-.45),Vec2(.82,-.25),Vec2(1,0)};
        if(k==1)p.head.sides=arrows::HeadSides::Left;
        if(k==2)p.line=arrows::ArrowLine::Dashed;
        arrows::addArrow(sample.doc,p,.6,nullptr);
    }
    for(int k=0;k<2;++k){auto p=arrows::resolvePreset(8,Vec2(50+k*180,285),Vec2(145+k*180,285),.6);
        if(k)p.head.sides=arrows::HeadSides::Left;arrows::addArrow(sample.doc,p,.6,nullptr);}
    sample.doc.documentExtensions().set("studio.page","540 400 1 1 0 0");
    std::string xml;require(studioio::exportXml(sample.doc,&xml).ok(),"export fixture");
    require(persistence::writeFileAtomic(dir+"/compatibility.cdxml",xml).ok(),"write fixture");
    tinyxml2::XMLDocument parsed;require(parsed.Parse(xml.c_str())==tinyxml2::XML_SUCCESS,"valid XML");
    int roleNodes=0,curves=0;std::set<int> ids;
    std::function<void(tinyxml2::XMLElement*)> inspect=[&](tinyxml2::XMLElement* e){
        if(e->Attribute("id")&&std::string(e->Name())!="font")require(ids.insert(e->IntAttribute("id")).second,"unique object IDs");
        if(std::string(e->Name())=="n"){
            if(e->Attribute("MoleculeStudioRole")){++roleNodes;require(e->Attribute("IgnoreWarnings","yes"),"generic warning scope");require(e->FirstChildElement("objecttag"),"persistent generic role");}
            if(e->IntAttribute("Element")==5)require(!e->Attribute("IgnoreWarnings"),"real boron retains validation");
        }
        if(std::string(e->Name())=="curve"&&e->Attribute("MoleculeStudioArrow")){++curves;require(e->Attribute("CurvePoints")&&!e->Attribute("AngularSize"),"route is not replaced by circular arc");}
        for(auto c=e->FirstChildElement();c;c=c->NextSiblingElement())inspect(c);
    };inspect(parsed.RootElement());require(roleNodes==3&&curves==3,"fixture coverage");
    // ChemDraw keeps a write-capable handle open after saving. A read-only
    // import must share that handle rather than spuriously refusing the file.
    HANDLE external=CreateFileW(W(dir+"/compatibility.cdxml").c_str(),GENERIC_WRITE,
        FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    require(external!=INVALID_HANDLE_VALUE,"external editor handle fixture");
    Session shared;auto sharedResult=shared.open(dir+"/compatibility.cdxml");CloseHandle(external);
    require(sharedResult.ok(),"read while external editor retains a write handle");
    Session reload;require(reload.open(dir+"/compatibility.cdxml").ok(),"CDXML reopen");
    int alivePaths=0;for(const auto& path:reload.doc.paths().records())if(path.isAlive())++alivePaths;
    require(reload.doc.stats().aliveAtoms==6&&alivePaths==0,"no duplicated curve paths");
    auto checkRoles=[&](const Document& d){int bases=0,nus=0,realB=0;for(const auto& a:d.atoms().records())if(a.isAlive()){
        auto role=mechanism::genericRole(d,a.id);if(role=="base"){++bases;require(d.atomLabel(a.id)=="B","charge is not literal label");}
        if(role=="nucleophile")++nus;if(a.element==5&&role.empty())++realB;
    }require(bases==2&&nus==1&&realB==1,"generic/elemental distinction");};checkRoles(reload.doc);
    std::string binary,error,decoded;require(cdxbinary::fromXml(xml,binary,error),error.c_str());
    require(persistence::writeFileAtomic(dir+"/compatibility.cdx",binary).ok(),"write CDX");
    require(cdxbinary::toXml(binary,decoded,error),error.c_str());
    require(decoded.find("ArrowheadHead=\"Full\"")!=std::string::npos&&decoded.find("ArrowheadHead=\"HalfLeft\"")!=std::string::npos,"binary preserves full/fishhook heads");
    require(decoded.find("ArrowheadWidth=")!=std::string::npos,"binary preserves head dimensions");
    require(cdxbinary::objectTag("arrow")==0x8021,"native arrow object code");
    std::string coords,mask;
    require(cdxbinary::fromXml("<CDXML><page id=\"1\"><arrow id=\"2\" Head3D=\"200 280 0\" Tail3D=\"150 280 0\"/><curve id=\"3\" CurveType=\"129\"/></page></CDXML>",coords,error),"coordinate fixture");
    require(cdxbinary::toXml(coords,mask,error)&&mask.find("Head3D=\"200 280 0\"")!=std::string::npos&&mask.find("CurveType=\"129\"")!=std::string::npos,"XYZ order and curve mask");
    std::string nativeHead;cdxbinary::put(nativeHead,0x0207,2);cdxbinary::put(nativeHead,12,2);
    for(int coordinate:{200,280,0})cdxbinary::put(nativeHead,uint32_t(coordinate*65536),4);
    require(coords.find(nativeHead)!=std::string::npos,"native XYZ wire order, independently of decoder");
    require(cdxbinary::property("MajorAxisEnd3D")->tag==0x20e&&cdxbinary::property("MinorAxisEnd3D")->tag==0x20f,"arc axes are distinct from center");
    Document binaryDoc;require(persistence::readCdxml(decoded,binaryDoc).ok(),"binary decode import");checkRoles(binaryDoc);
    require(persistence::writeFileAtomic(dir+"/binary-decoded.cdxml",decoded).ok(),"write binary decode");
    // Query node kinds are distinct CDXML types, not interchangeable with
    // elemental carbon or undefined application enum names.
    const std::string query="<CDXML><page id=\"1\"><fragment id=\"2\"><n id=\"3\" p=\"20 20\" NodeType=\"ElementList\" ElementList=\"7 8\"/></fragment></page></CDXML>";
    Document q;require(persistence::readCdxml(query,q).ok(),"query import");std::string qxml=persistence::writeCdxmlToString(q);
    require(qxml.find("NodeType=\"ElementList\"")!=std::string::npos&&qxml.find("ElementList=\"7 8\"")!=std::string::npos,"query semantics survive regeneration");
    Document pasted;require(studioio::append(q,pasted,{ObjectRef(ObjectKind::Atom,0)},Vec2(10,0),nullptr).ok(),"copy query");
    qxml=persistence::writeCdxmlToString(pasted);require(qxml.find("ElementList=\"7 8\"")!=std::string::npos,"query constraints survive clipboard subset");
    auto graph=[](const Document& d){std::vector<std::string> keys;for(const auto& a:d.atoms().records())if(a.isAlive()){
        std::vector<int> adjacent;for(auto bid:d.incidentBonds(a.id)){auto b=d.bonds().alive(bid);if(b)adjacent.push_back(int(b->order)*1000+d.atoms().alive(b->beginAtom==a.id?b->endAtom:b->beginAtom)->element);}
        std::sort(adjacent.begin(),adjacent.end());std::ostringstream key;key<<a.element<<','<<int(a.semantics.charge)<<','<<a.semantics.isotope<<','<<d.atomLabel(a.id);for(auto n:adjacent)key<<','<<n;keys.push_back(key.str());
    }std::sort(keys.begin(),keys.end());return keys;};
    for(int i=0;i<32;++i){Session a,b;require(persistence::readCdxml(projectCorpusXml(i),a.doc).ok(),"corpus import");
        const std::string path=dir+"/corpus-"+std::to_string(i)+".cdxml";require(a.save(path,true).ok()&&b.open(path).ok(),"corpus file roundtrip");require(graph(a.doc)==graph(b.doc),"corpus isotope/connectivity preserved");}
    Session catalog;int catalogCount=0;
    for(const auto& n:nickdata::nicknames){ObjectId id;catalog.doc.addAtom(Vec2((catalogCount%12)*70,(catalogCount/12)*36),6,&id);
        auto sem=catalog.doc.atoms().alive(id)->semantics;sem.nodeType=uint8_t(NodeType::Nickname);catalog.doc.setAtomSemantics(id,sem);catalog.doc.setAtomLabel(id,n.name);
        catalog.doc.extensions(ObjectRef(ObjectKind::Atom,id)).set("studio.nickname.fragment",n.fragment);++catalogCount;}
    require(catalog.save(dir+"/structural-nicknames.cdxml",true).ok(),"structural nickname export");Session back;
    require(back.open(dir+"/structural-nicknames.cdxml").ok()&&back.doc.stats().aliveAtoms==catalogCount,"nested nickname fragments remain attached");
    if(const char* source=std::getenv("MOLECULE_STUDIO_COMPAT_SOURCE")){
        Session user;require(user.open(source).ok(),"user file import");require(user.save(dir+"/user-repaired.cdxml",true).ok(),"user file export");
        app.s.doc=user.doc;app.s.refresh();app.s.fit();app.screenshot(dir+"/user-repaired.png");
    }
    if(const char* source=std::getenv("MOLECULE_STUDIO_CHEMDRAW_RESAVED")){
        Session user;auto imported=user.open(source);require(imported.ok(),("ChemDraw resaved import: "+imported.message).c_str());checkRoles(user.doc);
        require(user.save(dir+"/chemdraw-resaved-reimport.cdxml",true).ok(),"ChemDraw resaved export");
        app.s.doc=user.doc;app.s.refresh();app.s.fit();app.screenshot(dir+"/chemdraw-resaved-reimport.png");
    }
    std::ofstream(dir+"/compatibility-checks.json")<<"{\"passed\":true,\"scoped_generic_warnings\":true,\"persistent_role_tags\":true,\"charge_label_roundtrip\":true,\"real_boron_preserved\":true,\"native_bezier_routes\":true,\"unique_ids\":true,\"cdx_arrowheads\":true,\"element_list_preserved\":true,\"molecule_corpus\":32,\"structural_nicknames\":"<<catalogCount<<"}";
}

