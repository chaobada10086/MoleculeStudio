// Task 2 acceptance: external paste format confirmation.
//
// The decision itself is a pure function, so the rules are asserted without a
// window; then the real clipboard path is exercised in both branches (silent
// adoption on a blank canvas, and the modal question answered either way by a
// temporary host hook). Internal copy/paste must never prompt.
{
    using studioio::DrawingFormat;
    DrawingFormat a;a.bondLength=14.4;a.lineWidth=.6;a.font="Arial";
    DrawingFormat b;b.bondLength=20;b.lineWidth=1.2;b.font="Times New Roman";
    DrawingFormat sameFont=a;sameFont.font="arial";
    DrawingFormat tolerant=a;tolerant.bondLength=14.41;tolerant.lineWidth=.605;
    require(studioio::sameDrawingFormat(a,sameFont),"font comparison is case-insensitive");
    require(studioio::sameDrawingFormat(a,tolerant),"sub-visible differences are within tolerance");
    require(!studioio::sameDrawingFormat(a,b),"different drawing format is detected");
    require(studioio::pasteFormatDecision(b,a,false,false)==studioio::PasteFormatMode::Same,"internal paste never asks");
    require(studioio::pasteFormatDecision(a,a,false,true)==studioio::PasteFormatMode::Same,"identical external format never asks");
    require(studioio::pasteFormatDecision(b,a,true,true)==studioio::PasteFormatMode::AdoptSource,"blank canvas adopts the source format silently");
    require(studioio::pasteFormatDecision(b,a,false,true)==studioio::PasteFormatMode::Ask,"non-empty canvas asks before switching format");
    require(studioio::documentEmpty(Document()),"a fresh document is a blank canvas");
    {Document d;ObjectId id;require(d.addAtom(Vec2(),6,&id).ok(),"blank-canvas atom fixture");require(!studioio::documentEmpty(d),"an atom makes the canvas non-empty");}
    {Document d;ObjectId id;require(d.addText(Vec2(),10,"x",&id).ok(),"blank-canvas caption fixture");require(!studioio::documentEmpty(d),"a caption makes the canvas non-empty");}
    {Document d;d.setDefaultBondLength(14.4);d.setDefaultLineWidth(.6);d.setDefaultFont("Arial");
     studioio::adoptPasteFormat(d,b);
     require(std::abs(d.defaultBondLength()-20)<1e-9&&std::abs(d.defaultLineWidth()-1.2)<1e-9&&d.defaultFont()=="Times New Roman","adopting the source format updates bond length, line width and font");}
    {Document d;ObjectId atom,text;require(d.addAtom(Vec2(),6,&atom).ok(),"normalize fixture atom");
     require(d.addText(Vec2(20,20),12,"Caption",&text).ok(),"normalize fixture caption");
     std::vector<TextRun> runs;TextRun run;run.utf8="Caption";run.fontName="Times New Roman";run.size=12;runs.push_back(run);require(d.setTextRuns(text,runs).ok(),"normalize fixture runs");
     studioio::normalizePasteFormat(d,{ObjectRef(ObjectKind::Text,text)},a);
     require(d.texts().get(text)->format.fontName=="Arial"&&!d.runs(text).empty()&&d.runs(text).front().fontName=="Arial","keeping the current format re-styles pasted text");}

    // Real clipboard payloads. External XML carries its own document defaults.
    const std::string externalXml="<CDXML BondLength=\"25\" LineWidth=\"2.25\" CaptionSize=\"12\" LabelFont=\"3\" LabelSize=\"10\"><fonttable><font id=\"3\" charset=\"utf-8\" name=\"Times New Roman\"/></fonttable><colortable><color r=\"1\" g=\"1\" b=\"1\"/><color r=\"0\" g=\"0\" b=\"0\"/></colortable><page id=\"1\"><fragment id=\"2\"><n id=\"3\" p=\"0 0\"/><n id=\"4\" p=\"25 0\"/><b id=\"5\" B=\"3\" E=\"4\" Order=\"1\"/><t id=\"6\" p=\"30 30\"><s font=\"3\" size=\"12\">Caption</s></t></fragment></page></CDXML>";
    auto publish=[&](UINT format,const std::string& payload){
        require(OpenClipboard(canvas)!=FALSE,"open the clipboard for the paste-format check");
        EmptyClipboard();
        auto published=studioio::put(format,payload.c_str(),payload.size()+1);
        CloseClipboard();
        require(published.ok(),"publish the clipboard payload");
    };
    if(g_oleClipboard){g_oleClipboard->Release();g_oleClipboard=nullptr;}
    OleSetClipboard(nullptr);
    {
        publish(RegisterClipboardFormatW(L"ChemDraw XML"),externalXml);
        Session blank;auto blankBefore=blank.doc.digest();
        auto pasted=studioio::paste(blank,canvas);
        require(pasted.ok(),("external paste into a blank canvas: "+pasted.message).c_str());
        require(blank.doc.stats().aliveAtoms==2,"the blank canvas receives the pasted structure");
        require(std::abs(blank.doc.defaultBondLength()-25)<1e-6&&std::abs(blank.doc.defaultLineWidth()-2.25)<1e-6&&blank.doc.defaultFont()=="Times New Roman","a blank canvas adopts the source format as a whole");
        require(blank.status.find("采用复制源格式")!=std::string::npos,"the status bar explains the adopted format");
        require(blank.undo().ok()&&blank.doc.digest()==blankBefore,"the format adoption is part of the single paste undo step");
    }
    {
        Session canvasDoc;canvasDoc.doc.setDefaultBondLength(14.4);canvasDoc.doc.setDefaultLineWidth(.6);canvasDoc.doc.setDefaultFont("Arial");
        ObjectId existing;require(canvasDoc.doc.addAtom(Vec2(200,200),6,&existing).ok(),"existing canvas fixture");
        canvasDoc.refresh();
        publish(RegisterClipboardFormatW(L"ChemDraw XML"),externalXml);
        auto previousAsker=studioio::pasteFormatAsker();
        studioio::pasteFormatAsker()=[](const studioio::DrawingFormat&,const studioio::DrawingFormat&,bool* adoptSource){*adoptSource=false;return true;};
        auto pasted=studioio::paste(canvasDoc,canvas);
        studioio::pasteFormatAsker()=previousAsker;
        require(pasted.ok(),("keep-current-format paste: "+pasted.message).c_str());
        require(std::abs(canvasDoc.doc.defaultBondLength()-14.4)<1e-9&&canvasDoc.doc.defaultFont()=="Arial","keeping the current format leaves document defaults alone");
        require(canvasDoc.status.find("保留当前文档格式")!=std::string::npos,"the status bar explains the kept format");
        bool foundText=false,restyled=true;
        for(const auto& text:canvasDoc.doc.texts().records())if(text.isAlive()){
            foundText=true;
            if(text.format.fontName!="Arial")restyled=false;
            for(const auto& run:canvasDoc.doc.runs(text.id))if(run.fontName!="Arial")restyled=false;
        }
        require(foundText&&restyled,"pasted text is re-styled to the current document font");
    }
    {
        Session canvasDoc;canvasDoc.doc.setDefaultBondLength(14.4);canvasDoc.doc.setDefaultLineWidth(.6);canvasDoc.doc.setDefaultFont("Arial");
        ObjectId existing;require(canvasDoc.doc.addAtom(Vec2(200,200),6,&existing).ok(),"adopt-on-answer fixture");
        canvasDoc.refresh();
        publish(RegisterClipboardFormatW(L"ChemDraw XML"),externalXml);
        auto previousAsker=studioio::pasteFormatAsker();
        studioio::pasteFormatAsker()=[](const studioio::DrawingFormat&,const studioio::DrawingFormat&,bool* adoptSource){*adoptSource=true;return true;};
        auto pasted=studioio::paste(canvasDoc,canvas);
        studioio::pasteFormatAsker()=previousAsker;
        require(pasted.ok(),("adopt-source-format paste: "+pasted.message).c_str());
        require(std::abs(canvasDoc.doc.defaultBondLength()-25)<1e-6&&std::abs(canvasDoc.doc.defaultLineWidth()-2.25)<1e-6&&canvasDoc.doc.defaultFont()=="Times New Roman","choosing the source format updates the document defaults");
    }
    {
        // Internal copy/paste: the native clipboard format is this program's own,
        // so the question must never appear even when the formats differ.
        Document subset;subset.setDefaultBondLength(30);subset.setDefaultLineWidth(2.5);subset.setDefaultFont("Courier New");
        ObjectId one,two,bond;require(subset.addAtom(Vec2(),6,&one).ok(),"internal fixture atom");
        require(subset.addAtom(Vec2(30,0),6,&two).ok(),"internal fixture atom");
        require(subset.addBond(one,two,&bond).ok(),"internal fixture bond");
        std::ostringstream native;require(integration::saveSafeNative(subset,native).ok(),"internal native payload");
        publish(studioio::nativeFormat(),native.str());
        Session internal;internal.doc.setDefaultBondLength(21);internal.doc.setDefaultLineWidth(1.5);internal.doc.setDefaultFont("Arial");
        ObjectId existing;require(internal.doc.addAtom(Vec2(400,400),6,&existing).ok(),"internal target fixture");internal.refresh();
        auto pasted=studioio::paste(internal,canvas);
        require(pasted.ok(),("internal paste: "+pasted.message).c_str());
        require(std::abs(internal.doc.defaultBondLength()-21)<1e-9&&internal.doc.defaultFont()=="Arial","internal copy/paste never adopts the source format");
        require(internal.status=="已粘贴","internal copy/paste shows no format question");
    }
    {
        std::ofstream(testOut+"/paste-format-checks.json")<<"{\"pure_decision\":true,\"tolerance\":true,\"blank_canvas_auto_adopt\":true,\"prompt_keep_current\":true,\"prompt_adopt_source\":true,\"internal_no_prompt\":true,\"single_undo_step\":true}";
    }
}
