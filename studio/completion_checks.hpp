#pragma once
// Group completion: spelling dictionary, per-entry switches, and the formula
// typography that the inline label editor writes into the committed runs.
void App::completionCheck(const std::string& dir){
    App& app=*this;
    auto require=[](bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);};
    auto pump=[](){MSG message;int count=0;while(PeekMessageW(&message,0,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);if(++count>20000)throw std::runtime_error("completion notification loop");}};
    const bool originalEnabled=chemcompletion::enabled();
    const bool originalFormula=chemcompletion::formulaLayout();
    chemcompletion::enabled()=true;chemcompletion::formulaLayout()=true;
    // The test runs next to the program's own interface.ini: remember the user's
    // custom entries and put them back when the run is over.
    struct Saved{std::string alias,canonical;};std::vector<Saved> saved;
    for(const auto& entry:chemcompletion::list())if(entry.custom)saved.push_back({entry.key,entry.name});
    for(const auto& entry:saved)chemcompletion::removeCustom(entry.alias);
    int checks=0;
    // One label typed into a real inline editor, committed, then inspected.
    auto commit=[&](const char* typed,const char* expectText,const char* expectScripts,const char* why){
        app.s.cancel();app.s.doc=Document();app.s.history.clear();app.s.selected.clear();app.s.input.bindDocument(app.s.doc.id());app.s.refresh();
        ObjectId carbon,atom,bond;
        require(app.s.doc.addAtom(Vec2(0,0),6,&carbon).ok()&&app.s.doc.addAtom(Vec2(30,0),6,&atom).ok()&&app.s.doc.addBond(carbon,atom,&bond).ok(),"completion fixture");
        app.s.refresh();app.openText(kInvalidObjectId,atom);
        std::wstring value=W(std::string(typed));app.textEditor.select(0,-1);
        SendMessageW(app.edit,EM_REPLACESEL,TRUE,(LPARAM)value.c_str());pump();
        require(app.finishText(true),std::string("commit ")+why);
        auto stored=app.s.doc.atomLabel(atom);
        require(stored==std::string(expectText),std::string(why)+": stored label "+stored+" expected "+expectText);
        std::string scripts;for(const auto& run:studioatom::runs(app.s.doc,atom)){
            int script=(run.flags&kSubscript)?-1:(run.flags&kSuperscript)?1:0;
            for(size_t i=0;i<run.utf8.size();++i)if((run.utf8[i]&0xC0)!=0x80)scripts+=char(script<0?'-':script>0?'+':'0');
        }
        require(scripts==std::string(expectScripts),std::string(why)+": scripts "+scripts+" expected "+expectScripts);
        ++checks;
    };
    // Spelling (dictionary) and typography (counts, charges, isotopes, radicals).
    commit("so42-","SO42-","00-++","sulfate");
    commit("fe3+","Fe3+","00++","iron charge");
    commit("NH4+","NH4+","00-+","ammonium");
    commit("no2","NO2","00-","nitro completion");
    commit("1-naph","1-Naph","000000","locant stays on the baseline");
    commit("b(oh)2","B(OH)2","00000-","boronic acid");
    commit("cuSO4*5H2O","cuSO4*5H2O","0000-000-0","hydrate coefficient stays on the baseline");
    commit("Ph+*","Ph+*","00++","radical cation");
    commit("co2","CO2","00-","carbon dioxide typed in lower case");
    commit("so4","SO4","00-","sulfate typed in lower case");
    commit("cuso4","CuSO4","0000-","copper sulfate");
    commit("cu2+","Cu2+","00++","copper(II) ion");
    commit("CH3COOH","CH3COOH","00-0000","acetic acid keeps its counts");
    // Auto case at a bond terminus: lower-case input is an abbreviation, so it
    // reaches the dictionary instead of being read as an element symbol.
    commit("cn","CN","00","cyano auto case");
    commit("bpin","Bpin","0000","pinacol boronate auto case");
    commit("tbu","tBu","000","tert-butyl auto case");
    commit("boc","Boc","000","Boc auto case");
    commit("mgbr","MgBr","0000","Grignard auto case");
    // The committed cyano label really is the functional group, not a bare
    // element label: the definition is stored and the group expands to C#N.
    {
        app.s.cancel();app.s.doc=Document();app.s.history.clear();app.s.selected.clear();app.s.input.bindDocument(app.s.doc.id());app.s.refresh();
        ObjectId carbon,atom,bond;
        require(app.s.doc.addAtom(Vec2(0,0),6,&carbon).ok()&&app.s.doc.addAtom(Vec2(30,0),6,&atom).ok()&&app.s.doc.addBond(carbon,atom,&bond).ok(),"cyano fixture");
        app.s.refresh();app.openText(kInvalidObjectId,atom);
        std::wstring value=W(std::string("cn"));app.textEditor.select(0,-1);
        SendMessageW(app.edit,EM_REPLACESEL,TRUE,(LPARAM)value.c_str());pump();
        require(app.finishText(true),"commit cyano");
        require(app.s.doc.atomLabel(atom)=="CN","cn is auto-cased to CN");
        require(static_cast<const Document&>(app.s.doc).extensions(ObjectRef(ObjectKind::Atom,atom)).findValue("studio.nickname.fragment")!=nullptr,"CN stores the cyano group definition");
        auto expansion=functional_groups::prepareExpansion(app.s.doc,atom);
        require(expansion.anchor!=kInvalidObjectId&&expansion.graph&&expansion.graph->atoms().size()>=2,"cyano expands to a nitrile");
        ++checks;
    }
    // Every compiled entry has to be described by the generated document, and
    // element symbols keep their exact spelling while lower-case input is an
    // abbreviation (cn -> CN is cyano; Cn stays copernicium).
    require(nickdata::count==functionalgroupsdoc::count,"functional group document matches the compiled catalogue");
    require(chemcompletion::normalize("Co")=="Co"&&chemcompletion::normalize("Cn")=="Cn"&&chemcompletion::normalize("N")=="N"&&chemcompletion::normalize("O")=="O","exactly typed element symbols keep their case");
    require(chemcompletion::chemicalToken("Co")=="Co","captions keep exactly typed element symbols");
    require(chemcompletion::normalize("cn")=="CN"&&chemcompletion::normalize("li")=="Li"&&chemcompletion::normalize("bpin")=="Bpin","lower-case input completes to the reviewed abbreviation");
    require(chemcompletion::normalize("tbu")=="tBu"&&chemcompletion::normalize("boc")=="Boc"&&chemcompletion::normalize("mgbr")=="MgBr"&&chemcompletion::normalize("ns")=="Ns","new catalog entries complete in lower case");
    require(chemcompletion::normalize("TBS")=="TBS"&&chemcompletion::normalize("t-Bu")=="t-Bu","authored and canonical spellings are never rewritten");
    require(chemcompletion::normalize("iam")=="iam"&&chemcompletion::normalize("nam")=="nam"&&chemcompletion::chemicalToken("iam").empty(),"an unreviewed ambiguous key is not guessed");
    bool sawBpin=false,sawNs=false,sawDibal=false;
    for(const auto& entry:chemcompletion::list()){if(entry.key=="bpin"&&entry.enabled)sawBpin=true;if(entry.key=="ns"&&entry.enabled)sawNs=true;if(entry.key=="dibal"&&entry.enabled)sawDibal=true;}
    require(sawBpin&&sawNs&&sawDibal,"new abbreviations appear in the settings list");
    ++checks;
    // Every newly reviewed abbreviation must carry a chemically valid, single
    // attachment definition that expands without false valence warnings.
    for(const char* name:{"Bpin","pin","Ns","ONs","Bs","DIBAL","Li","MgBr","MgCl","MgI","Cp","Tr","OPiv","CCl3","CH2Cl","SPh","SePh","PPh2","SnBu3","C6F5","Mts"}){
        app.s.cancel();app.s.doc=Document();app.s.history.clear();app.s.selected.clear();app.s.input.bindDocument(app.s.doc.id());app.s.refresh();
        ObjectId left,right,bond;
        require(app.s.doc.addAtom(Vec2(0,0),6,&left).ok()&&app.s.doc.addAtom(Vec2(30,0),6,&right).ok()&&app.s.doc.addBond(left,right,&bond).ok(),"abbreviation fixture");
        require(nicknames::apply(app.s.doc,right,name).ok(),(std::string("apply ")+name).c_str());
        auto definition=functional_groups::prepareExpansion(app.s.doc,right);
        require(definition.anchor!=kInvalidObjectId&&definition.graph&&!definition.graph->atoms().records().empty(),(std::string("expand ")+name).c_str());
        std::vector<ObjectRef> inserted;functional_groups::applyExpansion(app.s.doc,definition,inserted);
        require(studio::validation::overValentAtoms(app.s.doc).empty(),(std::string("valence ")+name).c_str());
        ++checks;
    }
    // A caption is completed only when the whole caption is one chemical token.
    {
        auto caption=[&](const char* typed,const char* expectText,const char* expectScripts,const char* why){
            app.s.cancel();app.s.doc=Document();app.s.history.clear();app.s.selected.clear();app.s.input.bindDocument(app.s.doc.id());app.s.refresh();
            app.textEditor.begin(kInvalidObjectId,kInvalidObjectId,Vec2(0,0));
            std::wstring value=W(std::string(typed));SendMessageW(app.edit,EM_REPLACESEL,TRUE,(LPARAM)value.c_str());pump();
            require(app.finishText(true),std::string("commit caption ")+why);
            ObjectId id=kInvalidObjectId;for(const auto& text:app.s.doc.texts().records())if(text.isAlive())id=text.id;
            require(id!=kInvalidObjectId,std::string("caption created ")+why);
            std::string plain;for(const auto& run:app.s.doc.runs(id))plain+=run.utf8;
            require(plain==std::string(expectText),std::string("caption ")+why+": got "+plain);
            std::string scripts;for(const auto& run:app.s.doc.runs(id)){
                int script=(run.flags&kSubscript)?-1:(run.flags&kSuperscript)?1:0;
                for(size_t i=0;i<run.utf8.size();++i)if((run.utf8[i]&0xC0)!=0x80)scripts+=char(script<0?'-':script>0?'+':'0');
            }
            require(scripts==std::string(expectScripts),std::string("caption scripts ")+why+": "+scripts);
            ++checks;
        };
        caption("co2","CO2","00-","lower-case formula");
        caption("so4","SO4","00-","lower-case sulfate");
        caption("Table 2","Table 2","0000000","plain text is never re-cased");
    }
    // Per-entry switches.
    require(chemcompletion::normalize("no2")=="NO2","nitro is completed by default");
    require(chemcompletion::setEnabled("NO2",false),"disable one entry");
    require(chemcompletion::normalize("no2")=="no2","a disabled entry is left as typed");
    require(chemcompletion::setEnabled("NO2",true),"enable one entry again");
    require(chemcompletion::normalize("no2")=="NO2","re-enabled entry completes");
    // Custom entries.
    require(chemcompletion::addCustom("abcg","ABcG"),"add a custom abbreviation");
    require(chemcompletion::normalize("abcg")=="ABcG","custom abbreviation completes");
    require(chemcompletion::removeCustom("abcg"),"remove the custom abbreviation");
    require(chemcompletion::normalize("abcg")=="abcg","removed abbreviation no longer completes");
    // The list exposes every switch the settings page shows.
    bool sawNitro=false,sawCustom=false;
    for(const auto& entry:chemcompletion::list()){if(entry.key=="no2"&&entry.enabled)sawNitro=true;if(entry.custom)sawCustom=true;}
    require(sawNitro,"nitro appears enabled in the settings list");
    require(!sawCustom,"removed custom entry is gone from the list");
    // Formula typography can be switched off without touching the dictionary.
    chemcompletion::formulaLayout()=false;
    {auto pieces=chemcompletion::segments("SO42-");require(pieces.empty(),"formula typography off yields no segments");}
    chemcompletion::formulaLayout()=originalFormula;
    for(const auto& entry:saved)chemcompletion::addCustom(entry.alias,entry.canonical);
    chemcompletion::enabled()=originalEnabled;
    // "文字整理" applies the same pipeline to text that is already in the
    // document, in ONE undoable command, and leaves ordinary sentences alone.
    {
        app.s.cancel();app.s.doc=Document();app.s.history.clear();app.s.selected.clear();app.s.input.bindDocument(app.s.doc.id());app.s.refresh();
        ObjectId carbon,first,second,caption,plainCaption;
        require(app.s.doc.addAtom(Vec2(0,0),6,&carbon).ok(),"tidy fixture atom");
        require(app.s.doc.addAtom(Vec2(30,0),6,&first).ok(),"tidy fixture atom 2");
        require(app.s.doc.addAtom(Vec2(60,0),6,&second).ok(),"tidy fixture atom 3");
        app.s.doc.setAtomLabel(first,"so42-");app.s.doc.setAtomLabel(second,"nh4+");
        require(app.s.doc.addText(Vec2(0,-30),12,"co2",&caption).ok(),"tidy fixture caption");
        require(app.s.doc.addText(Vec2(0,-60),12,"Table 2",&plainCaption).ok(),"tidy fixture plain caption");
        app.s.refresh();app.s.selected=app.s.all();
        auto scripts=[](const std::vector<TextRun>& runs){std::string out;for(const auto& run:runs){int script=(run.flags&kSubscript)?-1:(run.flags&kSuperscript)?1:0;for(size_t i=0;i<run.utf8.size();++i)if((run.utf8[i]&0xC0)!=0x80)out+=char(script<0?'-':script>0?'+':'0');}return out;};
        auto captionText=[&](ObjectId id){std::string out;for(const auto& run:app.s.doc.runs(id))out+=run.utf8;return out;};
        size_t before=app.s.history.entries();
        // Drive the real quick button, not just the action, so placement and
        // dispatch are covered together.
        SetWindowPos(app.h,0,0,0,1500,950,SWP_NOMOVE|SWP_NOZORDER);app.layout();
        auto tidyButton=std::find_if(app.chemistryQuick.begin(),app.chemistryQuick.end(),[](const Button& b){return b.cmd=="@textTidy";});
        require(tidyButton!=app.chemistryQuick.end()&&tidyButton->label=="文字整理","文字整理 quick button exists");
        auto tidyRect=tidyButton->r;
        require(tidyRect.X>=0&&tidyRect.GetRight()*app.scale<=1500&&tidyRect.GetBottom()*app.scale<app.top,"文字整理 quick button is laid out");
        SendMessageW(app.h,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(int((tidyRect.X+tidyRect.Width*.5f)*app.scale),int((tidyRect.Y+tidyRect.Height*.5f)*app.scale)));
        SendMessageW(app.h,WM_LBUTTONUP,0,MAKELPARAM(int((tidyRect.X+tidyRect.Width*.5f)*app.scale),int((tidyRect.Y+tidyRect.Height*.5f)*app.scale)));
        require(app.s.history.entries()==before+1,"text tidy quick button is one undo step");
        require(app.s.doc.atomLabel(first)=="SO42-"&&scripts(studioatom::runs(app.s.doc,first))=="00-++","sulfate label tidy");
        require(app.s.doc.atomLabel(second)=="NH4+"&&scripts(studioatom::runs(app.s.doc,second))=="00-+","ammonium label tidy");
        require(captionText(caption)=="CO2"&&scripts(app.s.doc.runs(caption))=="00-","caption formula tidy");
        require(captionText(plainCaption)=="Table 2"&&scripts(app.s.doc.runs(plainCaption))=="0000000","ordinary caption is not re-cased");
        require(app.chemistryAction("textTidy"),"second text tidy runs");
        require(app.status.find("已经是整理后的格式")!=std::string::npos,"second tidy reports nothing to do");
        require(app.s.history.entries()==before+1,"a no-op tidy adds no undo entry");
        require(app.s.undo().ok(),"tidy undo");
        require(app.s.doc.atomLabel(first)=="so42-"&&app.s.doc.atomLabel(second)=="nh4+","undo restores labels");
        require(captionText(caption)=="co2","undo restores caption");
        require(studioatom::runs(app.s.doc,first).empty(),"undo restores plain atom label runs");
        require(app.s.undo(true).ok(),"tidy redo");
        require(app.s.doc.atomLabel(first)=="SO42-","redo reapplies the tidy");
        ++checks;
    }
    // Formulas embedded in a sentence are formatted in place; prose, figure
    // numbers and temperatures stay on the baseline.
    {
        app.s.cancel();app.s.doc=Document();app.s.history.clear();app.s.selected.clear();app.s.input.bindDocument(app.s.doc.id());app.s.refresh();
        const std::string sentence="1. NaH, (EtO)2P(O)CH2CO2Et, THF, 80 °C → 75%";
        ObjectId caption;require(app.s.doc.addText(Vec2(0,0),10,sentence,&caption).ok(),"embedded caption");
        app.s.refresh();app.s.selected={ObjectRef(ObjectKind::Text,caption)};
        SetWindowPos(app.h,0,0,0,1500,950,SWP_NOMOVE|SWP_NOZORDER);app.layout();
        auto button=std::find_if(app.chemistryQuick.begin(),app.chemistryQuick.end(),[](const Button& b){return b.cmd=="@textTidy";});
        require(button!=app.chemistryQuick.end(),"embedded tidy button");
        auto r=button->r;int px=int((r.X+r.Width*.5f)*app.scale),py=int((r.Y+r.Height*.5f)*app.scale);
        SendMessageW(app.h,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(px,py));
        SendMessageW(app.h,WM_LBUTTONUP,0,MAKELPARAM(px,py));
        std::string plain;int subscriptTwos=0;bool eightyBaseline=false;
        for(const auto& run:app.s.doc.runs(caption)){
            plain+=run.utf8;
            if(run.utf8=="2"&&(run.flags&kSubscript))++subscriptTwos;
            if(run.utf8.find("80")!=std::string::npos)eightyBaseline=!(run.flags&kSubscript);
        }
        require(plain==sentence,"embedded tidy keeps prose and spelling");
        require(subscriptTwos==3,"three atom counts inside the embedded formula become subscripts");
        require(eightyBaseline,"temperature 80 stays on the baseline");
        ++checks;
    }
    // Mixed-case abbreviations keep their spelling while atom counts still
    // become subscripts.
    {
        app.s.cancel();app.s.doc=Document();app.s.history.clear();app.s.selected.clear();app.s.input.bindDocument(app.s.doc.id());app.s.refresh();
        const std::string sentence="2. K2OsO2(OH)4, (DHQ)2PHAL, K3Fe(CN)6, K2CO3, PhB(OH)2 → 85%, 96% ee";
        ObjectId caption;require(app.s.doc.addText(Vec2(0,0),10,sentence,&caption).ok(),"mixed case caption");
        app.s.refresh();
        bool changed=false;auto err=texttidy::tidyText(app.s.doc,caption,&changed);
        require(err.ok()&&changed,"mixed case caption tidied");
        std::string plain;bool eightyFiveBaseline=true;
        for(const auto& run:app.s.doc.runs(caption)){plain+=run.utf8;if(run.utf8.find("85")!=std::string::npos)eightyFiveBaseline=!(run.flags&kSubscript);}
        require(plain==sentence,"mixed case abbreviations keep their spelling");
        require(plain.find("PhB(OH)2")!=std::string::npos&&plain.find("K2OsO2(OH)4")!=std::string::npos,"PhB and Os keep their case");
        require(eightyFiveBaseline,"yield 85 stays on the baseline");
        ++checks;
    }
    // Chemical italics: prefixes (i-Bu, n-Bu), stereo descriptors and the
    // standalone ee/de/dr/er abbreviations become italic; element symbols in
    // formulas stay upright.
    {
        app.s.cancel();app.s.doc=Document();app.s.history.clear();app.s.selected.clear();app.s.input.bindDocument(app.s.doc.id());app.s.refresh();
        const std::string sentence="8. i-BuOCOCl, NMM; NaBH4; (PhTe)2 · 90% (2 steps)";
        ObjectId caption;require(app.s.doc.addText(Vec2(0,0),10,sentence,&caption).ok(),"italic caption");
        app.s.refresh();
        bool changed=false;auto err=texttidy::tidyText(app.s.doc,caption,&changed);
        require(err.ok()&&changed,"italic caption tidied");
        bool prefixItalic=false,naItalic=false;
        for(const auto& run:app.s.doc.runs(caption)){
            if(run.utf8=="i"&&(run.flags&kItalic))prefixItalic=true;
            if(run.utf8.find("Na")!=std::string::npos&&(run.flags&kItalic))naItalic=true;
        }
        require(prefixItalic,"i-Bu prefix becomes italic");
        require(!naItalic,"Na in a formula stays upright");
        ObjectId ratio;require(app.s.doc.addText(Vec2(0,-20),10,"96% ee", &ratio).ok(),"ratio caption");
        app.s.refresh();bool ratioChanged=false;texttidy::tidyText(app.s.doc,ratio,&ratioChanged);
        bool eeItalic=false;for(const auto& run:app.s.doc.runs(ratio))if(run.utf8=="ee"&&(run.flags&kItalic))eeItalic=true;
        require(ratioChanged&&eeItalic,"standalone ee becomes italic");
        ObjectId alpha;require(app.s.doc.addText(Vec2(0,-40),10,"36-α", &alpha).ok(),"alpha caption");
        app.s.refresh();bool alphaChanged=false;texttidy::tidyText(app.s.doc,alpha,&alphaChanged);
        bool alphaItalic=false;for(const auto& run:app.s.doc.runs(alpha))if(run.utf8=="α"&&(run.flags&kItalic))alphaItalic=true;
        require(alphaChanged&&alphaItalic,"Greek alpha becomes italic");
        ++checks;
    }
    // Figure/scheme numbers and citation years are not chemical formulas.
    {
        app.s.cancel();app.s.doc=Document();app.s.history.clear();app.s.selected.clear();app.s.input.bindDocument(app.s.doc.id());app.s.refresh();
        const std::string prose="Scheme S1, p.S2, J. Am. Chem. Soc. 2023, 145, 25894";
        ObjectId caption;require(app.s.doc.addText(Vec2(0,0),10,prose,&caption).ok(),"prose caption");
        app.s.refresh();
        require(!texttidy::needsTidyText(app.s.doc,caption),"figure and citation numbers are left alone");
        ++checks;
    }
    // A structure selection reaches captions inside its box, while a caption
    // outside the box stays untouched.
    {
        app.s.cancel();app.s.doc=Document();app.s.history.clear();app.s.selected.clear();app.s.input.bindDocument(app.s.doc.id());app.s.refresh();
        ObjectId a,b,c,d,inside,outside;
        require(app.s.doc.addAtom(Vec2(0,0),6,&a).ok()&&app.s.doc.addAtom(Vec2(40,0),6,&b).ok()&&app.s.doc.addAtom(Vec2(40,40),6,&c).ok()&&app.s.doc.addAtom(Vec2(0,40),6,&d).ok(),"scope fixture atoms");
        require(app.s.doc.addText(Vec2(10,14),12,"co2",&inside).ok(),"scope fixture inside caption");
        require(app.s.doc.addText(Vec2(220,0),12,"so4",&outside).ok(),"scope fixture outside caption");
        app.s.refresh();app.s.selected={ObjectRef(ObjectKind::Atom,a),ObjectRef(ObjectKind::Atom,b),ObjectRef(ObjectKind::Atom,c),ObjectRef(ObjectKind::Atom,d)};
        SetWindowPos(app.h,0,0,0,1500,950,SWP_NOMOVE|SWP_NOZORDER);app.layout();
        auto tidyButton=std::find_if(app.chemistryQuick.begin(),app.chemistryQuick.end(),[](const Button& button){return button.cmd=="@textTidy";});
        require(tidyButton!=app.chemistryQuick.end(),"scope tidy button");
        auto r=tidyButton->r;int px=int((r.X+r.Width*.5f)*app.scale),py=int((r.Y+r.Height*.5f)*app.scale);
        SendMessageW(app.h,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(px,py));
        SendMessageW(app.h,WM_LBUTTONUP,0,MAKELPARAM(px,py));
        std::string insideText;for(const auto& run:app.s.doc.runs(inside))insideText+=run.utf8;
        std::string outsideText;for(const auto& run:app.s.doc.runs(outside))outsideText+=run.utf8;
        require(insideText=="CO2","caption inside the selected structure box is tidied");
        require(outsideText=="so4","caption outside the selected structure box is untouched");
        ++checks;
    }
    // Visual sample: lower-case labels and captions already in the document,
    // tidied by the new command from the quick bar.
    {
        app.s.cancel();app.s.doc=Document();app.s.history.clear();app.s.selected.clear();app.s.input.bindDocument(app.s.doc.id());app.s.refresh();
        const char* labels[]={"ch3","so42-","nh4+","fe3+","c6h12o6","co2h"};
        double x=0;
        for(const char* text:labels){
            ObjectId atom;require(app.s.doc.addAtom(Vec2(x,0),6,&atom).ok(),"tidy sample atom");
            app.s.doc.setAtomLabel(atom,text);x+=72;
        }
        ObjectId caption,plainCaption;
        require(app.s.doc.addText(Vec2(6,-40),12,"co2",&caption).ok(),"tidy sample caption");
        require(app.s.doc.addText(Vec2(120,-40),12,"Table 2",&plainCaption).ok(),"tidy sample plain caption");
        app.s.refresh();app.s.selected=app.s.all();
        require(app.chemistryAction("textTidy"),"tidy sample command");
        app.s.refresh();auto view=app.s.view();view.scale=.9;view.center=Vec2(190,-18);app.s.setView(view);
        app.invalidate();app.updateInfo();
        SetWindowPos(app.h,0,0,0,900,520,SWP_NOMOVE|SWP_NOZORDER);app.layout();app.s.refresh();
        app.screenshot(dir+"/text-tidy.png",false,false);
    }
    // Visual sample of the completed typography, drawn by the real renderer.
    {
        app.s.cancel();app.s.doc=Document();app.s.history.clear();app.s.selected.clear();app.s.input.bindDocument(app.s.doc.id());app.s.refresh();
        const char* labels[]={"SO42-","Fe3+","NH4+","Ph+*","CO2Me","C6H5-"};
        double x=0;
        for(const char* text:labels){
            ObjectId atom;require(app.s.doc.addAtom(Vec2(x,0),6,&atom).ok(),"sample atom");
            std::string canonical=chemcompletion::normalize(text);
            auto pieces=chemcompletion::segments(canonical);
            std::vector<core::TextRun> runs;
            for(const auto& piece:pieces){core::TextRun run;run.utf8=piece.text;run.flags=uint16_t((piece.script<0?kSubscript:piece.script>0?kSuperscript:0)|(piece.script==0?kFormula:0));runs.push_back(run);}
            app.s.doc.setAtomLabel(atom,canonical);studioatom::set(app.s.doc,atom,runs);
            x+=62;
        }
        // The same two lower-case captions the user reported, committed through
        // the real inline editor so the picture shows what typing produces.
        {double captionX=6;
         for(const char* text:{"co2","so4"}){
            app.textEditor.begin(kInvalidObjectId,kInvalidObjectId,Vec2(captionX,-42));
            std::wstring value=W(std::string(text));SendMessageW(app.edit,EM_REPLACESEL,TRUE,(LPARAM)value.c_str());pump();
            app.finishText(true);captionX+=90;
         }}
        app.s.refresh();auto view=app.s.view();view.scale=.9;view.center=Vec2(175,-16);app.s.setView(view);
        app.invalidate();app.updateInfo();
        SetWindowPos(app.h,0,0,0,900,520,SWP_NOMOVE|SWP_NOZORDER);app.layout();app.s.refresh();
        app.screenshot(dir+"/completion-labels.png",false,false);
    }
    // UI placement: the quick column on the Home tab and the "文字" tab card.
    {
        app.s.cancel();app.s.doc=Document();app.s.history.clear();app.s.selected.clear();app.s.input.bindDocument(app.s.doc.id());app.s.refresh();
        SetWindowPos(app.h,0,0,0,1500,950,SWP_NOMOVE|SWP_NOZORDER);app.layout();app.invalidate();UpdateWindow(app.h);
        app.screenshot(dir+"/quick-bar.png",true,true);
        app.switchTab(2);app.layout();app.invalidate();UpdateWindow(app.h);
        app.screenshot(dir+"/text-tab.png",true,true);
        app.switchTab(0);
    }
    std::ofstream(dir+"/completion-checks.json")<<"{\"label_cases\":"<<checks<<",\"per_entry_switch\":true,\"custom_entry\":true,\"formula_toggle\":true,\"settings_list\":true,\"text_tidy\":true,\"auto_case\":true,\"element_exact\":true,\"ambiguity\":true,\"cyano_expands\":true,\"doc_count\":true,\"new_entries_expand\":true}";
    s.dirty=false;PostMessageW(h,WM_CLOSE,0,0);
}