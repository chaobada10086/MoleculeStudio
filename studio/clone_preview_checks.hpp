#pragma once
// Exercise actual canvas messages and rendering, including the zero-offset
// first frame that used to pin the cached copy underneath its source.
void App::clonePreviewCheck(const std::string& dir){
    auto require=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};
    try{
        s.cancel();s.doc=Document();s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());s.refresh();
        s.activateCommand("chooseRingTool_Benzene");s.gesture(PointerPhase::Down,Vec2());s.gesture(PointerPhase::Up,Vec2());
        s.submit(commands::setElements({0},7),"preview label");
        s.selected=s.all();s.activateCommand("chooseSelectionTool_Marquee");
        auto view=s.view();view.scale=2;view.center=Vec2();s.setView(view);
        auto before=s.doc.digest();const size_t entries=s.history.cursor();
        std::vector<Vec2> originals;for(const auto& atom:s.doc.atoms().records())if(atom.isAlive())originals.push_back(atom.position);
        auto box=studioio::selectionBounds(s);Vec2 start=s.view().toDevice((box.min+box.max)*.5);
        auto at=[](Vec2 p){return MAKELPARAM(int(p.x),int(p.y));};
        auto pixels=[&](){std::vector<uint8_t> p;require(gl->readPixels(0,0,s.renderView.widthPx,s.renderView.heightPx,&p).ok(),"read pixels");return p;};
        render();auto initial=pixels();
        SendMessageW(canvas,WM_LBUTTONDOWN,MK_LBUTTON|MK_CONTROL,at(start));require(cloneDragging&&cloneSource,"Ctrl down starts copy preview");
        render();auto zero=pixels();
#ifdef MOLECULE_STUDIO_DX11
        dx11::Renderer reference;std::string why;require(reference.initialize(&why),"reference renderer");
#endif
        std::vector<uint8_t> previous=zero;
        std::ofstream log(dir+"/checks.txt");
        int frame=0;
        for(auto delta:{Vec2(150,70),Vec2(240,120),Vec2(190,10)}){
            const bool shifted=frame==2;Vec2 point=start+delta;
            double t=now();SendMessageW(canvas,WM_MOUSEMOVE,MK_LBUTTON|MK_CONTROL|(shifted?MK_SHIFT:0),at(point));render();auto actual=pixels();
            require(actual!=previous,"copy preview follows every mouse move");
            require(s.doc.digest()==before&&s.history.cursor()==entries,"preview must not mutate document or undo history");
            Vec2 expectedDelta=s.view().toDocument(Vec2(int(point.x),int(point.y)))-clonePress;
            if(shifted)expectedDelta.y=0;
            require(core::length(cloneDelta-expectedDelta)<1e-8,"preview displacement and Shift constraint");
#ifdef MOLECULE_STUDIO_DX11
            auto overlay=s.overlay();auto expected=overlay.scene();
            for(auto p:cloneSource->scene.primitives()){
                for(auto& pt:p.points)pt=pt+expectedDelta;
                for(auto& sub:p.subpaths)for(auto& pt:sub.points)pt=pt+expectedDelta;
                p.bounds.min=p.bounds.min+expectedDelta;p.bounds.max=p.bounds.max+expectedDelta;
                p.source=ObjectRef();p.version=0;p.cacheKey=0;expected.add(p);
            }
            render::RenderStats stats;require(reference.draw(s.gpuScene,&expected,s.renderView,&stats).ok(),"reference preview draw");
            std::vector<uint8_t> expectedPixels;require(reference.readPixels(0,0,s.renderView.widthPx,s.renderView.heightPx,&expectedPixels).ok(),"reference pixels");
            require(actual==expectedPixels,"live frame differs from fresh preview renderer");
#endif
            screenshot(dir+"/preview-"+std::to_string(frame)+".png",false,false);
            log<<"frame="<<frame<<" changed=1 reference_equal=1 total_ms="<<(now()-t)<<"\n";log.flush();
            previous=actual;++frame;
        }
        const Vec2 drop=start+Vec2(190,10);const Vec2 committedDelta=cloneDelta;
        SendMessageW(canvas,WM_LBUTTONUP,MK_CONTROL|MK_SHIFT,at(drop));
        require(!cloneDragging&&!cloneSource,"drop clears preview");require(s.doc.stats().aliveAtoms==originals.size()*2,"drop copies the structure once");
        for(size_t i=0;i<originals.size();++i){require(s.doc.atoms().get(ObjectId(i))->position==originals[i],"original stays put");require(core::length(s.doc.atoms().get(ObjectId(i+originals.size()))->position-originals[i]-committedDelta)<1e-8,"drop matches preview position");}
        auto copied=s.doc.digest();require(s.undo().ok()&&s.doc.digest()==before,"one-step undo");require(s.undo(true).ok()&&s.doc.digest()==copied,"redo copy");
        s.selected=s.all();box=studioio::selectionBounds(s);start=s.view().toDevice((box.min+box.max)*.5);
        SendMessageW(canvas,WM_LBUTTONDOWN,MK_LBUTTON|MK_CONTROL,at(start));require(cloneDragging,"second copy drag");
        SendMessageW(canvas,WM_MOUSEMOVE,MK_LBUTTON|MK_CONTROL,at(start+Vec2(100,50)));render();
        SendMessageW(canvas,WM_KEYDOWN,VK_ESCAPE,0);require(!cloneDragging&&!cloneSource&&s.doc.digest()==copied,"Escape cancels without inserting");
        log<<"shift_constraint=1 original_unchanged=1 drop_matches_preview=1 undo_redo=1 escape_cancel=1 passed=1\n";
        s.dirty=false;
    }catch(const std::exception& e){std::ofstream(dir+"/failure.txt")<<e.what();s.dirty=false;}
}
