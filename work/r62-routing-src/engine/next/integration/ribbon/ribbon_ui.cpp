#include "ribbon_ui.hpp"
#include <windowsx.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace r62 { namespace integration { namespace ribbon {
namespace {
std::wstring wide(const std::string& s){
    if(s.empty())return std::wstring();
    int n=MultiByteToWideChar(CP_UTF8,0,s.data(),(int)s.size(),0,0);
    std::wstring r((size_t)(n>0?n:0),L' ');
    if(n>0)MultiByteToWideChar(CP_UTF8,0,s.data(),(int)s.size(),&r[0],n);
    return r;
}
int textW(HDC dc,HFONT f,const std::wstring& s){
    if(s.empty())return 0;HGDIOBJ o=SelectObject(dc,f);SIZE sz={0,0};GetTextExtentPoint32W(dc,s.c_str(),(int)s.size(),&sz);SelectObject(dc,o);return sz.cx;
}
void fillRect(HDC dc,const RECT& r,COLORREF c){HBRUSH b=CreateSolidBrush(c);::FillRect(dc,&r,b);DeleteObject(b);}
void frameRect(HDC dc,const RECT& r,COLORREF c){HBRUSH b=(HBRUSH)GetStockObject(NULL_BRUSH);HGDIOBJ o=SelectObject(dc,b);HPEN p=CreatePen(PS_SOLID,1,c);HGDIOBJ po=SelectObject(dc,p);Rectangle(dc,r.left,r.top,r.right,r.bottom);SelectObject(dc,po);SelectObject(dc,o);DeleteObject(p);}
const COLORREF kPageBg=RGB(255,255,255);
const COLORREF kBarBg=RGB(240,242,245);
const COLORREF kGroupBg=RGB(250,251,253);
const COLORREF kBorder=RGB(214,218,224);
const COLORREF kText=RGB(30,32,36);
const COLORREF kTextDim=RGB(150,154,160);
const COLORREF kHoverBg=RGB(226,239,250);
const COLORREF kHoverBorder=RGB(150,195,235);
const COLORREF kChkBg=RGB(198,224,245);
const COLORREF kChkBorder=RGB(70,140,205);
const COLORREF kAccent=RGB(0,102,184);
}

struct PopupData {
    RibbonBar* bar;
    const Item* owner;
    std::vector<const Item*> items;
    int cols,cellW,cellH,hover;
    PopupData():bar(0),owner(0),cols(1),cellW(80),cellH(50),hover(-1){}
};

RibbonBar::RibbonBar():h_(0),popup_(0),tip_(0),parent_(0),inst_(0),font_(0),fontBold_(0),fontSmall_(0),fontTab_(0),
    dpi_(96),tabH_(25),contentH_(93),curTab_(0),scrollX_(0),focusIndex_(-1),hover_(0),press_(0),hoverTab_(-1),
    contentWidth_(0),focusTab_(0),popupData_(0),tipVisible_(false),hotIndex_(-1){
    lastMouse_.x=lastMouse_.y=0;
}
RibbonBar::~RibbonBar(){
    closePopups();
    if(tip_){DestroyWindow(tip_);tip_=0;}
    if(font_)DeleteObject(font_);
    if(fontBold_)DeleteObject(fontBold_);
    if(fontSmall_)DeleteObject(fontSmall_);
    if(fontTab_)DeleteObject(fontTab_);
}
int RibbonBar::scale(int v) const { return MulDiv(v,dpi_,96); }
int RibbonBar::heightPx() const { return scale(118); }
void RibbonBar::relayout(int dpi){
    if(dpi<=0)dpi=96;
    if(dpi==dpi_){layout();invalidate();return;}
    dpi_=dpi;
    if(font_)DeleteObject(font_);
    if(fontBold_)DeleteObject(fontBold_);
    if(fontSmall_)DeleteObject(fontSmall_);
    if(fontTab_)DeleteObject(fontTab_);
    auto mk=[&](int pt,bool bold,const wchar_t* face){return CreateFontW(-MulDiv(pt,dpi_,72),0,0,0,bold?FW_SEMIBOLD:FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,face);};
    font_=mk(9,false,L"Microsoft YaHei UI");
    fontBold_=mk(9,true,L"Microsoft YaHei UI");
    fontSmall_=mk(8,false,L"Microsoft YaHei UI");
    fontTab_=mk(10,false,L"Microsoft YaHei UI");
    tabH_=scale(25);contentH_=scale(93);
    layout();invalidate();
}

