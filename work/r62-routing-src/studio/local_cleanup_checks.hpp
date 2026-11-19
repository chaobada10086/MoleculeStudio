#pragma once
static void checkLocalCleanup(App& app,const std::string& dir){
    auto require=[](bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);};
    auto& s=app.s;int cases=0;
    for(double turn:{0.,1.1})for(double mirror:{-1.,1.})for(int selection:{0,1,2,3}){
        s.cancel();s.doc=Document();s.history.clear();s.selected.clear();s.doc.setDefaultBondLength(30);s.input.bindDocument(s.doc.id());
        const Vec2 points[]={{0,-15},{-25.9807621135,-30},{-51.961524227,-15},{-51.961524227,15},{-25.9807621135,30},{0,15},{25.9807621135,-30},{57,18},{82.9807621135,3}};
        std::vector<ObjectId> ids,bonds;ObjectId unrelated;
        for(auto point:points){point.x*=mirror;ObjectId id;s.doc.addAtom(mechanism::step_detail::rotate(point,turn)+Vec2(250,180),6,&id);ids.push_back(id);if(ids.size()==3)s.doc.addAtom(Vec2(-90,270),8,&unrelated);}
        for(size_t i=0;i<6;++i){ObjectId b;s.doc.addBond(ids[i],ids[(i+1)%6],&b);if(i%2==0)s.doc.setBondOrder(b,BondOrder::Double);bonds.push_back(b);}
        for(size_t i=6;i<9;++i){ObjectId b;s.doc.addBond(ids[i==6?0:i-1],ids[i],&b);if(i==8)s.doc.setBondOrder(b,BondOrder::Double);bonds.push_back(b);}
        ObjectId caption;s.doc.addText(Vec2(90,300),10,"keep caption",&caption);
        s.refresh();const Document source=s.doc;auto before=s.doc.digest();auto view=s.view();
        if(selection==0)for(size_t i=6;i<9;++i)s.selected.push_back(ObjectRef(ObjectKind::Atom,ids[i]));
        if(selection==1)s.selected={ObjectRef(ObjectKind::Bond,bonds[7]),ObjectRef(ObjectKind::Bond,bonds[8])};
        if(selection==2)for(auto id:ids)s.selected.push_back(ObjectRef(ObjectKind::Atom,id));
        const auto chosen=s.selected;
        // Same command dispatched by the selection's right-click menu.
        app.execute(Button("ctxClean","整理结构","","@clean"));
        if(cases==0)require(s.save(dir+"/first-local-cleanup.cdxml",true).ok(),"save first local-cleanup diagnostic");
        require(s.doc.digest()!=before,"native local cleanup changes the distorted side chain: "+app.status);
        for(auto id:ids){require(s.doc.atoms().alive(id)->element==6,"cleanup retains atom identity");if(selection<2&&std::find(ids.begin(),ids.begin()+6,id)!=ids.begin()+6)
            require(s.doc.atoms().alive(id)->position==source.atoms().alive(id)->position,"unselected ring atoms remain exactly fixed");}
        for(size_t i=0;i<6;++i)for(size_t j=0;j<6;++j)require(length((s.doc.atoms().alive(ids[i])->position-s.doc.atoms().alive(ids[j])->position)-(source.atoms().alive(ids[i])->position-source.atoms().alive(ids[j])->position))<1e-5,"full cleanup preserves benzene orientation, including mirrored and rotated inputs");
        for(auto id:bonds){auto a=s.doc.bonds().alive(id),old=source.bonds().alive(id);require(a&&a->order==old->order&&a->beginAtom==old->beginAtom&&a->endAtom==old->endAtom,"cleanup retains connectivity and bond orders");
            auto distance=length(s.doc.atoms().alive(a->beginAtom)->position-s.doc.atoms().alive(a->endAtom)->position);
            require(std::abs(distance-30)<1e-5,"selected chain and boundary bonds receive normal lengths: case="+std::to_string(cases)+" bond="+std::to_string(id)+" distance="+std::to_string(distance));}
        require(s.doc.atoms().alive(unrelated)->position==source.atoms().alive(unrelated)->position&&s.doc.texts().alive(caption)->position==source.texts().alive(caption)->position,"unselected molecule and caption stay fixed");
        require(s.selected==chosen&&s.view().center==view.center&&s.view().scale==view.scale,"cleanup preserves selection and viewport");
        if(turn==0&&mirror==1&&(selection==0||selection==2)){auto stem=selection==0?"local-side-chain-cleanup":"whole-pose-cleanup";require(s.save(dir+"/"+stem+".cdxml",true).ok(),"save local cleanup example");s.selected.clear();s.fit();app.invalidate();app.screenshot(dir+"/"+stem+".png");}
        const auto after=s.doc.digest();require(s.undo().ok()&&s.doc.digest()==before&&s.undo(true).ok()&&s.doc.digest()==after,"local cleanup has exact single-step undo and redo");++cases;
    }
    std::ofstream(dir+"/local-cleanup-checks.json")<<"{\"cases\":"<<cases<<",\"native_context_command\":true,\"partial_atoms_and_bonds\":true,\"unselected_coordinates_exact\":true,\"whole_ring_orientation\":true,\"rotation_and_mirror\":true,\"normal_boundary_bonds\":true,\"undo_redo\":true}";
}
