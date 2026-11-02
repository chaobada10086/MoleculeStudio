#pragma once
#include "hydride_default_checks.hpp"
#include "nickname_catalog.hpp"
#include "keymap_data.hpp"
#include "formula.hpp"
void App::tableCheck(const std::string& dir){
    if(GetFileAttributesW(W(dir+"/hydrogen-defaults").c_str())!=INVALID_FILE_ATTRIBUTES){hydrideDefaultCheck(dir);return;}
    using namespace r62::graphics;auto require=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};
    s.dirty=false;execute(Button("","","","@new"));require(ribbonTabs.size()==5,"five ribbon tabs");switchTab(4);require(ribbonTab==4&&visibleGroups().size()==6,"table tab commands available");
    auto id=insertTable(3,3,true);require(id!=kInvalidObjectId,"create editable three-line table");auto empty=s.doc.digest();
    require(tableEditor.paste(L"序号\t条件\t产率\n1\t乙醇\t95%\n2\t水\t88%"),"paste tabular data");auto populated=s.doc.digest();require(s.undo().ok()&&s.doc.digest()==empty,"paste single-step undo");require(s.undo(true).ok()&&s.doc.digest()==populated,"paste exact redo");
    auto read=[&](){TableData d;bool used=false;require(readTable(s.doc,id,&d,&used).ok(),"read native table");return d;};auto text=[&](uint32_t r,uint32_t c){std::string value;require(cellText(s.doc,id,r,c,&value,nullptr).ok(),"read cell text");return value;};
    auto data=read();auto lines=tableSegments(s.doc,id,data);require(lines.size()==3,"three-line table has exactly three strokes");for(auto line:lines)require(line.a.y==line.b.y,"three-line table has no vertical strokes");require(lines[0].width==1.2&&lines[1].width==.6&&lines[2].width==1.2,"top and bottom thicker than header separator");
    tableEditor.r0=tableEditor.r1=0;tableEditor.c0=tableEditor.c1=0;tableEditor.apply("rowAbove");require(read().grid.rows()==4&&text(1,0)=="序号"&&text(2,1)=="乙醇","insert before first row preserves all cells");require(s.undo().ok()&&s.doc.digest()==populated,"insert row undo");
    tableEditor.apply("colLeft");require(read().grid.columns()==4&&text(0,1)=="序号"&&text(1,2)=="乙醇","insert first column preserves text");tableEditor.apply("deleteCols");require(read().grid.columns()==3&&text(1,1)=="乙醇","delete column and restore indexing");
    tableEditor.r0=1;tableEditor.r1=2;tableEditor.c0=1;tableEditor.c1=2;tableEditor.apply("merge");data=read();require(data.grid.merged.size()==1,"merge rectangular cells");auto merged=text(1,1);require(merged.find("乙醇")!=std::string::npos&&merged.find("88%")!=std::string::npos,"merge preserves all source text");auto box=data.grid.cellBox(1,1);require(box.max.x-box.min.x==128&&box.max.y-box.min.y==48,"merged cell geometry");auto mergeDigest=s.doc.digest();require(s.undo().ok()&&read().grid.merged.empty(),"merge undo restores source cells");require(s.undo(true).ok()&&s.doc.digest()==mergeDigest,"merge redo exact");tableEditor.apply("split");require(read().grid.merged.empty()&&text(1,1)==merged,"split restores grid and preserves text");
    // Start a clean presentation table for rendering, persistence and copy tests.
    s.dirty=false;execute(Button("","","","@new"));id=insertTable(4,3,true,78,26);require(id!=kInvalidObjectId,"create final table");require(tableEditor.paste(L"Entry\tSolvent\tYield (%)\n1\tEthanol\t95\n2\tWater\t88\n3\tAcetone\t91"),"paste presentation fixture");tableEditor.selectAll();tableEditor.apply("three");data=read();
    auto boundsBefore=data.grid.frame;tableEditor.r0=tableEditor.r1=0;tableEditor.c0=0;tableEditor.c1=2;tableEditor.apply("bold");tableEditor.apply("center");tableEditor.apply("fontSize",11);
    auto header=read().at(0,0)->text;require(s.doc.texts().get(header)->format.align==1&&s.doc.texts().get(header)->size==11,"cell text formatting");tableEditor.selectAll();tableEditor.apply("middle");tableEditor.apply("equalRows");tableEditor.apply("equalCols");tableEditor.apply("autoFit");require(read().grid.frame.max.x>read().grid.frame.min.x,"auto fit geometry");
    tableEditor.apply("outer");require(tableSegments(s.doc,id,read()).size()==4,"outer border only");tableEditor.apply("none");require(tableSegments(s.doc,id,read()).empty(),"no border style");tableEditor.apply("grid");require(tableSegments(s.doc,id,read()).size()==9,"each grid line emitted once");tableEditor.apply("three");
    // Canvas edge controls add/delete rows and columns through the same model API.
    s.selected={ObjectRef(ObjectKind::Table,id)};TableControls controls;
    require(tableControlLayout(&controls)&&controls.table==id,"canvas table edge controls are available");
    auto rowCount=read().grid.rows(),columnCount=read().grid.columns();
    require(tableControlClick(Vec2(controls.addRow.X+controls.addRow.Width*.5f,controls.addRow.Y+controls.addRow.Height*.5f),0)&&read().grid.rows()==rowCount+1,"canvas + row control appends a row");
    require(tableControlLayout(&controls),"row controls follow the grown table");
    require(tableControlClick(Vec2(controls.delRow.X+controls.delRow.Width*.5f,controls.delRow.Y+controls.delRow.Height*.5f),0)&&read().grid.rows()==rowCount,"canvas − row control deletes the last row");
    require(tableControlLayout(&controls),"column controls follow the restored table");
    require(tableControlClick(Vec2(controls.addCol.X+controls.addCol.Width*.5f,controls.addCol.Y+controls.addCol.Height*.5f),0)&&read().grid.columns()==columnCount+1,"canvas + column control appends a column");
    require(tableControlLayout(&controls),"column controls follow the wider table");
    require(tableControlClick(Vec2(controls.delCol.X+controls.delCol.Width*.5f,controls.delCol.Y+controls.delCol.Height*.5f),0)&&read().grid.columns()==columnCount,"canvas − column control deletes the last column");
    // Exercise real canvas down/move/up dispatch, including clicks on cell text.
    for(const char* mode:{"three","grid","none"})for(int tab:{4,0}){
        tableEditor.apply(mode);ShowWindow(tableEditor.hwnd,SW_HIDE);switchTab(tab);s.activateCommand("chooseSelectionTool_Marquee");
        auto original=read();auto box=original.grid.cellBox(1,1);auto pixel=s.view().toDevice((box.min+box.max)*.5);
        int x=int(std::round(pixel.x)),y=int(std::round(pixel.y));LPARAM from=MAKELPARAM(x,y),to=MAKELPARAM(x+64,y+36);
        auto expected=s.view().toDocument(Vec2(x+64,y+36))-s.view().toDocument(Vec2(x,y));auto before=s.doc.digest();
        std::map<ObjectId,Vec2> textPositions;for(auto cell:original.cells)if(cell.kind==CellKind::Text)textPositions[cell.text]=s.doc.texts().get(cell.text)->position;
        SendMessageW(canvas,WM_LBUTTONDOWN,MK_LBUTTON,from);require(tableDragPending,"canvas press starts whole-table gesture");
        auto assertPreview=[&](Vec2 delta){auto preview=tableDragScene();size_t offset=preview.size()-s.scene.size();size_t pieces=0;
            std::set<ObjectRef> refs={ObjectRef(ObjectKind::Table,id)};for(auto cell:original.cells)if(cell.kind==CellKind::Text)refs.insert(ObjectRef(ObjectKind::Text,cell.text));
            for(size_t i=0;i<s.scene.size();++i){const auto& base=s.gpuScene.primitives()[offset+i];const auto& shown=preview.primitives()[offset+i];
                if(refs.count(s.scene.primitives()[i].source)){++pieces;require(core::length(shown.bounds.min-base.bounds.min-delta)<1e-8&&core::length(shown.bounds.max-base.bounds.max-delta)<1e-8,"every table preview piece follows pointer exactly");require(shown.version!=base.version,"moved preview invalidates GPU caches");}
                else require(shown.version==base.version&&shown.bounds.min==base.bounds.min&&shown.bounds.max==base.bounds.max,"unrelated drawing remains stationary");
            }require(pieces>0,"preview identifies actual table primitives");return preview;
        };
        SendMessageW(canvas,WM_MOUSEMOVE,MK_LBUTTON,to);require(tableDragActive&&s.doc.digest()==before,"drag preview does not mutate document");auto firstPreview=assertPreview(expected);render();
        std::vector<uint8_t> firstPixels,secondPixels;require(gl->readPixels(0,0,s.renderView.widthPx,s.renderView.heightPx,&firstPixels).ok(),"read first live drag frame");
        if(std::string(mode)=="three"&&tab==4)screenshot(dir+"/drag-frame-1.png");
        LPARAM finer=MAKELPARAM(x+65,y+37);SendMessageW(canvas,WM_MOUSEMOVE,MK_LBUTTON,finer);auto fineDelta=s.view().toDocument(Vec2(x+65,y+37))-s.view().toDocument(Vec2(x,y));auto secondPreview=assertPreview(fineDelta);render();
        require(gl->readPixels(0,0,s.renderView.widthPx,s.renderView.heightPx,&secondPixels).ok(),"read second live drag frame");require(firstPixels!=secondPixels,"one-pixel cursor movement changes actual rendered table before release");
        if(std::string(mode)=="three"&&tab==4)screenshot(dir+"/drag-frame-2.png");
        to=MAKELPARAM(x+32,y+18);expected=s.view().toDocument(Vec2(x+32,y+18))-s.view().toDocument(Vec2(x,y));SendMessageW(canvas,WM_MOUSEMOVE,MK_LBUTTON,to);assertPreview(expected);require(s.doc.digest()==before,"multiple live preview frames keep document untouched");
        if(std::string(mode)=="three"&&tab==4)screenshot(dir+"/drag-frame-return.png");
        SendMessageW(canvas,WM_LBUTTONUP,0,to);require(!tableDragPending,"mouse up ends table gesture");
        auto moved=read();require(core::length(moved.grid.frame.min-original.grid.frame.min-expected)<1e-8,"real canvas drag moves table border");
        for(auto value:textPositions)require(core::length(s.doc.texts().get(value.first)->position-value.second-expected)<1e-8,"real drag moves each cell exactly once");
        require(tableStyle(s.doc,id)==mode&&moved.grid.rowHeights==original.grid.rowHeights&&moved.grid.columnWidths==original.grid.columnWidths,"drag preserves table style and cell sizes");
        auto after=s.doc.digest();require(s.undo().ok()&&s.doc.digest()==before,"one drag one undo");require(s.undo(true).ok()&&s.doc.digest()==after,"drag redo exact");require(s.undo().ok(),"restore drag fixture");
        SendMessageW(canvas,WM_LBUTTONDOWN,MK_LBUTTON,from);SendMessageW(canvas,WM_LBUTTONUP,0,from);require(s.doc.digest()==before&&!tableDragPending,"single click selects without moving");
        SendMessageW(canvas,WM_LBUTTONDOWN,MK_LBUTTON,from);SendMessageW(canvas,WM_MOUSEMOVE,MK_LBUTTON,to);SendMessageW(canvas,WM_KEYDOWN,VK_ESCAPE,0);require(!tableDragPending&&s.doc.digest()==before,"Escape cancels pending drag");
        SendMessageW(canvas,WM_LBUTTONDOWN,MK_LBUTTON,from);SendMessageW(canvas,WM_MOUSEMOVE,MK_LBUTTON,to);SendMessageW(canvas,WM_CAPTURECHANGED,0,0);require(!tableDragPending&&s.doc.digest()==before,"lost capture cancels without mutation");
    }
    tableEditor.apply("three");switchTab(4);
    // Copy must use new Text IDs, not the original table's cell references.
    Document copy;ObjectId unrelated;copy.addText(Vec2(),10,"unrelated",&unrelated);std::vector<ObjectRef> refs;require(studioio::append(s.doc,copy,{ObjectRef(ObjectKind::Table,id)},Vec2(30,40),&refs).ok(),"copy complete native table");require(copy.tables().size()==1&&copy.texts().size()==13,"copied table includes text");TableData copied;bool used=false;require(readTable(copy,0,&copied,&used).ok(),"copied authoritative grid");require(copied.grid.frame.min==read().grid.frame.min+Vec2(30,40),"copied coordinates match requested offset");require(tableStyle(copy,0)=="three"&&copy.plainText(copied.at(3,1)->text)=="Acetone","copied style and cell content");
    auto beforeMove=read().grid.frame;auto cellBefore=s.doc.texts().get(read().at(1,1)->text)->position;
    require(s.submit(Command("move table",kInvalidateGeometry,[id](CommandContext& ctx){selection::translateObjects(ctx.document,{ObjectRef(ObjectKind::Table,id)},Vec2(17,23));return CommandResult::success();}),"move table").ok(),"move native table");
    require(read().grid.frame.min==beforeMove.min+Vec2(17,23)&&s.doc.texts().get(read().at(1,1)->text)->position==cellBefore+Vec2(17,23),"borders follow native selection move");require(s.undo().ok(),"move undo");
    tableEditor.r0=tableEditor.r1=0;tableEditor.c0=tableEditor.c1=0;auto unchanged=s.doc.digest();require(!tableEditor.apply("merge")&&s.doc.digest()==unchanged,"invalid merge fails atomically");
    require(s.save(dir+"/three-line-table.r62doc",false).ok(),"native table save");require(s.save(dir+"/three-line-table.cdxml",true).ok(),"CDXML table export");
    Session native,xml;require(native.open(dir+"/three-line-table.r62doc").ok(),"native table reopen");require(xml.open(dir+"/three-line-table.cdxml").ok(),"CDXML table reopen");
    for(auto* reopened:{&native,&xml}){require(reopened->doc.tables().size()==1,"reopened editable table object");TableData d;require(readTable(reopened->doc,0,&d,&used).ok(),"reopened table parameters");require(d.grid.rows()==4&&d.grid.columns()==3&&reopened->doc.plainText(d.at(2,1)->text)=="Water"&&tableStyle(reopened->doc,0)=="three","reopened contents and style");require(tableSegments(reopened->doc,0,d).size()==3,"reopened three strokes");}
    tableEditor.r0=tableEditor.r1=1;tableEditor.c0=0;tableEditor.c1=1;tableEditor.apply("merge");require(s.save(dir+"/merged-table.cdxml",true).ok(),"merged table export");Session mergedXml;require(mergedXml.open(dir+"/merged-table.cdxml").ok(),"merged table reopen");TableData mergedData;require(readTable(mergedXml.doc,0,&mergedData,&used).ok()&&mergedData.grid.merged.size()==1,"CDXML preserves spans");require(s.undo().ok(),"restore non-merged presentation");
    tableEditor.r0=tableEditor.r1=0;tableEditor.c0=tableEditor.c1=0;tableEditor.show(h);require(IsWindowVisible(tableEditor.hwnd),"modeless cell editor shown");SetWindowTextW(tableEditor.edit,L"Changed header");require(tableEditor.commit()&&text(0,0)=="Changed header"&&s.doc.texts().get(read().at(0,0)->text)->size==11,"real editor saves cell text");require(s.undo().ok()&&text(0,0)=="Entry","editor cell update undo");tableEditor.refresh();
    switchTab(2);RECT tabRect={int(ribbonTabs[4].r.X*scale),int(ribbonTabs[4].r.Y*scale),int(ribbonTabs[4].r.GetRight()*scale),int(ribbonTabs[4].r.GetBottom()*scale)};RECT fontRect;GetWindowRect(fontBox,&fontRect);MapWindowPoints(0,h,(POINT*)&fontRect,2);RECT intersection;require(!IntersectRect(&intersection,&tabRect,&fontRect),"font control does not overlap table tab");switchTab(4);
    ShowWindow(tableEditor.hwnd,SW_HIDE);s.selected.clear();s.fit();{auto view=s.view();view.scale=std::min(view.scale,2.2);s.setView(view);}screenshot(dir+"/three-line-table.png");
    ribbonCompact=false;layout();screenshot(dir+"/table-tab-expanded.png");ribbonCompact=true;layout();
    tableEditor.show(h);UpdateWindow(tableEditor.hwnd);{RECT r;GetClientRect(tableEditor.hwnd,&r);HDC screen=GetDC(tableEditor.hwnd),memory=CreateCompatibleDC(screen);HBITMAP bitmap=CreateCompatibleBitmap(screen,r.right,r.bottom);auto old=SelectObject(memory,bitmap);PrintWindow(tableEditor.hwnd,memory,PW_CLIENTONLY);Bitmap picture(bitmap,0);UINT count=0,bytes=0;GetImageEncodersSize(&count,&bytes);std::vector<uint8_t> storage(bytes);auto encoders=(ImageCodecInfo*)storage.data();GetImageEncoders(count,bytes,encoders);for(UINT i=0;i<count;++i)if(wcscmp(encoders[i].MimeType,L"image/png")==0){picture.Save(W(dir+"/table-editor.png").c_str(),&encoders[i].Clsid,0);break;}SelectObject(memory,old);DeleteObject(bitmap);DeleteDC(memory);ReleaseDC(tableEditor.hwnd,screen);}
    ShowWindow(tableEditor.hwnd,SW_HIDE);std::ofstream(dir+"/checks.json")<<"{\"table_tab\":true,\"canvas_drag\":true,\"drag_preview\":true,\"live_drag_pixels\":true,\"one_pixel_follow\":true,\"drag_cancel\":true,\"native_cells\":true,\"tsv_paste\":true,\"insert_preserves_text\":true,\"merge_split\":true,\"three_lines\":true,\"border_modes\":true,\"text_format\":true,\"copy\":true,\"native_roundtrip\":true,\"cdxml_roundtrip\":true,\"undo_redo\":true,\"real_editor\":true,\"canvas_row_column_controls\":true}";s.dirty=false;
    if(GetFileAttributesW(W(dir+"/keep-open").c_str())!=INVALID_FILE_ATTRIBUTES){scale=GetDpiForWindow(h)/96.f;ShowWindow(h,SW_MAXIMIZE);layout();tableEditor.show(h);return;}
    if(GetFileAttributesW(W(dir+"/shapes-plan.xml").c_str())!=INVALID_FILE_ATTRIBUTES){
        for(const std::string name:{"shapes","diagram-row","diagram-column"}){
            s.dirty=false;execute(Button("","new","","@new"));switchTab(1);aiContext();auto before=s.doc.digest();
            applyAI(chemservice::read(W(dir+"/"+name+"-plan.xml")));auto after=s.doc.digest();require(after!=before&&s.doc.paths().size()>0,"presentation actions create editable paths");
            bool filled=false;for(const auto& primitive:s.scene.primitives())if(primitive.source.kind==ObjectKind::Path&&primitive.style.fill.enabled){auto path=s.doc.paths().get(primitive.source.id);require(path&&primitive.style.fill.rgba==path->fillRGBA,"path fill must not become border color");require(primitive.z<10,"filled paths remain below node text");filled=true;}require(filled,"presentation has filled vector shapes");
            require(s.undo().ok()&&s.doc.digest()==before,"whole diagram undo");require(s.undo(true).ok()&&s.doc.digest()==after,"whole diagram redo");
            require(s.save(dir+"/"+name+".r62doc",false).ok(),"presentation native save");Session reopened;require(reopened.open(dir+"/"+name+".r62doc").ok()&&reopened.doc.paths().size()==s.doc.paths().size(),"presentation native reopen paths");
            require(s.save(dir+"/"+name+".cdxml",true).ok(),"presentation CDXML export");
            Session exported;require(exported.open(dir+"/"+name+".cdxml").ok()&&exported.doc.paths().size()==s.doc.paths().size(),"CDXML diagram paths reopen");
            for(size_t i=0;i<s.doc.paths().size();++i){auto a=s.doc.paths().get(i),b=exported.doc.paths().get(i);require(a&&b&&a->points.size()==b->points.size()&&a->closed==b->closed&&a->fillEnabled==b->fillEnabled&&a->fillRGBA==b->fillRGBA,"CDXML preserves vector geometry and fill");for(size_t j=0;j<a->points.size();++j)require(core::length(a->points[j]-b->points[j])<.0001,"CDXML preserves path vertices");}
            s.selected.clear();s.fit();screenshot(dir+"/"+name+".png");
        }
        std::ofstream(dir+"/presentation-checks.json")<<"{\"native_actions\":true,\"undo_redo\":true,\"native_roundtrip\":true,\"cdxml_export\":true}";s.dirty=false;
    }
    if(GetFileAttributesW(W(dir+"/ribbon-preview").c_str())!=INVALID_FILE_ATTRIBUTES){
        switchTab(1);ribbonCompact=false;ribbonVisible=true;
        for(int requested:{1560,2048,1000,940,800}){SetWindowPos(h,0,0,0,requested,900,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);layout();require(top<=int(188*scale),"graphics ribbon stays in a single band");screenshot(dir+"/graphics-"+std::to_string(requested)+".png");}
        s.dirty=false;
    }
    if(GetFileAttributesW(W(dir+"/dash-preview").c_str())!=INVALID_FILE_ATTRIBUTES){
        for(double width:{.6,1.19,2.0})for(bool dense:{false,true}){
            std::vector<Vec2> path;int count=dense?1000:1;for(int i=0;i<=count;++i)path.push_back(Vec2(193.7*i/count,0));path.push_back(Vec2(193.7,157.3));path.push_back(Vec2(0,157.3));
            std::vector<std::vector<Vec2>> runs;render::geom::applyDash(path,true,render::DashPattern(4,2),width,&runs);require(runs.size()>20,"rectangle contains repeated dashes");
            for(size_t i=0;i<runs.size();++i){double length=0;for(size_t j=1;j<runs[i].size();++j)length+=core::length(runs[i][j]-runs[i][j-1]);require(length<=4*width+1e-7,"dash does not grow across numerical boundaries");if(i+1<runs.size())require(std::abs(length-4*width)<1e-7,"uniform full dash length independent of vertices");}
        }
        for(auto groups:{makeTools(),makeExpandedTools(false)})for(auto group:groups)for(auto button:group.items)require(button.cmd!="chooseTableTool","table entry exists only in table tab");
        s.dirty=false;execute(Button("","new","","@new"));s.graphicsWidth=1.19;s.activateCommand("chooseDrawingTool_Rectangle_Dashed");
        s.gesture(PointerPhase::Down,Vec2(0,0),0);s.gesture(PointerPhase::Move,Vec2(120,100),0);
        {auto previewOverlay=s.overlay();bool dashed=false;for(const auto& primitive:previewOverlay.scene().primitives())if(!primitive.style.dash.solid())dashed=true;require(dashed,"dashed rectangle preview keeps dash pattern");screenshot(dir+"/dashed-preview.png");}
        s.gesture(PointerPhase::Up,Vec2(120,100),0);
        s.activateCommand("chooseDrawingTool_RoundRect_Dashed");s.gesture(PointerPhase::Down,Vec2(0,0),0);s.gesture(PointerPhase::Move,Vec2(140,100),0);
        {auto previewOverlay=s.overlay();bool dashed=false;for(const auto& primitive:previewOverlay.scene().primitives())if(!primitive.style.dash.solid())dashed=true;require(dashed,"dashed rounded rectangle preview keeps dash pattern");require(!s.ghost.items().empty()&&s.ghost.items()[0].points.size()>8,"rounded rectangle preview keeps rounded outline");screenshot(dir+"/dashed-round-preview.png");}
        s.gesture(PointerPhase::Up,Vec2(140,100),0);
        switchTab(1);ribbonCompact=false;ribbonVisible=true;s.selected.clear();layout();s.fit();screenshot(dir+"/dashed-frame.png");s.dirty=false;
        std::ofstream(dir+"/dash-checks.json")<<"{\"uniform_dashes\":true,\"three_widths\":true,\"vertex_density_independent\":true,\"duplicate_table_entries_removed\":true}";
    }
    if(GetFileAttributesW(W(dir+"/fixed-length").c_str())!=INVALID_FILE_ATTRIBUTES){
        s.dirty=false;execute(Button("","new","","@new"));ObjectId a=kInvalidObjectId,b=kInvalidObjectId,c=kInvalidObjectId,ab=kInvalidObjectId,bc=kInvalidObjectId;
        require(s.doc.addAtom(Vec2(0,0),6,&a).ok()&&s.doc.addAtom(Vec2(30,0),6,&b).ok()&&s.doc.addAtom(Vec2(30,30),6,&c).ok(),"fixed length fixture");
        require(s.doc.addBond(a,b,&ab).ok()&&s.doc.addBond(b,c,&bc).ok(),"fixed length bonds");s.refresh();
        auto d0=core::length(s.doc.atoms().get(b)->position-s.doc.atoms().get(c)->position);s.dragFixedLength=true;s.selected={ObjectRef(ObjectKind::Atom,c)};
        require(s.moveFixedLength(s.selected,Vec2(12,12)).ok(),"fixed length drag");auto d1=core::length(s.doc.atoms().get(b)->position-s.doc.atoms().get(c)->position);
        require(std::abs(d0-d1)<1e-8,"single attachment bond keeps its length");
        s.dirty=false;execute(Button("","new","","@new"));ObjectId pa=kInvalidObjectId,pb=kInvalidObjectId,pc=kInvalidObjectId,pab=kInvalidObjectId,pbc=kInvalidObjectId;
        require(s.doc.addAtom(Vec2(0,0),6,&pa).ok()&&s.doc.addAtom(Vec2(30,0),6,&pb).ok()&&s.doc.addAtom(Vec2(30,30),6,&pc).ok(),"preview fixture");require(s.doc.addBond(pa,pb,&pab).ok()&&s.doc.addBond(pb,pc,&pbc).ok(),"preview bonds");s.refresh();s.activateCommand("chooseSelectionTool_Marquee");s.selected={ObjectRef(ObjectKind::Atom,pc)};
        s.gesture(PointerPhase::Down,Vec2(30,30));s.gesture(PointerPhase::Move,Vec2(42,42));{std::ofstream dbg(dir+"/preview-debug.txt");dbg<<"moving="<<s.objectDragActive()<<" c="<<s.doc.atoms().get(pc)->position.x<<","<<s.doc.atoms().get(pc)->position.y<<" disp="<<s.displayScene().primitives().size()<<" scene="<<s.scene.primitives().size();}screenshot(dir+"/fixed-preview.png");s.gesture(PointerPhase::Up,Vec2(42,42));
        std::ofstream(dir+"/fixed-length-checks.json")<<"{\"fixed_length\":true,\"bond_before\":"<<d0<<",\"bond_after\":"<<d1<<"}";
    }
    if(GetFileAttributesW(W(dir+"/expand-local").c_str())!=INVALID_FILE_ATTRIBUTES){
        s.dirty=false;execute(Button("","new","","@new"));ObjectId nk=kInvalidObjectId;
        require(s.doc.addAtom(Vec2(0,0),6,&nk).ok(),"nickname fixture atom");
        require(nicknames::apply(s.doc,nk,"Et").ok(),"apply Et nickname");
        s.selected={ObjectRef(ObjectKind::Atom,nk)};
        require(chemistryAction("expandNickname",0),"expand nickname through local backend");
        require(s.doc.stats().aliveAtoms>=2,"Et expands to a structure, not a ChemDraw call");
        s.selected=s.all();require(chemistryAction("copySMILES",0),"copy SMILES through local backend");
        bool copied=false;if(OpenClipboard(canvas)){HANDLE h=GetClipboardData(CF_UNICODETEXT);if(h){auto p=(wchar_t*)GlobalLock(h);copied=p&&*p;GlobalUnlock(h);}CloseClipboard();}
        require(copied,"local copy SMILES writes clipboard text");
        std::ofstream(dir+"/expand-checks.json")<<"{\"local_expand\":true,\"local_smiles\":true}";
    }
    if(GetFileAttributesW(W(dir+"/peri-fusion").c_str())!=INVALID_FILE_ATTRIBUTES){
        s.dirty=false;execute(Button("","new","","@new"));{auto view=s.view();view.scale=1;s.setView(view);}
        require(s.activateCommand("chooseRingTool_Benzene"),"activate benzene for peri test");
        s.gesture(PointerPhase::Down,Vec2(0,0));s.gesture(PointerPhase::Move,Vec2(0,0));s.gesture(PointerPhase::Up,Vec2(0,0));
        auto fb=s.doc.bonds().get(0);auto fa=s.doc.atoms().get(fb->beginAtom)->position,fz=s.doc.atoms().get(fb->endAtom)->position;
        s.gesture(PointerPhase::Down,(fa+fz)*.5);s.gesture(PointerPhase::Move,(fa+fz)*.5+Vec2(0,20));s.gesture(PointerPhase::Up,(fa+fz)*.5);
        require(s.doc.stats().aliveAtoms==10&&s.doc.stats().aliveBonds==11,"naphthalene prepared");
        Vec2 bay;bool found=false;
        for(const auto& b:s.doc.atoms().records())if(b.isAlive()&&s.doc.incidentBonds(b.id).size()==3){
            auto inc=s.doc.incidentBonds(b.id);
            for(size_t i=0;i<inc.size()&&!found;++i)for(size_t j=i+1;j<inc.size()&&!found;++j){
                auto bi=s.doc.bonds().alive(inc[i]),bj=s.doc.bonds().alive(inc[j]);if(!bi||!bj)continue;
                auto ai=s.doc.atoms().get(bi->beginAtom==b.id?bi->endAtom:bi->beginAtom)->position;
                auto ci=s.doc.atoms().get(bj->beginAtom==b.id?bj->endAtom:bj->beginAtom)->position;
                Vec2 o=ai+ci-b.position;Vec2 v=ai-o;auto rotate=[&](Vec2 q,double deg){double r=deg*3.141592653589793/180,cs=cos(r),sn=sin(r);return Vec2(q.x*cs-q.y*sn,q.x*sn+q.y*cs);};
                Vec2 fresh[3]={o+rotate(v,60),o+rotate(v,120),o+rotate(v,180)};bool overlap=false;
                for(Vec2 q:fresh)for(const auto& other:s.doc.atoms().records())if(other.isAlive()&&core::length(q-other.position)<2.0)overlap=true;
                if(!overlap){bay=o;found=true;}
            }
            if(found)break;
        }
        require(found,"naphthalene has a free bay");
        {std::ofstream debug(dir+"/peri-debug.txt");debug<<"bay="<<bay.x<<","<<bay.y<<" ";} 
        s.gesture(PointerPhase::Down,bay);s.gesture(PointerPhase::Move,bay);s.gesture(PointerPhase::Up,bay);
        {std::ofstream debug(dir+"/peri-debug.txt",std::ios::app);debug<<"atoms="<<s.doc.stats().aliveAtoms<<" bonds="<<s.doc.stats().aliveBonds<<" status="<<s.status;}
        int doubles=0;for(const auto& b:s.doc.bonds().records())if(b.isAlive()&&b.order==BondOrder::Double)++doubles;
        require(s.doc.stats().aliveAtoms==13&&s.doc.stats().aliveBonds==15,"bay fusion builds a C13 peri system");
        require(doubles==6,"peri system has six Kekule double bonds");
        require(s.doc.validate().ok(),"peri system validates");
        s.fit();screenshot(dir+"/peri-fusion.png");require(s.save(dir+"/peri-fusion.r62doc",false).ok(),"save peri system");
        std::ofstream(dir+"/peri-checks.json")<<"{\"peri_fusion\":true,\"atoms\":13,\"bonds\":15,\"double_bonds\":6}";
    }
    if(GetFileAttributesW(W(dir+"/phenalene").c_str())!=INVALID_FILE_ATTRIBUTES){
        s.dirty=false;execute(Button("","new","","@new"));
        auto central=insertPhenalene(Vec2(0,0));
        int doubles=0;for(const auto& b:s.doc.bonds().records())if(b.isAlive()&&b.order==BondOrder::Double)++doubles;
        require(central!=kInvalidObjectId&&s.doc.stats().aliveAtoms==13&&s.doc.stats().aliveBonds==15,"3a1H-phenalene has 13 atoms and 15 bonds");
        require(doubles==6,"phenalene has six perimeter double bonds");
        int ch2=0;for(const auto& a:s.doc.atoms().records())if(a.isAlive()&&s.doc.incidentBonds(a.id).size()==2){int doubles=0;for(auto id:s.doc.incidentBonds(a.id))if(s.doc.bonds().get(id)->order==BondOrder::Double)++doubles;if(doubles==0)++ch2;}
        require(ch2==1,"phenalene has one CH2 carbon");
        require(s.doc.validate().ok(),"phenalene validates");
        s.fit();screenshot(dir+"/phenalene.png");require(s.save(dir+"/phenalene.r62doc",false).ok(),"save phenalene");
        std::ofstream(dir+"/phenalene-checks.json")<<"{\"phenalene\":true,\"atoms\":13,\"bonds\":15,\"double_bonds\":6}";
    }
    if(GetFileAttributesW(W(dir+"/fused-ring").c_str())!=INVALID_FILE_ATTRIBUTES){
        s.dirty=false;execute(Button("","new","","@new"));{auto view=s.view();view.scale=1;s.setView(view);}
        require(s.activateCommand("chooseRingTool_Benzene"),"activate benzene ring tool");
        s.gesture(PointerPhase::Down,Vec2(0,0));s.gesture(PointerPhase::Move,Vec2(0,0));s.gesture(PointerPhase::Up,Vec2(0,0));
        require(s.doc.stats().aliveAtoms==6&&s.doc.stats().aliveBonds==6,"first benzene is 6/6");
        auto bond=s.doc.bonds().get(0);auto a=s.doc.atoms().get(bond->beginAtom)->position,z=s.doc.atoms().get(bond->endAtom)->position;auto mid=a*.5+z*.5;
        s.gesture(PointerPhase::Down,mid);s.gesture(PointerPhase::Move,mid+Vec2(0,-20));s.gesture(PointerPhase::Up,mid);
        int doubles=0;for(const auto& b:s.doc.bonds().records())if(b.isAlive()&&b.order==BondOrder::Double)++doubles;
        require(s.doc.stats().aliveAtoms==10&&s.doc.stats().aliveBonds==11,"fused benzene reuses two atoms and one edge");
        require(doubles==5,"naphthalene keeps five Kekule double bonds");
        require(s.doc.validate().ok(),"fused naphthalene validates");
        s.fit();screenshot(dir+"/naphthalene.png");require(s.save(dir+"/naphthalene.r62doc",false).ok(),"save fused naphthalene");
        // A second fusion turns naphthalene into a three-ring system (anthracene or phenanthrene).
        const BondRecord* outer=nullptr;for(const auto& candidate:s.doc.bonds().records())if(candidate.isAlive()&&s.doc.incidentBonds(candidate.beginAtom).size()==2&&s.doc.incidentBonds(candidate.endAtom).size()==2){const auto& p0=s.doc.atoms().get(candidate.beginAtom)->position;const auto& p1=s.doc.atoms().get(candidate.endAtom)->position;if(std::abs(p0.y-p1.y)<1e-6){outer=&candidate;break;}}require(outer!=nullptr,"horizontal outer naphthalene edge with two degree-2 atoms");auto oa=s.doc.atoms().get(outer->beginAtom)->position,oz=s.doc.atoms().get(outer->endAtom)->position;auto omid=oa*.5+oz*.5;
        s.gesture(PointerPhase::Down,omid);s.gesture(PointerPhase::Move,omid+Vec2(0,20));s.gesture(PointerPhase::Up,omid);
        int fusedDoubles=0;for(const auto& b:s.doc.bonds().records())if(b.isAlive()&&b.order==BondOrder::Double)++fusedDoubles;
        {std::ofstream debug(dir+"/third-debug.txt");debug<<"atoms="<<s.doc.stats().aliveAtoms<<" bonds="<<s.doc.stats().aliveBonds<<" doubles="<<fusedDoubles<<" status="<<s.status;}
        require(s.doc.stats().aliveAtoms==14&&s.doc.stats().aliveBonds==16,"third benzene fuses onto naphthalene");
        require(fusedDoubles==7,"three fused rings keep seven Kekule double bonds");
        require(s.doc.validate().ok(),"three-ring fused system validates");
        s.fit();screenshot(dir+"/phenanthrene.png");require(s.save(dir+"/phenanthrene.r62doc",false).ok(),"save three-ring fused system");
        std::ofstream(dir+"/fused-checks.json")<<"{\"fused_benzene\":true,\"atoms\":10,\"bonds\":11,\"double_bonds\":5}";
    }
    if(GetFileAttributesW(W(dir+"/font-preview").c_str())!=INVALID_FILE_ATTRIBUTES){
        std::vector<std::vector<Vec2>> contours={{{0,0},{20,0},{20,20},{0,20}},{{2,2},{8,2},{8,8},{2,8}},{{12,2},{18,2},{18,8},{12,8}},{{2,12},{18,12},{18,18},{2,18}}};
        render::geom::Mesh mesh;require(render::geom::triangulateContours(contours,render::FillRule::EvenOdd,&mesh).ok(),"multi-hole glyph mesh");double area=0;for(size_t i=0;i+5<mesh.triangles.size();i+=6){double ax=mesh.triangles[i],ay=mesh.triangles[i+1],bx=mesh.triangles[i+2],by=mesh.triangles[i+3],cx=mesh.triangles[i+4],cy=mesh.triangles[i+5];area+=std::abs((bx-ax)*(cy-ay)-(by-ay)*(cx-ax))*.5;}require(std::abs(area-232)<.001,"all three glyph counters remain empty");
        s.dirty=false;execute(Button("","new","","@new"));s.selected.clear();aiContext();applyAI("<result ok='true'><actions><add_text x='0' y='50' size='48'>阿斯蒂芬</add_text><add_text x='0' y='100' size='24'>样品准备 测试合格？ 田 目 器 B8O</add_text></actions></result>");
        switchTab(2);layout();s.selected.clear();s.fit();screenshot(dir+"/font-counters.png");s.dirty=false;std::ofstream(dir+"/font-checks.json")<<"{\"multiple_holes\":true,\"cjk_fixture\":true}";
    }
    if(GetFileAttributesW(W(dir+"/shortcuts-check").c_str())!=INVALID_FILE_ATTRIBUTES){
        for(const auto& item:keymap::hotkeys)require(shortcutChinese(item)!="未翻译命令","all context shortcuts translated");
        for(const auto& item:keymap::menuKeys)require(shortcutChinese(item)!="未翻译命令","all menu shortcuts translated");
        std::ofstream list(dir+"/shortcuts-zh.tsv");for(const auto& row:shortcuts.rows){auto label=W(row.label);bool chinese=false;for(auto ch:label)if(ch>=0x4e00&&ch<=0x9fff)chinese=true;require(chinese,"every visible command has Chinese name");list<<row.id<<'\t'<<row.label<<'\t'<<U(ShortcutConfig::chord(row.key,row.modifiers))<<'\n';}
        std::ofstream(dir+"/shortcuts-checks.json")<<"{\"context_bindings\":97,\"menu_commands\":75,\"all_visible_names_chinese\":true}";
    }
    if(GetFileAttributesW(W(dir+"/biosugars-plan.xml").c_str())!=INVALID_FILE_ATTRIBUTES){
        s.dirty=false;execute(Button("","new","","@new"));s.doc.setDefaultBondLength(22.5);aiContext();auto before=s.doc.digest();applyAI(chemservice::read(W(dir+"/biosugars-plan.xml")));auto after=s.doc.digest();require(s.doc.stats().aliveAtoms>80,"bio templates create chemical atoms");require(s.undo().ok()&&s.doc.digest()==before,"bio batch undo");require(s.undo(true).ok()&&s.doc.digest()==after,"bio batch redo");require(s.save(dir+"/biosugars.cdxml",true).ok(),"sugars export");s.selected.clear();switchTab(1);ribbonCompact=false;ribbonVisible=true;layout();s.fit();screenshot(dir+"/biosugars.png");
        s.dirty=false;execute(Button("","new","","@new"));insertBioComponent(3);require(s.doc.stats().aliveAtoms==12,"native sugar gallery inserts actual editable molecule");s.selected.clear();s.fit();screenshot(dir+"/native-sugar.png");s.dirty=false;
        std::ofstream(dir+"/bio-native-checks.json")<<"{\"native_molecules\":true,\"gallery\":true,\"undo_redo\":true,\"cdxml_export\":true}";
    }
    if(GetFileAttributesW(W(dir+"/text-edit-check").c_str())!=INVALID_FILE_ATTRIBUTES){
        if(textEditor.active)finishText(false);s.dirty=false;execute(Button("","new","","@new"));ObjectId carbon,oxygen,bond;s.doc.addAtom(Vec2(0,0),6,&carbon);s.doc.addAtom(Vec2(25,0),8,&oxygen);s.doc.addBond(carbon,oxygen,&bond);s.refresh();s.activateCommand("chooseEdittingTool_Text");s.fit();auto original=s.doc.digest();
        auto start=[&](const wchar_t* value){openText(kInvalidObjectId,oxygen);require(textEditor.active&&IsWindowVisible(textEditor.acceptButton)&&IsWindowVisible(textEditor.cancelButton),"visible save/cancel controls");SendMessageW(edit,EM_REPLACESEL,TRUE,(LPARAM)value);};
        start(L"Ph");SendMessageW(edit,WM_KEYDOWN,VK_RETURN,0);require(!textEditor.active&&s.doc.atomLabel(oxygen)=="Ph"&&s.doc.atoms().get(oxygen)->element==6,"Enter changes hydroxyl oxygen to phenyl nickname");require(s.doc.bonds().get(bond)->beginAtom==carbon&&s.doc.bonds().get(bond)->endAtom==oxygen,"replacement preserves external bond");require(s.save(dir+"/phenyl-label.cdxml",true).ok(),"phenyl nickname export");require(s.undo().ok()&&s.doc.digest()==original,"phenyl edit one undo");require(s.undo(true).ok()&&s.doc.atomLabel(oxygen)=="Ph","phenyl edit redo");
        auto phenyl=s.doc.digest();start(L"Me");SendMessageW(edit,WM_KEYDOWN,VK_ESCAPE,0);require(!textEditor.active&&s.doc.digest()==phenyl,"Escape cancels nickname change");
        start(L"O");SendMessageW(h,WM_COMMAND,MAKEWPARAM(304,BN_CLICKED),(LPARAM)textEditor.acceptButton);require(!textEditor.active&&s.doc.atoms().get(oxygen)->element==8&&!s.doc.extensions(ObjectRef(ObjectKind::Atom,oxygen)).findValue("studio.nickname.fragment"),"save button restores oxygen and removes obsolete nickname fragment");require(s.save(dir+"/oxygen-label.cdxml",true).ok(),"oxygen restored export");
        start(L"Ph");auto click=s.view().toDevice(Vec2(80,60));auto where=MAKELPARAM(int(click.x),int(click.y));SendMessageW(canvas,WM_LBUTTONDOWN,MK_LBUTTON,where);SendMessageW(canvas,WM_LBUTTONUP,0,where);require(!textEditor.active&&s.doc.atomLabel(oxygen)=="Ph","blank canvas click saves without opening another draft");
        start(L"O");execute(Button("","选择","","chooseSelectionTool_Marquee"));require(!textEditor.active&&s.doc.atoms().get(oxygen)->element==8,"tool switch saves active edit");
        textEditor.begin(kInvalidObjectId,kInvalidObjectId,Vec2(50,50));textEditor.composing=true;SendMessageW(edit,WM_KEYDOWN,VK_RETURN,0);require(textEditor.active,"IME confirmation does not submit draft");textEditor.composing=false;textEditor.select(0,-1);SendMessageW(edit,EM_REPLACESEL,TRUE,(LPARAM)L"第一行");BYTE oldKeys[256],keys[256];GetKeyboardState(oldKeys);std::copy(oldKeys,oldKeys+256,keys);keys[VK_SHIFT]|=0x80;SetKeyboardState(keys);SendMessageW(edit,WM_KEYDOWN,VK_RETURN,0);SendMessageW(edit,WM_CHAR,L'\r',0);SetKeyboardState(oldKeys);require(textEditor.active,"Shift+Enter keeps multiline edit open");SendMessageW(edit,EM_REPLACESEL,TRUE,(LPARAM)L"第二行");{std::ofstream debug(dir+"/caption-debug.txt");debug<<U(textEditor.value())<<"\n";}SendMessageW(edit,WM_KEYDOWN,VK_RETURN,0);{std::ofstream debug(dir+"/caption-debug.txt",std::ios::app);debug<<"alive="<<s.doc.stats().aliveTexts<<" size="<<s.doc.texts().size()<<"\n";for(auto t:s.doc.texts().records())debug<<t.id<<":"<<fullText(s.doc,t.id)<<"\n";}require(!textEditor.active&&s.doc.stats().aliveTexts==1&&fullText(s.doc,0)=="第一行\n第二行",(std::string("Enter saves multiline caption: ")+fullText(s.doc,0)).c_str());
        auto caption=s.doc.digest();openText(0);textEditor.select(0,-1);SendMessageW(edit,EM_REPLACESEL,TRUE,(LPARAM)L"取消内容");SendMessageW(h,WM_COMMAND,MAKEWPARAM(305,BN_CLICKED),(LPARAM)textEditor.cancelButton);require(!textEditor.active&&s.doc.digest()==caption,"cancel button restores original caption");
        // A rejected command keeps the draft and controls available for recovery.
        start(L"Ph");textEditor.atom=999999;auto before=s.doc.digest();SendMessageW(edit,WM_KEYDOWN,VK_RETURN,0);require(textEditor.active&&textEditor.value()==L"Ph"&&s.doc.digest()==before,"failed save retains input and document");finishText(false);
        start(L"Ph");screenshot(dir+"/text-edit-context.png");finishText(false);s.dirty=false;std::ofstream(dir+"/text-edit-checks.json")<<"{\"enter_save\":true,\"escape_cancel\":true,\"save_cancel_buttons\":true,\"blank_click_save\":true,\"tool_switch_save\":true,\"shift_enter_multiline\":true,\"ime\":true,\"failed_save_retains_draft\":true,\"phenyl_semantics\":true,\"undo_redo\":true}";
    }
    if(GetFileAttributesW(W(dir+"/hydrogen-check").c_str())!=INVALID_FILE_ATTRIBUTES){
        if(textEditor.active)finishText(false);s.dirty=false;execute(Button("","new","","@new"));ObjectId o,n,c,b,charged;s.doc.addAtom(Vec2(0,0),8,&o);s.doc.addAtom(Vec2(50,0),7,&n);s.doc.addAtom(Vec2(75,0),6,&c);s.doc.addBond(n,c,&b);s.doc.addAtom(Vec2(100,60),7,&charged);auto charge=s.doc.atoms().get(charged)->semantics;charge.charge=1;s.doc.setAtomSemantics(charged,charge);s.refresh();s.fit();
        auto count=[&](ObjectId id){return int(s.doc.atoms().get(id)->semantics.implicitHydrogens);};
        auto base=s.doc.digest();require(cycleAtomHydrogens(o)&&count(o)==0,"water oxygen cycles automatic H2 to zero");require(s.undo().ok()&&s.doc.digest()==base,"hydrogen change undo");require(s.undo(true).ok()&&count(o)==0,"hydrogen change redo");cycleAtomHydrogens(o);require(count(o)==1,"oxygen H1");cycleAtomHydrogens(o);require(count(o)==2,"oxygen H2");cycleAtomHydrogens(o);require(count(o)==0&&s.doc.atoms().get(o)->element==8,"oxygen wraps without replacing atom");
        for(int expected:{0,1,2,0}){cycleAtomHydrogens(n);require(count(n)==expected,"amine H count respects its existing single bond");}
        for(int expected:{0,1,2,3,4,0}){cycleAtomHydrogens(charged);require(count(charged)==expected,"positive nitrogen supports up to four H");}
        s.doc.setBondOrder(b,BondOrder::Triple);s.refresh();cycleAtomHydrogens(n);require(count(n)==0,"nitrile cannot acquire hydrogen");
        auto point=s.view().toDevice(s.doc.atoms().get(o)->position);SendMessageW(canvas,WM_MOUSEMOVE,0,MAKELPARAM(int(point.x),int(point.y)));SendMessageW(canvas,WM_KEYDOWN,'H',0);SendMessageW(canvas,WM_CHAR,L'h',0);SendMessageW(canvas,WM_KEYUP,'H',0);require(count(o)==1&&s.doc.atoms().get(o)->element==8,"hover H shortcut adjusts hydrogen without changing oxygen");
        auto press=[&](ObjectId atom,UINT key,wchar_t character){auto p=s.view().toDevice(s.doc.atoms().get(atom)->position);SendMessageW(canvas,WM_MOUSEMOVE,0,MAKELPARAM(int(p.x),int(p.y)));SendMessageW(canvas,WM_KEYDOWN,key,0);SendMessageW(canvas,WM_CHAR,character,0);SendMessageW(canvas,WM_KEYUP,key,0);};
        for(int expected:{2,0,1}){press(o,'O',L'o');require(count(o)==expected&&s.doc.atoms().get(o)->element==8,"O on oxygen cycles H without changing element or double handling WM_CHAR");}
        s.doc.setBondOrder(b,BondOrder::Single);s.refresh();for(int expected:{1,2,0}){press(n,'N',L'n');require(count(n)==expected&&s.doc.atoms().get(n)->element==7,"N on nitrogen cycles H with bond limit");}
        auto beforeAlias=s.doc.digest();press(n,'N',L'n');require(count(n)==1&&s.undo().ok()&&s.doc.digest()==beforeAlias,"same-element cycle is one undo step");
        press(o,'N',L'n');require(s.doc.atoms().get(o)->element==7,"N on oxygen still changes element");press(o,'O',L'o');require(s.doc.atoms().get(o)->element==8,"O on nitrogen still changes element");
        require(s.save(dir+"/hydrogen-labels.r62doc",false).ok()&&s.save(dir+"/hydrogen-labels.cdxml",true).ok(),"H display persistence");Session native,xml;require(native.open(dir+"/hydrogen-labels.r62doc").ok()&&xml.open(dir+"/hydrogen-labels.cdxml").ok(),"H count files reopen");require(native.doc.atoms().get(o)->semantics.implicitHydrogens==1&&xml.doc.atoms().get(o)->semantics.implicitHydrogens==1,"pinned H count survives native and CDXML");
        openText(kInvalidObjectId,o);auto before=s.doc.digest();SendMessageW(edit,WM_KEYDOWN,'H',0);SendMessageW(edit,WM_CHAR,L'H',0);require(textEditor.active&&textEditor.value()==L"H"&&s.doc.digest()==before,"H enters text inside the editor");finishText(false);
        for(int ringSize:{5,6}){ObjectId nitrogen=kInvalidObjectId,first=kInvalidObjectId,previous=kInvalidObjectId;for(int i=0;i<ringSize;++i){ObjectId next;double angle=6.283185307179586*i/ringSize;s.doc.addAtom(Vec2(180+25*cos(angle),25*sin(angle)),i?6:7,&next);if(!i)nitrogen=first=next;else{ObjectId edge;s.doc.addBond(previous,next,&edge);require(s.doc.setBondSemantics(edge,BondSemantics::Aromatic).ok()&&s.doc.setBondOrder(edge,BondOrder::Aromatic).ok(),"create aromatic fixture edge");}previous=next;}ObjectId edge;s.doc.addBond(previous,first,&edge);require(s.doc.setBondSemantics(edge,BondSemantics::Aromatic).ok()&&s.doc.setBondOrder(edge,BondOrder::Aromatic).ok(),"create aromatic fixture edge");s.refresh();cycleAtomHydrogens(nitrogen);cycleAtomHydrogens(nitrogen);require(count(nitrogen)==(ringSize==5?1:0),(std::string("aromatic nitrogen ring=")+std::to_string(ringSize)+" H="+std::to_string(count(nitrogen))+" status="+status).c_str());}
        require(xml.save(dir+"/hydrogen-roundtrip-again.cdxml",true).ok(),"manual hydrogen metadata can be exported twice");Session twice;require(twice.open(dir+"/hydrogen-roundtrip-again.cdxml").ok(),"no duplicate H metadata on repeat export");
        s.selected.clear();s.fit();screenshot(dir+"/hydrogen-labels.png");s.dirty=false;std::ofstream(dir+"/hydrogen-checks.json")<<"{\"oxygen\":true,\"nitrogen\":true,\"charge_aware\":true,\"bond_order_limit\":true,\"hover_shortcut\":true,\"text_entry_preserved\":true,\"undo_redo\":true,\"save_reopen\":true}";
    }
    #include "tlc_checks.inc"
    PostMessageW(h,WM_CLOSE,0,0);
}

