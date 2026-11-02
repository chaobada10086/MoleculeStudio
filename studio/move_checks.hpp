#pragma once
void App::moveCheck(const std::string& dir){
    auto require=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};
    SetWindowPos(h,0,0,0,1400,900,SWP_NOMOVE|SWP_NOZORDER);scale=1;layout();
    s.cancel();s.doc=Document();s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());s.refresh();
    s.activateCommand("chooseRingTool_Benzene");s.gesture(PointerPhase::Down,Vec2());s.gesture(PointerPhase::Up,Vec2());
    auto molecule=s.all();ObjectId text,path;
    s.doc.addText(Vec2(-30,45),14,"Multiline text movement test\nSecond line with chemistry H2O\nThird line and a final line\nEnd of text",&text);
    PathRecord shape;shape.points={Vec2(-55,-35),Vec2(75,70),Vec2(-55,70)};s.doc.addPath(shape,&path);s.refresh();
    auto view=s.view();view.scale=3;view.center=Vec2(10,15);s.setView(view);s.activateCommand("chooseSelectionTool_Marquee");
    std::ofstream log(dir+"/move-check.txt");
    std::vector<std::vector<ObjectRef>> cases={molecule,{ObjectRef(ObjectKind::Text,text)},{ObjectRef(ObjectKind::Path,path)},{molecule.front()}};
    for(size_t test=0;test<cases.size();++test)for(int legacy=1;legacy>=0;--legacy){
        _putenv_s("MOLECULE_STUDIO_MOVE_LEGACY",legacy?"1":"");
        s.selected=cases[test];auto box=studioio::selectionBounds(s);Vec2 start=(box.min+box.max)*.5;
        if(test==3)start=s.doc.atoms().get(molecule.front().id)->position;
        auto digest=s.doc.digest();auto revision=s.serial;auto builds=s.rebuilds;
        s.gesture(PointerPhase::Down,start);require(s.objectDragActive(),"object drag starts");down=true;movePreviewPending=false;render();
        auto events=s.input.stats().events;double begin=now();std::vector<double> times;
        Vec2 end;
        for(int frame=0;frame<60;++frame){double t=now();
            for(int event=0;event<8;++event){int i=frame*8+event;end=s.view().toDevice(start)+Vec2((i%160)-80,(i%80)-40);
                SendMessageW(canvas,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(int(end.x),int(end.y)));
            }
            if(!legacy){lastMovePaintMs=now()-20;SendMessageW(canvas,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(int(end.x),int(end.y)));require(!movePreviewPending,"overdue preview painted during continuous input");}
            render();require(!movePreviewPending,"paint consumes latest preview");times.push_back(now()-t);
            require(s.serial==revision&&s.rebuilds==builds,"drag does not rebuild document scene");
        }
        double total=now()-begin;std::sort(times.begin(),times.end());
        log<<"case="<<test<<" legacy="<<legacy<<" events=480 frames=60 total_ms="<<total<<" per_frame_ms="<<total/60<<" p95_ms="<<times[56]<<" routed_events="<<s.input.stats().events-events<<"\n";log.flush();
        require(s.doc.digest()==digest,"drag leaves document unchanged");
        if(!legacy)require(s.input.stats().events==events,"drag skips repeated hotspot routing");
        // Release before another paint: the final pointer position must still win.
        end=end+Vec2(30,20);SendMessageW(canvas,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(int(end.x),int(end.y)));
        Vec2 finalPoint=s.view().toDocument(Vec2(int(end.x),int(end.y)));
        Session expected;expected.doc=s.doc;expected.input.bindDocument(expected.doc.id());expected.refresh();require(expected.move(s.selected,finalPoint-start).ok(),"expected movement");
        SendMessageW(canvas,WM_LBUTTONUP,0,MAKELPARAM(int(end.x),int(end.y)));
        require(s.doc.digest()==expected.doc.digest(),"release commits exact final pointer without waiting for paint");render();require(!movePreviewPending,"no stale preview after release");
        require(s.undo().ok()&&s.doc.digest()==digest,"movement is one undo step");
        s.selected=cases[test];s.gesture(PointerPhase::Down,start);down=true;movePreviewPending=true;movePreviewPoint=start+Vec2(20,10);s.cancel();down=false;render();require(s.doc.digest()==digest&&!movePreviewPending,"cancel drops queued preview");
    }
    _putenv_s("MOLECULE_STUDIO_MOVE_LEGACY","");s.dirty=false;log<<"passed\n";PostMessageW(h,WM_CLOSE,0,0);
}
