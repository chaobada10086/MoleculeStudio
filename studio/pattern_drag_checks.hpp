#pragma once
// Headless coverage for the drag-to-size pattern insertion path. The canvas
// mouse handlers only set patternStart/patternEnd and call these methods.
static void patternDragCheck(App& app,const std::string& dir){
    auto require=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    auto bounds=[](const PathRecord& path){
        Box box(path.points.front(),path.points.front());
        for(auto point:path.points)box.add(point);
        return box;
    };
    auto& s=app.s;
    s.cancel();s.doc=Document();s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());s.refresh();
    auto pathCount=[&](){size_t n=0;for(const auto& path:s.doc.paths().records())if(path.isAlive())++n;return n;};
    auto pathAt=[&](size_t index)->const PathRecord*{size_t n=0;for(const auto& path:s.doc.paths().records())if(path.isAlive()){if(n++==index)return &path;}return nullptr;};

    // Drag from (10,20) to (106,84): the pattern fills that 96 x 64 box.
    app.insertAutoShape(0);
    require(app.patternPlacing,"choosing a pattern enters placement mode");
    app.patternStart=Vec2(10,20);app.patternEnd=Vec2(106,84);
    app.updatePatternPreview();
    require(s.ghost.items().size()==1,"drag shows a live pattern preview");
    app.finishPatternPlacement(Vec2(106,84));
    require(!app.patternPlacing&&s.ghost.items().empty(),"release leaves placement mode and clears preview");
    require(pathCount()==1,"drag inserts one pattern");
    const PathRecord* first=pathAt(0);
    require(first&&first->points.size()==4,"rectangle pattern has four corners");
    auto firstBox=bounds(*first);
    require(std::abs(firstBox.width()-96)<1e-6&&std::abs(firstBox.height()-64)<1e-6,"pattern follows the drag box");
    require(std::abs(firstBox.min.x-10)<1e-6&&std::abs(firstBox.min.y-20)<1e-6,"pattern starts at the drag corner");

    // A click without a drag uses the 96 x 64 default, centred on the click.
    app.insertAutoShape(4);
    app.patternStart=Vec2(0,0);
    app.finishPatternPlacement(Vec2(0,0));
    require(pathCount()==2,"click inserts a default-sized pattern");
    const PathRecord* second=pathAt(1);
    require(second,"second pattern exists");
    auto secondBox=bounds(*second);
    require(std::abs(secondBox.width()-96)<1e-6&&std::abs(secondBox.height()-64)<1e-6,"default click keeps the default size");
    require(std::abs(secondBox.min.x+48)<1e-6&&std::abs(secondBox.min.y+32)<1e-6,"default click centres on the click");

    std::ofstream(dir+"/pattern_drag_checks.json")<<"{\"placement_mode\":true,\"live_preview\":true,\"drag_size\":true,\"drag_origin\":true,\"click_default_size\":true,\"click_centred\":true}";
}