bool RibbonBar::create(HWND parent,HINSTANCE inst,int dpi){
    parent_=parent;inst_=inst;dpi_=dpi?dpi:96;
    WNDCLASSW wc={};wc.style=CS_HREDRAW|CS_VREDRAW;wc.lpfnWndProc=&RibbonBar::proc;wc.hInstance=inst;
    wc.hCursor=LoadCursorW(0,MAKEINTRESOURCEW(32512));wc.lpszClassName=L"T19RibbonBar";
    RegisterClassW(&wc);
    WNDCLASSW pc={};pc.style=CS_HREDRAW|CS_VREDRAW;pc.lpfnWndProc=&RibbonBar::popupProc;pc.hInstance=inst;
    pc.hCursor=LoadCursorW(0,MAKEINTRESOURCEW(32512));pc.lpszClassName=L"T19RibbonPopup";RegisterClassW(&pc);
    WNDCLASSW tc={};tc.style=CS_HREDRAW|CS_VREDRAW;tc.lpfnWndProc=&RibbonBar::tipProc;tc.hInstance=inst;
    tc.lpszClassName=L"T19RibbonTip";RegisterClassW(&tc);
    auto mk=[&](int pt,bool bold,const wchar_t* face){return CreateFontW(-MulDiv(pt,dpi_,72),0,0,0,bold?FW_SEMIBOLD:FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,face);};
    font_=mk(9,false,L"Microsoft YaHei UI");
    fontBold_=mk(9,true,L"Microsoft YaHei UI");
    fontSmall_=mk(8,false,L"Microsoft YaHei UI");
    fontTab_=mk(10,false,L"Microsoft YaHei UI");
    h_=CreateWindowExW(0,L"T19RibbonBar",L"",WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS,0,0,100,heightPx(),parent,0,inst,this);
    if(!h_)return false;
    tabH_=scale(25);contentH_=scale(93);
    layout();
    return true;
}

int RibbonBar::itemWidth(HDC dc,const Item& it) const {
    switch(it.kind){
    case ItemKind::Large:case ItemKind::Split:case ItemKind::Gallery:return scale(58);
    case ItemKind::Separator:return scale(10);
    case ItemKind::Label:return textW(dc,fontSmall_,wide(it.label))+scale(12);
    default:return scale(24)+textW(dc,fontSmall_,wide(it.label))+scale(10);
    }
}

void RibbonBar::layout(){
    hits_.clear();tabRects_.clear();groupRects_.clear();contentWidth_=0;
    if(!h_)return;
    RECT cr;GetClientRect(h_,&cr);
    HDC dc=GetDC(h_);
    const Model& m=ribbonModel();
    int tx=scale(6);
    for(size_t i=0;i<m.tabs.size();++i){
        int tw=textW(dc,fontTab_,wide(m.tabs[i].title))+scale(26);
        RECT r={tx,scale(3),tx+tw,tabH_-scale(1)};
        tabRects_.push_back(r);tx+=tw;
    }
    if(curTab_>=0&&curTab_<(int)m.tabs.size()){
        const Tab& t=m.tabs[curTab_];
        int x=scale(6)-scrollX_;
        int bandTop=tabH_+scale(3);
        int bandBottom=tabH_+contentH_-scale(18);
        for(size_t gi=0;gi<t.groups.size();++gi){
            const Group& g=t.groups[gi];
            int gw=scale(10);
            for(size_t ii=0;ii<g.items.size();++ii)gw+=itemWidth(dc,g.items[ii])+scale(4);
            RECT gr={x,tabH_+scale(2),x+gw,tabH_+contentH_-scale(2)};
            groupRects_.push_back(gr);
            int ix=x+scale(5);
            for(size_t ii=0;ii<g.items.size();++ii){
                const Item& it=g.items[ii];
                int iw=itemWidth(dc,it);
                if(it.kind==ItemKind::Separator){RECT r={ix,bandTop,ix+iw,bandBottom};Hit hh={r,&it};hits_.push_back(hh);ix+=iw;}
                else if(it.kind==ItemKind::Large||it.kind==ItemKind::Split||it.kind==ItemKind::Gallery){
                    RECT r={ix,bandTop,ix+iw,bandTop+scale(62)};Hit hh={r,&it};hits_.push_back(hh);ix+=iw+scale(4);
                }else if(it.kind==ItemKind::Label){
                    RECT r={ix,bandTop,ix+iw,bandBottom};Hit hh={r,&it};hits_.push_back(hh);ix+=iw+scale(4);
                }else{
                    int hgt=scale(26);int y=(bandTop+bandBottom-hgt)/2;
                    RECT r={ix,y,ix+iw,y+hgt};Hit hh={r,&it};hits_.push_back(hh);ix+=iw+scale(4);
                }
            }
            x=gr.right+scale(6);
        }
        contentWidth_=x+scrollX_;
    }
    ReleaseDC(h_,dc);
    RECT cl;GetClientRect(h_,&cl);
    int maxScroll=contentWidth_-cl.right+scale(24);
    if(maxScroll<0)maxScroll=0;
    if(scrollX_>maxScroll){scrollX_=maxScroll;layout();}
}

