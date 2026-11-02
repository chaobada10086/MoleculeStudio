#pragma once
ObjectId App::tableAt(Vec2 point,uint32_t* row,uint32_t* col)const{
    for(auto it=s.doc.tables().records().rbegin();it!=s.doc.tables().records().rend();++it)if(it->isAlive()){
        r62::graphics::TableData data;bool used=false;if(!r62::graphics::readTable(s.doc,it->id,&data,&used).ok())continue;
        if(data.grid.cellAt(point,row,col))return it->id;
    }return kInvalidObjectId;
}
bool App::beginTableDrag(Vec2 pixel,uint32_t modifiers){
    const auto& tool=s.entries[s.activeIndex];bool selection=tool.owner=="T05"&&tool.command.find("Eraser")==std::string::npos;
    if((ribbonTab!=4&&!selection)||(modifiers&kModControl))return false;
    auto point=s.view().toDocument(pixel);auto id=tableAt(point);if(id==kInvalidObjectId)return false;
    if(IsWindowVisible(tableEditor.hwnd)&&!tableEditor.commit())return true;
    if(textEditor.active)finishText(true);if(!tablePick(point))return false;
    SetFocus(canvas);tableDragPending=true;tableDragActive=false;draggedTable=id;tableDragPress=point;tableDragDelta=Vec2();tableDragDocument=s.doc.id();tableDragSerial=s.serial;tablePreviewDocument=0;
    SetCapture(canvas);status="按住拖动整张表格 · 双击编辑单元格 · Esc 取消";updateCanvasCursor();invalidate();return true;
}
void App::cancelTableDrag(){
    tablePreview.clear();tablePreviewSlots.clear();tablePreviewDocument=0;
    bool pending=tableDragPending;tableDragPending=tableDragActive=false;draggedTable=kInvalidObjectId;tableDragDelta=Vec2();
    if(pending&&GetCapture()==canvas)ReleaseCapture();if(pending)invalidate();
}
void App::updateTableDrag(Vec2 pixel,uint32_t modifiers){
    if(!tableDragPending)return;if(s.doc.id()!=tableDragDocument||s.serial!=tableDragSerial||!s.doc.tables().alive(draggedTable)){cancelTableDrag();status="文档已更改，已取消表格拖动";return;}
    auto delta=s.view().toDocument(pixel)-tableDragPress;
    if(!tableDragActive&&core::length(delta)<s.view().deviceRadiusToDocument(3))return;
    tableDragActive=true;if(modifiers&kModShift){if(std::abs(delta.x)>=std::abs(delta.y))delta.y=0;else delta.x=0;}
    tableDragDelta=delta;status="移动整表 · 松开鼠标放置，Shift 限定方向，Esc 取消";updateCanvasCursor();invalidate();
}
void App::finishTableDrag(Vec2 pixel,uint32_t modifiers){
    updateTableDrag(pixel,modifiers);if(!tableDragPending)return;
    auto id=draggedTable;auto delta=tableDragDelta;bool moving=tableDragActive&&core::length(delta)>1e-8;
    // Release capture only after clearing the pending state: capture loss must
    // cancel an interrupted gesture, never undo a successful mouse-up.
    cancelTableDrag();if(!moving){status="表格已选中 · 按住拖动，双击编辑";invalidate();return;}
    auto e=s.move({ObjectRef(ObjectKind::Table,id)},delta);error(e);if(e.ok()){
        s.selected={ObjectRef(ObjectKind::Table,id)};if(tableEditor.valid()&&tableEditor.table==id)tableEditor.refresh();
        status="整张表格已移动 · Ctrl+Z 撤销";
    }updateCanvasCursor();invalidate();
}
const render::Scene& App::tableDragScene(){
    std::set<ObjectRef> owned;owned.insert(ObjectRef(ObjectKind::Table,draggedTable));if(auto table=s.doc.tables().alive(draggedTable))for(auto id:table->cells)if(id!=kInvalidObjectId)owned.insert(ObjectRef(ObjectKind::Text,id));
    // GPU primitives deliberately have anonymous sources. Use the editable
    // scene to identify table pieces, then move their corresponding GPU copies.
    // The GPU scene consists of the backdrop followed by this same ordered scene.
    const auto& originals=s.scene.primitives();
    if(tablePreviewDocument!=s.doc.id()||tablePreviewSerial!=s.serial||tablePreview.size()!=s.gpuScene.size()){
        tablePreview=s.gpuScene;tablePreviewSlots.clear();for(size_t i=0;i<originals.size();++i)if(owned.count(originals[i].source))tablePreviewSlots.push_back(i);
        tablePreviewDocument=s.doc.id();tablePreviewSerial=s.serial;
    }
    auto& preview=tablePreview;
    if(preview.size()<originals.size())return preview;
    size_t offset=preview.size()-originals.size();uint64_t revision=1469598103934665603ull;
    auto hash=[&](const void* value,size_t length){auto bytes=static_cast<const unsigned char*>(value);for(size_t k=0;k<length;++k){revision^=bytes[k];revision*=1099511628211ull;}};
    hash(&tableDragSerial,sizeof(tableDragSerial));hash(&tableDragDocument,sizeof(tableDragDocument));hash(&tableDragDelta.x,sizeof(double));hash(&tableDragDelta.y,sizeof(double));
    for(size_t i:tablePreviewSlots){
        auto& p=preview.mutablePrimitives()[offset+i];p=s.gpuScene.primitives()[offset+i];for(auto& point:p.points)point=point+tableDragDelta;for(auto& sub:p.subpaths)for(auto& point:sub.points)point=point+tableDragDelta;
        p.bounds.min=p.bounds.min+tableDragDelta;p.bounds.max=p.bounds.max+tableDragDelta;
        // Both the mesh cache and DX11 foreground cache must observe every
        // pointer position, even though no document transaction is made yet.
        p.version=(revision^p.cacheKey^(uint64_t(i+1)*0x9E3779B97F4A7C15ull))|1ull;
    }return preview;
}
ObjectId App::insertTable(uint32_t rows,uint32_t cols,bool three,double cellWidth,double cellHeight){
    if(!rows||!cols||rows>100||cols>30||cellWidth<12||cellHeight<12||(three&&rows<2))return kInvalidObjectId;
    if(textEditor.active)finishText(true);s.cancel();auto id=std::make_shared<ObjectId>(kInvalidObjectId);auto center=s.view().center;
    auto e=s.submit(Command(three?"插入三线表":"插入表格",kInvalidateStructure|kInvalidateGeometry,[=](CommandContext& ctx){r62::graphics::TableGrid grid;grid.rowHeights.assign(rows,cellHeight);grid.columnWidths.assign(cols,cellWidth);grid.frame=Box(center-Vec2(cols*cellWidth*.5,rows*cellHeight*.5),center+Vec2(cols*cellWidth*.5,rows*cellHeight*.5));grid.borderWidth=.6;
        auto result=r62::graphics::createTable(ctx.document,grid,id.get());if(result.ok())ctx.document.extensions(ObjectRef(ObjectKind::Table,*id)).set("studio.table.style",three?"three":"grid");return result.ok()?CommandResult::success():CommandResult::fail(result.code,result.message);
    }),three?"插入三线表":"插入表格");error(e);if(!e.ok())return kInvalidObjectId;
    s.selected={ObjectRef(ObjectKind::Table,*id)};s.activateCommand("chooseSelectionTool_Marquee");tableEditor.changed=[this](){invalidate();};tableEditor.attach(&s,*id);switchTab(4);status="表格已插入 · 双击单元格编辑；选项卡可设置行列、合并与边框";invalidate();return *id;
}
bool App::tablePick(Vec2 point,bool open){
    for(auto it=s.doc.tables().records().rbegin();it!=s.doc.tables().records().rend();++it)if(it->isAlive()){
        r62::graphics::TableData data;bool used=false;if(!r62::graphics::readTable(s.doc,it->id,&data,&used).ok())continue;uint32_t row=0,col=0;if(!data.grid.cellAt(point,&row,&col))continue;
        if(IsWindowVisible(tableEditor.hwnd)&&!tableEditor.commit())return true;
        if(textEditor.active)finishText(true);s.cancel();s.selected={ObjectRef(ObjectKind::Table,it->id)};tableEditor.changed=[this](){invalidate();};tableEditor.attach(&s,it->id,row,col);status="表格 · 第 "+std::to_string(row+1)+" 行，第 "+std::to_string(col+1)+" 列 · 双击编辑";
        if(open){switchTab(4);tableEditor.show(h);}invalidate();return true;
    }return false;
}
void App::tableContextMenu(Vec2 point,POINT screen){
    uint32_t row=0,col=0;ObjectId id=tableAt(point,&row,&col);if(id==kInvalidObjectId)return;
    if(IsWindowVisible(tableEditor.hwnd)&&!tableEditor.commit())return;
    if(textEditor.active)finishText(true);s.cancel();s.selected={ObjectRef(ObjectKind::Table,id)};
    tableEditor.changed=[this](){invalidate();};tableEditor.attach(&s,id,row,col);
    HMENU menu=CreatePopupMenu();
    studio_i18n::appendMenu(menu,MF_STRING,1,L"上方插入行");studio_i18n::appendMenu(menu,MF_STRING,2,L"下方插入行");studio_i18n::appendMenu(menu,MF_STRING,3,L"删除行");
    studio_i18n::appendMenu(menu,MF_SEPARATOR,0,0);studio_i18n::appendMenu(menu,MF_STRING,4,L"左侧插入列");studio_i18n::appendMenu(menu,MF_STRING,5,L"右侧插入列");studio_i18n::appendMenu(menu,MF_STRING,6,L"删除列");
    studio_i18n::appendMenu(menu,MF_SEPARATOR,0,0);studio_i18n::appendMenu(menu,MF_STRING,7,L"行间距…");studio_i18n::appendMenu(menu,MF_STRING,8,L"复制表格	Ctrl+C");studio_i18n::appendMenu(menu,MF_STRING,9,L"粘贴表格	Ctrl+V");studio_i18n::appendMenu(menu,MF_STRING,10,L"编辑表格…");
    int choice=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY|TPM_RIGHTBUTTON,screen.x,screen.y,0,h,0);DestroyMenu(menu);
    if(choice==1)tableEditor.apply("rowAbove");else if(choice==2)tableEditor.apply("rowBelow");else if(choice==3)tableEditor.apply("deleteRows");
    else if(choice==4)tableEditor.apply("colLeft");else if(choice==5)tableEditor.apply("colRight");else if(choice==6)tableEditor.apply("deleteCols");
    else if(choice==7)tableEditor.rowSpacingDialog();else if(choice==8)execute(Button("ctxCopyTable","复制表格","","@copy"));
    else if(choice==9)execute(Button("ctxPasteTable","粘贴表格","","@paste"));else if(choice==10)tableEditor.show(h);
    updateCanvasCursor();invalidate();
}
bool App::tableControlLayout(TableControls* out)const{
    if(!out)return false;ObjectId id=kInvalidObjectId;
    for(auto ref:s.selected)if(ref.kind==ObjectKind::Table&&s.doc.tables().alive(ref.id)){id=ref.id;break;}
    if(id==kInvalidObjectId)return false;
    r62::graphics::TableData data;bool used=false;if(!r62::graphics::readTable(s.doc,id,&data,&used).ok())return false;
    Vec2 a=s.view().toDevice(data.grid.frame.min),b=s.view().toDevice(data.grid.frame.max);
    float left=float(std::min(a.x,b.x)),top=float(std::min(a.y,b.y)),right=float(std::max(a.x,b.x)),bottom=float(std::max(a.y,b.y));
    const float size=18.f,gap=3.f;if(right-left<8||bottom-top<8)return false;
    out->table=id;out->rows=data.grid.rows();out->cols=data.grid.columns();
    out->addRow=RectF(right-size*2-gap,bottom+5,size,size);out->delRow=RectF(right-size,bottom+5,size,size);
    out->addCol=RectF(right+5,bottom-size*2-gap,size,size);out->delCol=RectF(right+5,bottom-size,size,size);
    return true;
}
bool App::tableControlClick(Vec2 pixel,uint32_t){
    TableControls c;if(!tableControlLayout(&c))return false;
    auto inside=[](const RectF& r,Vec2 p){return p.x>=r.X&&p.x<=r.GetRight()&&p.y>=r.Y&&p.y<=r.GetBottom();};
    if(inside(c.addRow,pixel)){tableEditor.attach(&s,c.table,c.rows-1,0);tableEditor.apply("rowBelow");status="已在表格底部添加一行";invalidate();return true;}
    if(inside(c.delRow,pixel)){tableEditor.attach(&s,c.table,c.rows-1,0);tableEditor.r0=tableEditor.r1=c.rows-1;tableEditor.c0=0;tableEditor.c1=c.cols-1;tableEditor.apply("deleteRows");status="已删除表格最后一行";invalidate();return true;}
    if(inside(c.addCol,pixel)){tableEditor.attach(&s,c.table,0,c.cols-1);tableEditor.apply("colRight");status="已在表格右侧添加一列";invalidate();return true;}
    if(inside(c.delCol,pixel)){tableEditor.attach(&s,c.table,0,c.cols-1);tableEditor.r0=0;tableEditor.r1=c.rows-1;tableEditor.c0=tableEditor.c1=c.cols-1;tableEditor.apply("deleteCols");status="已删除表格最后一列";invalidate();return true;}
    return false;
}
void App::tableCommand(const std::string& op){
    if(op=="insert"||op=="insertThree"){
        HMENU menu=CreatePopupMenu();studio_i18n::appendMenu(menu,MF_STRING,1,L"2 行 × 2 列");studio_i18n::appendMenu(menu,MF_STRING,2,L"3 行 × 3 列");studio_i18n::appendMenu(menu,MF_STRING,3,L"4 行 × 3 列");studio_i18n::appendMenu(menu,MF_STRING,4,L"5 行 × 4 列");studio_i18n::appendMenu(menu,MF_SEPARATOR,0,0);studio_i18n::appendMenu(menu,MF_STRING,5,L"自定义行数与列数…");POINT at;GetCursorPos(&at);int chosen=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY,at.x,at.y,0,h,0);DestroyMenu(menu);if(!chosen)return;
        int rows=chosen==1?2:chosen==2?3:chosen==3?4:5,cols=chosen==1?2:chosen==4?4:3;if(chosen==5){double value=3;if(!NumberDialog::show(h,L"插入表格",L"行数（1–100）",3,&value,op=="insertThree"?2:1,100))return;rows=int(value);if(!NumberDialog::show(h,L"插入表格",L"列数（1–30）",3,&value,1,30))return;cols=int(value);}if(insertTable(uint32_t(rows),uint32_t(cols),op=="insertThree")!=kInvalidObjectId)tableEditor.show(h);return;
    }
    ObjectId id=kInvalidObjectId;for(auto ref:s.selected)if(ref.kind==ObjectKind::Table&&s.doc.tables().alive(ref.id)){id=ref.id;break;}
    if(id==kInvalidObjectId&&IsWindowVisible(tableEditor.hwnd)&&tableEditor.valid())id=tableEditor.table;
    if(id==kInvalidObjectId){status="请先点击画布中的表格或插入新表格";invalidate();return;}
    if(!tableEditor.valid()||tableEditor.table!=id){tableEditor.changed=[this](){invalidate();};tableEditor.attach(&s,id);}
    if(op=="edit"){tableEditor.show(h);return;}if(op=="selectAll"){tableEditor.selectAll();tableEditor.show(h);return;}
    double value=0;r62::graphics::TableData data;tableEditor.data(&data);
    if(op=="rowHeight"||op=="colWidth"||op=="fontSize"||op=="borderWidth"){
        value=op=="rowHeight"?data.grid.rowHeights[tableEditor.r1]:op=="colWidth"?data.grid.columnWidths[tableEditor.c1]:op=="fontSize"?10:data.grid.borderWidth;
        const wchar_t* label=op=="rowHeight"?L"行间距 / 行高（pt）":op=="colWidth"?L"列宽（pt）":op=="fontSize"?L"字号（pt）":L"基础线宽（pt；三线表顶线、底线为两倍）";
        if(!NumberDialog::show(h,L"表格设置",label,value,&value,op=="borderWidth"?.1:op=="fontSize"?4:12,op=="borderWidth"?6:op=="fontSize"?72:1000))return;
    }
    bool success=tableEditor.apply(op,value);if(success&&op=="deleteTable")s.selected.clear();status=success?"表格操作完成；可撤销":s.status;invalidate();
}
