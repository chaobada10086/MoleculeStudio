#pragma once
#include "chem_completion.hpp"
#include "group_preview.hpp"
#include "drawing_settings.hpp"
namespace groupchoice {
struct Dialog {std::string key,result;bool settings,attached,expanding;
    std::vector<std::string> options;
    std::unique_ptr<Session> preview;
    std::vector<std::unique_ptr<Session>> pictures;
    compatible::Renderer renderer;};
inline std::wstring pref(const std::string& key){return W("Ambiguous."+key);}
inline std::wstring description(const std::string& label){
    if(label.size()>1&&label[0]=='#')return W(label.substr(1)+"   元素 · 原子序数 "+std::to_string(elementNumber(label.substr(1))));
    if(label=="MeS")return L"MeS / SMe   甲硫基（–SCH₃）";
    if(label=="Mes")return L"Mes   2,4,6-三甲基苯基";
    if(auto b=bridgedata::find(label))return W(label+"   双端连接基   "+b->formula);
    if(auto n=nickdata::find(label)){
        for(auto& info:groupreference::descriptions)if(label==info.key)return W(label+"   "+info.value+"   "+n->formula);
        return W(label+"   "+n->formula);
    }
    if(auto c=compounddata::find(label))return W(label+"   "+c->title+"   "+c->formula);
    return W(label);
}
inline std::unique_ptr<Session> pictureFor(const std::string& name){
    std::unique_ptr<Session> picture;
        if(name[0]=='#'){
            picture.reset(new Session());ObjectId atom;auto symbol=name.substr(1);
            picture->doc.addAtom(Vec2(),elementNumber(symbol),&atom);picture->doc.setAtomLabel(atom,symbol);
            auto sem=picture->doc.atoms().alive(atom)->semantics;sem.showLabel=true;sem.implicitHydrogens=0;
            picture->doc.setAtomSemantics(atom,sem);picture->refresh();
        }else{
        auto group=nickdata::find(name);if(!group)group=nickdata::find(nicknameform::oriented(name,false));
        auto bridge=bridgedata::find(name);
        auto compound=compounddata::find(name);
        const char* raw=bridge?bridge->fragment:group?group->fragment:compound?compound->fragment:nullptr;
        if(raw)try{picture=grouppreview::load(raw);}catch(const std::runtime_error&){}
        }
    if(picture){
        DrawingSettings style;style.acsDefaults();auto& doc=picture->doc;
        for(auto& edge:doc.bonds().records())if(edge.isAlive()){
            double len=length(doc.atoms().alive(edge.beginAtom)->position-doc.atoms().alive(edge.endAtom)->position);
            if(len>0)for(auto& atom:doc.atoms().records())if(atom.isAlive())doc.setAtomPosition(atom.id,atom.position*(style.bondLength/len));
            break;
        }
        style.apply(doc);picture->refresh();
    }
    return picture;
}
inline void updatePreview(HWND h,Dialog& d){for(size_t i=0;i<d.options.size();++i)InvalidateRect(GetDlgItem(h,1200+i),nullptr,TRUE);}
inline void drawPreview(Dialog& d,const DRAWITEMSTRUCT& item){
    FillRect(item.hDC,&item.rcItem,static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
    if(!d.preview){RECT box=item.rcItem;DrawTextW(item.hDC,L"结构预览不可用",-1,&box,DT_CENTER|DT_VCENTER|DT_SINGLELINE);return;}
    auto bounds=d.preview->scene.bounds();if(bounds.empty())return;
    auto view=d.preview->renderView;
    int w=item.rcItem.right-item.rcItem.left,h=item.rcItem.bottom-item.rcItem.top;
    double dpi=GetDpiForWindow(item.hwndItem)/96.;
    view.widthPx=w;view.heightPx=h;view.transform.dpiScale=1;
    view.transform.center=(bounds.min+bounds.max)*.5;view.transform.viewportCenter=Vec2(w*.5,h*.5);
    view.transform.scale=std::min(4.*dpi,std::min((w-20*dpi)/std::max(1.,bounds.width()),(h-20*dpi)/std::max(1.,bounds.height())));
    if(d.renderer.draw(d.preview->scene,nullptr,view,nullptr).ok())d.renderer.present(item.hDC);
}
inline bool standaloneChoice(const std::string& name){
    if(name.empty()||name[0]=='#'||nickdata::find(nicknameform::oriented(name,false)))return false;
    auto c=compounddata::find(name);return c&&compounddata::canonical(name)==name;

}
inline void fill(HWND h,Dialog& d){
    auto saved=chemcompletion::readKey(pref(d.key).c_str());
    auto combo=GetDlgItem(h,1102);SendMessageW(combo,CB_RESETCONTENT,0,0);
    for(size_t i=0;i<d.options.size();++i)DestroyWindow(GetDlgItem(h,1200+i));
    int index=0,selected=-1;d.options.clear();d.pictures.clear();
    for(auto& label:chemcompletion::ambiguousGroups().at(d.key)){
        if((d.expanding&&label[0]=='#'))continue;
        d.options.push_back(label);
        auto title=description(label);SendMessageW(combo,CB_ADDSTRING,0,(LPARAM)title.c_str());
        d.pictures.push_back(pictureFor(label));
        if(!(d.expanding&&standaloneChoice(label))&&(label==saved||selected<0))selected=index;
        ++index;
    }
    RECT area;auto pane=GetDlgItem(h,1104);GetWindowRect(pane,&area);MapWindowPoints(nullptr,h,(POINT*)&area,2);
    ShowWindow(pane,SW_HIDE);ShowWindow(combo,SW_HIDE);
    int cols=std::min(3,index),rows=cols?(index+cols-1)/cols:0;
    for(int i=0;i<index;++i){
        int width=(area.right-area.left)/cols,height=(area.bottom-area.top)/rows;
        auto card=CreateWindowW(L"BUTTON",description(d.options[i]).c_str(),WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW|BS_NOTIFY,
            area.left+i%cols*width,area.top+i/cols*height,width-3,height-3,h,(HMENU)(INT_PTR)(1200+i),GetModuleHandleW(nullptr),nullptr);
        SendMessageW(card,WM_SETFONT,SendMessageW(h,WM_GETFONT,0,0),FALSE);
        EnableWindow(card,!(d.expanding&&standaloneChoice(d.options[i])));
    }
    EnableWindow(GetDlgItem(h,IDOK),selected>=0);
    SendMessageW(combo,CB_SETCURSEL,selected,0);CheckDlgButton(h,1103,std::find(d.options.begin(),d.options.end(),saved)==d.options.end()?BST_UNCHECKED:BST_CHECKED);updatePreview(h,d);
}
inline INT_PTR CALLBACK proc(HWND h,UINT msg,WPARAM w,LPARAM l){
    auto d=reinterpret_cast<Dialog*>(GetWindowLongPtrW(h,DWLP_USER));
    if(msg==WM_INITDIALOG){
        d=reinterpret_cast<Dialog*>(l);SetWindowLongPtrW(h,DWLP_USER,l);
        int i=0;for(auto& entry:chemcompletion::ambiguousGroups()){
            SendDlgItemMessageW(h,1101,CB_ADDSTRING,0,(LPARAM)W(entry.first).c_str());
            if(entry.first==d->key)SendDlgItemMessageW(h,1101,CB_SETCURSEL,i,0);++i;
        }
        d->renderer.initialize(nullptr);EnableWindow(GetDlgItem(h,1101),d->settings);fill(h,*d);return TRUE;
    }
    if(msg==WM_DRAWITEM&&d&&w>=1200&&w<1200+d->options.size()){
        size_t i=w-1200;auto item=*reinterpret_cast<DRAWITEMSTRUCT*>(l);RECT box=item.rcItem;
        auto dc=item.hDC;int footer=MulDiv(50,GetDpiForWindow(h),96);
        item.rcItem.bottom=std::max(1L,box.bottom-footer);
        d->preview.swap(d->pictures[i]);drawPreview(*d,item);d->preview.swap(d->pictures[i]);
        RECT caption=box;caption.top=item.rcItem.bottom;FillRect(dc,&caption,(HBRUSH)GetStockObject(WHITE_BRUSH));
        auto title=description(d->options[i]);if(d->attached&&standaloneChoice(d->options[i]))title+=L"（独立分子）";
        SelectObject(dc,(HFONT)SendMessageW(h,WM_GETFONT,0,0));SetBkMode(dc,TRANSPARENT);DrawTextW(dc,title.c_str(),-1,&caption,DT_CENTER|DT_WORDBREAK|DT_NOPREFIX);
        bool selected=SendDlgItemMessageW(h,1102,CB_GETCURSEL,0,0)==LRESULT(i);
        FrameRect(dc,&box,GetSysColorBrush(selected?COLOR_HIGHLIGHT:COLOR_3DSHADOW));if(selected){InflateRect(&box,-1,-1);FrameRect(dc,&box,GetSysColorBrush(COLOR_HIGHLIGHT));}return TRUE;
    }
    if(msg==WM_COMMAND&&d&&(HIWORD(w)==BN_CLICKED||HIWORD(w)==BN_DOUBLECLICKED)&&LOWORD(w)>=1200&&LOWORD(w)<1200+d->options.size()){
        SendDlgItemMessageW(h,1102,CB_SETCURSEL,LOWORD(w)-1200,0);updatePreview(h,*d);
        if(HIWORD(w)==BN_DOUBLECLICKED)SendMessageW(h,WM_COMMAND,IDOK,0);return TRUE;
    }
    if(msg==WM_DRAWITEM&&w==1104&&d){drawPreview(*d,*reinterpret_cast<DRAWITEMSTRUCT*>(l));return TRUE;}
    if(msg==WM_COMMAND&&LOWORD(w)==1102&&HIWORD(w)==CBN_SELCHANGE){updatePreview(h,*d);return TRUE;}
    if(msg==WM_COMMAND&&LOWORD(w)==1101&&HIWORD(w)==CBN_SELCHANGE){
        auto it=chemcompletion::ambiguousGroups().begin();std::advance(it,SendDlgItemMessageW(h,1101,CB_GETCURSEL,0,0));d->key=it->first;fill(h,*d);return TRUE;
    }
    if(msg==WM_COMMAND&&LOWORD(w)==IDOK){
        auto selected=SendDlgItemMessageW(h,1102,CB_GETCURSEL,0,0);if(selected<0||size_t(selected)>=d->options.size())return TRUE;
        auto it=d->options.begin()+selected;
        d->result=*it;
        if(!chemcompletion::writeKey(pref(d->key).c_str(),IsDlgButtonChecked(h,1103)==BST_CHECKED?*it:"")){
            MessageBoxW(h,L"无法保存歧义官能团选择",L"MoleculeStudio",MB_OK);return TRUE;
        }
        EndDialog(h,IDOK);return TRUE;
    }
    if(msg==WM_CLOSE||(msg==WM_COMMAND&&LOWORD(w)==IDCANCEL)){EndDialog(h,IDCANCEL);return TRUE;}
    return FALSE;
}
inline std::string choose(HWND owner,const std::string& key,bool settings=false,bool attached=false,bool prompt=false){
    auto saved=chemcompletion::readKey(pref(key).c_str());
    if(!settings&&!prompt&&chemcompletion::ambiguousGroups().at(key).count(saved))return saved;
    Dialog d{key,"",settings,attached,prompt};return DialogBoxParamW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(601),owner,proc,(LPARAM)&d)==IDOK?d.result:"";
}
}