void RibbonBar::invalidate(){ if(h_)InvalidateRect(h_,0,TRUE); }
void RibbonBar::closePopups(){
    if(popup_){HWND p=popup_;popup_=0;DestroyWindow(p);}
    if(popupData_){delete popupData_;popupData_=0;}
}

bool RibbonBar::isEnabled(const Item& it) const {
    if(it.source==SourceKind::Unsupported)return false;
    if(it.kind==ItemKind::Label)return false;
    if(it.kind==ItemKind::Separator)return false;
    if(state_.disabled.count(it.id))return false;
    if(it.source==SourceKind::SourceCommand&&state_.unavailable.count(it.command))return false;
    return true;
}
std::string RibbonBar::reasonFor(const Item& it) const {
    std::map<std::string,std::string>::const_iterator i=state_.reason.find(it.id);
    if(i!=state_.reason.end())return i->second;
    if(!it.command.empty()){i=state_.reason.find(it.command);if(i!=state_.reason.end())return i->second;}
    if(!it.reason.empty())return it.reason;
    return "此功能在当前装配中未接通，未提供替代行为。";
}

void RibbonBar::emit(const Item& it){
    if(!sink_)return;
    Event e;
    if(it.source==SourceKind::SourceCommand){e.kind=Event::Kind::Command;e.command=it.command;}
    else if(it.source==SourceKind::Action){e.kind=Event::Kind::Action;e.action=it.action;}
    else if(it.source==SourceKind::FileOp){e.kind=Event::Kind::FileOp;e.fileOp=it.value;}
    else if(it.source==SourceKind::PropertyOp){
        if(!isEnabled(it)){e.kind=Event::Kind::PropertyOp;e.command="__refused__";e.text=reasonFor(it);}
        else {e.kind=Event::Kind::PropertyOp;e.command=it.command;e.value=it.value;}
    }else{e.kind=Event::Kind::PropertyOp;e.command="__unsupported__";e.text=reasonFor(it);}
    sink_(e);
}

void RibbonBar::activateItemId(const std::string& id){
    const Model& m=ribbonModel();
    std::vector<const Item*> stack;
    for(size_t i=0;i<m.tabs.size();++i)for(size_t j=0;j<m.tabs[i].groups.size();++j)for(size_t k=0;k<m.tabs[i].groups[j].items.size();++k){
        stack.push_back(&m.tabs[i].groups[j].items[k]);
    }
    while(!stack.empty()){
        const Item* it=stack.back();stack.pop_back();
        if(it->id==id){emit(*it);return;}
        for(size_t c=0;c<it->children.size();++c)stack.push_back(&it->children[c]);
    }
}

void RibbonBar::scrollBy(int dx){
    scrollX_+=dx;
    if(scrollX_<0)scrollX_=0;
    layout();invalidate();
}

