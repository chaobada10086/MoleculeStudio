#pragma once
void App::textExtendCheck(const std::string& dir){
    auto require=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    auto pump=[](){MSG message;int count=0;while(PeekMessageW(&message,0,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);if(++count>10000)throw std::runtime_error("text input notification loop");}};
    SetWindowPos(h,0,0,0,1200,900,SWP_NOMOVE|SWP_NOZORDER);scale=1;layout();
    s.cancel();s.doc=Document();s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());s.refresh();
    auto view=s.view();view.scale=1;view.center=Vec2();s.setView(view);
    require(SendMessageW(edit,EM_GETLIMITTEXT,0,0)==65536,"retain the 65536 character input limit");
    std::wstring expected=L"中文横向延伸 abc ";for(int i=0;i<500;++i)expected+=L"abcdefghi ";
    for(bool existing:{false,true}){
        ObjectId id=kInvalidObjectId;Vec2 anchor=s.view().toDocument(Vec2(80,120));
        if(existing){require(s.doc.addText(anchor,10,"Old box",&id).ok(),"create existing boxed caption");s.refresh();openText(id);}
        else{textEditor.begin(kInvalidObjectId,kInvalidObjectId,anchor,true,0);textBackdrop.clear();}
        require(textEditor.widthLimit==0,"unbounded new and existing text extend horizontally");
        textEditor.select(0,-1);SendMessageW(edit,EM_REPLACESEL,TRUE,(LPARAM)L"abc");pump();
        {POINT scroll={};SendMessageW(edit,EM_GETSCROLLPOS,0,(LPARAM)&scroll);require(scroll.x==0,"short text stays fully visible as the editor grows");}
        auto original=s.doc.digest();textEditor.select(0,-1);SendMessageW(edit,EM_REPLACESEL,TRUE,(LPARAM)expected.c_str());pump();
        require(textEditor.value()==expected,"all Chinese and Latin input retained");
        require(SendMessageW(edit,EM_GETLINECOUNT,0,0)==1,"long caption does not wrap at box or window width");
        require(s.doc.digest()==original,"typing remains a draft until commit");
        auto end=LONG(expected.size());textEditor.select(end,end);SendMessageW(edit,WM_CHAR,L'Z',1);pump();
        auto wanted=expected+L"Z";require(textEditor.value()==wanted,"can keep typing after passing the visible window edge");
        RECT viewport;GetClientRect(edit,&viewport);POINTL caret={};SendMessageW(edit,EM_POSFROMCHAR,(WPARAM)&caret,LONG(wanted.size()));
        POINT scroll={};SendMessageW(edit,EM_GETSCROLLPOS,0,(LPARAM)&scroll);
        require(scroll.x>0,"long horizontal input scrolls with the caret");
        require(caret.x>=0&&caret.x<=viewport.right+2,"end caret remains in the visible editor");
        RECT editorBox,canvasBox;GetWindowRect(edit,&editorBox);GetWindowRect(canvas,&canvasBox);
        require(editorBox.right<=canvasBox.right&&editorBox.left>=canvasBox.left,"native editor remains within the canvas");
        // Home/End must navigate the same full logical line after scrolling.
        textEditor.select(0,0);textEditor.resetScroll();SendMessageW(edit,EM_GETSCROLLPOS,0,(LPARAM)&scroll);require(scroll.x==0,"return to start of long text");
        textEditor.select(LONG(wanted.size()),LONG(wanted.size()));textEditor.resetScroll();
        // Explicit line breaks are the only automatic-layout break points.
        SendMessageW(edit,EM_REPLACESEL,TRUE,(LPARAM)L"\r第二行");pump();wanted+=L"\r第二行";
        require(SendMessageW(edit,EM_GETLINECOUNT,0,0)==2,"manual newline stays a newline");
        textEditor.select(0,2);textEditor.toggle(kBold);auto nativeRuns=textEditor.runs();
        require(!nativeRuns.empty()&&(nativeRuns.front().flags&kBold),"formatting survives the scrolled editor");
        require(finishText(true),"commit extending caption");require(s.selected.size()==1,"committed caption selected");
        auto committed=s.selected[0].id;std::string plain;for(auto r:s.doc.runs(committed))plain+=r.utf8;
        std::wstring normalized=wanted;std::replace(normalized.begin(),normalized.end(),L'\r',L'\n');
        require(plain==U(normalized),"commit preserves every character and explicit line break");
        require(!s.doc.extensions(ObjectRef(ObjectKind::Text,committed)).findValue("studio.text.width"),"saved caption has no forced wrapping width");
        auto after=s.doc.digest();require(s.undo().ok()&&s.doc.digest()==original,"one undo restores previous caption");require(s.undo(true).ok()&&s.doc.digest()==after,"redo restores complete extending text");
        auto path=dir+(existing?"/existing-extended.cdxml":"/new-extended.cdxml");require(s.save(path,true).ok(),"save long caption");
        Session reopened;require(reopened.open(path).ok(),"reopen extending caption");bool found=false;for(auto& t:reopened.doc.texts().records())if(t.isAlive()){std::string value;for(auto r:reopened.doc.runs(t.id))value+=r.utf8;if(value==plain)found=true;}require(found,"save and reload retain full long caption");
        openText(committed);pump();require(textEditor.value()==wanted,"re-edit restores the complete input");require(finishText(false),"cancel unchanged re-edit");
    }
    // A dragged creation rectangle has an explicit width and wraps long input.
    s.activateCommand("chooseEdittingTool_Text");auto a=s.view().toDevice(Vec2(-100,30)),b=s.view().toDevice(Vec2(-60,30));
    SendMessageW(canvas,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(int(a.x),int(a.y)));SendMessageW(canvas,WM_LBUTTONUP,0,MAKELPARAM(int(b.x),int(b.y)));
    require(textEditor.active&&std::abs(textEditor.widthLimit-40)<2,"drag-created box preserves requested width");
    SendMessageW(edit,EM_REPLACESEL,TRUE,(LPARAM)L"A fixed width text box wraps text and preserves 中文");pump();
    require(SendMessageW(edit,EM_GETLINECOUNT,0,0)>1,"fixed box wraps long input");require(finishText(true),"commit fixed box");
    auto boxed=s.selected.front().id;openText(boxed);pump();require(std::abs(textEditor.widthLimit-40)<2,"re-edit preserves fixed width");finishText(false);
    require(SendMessageW(edit,EM_GETLIMITTEXT,0,0)==65536,"input limit remains unchanged after editing");
    std::ofstream(dir+"/text-extend-checks.json")<<"{\"character_limit\":65536,\"unbounded_captions_extend\":true,\"drag_created_box_wraps\":true,\"long_input_characters\":"<<expected.size()<<",\"no_automatic_wrap\":true,\"caret_follows_input\":true,\"manual_newlines\":true,\"rich_formatting\":true,\"undo_redo\":true,\"save_reload_reedit\":true}";
    {bool originalCompletion=chemcompletion::enabled();chemcompletion::enabled()=true;
     ObjectId carbon,nitro,bond;s.doc.addAtom(Vec2(70,50),6,&carbon);s.doc.addAtom(Vec2(57.5292,57.2),6,&nitro);s.doc.addBond(carbon,nitro,&bond);s.refresh();
     openText(kInvalidObjectId,nitro);textEditor.select(0,-1);SendMessageW(edit,EM_REPLACESEL,TRUE,(LPARAM)L"no2");pump();require(finishText(true),"commit nitro input");
     require(s.doc.atomLabel(nitro)=="NO2","nitro normalized on commit");auto labelRuns=studioatom::runs(s.doc,nitro);require(!labelRuns.empty()&&(labelRuns[0].flags&kFormula),"atom digits use chemical formula typography");
     require(s.undo().ok(),"undo nitro label");require(s.undo(true).ok()&&s.doc.atomLabel(nitro)=="NO2","redo nitro label");chemcompletion::enabled()=originalCompletion;
     std::ofstream(dir+"/nitro-input-check.json")<<"{\"lowercase_completion\":true,\"formula_subscript\":true,\"undo_redo\":true}";}
    s.dirty=false;PostMessageW(h,WM_CLOSE,0,0);
}
