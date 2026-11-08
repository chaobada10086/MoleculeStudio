#ifndef STUDIO_INLINE_TEXT_HPP
#define STUDIO_INLINE_TEXT_HPP
#include "chem_italic.hpp"
#include "chem_completion.hpp"
// Host callbacks for the inline text editor. They are injected at
// initialize() and required by begin(): a missing host throws immediately,
// so the silent "typing goes nowhere" failure mode of unwired callbacks
// cannot happen.
struct TextEditHost {
    virtual ~TextEditHost() {}
    virtual void textEditChanged() = 0;
    virtual void textEditSelectionChanged() = 0;
    virtual void textEditCompositionChanged(bool on) = 0;
    virtual void textEditFinishing() = 0;
};
// The in-place text editor is one borderless RichEdit placed at the text's
// document position. Caret, selection, IME composition and mouse handling are
// the control's own; the document is written exactly once, by the commit
// command in finish().
//
struct InlineTextEditor {
    std::function<bool(UINT,WPARAM)> shortcut;
    Session* session=nullptr;HWND owner=0,canvas=0,hwnd=0,acceptButton=0,cancelButton=0,help=0;WNDPROC original=nullptr;
    TextEditHost* host=nullptr;
    std::function<void(const Error&,bool)> completed;
    ObjectId text=kInvalidObjectId,atom=kInvalidObjectId;Vec2 anchor;
    bool active=false,loading=false,composing=false,closing=false,refreshPending=false,suppressReturnChar=false;double size=10,zoom=1;
    // Silence synthetic selections used when reading or applying rich formatting.
    struct QuietEdit {
        InlineTextEditor& editor; bool loading; LRESULT mask;
        QuietEdit(InlineTextEditor& e):editor(e),loading(e.loading){e.loading=true;mask=studio_i18n::sendMessage(e.hwnd,EM_SETEVENTMASK,0,0);}
        ~QuietEdit(){studio_i18n::sendMessage(editor.hwnd,EM_SETEVENTMASK,0,mask);editor.loading=loading;}
    };
    LONG appliedZoom=0,appliedZoomDenominator=0;int appliedWrap=-1;
    // Defaults follow the ChemDraw/ACS Document 1996 preset (Arial 10); the App
    // overwrites them from the document defaults as soon as one exists.
    double nextSize=10;std::string nextFamily="Arial";uint16_t nextFlags=0;
    std::string family="Arial";TextFormat format;std::vector<TextRun> initial;std::vector<uint32_t> initialColors,runColors;
    int contentWidth=12,contentHeight=20,ascent=15;double widthLimit=0,lineHeight=0;
    bool resizing=false;int resizeEdge=0;std::wstring spacingFace;
    std::vector<std::wstring> measuredStyles;
    std::wstring ascentKey;int lastX=0,lastY=0,lastW=0,lastH=0;
    void initialize(HWND parent,HWND surface,Session* s,TextEditHost* h){owner=parent;canvas=surface;session=s;host=h;nextSize=s->doc.defaultTextSize();nextFamily=s->doc.defaultFont().empty()?"Arial":s->doc.defaultFont();LoadLibraryW(L"Msftedit.dll");
        if(!host)throw std::runtime_error("文字编辑器缺少宿主回调");
        hwnd=studio_i18n::createWindowEx(0,L"RICHEDIT50W",L"",WS_CHILD|ES_MULTILINE|ES_NOHIDESEL|ES_WANTRETURN|ES_AUTOHSCROLL,0,0,12,20,parent,(HMENU)301,GetModuleHandleW(0),0);
        if(!hwnd)throw std::runtime_error("无法创建原位文字编辑器");SetWindowLongPtrW(hwnd,GWLP_USERDATA,(LONG_PTR)this);original=(WNDPROC)SetWindowLongPtrW(hwnd,GWLP_WNDPROC,(LONG_PTR)proc);
        studio_i18n::sendMessage(hwnd,EM_EXLIMITTEXT,0,65536);studio_i18n::sendMessage(hwnd,EM_SETBKGNDCOLOR,0,RGB(255,255,255));studio_i18n::sendMessage(hwnd,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,0);studio_i18n::sendMessage(hwnd,EM_SETEVENTMASK,0,ENM_CHANGE|ENM_SELCHANGE|ENM_REQUESTRESIZE);
        // Keep the native input context and Asian language defaults. A fresh HIMC
        // loses the user's conversion state; automatic keyboard switching can
        // silently select English when applying our Latin chemical fonts.
        HIMC context=ImmGetContext(hwnd);if(context)ImmReleaseContext(hwnd,context);else ImmAssociateContextEx(hwnd,nullptr,IACE_DEFAULT);
        auto language=studio_i18n::sendMessage(hwnd,EM_GETLANGOPTIONS,0,0);
        studio_i18n::sendMessage(hwnd,EM_SETLANGOPTIONS,0,(language|IMF_AUTOFONT|IMF_DUALFONT)&~IMF_AUTOKEYBOARD);
        studio_i18n::sendMessage(hwnd,EM_SETEDITSTYLE,SES_USECTF,SES_USECTF|SES_NOIME);studio_i18n::sendMessage(hwnd,EM_SETTARGETDEVICE,0,1);
        acceptButton=studio_i18n::createWindow(L"BUTTON",L"保存",WS_CHILD|BS_PUSHBUTTON,0,0,1,1,owner,(HMENU)304,GetModuleHandleW(0),0);
        cancelButton=studio_i18n::createWindow(L"BUTTON",L"取消",WS_CHILD|BS_PUSHBUTTON,0,0,1,1,owner,(HMENU)305,GetModuleHandleW(0),0);
        help=studio_i18n::createWindow(L"STATIC",L"",WS_CHILD|SS_CENTERIMAGE,0,0,1,1,owner,(HMENU)306,GetModuleHandleW(0),0);
        for(auto control:{acceptButton,cancelButton,help})studio_i18n::sendMessage(control,WM_SETFONT,(WPARAM)GetStockObject(DEFAULT_GUI_FONT),TRUE);
    }
    ObjectRef editingObject()const{return atom!=kInvalidObjectId?ObjectRef(ObjectKind::Atom,atom):ObjectRef(ObjectKind::Text,text);}
    double pixelsPerUnit()const{return session->view().scale*session->view().dpiScale;}
    double currentWidth()const{double pp=pixelsPerUnit();return widthLimit>0?widthLimit:(pp>0?contentWidth/pp:0);}
    int edgeAt(int x)const{RECT rc;GetClientRect(hwnd,&rc);int hot=std::max(4,int(std::lround(5*session->view().dpiScale)));if(x<=hot)return 3;if(x>=rc.right-hot)return 4;return 0;}
    void beginResize(int edge){resizing=true;resizeEdge=edge;double width=std::max(2.0,currentWidth());widthLimit=width;session->input.setFocus(interaction::InputFocus::TextEntry);SetCapture(hwnd);resize();}
    void updateResize(int x,int y){if(!resizing)return;POINT point={x,y};MapWindowPoints(hwnd,canvas,&point,1);Vec2 at=session->view().toDocument(Vec2(point.x,point.y));double minimum=std::max(2.0,size*0.5);int align=format.align>2?0:int(format.align);double width=std::max(minimum,currentWidth());double left=anchor.x-(align==1?width*.5:align==2?width:0.0),right=left+width;if(resizeEdge==4)right=std::max(left+minimum,at.x);else left=std::min(right-minimum,at.x);double next=right-left;widthLimit=next;anchor.x=align==1?left+next*.5:align==2?right:left;resize();SetCursor(LoadCursorW(0,MAKEINTRESOURCEW(32644)));}
    void finishResize(){if(!resizing)return;resizing=false;if(GetCapture()==hwnd)ReleaseCapture();resize();SetFocus(hwnd);}
    static DWORD effects(uint16_t flags){return (flags&kBold?CFE_BOLD:0)|(flags&kItalic?CFE_ITALIC:0)|(flags&kUnderline?CFE_UNDERLINE:0)|(flags&kSubscript?CFE_SUBSCRIPT:0)|(flags&kSuperscript?CFE_SUPERSCRIPT:0);}
    CHARFORMAT2W style(const TextRun& r)const{CHARFORMAT2W c={};c.cbSize=sizeof(c);c.dwMask=CFM_FACE|CFM_SIZE|CFM_BOLD|CFM_ITALIC|CFM_UNDERLINE|CFM_SUBSCRIPT|CFM_SUPERSCRIPT|CFM_COLOR|CFM_CHARSET;c.dwEffects=effects(r.flags);auto color=format.colorRGBA;c.crTextColor=(color&255)?RGB((color>>24)&255,(color>>16)&255,(color>>8)&255):RGB(0,0,0);c.bCharSet=DEFAULT_CHARSET;c.yHeight=LONG(std::lround((r.size>0?r.size:size)*20));auto f=W(r.fontName.empty()?family:r.fontName);wcsncpy(c.szFaceName,f.c_str(),LF_FACESIZE-1);return c;}
    std::wstring value()const{GETTEXTLENGTHEX n={GTL_NUMCHARS|GTL_PRECISE,1200};LRESULT length=studio_i18n::sendMessage(hwnd,EM_GETTEXTLENGTHEX,(WPARAM)&n,0);std::wstring out(size_t(std::max<LRESULT>(0,length))+2,L'\0');GETTEXTEX get={};get.cb=DWORD(out.size()*sizeof(wchar_t));get.codepage=1200;auto got=studio_i18n::sendMessage(hwnd,EM_GETTEXTEX,(WPARAM)&get,(LPARAM)&out[0]);out.resize(size_t(std::max<LRESULT>(0,got)));return out;}
    void select(LONG a,LONG b){CHARRANGE r={a,b};studio_i18n::sendMessage(hwnd,EM_EXSETSEL,0,(LPARAM)&r);}
    void pointCaret(Vec2 pixel,bool word=false){POINT point={LONG(pixel.x),LONG(pixel.y)};MapWindowPoints(canvas,hwnd,&point,1);LONG index=LONG(studio_i18n::sendMessage(hwnd,EM_CHARFROMPOS,0,(LPARAM)&point));if(index<0)index=0;LONG start=index,end=index;if(word){start=LONG(studio_i18n::sendMessage(hwnd,EM_FINDWORDBREAK,WB_LEFT,index+1));end=LONG(studio_i18n::sendMessage(hwnd,EM_FINDWORDBREAK,WB_RIGHT,index));}select(start,end);SetFocus(hwnd);}
    // Line spacing equal to the canvas layout's. The layout advances every
    // line by the block-wide maximum ascent and descent over the runs' own
    // fonts and sizes (rich_text.cpp; no external leading, scripts shifted),
    // so the exact rule uses that same maximum. Metrics must come from the
    // runs actually in the control: text typed through an East Asian IME
    // carries the EA font, whose metrics differ from the latin default
    // family, and using the default alone left the editor's lines tighter
    // than the committed ones. bLineSpacingRule 3 is "at least" in RichEdit
    // and silently loses to the font's natural height; 4 is the fixed rule.
    void applyCanvasLineSpacing(const CHARFORMAT2W* inputStyle=nullptr){
        if(!session)return;
        QuietEdit quiet(*this);
        double maxAsc=0,maxDesc=0;
        std::vector<TextRun> current;
        if(inputStyle){TextRun r;r.fontName=U(inputStyle->szFaceName);r.size=inputStyle->yHeight/20.;r.flags=(inputStyle->dwEffects&CFE_BOLD?kBold:0)|(inputStyle->dwEffects&CFE_ITALIC?kItalic:0)|(inputStyle->dwEffects&CFE_SUPERSCRIPT?kSuperscript:0)|(inputStyle->dwEffects&CFE_SUBSCRIPT?kSubscript:0);current.push_back(r);}
        else current=runs();
        if(current.empty()){
            text::FontSpec spec(family,(format.flags&kBold)!=0,(format.flags&kItalic)!=0);
            text::LineMetrics m;if(session->glyphs.lineMetrics(spec,&m)){maxAsc=m.ascent*(size>0?size:10.0);maxDesc=m.descent*(size>0?size:10.0);}
        }else for(const auto& r:current){
            text::FontSpec spec(r.fontName.empty()?family:r.fontName,(r.flags&kBold)!=0,(r.flags&kItalic)!=0);
            text::LineMetrics m;if(!session->glyphs.lineMetrics(spec,&m))continue;
            double sz=(r.size>0?r.size:size);
            double asc=m.ascent*sz,desc=m.descent*sz;
            if(r.flags&kSuperscript){double s=sz*0.75;asc=s*0.36+m.ascent*s;desc=0;}
            if(r.flags&kSubscript){double s=sz*0.75;asc=0;desc=s*0.18+m.descent*s;}
            if(asc>maxAsc)maxAsc=asc;
            if(desc>maxDesc)maxDesc=desc;
        }
        double line=maxAsc+maxDesc;
        if(!(line>0))return;
        if(inputStyle)line=std::max(line,lineHeight);
        if(std::abs(line-lineHeight)<.025)return;
        lineHeight=line;
        CHARRANGE saved;studio_i18n::sendMessage(hwnd,EM_EXGETSEL,0,(LPARAM)&saved);
        select(0,-1);
        PARAFORMAT2 p={};p.cbSize=sizeof(p);p.dwMask=PFM_LINESPACING;p.bLineSpacingRule=4;p.dyLineSpacing=LONG(std::lround(line*20));
        studio_i18n::sendMessage(hwnd,EM_SETPARAFORMAT,0,(LPARAM)&p);
        studio_i18n::sendMessage(hwnd,EM_EXSETSEL,0,(LPARAM)&saved);
    }
    // A carbon that carries no user label is opened for typing EMPTY. Showing the
    // implicit "C" forced a delete before the first character, both when the label
    // was reached from a bond endpoint and from the text tool's type-to-label path.
    // Labels that really are text (nicknames, explicit text, manual hydrogens,
    // charges, isotopes, radicals) still open with their own runs.
    bool blankStart(ObjectId a)const{
        if(a==kInvalidObjectId||!session)return false;
        const auto* atom=session->doc.atoms().alive(a);if(!atom||atom->element!=6)return false;
        if(atom->semantics.charge||atom->semantics.isotope||atom->semantics.radical)return false;
        const auto& bag=static_cast<const Document&>(session->doc).extensions(ObjectRef(ObjectKind::Atom,a));
        if(bag.findValue("studio.nickname.fragment")||bag.findValue("studio.hydrogens.manual")||bag.findValue("studio.label.text")||bag.findValue("studio.atom.runs"))return false;
        auto label=session->doc.atomLabel(a);return label.empty()||label=="C";
    }
    void begin(ObjectId t,ObjectId a,Vec2 p,bool atEnd=true,double wrap=0){
        if(active&&!finish(true).ok())return;ImmAssociateContextEx(canvas,nullptr,IACE_DEFAULT);session->cancel();resizing=false;text=t;atom=a;anchor=p;widthLimit=a==kInvalidObjectId?wrap:0;
        composing=false;spacingFace.clear();measuredStyles.clear();ascentKey.clear();lineHeight=0;format=TextFormat();format.colorRGBA=session->graphicsColor;size=nextSize;family=nextFamily;format.flags=nextFlags;initial.clear();initialColors.clear();
        if(a!=kInvalidObjectId){format.colorRGBA=0x000000FFu;size=10;if(auto v=session->doc.documentExtensions().findValue("studio.style.LabelSize"))size=std::atof(v->c_str());family=session->doc.defaultFont();anchor=session->doc.atoms().get(a)->position;TextRun r;r.utf8=session->doc.atomLabel(a);if(r.utf8.empty())r.utf8=core::elementSymbol(session->doc.atoms().get(a)->element);initial=studioatom::runs(session->doc,a);if(initial.empty())initial.push_back(r);if(blankStart(a))initial.clear();}
        else if(t!=kInvalidObjectId){auto r=session->doc.texts().get(t);anchor=r->position;size=r->size>0?r->size:size;format=r->format;family=format.fontName.empty()?session->doc.defaultFont():format.fontName;initial=session->doc.runs(t);}
        if(t!=kInvalidObjectId){const auto& bag=static_cast<const Document&>(session->doc).extensions(ObjectRef(ObjectKind::Text,t));if(auto width=bag.findValue("studio.text.width"))widthLimit=std::max(0.,std::atof(width->c_str()));}
        if(t!=kInvalidObjectId||a!=kInvalidObjectId){auto& bag=static_cast<const Document&>(session->doc).extensions(editingObject());if(auto colors=bag.findValue("studio.text.colors")){std::istringstream in(*colors);uint32_t c;while(in>>c)initialColors.push_back(c);}if(auto color=bag.findValue("studio.color"))format.colorRGBA=uint32_t(std::strtoull(color->c_str(),0,10));}
        if(t!=kInvalidObjectId||a!=kInvalidObjectId){const auto& bag=static_cast<const Document&>(session->doc).extensions(editingObject());if(auto color=bag.findValue("studio.color.override")){format.colorRGBA=uint32_t(std::strtoull(color->c_str(),0,10));initialColors.assign(initial.size(),format.colorRGBA);}}
        contentWidth=12;contentHeight=20;lastX=lastY=lastW=lastH=0;active=loading=true;session->selected.clear();studio_i18n::sendMessage(hwnd,WM_SETREDRAW,FALSE,0);studio_i18n::setWindowText(hwnd,L"");TextRun base;base.flags=format.flags;base.fontName=family;base.size=size;auto cf=style(base);studio_i18n::sendMessage(hwnd,EM_SETCHARFORMAT,SCF_ALL,(LPARAM)&cf);
        PARAFORMAT2 pf={};pf.cbSize=sizeof(pf);pf.dwMask=PFM_ALIGNMENT|PFM_SPACEBEFORE|PFM_SPACEAFTER|PFM_LINESPACING;pf.wAlignment=format.align==1?PFA_CENTER:format.align==2?PFA_RIGHT:format.align==3?PFA_JUSTIFY:PFA_LEFT;pf.bLineSpacingRule=0;pf.dyLineSpacing=0;studio_i18n::sendMessage(hwnd,EM_SETPARAFORMAT,0,(LPARAM)&pf);
        for(size_t ri=0;ri<initial.size();++ri){auto r=initial[ri];auto w=W(r.utf8);std::wstring normalized;for(auto c:w){if(c==L'\n')normalized+=L'\r';else if(c!=L'\r')normalized+=c;}LONG start=LONG(value().size());select(start,start);auto cs=style(r);if(ri<initialColors.size()){auto color=initialColors[ri];cs.crTextColor=RGB((color>>24)&255,(color>>16)&255,(color>>8)&255);}studio_i18n::sendMessage(hwnd,EM_SETCHARFORMAT,SCF_SELECTION,(LPARAM)&cs);studio_i18n::sendMessage(hwnd,EM_REPLACESEL,FALSE,(LPARAM)normalized.c_str());if(r.flags&kFormula){select(start,start+LONG(normalized.size()));toggle(kFormula);}}
        if(atom!=kInvalidObjectId)select(0,-1);else if(atEnd)select(LONG(value().size()),LONG(value().size()));else select(0,0);
        studio_i18n::sendMessage(hwnd,EM_EMPTYUNDOBUFFER,0,0);studio_i18n::sendMessage(hwnd,WM_SETREDRAW,TRUE,0);loading=false;applyCanvasLineSpacing();session->input.setFocus(interaction::InputFocus::TextEntry);studio_i18n::setWindowText(help,a!=kInvalidObjectId?L"Enter 保存 · Esc 保存":L"Enter 保存 · Shift+Enter 换行 · Esc 保存");resize();ShowWindow(hwnd,SW_SHOW);for(auto control:{help,acceptButton,cancelButton})ShowWindow(control,chromeVisible()?SW_SHOW:SW_HIDE);SetFocus(hwnd);InvalidateRect(hwnd,0,TRUE);InvalidateRect(canvas,0,FALSE);
    }
    // First-line baseline distance from the editor's client top. The COM
    // measurement only depends on font, size, zoom and the current face, so it
    // is cached per that key: measuring per keystroke (CreateFont, text
    // metrics and an ITextDocument::GetPoint chain) is what made backspace
    // stutter. The key includes the face so an East Asian first line, whose
    // baseline sits differently inside the same box, re-measures on change.
    void metrics(){std::wstring key=W(family)+L"|"+std::to_wstring(size)+L"|"+std::to_wstring(zoom)+L"|"+spacingFace;
        if(key==ascentKey&&ascent>0)return;
        HDC dc=GetDC(hwnd);HFONT font=CreateFontW(-int(std::lround(size*session->view().scale*session->view().dpiScale)),0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,W(family).c_str());auto old=SelectObject(dc,font);TEXTMETRICW tm={};GetTextMetricsW(dc,&tm);ascent=tm.tmAscent;SelectObject(dc,old);DeleteObject(font);ReleaseDC(hwnd,dc);IUnknown* ole=nullptr;if(studio_i18n::sendMessage(hwnd,EM_GETOLEINTERFACE,0,(LPARAM)&ole)&&ole){ITextDocument* document=nullptr;const IID iid={0x8CC497C0,0xA1DF,0x11CE,{0x80,0x98,0x00,0xAA,0x00,0x47,0xBE,0x5D}};if(SUCCEEDED(ole->QueryInterface(iid,(void**)&document))){ITextRange* range=nullptr;if(SUCCEEDED(document->Range(0,0,&range))){LONG x=0,y=0;if(SUCCEEDED(range->GetPoint(tomStart|tomClientCoord|TA_BASELINE,&x,&y))&&y>0)ascent=int(y);range->Release();}document->Release();}ole->Release();}ascentKey=key;}
    bool chromeVisible()const{return atom!=kInvalidObjectId;}
    void position(){if(!active)return;metrics();auto p=session->view().toDevice(anchor);POINT origin={0,0};MapWindowPoints(canvas,owner,&origin,1);int x=int(std::lround(p.x))+origin.x,y=int(std::lround(p.y))+origin.y-ascent;if(atom!=kInvalidObjectId){x-=contentWidth/2;y=int(std::lround(p.y))+origin.y-contentHeight/2;}else if(format.align==1)x-=contentWidth/2;else if(format.align==2)x-=contentWidth;
        int w=std::max(12,contentWidth),ht=std::max(8,contentHeight+2);
        if(atom==kInvalidObjectId&&widthLimit<=0){
            // The caption grows in document space; only the native editor's
            // viewport is bounded horizontally by the canvas. A huge window
            // would put the caret outside the app and hit Win32 size limits.
            RECT viewport;GetClientRect(canvas,&viewport);MapWindowPoints(canvas,owner,(POINT*)&viewport,2);
            x=std::max(int(viewport.left)+2,std::min(x,int(viewport.right)-20));
            w=std::min(w,std::max(12,int(viewport.right)-x-4));
        }
        if(x!=lastX||y!=lastY||w!=lastW||ht!=lastH){
            RECT oldRect={lastX,lastY,lastX+lastW,lastY+lastH};MapWindowPoints(owner,canvas,(POINT*)&oldRect,2);
            SetWindowPos(hwnd,HWND_TOP,x,y,w,ht,SWP_NOACTIVATE|SWP_NOREDRAW);
            if(w!=lastW||ht!=lastH){RECT rect={0,0,std::max(10,w),std::max(4,ht-1)};studio_i18n::sendMessage(hwnd,EM_SETRECTNP,0,(LPARAM)&rect);}
            if(lastW&&lastH){RECT newRect={x,y,x+w,y+ht};MapWindowPoints(owner,canvas,(POINT*)&newRect,2);
                HRGN exposed=CreateRectRgnIndirect(&oldRect),covered=CreateRectRgnIndirect(&newRect);
                if(CombineRgn(exposed,exposed,covered,RGN_DIFF)!=NULLREGION)InvalidateRgn(canvas,exposed,FALSE);
                DeleteObject(exposed);DeleteObject(covered);}
            lastX=x;lastY=y;lastW=w;lastH=ht;InvalidateRect(hwnd,0,FALSE);
        }
        if(!chromeVisible())return;
        double dpi=GetDpiForWindow(owner)/96.;int barWidth=int(360*dpi),barHeight=int(25*dpi);RECT client;GetClientRect(owner,&client);int left=std::max(4,std::min(x,int(client.right)-barWidth-4)),top=y+contentHeight+int(8*dpi);if(top+barHeight>client.bottom-24)top=std::max(4,y-barHeight-int(5*dpi));
        auto place=[&](HWND control,int x,int width){RECT old;GetWindowRect(control,&old);MapWindowPoints(0,owner,(POINT*)&old,2);if(old.left!=x||old.top!=top||old.right-old.left!=width||old.bottom-old.top!=barHeight)SetWindowPos(control,HWND_TOP,x,top,width,barHeight,SWP_NOACTIVATE);};
        place(help,left,int(240*dpi));place(acceptButton,left+int(244*dpi),int(54*dpi));place(cancelButton,left+int(302*dpi),int(54*dpi));
    }
    int fullTextHeight(){
        // Native resize notifications can arrive while the caret has scrolled
        // to the last paragraph. Use document-relative end position as a floor.
        GETTEXTLENGTHEX length={GTL_NUMCHARS|GTL_PRECISE,1200};
        LONG end=LONG(studio_i18n::sendMessage(hwnd,EM_GETTEXTLENGTHEX,(WPARAM)&length,0));
        POINTL point={};POINT scroll={};
        studio_i18n::sendMessage(hwnd,EM_POSFROMCHAR,(WPARAM)&point,end);
        studio_i18n::sendMessage(hwnd,EM_GETSCROLLPOS,0,(LPARAM)&scroll);
        return std::max(4,int(point.y+scroll.y+std::ceil(std::max(size,lineHeight)*pixelsPerUnit())+2));
    }
    void settleInput(){
        if(!active||loading||composing)return;
        loading=true;
        int fullHeight=fullTextHeight();
        if(fullHeight>contentHeight){contentHeight=fullHeight;position();}
        resetScroll();loading=false;
    }
    void resetScroll(){
        if(atom==kInvalidObjectId&&widthLimit<=0){
            studio_i18n::sendMessage(hwnd,EM_SCROLLCARET,0,0);
            // Keep short multiline captions fully visible, but retain the
            // horizontal offset needed to continue typing beyond the viewport.
            POINT scroll={};studio_i18n::sendMessage(hwnd,EM_GETSCROLLPOS,0,(LPARAM)&scroll);
            bool restore=false;
            if(contentWidth<=lastW&&scroll.x){scroll.x=0;restore=true;}
            if(contentHeight+2<=lastH&&scroll.y){scroll.y=0;restore=true;}
            if(restore)studio_i18n::sendMessage(hwnd,EM_SETSCROLLPOS,0,(LPARAM)&scroll);
            return;
        }
        POINT scroll={};studio_i18n::sendMessage(hwnd,EM_GETSCROLLPOS,0,(LPARAM)&scroll);if(scroll.x||scroll.y){POINT origin={};studio_i18n::sendMessage(hwnd,EM_SETSCROLLPOS,0,(LPARAM)&origin);}
    }
    void resize(){if(!active||loading)return;loading=true;double pp=pixelsPerUnit();
        // The RichEdit formats through GDI, whose LOGPIXELS is the system DPI
        // of the primary monitor, while the canvas maps document units with
        // the window's per-monitor DPI. On mixed-DPI setups the two differ and
        // the editor's text rendered larger or smaller than the committed
        // drawing (size and line spacing alike); the zoom ratio absorbs that
        // factor so both render at the same pixels per point.
        HDC dc=GetDC(hwnd);int logpix=GetDeviceCaps(dc,LOGPIXELSY);ReleaseDC(hwnd,dc);
        if(logpix<=0)logpix=96;
        zoom=std::max(1.0/63,std::min(63.0,session->view().scale*GetDpiForWindow(owner)/(double)logpix));
        // RichEdit rejects numerators/denominators above 65535, even
        // when their ratio is valid. Keep both bounded at high canvas zoom.
        LONG denominator=LONG(std::floor(60000.0/std::max(1.0,zoom)));
        LONG numerator=LONG(std::lround(zoom*denominator));
        if(numerator!=appliedZoom||denominator!=appliedZoomDenominator){
            if(studio_i18n::sendMessage(hwnd,EM_SETZOOM,numerator,denominator)){
                appliedZoom=numerator;appliedZoomDenominator=denominator;
            }else{loading=false;throw std::runtime_error("文字编辑缩放设置失败");}
        }
        auto nativeStyle=GetWindowLongPtrW(hwnd,GWL_STYLE);
        auto nextStyle=widthLimit>0?(nativeStyle&~LONG_PTR(ES_AUTOHSCROLL)):(nativeStyle|ES_AUTOHSCROLL);
        if(nextStyle!=nativeStyle)SetWindowLongPtrW(hwnd,GWL_STYLE,nextStyle);
        if(widthLimit>0){
            // A fixed text box wraps at the control width: the window is exactly
            // the box width, and (0,0) restores normal wrapping at that width. The
            // old twips target-device width silently DISABLED wrapping (verified
            // against RICHEDIT50W), which made long boxed text clip instead of wrap.
            contentWidth=std::max(12,int(std::lround(widthLimit*pp)));
            if(appliedWrap!=0){studio_i18n::sendMessage(hwnd,EM_SETTARGETDEVICE,0,0);appliedWrap=0;}
        }else if(appliedWrap!=1){studio_i18n::sendMessage(hwnd,EM_SETTARGETDEVICE,0,1);appliedWrap=1;}
        position();resetScroll();loading=false;studio_i18n::sendMessage(hwnd,EM_REQUESTRESIZE,0,0);}
    bool notify(NMHDR* hdr){if(hdr->hwndFrom!=hwnd)return false;
        // EN_CHANGE arrives as WM_COMMAND on the parent (handled there);
        // EN_SELCHANGE and EN_REQUESTRESIZE arrive here as WM_NOTIFY.
        if(hdr->code==EN_SELCHANGE&&active&&!loading){if(host)host->textEditSelectionChanged();return true;}
        if(hdr->code==EN_REQUESTRESIZE&&active&&!loading){auto r=(REQRESIZE*)hdr;contentWidth=std::max(12,int(r->rc.right-r->rc.left));contentHeight=std::max(fullTextHeight(),int(r->rc.bottom-r->rc.top));if(widthLimit>0)contentWidth=std::max(12,int(std::lround(widthLimit*session->view().scale*session->view().dpiScale)));loading=true;position();resetScroll();loading=false;InvalidateRect(hwnd,0,FALSE);}return true;}
    // Content changed (from the parent's EN_CHANGE): one coalesced re-layout
    // per pump cycle, however many input messages produced the change. The
    // heavy work — preview sync, re-wrap, scroll reset — runs in WM_APP+51.
    void noteContentChange(){if(active&&!loading&&!refreshPending){refreshPending=true;PostMessageW(hwnd,WM_APP+51,0,0);}}
    std::vector<TextRun> runs(){
        // TOM ranges inspect formatting without moving the live selection.
        // Per-character EM_EXSETSEL raised thousands of accessibility events
        // and repeatedly reformatted long captions, making commit appear hung.
        IUnknown* ole=nullptr;ITextDocument* document=nullptr;
        const IID iid={0x8CC497C0,0xA1DF,0x11CE,{0x80,0x98,0x00,0xAA,0x00,0x47,0xBE,0x5D}};
        if(studio_i18n::sendMessage(hwnd,EM_GETOLEINTERFACE,0,(LPARAM)&ole)&&ole){ole->QueryInterface(iid,(void**)&document);ole->Release();}
        if(!document)return runsBySelection();
        auto w=value();auto italic=chemitalic::maskUtf16(w);std::vector<TextRun> out;runColors.clear();bool failed=false;
        for(LONG at=0;at<LONG(w.size());){
            ITextRange* range=nullptr;ITextFont* font=nullptr;LONG delta=0,end=at+1;
            if(FAILED(document->Range(at,at+1,&range))||!range){failed=true;break;}
            if(FAILED(range->Expand(tomCharFormat,&delta))||FAILED(range->GetEnd(&end))||end<=at||FAILED(range->GetFont(&font))||!font){range->Release();failed=true;break;}
            end=std::min(end,LONG(w.size()));BSTR name=nullptr;float points=0;LONG bold=0,slant=0,underline=0,sub=0,super=0,color=0;
            font->GetName(&name);font->GetSize(&points);font->GetBold(&bold);font->GetItalic(&slant);font->GetUnderline(&underline);font->GetSubscript(&sub);font->GetSuperscript(&super);font->GetForeColor(&color);
            TextRun base;base.fontName=name?U(std::wstring(name,SysStringLen(name))):family;if(name)SysFreeString(name);
            base.size=std::abs(points-size)<.06?0:points;base.flags=(bold==tomTrue?kBold:0)|(slant==tomTrue?kItalic:0)|(underline>0?kUnderline:0)|(sub==tomTrue?kSubscript:0)|(super==tomTrue?kSuperscript:0);
            uint32_t rgba=color<0?format.colorRGBA:(uint32_t(GetRValue(color))<<24)|(uint32_t(GetGValue(color))<<16)|(uint32_t(GetBValue(color))<<8)|255;
            font->Release();range->Release();
            for(LONG i=at;i<end;){LONG count=(w[i]>=0xD800&&w[i]<=0xDBFF&&i+1<LONG(w.size()))?2:1;TextRun r=base;r.utf8=w[i]==L'\r'?"\n":U(w.substr(i,count));if(size_t(i)<italic.size()&&italic[i])r.flags|=kItalic;
                if(!out.empty()&&runColors.back()==rgba&&out.back().flags==r.flags&&out.back().fontName==r.fontName&&out.back().size==r.size)out.back().utf8+=r.utf8;else{out.push_back(r);runColors.push_back(rgba);}i+=count;
            }
            at=end;
        }
        document->Release();return failed?runsBySelection():out;
    }
    std::vector<TextRun> runsBySelection(){QuietEdit quiet(*this);POINT savedScroll={};studio_i18n::sendMessage(hwnd,EM_GETSCROLLPOS,0,(LPARAM)&savedScroll);auto w=value();auto autoItalic=chemitalic::maskUtf16(w);std::vector<TextRun> out;runColors.clear();CHARRANGE saved;studio_i18n::sendMessage(hwnd,EM_EXGETSEL,0,(LPARAM)&saved);studio_i18n::sendMessage(hwnd,WM_SETREDRAW,FALSE,0);for(size_t i=0;i<w.size();){size_t n=(w[i]>=0xD800&&w[i]<=0xDBFF&&i+1<w.size())?2:1;select(LONG(i),LONG(i+n));CHARFORMAT2W c={};c.cbSize=sizeof(c);studio_i18n::sendMessage(hwnd,EM_GETCHARFORMAT,SCF_SELECTION,(LPARAM)&c);TextRun r;r.utf8=w[i]==L'\r'?"\n":U(w.substr(i,n));r.fontName=U(c.szFaceName);r.size=c.yHeight/20.0;if(std::abs(r.size-size)<.06)r.size=0;r.flags=(c.dwEffects&CFE_BOLD?kBold:0)|(c.dwEffects&CFE_ITALIC?kItalic:0)|(c.dwEffects&CFE_UNDERLINE?kUnderline:0)|(c.dwEffects&CFE_SUBSCRIPT?kSubscript:0)|(c.dwEffects&CFE_SUPERSCRIPT?kSuperscript:0);if(i<autoItalic.size()&&autoItalic[i])r.flags|=kItalic;uint32_t color=(uint32_t(GetRValue(c.crTextColor))<<24)|(uint32_t(GetGValue(c.crTextColor))<<16)|(uint32_t(GetBValue(c.crTextColor))<<8)|255;if(!out.empty()&&runColors.back()==color&&out.back().flags==r.flags&&out.back().fontName==r.fontName&&out.back().size==r.size)out.back().utf8+=r.utf8;else{out.push_back(r);runColors.push_back(color);}i+=n;}studio_i18n::sendMessage(hwnd,EM_EXSETSEL,0,(LPARAM)&saved);studio_i18n::sendMessage(hwnd,EM_SETSCROLLPOS,0,(LPARAM)&savedScroll);studio_i18n::sendMessage(hwnd,WM_SETREDRAW,TRUE,0);return out;}
    Error finish(bool accept){if(!active||closing)return Error();closing=true;if(resizing)finishResize();if(host)host->textEditFinishing();
        if(accept){
            // Completion runs once, on accept: the typed spelling becomes the
            // dictionary spelling (tbu -> tBu) and the glyphs get formula
            // typography (SO42- -> SO4^2-, Fe3+ -> Fe^3+, Ph+* -> Ph^+ with a
            // radical dot). A caption is completed only when the WHOLE caption
            // is one chemical token, so sentences are never re-cased.
            auto typed=U(value());
            std::string canonical;
            if(atom!=kInvalidObjectId)canonical=chemcompletion::normalize(typed);
            else if(!typed.empty())canonical=chemcompletion::chemicalToken(typed);
            if(!canonical.empty()){
                auto pieces=chemcompletion::formulaLayout()?chemcompletion::segments(canonical):std::vector<chemcompletion::Segment>();
                if(canonical!=typed||!pieces.empty()){
                    QuietEdit quiet(*this);
                    CHARRANGE saved;studio_i18n::sendMessage(hwnd,EM_EXGETSEL,0,(LPARAM)&saved);
                    studio_i18n::sendMessage(hwnd,WM_SETREDRAW,FALSE,0);
                    // Only a spelling change rewrites the characters (a whole-text
                    // replace inherits one character format, so it must not run when
                    // the typography pass is the only reason to touch the control).
                    if(canonical!=typed){select(0,-1);auto normalized=W(canonical);studio_i18n::sendMessage(hwnd,EM_REPLACESEL,TRUE,(LPARAM)normalized.c_str());}
                    auto customFormat=chemcompletion::savedFormat(canonical);
                    LONG at=0;
                    for(const auto& piece:pieces){
                        LONG length=LONG(W(piece.text).size());if(length<=0)continue;
                        select(at,at+length);
                        CHARFORMAT2W effect={};effect.cbSize=sizeof(effect);effect.dwMask=CFM_SUBSCRIPT|CFM_SUPERSCRIPT;
                        effect.dwEffects=piece.script<0?CFE_SUBSCRIPT:piece.script>0?CFE_SUPERSCRIPT:0;
                        if(customFormat.size()==canonical.size()&&size_t(at)<customFormat.size()){effect.dwMask|=CFM_ITALIC;if((customFormat[at]-'0')&4)effect.dwEffects|=CFE_ITALIC;}
                        studio_i18n::sendMessage(hwnd,EM_SETCHARFORMAT,SCF_SELECTION,(LPARAM)&effect);
                        at+=length;
                    }
                    studio_i18n::sendMessage(hwnd,EM_EXSETSEL,0,(LPARAM)&saved);
                    studio_i18n::sendMessage(hwnd,WM_SETREDRAW,TRUE,0);
                    applyCanvasLineSpacing();resize();
                }
            }
        }
        auto r=runs();if(atom!=kInvalidObjectId)for(auto& part:r)if(!(part.flags&(kSubscript|kSuperscript)))part.flags|=kFormula;std::ostringstream colorsOut;for(auto color:runColors)colorsOut<<color<<' ';auto colors=colorsOut.str();std::string plain;for(auto& part:r)plain+=part.utf8;auto t=text,a=atom;auto p=anchor;auto f=format;double sz=size,wrap=widthLimit;Error e;
        if(accept){if(a!=kInvalidObjectId){e=session->submit(Command("原位原子标签",kInvalidateStructure|kInvalidateAppearance|kInvalidateGeometry,[a,plain,r,colors](CommandContext& c){auto outcome=nicknames::labelCommand(a,plain).execute(c);auto e=outcome.error;if(e.ok()){c.document.setAtomLabel(a,plain);studioatom::set(c.document,a,r);auto& bag=c.document.extensions(ObjectRef(ObjectKind::Atom,a));bag.erase("studio.color.override");bag.set("studio.text.colors",colors);bag.set("studio.text.colors.edited","1");}return e.ok()?CommandResult::success():CommandResult::fail(e.code,e.message);}),"原位原子标签");}
            else if(t!=kInvalidObjectId||!plain.empty()){
                auto id=std::make_shared<ObjectId>(t);e=session->submit(Command("原位文字",kInvalidateAppearance|kInvalidateGeometry|kInvalidateStructure,[id,p,f,sz,wrap,r,plain,colors](CommandContext& c){Error e;if(*id==kInvalidObjectId)e=c.document.addText(p,sz,"",id.get());if(!e.ok())return CommandResult::fail(e.code,e.message);if(plain.empty())e=c.document.removeObject(ObjectRef(ObjectKind::Text,*id));else{e=c.document.setTextRuns(*id,r);if(e.ok()){auto& bag=c.document.extensions(ObjectRef(ObjectKind::Text,*id));bag.erase("studio.color.override");bag.set("studio.text.colors",colors);bag.set("studio.text.colors.edited","1");}if(e.ok())e=c.document.setTextFormat(*id,f);if(e.ok())e=c.document.setTextSize(*id,sz);if(e.ok())e=c.document.setTextPosition(*id,p);if(wrap>0)c.document.extensions(ObjectRef(ObjectKind::Text,*id)).set("studio.text.width",std::to_string(wrap));else c.document.extensions(ObjectRef(ObjectKind::Text,*id)).erase("studio.text.width");}return e.ok()?CommandResult::success():CommandResult::fail(e.code,e.message);}),"原位文字");t=*id;
            }
        }
        if(!e.ok()){closing=false;studio_i18n::setWindowText(help,W("未保存："+e.message).c_str());if(completed)completed(e,accept);SetFocus(hwnd);PostMessageW(owner,WM_APP+31,0,0);return e;}
        active=false;ImmAssociateContext(canvas,nullptr);ShowWindow(hwnd,SW_HIDE);for(auto control:{help,acceptButton,cancelButton})ShowWindow(control,SW_HIDE);session->input.setFocus(interaction::InputFocus::Canvas);bool textTool=session->entries[session->activeIndex].owner=="T09";if(textTool)session->activateCommand("chooseSelectionTool_Marquee");session->selected.clear();if(t!=kInvalidObjectId&&session->doc.texts().alive(t))session->selected.push_back(ObjectRef(ObjectKind::Text,t));SetFocus(canvas);InvalidateRect(canvas,0,FALSE);PostMessageW(owner,WM_APP+31,0,0);closing=false;if(completed)completed(e,accept);return e;
    }
    // Selecting a face/size for the text being edited must not change the default
    // for future captions: ChemDraw keeps the document's caption style as the
    // default and treats the edit as local formatting. Only choosing a format
    // with no editor open (style menu / combo box) updates nextFamily/nextSize.
    void setFont(const std::string& name,double points=0){if(!active){if(!name.empty())nextFamily=name;if(points>0)nextSize=points;return;}CHARFORMAT2W c={};c.cbSize=sizeof(c);if(!name.empty()){c.dwMask|=CFM_FACE;auto f=W(name);wcsncpy(c.szFaceName,f.c_str(),LF_FACESIZE-1);}if(points>0){c.dwMask|=CFM_SIZE;c.yHeight=LONG(std::lround(points*20));}studio_i18n::sendMessage(hwnd,EM_SETCHARFORMAT,SCF_SELECTION,(LPARAM)&c);applyCanvasLineSpacing();resize();if(host)host->textEditChanged();SetFocus(hwnd);}
    void toggle(int flag){if(!active)return;if(flag==kFormula){QuietEdit quiet(*this);CHARRANGE selection;studio_i18n::sendMessage(hwnd,EM_EXGETSEL,0,(LPARAM)&selection);auto text=value();LONG begin=selection.cpMin,end=selection.cpMax;if(begin==end){begin=0;end=LONG(text.size());}bool afterAtom=false;studio_i18n::sendMessage(hwnd,WM_SETREDRAW,FALSE,0);for(LONG i=begin;i<end&&i<LONG(text.size());i++){wchar_t ch=text[i];select(i,i+1);CHARFORMAT2W f={};f.cbSize=sizeof(f);f.dwMask=CFM_SUBSCRIPT|CFM_SUPERSCRIPT;LONG first=i;while(first>0&&text[first-1]>=L'0'&&text[first-1]<=L'9')--first;bool rIndex=first>0&&text[first-1]==L'R'&&(first<2||!iswalpha(text[first-2]));f.dwEffects=(ch>=L'0'&&ch<=L'9'&&afterAtom)?(rIndex?CFE_SUPERSCRIPT:CFE_SUBSCRIPT):0;studio_i18n::sendMessage(hwnd,EM_SETCHARFORMAT,SCF_SELECTION,(LPARAM)&f);if((ch>=L'A'&&ch<=L'Z')||(ch>=L'a'&&ch<=L'z')||ch==L')'||ch==L']')afterAtom=true;else if(!(ch>=L'0'&&ch<=L'9'))afterAtom=false;}studio_i18n::sendMessage(hwnd,EM_EXSETSEL,0,(LPARAM)&selection);studio_i18n::sendMessage(hwnd,WM_SETREDRAW,TRUE,0);resize();if(host)host->textEditChanged();SetFocus(hwnd);return;}CHARFORMAT2W c={};c.cbSize=sizeof(c);studio_i18n::sendMessage(hwnd,EM_GETCHARFORMAT,SCF_SELECTION,(LPARAM)&c);DWORD mask=flag==kBold?CFM_BOLD:flag==kItalic?CFM_ITALIC:flag==kUnderline?CFM_UNDERLINE:flag==kSubscript?CFM_SUBSCRIPT:CFM_SUPERSCRIPT;DWORD effect=flag==kBold?CFE_BOLD:flag==kItalic?CFE_ITALIC:flag==kUnderline?CFE_UNDERLINE:flag==kSubscript?CFE_SUBSCRIPT:CFE_SUPERSCRIPT;c.dwMask=mask;c.dwEffects=(c.dwEffects&effect)?0:effect;studio_i18n::sendMessage(hwnd,EM_SETCHARFORMAT,SCF_SELECTION,(LPARAM)&c);applyCanvasLineSpacing();resize();if(host)host->textEditChanged();SetFocus(hwnd);}
    void align(int value){
        if(!active){format.align=uint8_t(value);return;}
        // Alignment is a box property. Apply it to every paragraph, preserving
        // both the selection and the box's left edge when its anchor changes.
        double width=currentWidth();int old=format.align;anchor.x+=(value==1?width*.5:value==2?width:0)-(old==1?width*.5:old==2?width:0);
        format.align=uint8_t(value);{QuietEdit quiet(*this);CHARRANGE saved;studio_i18n::sendMessage(hwnd,EM_EXGETSEL,0,(LPARAM)&saved);select(0,-1);
        PARAFORMAT2 p={};p.cbSize=sizeof(p);p.dwMask=PFM_ALIGNMENT;p.wAlignment=value==1?PFA_CENTER:value==2?PFA_RIGHT:value==3?PFA_JUSTIFY:PFA_LEFT;
        studio_i18n::sendMessage(hwnd,EM_SETPARAFORMAT,0,(LPARAM)&p);studio_i18n::sendMessage(hwnd,EM_EXSETSEL,0,(LPARAM)&saved);}
        resize();if(host)host->textEditChanged();SetFocus(hwnd);
    }
    static void traceIme(HWND h,UINT m,WPARAM w){
        // Opt-in diagnostics for actual key/IME routing; never logs document text.
        static std::wstring path=[](){wchar_t file[32768]={};GetEnvironmentVariableW(L"MOLECULE_STUDIO_IME_TRACE",file,32768);return std::wstring(file);}();
        if(path.empty()||!(m==WM_SETFOCUS||m==WM_KILLFOCUS||m==WM_INPUTLANGCHANGE||m==WM_KEYDOWN||m==WM_IME_STARTCOMPOSITION||m==WM_IME_ENDCOMPOSITION||m==WM_IME_COMPOSITION))return;
        wchar_t focusClass[100]={};GetClassNameW(GetFocus(),focusClass,100);HIMC context=ImmGetContext(h);bool open=context&&ImmGetOpenStatus(context);if(context)ImmReleaseContext(h,context);
        std::ofstream log(U(path),std::ios::app);log<<"message="<<std::hex<<m<<" key="<<w<<" hkl="<<uintptr_t(GetKeyboardLayout(0))<<" focus="<<U(focusClass)<<" ime_open="<<open<<" ctf_open="<<studio_i18n::sendMessage(h,EM_GETCTFOPENSTATUS,0,0)<<" comp_mode="<<studio_i18n::sendMessage(h,EM_GETIMECOMPMODE,0,0)<<"\n";
    }
    static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){traceIme(h,m,w);auto self=(InlineTextEditor*)GetWindowLongPtrW(h,GWLP_USERDATA);if(!self)return DefWindowProcW(h,m,w,l);
        // Space is text, never a shortcut: with a caption open it must insert a
        // space and keep the editor alive even when a row binds VK_SPACE
        // (shortcut_config.hpp marquee / keymap_data.hpp Generic " ").
        if((m==WM_KEYDOWN||m==WM_SYSKEYDOWN)&&w!=VK_SPACE&&w!=VK_PROCESSKEY&&w!=VK_SHIFT&&w!=VK_CONTROL&&w!=VK_MENU&&(GetKeyState(VK_CONTROL)<0||GetKeyState(VK_MENU)<0||(w>=VK_F1&&w<=VK_F24))&&!self->composing&&self->shortcut&&self->shortcut(m,w))return 0;
        // While typing, the left/right edge of the box resizes the wrap width,
        // exactly like dragging a text box in presentation software: the text
        // re-wraps live and the width is saved on commit.
        if(self->active&&self->atom==kInvalidObjectId){
            if(m==WM_SETCURSOR&&LOWORD(l)==HTCLIENT){POINT point;GetCursorPos(&point);ScreenToClient(h,&point);if(self->edgeAt(point.x)){SetCursor(LoadCursorW(0,MAKEINTRESOURCEW(32644)));return TRUE;}}
            if(m==WM_LBUTTONDOWN&&!self->resizing){int edge=self->edgeAt(GET_X_LPARAM(l));if(edge){self->beginResize(edge);return 0;}}
            if(m==WM_MOUSEMOVE&&self->resizing){self->updateResize(GET_X_LPARAM(l),GET_Y_LPARAM(l));return 0;}
            if(m==WM_LBUTTONUP&&self->resizing){self->finishResize();return 0;}
        }
        if(m==WM_CAPTURECHANGED&&self->resizing)self->resizing=false;
        if(m==WM_APP+51){self->refreshPending=false;if(self->active&&!self->composing){
            // Single coalesced re-layout per content change. The face at the
            // caret is checked here: the first East Asian character switches
            // the run font and with it the metrics the line spacing follows.
            CHARFORMAT2W cf={};cf.cbSize=sizeof(cf);studio_i18n::sendMessage(h,EM_GETCHARFORMAT,SCF_SELECTION,(LPARAM)&cf);
            std::wstring styleKey=std::wstring(cf.szFaceName)+std::to_wstring(cf.yHeight)+L"/"+std::to_wstring(cf.dwEffects);
            bool faceChanged=std::find(self->measuredStyles.begin(),self->measuredStyles.end(),styleKey)==self->measuredStyles.end();
            self->spacingFace=cf.szFaceName;
            if(faceChanged){self->measuredStyles.push_back(styleKey);self->applyCanvasLineSpacing(&cf);}
            self->resize();self->resetScroll();
            if(self->host)self->host->textEditChanged();
        }return 0;}
        if(m==WM_IME_STARTCOMPOSITION){self->composing=true;if(self->host)self->host->textEditCompositionChanged(true);}
        if(m==WM_IME_ENDCOMPOSITION){self->composing=false;self->noteContentChange();if(self->host)self->host->textEditCompositionChanged(false);}
        if(m==WM_CHAR&&!self->active)return 0;
        if(m==WM_CHAR&&w==L'\r'&&self->suppressReturnChar){self->suppressReturnChar=false;return 0;}
        if(m==WM_KEYDOWN&&!self->composing){self->suppressReturnChar=false;if(w==VK_ESCAPE){self->finish(true);return 0;}if(w==VK_RETURN){if(self->atom!=kInvalidObjectId||GetKeyState(VK_SHIFT)>=0||GetKeyState(VK_CONTROL)<0)self->finish(true);else{studio_i18n::sendMessage(h,EM_REPLACESEL,TRUE,(LPARAM)L"\r");self->suppressReturnChar=true;}return 0;}}
        // Content changes arrive as EN_CHANGE (one per change, coalesced in
        // notify()); the per-keystroke resize path that used to live here ran
        // twice per keystroke and made held-backspace stutter.
        // WM_SETTEXT resets RichEdit zoom internally, including when begin()
        // reloads an existing caption. Never reuse the previous zoom cache.
        if(m==WM_SETTEXT){self->appliedZoom=0;self->appliedZoomDenominator=0;}
        LRESULT result=CallWindowProcW(self->original,h,m,w,l);
        // RichEdit scrolls to the caret AFTER synchronous change/resize
        // notifications. Settle after native input, before the next paint;
        // waiting for WM_APP+51 leaves a frame showing only the last line.
        if(m==WM_CHAR||m==WM_KEYDOWN||m==WM_PASTE||m==WM_CUT||m==WM_IME_ENDCOMPOSITION)self->settleInput();
        return result;
    }
};
#endif