void RibbonBar::drawItem(HDC dc,const Hit& hit,bool focus){
    const Item& it=*hit.item;
    RECT r=hit.r;
    bool en=isEnabled(it);
    bool chk=state_.checked.count(it.id)>0;
    bool focusHit=false;
    if(focus&&focusIndex_>=0&&focusIndex_<(int)hits_.size())focusHit=(hits_[focusIndex_].item==&it);
    bool hov=(hover_==&it)||focusHit;
    if(it.kind==ItemKind::Separator){
        int x=(r.left+r.right)/2;HPEN p=CreatePen(PS_SOLID,1,kBorder);HGDIOBJ o=SelectObject(dc,p);
        MoveToEx(dc,x,r.top+scale(2),0);LineTo(dc,x,r.bottom-scale(2));SelectObject(dc,o);DeleteObject(p);return;
    }
    if(chk){fillRect(dc,r,kChkBg);frameRect(dc,r,kChkBorder);}
    else if(hov){fillRect(dc,r,kHoverBg);frameRect(dc,r,kHoverBorder);}
    COLORREF col=en?kText:kTextDim;
    COLORREF ico=en?kAccent:RGB(170,174,180);
    bool large=(it.kind==ItemKind::Large||it.kind==ItemKind::Split||it.kind==ItemKind::Gallery);
    if(it.kind==ItemKind::Label){
        HGDIOBJ o=SelectObject(dc,fontSmall_);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,kTextDim);
        DrawTextW(dc,wide(it.label).c_str(),-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
        SelectObject(dc,o);return;
    }
    std::map<std::string,std::string>::const_iterator vi=state_.value.find(it.id);
    std::wstring label=wide(it.label);
    if(vi!=state_.value.end()&&!vi->second.empty())label+=L"  ["+wide(vi->second)+L"]";
    if(large){
        int isz=scale(30);
        int icx=(r.left+r.right)/2;
        drawIcon(dc,it.icon,it.iconVariant,icx-isz/2,r.top+scale(4),isz,col,ico);
        if(it.kind==ItemKind::Split||it.kind==ItemKind::Gallery){
            POINT p[3]={{r.right-scale(12),r.top+scale(6)},{r.right-scale(5),r.top+scale(6)},{r.right-scale(8),r.top+scale(10)}};
            HBRUSH b=CreateSolidBrush(ico);HGDIOBJ o=SelectObject(dc,b);HGDIOBJ po=SelectObject(dc,GetStockObject(NULL_PEN));
            Polygon(dc,p,3);SelectObject(dc,po);SelectObject(dc,o);DeleteObject(b);
        }
        RECT tr={r.left+scale(1),r.top+scale(35),r.right-scale(1),r.bottom-scale(1)};
        HGDIOBJ o=SelectObject(dc,fontSmall_);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,col);
        DrawTextW(dc,label.c_str(),-1,&tr,DT_CENTER|DT_TOP|DT_WORDBREAK|DT_NOPREFIX);
        SelectObject(dc,o);
    }else{
        int isz=scale(16);
        drawIcon(dc,it.icon,it.iconVariant,r.left+scale(4),r.top+(r.bottom-r.top-isz)/2,isz,col,ico);
        RECT tr={r.left+scale(23),r.top,r.right-scale(4),r.bottom};
        HGDIOBJ o=SelectObject(dc,fontSmall_);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,col);
        DrawTextW(dc,label.c_str(),-1,&tr,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX|DT_END_ELLIPSIS);
        SelectObject(dc,o);
    }
}

