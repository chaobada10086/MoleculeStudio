#pragma once
#include "catalytic_metal.hpp"
struct CycleFields {
    std::wstring title;std::vector<std::wstring> labels;std::vector<std::string> values;CatalyticMetal metal;
    bool cycle=false,clockwise=true,fixedSize=false;double rotation=0,scale=1;std::string issue;
    std::shared_ptr<TemplateThumbnail> cyclePreview,metalPreview;std::shared_ptr<Session> selectedSource;
    HFONT font=nullptr;HWND window=nullptr;
    int px(double n)const{return int(std::round(n*scale));}
    void read(){for(size_t i=0;i<values.size();++i){wchar_t v[2048];GetDlgItemTextW(window,710+int(i),v,2048);values[i]=U(v);}auto text=[&](int id){wchar_t v[2048];GetDlgItemTextW(window,id,v,2048);return U(v);};metal.element=text(730);metal.oxidation=text(731);metal.ligands=text(732);metal.numerals=int(SendDlgItemMessageW(window,740,CB_GETCURSEL,0,0));metal.format=int(SendDlgItemMessageW(window,741,CB_GETCURSEL,0,0));metal.excited=IsDlgButtonChecked(window,742)==BST_CHECKED;metal.collapsed=IsDlgButtonChecked(window,743)==BST_CHECKED;}
    bool rebuild(){issue=metal.validate();double count=4,rx=130,ry=110;if(cycle&&(values.size()!=3||!DrawingSettings::parse(W(values[0]).c_str(),3,12,&count)||count!=std::floor(count)||!DrawingSettings::parse(W(values[1]).c_str(),1,2000,&rx)||!DrawingSettings::parse(W(values[2]).c_str(),1,2000,&ry)))issue="步骤需为 3–12 的整数；半径需为 1–2000 pt。";
        if(!issue.empty())return false;try{auto single=std::make_shared<Session>();single->doc.setDefaultFont("Arial");single->doc.setDefaultBondLength(22);metal.apply(single->doc,kInvalidObjectId,Vec2());single->refresh();metalPreview=std::make_shared<TemplateThumbnail>(single);metalPreview->maxBondPixels=120;
            if(cycle){auto demo=std::make_shared<Session>();demo->doc.setDefaultFont("Arial");demo->doc.setDefaultBondLength(22);std::vector<ObjectId> nodes;
                if(selectedSource){std::vector<ObjectRef> copied;studioio::append(selectedSource->doc,demo->doc,selectedSource->all(),Vec2(),&copied);std::set<ObjectId> seen;for(auto r:copied)if(r.kind==ObjectKind::Atom&&!seen.count(r.id)){for(auto x:studio::catalytic::component(demo->doc,r.id))if(x.kind==ObjectKind::Atom)seen.insert(x.id);nodes.push_back(r.id);}}
                if(nodes.empty())for(int i=0;i<int(count);++i)nodes.push_back(metal.apply(demo->doc,kInvalidObjectId,Vec2(i*60,0)));std::vector<ObjectRef> made;auto e=studio::catalytic::arrange(demo->doc,nodes,Vec2(),rx,ry,clockwise,&made,fixedSize,rotation);if(!e.ok())throw std::runtime_error(e.message);demo->refresh();cyclePreview=std::make_shared<TemplateThumbnail>(demo);cyclePreview->maxBondPixels=54;
            }return true;
        }catch(const std::exception& e){issue=e.what();return false;}
    }
    static INT_PTR CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){auto d=(CycleFields*)GetWindowLongPtrW(h,DWLP_USER);
        if(m==WM_INITDIALOG){d=(CycleFields*)l;SetWindowLongPtrW(h,DWLP_USER,(LONG_PTR)d);d->window=h;SetWindowTextW(h,d->title.c_str());
            MONITORINFO monitor={};monitor.cbSize=sizeof(monitor);GetMonitorInfoW(MonitorFromWindow(h,MONITOR_DEFAULTTONEAREST),&monitor);auto work=monitor.rcWork;double fit=std::min({double(GetDpiForWindow(h))/96.,double(work.right-work.left-40)/920.,double(work.bottom-work.top-40)/(d->cycle?650.:510.)});
            int width=int(920*fit),height=int((d->cycle?650:510)*fit);SetWindowPos(h,nullptr,work.left+(work.right-work.left-width)/2,work.top+(work.bottom-work.top-height)/2,width,height,SWP_NOZORDER);
            RECT cr;GetClientRect(h,&cr);d->scale=double(cr.right)/900.;d->font=CreateFontW(-d->px(14),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");
            auto add=[&](const wchar_t* c,const std::wstring& t,int id,int x,int y,int width,int height,DWORD style){auto v=CreateWindowExW(0,c,t.c_str(),WS_CHILD|WS_VISIBLE|style,d->px(x),d->px(y),d->px(width),d->px(height),h,(HMENU)(INT_PTR)id,0,0);SendMessageW(v,WM_SETFONT,(WPARAM)d->font,0);};
            for(size_t i=0;i<d->values.size();++i){add(L"STATIC",d->labels[i],0,20,30+int(i)*40,170,27,0);add(L"EDIT",W(d->values[i]),710+int(i),200,26+int(i)*40,200,28,WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL);}
            int y=d->cycle?170:35;const wchar_t* names[]={L"金属元素",L"氧化态",L"末端配体（英文逗号）"};std::string fields[]={d->metal.element,d->metal.oxidation,d->metal.ligands};for(int i=0;i<3;++i){add(L"STATIC",names[i],0,20,y+i*40,175,27,0);add(L"EDIT",W(fields[i]),730+i,200,y-4+i*40,200,28,WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL);}
            add(L"STATIC",L"价态数字格式",0,20,y+128,175,27,0);add(L"COMBOBOX",L"",740,200,y+124,200,180,CBS_DROPDOWNLIST|WS_TABSTOP|WS_VSCROLL);for(auto s:{L"罗马数字 II / III",L"阿拉伯数字 2 / 3"})SendDlgItemMessageW(h,740,CB_ADDSTRING,0,(LPARAM)s);SendDlgItemMessageW(h,740,CB_SETCURSEL,d->metal.numerals,0);
            add(L"STATIC",L"价态显示位置",0,20,y+168,175,27,0);add(L"COMBOBOX",L"",741,200,y+164,200,180,CBS_DROPDOWNLIST|WS_TABSTOP|WS_VSCROLL);for(auto s:{L"括号上标  Pd⁽ᴵᴵ⁾",L"无括号上标  Pdᴵᴵ",L"同行括号  Pd(II)",L"同行无括号  PdII"})SendDlgItemMessageW(h,741,CB_ADDSTRING,0,(LPARAM)s);SendDlgItemMessageW(h,741,CB_SETCURSEL,d->metal.format,0);
            add(L"BUTTON",L"激发态 *（独立上标）",742,20,y+208,360,28,BS_AUTOCHECKBOX|WS_TABSTOP);CheckDlgButton(h,742,d->metal.excited?BST_CHECKED:BST_UNCHECKED);add(L"BUTTON",L"配合物收起显示（右键可展开）",743,20,y+245,380,28,BS_AUTOCHECKBOX|WS_TABSTOP);CheckDlgButton(h,743,d->metal.collapsed?BST_CHECKED:BST_UNCHECKED);
            int bottom=d->cycle?548:410;add(L"BUTTON",L"确定",IDOK,200,bottom,95,30,BS_DEFPUSHBUTTON|WS_TABSTOP);add(L"BUTTON",L"取消",IDCANCEL,305,bottom,95,30,WS_TABSTOP);d->rebuild();return TRUE;
        }
        if(!d)return FALSE;if(m==WM_COMMAND){int id=LOWORD(w),event=HIWORD(w);if(id==IDCANCEL){EndDialog(h,0);return TRUE;}if(id==IDOK){d->read();if(!d->rebuild()){InvalidateRect(h,nullptr,FALSE);return TRUE;}EndDialog(h,1);return TRUE;}if(event==EN_CHANGE||event==CBN_SELCHANGE||id==742||id==743){SetTimer(h,32,160,nullptr);return TRUE;}}
        if(m==WM_TIMER&&w==32){KillTimer(h,32);d->read();d->rebuild();InvalidateRect(h,nullptr,FALSE);return TRUE;}
        if(m==WM_PAINT){PAINTSTRUCT ps;HDC dc=BeginPaint(h,&ps);Graphics g(dc);g.Clear(Color(255,247,249,248));g.SetSmoothingMode(SmoothingModeAntiAlias);auto rect=[&](int x,int y,int w,int height){return RectF(float(d->px(x)),float(d->px(y)),float(d->px(w)),float(d->px(height)));};
            if(d->cycle){txt(g,"循环路径预览",rect(445,15,425,24),float(d->px(14)),ink,true);roundbox(g,rect(440,45,430,245),7,white);if(d->cyclePreview)d->cyclePreview->paint(g,rect(450,55,410,225));}
            int y=d->cycle?325:20;txt(g,"配合物预览",rect(445,y,425,24),float(d->px(14)),ink,true);roundbox(g,rect(440,y+30,430,d->cycle?210:350),7,white);if(d->metalPreview)d->metalPreview->paint(g,rect(450,y+40,410,d->cycle?190:330));if(!d->issue.empty())txt(g,d->issue,rect(20,d->cycle?470:345,390,62),float(d->px(13)),Color(255,170,45,45));EndPaint(h,&ps);return TRUE;}
        if(m==WM_DESTROY){KillTimer(h,32);if(d->font)DeleteObject(d->font);return TRUE;}return FALSE;
    }
    bool show(HWND owner){struct Template{DLGTEMPLATE t;WORD menu,cls,title;}t={};t.t.style=WS_POPUP|WS_CAPTION|WS_SYSMENU|DS_MODALFRAME;t.t.cx=390;t.t.cy=cycle?310:245;return DialogBoxIndirectParamW(GetModuleHandleW(0),&t.t,owner,proc,(LPARAM)this)==1;}
};