// Exercise the real mouse handlers and renderer, including release before paint.
void App::patternPerformanceCheck(const std::string& dir){
    App& app=*this;
    auto require=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};
    auto& s=app.s;
    SetWindowPos(app.h,0,0,0,1400,900,SWP_NOMOVE|SWP_NOZORDER);app.scale=1;app.layout();
    std::ofstream log(dir+"/pattern-performance.txt");
    for(int count:{0,4000}){
        s.cancel();s.doc=Document();s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());
        for(int i=0;i<count;++i){PathRecord p;double x=(i%80)*16,y=(i/80)*16;p.closed=true;p.points={Vec2(x,y),Vec2(x+10,y),Vec2(x+5,y+10)};ObjectId id;s.doc.addPath(p,&id);}
        s.refresh();s.fit();s.dirty=false;
        for(int legacy=1;legacy>=0;--legacy){
            _putenv_s("MOLECULE_STUDIO_PATTERN_LEGACY",legacy?"1":"");
            app.insertAutoShape(4);
            SendMessageW(app.canvas,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(140,100));
            auto digest=s.doc.digest();auto serial=s.serial;auto builds=s.rebuilds;auto history=s.history.cursor();
            auto chrome=app.lastChrome,status=app.lastStatus;
            app.render();double eventMs=0;std::vector<double> frames;
            for(int frame=0;frame<60;++frame){double begin=App::now();
                for(int event=0;event<8;++event){int i=frame*8+event;
                    SendMessageW(app.canvas,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(220+i%180,170+i%90));
                }
                eventMs+=App::now()-begin;
                if(!legacy)require(app.patternPreviewPending,"moves queue preview until paint");
                app.render();frames.push_back(App::now()-begin);
                require(!app.patternPreviewPending&&s.ghost.items().size()==1,"paint consumes preview");
                const auto& shape=presentationCatalog()[4];const auto& points=s.ghost.items().front().points;
                Vec2 origin(std::min(app.patternStart.x,app.patternEnd.x),std::min(app.patternStart.y,app.patternEnd.y));
                for(size_t j=0;j<shape.points.size();++j){auto expected=origin+Vec2(shape.points[j].x*std::abs(app.patternEnd.x-app.patternStart.x),shape.points[j].y*std::abs(app.patternEnd.y-app.patternStart.y));require(std::hypot(points[j].x-expected.x,points[j].y-expected.y)<1e-9,"preview geometry matches pointer");}
                require(s.serial==serial&&s.rebuilds==builds,"preview does not rebuild document");
            }
            require(s.doc.digest()==digest&&s.history.cursor()==history&&!s.dirty,"preview leaves document and undo unchanged");
            if(!legacy)require(app.lastChrome==chrome&&app.lastStatus==status,"drag skips shell synchronization");
            double total=0;for(double t:frames)total+=t;std::sort(frames.begin(),frames.end());
            log<<"paths="<<count<<" legacy="<<legacy<<" events=480 frames=60 event_ms="<<eventMs<<" mean_frame_ms="<<total/60<<" median_ms="<<frames[30]<<" p95_ms="<<frames[56]<<"\n";log.flush();
            // The release point wins even if no paint occurred for the last move.
            SendMessageW(app.canvas,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(420,280));
            SendMessageW(app.canvas,WM_LBUTTONUP,0,MAKELPARAM(460,310));
            require(!app.patternPlacing&&!app.patternPreviewPending&&s.ghost.items().empty(),"release clears queued preview");
            require(s.history.cursor()==history+1&&s.selected.size()==1,"insertion is one undo step");
            const auto* path=s.doc.paths().alive(s.selected.front().id);require(path,"inserted path exists");
            Vec2 end=s.view().toDocument(Vec2(460,310));const auto& shape=presentationCatalog()[4];
            for(size_t j=0;j<path->points.size();++j){auto expected=app.patternStart+Vec2(shape.points[j].x*(end.x-app.patternStart.x),shape.points[j].y*(end.y-app.patternStart.y));require(std::hypot(path->points[j].x-expected.x,path->points[j].y-expected.y)<1e-9,"release uses final coordinates");}
            require(s.undo().ok()&&s.doc.digest()==digest,"undo restores original document");s.selected.clear();s.dirty=false;
            for(int cancel=0;cancel<3;++cancel){
                app.insertAutoShape(0);SendMessageW(app.canvas,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(100,100));
                SendMessageW(app.canvas,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(300,220));
                if(cancel==0)SendMessageW(app.canvas,WM_KEYDOWN,VK_ESCAPE,0);
                else if(cancel==1)ReleaseCapture();else SendMessageW(app.canvas,WM_KILLFOCUS,0,0);
                app.render();require(!app.patternPlacing&&!app.patternPreviewPending&&s.ghost.items().empty()&&s.doc.digest()==digest,"cancel clears pending preview without edits");
            }
        }
    }
    size_t comparisons=0;
    for(size_t shape:{size_t(0),size_t(4),presentationCatalog().size()-1})for(int dx:{-1,1})for(int dy:{-1,1}){
        app.insertAutoShape(shape);SendMessageW(app.canvas,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(400,240));
        std::vector<uint8_t> oldPixels,newPixels;
        _putenv_s("MOLECULE_STUDIO_PATTERN_LEGACY","1");
        SendMessageW(app.canvas,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(400+dx*120,240+dy*90));app.render();
        require(app.gl->readPixels(0,0,s.renderView.widthPx,s.renderView.heightPx,&oldPixels).ok(),"read legacy preview pixels");
        _putenv_s("MOLECULE_STUDIO_PATTERN_LEGACY","");
        SendMessageW(app.canvas,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(400+dx*120,240+dy*90));app.render();
        require(app.gl->readPixels(0,0,s.renderView.widthPx,s.renderView.heightPx,&newPixels).ok(),"read optimized preview pixels");
        require(oldPixels==newPixels,"optimized preview pixels match legacy in every drag direction");++comparisons;
        app.cancelPatternPlacement();
    }
    _putenv_s("MOLECULE_STUDIO_PATTERN_LEGACY","");s.dirty=false;log<<"pixel_comparisons="<<comparisons<<"\npassed\n";PostMessageW(app.h,WM_CLOSE,0,0);
}