void RibbonBar::paint(){
    PAINTSTRUCT ps;HDC dc=BeginPaint(h_,&ps);
    RECT cr;GetClientRect(h_,&cr);
    paintInto(dc,cr.right,cr.bottom);
    EndPaint(h_,&ps);
}
void RibbonBar::paintInto(HDC dc,int width,int height){
    RECT cr={0,0,width,height};
    HDC mem=CreateCompatibleDC(dc);
    HBITMAP bmp=CreateCompatibleBitmap(dc,width>0?width:1,height>0?height:1);
    HGDIOBJ ob=SelectObject(mem,bmp);
    fillRect(mem,cr,kBarBg);
    const Model& m=ribbonModel();
    HGDIOBJ of=SelectObject(mem,fontTab_);SetBkMode(mem,TRANSPARENT);
    for(size_t i=0;i<tabRects_.size()&&i<m.tabs.size();++i){
        RECT r=tabRects_[i];bool act=((int)i==curTab_);
        if(act){
            RECT body={r.left,r.top+scale(2),r.right,r.bottom+scale(4)};fillRect(mem,body,kPageBg);
            RECT top={r.left,scale(0),r.right,scale(3)};fillRect(mem,top,kAccent);
            RECT l={r.left,r.top+scale(2),r.left+scale(1),r.bottom+scale(4)};fillRect(mem,l,kBorder);
            RECT rr={r.right-scale(1),r.top+scale(2),r.right,r.bottom+scale(4)};fillRect(mem,rr,kBorder);
        }else if((int)i==hoverTab_){fillRect(mem,r,kHoverBg);}
        SetTextColor(mem,act?kAccent:kText);
        RECT tr=r;tr.bottom+=scale(4);
        DrawTextW(mem,wide(m.tabs[i].title).c_str(),-1,&tr,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
    }
    SelectObject(mem,of);
    RECT content={0,tabH_,cr.right,cr.bottom};fillRect(mem,content,kPageBg);
    if(curTab_>=0&&curTab_<(int)m.tabs.size()){
        const Tab& t=m.tabs[curTab_];
        for(size_t i=0;i<groupRects_.size()&&i<t.groups.size();++i){
            RECT g=groupRects_[i];fillRect(mem,g,kGroupBg);frameRect(mem,g,kBorder);
            HGDIOBJ o=SelectObject(mem,fontSmall_);SetTextColor(mem,kTextDim);
            RECT tr={g.left,g.bottom-scale(16),g.right,g.bottom-scale(1)};
            DrawTextW(mem,wide(t.groups[i].title).c_str(),-1,&tr,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
            SelectObject(mem,o);
        }
    }
    HWND fw=GetFocus();
    for(size_t i=0;i<hits_.size();++i)drawItem(mem,hits_[i],fw==h_&&(int)i==focusIndex_);
    if(contentWidth_>cr.right){
        RECT l={cr.right-scale(22),cr.bottom-scale(20),cr.right-scale(11),cr.bottom-scale(4)};
        RECT r={cr.right-scale(11),cr.bottom-scale(20),cr.right-scale(0),cr.bottom-scale(4)};
        fillRect(mem,l,kGroupBg);frameRect(mem,l,kBorder);fillRect(mem,r,kGroupBg);frameRect(mem,r,kBorder);
        POINT a[3]={{l.right-scale(7),l.top+scale(5)},{l.right-scale(3),l.top+scale(8)},{l.right-scale(7),l.top+scale(11)}};
        POINT b[3]={{r.left+scale(7),r.top+scale(5)},{r.left+scale(3),r.top+scale(8)},{r.left+scale(7),r.top+scale(11)}};
        HBRUSH br=CreateSolidBrush(kText);HGDIOBJ o=SelectObject(mem,br);HGDIOBJ po=SelectObject(mem,GetStockObject(NULL_PEN));
        Polygon(mem,a,3);Polygon(mem,b,3);SelectObject(mem,po);SelectObject(mem,o);DeleteObject(br);
    }
    BitBlt(dc,0,0,width,height,mem,0,0,SRCCOPY);
    SelectObject(mem,ob);DeleteObject(bmp);DeleteDC(mem);
}

void RibbonBar::showTip(const Item& it,POINT screen){
    if(it.tooltip.empty())return;
    std::string txt=it.tooltip;
    if(!isEnabled(it))txt+="\n[未接通] "+reasonFor(it);
    tipText_=wide(txt);
    if(!tip_){tip_=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW,L"T19RibbonTip",L"",WS_POPUP,0,0,10,10,parent_,0,inst_,this);}
    if(!tip_)return;
    HDC dc=GetDC(tip_);HGDIOBJ o=SelectObject(dc,fontSmall_);
    RECT calc={0,0,scale(320),0};DrawTextW(dc,tipText_.c_str(),-1,&calc,DT_CALCRECT|DT_LEFT|DT_WORDBREAK|DT_NOPREFIX);
    SelectObject(dc,o);ReleaseDC(tip_,dc);
    int w=calc.right+scale(10),h=calc.bottom+scale(8);
    int x=screen.x+scale(12),y=screen.y+scale(20);
    RECT wa={0,0,0,0};SystemParametersInfoW(SPI_GETWORKAREA,0,&wa,0);
    if(x+w>wa.right)x=wa.right-w-2;if(y+h>wa.bottom)y=screen.y-h-4;
    SetWindowPos(tip_,HWND_TOPMOST,x,y,w,h,SWP_NOACTIVATE|SWP_SHOWWINDOW);
    tipVisible_=true;
}
void RibbonBar::hideTip(){ if(tip_&&tipVisible_)ShowWindow(tip_,SW_HIDE);tipVisible_=false; }

void RibbonBar::openPopup(const Item& owner,RECT anchor){
    closePopups();
    if(owner.children.empty())return;
    PopupData* d=new PopupData();d->bar=this;d->owner=&owner;
    for(size_t i=0;i<owner.children.size();++i)d->items.push_back(&owner.children[i]);
    size_t n=d->items.size();
    d->cols=n>30?8:(n>16?6:(n>8?5:3));
    d->cellW=scale(88);d->cellH=scale(52);
    int rows=(int)((n+d->cols-1)/d->cols);
    int pw=d->cols*d->cellW+scale(8);
    int ph=rows*d->cellH+scale(8);
    int maxH=scale(440);if(ph>maxH)ph=maxH;
    int x=anchor.left,y=anchor.bottom+scale(1);
    RECT wa={0,0,0,0};SystemParametersInfoW(SPI_GETWORKAREA,0,&wa,0);
    if(x+pw>wa.right)x=wa.right-pw-2;if(x<wa.left)x=wa.left+2;
    if(y+ph>wa.bottom)y=anchor.top-ph-1;
    popupData_=d;
    popup_=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST,L"T19RibbonPopup",L"",WS_POPUP,x,y,pw,ph,parent_,0,inst_,d);
    if(!popup_){delete d;popupData_=0;return;}
    ShowWindow(popup_,SW_SHOWNOACTIVATE);
    SetCapture(popup_);
}

