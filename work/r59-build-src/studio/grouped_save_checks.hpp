#pragma once
static void checkGroupedSave(App& app,const std::string& dir){
    auto require=[](bool ok,const std::string& why){if(!ok)throw std::runtime_error("grouped save: "+why);};
    auto count=[](const Document& d){int n=0;for(auto& a:d.arrows().records())if(a.isAlive())++n;return n;};
    const char* source=std::getenv("MOLECULE_STUDIO_GROUPED_SAVE_SOURCE");
    std::string bytes;
    if(source)require(persistence::readFileBytes(source,&bytes).ok(),"read user fixture");
    // Give this fixture a fixed page origin; the exporter intentionally rebases
    // a fresh, centred sheet, which is a different concern from save fidelity.
    else bytes="<CDXML><page id=\"1\" BoundingBox=\"0 0 595 842\"><fragment id=\"2\"/><group id=\"3\"><group id=\"4\"><arrow id=\"5\" Tail3D=\"10 10 0\" Head3D=\"50 40 0\" ArrowheadHead=\"Full\"/></group></group></page></CDXML>";
    auto path=dir+"/grouped-save.cdxml";
    require(persistence::writeFileAtomic(path,bytes).ok(),"copy fixture");
    require(app.s.open(path).ok(),"open fixture");int expected=count(app.s.doc);require(expected>0,"arrows loaded");
    for(int cycle=0;cycle<3;++cycle){
        for(const auto a:app.s.doc.arrows().records())if(a.isAlive()){
            Vec2 points[4];for(int i=0;i<4;++i)points[i]=a.geometry[i]+Vec2(3,2);
            require(app.s.doc.setAnnotationGeometry(a.id,points,4).ok(),"edit arrow");break;
        }
        app.s.dirty=true;
        require(app.save(),"save existing file");require(!app.s.dirty,"saved state cleared");
        Session back;require(back.open(path).ok(),"reopen saved file");require(count(back.doc)==expected,"arrow count preserved");
        for(auto& a:app.s.doc.arrows().records())if(a.isAlive()){
            auto b=back.doc.arrows().alive(a.id);require(b!=nullptr,"arrow geometry retained");
            for(size_t i=0;i<4;++i)require(length(a.geometry[i]-b->geometry[i])<1e-6,"edited coordinates persisted");
        }
        require(app.leave(),"clean document closes without confirmation");
        require(app.s.open(path).ok(),"reload next cycle");
    }
    // Repeated engine exports also retain nested group closure tags.
    Document nested;require(persistence::readCdxml("<CDXML><page id=\"1\"><group id=\"2\"><group id=\"3\"><n id=\"4\" p=\"0 0\"/></group></group></page></CDXML>",nested).ok(),"nested fixture");
    for(int i=0;i<3;++i){std::string xml;require(studioio::exportXml(nested,&xml).ok(),"nested export");Document back;require(persistence::readCdxml(xml,back).ok(),"nested reopen");nested=std::move(back);}

    int cases=0;
    for(const std::string tail:{"</group></page></CDXML>","</group><group id=\"9\"/></page></CDXML>","</group><!-- tail > comment --></page></CDXML>"})for(bool remove:{false,true}){
        Document d;require(persistence::readCdxml("<CDXML><page id=\"1\"><group id=\"2\"><n id=\"3\" p=\"0 0\"/></group><group id=\"4\"><n id=\"5\" p=\"20 0\"/>"+tail,d).ok(),"tail fixture");
        if(remove){auto records=d.atoms().records();require(d.removeAtom(records.back().id).ok(),"remove boundary owner");}
        std::string xml;require(studioio::exportXml(d,&xml).ok(),"tail export");Document back;require(persistence::readCdxml(xml,back).ok(),"tail reload");require(back.stats().aliveAtoms==d.stats().aliveAtoms,"tail atom count");++cases;
    }
    for(const char* ext:{".cdxml",".cdx",".r62doc"}){
        require(app.s.open(path).ok(),"format source");auto target=dir+"/save-format"+ext;app.s.dirty=true;
        require(app.saveToPath(target).ok(),"save as format");require(app.saveToPath(target).ok(),"overwrite same destination");
        Session back;require(back.open(target).ok()&&count(back.doc)==expected,"format reopen");
        require(app.save(),"ordinary save format");require(back.open(target).ok(),"ordinary format reopen");
        auto originalPath=app.s.sourcePath;std::string before;require(persistence::readFileBytes(target,&before).ok(),"read before failure");
        app.s.dirty=true;require(!app.saveToPath(dir+"/nonexistent-directory/fail.cdxml").ok(),"write failure reported");
        require(app.s.dirty&&app.s.sourcePath==originalPath,"failed save retains dirty state and filename");std::string after;require(persistence::readFileBytes(target,&after).ok()&&before==after,"failed save keeps previous bytes");++cases;
    }
    app.s.dirty=false;
    std::ofstream(dir+"/grouped-save-checks.json")<<"{\"edit_save_reopen_cycles\":3,\"arrow_geometry_preserved\":true,\"clean_close\":true,\"nested_group_roundtrips\":3,\"additional_cases\":"<<cases<<"}";
}
