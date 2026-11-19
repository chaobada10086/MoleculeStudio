#pragma once
void App::insertionPerformanceCheck(const std::string& dir){
    auto require=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};
    SetWindowPos(h,0,0,0,1400,900,SWP_NOMOVE|SWP_NOZORDER);scale=1;layout();
    std::ofstream log(dir+"/insertion-performance.txt");
    s.cancel();s.doc=Document();s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());
    if(auto file=std::getenv("MOLECULE_STUDIO_INSERT_FILE")){require(s.open(file).ok(),"open insertion fixture");}
    else{for(int i=0;i<4000;++i){PathRecord p;p.closed=true;double x=(i%80)*16,y=(i/80)*16;p.points={Vec2(x,y),Vec2(x+10,y),Vec2(x+5,y+10)};ObjectId id;s.doc.addPath(p,&id);}s.refresh();s.fit();}
    s.selected.clear();s.dirty=false;
    log<<"scene="<<s.gpuScene.size()<<" backend="<<gl->name()<<"\n";
    size_t comparisons=0;
    for(const char* command:{"chooseDrawingTool_Rectangle_Hollow","chooseDrawingTool_Oval_Hollow","chooseArrowTool_Large_Solid","chooseArrowTool_Large_Equilibrium","chooseBracketTool_CurlyBrackets"}){
        std::vector<std::vector<uint8_t>> reference;uint64_t expectedDigest=0;
        for(int legacy=1;legacy>=0;--legacy){
            _putenv_s("MOLECULE_STUDIO_INSERT_LEGACY",legacy?"1":"");
            require(s.activateCommand(command),"activate drawing tool");
            auto digest=s.doc.digest();auto history=s.history.cursor();auto serial=s.serial;auto rebuild=s.rebuilds;
            double pressStart=now();SendMessageW(canvas,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(180,120));render();log<<"press_ms="<<now()-pressStart<<"\n";log.flush();
            double eventMs=0,overlayMs=0,drawMs=0,preMs=0,guidesMs=0;std::vector<double> times;
            size_t hits=dx11Renderer?dx11Renderer->insertionRasterHits():0;
            for(int frame=0;frame<40;++frame){double start=now();
                for(int event=0;event<8;++event){int i=frame*8+event;WPARAM mods=MK_LBUTTON|(frame>=10&&frame<20?MK_SHIFT:0);SendMessageW(canvas,WM_MOUSEMOVE,mods,MAKELPARAM(280+i%180,170+i%90));}
                eventMs+=now()-start;
                if(!legacy)require(insertionPreviewPending,"graphics moves deferred to paint");
                render();times.push_back(now()-start);overlayMs+=lastOverlayMs;drawMs+=lastDrawMs;preMs+=lastPreMs;guidesMs+=lastGuidesMs;
                require(s.serial==serial&&s.rebuilds==rebuild,"preview never rebuilds document");
                if(frame==0||frame==19||frame==39){std::vector<uint8_t> pixels;require(gl->readPixels(0,0,s.renderView.widthPx,s.renderView.heightPx,&pixels).ok(),"read insertion preview pixels");if(legacy)reference.push_back(std::move(pixels));else{require(pixels==reference[frame==0?0:frame==19?1:2],"optimized insertion preview pixels equal original");++comparisons;}}
            }
            require(s.doc.digest()==digest&&s.history.cursor()==history,"preview leaves document unchanged");
            size_t rasterHits=(dx11Renderer?dx11Renderer->insertionRasterHits():0)-hits;
            if(!legacy&&dx11Renderer){log<<"cache_hits="<<rasterHits<<" miss_why="<<dx11Renderer->insertionRasterMissWhy()<<" down="<<down<<" fast="<<fastGraphicTool()<<"\n";log.flush();}
            if(!legacy&&dx11Renderer&&lastStats.triangles>200000)require(rasterHits>=39,"stationary GPU document raster reused");
            double total=0;for(double t:times)total+=t;std::sort(times.begin(),times.end());
            log<<command<<" legacy="<<legacy<<" event_ms="<<eventMs<<" mean_ms="<<total/40<<" median_ms="<<times[20]<<" p95_ms="<<times[37]<<" pre_ms="<<preMs/40<<" overlay_ms="<<overlayMs/40<<" draw_ms="<<drawMs/40<<" guides_ms="<<guidesMs/40<<" raster_hits="<<rasterHits<<"\n";log.flush();
            // No paint between the last move and release: commit the release point.
            SendMessageW(canvas,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(450,290));double releaseStart=now();SendMessageW(canvas,WM_LBUTTONUP,0,MAKELPARAM(480,320));log<<"release_ms="<<now()-releaseStart<<"\n";log.flush();
            require(!insertionPreviewPending&&s.history.cursor()==history+1,"release commits once and drops queued move");
            auto resultDigest=s.doc.digest();if(legacy)expectedDigest=resultDigest;else require(resultDigest==expectedDigest,"release geometry and style match original including exact final pointer");
            if(!legacy){
                std::vector<uint8_t> partial,full;render();require(gl->readPixels(0,0,s.renderView.widthPx,s.renderView.heightPx,&partial).ok(),"read locally updated graphics");s.refresh();render();require(gl->readPixels(0,0,s.renderView.widthPx,s.renderView.heightPx,&full).ok(),"read full scene reference");require(partial==full,"local insertion rebuild preserves painter order and pixels");++comparisons;
            }
            auto committed=s.doc.digest();require(s.undo().ok()&&s.doc.digest()==digest,"insertion atomic undo");require(s.undo(true).ok()&&s.doc.digest()==committed,"insertion redo");require(s.undo().ok(),"reset fixture");s.selected.clear();s.dirty=false;
            for(int cancellation=0;cancellation<3;++cancellation){require(s.activateCommand(command),"reactivate tool");SendMessageW(canvas,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(180,120));SendMessageW(canvas,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(400,240));
                if(cancellation==0)SendMessageW(canvas,WM_KEYDOWN,VK_ESCAPE,0);else if(cancellation==1)ReleaseCapture();else SendMessageW(canvas,WM_KILLFOCUS,0,0);
                render();require(!down&&!insertionPreviewPending&&s.doc.digest()==digest,"cancel discards queued graphic preview");}
        }
    }
    _putenv_s("MOLECULE_STUDIO_INSERT_LEGACY","");
    require(s.activateCommand("chooseArrowTool_Large_Solid"),"activate invalidation fixture");SendMessageW(canvas,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(180,120));SendMessageW(canvas,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(400,240));render();
    auto referenceFrame=[&](){std::vector<uint8_t> cached,reference;render();require(gl->readPixels(0,0,s.renderView.widthPx,s.renderView.heightPx,&cached).ok(),"read refreshed raster");_putenv_s("MOLECULE_STUDIO_INSERT_LEGACY","1");render();require(gl->readPixels(0,0,s.renderView.widthPx,s.renderView.heightPx,&reference).ok(),"read uncached reference");require(cached==reference,"raster invalidation matches uncached rendering");++comparisons;_putenv_s("MOLECULE_STUDIO_INSERT_LEGACY","");};
    auto v=s.view();v.center=v.center+Vec2(15,7);s.setView(v);referenceFrame();v.scale*=1.2;s.setView(v);referenceFrame();
    SetWindowPos(h,0,0,0,1300,850,SWP_NOMOVE|SWP_NOZORDER);layout();referenceFrame();
    ObjectId label;s.doc.addText(s.view().center,14,"Raster invalidation",&label);s.refresh();referenceFrame();
    SendMessageW(canvas,WM_KEYDOWN,VK_ESCAPE,0);render();
    _putenv_s("MOLECULE_STUDIO_INSERT_LEGACY","");s.dirty=false;log<<"pixel_comparisons="<<comparisons<<"\npassed\n";PostMessageW(h,WM_CLOSE,0,0);
}
