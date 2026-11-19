#pragma once
#include "template_thumbnail.hpp"
#include "calculator_model.hpp"
struct CalculatorDialog {
    Session* session=nullptr;std::vector<yieldcalc::Row> rows;
    struct Fields {HWND mass=0,massUnit=0,amount=0,amountUnit=0,amountLabel=0,coefficient=0,role=0,result=0,include=0;bool massSource=true;int autoUnit=1;std::shared_ptr<TemplateThumbnail> thumbnail;};
    std::vector<Fields> fields;std::vector<HWND> inputs,keyButtons;
    HWND hwnd=0,tabs=0,yieldPage=0,cards=0,massPage=0,start=0,startUnit=0,startCoefficient=0,status=0,insertButton=0,annotationUnit=0,massChoice=0,lastInput=0;
    HWND massPreview=0;std::shared_ptr<TemplateThumbnail> massThumbnail;MassCalculatorDialog mass;HFONT font=0,bold=0,keyFont=0;double dpi=1;int offset=0,activeTab=0,previousStartUnit=0;bool quiet=false;
    static std::wstring text(HWND h){int n=GetWindowTextLengthW(h);std::vector<wchar_t> buffer(size_t(n)+1);GetWindowTextW(h,buffer.data(),int(buffer.size()));return buffer.data();}
    int px(double n)const{return int(std::lround(n*dpi));}
    int selection(HWND h)const{return int(SendMessageW(h,CB_GETCURSEL,0,0));}
    HWND control(HWND parent,const wchar_t* cls,const std::wstring& value,int id,int x,int y,int w,int h,DWORD style=0){
        auto c=studio_i18n::createWindowEx(0,cls,value.c_str(),WS_CHILD|WS_VISIBLE|style,px(x),px(y),px(w),px(h),parent,(HMENU)(INT_PTR)id,GetModuleHandleW(0),nullptr);
        SendMessageW(c,WM_SETFONT,(WPARAM)font,TRUE);return c;
    }
    HWND combo(HWND parent,int id,int x,int y,int width,const std::vector<std::wstring>& values,int chosen){
        auto c=control(parent,L"COMBOBOX",L"",id,x,y,width,220,CBS_DROPDOWNLIST|WS_TABSTOP|WS_VSCROLL);
        for(const auto& value:values)SendMessageW(c,CB_ADDSTRING,0,(LPARAM)value.c_str());SendMessageW(c,CB_SETCURSEL,chosen,0);return c;
    }
    static LRESULT CALLBACK inputProc(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR data){
        auto d=(CalculatorDialog*)data;
        if(m==WM_SETFOCUS){d->lastInput=h;for(size_t i=0;i<d->fields.size();++i)if(h==d->fields[i].mass||h==d->fields[i].amount||h==d->fields[i].coefficient){d->ensureVisible(i);break;}}
        if(m==WM_NCDESTROY)RemoveWindowSubclass(h,inputProc,1);return DefSubclassProc(h,m,w,l);
    }
    HWND edit(HWND parent,int id,int x,int y,int w,const std::wstring& value=L""){
        auto h=control(parent,L"EDIT",value,id,x,y,w,28,WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL);
        SendMessageW(h,EM_SETLIMITTEXT,80,0);SetWindowSubclass(h,inputProc,1,(DWORD_PTR)this);inputs.push_back(h);return h;
    }
    void set(HWND h,const std::wstring& value){SetWindowTextW(h,value.c_str());}
    double startFactor()const{const double values[]={.001,1.,.000001};return values[std::max(0,std::min(2,selection(startUnit)))];}
    void ensureVisible(size_t index){
        int left=int(index)*236,right=left+224,current=offset;if(left<offset)offset=left;else if(right>offset+730)offset=right-730;
        if(offset!=current)moveCards();
    }
    void moveCards(){
        int maximum=std::max(0,int(rows.size())*236-12-730);offset=std::max(0,std::min(maximum,offset));
        for(size_t i=0;i<fields.size();++i){auto& f=fields[i];int x=int(i)*236-offset;auto move=[&](HWND h,int dx,int y,int w,int height){SetWindowPos(h,0,px(x+dx),px(y),px(w),px(height),SWP_NOZORDER|SWP_NOACTIVATE);};
            move(f.include,12,8,198,24);move(f.mass,12,218,132,28);move(f.massUnit,150,218,62,220);
            move(f.amountLabel,12,254,198,22);move(f.amount,12,276,132,28);move(f.amountUnit,150,276,62,220);
            move(f.role,12,318,100,220);move(f.coefficient,166,318,46,28);move(f.result,12,348,200,24);
        }
        SCROLLINFO info={};info.cbSize=sizeof(info);info.fMask=SIF_RANGE|SIF_PAGE|SIF_POS;info.nMin=0;info.nMax=std::max(729,int(rows.size())*236-13);info.nPage=730;info.nPos=offset;SetScrollInfo(cards,SB_HORZ,&info,TRUE);InvalidateRect(cards,nullptr,TRUE);
    }
    void renderRow(size_t i,bool normalize=false){
        auto& row=rows[i];auto& f=fields[i];bool wasQuiet=quiet;quiet=true;
        if(row.valid){
            if(f.massSource||normalize)f.autoUnit=yieldcalc::amountUnit(row.mol);
            int unit=row.amountDisplay?row.amountDisplay:f.autoUnit;
            if(!f.massSource||normalize)set(f.mass,yieldcalc::compact(row.grams/yieldcalc::massFactor(row.massUnit)));
            if(f.massSource||normalize)set(f.amount,yieldcalc::compact(row.mol/yieldcalc::amountFactor(unit)));
        }else{if(f.massSource)set(f.amount,L"");else set(f.mass,L"");}
        int unit=row.amountDisplay?row.amountDisplay:f.autoUnit;const wchar_t* names[]={L"",L"mmol",L"mol",L"µmol"};
        set(f.amountLabel,std::wstring(L"物质的量（")+names[unit]+L"）");quiet=wasQuiet;
    }
    void recalculate(size_t i,bool fromMass){
        if(quiet)return;auto& row=rows[i];auto& f=fields[i];f.massSource=fromMass;double value=0;
        std::wstring raw=text(fromMass?f.mass:f.amount);row.valid=false;row.yieldValid=false;row.problem.clear();
        if(!raw.empty()){
            if(!row.formula.valid)row.problem="分子量不可用";
            else if(!yieldcalc::number(raw,value))row.problem="请输入非负数值";
            else{double factor=fromMass?yieldcalc::massFactor(row.massUnit):yieldcalc::amountFactor(row.amountDisplay?row.amountDisplay:f.autoUnit);
                row.valid=yieldcalc::convert(value*factor,row.formula.averageMass,fromMass,row.grams,row.mol);if(!row.valid)row.problem="数值超出范围";}
        }
        renderRow(i);refreshResults();
    }
    void refreshResults(){
        if(quiet)return;quiet=true;double initial=0,initialCoefficient=1;bool haveInitial=false;
        haveInitial=yieldcalc::number(text(start),initial,true);initial*=startFactor();
        haveInitial=yieldcalc::number(text(startCoefficient),initialCoefficient,true)&&haveInitial;
        bool any=false;
        for(size_t i=0;i<rows.size();++i){auto& row=rows[i];auto& f=fields[i];
            row.role=selection(f.role);row.include=SendMessageW(f.include,BM_GETCHECK,0,0)==BST_CHECKED;
            bool coefficientOk=yieldcalc::number(text(f.coefficient),row.coefficient,true);
            row.initialMol=haveInitial?initial:0;row.initialCoefficient=initialCoefficient;
            row.yieldValid=row.valid&&row.role==2&&coefficientOk&&haveInitial&&yieldcalc::percent(row.mol,initial,row.coefficient,initialCoefficient,row.yield);
            std::wstring result;
            if(!row.problem.empty())result=W(row.problem);
            else if(!coefficientOk)result=L"计量系数须大于 0";
            else if(row.yieldValid)result=L"Yield = "+yieldcalc::compact(row.yield,5)+L" %";
            else if(row.valid)result=row.role==2?L"填写起始物用量后显示产率":L"质量与物质的量已换算";
            set(f.result,result);any|=row.valid&&coefficientOk&&row.include;
        }
        EnableWindow(insertButton,any);set(status,haveInitial?L"产率按填写的起始物用量及计量系数计算":L"填写起始物用量后显示产率");quiet=false;
    }
    void makeYieldPage(){
        yieldPage=control(hwnd,L"STATIC",L"",600,12,52,750,500,WS_CLIPCHILDREN);
        SetWindowLongPtrW(yieldPage,GWL_EXSTYLE,GetWindowLongPtrW(yieldPage,GWL_EXSTYLE)|WS_EX_CONTROLPARENT);
        SetWindowSubclass(yieldPage,pageProc,1,(DWORD_PTR)this);
        double remembered=0,rememberedCoefficient=1;for(const auto& row:rows)if(row.initialMol>0){remembered=row.initialMol;rememberedCoefficient=row.initialCoefficient;break;}
        control(yieldPage,L"STATIC",L"起始物物质的量",0,8,8,112,24);start=edit(yieldPage,602,124,4,104,remembered>0?yieldcalc::compact(remembered*1000):L"");
        startUnit=combo(yieldPage,603,234,4,78,{L"mmol",L"mol",L"µmol"},0);
        control(yieldPage,L"STATIC",L"计量系数",0,330,8,68,24);startCoefficient=edit(yieldPage,604,402,4,54,yieldcalc::compact(rememberedCoefficient));
        status=control(yieldPage,L"STATIC",L"",605,8,42,728,24);
        WNDCLASSW cls={};cls.lpfnWndProc=cardsProc;cls.hInstance=GetModuleHandleW(0);cls.hCursor=LoadCursorW(0,MAKEINTRESOURCEW(32512));cls.hbrBackground=(HBRUSH)GetStockObject(WHITE_BRUSH);cls.lpszClassName=L"MoleculeStudioCalculatorCards";RegisterClassW(&cls);
        cards=CreateWindowExW(0,cls.lpszClassName,L"",WS_CHILD|WS_VISIBLE|WS_HSCROLL|WS_CLIPCHILDREN,px(8),px(72),px(730),px(428),yieldPage,(HMENU)610,GetModuleHandleW(0),this);
        SetWindowLongPtrW(cards,GWL_EXSTYLE,GetWindowLongPtrW(cards,GWL_EXSTYLE)|WS_EX_CONTROLPARENT);
        fields.resize(rows.size());
        for(size_t i=0;i<rows.size();++i){auto& f=fields[i];auto& row=rows[i];int base=1000+int(i)*20;
            f.include=control(cards,L"BUTTON",L"化合物 "+std::to_wstring(i+1),base,0,0,198,24,BS_AUTOCHECKBOX|WS_TABSTOP);SendMessageW(f.include,BM_SETCHECK,BST_CHECKED,0);
            f.mass=edit(cards,base+1,0,0,132);f.massUnit=combo(cards,base+2,0,0,62,{L"g",L"mg",L"µg",L"kg"},row.massUnit);
            f.amountLabel=control(cards,L"STATIC",L"物质的量（mmol）",base+3,0,0,198,22);
            f.amount=edit(cards,base+4,0,0,132);f.amountUnit=combo(cards,base+5,0,0,62,{L"自动",L"mmol",L"mol",L"µmol"},0);
            f.role=combo(cards,base+6,0,0,100,{L"反应物",L"试剂",L"产物"},row.role);f.coefficient=edit(cards,base+7,0,0,46,yieldcalc::compact(row.coefficient));
            f.result=control(cards,L"STATIC",L"",base+8,0,0,200,36);
            f.thumbnail=std::make_shared<TemplateThumbnail>(row.preview);
            f.thumbnail->maxBondPixels=36*dpi;
            if(row.valid)set(f.mass,yieldcalc::compact(row.grams));renderRow(i);
        }
        moveCards();
    }
    void createMassPage(){
        if(massPage)DestroyWindow(massPage);int index=selection(massChoice);mass.formula=index>=0&&size_t(index)<rows.size()?rows[size_t(index)].formula:Formula();
        massThumbnail.reset();if(index>=0&&size_t(index)<rows.size()){massThumbnail=std::make_shared<TemplateThumbnail>(rows[size_t(index)].preview);massThumbnail->maxBondPixels=64*dpi;}
        mass.hostDpi=dpi;massPage=mass.createPage(hwnd);SetWindowPos(massPage,0,px(12),px(52),px(750),px(505),SWP_NOZORDER);ShowWindow(massPage,activeTab?SW_SHOW:SW_HIDE);if(massPreview)InvalidateRect(massPreview,nullptr,TRUE);
    }
    void switchPage(){
        activeTab=int(SendMessageW(tabs,TCM_GETCURSEL,0,0));ShowWindow(yieldPage,activeTab?SW_HIDE:SW_SHOW);ShowWindow(massPage,activeTab?SW_SHOW:SW_HIDE);ShowWindow(massChoice,activeTab?SW_SHOW:SW_HIDE);
        ShowWindow(insertButton,activeTab?SW_HIDE:SW_SHOW);ShowWindow(annotationUnit,activeTab?SW_HIDE:SW_SHOW);ShowWindow(GetDlgItem(hwnd,704),activeTab?SW_HIDE:SW_SHOW);
        for(auto h:keyButtons)ShowWindow(h,activeTab?SW_HIDE:SW_SHOW);for(int id:{800,801})ShowWindow(GetDlgItem(hwnd,id),activeTab?SW_HIDE:SW_SHOW);ShowWindow(massPreview,activeTab?SW_SHOW:SW_HIDE);ShowWindow(GetDlgItem(hwnd,802),activeTab?SW_SHOW:SW_HIDE);if(!activeTab&&start)SetFocus(start);
    }
    void key(int id){
        if(!lastInput||!IsWindow(lastInput)||activeTab)return;
        if(id==15){auto found=std::find(inputs.begin(),inputs.end(),lastInput);size_t at=found==inputs.end()?0:size_t(found-inputs.begin()+1);for(size_t j=0;j<inputs.size();++j){auto h=inputs[(at+j)%inputs.size()];if(IsWindowVisible(h)&&!((GetWindowLongPtrW(h,GWL_STYLE)&ES_READONLY))){SetFocus(h);SendMessageW(h,EM_SETSEL,0,-1);break;}}return;}
        SetFocus(lastInput);if(GetWindowLongPtrW(lastInput,GWL_STYLE)&ES_READONLY)return;
        const wchar_t* keys[]={L"7",L"8",L"9",L"",L"4",L"5",L"6",L"",L"1",L"2",L"3",L"",L"0",L".",L"e",L""};
        if(id==3){DWORD a=0,b=0;SendMessageW(lastInput,EM_GETSEL,(WPARAM)&a,(LPARAM)&b);if(a==b&&a>0)SendMessageW(lastInput,EM_SETSEL,a-1,b);SendMessageW(lastInput,EM_REPLACESEL,TRUE,(LPARAM)L"");}
        else if(id==7){SetWindowTextW(lastInput,L"");}
        else if(id==11){auto value=text(lastInput);DWORD at=0;SendMessageW(lastInput,EM_GETSEL,(WPARAM)&at,0);auto exponent=value.find_last_of(L"eE");
            if(exponent!=std::wstring::npos&&at>exponent){size_t sign=exponent+1;if(sign<value.size()&&value[sign]==L'-')value.erase(sign,1);else if(sign<value.size()&&value[sign]==L'+')value[sign]=L'-';else value.insert(sign,1,L'-');}
            else if(!value.empty()&&value.front()==L'-')value.erase(value.begin());else value=L"-"+value;
            SetWindowTextW(lastInput,value.c_str());SendMessageW(lastInput,EM_SETSEL,value.size(),value.size());}
        else SendMessageW(lastInput,EM_REPLACESEL,TRUE,(LPARAM)keys[id]);
    }
    void command(int id,int notification){
        if(quiet)return;
        if((id==602||id==604)&&notification==EN_CHANGE){refreshResults();return;}
        if(id==603&&notification==CBN_SELCHANGE){{double value=0;const double factors[]={.001,1.,.000001};if(yieldcalc::number(text(start),value)){quiet=true;set(start,yieldcalc::compact(value*factors[previousStartUnit]/startFactor(),14));quiet=false;}}previousStartUnit=std::max(0,std::min(2,selection(startUnit)));refreshResults();return;}
        if(id<1000||size_t((id-1000)/20)>=rows.size())return;
        size_t i=size_t((id-1000)/20);int field=(id-1000)%20;auto& row=rows[i];auto& f=fields[i];
        if((field==1||field==4)&&notification==EN_CHANGE){recalculate(i,field==1);return;}
        if(field==4&&notification==EN_KILLFOCUS){renderRow(i,true);return;}
        if((field==2||field==5)&&notification==CBN_SELCHANGE){
            row.massUnit=selection(f.massUnit);row.amountDisplay=selection(f.amountUnit);renderRow(i,true);refreshResults();return;}
        if((field==6&&notification==CBN_SELCHANGE)||(field==7&&notification==EN_CHANGE)||(field==0&&notification==BN_CLICKED))refreshResults();
    }
    static LRESULT CALLBACK pageProc(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR data){
        auto d=(CalculatorDialog*)data;if(m==WM_COMMAND){d->command(LOWORD(w),HIWORD(w));return 0;}if(m==WM_NCDESTROY)RemoveWindowSubclass(h,pageProc,1);return DefSubclassProc(h,m,w,l);
    }
    static LRESULT CALLBACK cardsProc(HWND h,UINT m,WPARAM w,LPARAM l){
        auto d=(CalculatorDialog*)GetWindowLongPtrW(h,GWLP_USERDATA);if(m==WM_NCCREATE){d=(CalculatorDialog*)((CREATESTRUCTW*)l)->lpCreateParams;SetWindowLongPtrW(h,GWLP_USERDATA,(LONG_PTR)d);}if(!d)return DefWindowProcW(h,m,w,l);
        if(m==WM_COMMAND){d->command(LOWORD(w),HIWORD(w));return 0;}
        if(m==WM_CTLCOLORSTATIC||m==WM_CTLCOLORBTN){SetBkMode((HDC)w,TRANSPARENT);SetTextColor((HDC)w,RGB(40,52,53));return (LRESULT)GetStockObject(WHITE_BRUSH);}
        if(m==WM_HSCROLL){int at=d->offset;switch(LOWORD(w)){case SB_LINELEFT:at-=40;break;case SB_LINERIGHT:at+=40;break;case SB_PAGELEFT:at-=236;break;case SB_PAGERIGHT:at+=236;break;case SB_THUMBTRACK:case SB_THUMBPOSITION:{SCROLLINFO info={};info.cbSize=sizeof(info);info.fMask=SIF_TRACKPOS;GetScrollInfo(h,SB_HORZ,&info);at=info.nTrackPos;break;}}d->offset=at;d->moveCards();return 0;}
        if(m==WM_MOUSEWHEEL){d->offset-=GET_WHEEL_DELTA_WPARAM(w)/WHEEL_DELTA*118;d->moveCards();return 0;}
        if(m==WM_PAINT){PAINTSTRUCT paint;HDC dc=BeginPaint(h,&paint);Graphics g(dc);g.Clear(Color(255,247,249,248));g.SetSmoothingMode(SmoothingModeAntiAlias);
            for(size_t i=0;i<d->rows.size();++i){float x=float(d->px(int(i)*236-d->offset));auto rect=[&](float a,float b,float c,float e){return RectF(x+float(d->px(a)),float(d->px(b)),float(d->px(c)),float(d->px(e)));};auto& row=d->rows[i];
                roundbox(g,rect(0,0,224,388),float(d->px(7)),white);if(d->fields[i].thumbnail)d->fields[i].thumbnail->paint(g,rect(12,32,200,108));
                txt(g,displayFormula(row.formula.plain),rect(12,144,200,24),float(d->px(15)),ink,true,true);
                txt(g,row.formula.valid?U(yieldcalc::compact(row.formula.averageMass,7))+" g/mol":"分子量不可用",rect(12,170,200,22),float(d->px(12)),muted,false,true);
                txt(g,"质量",rect(12,196,200,22),float(d->px(13)));txt(g,"系数",rect(120,320,42,22),float(d->px(12)));
            }
            if(d->rows.empty())txt(g,"请先在画布中选中一个分子，或框选整条反应式。",RectF(18,60,float(d->px(690)),80),float(d->px(16)),muted,false,true);
            EndPaint(h,&paint);return 0;}
        return DefWindowProcW(h,m,w,l);
    }
    static INT_PTR CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){
        auto d=(CalculatorDialog*)GetWindowLongPtrW(h,DWLP_USER);
        if(m==WM_INITDIALOG){d=(CalculatorDialog*)l;d->hwnd=h;SetWindowLongPtrW(h,DWLP_USER,(LONG_PTR)d);SetWindowTextW(h,L"计算器");
            MONITORINFO monitor={};monitor.cbSize=sizeof(monitor);GetMonitorInfoW(MonitorFromWindow(GetParent(h),MONITOR_DEFAULTTONEAREST),&monitor);auto work=monitor.rcWork;
            d->dpi=std::min({GetDpiForWindow(h)/96.,(work.right-work.left-32)/1040.,(work.bottom-work.top-40)/640.});
            int width=d->px(1040),height=d->px(624);SetWindowPos(h,0,work.left+(work.right-work.left-width)/2,work.top+(work.bottom-work.top-height)/2,width,height,SWP_NOZORDER);
            d->font=CreateFontW(-d->px(14),0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");
            d->tabs=d->control(h,WC_TABCONTROLW,L"",700,12,10,750,32,WS_TABSTOP);
            for(int i=0;i<2;++i){TCITEMW item={};item.mask=TCIF_TEXT;item.pszText=(LPWSTR)(i?L"质谱计算器":L"产率计算器");SendMessageW(d->tabs,TCM_INSERTITEMW,i,(LPARAM)&item);}
            std::vector<std::wstring> choices;for(size_t i=0;i<d->rows.size();++i)choices.push_back(std::to_wstring(i+1)+L" · "+W(displayFormula(d->rows[i].formula.plain)));
            d->massChoice=d->combo(h,701,366,13,368,choices,0);ShowWindow(d->massChoice,SW_HIDE);
            d->control(h,L"STATIC",L"标注质量单位",704,20,564,100,26);d->annotationUnit=d->combo(h,702,124,560,86,{L"自动",L"g",L"mg",L"µg"},0);
            d->insertButton=d->control(h,L"BUTTON",L"插入质量和产率",703,230,560,180,32,WS_TABSTOP);
            d->control(h,L"BUTTON",L"关闭",IDCANCEL,874,560,126,32,WS_TABSTOP);
            d->control(h,L"STATIC",L"软键盘",800,790,68,200,28);
            const wchar_t* keys[]={L"7",L"8",L"9",L"⌫",L"4",L"5",L"6",L"清空",L"1",L"2",L"3",L"±",L"0",L".",L"e",L"下一项"};
            d->keyFont=CreateFontW(-d->px(23),0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");
            for(int i=0;i<16;++i){auto key=d->control(h,L"BUTTON",keys[i],9000+i,780+(i%4)*60,108+(i/4)*84,56,78,WS_TABSTOP);if(i!=7&&i!=15)SendMessageW(key,WM_SETFONT,(WPARAM)d->keyFont,TRUE);d->keyButtons.push_back(key);}
            d->control(h,L"STATIC",L"点击输入框后使用\n支持小数和科学计数法",801,790,456,218,60);
            d->control(h,L"STATIC",L"所选结构",802,790,68,218,28);
            d->massPreview=d->control(h,L"STATIC",L"",803,780,108,236,288,SS_OWNERDRAW);ShowWindow(d->massPreview,SW_HIDE);ShowWindow(GetDlgItem(h,802),SW_HIDE);
            d->quiet=true;d->makeYieldPage();d->createMassPage();d->quiet=false;d->refreshResults();d->switchPage();return FALSE;
        }
        if(!d)return FALSE;
        if(m==WM_COMMAND){int id=LOWORD(w);if(id==IDCANCEL||id==IDOK){EndDialog(h,0);return TRUE;}
            if(id>=9000&&id<9016){d->key(id-9000);return TRUE;}
            if(id==701&&HIWORD(w)==CBN_SELCHANGE){d->createMassPage();return TRUE;}
            if(id==703){d->refreshResults();auto error=yieldcalc::insert(*d->session,d->rows,d->selection(d->annotationUnit));SetWindowTextW(d->status,error.ok()?L"已插入结构下方 · 可继续修改后更新 · Ctrl+Z 可撤销":W(error.message).c_str());return TRUE;}
        }
        if(m==WM_NOTIFY&&((NMHDR*)l)->hwndFrom==d->tabs&&((NMHDR*)l)->code==TCN_SELCHANGE){d->switchPage();return TRUE;}
        if(m==WM_DRAWITEM&&((DRAWITEMSTRUCT*)l)->hwndItem==d->massPreview){auto draw=(DRAWITEMSTRUCT*)l;Graphics g(draw->hDC);g.Clear(white);if(d->massThumbnail){auto rect=draw->rcItem;d->massThumbnail->paint(g,RectF(8,8,float(rect.right-rect.left-16),float(rect.bottom-rect.top-16)));}return TRUE;}
        if(m==WM_CLOSE){EndDialog(h,0);return TRUE;}
        if(m==WM_DESTROY){if(d->font)DeleteObject(d->font);if(d->keyFont)DeleteObject(d->keyFont);return TRUE;}return FALSE;
    }
    void show(HWND owner,Session& source){session=&source;rows=yieldcalc::collect(source,source.selected);
        struct Template{DLGTEMPLATE t;WORD menu,cls,title;}t={};t.t.style=WS_POPUP|WS_CAPTION|WS_SYSMENU|DS_MODALFRAME|WS_CLIPCHILDREN;t.t.cx=1040;t.t.cy=624;
        DialogBoxIndirectParamW(GetModuleHandleW(0),&t.t,owner,proc,(LPARAM)this);
    }
};
void App::showMassCalculator(){if(textEditor.active&&!finishText(true))return;updateInfo();CalculatorDialog dialog;dialog.show(h,s);updateInfo();invalidate();}
