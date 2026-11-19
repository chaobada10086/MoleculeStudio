from pathlib import Path
p=Path('studio/projection_checks.hpp');s=p.read_text(encoding='utf-8')
a=s.index('        require(app.editProjectionEndpoint(tip)');b=s.index('        const Vec2 center',a)
s=s[:a]+'''        auto click=[&](Vec2 point,bool twice){auto pixel=app.s.view().toDevice(point);auto pos=MAKELPARAM(int(std::lround(pixel.x)),int(std::lround(pixel.y)));studio_i18n::sendMessage(app.canvas,WM_MOUSEMOVE,0,pos);studio_i18n::sendMessage(app.canvas,WM_LBUTTONDOWN,MK_LBUTTON,pos);studio_i18n::sendMessage(app.canvas,WM_LBUTTONUP,0,pos);if(twice){studio_i18n::sendMessage(app.canvas,WM_LBUTTONDBLCLK,MK_LBUTTON,pos);studio_i18n::sendMessage(app.canvas,WM_LBUTTONUP,0,pos);}};
        app.s.activateCommand("chooseEdittingTool_Text");
        click(tip,false);
        require(app.textEditor.active,"text-tool click on grouped endpoint opens editor");
        auto label=app.textEditor.text;
        require(label!=kInvalidObjectId,"endpoint creates a grouped caption");
        studio_i18n::setWindowText(app.textEditor.hwnd,L"CH3");require(app.finishText(true),"commit substituent");
        require(fullText(app.s.doc,label)=="CH3"&&app.s.doc.stats().aliveTexts==1,"one substituent saved");
        app.s.activateCommand("chooseSelectionTool_Marquee");click(tip,true);
        require(app.textEditor.active&&app.textEditor.text==label,"real double-click reuses occupied endpoint");
        studio_i18n::setWindowText(app.textEditor.hwnd,L"CH2CH2OH");require(app.finishText(true),"change substituent");
        require(app.s.undo().ok()&&fullText(app.s.doc,label)=="CH3","undo label change");
        require(app.s.undo(true).ok()&&fullText(app.s.doc,label)=="CH2CH2OH","redo label change");
        auto labelBox=selection::objectBounds(app.s.doc,ObjectRef(ObjectKind::Text,label));
        auto labelPoint=(labelBox.min+labelBox.max)*.5;
        app.s.activateCommand("chooseEdittingTool_Text");click(labelPoint,false);
        require(app.textEditor.active&&app.textEditor.text==label,"text tool edits long existing group label");app.finishText(false);
        app.s.activateCommand("chooseSelectionTool_Marquee");
        auto pixel=app.s.view().toDevice(tip);studio_i18n::sendMessage(app.canvas,WM_MOUSEMOVE,0,MAKELPARAM(int(std::lround(pixel.x)),int(std::lround(pixel.y))));
        studio_i18n::sendMessage(app.canvas,WM_CHAR,'O',0);studio_i18n::sendMessage(app.canvas,WM_CHAR,'H',0);
        require(app.textEditor.active&&app.textEditor.text==label,"hover typing opens existing substituent");require(app.finishText(true)&&fullText(app.s.doc,label)=="OH","hover typing replaces label");
        require(app.s.doc.stats().aliveTexts==1&&app.s.doc.stats().aliveAtoms==0,"editing does not stack labels or create stray atoms");
        for(bool binary:{false,true}){
            auto file=dir+"/edited-projection-"+std::to_string(i)+(binary?".cdx":".cdxml");
            require(app.s.save(file,true).ok(),"save edited projection");Session reopened;require(reopened.open(file).ok(),"reopen edited projection");
            app.s.doc=reopened.doc;app.s.selected.clear();app.s.history.clear();app.s.input.bindDocument(app.s.doc.id());app.s.refresh();
            click(tip,true);require(app.textEditor.active&&fullText(app.s.doc,app.textEditor.text)=="OH","reopened substituent remains editable");app.finishText(false);
        }
''' +s[b:]
p.write_text(s,encoding='utf-8')
