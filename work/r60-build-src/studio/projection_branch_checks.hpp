#pragma once
static void checkProjectionBranches(App& app,const std::string& dir){
    auto require=[](bool ok,const std::string& why){if(!ok)throw std::runtime_error("projection branches: "+why);};
    const int kinds[]={0,1,2,1,2},variants[]={2,0,0,1,1};int checked=0;Session gallery;
    gallery.doc.setDefaultFont("Arial");gallery.doc.setDefaultBondLength(24);gallery.doc.setDefaultTextSize(10);
    studioio::Page page;page.width=650;page.height=530;page.x=page.y=0;page.store(gallery.doc);
    for(int sourceView=0;sourceView<5;++sourceView){
        Document style=gallery.doc;auto source=stereo_projection::make(style,kinds[sourceView],variants[sourceView]);
        app.s.cancel();app.s.doc=source->doc;app.s.history.clear();app.s.selected.clear();app.s.input.bindDocument(app.s.doc.id());app.s.refresh();app.s.fit();app.s.activateCommand("chooseSelectionTool_Marquee");
        ObjectId axis=kInvalidObjectId;for(const auto& b:app.s.doc.bonds().records())if(b.isAlive()&&projection_molecule::axis(app.s.doc,b)){axis=b.id;break;}
        auto axisBond=*app.s.doc.bonds().alive(axis);ObjectId root=kInvalidObjectId,core=axisBond.beginAtom;std::vector<ObjectId> tips;
        for(const auto& a:app.s.doc.atoms().records())if(a.isAlive()&&app.s.doc.incidentBonds(a.id).size()==1)tips.push_back(a.id);
        for(auto bid:app.s.doc.incidentBonds(core)){auto b=app.s.doc.bonds().alive(bid);auto n=b->beginAtom==core?b->endAtom:b->beginAtom;if(n!=axisBond.endAtom&&app.s.doc.incidentBonds(n).size()==1){root=n;break;}}
        auto hover=[&](ObjectId id){auto p=app.s.view().toDevice(app.s.doc.atoms().alive(id)->position);SendMessageW(app.canvas,WM_MOUSEMOVE,0,MAKELPARAM(int(std::lround(p.x)),int(std::lround(p.y))));};
        hover(root);auto before=app.s.doc.digest();SendMessageW(app.canvas,WM_KEYDOWN,'7',1);SendMessageW(app.canvas,WM_CHAR,'7',1);
        require(app.s.doc.stats().aliveAtoms==12,"7 shortcut grows real cyclopentane");auto attached=app.s.doc.digest();require(app.s.undo().ok()&&app.s.doc.digest()==before&&app.s.undo(true).ok()&&app.s.doc.digest()==attached,"ring placement undo redo");
        std::vector<ObjectId> ring=projection_molecule::component(app.s.doc,root,core);require(ring.size()==5,"attached ring has five vertices");
        auto outward=[&](const Document& d){auto ray=d.atoms().alive(root)->position-d.atoms().alive(core)->position;ray=ray*(1/length(ray));Vec2 centroid;
            for(auto id:ring){auto offset=d.atoms().alive(id)->position-d.atoms().alive(root)->position;require(dot(offset,ray)>-1e-5,"ring stays outside its spoke endpoint");centroid=centroid+offset;}
            require(dot(centroid,ray)>d.defaultBondLength(),"ring extends away from projection centre");
        };outward(app.s.doc);
        int index=0;for(auto id:tips)if(id!=root){if(index==0)require(app.s.submit(nicknames::labelCommand(id,"t-Bu"),"原子快捷键").ok(),"tert-butyl attachment");if(index==1)require(app.s.submit(nicknames::labelCommand(id,"Br"),"原子快捷键").ok(),"bromine attachment");++index;}
        auto original=app.s.doc;auto expected=studio::analyze(original,app.s.all());require(expected.valid,"extended projection formula");
        for(int target=0;target<5;++target){app.s.doc=original;app.s.history.clear();app.s.input.bindDocument(app.s.doc.id());app.s.refresh();auto digest=app.s.doc.digest();app.convertProjection(axis,kinds[target],variants[target]);outward(app.s.doc);
            for(auto a:ring)for(auto b:ring)require(std::abs(length(original.atoms().alive(a)->position-original.atoms().alive(b)->position)-length(app.s.doc.atoms().alive(a)->position-app.s.doc.atoms().alive(b)->position))<1e-6,"conversion preserves rigid ring geometry");
            auto formula=studio::analyze(app.s.doc,app.s.all());require(formula.valid&&formula.plain==expected.plain&&std::abs(formula.averageMass-expected.averageMass)<1e-6,"ring/group formula survives conversion");
            auto after=app.s.doc.digest();if(after!=digest)require(app.s.undo().ok()&&app.s.doc.digest()==digest&&app.s.undo(true).ok()&&app.s.doc.digest()==after,"extended conversion undo redo");
            std::string xml;require(studioio::exportXml(app.s.doc,&xml).ok(),"extended export");std::ofstream(dir+"/branch-"+std::to_string(sourceView)+"-"+std::to_string(target)+".cdxml")<<xml;
            if(sourceView==1){app.s.selected.clear();auto bounds=app.s.scene.bounds();require(studioio::append(app.s.doc,gallery.doc,app.s.all(),Vec2(105+(target%3)*215,115+(target/3)*260)-(bounds.min+bounds.max)*.5,nullptr).ok(),"branch gallery");}
            ++checked;
        }
    }
    app.s.doc=gallery.doc;app.s.history.clear();app.s.selected.clear();app.s.input.bindDocument(app.s.doc.id());app.s.refresh();app.s.fit();app.invalidate();app.screenshot(dir+"/projection-branches.png");
    std::ofstream(dir+"/projection-branch-checks.json")<<"{\"source_views\":5,\"conversion_pairs\":"<<checked<<",\"actual_ring_shortcut\":true,\"outward_ring\":true,\"rigid_geometry\":true,\"formula_mass\":true,\"undo_redo\":true}";
}
