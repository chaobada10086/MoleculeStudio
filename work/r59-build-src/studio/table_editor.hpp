#pragma once
#include "engine/table_layout.hpp"
struct StudioTableEditor {
    HWND hwnd=0,grid=0,edit=0,owner=0,addRowBtn=0,delRowBtn=0,addColBtn=0,delColBtn=0,rowSpaceBtn=0;HFONT font=0;Session* session=nullptr;DocumentId document=0;ObjectId table=kInvalidObjectId;
    uint32_t r0=0,c0=0,r1=0,c1=0;int scrollX=0,scrollY=0;float scale=1;bool dragging=false,loading=false;std::function<void()> changed;
    ~StudioTableEditor(){if(IsWindow(hwnd))DestroyWindow(hwnd);if(font)DeleteObject(font);}
    bool data(r62::graphics::TableData* out)const{bool used=false;return session&&session->doc.id()==document&&r62::graphics::readTable(session->doc,table,out,&used).ok();}
    bool valid()const{r62::graphics::TableData d;return data(&d);}
    void bounds(uint32_t& top,uint32_t& left,uint32_t& bottom,uint32_t& right)const{top=std::min(r0,r1);left=std::min(c0,c1);bottom=std::max(r0,r1);right=std::max(c0,c1);}
    void refresh(){r62::graphics::TableData d;if(!data(&d)){if(IsWindow(hwnd))ShowWindow(hwnd,SW_HIDE);return;}r0=std::min(r0,d.grid.rows()-1);r1=std::min(r1,d.grid.rows()-1);c0=std::min(c0,d.grid.columns()-1);c1=std::min(c1,d.grid.columns()-1);
        if(IsWindow(hwnd)){loading=true;std::string text;r62::graphics::cellText(session->doc,table,r1,c1,&text,nullptr);studio_i18n::setWindowText(edit,W(text).c_str());loading=false;
            std::wstring title=L"表格编辑 · "+std::to_wstring(d.grid.rows())+L" 行 × "+std::to_wstring(d.grid.columns())+L" 列";studio_i18n::setWindowText(hwnd,title.c_str());
            studio_i18n::setWindowText(GetDlgItem(hwnd,5),(L"单元格 "+std::to_wstring(r1+1)+L" 行 "+std::to_wstring(c1+1)+L" 列 · 拖动选择范围，Ctrl+V 粘贴多行多列，Ctrl+Enter 应用文字").c_str());InvalidateRect(grid,nullptr,FALSE);}
        if(changed)changed();
    }
    template<class F> bool submit(const std::string& title,F fn){if(!valid())return false;auto id=table;auto e=session->submit(Command(title,kInvalidateStructure|kInvalidateGeometry|kInvalidateAppearance,[id,fn](CommandContext& ctx){auto error=fn(ctx.document,id);return error.ok()?CommandResult::success():CommandResult::fail(error.code,error.message);}),title);if(!e.ok()){if(IsWindow(hwnd))studio_i18n::setWindowText(GetDlgItem(hwnd,5),W(e.message).c_str());session->status=e.message;return false;}refresh();return true;}
    void attach(Session* value,ObjectId id,uint32_t row=0,uint32_t column=0){session=value;document=value->doc.id();table=id;r0=r1=row;c0=c1=column;scrollX=scrollY=0;refresh();}
    bool commit(){if(!IsWindow(edit)||!valid())return false;auto value=U(AiWindow::controlText(edit));std::string old;r62::graphics::cellText(session->doc,table,r1,c1,&old,nullptr);if(old==value)return true;auto row=r1,column=c1;r62::graphics::TableData d;data(&d);auto cell=d.at(row,column);double size=cell&&cell->kind==r62::graphics::CellKind::Text?session->doc.texts().get(cell->text)->size:10;return submit("编辑单元格",[value,row,column,size](Document& doc,ObjectId id){return r62::graphics::setCellText(doc,id,row,column,value,size);});}
    void navigate(int dx,int dy){if(!commit())return;r62::graphics::TableData d;if(!data(&d))return;int row=int(r1)+dy,column=int(c1)+dx;if(column>=int(d.grid.columns())){column=0;++row;}if(column<0){column=int(d.grid.columns())-1;--row;}r0=r1=uint32_t(std::max(0,std::min(int(d.grid.rows())-1,row)));c0=c1=uint32_t(std::max(0,std::min(int(d.grid.columns())-1,column)));refresh();}
    bool apply(const std::string& op,double value=0){
        if(IsWindowVisible(hwnd)&&!commit())return false;r62::graphics::TableData d;if(!data(&d))return false;uint32_t top,left,bottom,right;bounds(top,left,bottom,right);
        return submit("表格："+op,[=](Document& doc,ObjectId id)->Error{using namespace r62::graphics;TableData current;bool used=false;auto e=readTable(doc,id,&current,&used);if(!e.ok())return e;
            if(op=="rowAbove"||op=="rowBelow")return insertRow(doc,id,op=="rowAbove"?top:bottom+1,current.grid.rowHeights[top]);
            if(op=="colLeft"||op=="colRight")return insertColumn(doc,id,op=="colLeft"?left:right+1,current.grid.columnWidths[left]);
            if(op=="deleteRows"){if(bottom-top+1>=current.grid.rows())return Error(ErrorCode::InvalidArgument,"请使用删除表格，至少保留一行");for(int row=int(bottom);row>=int(top);--row){e=deleteRow(doc,id,uint32_t(row));if(!e.ok())return e;}return Error();}
            if(op=="deleteCols"){if(right-left+1>=current.grid.columns())return Error(ErrorCode::InvalidArgument,"请使用删除表格，至少保留一列");for(int col=int(right);col>=int(left);--col){e=deleteColumn(doc,id,uint32_t(col));if(!e.ok())return e;}return Error();}
            if(op=="deleteTable"){for(auto cell:current.cells)if(cell.kind==CellKind::Text&&doc.texts().alive(cell.text)){e=doc.removeObject(ObjectRef(ObjectKind::Text,cell.text));if(!e.ok())return e;}return doc.removeObject(ObjectRef(ObjectKind::Table,id));}
            if(op=="merge")return mergeCells(doc,id,top,left,bottom-top+1,right-left+1);
            if(op=="split")return splitCell(doc,id,r1,c1);
            if(op=="rowHeight"||op=="equalRows"){double height=value;if(op=="equalRows"){height=0;for(uint32_t row=top;row<=bottom;++row)height+=current.grid.rowHeights[row];height/=bottom-top+1;}for(uint32_t row=top;row<=bottom;++row)current.grid.rowHeights[row]=height;current.grid.frame.max.y=current.grid.frame.min.y+current.grid.totalHeight();return writeTable(doc,id,current);}
            if(op=="colWidth"||op=="equalCols"){double width=value;if(op=="equalCols"){width=0;for(uint32_t col=left;col<=right;++col)width+=current.grid.columnWidths[col];width/=right-left+1;}for(uint32_t col=left;col<=right;++col)current.grid.columnWidths[col]=width;current.grid.frame.max.x=current.grid.frame.min.x+current.grid.totalWidth();return writeTable(doc,id,current);}
            if(op=="grid"||op=="three"||op=="outer"||op=="none"||op=="horizontal"){
                doc.noteTouchedExtensions(ObjectRef(ObjectKind::Table,id),false);doc.extensions(ObjectRef(ObjectKind::Table,id)).set("studio.table.style",op);current.grid.borders.outer=op=="none"?TableBorderStyle::None:TableBorderStyle::Thin;current.grid.borders.inner=(op=="none"||op=="outer")?TableBorderStyle::None:TableBorderStyle::Thin;
                if(op=="three"&&current.grid.rows()<2)return Error(ErrorCode::InvalidArgument,"三线表需要至少两行：表头和数据行");
                if(op=="three")for(uint32_t col=0;col<current.grid.columns();++col){auto cell=current.at(0,col);if(cell->kind==CellKind::Text){auto fmt=doc.texts().get(cell->text)->format;fmt.flags|=kBold;e=doc.setTextFormat(cell->text,fmt);if(!e.ok())return e;}}
                return writeTable(doc,id,current);
            }
            if(op=="borderWidth")return setTableBorderWidth(doc,id,value);
            if(op=="autoFit"){
                for(uint32_t col=0;col<current.grid.columns();++col){double width=36;for(uint32_t row=0;row<current.grid.rows();++row){auto cell=current.at(row,col);if(cell->kind!=CellKind::Text||coveredCell(current.grid,row,col))continue;bool merged=false;for(auto m:current.grid.merged)if(m.row==row&&m.column==col&&m.columnSpan>1)merged=true;if(merged)continue;auto text=doc.plainText(cell->text);double units=0,line=0;for(unsigned char c:text){if(c=='\n'){units=std::max(units,line);line=0;}else if((c&0xc0)!=0x80)line+=c>=0xe0?1.0:.65;}units=std::max(units,line);width=std::max(width,units*doc.texts().get(cell->text)->size+10);}current.grid.columnWidths[col]=std::min(400.,width);}
                for(uint32_t row=0;row<current.grid.rows();++row){double height=22;for(uint32_t col=0;col<current.grid.columns();++col){auto cell=current.at(row,col);if(cell->kind==CellKind::Text){auto text=doc.plainText(cell->text);height=std::max(height,doc.texts().get(cell->text)->size*(1+1.25*std::count(text.begin(),text.end(),'\n'))+8);}}current.grid.rowHeights[row]=height;}
                current.grid.frame.max=current.grid.frame.min+Vec2(current.grid.totalWidth(),current.grid.totalHeight());return writeTable(doc,id,current);
            }
            for(uint32_t row=top;row<=bottom;++row)for(uint32_t col=left;col<=right;++col){auto cell=current.at(row,col);if(coveredCell(current.grid,row,col))continue;
                if(op=="clear"){e=clearCell(doc,id,row,col);if(!e.ok())return e;continue;}if(cell->kind!=CellKind::Text)continue;
                if(op=="fontSize"){e=doc.setTextSize(cell->text,value);if(!e.ok())return e;}
                else if(op=="left"||op=="center"||op=="right"||op=="bold"){auto fmt=doc.texts().get(cell->text)->format;if(op=="bold")fmt.flags^=kBold;else{fmt.align=op=="left"?0:op=="center"?1:2;cell->textOffset.x=0;}e=doc.setTextFormat(cell->text,fmt);if(!e.ok())return e;}
                else if(op=="top"||op=="middle"||op=="bottom"){doc.noteTouchedExtensions(ObjectRef(ObjectKind::Text,cell->text),false);doc.extensions(ObjectRef(ObjectKind::Text,cell->text)).set("studio.cell.valign",op);cell->textOffset.y=0;}
            }if(op=="clear")return Error();return writeTable(doc,id,current);
        });
    }
    bool paste(const std::wstring& value){
        if(value.size()>100000)return false;std::vector<std::vector<std::string>> rows(1);std::wstring cell;
        for(size_t i=0;i<=value.size();++i){wchar_t ch=i==value.size()?L'\n':value[i];if(ch==L'\r')continue;if(ch==L'\t'||ch==L'\n'){rows.back().push_back(U(cell));cell.clear();if(ch==L'\n'&&i<value.size()-1)rows.push_back({});}else cell+=ch;}
        while(!rows.empty()&&rows.back().empty())rows.pop_back();if(rows.empty())return false;size_t cols=0;for(auto& row:rows)cols=std::max(cols,row.size());auto row=r1,col=c1;
        if(row+rows.size()>100||col+cols>30){if(IsWindow(hwnd))studio_i18n::setWindowText(GetDlgItem(hwnd,5),L"粘贴范围最多 100 行、30 列");return false;}
        return submit("粘贴表格内容",[rows,cols,row,col](Document& doc,ObjectId id)->Error{using namespace r62::graphics;TableData data;bool used=false;auto e=readTable(doc,id,&data,&used);if(!e.ok())return e;
            for(auto m:data.grid.merged)if(row<m.row+m.rowSpan&&row+rows.size()>m.row&&col<m.column+m.columnSpan&&col+cols>m.column)return Error(ErrorCode::InvalidArgument,"粘贴区域含合并单元格，请先拆分");
            while(data.grid.rows()<row+rows.size()){e=insertRow(doc,id,data.grid.rows(),24);if(!e.ok())return e;readTable(doc,id,&data,&used);}while(data.grid.columns()<col+cols){e=insertColumn(doc,id,data.grid.columns(),64);if(!e.ok())return e;readTable(doc,id,&data,&used);}
            for(size_t r=0;r<rows.size();++r)for(size_t c=0;c<rows[r].size();++c){e=setCellText(doc,id,row+uint32_t(r),col+uint32_t(c),rows[r][c],10);if(!e.ok())return e;}return Error();
        });
    }
    void pasteClipboard(){if(!OpenClipboard(hwnd))return;std::wstring value;if(auto memory=GetClipboardData(CF_UNICODETEXT)){auto text=(const wchar_t*)GlobalLock(memory);if(text){value=text;GlobalUnlock(memory);}}CloseClipboard();if(!value.empty())paste(value);}
    void selectAll(){r62::graphics::TableData d;if(data(&d)){r0=c0=0;r1=d.grid.rows()-1;c1=d.grid.columns()-1;refresh();}}
    void addRowAtBottom(){r62::graphics::TableData d;if(!data(&d))return;r0=r1=d.grid.rows()-1;apply("rowBelow");}
    void deleteSelectedRows(){apply("deleteRows");}
    void addColumnAtRight(){r62::graphics::TableData d;if(!data(&d))return;c0=c1=d.grid.columns()-1;apply("colRight");}
    void deleteSelectedColumns(){apply("deleteCols");}
    void rowSpacingDialog(){r62::graphics::TableData d;if(!data(&d))return;uint32_t top,left,bottom,right;bounds(top,left,bottom,right);double value=d.grid.rowHeights[top];if(NumberDialog::show(hwnd,L"行间距",L"行高（pt）",value,&value,12,240))apply("rowHeight",value);}
    void copySelection(){
        r62::graphics::TableData d;if(!data(&d))return;uint32_t top,left,bottom,right;bounds(top,left,bottom,right);std::wstring out;
        for(uint32_t row=top;row<=bottom;++row){for(uint32_t col=left;col<=right;++col){std::string text;r62::graphics::cellText(session->doc,table,row,col,&text,nullptr);if(col>left)out+=L'\t';out+=W(text);}if(row<bottom)out+=L"\r\n";}
        if(!OpenClipboard(hwnd))return;EmptyClipboard();size_t bytes=(out.size()+1)*sizeof(wchar_t);HGLOBAL memory=GlobalAlloc(GMEM_MOVEABLE,bytes);if(memory){void* p=GlobalLock(memory);if(p){memcpy(p,out.c_str(),bytes);GlobalUnlock(memory);SetClipboardData(CF_UNICODETEXT,memory);}}CloseClipboard();
    }
    void contextMenu(POINT screen){
        if(!commit())return;r62::graphics::TableData d;if(!data(&d))return;
        HMENU menu=CreatePopupMenu();
        studio_i18n::appendMenu(menu,MF_STRING,1,L"上方插入行");studio_i18n::appendMenu(menu,MF_STRING,2,L"下方插入行");studio_i18n::appendMenu(menu,MF_STRING,3,L"删除所选行");
        studio_i18n::appendMenu(menu,MF_SEPARATOR,0,0);studio_i18n::appendMenu(menu,MF_STRING,4,L"左侧插入列");studio_i18n::appendMenu(menu,MF_STRING,5,L"右侧插入列");studio_i18n::appendMenu(menu,MF_STRING,6,L"删除所选列");
        studio_i18n::appendMenu(menu,MF_SEPARATOR,0,0);studio_i18n::appendMenu(menu,MF_STRING,7,L"行间距…");studio_i18n::appendMenu(menu,MF_STRING,8,L"复制所选单元格");studio_i18n::appendMenu(menu,MF_STRING,9,L"粘贴多行多列");
        int choice=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY|TPM_RIGHTBUTTON,screen.x,screen.y,0,hwnd,0);DestroyMenu(menu);
        if(choice==1)apply("rowAbove");else if(choice==2)apply("rowBelow");else if(choice==3)apply("deleteRows");
        else if(choice==4)apply("colLeft");else if(choice==5)apply("colRight");else if(choice==6)apply("deleteCols");
        else if(choice==7)rowSpacingDialog();else if(choice==8)copySelection();else if(choice==9)pasteClipboard();
    }
    void updateFont(){HFONT previous=font;font=CreateFontW(-int(15*scale),0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");for(int id=2;id<=11;++id)studio_i18n::sendDlgItemMessage(hwnd,id,WM_SETFONT,(WPARAM)font,TRUE);if(previous)DeleteObject(previous);}
    void layout(){RECT rc;GetClientRect(hwnd,&rc);int w=int(rc.right/scale),h=int(rc.bottom/scale);auto move=[&](HWND c,int x,int y,int width,int height){MoveWindow(c,int(x*scale),int(y*scale),int(width*scale),int(height*scale),TRUE);};int gridW=std::max(200,w-32-84),gridH=std::max(80,h-222);move(grid,16,50,gridW,gridH);move(edit,16,h-126,w-32,66);move(GetDlgItem(hwnd,5),16,14,w-32,28);move(GetDlgItem(hwnd,2),16,h-46,100,30);move(GetDlgItem(hwnd,3),124,h-46,140,30);move(GetDlgItem(hwnd,4),w-96,h-46,80,30);int rightX=16+gridW+8;move(addColBtn,rightX,50,76,26);move(delColBtn,rightX,80,76,26);move(rowSpaceBtn,rightX,110,76,26);int bottomY=50+gridH+6;move(addRowBtn,16,bottomY,76,26);move(delRowBtn,98,bottomY,76,26);}
    void show(HWND parent){owner=parent;if(!IsWindow(hwnd)){scale=GetDpiForWindow(parent)/96.f;WNDCLASSW wc={};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(0);wc.hCursor=LoadCursorW(0,MAKEINTRESOURCEW(32512));wc.lpszClassName=L"MoleculeStudioTableEditor";RegisterClassW(&wc);wc.style=CS_DBLCLKS;wc.lpfnWndProc=gridProc;wc.lpszClassName=L"MoleculeStudioTableGrid";RegisterClassW(&wc);
            hwnd=studio_i18n::createWindowEx(WS_EX_TOOLWINDOW,L"MoleculeStudioTableEditor",L"表格编辑",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,int(840*scale),int(560*scale),parent,0,GetModuleHandleW(0),this);
            
            grid=studio_i18n::createWindowEx(WS_EX_CLIENTEDGE,L"MoleculeStudioTableGrid",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_HSCROLL|WS_VSCROLL,0,0,10,10,hwnd,(HMENU)1,GetModuleHandleW(0),this);
            edit=studio_i18n::createWindowEx(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL,0,0,10,10,hwnd,(HMENU)6,GetModuleHandleW(0),0);studio_i18n::sendMessage(edit,EM_SETLIMITTEXT,10000,0);SetWindowSubclass(edit,entryProc,1,(DWORD_PTR)this);
            for(auto pair:{std::make_pair(2,L"应用文字"),std::make_pair(3,L"粘贴多行多列"),std::make_pair(4,L"关闭")})studio_i18n::createWindowEx(0,L"BUTTON",pair.second,WS_CHILD|WS_VISIBLE|WS_TABSTOP,0,0,10,10,hwnd,(HMENU)INT_PTR(pair.first),GetModuleHandleW(0),0);
            addRowBtn=studio_i18n::createWindowEx(0,L"BUTTON",L"+ 行",WS_CHILD|WS_VISIBLE|WS_TABSTOP,0,0,10,10,hwnd,(HMENU)7,GetModuleHandleW(0),0);
            delRowBtn=studio_i18n::createWindowEx(0,L"BUTTON",L"− 行",WS_CHILD|WS_VISIBLE|WS_TABSTOP,0,0,10,10,hwnd,(HMENU)8,GetModuleHandleW(0),0);
            addColBtn=studio_i18n::createWindowEx(0,L"BUTTON",L"+ 列",WS_CHILD|WS_VISIBLE|WS_TABSTOP,0,0,10,10,hwnd,(HMENU)9,GetModuleHandleW(0),0);
            delColBtn=studio_i18n::createWindowEx(0,L"BUTTON",L"− 列",WS_CHILD|WS_VISIBLE|WS_TABSTOP,0,0,10,10,hwnd,(HMENU)10,GetModuleHandleW(0),0);
            rowSpaceBtn=studio_i18n::createWindowEx(0,L"BUTTON",L"行间距…",WS_CHILD|WS_VISIBLE|WS_TABSTOP,0,0,10,10,hwnd,(HMENU)11,GetModuleHandleW(0),0);
            studio_i18n::createWindowEx(0,L"STATIC",L"",WS_CHILD|WS_VISIBLE,0,0,10,10,hwnd,(HMENU)5,GetModuleHandleW(0),0);updateFont();layout();}
        refresh();ShowWindow(hwnd,SW_SHOWNORMAL);SetForegroundWindow(hwnd);SetFocus(grid);
    }
    static LRESULT CALLBACK entryProc(HWND h,UINT m,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR value){auto a=(StudioTableEditor*)value;if(m==WM_GETDLGCODE)return DLGC_WANTTAB|DLGC_WANTCHARS;if(m==WM_KEYDOWN&&wp==VK_TAB){a->navigate(GetKeyState(VK_SHIFT)<0?-1:1,0);SetFocus(a->grid);return 0;}if(m==WM_KEYDOWN&&wp==VK_RETURN&&GetKeyState(VK_CONTROL)<0){a->commit();SetFocus(a->grid);return 0;}if(m==WM_KEYDOWN&&wp==VK_ESCAPE){a->refresh();SetFocus(a->grid);return 0;}return DefSubclassProc(h,m,wp,lp);}
    static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM wp,LPARAM lp){auto a=(StudioTableEditor*)GetWindowLongPtrW(h,GWLP_USERDATA);if(m==WM_NCCREATE){a=(StudioTableEditor*)((CREATESTRUCTW*)lp)->lpCreateParams;SetWindowLongPtrW(h,GWLP_USERDATA,(LONG_PTR)a);a->hwnd=h;}if(!a)return DefWindowProcW(h,m,wp,lp);
        switch(m){case WM_DPICHANGED:{a->scale=HIWORD(wp)/96.f;auto r=(RECT*)lp;SetWindowPos(h,0,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);a->updateFont();a->layout();InvalidateRect(a->grid,0,TRUE);return 0;}case WM_SIZE:if(a->grid)a->layout();return 0;case WM_GETMINMAXINFO:((MINMAXINFO*)lp)->ptMinTrackSize={int(580*a->scale),int(360*a->scale)};return 0;case WM_ERASEBKGND:{RECT r;GetClientRect(h,&r);FillRect((HDC)wp,&r,(HBRUSH)(COLOR_WINDOW+1));return 1;}case WM_COMMAND:if(LOWORD(wp)==2)a->commit();if(LOWORD(wp)==3)a->pasteClipboard();if(LOWORD(wp)==4){if(a->commit())ShowWindow(h,SW_HIDE);}if(LOWORD(wp)==7)a->addRowAtBottom();if(LOWORD(wp)==8)a->deleteSelectedRows();if(LOWORD(wp)==9)a->addColumnAtRight();if(LOWORD(wp)==10)a->deleteSelectedColumns();if(LOWORD(wp)==11)a->rowSpacingDialog();return 0;case WM_CLOSE:if(a->commit())ShowWindow(h,SW_HIDE);return 0;}
        return DefWindowProcW(h,m,wp,lp);
    }
    static constexpr float columnScale=1.55f,rowScale=1.40f;
    std::vector<float> columnEdges(const r62::graphics::TableData& d)const{std::vector<float> edges(1,38.f);for(float width:d.grid.columnWidths)edges.push_back(edges.back()+std::max(8.f,width*columnScale));return edges;}
    std::vector<float> rowEdges(const r62::graphics::TableData& d)const{std::vector<float> edges(1,28.f);for(float height:d.grid.rowHeights)edges.push_back(edges.back()+std::max(10.f,height*rowScale));return edges;}
    void hit(int x,int y,bool extend){r62::graphics::TableData d;if(!data(&d))return;x=int(x/scale);y=int(y/scale);auto xs=columnEdges(d),ys=rowEdges(d);float cx=float(x+scrollX),cy=float(y+scrollY);uint32_t row=0,col=0;while(row+2<ys.size()&&cy>=ys[row+1])++row;while(col+2<xs.size()&&cx>=xs[col+1])++col;row=std::min(row,d.grid.rows()-1);col=std::min(col,d.grid.columns()-1);
        for(auto m:d.grid.merged)if(row>=m.row&&row<m.row+m.rowSpan&&col>=m.column&&col<m.column+m.columnSpan){row=m.row;col=m.column;}
        r1=row;c1=col;if(!extend){r0=row;c0=col;}refresh();}
    static LRESULT CALLBACK gridProc(HWND h,UINT m,WPARAM wp,LPARAM lp){auto a=(StudioTableEditor*)GetWindowLongPtrW(h,GWLP_USERDATA);if(m==WM_NCCREATE){a=(StudioTableEditor*)((CREATESTRUCTW*)lp)->lpCreateParams;SetWindowLongPtrW(h,GWLP_USERDATA,(LONG_PTR)a);}if(!a)return DefWindowProcW(h,m,wp,lp);
        switch(m){case WM_GETDLGCODE:return DLGC_WANTARROWS|DLGC_WANTTAB|DLGC_WANTCHARS;
        case WM_LBUTTONDOWN:if(!a->commit())return 0;SetFocus(h);a->hit(GET_X_LPARAM(lp),GET_Y_LPARAM(lp),GetKeyState(VK_SHIFT)<0);a->dragging=true;SetCapture(h);return 0;
        case WM_CONTEXTMENU:{POINT screen={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};if(screen.x==-1&&screen.y==-1)GetCursorPos(&screen);POINT client=screen;ScreenToClient(h,&client);if(!a->commit())return 0;a->hit(client.x,client.y,false);a->contextMenu(screen);return 0;}
        case WM_MOUSEMOVE:if(a->dragging)a->hit(GET_X_LPARAM(lp),GET_Y_LPARAM(lp),true);return 0;case WM_LBUTTONUP:a->dragging=false;ReleaseCapture();return 0;
        case WM_LBUTTONDBLCLK:SetFocus(a->edit);studio_i18n::sendMessage(a->edit,EM_SETSEL,0,-1);return 0;
        case WM_KEYDOWN:if(GetKeyState(VK_CONTROL)<0&&(wp=='Z'||wp=='Y')){a->session->undo(wp=='Y'||GetKeyState(VK_SHIFT)<0);a->refresh();return 0;}if(GetKeyState(VK_CONTROL)<0&&wp=='V'){a->pasteClipboard();return 0;}if(GetKeyState(VK_CONTROL)<0&&wp=='A'){a->selectAll();return 0;}if(wp==VK_DELETE){a->apply("clear");return 0;}if(wp==VK_RETURN){SetFocus(a->edit);studio_i18n::sendMessage(a->edit,EM_SETSEL,0,-1);return 0;}if(wp==VK_TAB){a->navigate(GetKeyState(VK_SHIFT)<0?-1:1,0);return 0;}if(wp>=VK_LEFT&&wp<=VK_DOWN){a->navigate(wp==VK_LEFT?-1:wp==VK_RIGHT?1:0,wp==VK_UP?-1:wp==VK_DOWN?1:0);return 0;}break;
        case WM_CHAR:if(wp>=32&&GetKeyState(VK_CONTROL)>=0){SetFocus(a->edit);studio_i18n::sendMessage(a->edit,EM_SETSEL,0,-1);studio_i18n::sendMessage(a->edit,WM_CHAR,wp,lp);return 0;}break;
        case WM_MOUSEWHEEL:a->scrollY=std::max(0,a->scrollY-GET_WHEEL_DELTA_WPARAM(wp)/WHEEL_DELTA*72);InvalidateRect(h,nullptr,FALSE);return 0;
        case WM_VSCROLL:case WM_HSCROLL:{int& offset=m==WM_VSCROLL?a->scrollY:a->scrollX;SCROLLINFO info={sizeof(info),SIF_ALL};GetScrollInfo(h,m==WM_VSCROLL?SB_VERT:SB_HORZ,&info);int code=LOWORD(wp);if(code==SB_THUMBTRACK)offset=info.nTrackPos;else offset+=code==SB_LINEUP?-36:code==SB_LINEDOWN?36:code==SB_PAGEUP?-200:code==SB_PAGEDOWN?200:0;offset=std::max(0,offset);InvalidateRect(h,nullptr,FALSE);return 0;}
        case WM_ERASEBKGND:return 1;
        case WM_PAINT:{PAINTSTRUCT ps;HDC dc=BeginPaint(h,&ps);RECT rc;GetClientRect(h,&rc);int pw=rc.right,ph=rc.bottom;rc.right=LONG(pw/a->scale);rc.bottom=LONG(ph/a->scale);Bitmap buffer(std::max(1,pw),std::max(1,ph),PixelFormat32bppARGB);{Graphics g(&buffer);g.ScaleTransform(a->scale,a->scale);g.Clear(Color(255,255,255,255));g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);r62::graphics::TableData d;
            if(a->data(&d)){uint32_t top,left,bottom,right;a->bounds(top,left,bottom,right);auto xs=a->columnEdges(d),ys=a->rowEdges(d);int totalW=int(xs.back()),totalH=int(ys.back());int maxX=std::max(0,totalW-int(rc.right)),maxY=std::max(0,totalH-int(rc.bottom));a->scrollX=std::min(a->scrollX,maxX);a->scrollY=std::min(a->scrollY,maxY);SCROLLINFO sx={sizeof(sx),SIF_RANGE|SIF_PAGE|SIF_POS,0,totalW,UINT(rc.right),a->scrollX};SCROLLINFO sy={sizeof(sy),SIF_RANGE|SIF_PAGE|SIF_POS,0,totalH,UINT(rc.bottom),a->scrollY};SetScrollInfo(h,SB_HORZ,&sx,TRUE);SetScrollInfo(h,SB_VERT,&sy,TRUE);
                SolidBrush head(Color(255,241,246,245)),selected(Color(255,219,241,236));Pen border(Color(255,211,222,220),1),active(Color(255,0,126,115),2);g.FillRectangle(&head,0,0,rc.right,28);g.FillRectangle(&head,0,0,38,rc.bottom);
                for(uint32_t row=0;row<d.grid.rows();++row)for(uint32_t col=0;col<d.grid.columns();++col){if(r62::graphics::coveredCell(d.grid,row,col))continue;int rs=1,cs=1;for(auto merge:d.grid.merged)if(merge.row==row&&merge.column==col){rs=int(merge.rowSpan);cs=int(merge.columnSpan);}RectF b(xs[col]-a->scrollX,ys[row]-a->scrollY,xs[col+cs]-xs[col],ys[row+rs]-ys[row]);if(b.GetRight()<38||b.GetBottom()<28||b.X>rc.right||b.Y>rc.bottom)continue;auto state=g.Save();g.SetClip(Rect(38,28,rc.right-38,rc.bottom-28));if(row<=bottom&&row+rs>top&&col<=right&&col+cs>left)g.FillRectangle(&selected,b);g.DrawRectangle(&border,b);std::string text;r62::graphics::cellText(a->session->doc,a->table,row,col,&text,nullptr);rawTxt(g,text,RectF(b.X+5,b.Y+5,b.Width-10,b.Height-10),12,Color(255,35,55,55));if(row==a->r1&&col==a->c1)g.DrawRectangle(&active,b);g.Restore(state);}
                for(uint32_t row=0;row<d.grid.rows();++row){float y=ys[row]-a->scrollY;float height=ys[row+1]-ys[row];if(y>=28&&y<rc.bottom)txt(g,std::to_string(row+1),RectF(0,y,36,height),11,Color(255,75,100,95),false,true);}for(uint32_t col=0;col<d.grid.columns();++col){float x=xs[col]-a->scrollX;float width=xs[col+1]-xs[col];if(x>=38&&x<rc.right)txt(g,std::to_string(col+1),RectF(x,0,width,28),11,Color(255,75,100,95),false,true);}
            }}Graphics out(dc);out.DrawImage(&buffer,Rect(0,0,pw,ph),0,0,pw,ph,UnitPixel);out.Flush();EndPaint(h,&ps);return 0;}
        }return DefWindowProcW(h,m,wp,lp);
    }
};
