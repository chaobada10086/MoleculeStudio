#pragma once
// Runs through the same wheel handler and canvas renderer used interactively.
void App::zoomPerformanceCheck(const std::string& file){
    std::ofstream out(testOut);auto opened=s.open(file);if(!opened.ok()){out<<"checks=FAIL open="<<opened.message;return;}
    Document input=s.doc;auto refs=s.all();auto bounds=s.scene.bounds();
    for(int i=1;i<4;++i)studioio::append(input,s.doc,refs,Vec2((i%2)*(bounds.width()+30),(i/2)*(bounds.height()+30)),nullptr);
    s.refresh();s.activateCommand("chooseSelectionTool_Marquee");s.dirty=false;s.fit();
    out<<"atoms="<<s.doc.stats().aliveAtoms<<" primitives="<<s.scene.size()<<"\n";
    bool allOk=true;
    for(bool selected:{false,true}){
        if(selected)s.selected=s.all();else s.selected.clear();
        // Text formatting synchronization used to expand this entire selection
        // for every wheel message, even with the text ribbon closed.
        ribbonTab=selected?2:0;layout();s.fit();updateInfo();invalidate();UpdateWindow(h);UpdateWindow(canvas);render();
        const auto initial=s.view();const auto document=s.doc.digest();auto serial=s.serial,rebuild=s.rebuilds;auto history=s.history.cursor();auto formula=formulaBuilds;auto chrome=zoomChromeSyncs;auto events=zoomEvents;
        POINT screen={int(s.renderView.widthPx*.53),int(s.renderView.heightPx*.43)};ClientToScreen(canvas,&screen);
        const int count=480;double eventMs=0,paintMs=0;bool anchors=true;std::vector<double> samples;
        for(int i=0;i<count;++i){
            // Fractional wheel deltas exercise high-resolution mice/trackpads.
            int delta=((i/24)%2?-1:1)*30;POINT client=screen;ScreenToClient(canvas,&client);Vec2 pixel(client.x,client.y);auto anchor=s.view().toDocument(pixel);
            auto start=now();studio_i18n::sendMessage(canvas,WM_MOUSEWHEEL,MAKEWPARAM(0,SHORT(delta)),MAKELPARAM(screen.x,screen.y));double elapsed=now()-start;eventMs+=elapsed;samples.push_back(elapsed);
            anchors=anchors&&length(s.view().toDocument(pixel)-anchor)<1e-7;
            if(i%8==7){start=now();UpdateWindow(canvas);paintMs+=now()-start;}
        }
        studio_i18n::sendMessage(canvas,WM_TIMER,7,0);UpdateWindow(canvas);
        bool unchanged=s.doc.digest()==document&&s.serial==serial&&s.rebuilds==rebuild&&s.history.cursor()==history&&formulaBuilds==formula;
        bool roundtrip=std::abs(s.view().scale-initial.scale)<1e-9&&length(s.view().center-initial.center)<1e-7;
        allOk=allOk&&anchors&&unchanged&&roundtrip&&zoomEvents-events==count;
        std::sort(samples.begin(),samples.end());
        // Compare final native canvas pixels between the legacy and optimized
        // paths at exactly the same transform, including selection handles.
        s.setView(initial);render();std::vector<uint8_t> pixels;gl->readPixels(0,0,s.renderView.widthPx,s.renderView.heightPx,&pixels);uint64_t hash=1469598103934665603ull;for(auto b:pixels){hash^=b;hash*=1099511628211ull;}
        out<<(selected?"selected":"blank")<<" events="<<count<<" event_ms="<<eventMs<<" event_avg_ms="<<eventMs/count<<" event_p95_ms="<<samples[size_t(count*.95)]<<" paint_ms="<<paintMs<<" chrome_syncs="<<zoomChromeSyncs-chrome<<" scene_rebuilds="<<s.rebuilds-rebuild<<" formula_rebuilds="<<formulaBuilds-formula<<" anchor_exact="<<anchors<<" unchanged="<<unchanged<<" roundtrip="<<roundtrip<<" pixels="<<hash<<"\n";
    }
    ribbonTab=0;s.selected.clear();s.dirty=false;out<<"checks="<<(allOk?"PASS":"FAIL")<<"\n";
}