LRESULT CALLBACK RibbonBar::tipProc(HWND h,UINT m,WPARAM wp,LPARAM lp){
    RibbonBar* b=(RibbonBar*)GetWindowLongPtrW(h,GWLP_USERDATA);
    if(m==WM_CREATE){b=(RibbonBar*)((CREATESTRUCTW*)lp)->lpCreateParams;SetWindowLongPtrW(h,GWLP_USERDATA,(LONG_PTR)b);return 0;}
    if(!b)return DefWindowProcW(h,m,wp,lp);
    if(m==WM_PAINT){PAINTSTRUCT ps;HDC dc=BeginPaint(h,&ps);RECT cr;GetClientRect(h,&cr);
        fillRect(dc,cr,RGB(255,255,253));frameRect(dc,cr,RGB(120,120,120));
        HGDIOBJ o=SelectObject(dc,b->fontSmall_);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(30,30,30));
        RECT r=cr;InflateRect(&r,-4,-3);DrawTextW(dc,b->tipText_.c_str(),-1,&r,DT_LEFT|DT_TOP|DT_WORDBREAK|DT_NOPREFIX);
        SelectObject(dc,o);EndPaint(h,&ps);return 0;}
    if(m==WM_ERASEBKGND)return 1;
    return DefWindowProcW(h,m,wp,lp);
}

