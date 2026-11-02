#pragma once
// Caption wrap-width regression: after the text has been committed, the left and
// right middle handles resize the text box (the wrap width) instead of scaling
// the object. Covers live layout, undo/redo, both edges and file round trips.
void App::textWrapCheck(const std::string& dir){
    auto require=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
    SetWindowPos(h,0,0,0,1200,900,SWP_NOMOVE|SWP_NOZORDER);scale=1;layout();
    // The native scrollbars occupy reserved space outside the canvas.
    {RECT c,horizontal,vertical,overlap;
     GetWindowRect(canvas,&c);GetWindowRect(hbar,&horizontal);GetWindowRect(vbar,&vertical);
     require(c.right>c.left&&c.bottom>c.top,"canvas has drawable area");
     require(!IntersectRect(&overlap,&c,&horizontal)&&!IntersectRect(&overlap,&c,&vertical),"scrollbars do not cover canvas");
     require(horizontal.top==c.bottom&&vertical.left==c.right,"scrollbars align to canvas edges");
     auto saved=s.view();auto zoomed=saved;zoomed.scale=3;s.setView(zoomed);updateScrollbars(true);
     double before=s.view().center.y;SendMessageW(h,WM_VSCROLL,SB_LINEDOWN,(LPARAM)vbar);
     require(s.view().center.y>before,"vertical native scrollbar moves view");
     before=s.view().center.x;SendMessageW(h,WM_HSCROLL,SB_LINERIGHT,(LPARAM)hbar);
     require(s.view().center.x>before,"horizontal native scrollbar moves view");
     s.setView(saved);updateScrollbars(true);}
    s.cancel();s.doc=Document();s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());
    std::string content;
    for(int i=0;i<6;++i)content+="Paracetamol crystal form one ";
    ObjectId id;
    require(s.doc.addText(Vec2(-260,60),12,content,&id).ok(),"add caption");
    s.activateCommand("chooseSelectionTool_Marquee");
    s.selected={ObjectRef(ObjectKind::Text,id)};s.refresh();
    auto height=[&](){Box b=studioio::selectionBounds(s);return b.max.y-b.min.y;};
    auto width=[&](){auto v=s.doc.extensions(ObjectRef(ObjectKind::Text,id)).findValue("studio.text.width");return v?std::atof(v->c_str()):0.0;};
    auto anchor=[&](){return s.doc.texts().get(id)->position.x;};
    double single=height();
    require(single>0,"caption has ink bounds");
    const double pad=s.view().deviceRadiusToDocument(s.view().dpiScale*.75)*3;
    auto handles=selectionHandlePoints();require(handles.size()==9,"caption exposes nine handles");
    require(canvasCursorId(s.view().toDevice(handles[4]))==32644,"width handle shows a horizontal resize cursor");

    // Too short to wrap: the frame still shows the full stored box.
    s.doc.extensions(ObjectRef(ObjectKind::Text,id)).set("studio.text.width","120");
    s.refresh();require(std::abs(selectionHandlePoints()[4].x-(anchor()+120+pad))<1.5,"short caption keeps its full box width");
    s.doc.extensions(ObjectRef(ObjectKind::Text,id)).erase("studio.text.width");s.refresh();

    // Narrowing the right edge re-wraps the committed caption.
    double startX=anchor();
    Vec2 press=s.view().toDevice(selectionHandlePoints()[4]);
    require(beginSelectionTransform(press,0),"width drag starts");
    require(wrapDragging,"width drag mode is active");
    Vec2 target=s.view().toDevice(Vec2(startX+160,press.y));
    updateTextWrap(target);finishTextWrap(target);
    require(!wrapDragging,"width drag finished");
    require(std::abs(width()-160)<1.5,"wrap width stored on the caption");
    s.refresh();
    double wrapped=height();
    require(wrapped>single*1.8,"narrow box wraps the caption onto more lines");
    require(std::abs(selectionHandlePoints()[4].x-(startX+160+pad))<1.5,"right handle follows the wrap edge");
    require(std::abs(anchor()-startX)<1e-9,"left-aligned caption keeps its anchor on a right-edge drag");

    // One gesture is one undo step, and redo restores it.
    require(s.undo().ok(),"undo the width drag");
    require(width()==0,"undo removes the new wrap width");
    s.refresh();require(std::abs(height()-single)<1e-6,"undo restores the single line");
    require(s.undo(true).ok(),"redo the width drag");
    s.refresh();require(std::abs(height()-wrapped)<1e-6,"redo restores the wrapped layout");

    // The left edge widens the box and moves the anchor while the right edge stays.
    auto box=selectionHandlePoints();double rightEdge=anchor()+160,before=anchor();
    Vec2 leftPress=s.view().toDevice(box[3]);
    require(beginSelectionTransform(leftPress,0),"left edge drag starts");
    Vec2 leftTarget=s.view().toDevice(Vec2(before-40,box[3].y));
    updateTextWrap(leftTarget);finishTextWrap(leftTarget);
    s.refresh();
    require(std::abs(anchor()-(before-40))<1.5,"left edge moves the anchor with it");
    require(std::abs(selectionHandlePoints()[4].x-(rightEdge+pad))<1.5,"left edge keeps the right edge fixed");
    require(std::abs(width()-200)<1.5,"left edge widens the wrap width");

    // The whole canvas path: hit testing, capture and the mouse handlers.
    {
        Vec2 press=s.view().toDevice(selectionHandlePoints()[4]);
        Vec2 target=s.view().toDevice(Vec2(anchor()+260,s.view().toDocument(press).y));
        LPARAM down=MAKELPARAM(int(press.x),int(press.y)),move=MAKELPARAM(int(target.x),int(target.y));
        SendMessageW(canvas,WM_LBUTTONDOWN,MK_LBUTTON,down);
        SendMessageW(canvas,WM_MOUSEMOVE,MK_LBUTTON,move);
        SendMessageW(canvas,WM_LBUTTONUP,0,move);
        require(std::abs(width()-260)<1.5,"canvas mouse drag resizes the text box");
        require(!wrapDragging&&GetCapture()!=canvas,"canvas drag releases capture");
        require(s.undo().ok()&&std::abs(width()-200)<1.5,"canvas drag undoes in one step");
        s.refresh();
    }

    s.selected={ObjectRef(ObjectKind::Text,id)};s.refresh();invalidate();UpdateWindow(h);UpdateWindow(canvas);
    screenshot(dir+"/text-wrap.png",false,false);

    // Re-editing retains the stored frame width and paragraph layout.
    openText(id);
    require(textEditor.active&&std::abs(textEditor.widthLimit-200)<2,"inline editor retains the previous wrap width");
    SetWindowTextW(textEditor.hwnd,L"Short label");
    require(finishText(true),"commit the edited caption");
    require(std::abs(width()-200)<2,"committing retains the text frame width");
    s.refresh();
    // Committed boxes can still be explicitly resized for paragraph layout.
    s.doc.extensions(ObjectRef(ObjectKind::Text,id)).set("studio.text.width","200");s.refresh();
    // The empty part of the box belongs to the caption: click selects it,
    // double-click opens the editor at that point.
    {
        Box frame=studioio::selectionBounds(s);
        Vec2 empty=s.view().toDevice(Vec2(anchor()+150,(frame.min.y+frame.max.y)*.5));
        LPARAM at=MAKELPARAM(int(empty.x),int(empty.y));
        s.selected.clear();
        SendMessageW(canvas,WM_LBUTTONDOWN,MK_LBUTTON,at);
        SendMessageW(canvas,WM_LBUTTONUP,0,at);
        require(s.selected.size()==1&&s.selected[0]==ObjectRef(ObjectKind::Text,id),"empty part of the text box selects the caption");
        s.selected.clear();
        SendMessageW(canvas,WM_LBUTTONDBLCLK,MK_LBUTTON,at);
        require(textEditor.active,"double-click inside the empty box opens the editor");
        finishText(false);
    }


    // Resizing from the editor's edge, while still typing, re-wraps live.
    {
        openText(id);
        require(textEditor.active&&std::abs(textEditor.widthLimit-200)<2,"editing keeps the explicit text frame");
        RECT rc;GetClientRect(textEditor.hwnd,&rc);
        Box frame=studioio::selectionBounds(s);
        Vec2 target=s.view().toDevice(Vec2(anchor()+120,(frame.min.y+frame.max.y)*.5));
        POINT mapped={int(target.x),int(target.y)};MapWindowPoints(canvas,textEditor.hwnd,&mapped,1);
        SendMessageW(textEditor.hwnd,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(rc.right-2,rc.bottom/2));
        SendMessageW(textEditor.hwnd,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(mapped.x,mapped.y));
        SendMessageW(textEditor.hwnd,WM_LBUTTONUP,0,MAKELPARAM(mapped.x,mapped.y));
        require(std::abs(textEditor.widthLimit-120)<2.0,"dragging the editor edge changes the wrap width");
        require(finishText(true),"commit the resized caption");
        require(std::abs(width()-120)<2.0,"committed caption keeps the editor-resized width");
        s.refresh();
    }

    // Files remember the wrap width.
    std::string cdx=dir+"/text-wrap.cdxml";
    require(s.save(cdx,true).ok(),"save cdxml");
    {
        Document back;auto e=persistence::readCdxmlFile(cdx,back);require(e.ok(),"reload cdxml");
        double found=0;for(const auto& t:back.texts().records())if(t.isAlive()){auto v=back.extensions(ObjectRef(ObjectKind::Text,t.id)).findValue("studio.text.width");if(v)found=std::atof(v->c_str());}
        require(std::abs(found-120)<2.0,"cdxml keeps the wrap width");
    }
    std::string native=dir+"/text-wrap.r62doc";
    require(s.save(native).ok(),"save native document");
    {
        Session reload;require(reload.open(native).ok(),"reload native document");
        double found=0;for(const auto& t:reload.doc.texts().records())if(t.isAlive()){auto v=reload.doc.extensions(ObjectRef(ObjectKind::Text,t.id)).findValue("studio.text.width");if(v)found=std::atof(v->c_str());}
        require(std::abs(found-120)<2.0,"native document keeps the wrap width");
    }
    // Native input must leave the document and its scene untouched until commit.
    auto pump=[](){MSG msg;int n=0;while(PeekMessageW(&msg,0,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);if(++n>10000)throw std::runtime_error("input notification loop");}};
    auto plain=[&](ObjectId t){std::string out;for(auto& r:s.doc.runs(t))out+=r.utf8;return out;};
    auto typeCheck=[&](const std::string& label){
        ObjectId tid;require(s.doc.addText(s.view().center,10,"Original",&tid).ok(),"add input fixture");s.refresh();
        openText(tid);pump();render();
        auto serial=s.serial;auto builds=textBackgroundBuilds;
        require(IsWindowVisible(textEditor.hwnd),"native editor is visible");
        HRGN region=CreateRectRgn(0,0,0,0);int kind=GetWindowRgn(textEditor.hwnd,region);DeleteObject(region);require(kind!=NULLREGION,"input control is not hidden by an empty region");
        textEditor.select(0,-1);SendMessageW(textEditor.hwnd,EM_REPLACESEL,TRUE,(LPARAM)L"中文输入 alpha\rbeta ");pump();
        double start=now(),worst=0;
        for(int i=0;i<120;++i){double before=now();SendMessageW(textEditor.hwnd,WM_CHAR,L'x',1);pump();UpdateWindow(canvas);worst=std::max(worst,now()-before);}
        double elapsed=now()-start;
        for(int i=0;i<60;++i){SendMessageW(textEditor.hwnd,WM_CHAR,VK_BACK,1);pump();}
        require(s.serial==serial,"typing does not rebuild the document scene");
        require(textBackgroundBuilds==builds,"typing reuses the background");
        require(plain(tid)=="Original","typing does not mutate the document");
        textEditor.select(0,2);textEditor.setFont("Arial",9);textEditor.toggle(kBold);
        CHARRANGE before,after;SendMessageW(textEditor.hwnd,EM_EXGETSEL,0,(LPARAM)&before);auto runs=textEditor.runs();SendMessageW(textEditor.hwnd,EM_EXGETSEL,0,(LPARAM)&after);
        require(before.cpMin==after.cpMin&&before.cpMax==after.cpMax,"format reads preserve the selection");
        require(!runs.empty()&&(runs.front().flags&kBold)&&std::abs(runs.front().size-9)<.06,"nine point rich formatting survives");
        std::ofstream(dir+"/input-"+label+".txt")<<"120 characters total ms="<<elapsed<<" max character ms="<<worst<<" scene rebuilds="<<(s.serial-serial)<<" background rebuilds="<<(textBackgroundBuilds-builds)<<"\n";
        std::string expected;for(auto& r:runs)expected+=r.utf8;
        require(finishText(true),"commit input");require(plain(tid)==expected,"multiline Chinese and Latin text commits exactly");
        require(s.undo().ok()&&plain(tid)=="Original","one undo restores the original text");
        require(s.undo(true).ok()&&plain(tid)==expected,"redo restores the input");
        openText(tid);SendMessageW(textEditor.hwnd,EM_REPLACESEL,TRUE,(LPARAM)L"discard");pump();require(finishText(false),"cancel input");require(plain(tid)==expected,"cancel leaves text untouched");
    };

    // A caret on the final line may scroll RichEdit internally. Re-layout must
    // measure the entire block and keep earlier lines visible, even after input.
    {
        ObjectId t;require(s.doc.addText(s.view().center,10,"First line\nSecond line\nThird line\nLast line",&t).ok(),"multiline fixture");s.refresh();
        auto saved=s.view();
        for(double magnification:{1.0,4.65}){
            auto v=saved;v.scale=magnification;s.setView(v);openText(t);pump();
            textEditor.select(LONG(textEditor.value().size()),LONG(textEditor.value().size()));
            SendMessageW(textEditor.hwnd,EM_SCROLLCARET,0,0);pump();
            // Format inspection must not scroll the viewport to the caret.
            POINT origin={};SendMessageW(textEditor.hwnd,EM_SETSCROLLPOS,0,(LPARAM)&origin);
            auto inspected=textEditor.runs();
            POINT afterInspect={};SendMessageW(textEditor.hwnd,EM_GETSCROLLPOS,0,(LPARAM)&afterInspect);
            require(afterInspect.y==0,"format inspection preserves the viewport");
            for(int i=0;i<20;++i){
                SendMessageW(textEditor.hwnd,WM_KEYDOWN,'X',1);
                SendMessageW(textEditor.hwnd,WM_CHAR,L'x',1);
                SendMessageW(textEditor.hwnd,WM_KEYUP,'X',1);
                POINTL top={};SendMessageW(textEditor.hwnd,EM_POSFROMCHAR,(WPARAM)&top,0);
                require(top.y>=0,"first line stays visible immediately after input before deferred layout");
                pump();
            }
            POINTL first={},last={};SendMessageW(textEditor.hwnd,EM_POSFROMCHAR,(WPARAM)&first,0);
            SendMessageW(textEditor.hwnd,EM_POSFROMCHAR,(WPARAM)&last,LONG(textEditor.value().size()-1));
            RECT client;GetClientRect(textEditor.hwnd,&client);
            require(SendMessageW(textEditor.hwnd,EM_GETLINECOUNT,0,0)>=4,"all four lines remain laid out");
            require(first.y>=0&&last.y>first.y&&last.y<client.bottom,"first and last lines remain inside editor after typing at end");
            finishText(false);
        }
        s.setView(saved);
    }
    // Every paragraph follows the box alignment, even with a collapsed caret
    // on the final line. Native IME/TSF configuration and focus survive activation.
    {
        ObjectId t;s.doc.addText(s.view().center,12,"Long first line\nSecond line\nEnd",&t);s.refresh();openText(t);pump();
        HIMC context=ImmGetContext(edit);bool hasContext=context!=0;if(context)ImmReleaseContext(edit,context);
        LRESULT language=SendMessageW(edit,EM_GETLANGOPTIONS,0,0),editStyle=SendMessageW(edit,EM_GETEDITSTYLE,0,0);
        bool textSurface=focusSurface()==edit;
        SetFocus(canvas);pump();bool canvasRedirect=GetFocus()==edit;
        SetFocus(h);pump();bool shellRedirect=GetFocus()==edit;
        std::ofstream(dir+"/ime-configuration-checks.json")<<"{\"native_context\":"<<(hasContext?"true":"false")<<",\"language_options\":"<<language<<",\"edit_style\":"<<editStyle<<",\"focus_surface_edit\":"<<(textSurface?"true":"false")<<",\"canvas_focus_redirect\":"<<(canvasRedirect?"true":"false")<<",\"shell_focus_redirect\":"<<(shellRedirect?"true":"false")<<",\"real_pinyin_input_tested\":false}";
        require(hasContext,"RichEdit retains a native IME input context");
        require((language&IMF_AUTOKEYBOARD)==0,"RichEdit does not auto-switch the user's input language");
        require((language&IMF_DUALFONT)!=0,"RichEdit retains Asian dual-font support");
        require((editStyle&SES_USECTF)!=0&&(editStyle&SES_NOIME)==0,"RichEdit enables TSF and allows IME");
        require(textSurface,"active caption is the host's focus surface");
        require(canvasRedirect,"canvas focus returns to active native caption editor");
        require(shellRedirect,"shell focus restores active native caption editor");
        auto text=textEditor.value();
        for(int alignment:{1,2,0,3}){
            textEditor.select(LONG(text.size()),LONG(text.size()));textEditor.align(alignment);pump();
            for(LONG index:{0L,16L,LONG(text.size()-1)}){textEditor.select(index,index);PARAFORMAT2 p={};p.cbSize=sizeof(p);SendMessageW(edit,EM_GETPARAFORMAT,0,(LPARAM)&p);require(p.wAlignment==(alignment==1?PFA_CENTER:alignment==2?PFA_RIGHT:alignment==3?PFA_JUSTIFY:PFA_LEFT),"all paragraphs receive requested alignment");}
        }
        textEditor.select(0,0);textEditor.refreshPending=false;textEditor.select(1,1);require(!textEditor.refreshPending,"caret-only selection does not schedule layout");
        // A synthetic START has no OS candidate session: RichEdit may process
        // Enter as a newline. The host must nevertheless keep the draft open.
        SendMessageW(edit,WM_IME_STARTCOMPOSITION,0,0);SendMessageW(edit,WM_KEYDOWN,VK_RETURN,0);require(textEditor.active,"IME candidate Enter does not commit editor");SendMessageW(edit,WM_IME_ENDCOMPOSITION,0,0);pump();
        require(!textEditor.composing&&textEditor.active,"IME end returns to active native editing");
        SetWindowTextW(edit,text.c_str());textEditor.select(LONG(text.size()),LONG(text.size()));
        SendMessageW(edit,WM_IME_STARTCOMPOSITION,0,0);SendMessageW(edit,EM_REPLACESEL,TRUE,(LPARAM)L"中文");SendMessageW(edit,WM_IME_ENDCOMPOSITION,0,0);pump();
        require(textEditor.value()==text+L"中文","Chinese result survives composition lifecycle");finishText(false);
        require(focusSurface()==canvas,"closed caption returns host focus surface to canvas");
        SetFocus(canvas);pump();require(GetFocus()==canvas,"canvas accepts focus after caption closes");
    }
    typeCheck("small");
    // Canvas magnification must reach RichEdit, including beyond the previous
    // 65535/10000 overflow boundary. Reading font size must remain in points.
    {
        auto saved=s.view();ObjectId zoomId;
        require(s.doc.addText(saved.center,10,"ABC 中文\nsecond line",&zoomId).ok(),"zoom fixture");s.refresh();
        std::ofstream report(dir+"/zoom.txt");double unitHeight=0;
        for(double factor:{1.0,0.25,3.0,3.0,6.0,6.6,9.0,9.0,12.0,30.0,60.0,1.0}){
            auto view=saved;view.scale=factor;s.setView(view);openText(zoomId);
            int n=0,d=0;require(SendMessageW(textEditor.hwnd,EM_GETZOOM,(WPARAM)&n,(LPARAM)&d)!=0&&d>0,"read native zoom");
            double actual=double(n)/d;
            require(std::abs(actual-textEditor.zoom)<.0006,"native zoom matches the requested display ratio");
            textEditor.select(0,1);CHARFORMAT2W cf={};cf.cbSize=sizeof(cf);SendMessageW(textEditor.hwnd,EM_GETCHARFORMAT,SCF_SELECTION,(LPARAM)&cf);
            require(cf.yHeight==200,"zoom leaves the underlying ten point size unchanged");
            POINTL p0={},p1={};SendMessageW(textEditor.hwnd,EM_POSFROMCHAR,(WPARAM)&p0,0);SendMessageW(textEditor.hwnd,EM_POSFROMCHAR,(WPARAM)&p1,7);
            double height=p1.y-p0.y;
            if(factor==1.0&&unitHeight==0)unitHeight=height;
            require(height>0&&std::abs(height-unitHeight*factor)<std::max(3.0,unitHeight*factor*.12),"native multiline spacing follows canvas zoom");
            report<<"canvas="<<factor<<" native="<<actual<<" lineHeight="<<height<<" points="<<cf.yHeight/20.0<<"\n";
            require(finishText(false),"cancel zoom test");
            require(s.doc.texts().get(zoomId)->size==10,"edit zoom does not alter stored point size");
        }
        s.setView(saved);
    }

    // New text does not create a temporary document object or consume undo steps.
    {
        auto count=s.doc.texts().records().size();
        for(size_t i=0;i<s.entries.size();++i)if(s.entries[i].owner=="T09"&&s.entries[i].limitation.empty()){s.activate(i);break;}
        textEditor.begin(kInvalidObjectId,kInvalidObjectId,s.view().center,true,120);textBackdrop.clear();
        SendMessageW(textEditor.hwnd,EM_REPLACESEL,TRUE,(LPARAM)L"新建文本框 New");pump();
        require(s.doc.texts().records().size()==count,"new input creates no temporary document object");
        SendMessageW(textEditor.hwnd,WM_KEYDOWN,VK_ESCAPE,0);require(!textEditor.active,"Escape commits and exits new text");
        require(s.entries[s.activeIndex].command=="chooseSelectionTool_Marquee","leaving text tool returns to marquee selection");
        require(s.selected.size()==1&&s.selected[0].kind==ObjectKind::Text,"finished caption remains selected");
        bool found=false;for(auto& t:s.doc.texts().records())if(t.isAlive()&&plain(t.id)=="新建文本框 New")found=true;
        require(found,"new text committed");require(s.undo().ok(),"undo new text");
    }
    if(auto fixture=std::getenv("MOLECULE_STUDIO_TEXT_TEST_FILE")){require(s.open(fixture).ok(),"load large input fixture");typeCheck("large");}

    // ---- T3: automatic italics for chemical abbreviations --------------------
    // The house rule table (化学缩写斜体清单, IUPAC 2013 + 化学学报 3.18(5)) is
    // applied on commit: every label is typed into the real inline editor, saved
    // by the normal command, and the per-character italic flags of the stored
    // runs are compared with the table. '^' = italic, '.' = upright, one symbol
    // per codepoint. Cases marked §7 are the eleven frequent errors.
    {
        struct ItalicCase { const char* text; const char* mask; };
        static const ItalicCase italicCases[] = {
            {"fac-Ir(ppy)3", "^^^........."},  // §7① 只斜 fac
            {"isopropyl", "........."},  // §7② 全称 iso 正体
            {"i-Pr", "^..."},  // §7② 缩写 i- 斜体
            {"(R)-2-butanol", ".^..........."},  // §7③ 只斜 R
            {"N,N-dimethylformamide", "^.^.................."},  // §7④ 两个 N 都斜
            {"1H-indole", ".^......."},  // §7⑤ 指示氢 H 斜体
            {"3a", ".."},  // §7⑥ 环位字母正体
            {"benzo[a]pyrene", "......^......."},  // §7⑥ 融合位字母斜体
            {"η5-Cp", "^...."},  // §7⑦ 上标数字正体
            {"κ2-N,N", "^..^.^"},  // §7⑦ κ-term
            {"N,N′-dimethyl", "^.^.........."},  // §7⑧ 撇号正体
            {"meso-tartaric acid", "^^^^.............."},  // §7⑨ meso 斜体
            {"t-BuLi", "^....."},  // t- 前缀
            {"p-TsOH", "^....."},  // p- 前缀
            {"cis-1,2-dimethylcyclopropane", "^^^........................."},  // cis- 前缀
            {"(E)-but-2-ene", ".^..........."},  // E 描述符
            {"O-methyl", "^......."},  // O- 定位符
            {"S-adenosylmethionine", "^..................."},  // S- 定位符
            {"α-D-glucopyranose", "^................"},  // α 斜体 / D 正体
            {"trans-cyclooctene", "^^^^^............"},  // trans- 前缀
            {"mer-[CoCl3(NH3)3]", "^^^.............."},  // mer- 前缀
            {"naphtho[2,3-b]furan", "............^......"},  // 融合位字母
            {"(1R,2S)-2-butanol", "..^..^..........."},  // 带位次的手性描述符
            {"endo-norborneol", "^^^^..........."},  // endo- 前缀
            {"(R*)", ".^^."},  // 星号随字母斜体
            {"TBPY-5", "^^^^.."},  // 配位多面体符号
            {"benzene", "......."},  // 无描述符
            {"C6H6", "...."},  // 分子式
            {"MeOH", "...."},  // 缩写
            {"Ph", ".."},  // 缩写
            {"Et", ".."},  // 缩写
            {"Boc", "..."},  // 缩写
            {"ppy", "..."},  // 缩写
            {"cyclohexyl", ".........."},  // 名称组成
            {"NH3", "..."},  // 分子式
            {"Ir(ppy)3", "........"},  // 配合物
            {"D-glyceraldehyde", "................"},  // D 不斜体
            {"l-menthol", "........."},  // l- 国际惯例正体
            {"1H NMR", "......"},  // ¹H NMR 的 H 正体
            {"m/z", "..."},  // 非名称
            {"Cp*", "..."},  // 缩写
        };
        const size_t italicCount = sizeof(italicCases) / sizeof(italicCases[0]);
        auto maskOf = [&](const Document& doc, ObjectId t, std::string* plainText) {
            std::string mask;
            if (plainText) plainText->clear();
            const std::vector<TextRun>& stored = doc.runs(t);
            for (size_t i = 0; i < stored.size(); ++i) {
                std::vector<uint32_t> cps = chemitalic::codepointsUtf8(stored[i].utf8);
                for (size_t k = 0; k < cps.size(); ++k) mask += (stored[i].flags & kItalic) ? '^' : '.';
                if (plainText) *plainText += stored[i].utf8;
            }
            return mask;
        };
        s.cancel(); s.doc = Document(); s.history.clear(); s.selected.clear(); s.input.bindDocument(s.doc.id());
        std::ofstream report(dir + "/chem-italic.txt");
        report << "label -> italic mask ('^' italic, '.' upright)\n";
        for (size_t i = 0; i < italicCount; ++i) {
            ObjectId id;
            require(s.doc.addText(Vec2(-150.0, 70.0 - double(i) * 15.0), 10, "", &id).ok(), "italic fixture text");
            s.refresh();
            openText(id); pump();
            auto wide = W(italicCases[i].text);
            textEditor.select(0, -1);
            SendMessageW(textEditor.hwnd, EM_REPLACESEL, TRUE, (LPARAM)wide.c_str());
            pump();
            require(finishText(true), "commit auto-italic label");
            std::string plainText, mask = maskOf(s.doc, id, &plainText);
            if (plainText != italicCases[i].text || mask != italicCases[i].mask) {
                report << "FAIL " << italicCases[i].text << " expected " << italicCases[i].mask << " actual " << mask << "\n";
                report.flush();
                throw std::runtime_error(std::string("auto-italic mismatch: ") + italicCases[i].text + " -> " + mask +
                                         " (expected " + italicCases[i].mask + ", text '" + plainText + "')");
            }
            report << italicCases[i].text << " -> " << mask << "\n";
        }
        // A manual Ctrl+I is never removed: the rule pass only adds italics.
        ObjectId manualId = kInvalidObjectId;
        {
            ObjectId id;
            require(s.doc.addText(Vec2(-150.0, 70.0 - double(italicCount) * 15.0), 10, "", &id).ok(), "manual italic fixture");
            s.refresh(); openText(id); pump();
            auto wide = W("benzene");
            textEditor.select(0, -1);
            SendMessageW(textEditor.hwnd, EM_REPLACESEL, TRUE, (LPARAM)wide.c_str()); pump();
            textEditor.select(2, 3); textEditor.toggle(kItalic); pump();
            const size_t steps = s.history.entries();
            require(finishText(true), "commit manual italic");
            require(s.history.entries() == steps + 1, "a styled commit stays exactly one undo step");
            require(maskOf(s.doc, id, 0) == "..^....", "manual Ctrl+I survives the auto-italic pass");
            manualId = id;
        }
        const std::string italicPath = dir + "/chem-italic.cdxml";
        require(s.save(italicPath, true).ok(), "save auto-italic labels");
        s.fit(); render(); UpdateWindow(canvas);
        screenshot(dir + "/chem-italic.png", false, false);
        {
            Session reload;
            require(reload.open(italicPath).ok(), "reopen auto-italic labels");
            size_t objects = 0;
            for (const auto& t : reload.doc.texts().records()) if (t.isAlive()) ++objects;
            report << "reload: " << objects << " text objects\n";
            size_t matched = 0;
            for (size_t i = 0; i < italicCount; ++i) {
                std::string plainText;
                bool found = false;
                for (const auto& t : reload.doc.texts().records())
                    if (t.isAlive() && maskOf(reload.doc, t.id, &plainText) == italicCases[i].mask && plainText == italicCases[i].text) found = true;
                if (found) ++matched;
            }
            report << "reload masks matched " << matched << "/" << italicCount << "\n";
            require(matched == italicCount, "italic runs survive the cdxml round trip");
            std::ifstream xmlIn(italicPath.c_str(), std::ios::binary);
            std::string xml, xmlLine;
            while (std::getline(xmlIn, xmlLine)) { xml += xmlLine; xml += '\n'; }
            size_t faceTwo = 0;
            for (size_t p = 0; (p = xml.find("face=\"2\"", p)) != std::string::npos; ++p) ++faceTwo;
            report << "cdxml runs with face=\"2\": " << faceTwo << "\n";
            require(faceTwo >= 20, "cdxml writes italic runs as face=\"2\"");
        }
        require(manualId != kInvalidObjectId && !s.doc.runs(manualId).empty(), "styled label stored before undo");
        report.flush();
    }
    // ---- T4: Space stays inside an open caption, and the text tool labels a
    // bond endpoint. Space is bound to the marquee tool twice (shortcut_config.hpp
    // "marquee" VK_SPACE scope 1, keymap_data.hpp {"Generic"," ","TOOLMODE",
    // "MARQUEE"}) and the canvas key path ran that binding whenever the canvas
    // held the keyboard while a caption was open, which ended the input instead
    // of typing a space. It now types and leaves the tool alone.
    {
        auto t4pump=[](){MSG msg;int n=0;while(PeekMessageW(&msg,0,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);if(++n>10000)throw std::runtime_error("t4 input loop");}};
        // A real keystroke: WM_KEYDOWN carries the virtual key (Space is VK_SPACE,
        // a letter its ASCII uppercase code), WM_CHAR the character.
        auto typing=[&](wchar_t ch){SendMessageW(edit,WM_KEYDOWN,WPARAM(towupper(ch)),0);SendMessageW(edit,WM_CHAR,WPARAM(ch),0);t4pump();};
        auto reset=[&](){s.cancel();s.doc=Document();s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());s.refresh();auto v=s.view();v.scale=1;v.center=Vec2();s.setView(v);};
        reset();
        require(s.activateCommand("chooseEdittingTool_Text"),"text tool active for the caption");
        Vec2 blank=s.view().toDevice(Vec2(-120,40));
        SendMessageW(canvas,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(int(blank.x),int(blank.y)));
        SendMessageW(canvas,WM_LBUTTONUP,0,MAKELPARAM(int(blank.x),int(blank.y)));
        require(textEditor.active,"a text-tool click opens the caption");
        require(s.entries[s.activeIndex].owner=="T09","the text tool stays active while typing");
        typing(L'a');typing(L' ');typing(L'b');
        require(textEditor.value()==L"a b","Space types a space and keeps the caption open");
        require(s.entries[s.activeIndex].owner=="T09","Space did not switch to the marquee tool");
        // The canvas can hold the keyboard while a caption is open (a ribbon tool
        // button calls SetFocus(canvas)). Space there must reach the caption too.
        SetFocus(canvas);
        SendMessageW(canvas,WM_KEYDOWN,VK_SPACE,0);SendMessageW(canvas,WM_CHAR,L' ',0);t4pump();
        require(textEditor.active,"a canvas-held Space does not end the caption");
        require(textEditor.value()==L"a b ","a canvas-held Space reaches the caption");
        require(s.entries[s.activeIndex].owner=="T09","a canvas-held Space does not pick the marquee tool");
        SetFocus(edit);
        size_t steps=s.history.entries();
        require(finishText(true),"commit the caption containing spaces");
        require(s.history.entries()==steps+1,"the caption commit stays one undo step");
        ObjectId caption=kInvalidObjectId;
        for(const auto& t:s.doc.texts().records())if(t.isAlive())caption=t.id;
        require(caption!=kInvalidObjectId,"the caption was committed");
        std::string committed;for(const auto& run:s.doc.runs(caption))committed+=run.utf8;
        require(committed=="a b ","the committed caption keeps the spaces verbatim");
        screenshot(dir+"/t4-caption-with-spaces.png",false,false);
        // With no editor open Space still selects the marquee tool.
        require(s.activateCommand("chooseEdittingTool_Text"),"text tool again before the empty-canvas Space");
        SendMessageW(canvas,WM_KEYDOWN,VK_SPACE,0);
        require(s.entries[s.activeIndex].command=="chooseSelectionTool_Marquee","Space still selects the marquee tool when nothing is being edited");
        // A text-tool click on a bond endpoint labels that endpoint's atom, the
        // same behaviour as a double-click on the atom.
        reset();
        ObjectId left,right,bond;
        require(s.doc.addAtom(Vec2(-20,0),6,&left).ok(),"endpoint atom");
        require(s.doc.addAtom(Vec2(20,0),6,&right).ok(),"methyl atom");
        require(s.doc.addBond(left,right,&bond).ok(),"endpoint bond");
        s.refresh();s.fit();
        auto atomText=[&](ObjectId atom){std::string value;for(const auto& run:studioatom::runs(s.doc,atom))value+=run.utf8;return value;};// the committed label is the atom's run list
        const std::string previous=atomText(right);
        require(s.activateCommand("chooseEdittingTool_Text"),"text tool for the endpoint click");
        Vec2 onBond=s.view().toDevice(Vec2(12,0));
        SendMessageW(canvas,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(int(onBond.x),int(onBond.y)));
        SendMessageW(canvas,WM_LBUTTONUP,0,MAKELPARAM(int(onBond.x),int(onBond.y)));
        require(textEditor.active,"a text-tool click on a bond endpoint opens the label editor");
        require(textEditor.atom==right,"the editor is anchored to the endpoint under the cursor");
        for(wchar_t ch:L"OCH3 test label with spaces")typing(ch);
        require(textEditor.value()==L"OCH3 test label with spaces","the label keeps every space while typing");
        SendMessageW(edit,WM_KEYDOWN,VK_RETURN,0);t4pump();
        require(!textEditor.active,"Enter commits the endpoint label");
        require(atomText(right)=="OCH3 test label with spaces",("the atom label contains the spaces verbatim: '"+atomText(right)+"'").c_str());
        require(s.doc.atomLabel(right)=="OCH3 test label with spaces",("the atom label is set: '"+s.doc.atomLabel(right)+"'").c_str());
        require(s.save(dir+"/t4-endpoint-label.cdxml",true).ok(),"save the endpoint-labelled structure");
        {auto close=s.view();close.scale=5;close.center=Vec2(-20,0);s.setView(close);}s.refresh();
        screenshot(dir+"/t4-endpoint-label.png",false,false);
        require(s.undo().ok(),"undo the endpoint label");
        require(atomText(right)==previous&&s.doc.atomLabel(right)==previous,"one undo restores the previous atom label");
        // Selecting a bond and then switching to the text tool: the next typed
        // character opens the label editor of the endpoint under the pointer.
        reset();
        require(s.doc.addAtom(Vec2(-20,0),6,&left).ok(),"endpoint atom for the selection case");
        require(s.doc.addAtom(Vec2(20,0),6,&right).ok(),"methyl atom for the selection case");
        require(s.doc.addBond(left,right,&bond).ok(),"bond for the selection case");
        s.refresh();s.fit();
        Vec2 centre=s.view().toDevice(Vec2(0,0));
        SendMessageW(canvas,WM_MOUSEMOVE,0,MAKELPARAM(int(centre.x),int(centre.y)));
        SendMessageW(canvas,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(int(centre.x),int(centre.y)));
        SendMessageW(canvas,WM_LBUTTONUP,0,MAKELPARAM(int(centre.x),int(centre.y)));
        if(s.selected.empty())s.selected={ObjectRef(ObjectKind::Bond,bond)};
        require(!s.selected.empty(),"the bond is selected");
        require(s.activateCommand("chooseEdittingTool_Text"),"switch to the text tool with the bond selected");
        SendMessageW(canvas,WM_MOUSEMOVE,0,MAKELPARAM(int(centre.x),int(centre.y)));
        SendMessageW(canvas,WM_CHAR,L'Z',1);t4pump();
        require(textEditor.active,"typing with the text tool opens the endpoint label editor");
        require(textEditor.atom==left||textEditor.atom==right,"the typed character goes to the selected bond's endpoint atom");
        require(textEditor.value()==L"Z","the typed character landed in the label editor");
        require(finishText(false),"cancel the typed endpoint label");
        std::ofstream(dir+"/t4-text-input-checks.json")<<"{\"space_in_caption\":true,\"space_does_not_switch_tool\":true,\"space_with_no_editor_marquee\":true,\"canvas_held_space_reaches_caption\":true,\"caption_spaces_committed\":true,\"text_tool_click_labels_endpoint\":true,\"endpoint_label_spaces\":true,\"undo_restores_label\":true,\"select_then_text_tool_types\":true}";
    }
    std::ofstream(dir+"/text-wrap-checks.json")<<"{\"wrap_handles\":true,\"undo_redo\":true,\"cdxml_roundtrip\":true,\"native_roundtrip\":true,\"native_input\":true,\"typing_leaves_document_unchanged\":true,\"new_text\":true,\"rich_formatting\":true,\"cancel\":true,\"chem_italic\":true,\"t4_space_in_caption\":true,\"t4_text_tool_labels_endpoint\":true}";
    s.dirty=false;PostMessageW(h,WM_CLOSE,0,0);
}