LRESULT CALLBACK RibbonBar::popupProc(HWND h,UINT m,WPARAM wp,LPARAM lp){
    PopupData* d=(PopupData*)GetWindowLongPtrW(h,GWLP_USERDATA);
    if(m==WM_CREATE){d=(PopupData*)((CREATESTRUCTW*)lp)->lpCreateParams;SetWindowLongPtrW(h,GWLP_USERDATA,(LONG_PTR)d);return 0;}
    if(!d)return DefWindowProcW(h,m,wp,lp);
    RibbonBar* b=d->bar;
    switch(m){
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:{PAINTSTRUCT ps;HDC dc=BeginPaint(h,&ps);RECT cr;GetClientRect(h,&cr);
        fillRect(dc,cr,kPageBg);frameRect(dc,cr,kBorder);
        for(size_t i=0;i<d->items.size();++i){
            int col=(int)i%d->cols,row=(int)i/d->cols;
            RECT r={col*d->cellW+4,row*d->cellH+4,col*d->cellW+4+d->cellW-8,row*d->cellH+4+d->cellH-8};
            bool hov=((int)i==d->hover);
            if(hov){fillRect(dc,r,kHoverBg);frameRect(dc,r,kHoverBorder);}
            const Item& it=*d->items[i];
            bool en=!((it.source==SourceKind::Unsupported)||b->state_.unavailable.count(it.command));
            COLORREF col2=en?kText:kTextDim,ic=en?kAccent:RGB(170,174,180);
            int isz=b->scale(24);
            drawIcon(dc,it.icon,it.iconVariant,r.left+8,r.top+4,isz,ic,col2);
            RECT tr={r.left+isz+14,r.top+6,r.right-4,r.top+4+isz};
            HGDIOBJ o=SelectObject(dc,b->fontSmall_);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,col2);
            DrawTextW(dc,wide(it.label).c_str(),-1,&tr,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX|DT_END_ELLIPSIS);
            RECT br={r.left+4,r.bottom-b->scale(15),r.right-4,r.bottom-1};
            SetTextColor(dc,kTextDim);
            DrawTextW(dc,wide(en?"":(it.reason.empty()?"未接通":it.reason)).c_str(),-1,&br,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX|DT_END_ELLIPSIS);
            SelectObject(dc,o);
        }
        EndPaint(h,&ps);return 0;}
    case WM_MOUSEMOVE:{POINT p={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};int col=p.x/d->cellW,row=p.y/d->cellH;
        size_t idx=(size_t)row*d->cols+col;int nh=(p.x>=0&&p.y>=0&&idx<d->items.size())?(int)idx:-1;
        if(nh!=d->hover){d->hover=nh;InvalidateRect(h,0,TRUE);}
        return 0;}
    case WM_LBUTTONDOWN:{POINT p={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};RECT cr;GetClientRect(h,&cr);
        if(!PtInRect(&cr,p)){b->closePopups();return 0;}
        int col=p.x/d->cellW,row=p.y/d->cellH;size_t idx=(size_t)row*d->cols+col;
        if(idx<d->items.size()){const Item* it=d->items[idx];b->closePopups();b->emit(*it);}
        return 0;}
    case WM_KEYDOWN:if(wp==VK_ESCAPE){b->closePopups();return 0;}break;
    case WM_MOUSEWHEEL:case WM_CAPTURECHANGED:if(b->popup_)b->closePopups();return 0;
    case WM_DESTROY:return 0;
    default:break;
    }
    return DefWindowProcW(h,m,wp,lp);
}

LRESULT CALLBACK RibbonBar::proc(HWND h,UINT m,WPARAM wp,LPARAM lp){
    RibbonBar* b=(RibbonBar*)GetWindowLongPtrW(h,GWLP_USERDATA);
    if(m==WM_CREATE){b=(RibbonBar*)((CREATESTRUCTW*)lp)->lpCreateParams;b->h_=h;SetWindowLongPtrW(h,GWLP_USERDATA,(LONG_PTR)b);return 0;}
    if(!b)return DefWindowProcW(h,m,wp,lp);
    switch(m){
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:b->paint();return 0;
    case WM_PRINTCLIENT:{RECT cr;GetClientRect(h,&cr);b->paintInto((HDC)wp,cr.right,cr.bottom);return 0;}
    case WM_SIZE:b->layout();b->invalidate();return 0;
    case WM_SETFOCUS:if(b->focusIndex_<0&&!b->hits_.empty())b->focusIndex_=0;b->invalidate();return 0;
    case WM_KILLFOCUS:b->hideTip();b->invalidate();return 0;
    case WM_MOUSEMOVE:{
        POINT p={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};b->lastMouse_=p;
        TRACKMOUSEEVENT t={sizeof(t),TME_LEAVE,h,0};TrackMouseEvent(&t);
        const Item* nh=0;for(size_t i=0;i<b->hits_.size();++i)if(PtInRect(&b->hits_[i].r,p)){nh=b->hits_[i].item;break;}
        int nt=-1;for(size_t i=0;i<b->tabRects_.size();++i)if(PtInRect(&b->tabRects_[i],p)){nt=(int)i;break;}
        if(nh!=b->hover_||nt!=b->hoverTab_){
            b->hover_=nh;b->hoverTab_=nt;b->hideTip();
            KillTimer(h,2);
            if(nh&&!nh->tooltip.empty())SetTimer(h,2,500,0);
            b->invalidate();
        }
        return 0;}
    case WM_MOUSELEAVE:b->hover_=0;b->hoverTab_=-1;b->hideTip();b->invalidate();return 0;
    case WM_TIMER:if(wp==2){KillTimer(h,2);if(b->hover_){POINT sp=b->lastMouse_;ClientToScreen(h,&sp);b->showTip(*b->hover_,sp);}return 0;}break;
    case WM_LBUTTONDOWN:{
        SetFocus(h);
        POINT p={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
        for(size_t i=0;i<b->tabRects_.size();++i)if(PtInRect(&b->tabRects_[i],p)){
            if((int)i!=b->curTab_){b->curTab_=(int)i;b->scrollX_=0;b->focusIndex_=0;b->layout();b->invalidate();}
            return 0;
        }
        if(b->contentWidth_>0){
            RECT cl;GetClientRect(h,&cl);
            if(b->contentWidth_>cl.right){
                RECT l={cl.right-b->scale(22),cl.bottom-b->scale(20),cl.right-b->scale(11),cl.bottom-b->scale(4)};
                RECT r={cl.right-b->scale(11),cl.bottom-b->scale(20),cl.right,cl.bottom-b->scale(4)};
                if(PtInRect(&l,p)){b->scrollBy(-b->scale(60));return 0;}
                if(PtInRect(&r,p)){b->scrollBy(b->scale(60));return 0;}
            }
        }
        b->press_=0;
        for(size_t i=0;i<b->hits_.size();++i)if(PtInRect(&b->hits_[i].r,p)&&b->hits_[i].item->kind!=ItemKind::Separator){b->press_=b->hits_[i].item;b->focusIndex_=(int)i;break;}
        b->invalidate();return 0;}
    case WM_LBUTTONUP:{
        if(b->press_){
            const Item* it=b->press_;b->press_=0;
            POINT p={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
            const Item* up=0;RECT ur={0,0,0,0};
            for(size_t i=0;i<b->hits_.size();++i)if(PtInRect(&b->hits_[i].r,p)){up=b->hits_[i].item;ur=b->hits_[i].r;break;}
            if(up==it&&it->kind!=ItemKind::Label){
                bool chevron=(it->kind==ItemKind::Split||it->kind==ItemKind::Gallery);
                RECT cr={ur.right-b->scale(16),ur.top,ur.right,ur.bottom};
                if(it->kind==ItemKind::Gallery||(it->kind==ItemKind::Split&&PtInRect(&cr,p))){
                    if(it->kind==ItemKind::Split&&!PtInRect(&cr,p))b->emit(*it);else b->openPopup(*it,ur);
                }else b->emit(*it);
            }
        }
        b->invalidate();return 0;}
    case WM_MOUSEWHEEL:{
        RECT cl;GetClientRect(h,&cl);
        int d=(short)HIWORD(wp);
        if(b->contentWidth_>cl.right)b->scrollBy(d>0?-b->scale(60):b->scale(60));
        return 0;}
    case WM_KEYDOWN:{
        if(wp==VK_ESCAPE){b->closePopups();b->hideTip();return 0;}
        if(wp==VK_TAB){b->focusTab_=b->curTab_;}
        if(wp==VK_LEFT||wp==VK_RIGHT){
            if(b->hits_.empty())return 0;
            int n=(int)b->hits_.size();
            if(b->focusIndex_<0)b->focusIndex_=0;
            else{b->focusIndex_+=(wp==VK_RIGHT?1:-1);if(b->focusIndex_<0)b->focusIndex_=n-1;if(b->focusIndex_>=n)b->focusIndex_=0;}
            b->invalidate();return 0;}
        if(wp==VK_DOWN||wp==VK_UP){
            const Model& mm=ribbonModel();int nt=b->curTab_+(wp==VK_DOWN?1:-1);
            if(nt<0)nt=(int)mm.tabs.size()-1;if(nt>=(int)mm.tabs.size())nt=0;
            b->curTab_=nt;b->scrollX_=0;b->focusIndex_=0;b->layout();b->invalidate();return 0;}
        if(wp==VK_RETURN||wp==VK_SPACE){
            if(b->focusIndex_>=0&&b->focusIndex_<(int)b->hits_.size()){
                const Item* it=b->hits_[b->focusIndex_].item;
                if(it->kind==ItemKind::Gallery||it->kind==ItemKind::Split)b->openPopup(*it,b->hits_[b->focusIndex_].r);
                else b->emit(*it);
            }
            return 0;}
        break;}
    default:break;
    }
    return DefWindowProcW(h,m,wp,lp);
}

}}}
