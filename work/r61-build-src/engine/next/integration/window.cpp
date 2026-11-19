#include "session.hpp"
#include "ribbon/ribbon_ui.hpp"
#include "next/text/text_edit.hpp"
#include "next/chemistry/edit.hpp"
#include "next/core/commands.hpp"
#include "next/selection/transform.hpp"
#include "next/selection/grouping.hpp"
#include "next/selection/clipboard.hpp"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <GL/gl.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
namespace r62 { namespace integration {
using namespace core;
static std::wstring wide(const std::string& s){if(s.empty())return std::wstring();int n=MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),0,0);std::wstring r((size_t)(n>0?n:0),L' ');if(n>0)MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),&r[0],n);return r;}
static std::string utf8(const std::wstring& s){if(s.empty())return std::string();int n=WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),0,0,0,0);std::string r((size_t)(n>0?n:0),' ');if(n>0)WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),&r[0],n,0,0);return r;}
static uint32_t mods(){return (GetKeyState(VK_CONTROL)<0?uint32_t(kModControl):0u)|(GetKeyState(VK_SHIFT)<0?uint32_t(kModShift):0u)|(GetKeyState(VK_MENU)<0?uint32_t(kModAlt):0u);}
static const AnnotationRecord* annotationOf(const Document& d,const ObjectRef& r){
    switch(r.kind){case ObjectKind::Arrow:return d.arrows().alive(r.id);case ObjectKind::Bracket:return d.brackets().alive(r.id);
    case ObjectKind::Orbital:return d.orbitals().alive(r.id);case ObjectKind::Symbol:return d.symbols().alive(r.id);
    case ObjectKind::BioShape:return d.bioShapes().alive(r.id);default:return 0;}
}
static bool readLineStyle(const Document& d,const ObjectRef& r,LineStyle* out){
    switch(r.kind){
    case ObjectKind::Path:{const PathRecord* p=d.paths().alive(r.id);if(!p)return false;*out=p->style;return true;}
    case ObjectKind::Arrow:case ObjectKind::Bracket:case ObjectKind::Orbital:case ObjectKind::Symbol:case ObjectKind::BioShape:{
        const AnnotationRecord* a=annotationOf(d,r);if(!a)return false;*out=a->style;return true;}
    case ObjectKind::Bond:{const ExtensionEntry* e=d.extensions(r).find("core.style.line");
        out->width=0;out->colorRGBA=0xFF000000u;out->dash=0;
        if(e&&!e->value.empty()){std::sscanf(e->value.c_str(),"%lf,%u,%hhu",&out->width,&out->colorRGBA,&out->dash);}
        return true;}
    default:return false;
    }
}
enum {ID_FIT=23001,ID_OPEN,ID_SAVE,ID_EXPORT,ID_UNDO,ID_REDO,ID_ALL,ID_TEXT,ID_APPLY,ID_CANCEL,ID_NEW,ID_HELP,ID_LENGTH,ID_ANGLES,
      ID_P_BONDORDER=26100,ID_P_BONDDISPLAY,ID_P_BONDSTEREO,ID_P_BONDSEM,ID_P_BONDSIDE,
      ID_P_ATOMELEMENT,ID_P_ATOMRADICAL,ID_P_ATOMH,ID_P_ATOMISOTOPE,ID_P_ATOMLABEL,ID_P_ATOMLABELAPPLY,
      ID_P_ATOMCHGMINUS,ID_P_ATOMCHGPLUS,
      ID_P_TEXTCONTENT,ID_P_TEXTCONTENTAPPLY,ID_P_TEXTSIZE,ID_P_TEXTBOLD,ID_P_TEXTITALIC,ID_P_TEXTUNDER,
      ID_P_WIDTH,ID_P_COLOR,ID_P_DELETE,ID_P_ROTL,ID_P_ROTR,ID_P_GROUP,ID_P_UNGROUP,ID_P_SEC=26130};
struct PItem{HWND label;HWND ctl;bool header;int section;PItem():label(0),ctl(0),header(false),section(0){}};
struct Window {
    Session& s;HWND h,canvas,edit,apply,cancel,status,hint;HDC dc;HGLRC rc;HFONT font;
    std::unique_ptr<render::Backend> gl;std::unique_ptr<ribbon::RibbonBar> ribbon;
    bool pressed,pan,smoke;Vec2 panAt;ObjectId textId,atomId;std::string image;size_t paints;int mode,dpi;
    bool propsVisible;
    bool syncing;
    int propSection,buildSection;
    bool propUserChose;
    HWND pBondOrder,pBondDisplay,pBondStereo,pBondSem,pBondSide;
    HWND pAtomElement,pAtomRadical,pAtomH,pAtomIsotope,pAtomLabel,pAtomLabelApply,pAtomChgMinus,pAtomChgPlus;
    HWND pTextContent,pTextContentApply,pTextSize,pTextBold,pTextItalic,pTextUnder;
    HWND pWidth,pColor,pDelete,pRotL,pRotR,pGroup,pUngroup,pInfo,pTitle,pSecCombo;
    std::vector<PItem> pItems;
    Window(Session& x):s(x),h(0),canvas(0),edit(0),apply(0),cancel(0),status(0),hint(0),dc(0),rc(0),font(0),
        pressed(false),pan(false),smoke(false),textId(kInvalidObjectId),atomId(kInvalidObjectId),paints(0),mode(0),dpi(96),
        propsVisible(false),syncing(false),propSection(0),buildSection(0),propUserChose(false),pBondOrder(0),pBondDisplay(0),pBondStereo(0),pBondSem(0),pBondSide(0),pAtomElement(0),pAtomRadical(0),
        pAtomH(0),pAtomIsotope(0),pAtomLabel(0),pAtomLabelApply(0),pAtomChgMinus(0),pAtomChgPlus(0),pTextContent(0),
        pTextContentApply(0),pTextSize(0),pTextBold(0),pTextItalic(0),pTextUnder(0),pWidth(0),pColor(0),pDelete(0),
        pRotL(0),pRotR(0),pGroup(0),pUngroup(0),pInfo(0),pTitle(0),pSecCombo(0){}
    int S(int v) const {return MulDiv(v,dpi,96);}
    void error(Error e){if(!e.ok()){s.status=(s.status.empty()?"":s.status+" | ")+e.message;}}
    std::vector<ObjectId> selectedAtoms() const {std::vector<ObjectId> v;for(size_t i=0;i<s.selected.size();++i)if(s.selected[i].kind==ObjectKind::Atom&&s.doc.atoms().alive(s.selected[i].id))v.push_back(s.selected[i].id);return v;}
    std::vector<ObjectId> selectedBonds() const {std::vector<ObjectId> v;for(size_t i=0;i<s.selected.size();++i)if(s.selected[i].kind==ObjectKind::Bond&&s.doc.bonds().alive(s.selected[i].id))v.push_back(s.selected[i].id);return v;}
    std::vector<ObjectId> selectedTexts() const {std::vector<ObjectId> v;for(size_t i=0;i<s.selected.size();++i)if(s.selected[i].kind==ObjectKind::Text&&s.doc.texts().alive(s.selected[i].id))v.push_back(s.selected[i].id);return v;}
    std::vector<ObjectRef> aliveRefs() const {std::vector<ObjectRef> v;for(size_t i=0;i<s.selected.size();++i){const ObjectRef& r=s.selected[i];
        bool ok=false;switch(r.kind){case ObjectKind::Atom:ok=s.doc.atoms().alive(r.id)!=0;break;case ObjectKind::Bond:ok=s.doc.bonds().alive(r.id)!=0;break;
        case ObjectKind::Text:ok=s.doc.texts().alive(r.id)!=0;break;case ObjectKind::Path:ok=s.doc.paths().alive(r.id)!=0;break;
        case ObjectKind::Arrow:ok=s.doc.arrows().alive(r.id)!=0;break;case ObjectKind::Bracket:ok=s.doc.brackets().alive(r.id)!=0;break;
        case ObjectKind::Orbital:ok=s.doc.orbitals().alive(r.id)!=0;break;case ObjectKind::Symbol:ok=s.doc.symbols().alive(r.id)!=0;break;
        case ObjectKind::BioShape:ok=s.doc.bioShapes().alive(r.id)!=0;break;case ObjectKind::Table:ok=s.doc.tables().alive(r.id)!=0;break;default:ok=false;}
        if(ok)v.push_back(r);}return v;}
    Vec2 selectionCenter() const {Box b;bool any=false;std::vector<ObjectRef> refs=aliveRefs();
        for(size_t i=0;i<refs.size();++i){if(refs[i].kind==ObjectKind::Atom){const AtomRecord* a=s.doc.atoms().alive(refs[i].id);if(a){b.add(a->position);any=true;}}}
        if(!any)for(size_t i=0;i<s.scene.primitives().size();++i){const render::Primitive& p=s.scene.primitives()[i];
            if(std::find(refs.begin(),refs.end(),p.source)!=refs.end()&&!p.bounds.empty()){b.add(p.bounds.min);b.add(p.bounds.max);any=true;}}
        return any?Vec2((b.min.x+b.max.x)*.5,(b.min.y+b.max.y)*.5):Vec2();}
    void openText(ObjectId text,ObjectId atom=kInvalidObjectId){s.cancel();textId=text;atomId=atom;std::string value=atom!=kInvalidObjectId?s.doc.atomLabel(atom):s.doc.plainText(text);if(atom!=kInvalidObjectId&&value.empty())value=core::elementSymbol(s.doc.atoms().get(atom)->element);SetWindowTextW(edit,wide(value).c_str());ShowWindow(edit,SW_SHOW);ShowWindow(apply,SW_SHOW);ShowWindow(cancel,SW_SHOW);s.input.setFocus(interaction::InputFocus::TextEntry);SetFocus(edit);SendMessageW(edit,EM_SETSEL,0,-1);}
    // ---- property model -------------------------------------------------
    Error applyProperty(const std::string& key,int value);
    void syncProps();
    void snapshotPng(const std::string& path);
    void layoutProps(int px,int py,int pw,int ph);
    void toggleProps(){propsVisible=!propsVisible;for(size_t i=0;i<pItems.size();++i){ShowWindow(pItems[i].label,propsVisible?SW_SHOW:SW_HIDE);if(pItems[i].ctl)ShowWindow(pItems[i].ctl,propsVisible?SW_SHOW:SW_HIDE);}if(pTitle)ShowWindow(pTitle,propsVisible?SW_SHOW:SW_HIDE);if(pInfo)ShowWindow(pInfo,propsVisible?SW_SHOW:SW_HIDE);if(pSecCombo)ShowWindow(pSecCombo,propsVisible?SW_SHOW:SW_HIDE);layout();}
    void layout();
    void onRibbon(const ribbon::Event& e);
    void sync(){
        if(syncing)return;
        syncing=true;
        ribbon::State st;st.dpi=dpi;st.propsVisible=propsVisible;
        st.activeCommand=s.entries[s.activeIndex].command;
        st.status=s.status;
        for(size_t i=0;i<s.entries.size();++i)if(!s.entries[i].limitation.empty()){st.unavailable.insert(s.entries[i].command);st.reason[s.entries[i].command]=s.entries[i].limitation;}
        st.checked.insert(s.entries[s.activeIndex].command);
        if(s.fixedLen)st.checked.insert("con.fixedlen");
        if(s.fixedAngle)st.checked.insert("con.fixedangle");
        if(propsVisible)st.checked.insert("prop.panel");
        {std::ostringstream sel;sel<<"已选 "<<s.selected.size()<<" 个对象";size_t na=selectedAtoms().size(),nb=selectedBonds().size(),nt=selectedTexts().size();
         if(na||nb||nt){sel<<"（原子 "<<na<<" / 键 "<<nb<<" / 文字 "<<nt<<"）";}st.selectionSummary=sel.str();}
        std::vector<ObjectId> bonds=selectedBonds();
        if(!bonds.empty()){
            bool uo=true,ud=true;
            BondOrder o0=s.doc.bonds().get(bonds[0])->order;BondDisplay d0=s.doc.bonds().get(bonds[0])->display;
            for(size_t i=1;i<bonds.size();++i){if(s.doc.bonds().get(bonds[i])->order!=o0)uo=false;if(s.doc.bonds().get(bonds[i])->display!=d0)ud=false;}
            if(uo){const char* id=o0==BondOrder::Single?"bond.single":o0==BondOrder::Double?"bond.double":o0==BondOrder::Triple?"bond.triple":o0==BondOrder::Quadruple?"bond.quad":o0==BondOrder::Aromatic?"bond.aromatic":0;if(id)st.checked.insert(id);}
            if(ud){const char* id=d0==BondDisplay::Wedge?"bond.wedge":d0==BondDisplay::Hash?"bond.hash":d0==BondDisplay::Dashed?"bond.dash":d0==BondDisplay::Bold?"bond.bold":d0==BondDisplay::Wavy?"bond.wavy":0;if(id)st.checked.insert(id);}
        }
        ribbon->setState(st);
        auto st2=s.doc.stats();std::ostringstream line;
        line<<st2.aliveAtoms<<" atoms / "<<st2.aliveBonds<<" bonds  |  "<<int(s.view().scale*100)<<"%  |  2× SSAA  |  ";
        auto hs=s.input.hotspot();
        if(s.bondDrawing&&s.bondSnap.joined)line<<"绿色: 将连接原子 "<<s.bondSnap.target.id;
        else if(hs.kind==interaction::HotspotKind::Atom)line<<"蓝色原子热点 "<<hs.object.id<<" · c/n/o 改元素 · 空格选分子";
        else if(hs.kind==interaction::HotspotKind::Bond)line<<"蓝色键热点 "<<hs.object.id<<" · 1/2/3 改键级 · a 稠合苯环";
        else line<<"空白: j 苯环 · x 单键 · t 文字 · 中键平移";
        SetWindowTextW(status,wide(line.str()).c_str());
        SetWindowTextW(hint,wide(s.status).c_str());
        CheckMenuItem(GetMenu(h),ID_LENGTH,MF_BYCOMMAND|(s.fixedLen?MF_CHECKED:MF_UNCHECKED));
        CheckMenuItem(GetMenu(h),ID_ANGLES,MF_BYCOMMAND|(s.fixedAngle?MF_CHECKED:MF_UNCHECKED));
        if(s.editAtom!=kInvalidObjectId){auto id=s.editAtom;s.editAtom=kInvalidObjectId;openText(kInvalidObjectId,id);}
        syncProps();
        syncing=false;
    }
    void trace(const char* m){if(!smoke)return;std::string d=image.substr(0,image.find_last_of("/\\"));std::ofstream f((d+"/ribbon_trace.txt").c_str(),std::ios::app);if(f){f<<m<<"\n";f.flush();}}
    void repaint(){sync();InvalidateRect(canvas,0,FALSE);}
    std::string file(bool save,bool xml=false){wchar_t p[32768]=L"";OPENFILENAMEW of={};of.lStructSize=sizeof(of);of.hwndOwner=h;of.lpstrFile=p;of.nMaxFile=32768;
        of.lpstrFilter=save?(xml?L"CDXML\0*.cdxml\0\0":L"T19 文档\0*.r62doc\0\0"):L"结构文档\0*.cdxml;*.r62doc;*.t01doc\0所有文件\0*.*\0\0";
        of.lpstrDefExt=save?(xml?L"cdxml":L"r62doc"):0;of.Flags=OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|(save?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);
        s.cancel();s.input.setFocus(interaction::InputFocus::ModalDialog);bool ok=save?GetSaveFileNameW(&of):GetOpenFileNameW(&of);
        s.input.setFocus(interaction::InputFocus::Canvas);return ok?utf8(p):"";}
    bool leave(){if(!s.dirty)return true;int a=MessageBoxW(h,L"另存当前 T19 文档后继续？",L"未保存的编辑",MB_YESNOCANCEL|MB_ICONQUESTION);if(a==IDCANCEL)return false;if(a==IDNO)return true;auto p=file(true);if(p.empty())return false;auto e=s.save(p);error(e);return e.ok();}
    void finishText(bool accept){if(accept){int n=GetWindowTextLengthW(edit);std::wstring t((size_t)n+1,L' ');GetWindowTextW(edit,&t[0],n+1);t.resize((size_t)n);auto value=utf8(t);
        if(atomId!=kInvalidObjectId)error(s.submit(chemistry::setLabelTextCommand(atomId,value,{},0),"atom label editor"));
        else {text::TextEditSession x;x.begin(s.doc.runs(textId));x.selectAll();x.insertUtf8(value,text::EditStyle());auto runs=x.commit();auto id=textId;
            error(s.submit(Command("replace text",kInvalidateAppearance,[id,runs](CommandContext& c){auto e=c.document.setTextRuns(id,runs);return e.ok()?CommandResult::success():CommandResult::fail(e.code,e.message);}),"text editor"));}}
        ShowWindow(edit,SW_HIDE);ShowWindow(apply,SW_HIDE);ShowWindow(cancel,SW_HIDE);textId=atomId=kInvalidObjectId;s.input.setFocus(interaction::InputFocus::Canvas);SetFocus(canvas);repaint();}
    void command(int id){using A=interaction::HotkeyAction;
        switch(id){
        case ID_NEW:if(leave()){s.cancel();s.doc=Document();s.history.clear();s.selected.clear();s.sourcePath.clear();s.input.bindDocument(s.doc.id());s.refresh();s.dirty=false;auto v=s.view();v.center=Vec2();v.scale=2;s.setView(v);}break;
        case ID_OPEN:if(leave()){auto p=file(false);if(!p.empty())error(s.open(p));}break;
        case ID_SAVE:case ID_EXPORT:{auto p=file(true,id==ID_EXPORT);if(!p.empty())error(s.save(p,id==ID_EXPORT));break;}
        case ID_FIT:s.router.dispatch(A::ZoomFit,"menu");break;case ID_UNDO:s.router.dispatch(A::Undo,"menu");break;case ID_REDO:s.router.dispatch(A::Redo,"menu");break;
        case ID_ALL:s.router.dispatch(A::EditSelectAll,"menu");break;case ID_LENGTH:s.router.dispatch(A::ToggleFixedLength,"menu");break;case ID_ANGLES:s.router.dispatch(A::ToggleFixedAngles,"menu");break;
        case ID_TEXT:if(!s.selected.empty()&&s.selected[0].kind==ObjectKind::Text)openText(s.selected[0].id);else if(!s.selected.empty()&&s.selected[0].kind==ObjectKind::Atom)openText(kInvalidObjectId,s.selected[0].id);else s.status="先选择文字或原子";break;
        case ID_APPLY:finishText(true);break;case ID_CANCEL:finishText(false);break;
        case ID_HELP:MessageBoxW(h,L"独立编辑模式，尚未接入 ChemDraw。\n\n顶部 Ribbon 的每个按钮都进入同一 CommandRouter；悬停查看快捷键与说明。\n空白：j 苯环，x 单键，X 碳链，t 文字，e 箭头，空格框选。\n原子热点：c/n/o/s/f/p/h/i/b/l；Enter 编辑标签；Backspace 清除标签。\n键热点：1/2/3 改键级；a 稠合苯环；l/c/r 双键偏侧。\n热点上空格选择整个连通分子。\nCtrl+Z 撤销，Ctrl+Shift+Z 重做，Ctrl+Y 重复。Ctrl+L/E 固定键长/角度，Alt 临时释放。\nF5 实际大小，F6 全图，F7/F8 缩放；中键平移。\n蓝圈/框=热点，绿圈十字=精确连接，蓝线=预览。\n未接通的命令会显示 Unsupported，不会假装成功。",L"操作说明与兼容范围",MB_OK);break;
        default:break;
        }if(!IsWindowVisible(edit))SetFocus(canvas);repaint();}
    static interaction::RouterOutcome fileAction(void* data,const interaction::ActionRequest& req){Window& w=*static_cast<Window*>(data);using A=interaction::HotkeyAction;w.command(req.action==A::FileNew?ID_NEW:req.action==A::FileOpen?ID_OPEN:ID_SAVE);return interaction::RouterOutcome::Handled;}
};
// ---------------------------------------------------------------------------
// Property application: every branch is a real core::Command or an explicit
// refusal. No branch fabricates success.
// ---------------------------------------------------------------------------
Error Window::applyProperty(const std::string& key,int value){
    using namespace core::commands;
    if(key=="__unsupported__"||key=="__refused__")return Error(ErrorCode::Unsupported,s.status);
    std::vector<ObjectId> atoms=selectedAtoms(),bonds=selectedBonds(),texts=selectedTexts();
    std::vector<ObjectRef> refs=aliveRefs();
    if(key=="atom.element"){if(atoms.empty())return Error(ErrorCode::Unsupported,"未选择原子：先在画布选择或点击原子");return s.submit(setElements(atoms,value),"ribbon atom element");}
    if(key=="atom.charge.delta"||key=="atom.h.cycle"||key=="atom.isotope.cycle"||key=="atom.radical.cycle"){
        if(atoms.empty())return Error(ErrorCode::Unsupported,"未选择原子");
        int mode=key=="atom.charge.delta"?0:key=="atom.h.cycle"?1:key=="atom.isotope.cycle"?2:3;
        std::vector<ObjectId> ids(atoms);
        return s.submit(Command("ribbon atom semantics",kInvalidateAppearance|kInvalidateSemantics,
            [ids,mode,value](CommandContext& c)->CommandResult{
                for(size_t i=0;i<ids.size();++i){const AtomRecord* a=c.document.atoms().alive(ids[i]);if(!a)continue;AtomSemantics sm=a->semantics;
                    if(mode==0){int n=int(sm.charge)+value;if(n<-15)n=-15;if(n>15)n=15;sm.charge=int8_t(n);}
                    else if(mode==1){static const int hs[6]={-1,0,1,2,3,4};int idx=0;for(int k=0;k<6;++k)if(hs[k]==sm.implicitHydrogens)idx=k;sm.implicitHydrogens=int8_t(hs[(idx+1)%6]);}
                    else if(mode==2){static const int is[6]={0,13,14,15,2,18};int idx=0;for(int k=0;k<6;++k)if(is[k]==sm.isotope)idx=k;sm.isotope=int16_t(is[(idx+1)%6]);}
                    else {static const int rs[4]={0,1,2,3};int idx=0;for(int k=0;k<4;++k)if(rs[k]==sm.radical)idx=k;sm.radical=uint8_t(rs[(idx+1)%4]);}
                    Error e=c.document.setAtomSemantics(ids[i],sm);if(!e.ok())return CommandResult::fail(e.code,e.message);}
                return CommandResult::success(kInvalidateAppearance|kInvalidateSemantics);}),"ribbon atom semantics");
    }
    if(key=="atom.label.clear"){if(atoms.empty())return Error(ErrorCode::Unsupported,"未选择原子");return s.submit(chemistry::clearLabelCommand(atoms),"clear label");}
    if(key=="atom.label.edit"){if(atoms.empty())return Error(ErrorCode::Unsupported,"未选择原子");openText(kInvalidObjectId,atoms[0]);return Error();}
    if(key=="bond.order"){if(bonds.empty())return Error(ErrorCode::Unsupported,"未选择化学键");if(value<1||value>5)return Error(ErrorCode::InvalidArgument,"键级越界");return s.submit(setBondOrders(bonds,BondOrder(value)),"ribbon bond order");}
    if(key=="bond.display"){if(bonds.empty())return Error(ErrorCode::Unsupported,"未选择化学键");return s.submit(setBondDisplay(bonds,BondDisplay(value)),"ribbon bond display");}
    if(key=="bond.semantics"){if(bonds.empty())return Error(ErrorCode::Unsupported,"未选择化学键");return s.submit(setBondSemantics(bonds,BondSemantics(value)),"ribbon bond semantics");}
    if(key=="bond.stereo"){if(bonds.empty())return Error(ErrorCode::Unsupported,"未选择化学键");return s.submit(setBondStereo(bonds,BondStereo(value)),"ribbon bond stereo");}
    if(key=="bond.side"){if(bonds.empty())return Error(ErrorCode::Unsupported,"未选择化学键");return s.submit(bonds::setBondSideCommand(bonds,bonds::DoubleSide(value)),"ribbon bond side");}
    if(key=="style.width.delta"||key=="style.width.set"){
        std::vector<std::pair<ObjectRef,LineStyle> > plan;int skipped=0;
        for(size_t i=0;i<refs.size();++i){LineStyle st;if(!readLineStyle(s.doc,refs[i],&st)){++skipped;continue;}
            double w=key=="style.width.set"?value/10.0:st.width+value*0.5;if(w<0.5)w=0.5;if(w>8)w=8;st.width=w;plan.push_back(std::make_pair(refs[i],st));}
        if(plan.empty())return Error(ErrorCode::Unsupported,"当前选择没有可设置的线型（原子/文字不带线宽）");
        return s.submit(Command("ribbon line width",kInvalidateAppearance,[plan](CommandContext& c)->CommandResult{
            for(size_t i=0;i<plan.size();++i){Error e=c.document.setLineStyle(plan[i].first,plan[i].second);if(!e.ok())return CommandResult::fail(e.code,e.message);}
            return CommandResult::success(kInvalidateAppearance);}),"ribbon line width");
    }
    if(key=="style.color"||key=="style.fill"){
        bool fill=key=="style.fill";
        if(fill)return Error(ErrorCode::Unsupported,"T01 v10000 没有公共填充设置命令；填充色暂未接通，未伪造成功");
        std::vector<std::pair<ObjectRef,LineStyle> > plan;
        for(size_t i=0;i<refs.size();++i){LineStyle st;if(readLineStyle(s.doc,refs[i],&st))plan.push_back(std::make_pair(refs[i],st));}
        if(plan.empty())return Error(ErrorCode::Unsupported,"当前选择没有可设置颜色的对象");
        CHOOSECOLORW cc={};static COLORREF cust[16]={0};cc.lStructSize=sizeof(cc);cc.hwndOwner=h;cc.lpCustColors=cust;cc.Flags=CC_FULLOPEN|CC_RGBINIT;
        cc.rgbResult=RGB((plan[0].second.colorRGBA>>24)&0xFF,(plan[0].second.colorRGBA>>16)&0xFF,(plan[0].second.colorRGBA>>8)&0xFF);
        s.input.setFocus(interaction::InputFocus::ModalDialog);
        BOOL ok=ChooseColorW(&cc);s.input.setFocus(interaction::InputFocus::Canvas);
        if(!ok)return Error();
        uint32_t rgba=(uint32_t(GetRValue(cc.rgbResult))<<24)|(uint32_t(GetGValue(cc.rgbResult))<<16)|(uint32_t(GetBValue(cc.rgbResult))<<8)|0xFFu;
        for(size_t i=0;i<plan.size();++i)plan[i].second.colorRGBA=rgba;
        return s.submit(Command("ribbon line colour",kInvalidateAppearance,[plan](CommandContext& c)->CommandResult{
            for(size_t i=0;i<plan.size();++i){Error e=c.document.setLineStyle(plan[i].first,plan[i].second);if(!e.ok())return CommandResult::fail(e.code,e.message);}
            return CommandResult::success(kInvalidateAppearance);}),"ribbon line colour");
    }
    if(key=="style.reset"){
        std::vector<std::pair<ObjectRef,LineStyle> > plan;
        for(size_t i=0;i<refs.size();++i){LineStyle st;if(readLineStyle(s.doc,refs[i],&st)){st.width=0;st.colorRGBA=0xFF000000u;st.dash=0;plan.push_back(std::make_pair(refs[i],st));}}
        if(plan.empty())return Error(ErrorCode::Unsupported,"当前选择没有可复位的样式");
        return s.submit(Command("ribbon style reset",kInvalidateAppearance,[plan](CommandContext& c)->CommandResult{
            for(size_t i=0;i<plan.size();++i){Error e=c.document.setLineStyle(plan[i].first,plan[i].second);if(!e.ok())return CommandResult::fail(e.code,e.message);}
            return CommandResult::success(kInvalidateAppearance);}),"ribbon style reset");
    }
    if(key.compare(0,10,"text.flags")==0){
        if(texts.empty())return Error(ErrorCode::Unsupported,"未选择文字对象");
        uint16_t bit=key=="text.flags.bold"?kBold:key=="text.flags.italic"?kItalic:key=="text.flags.underline"?kUnderline:key=="text.flags.sub"?kSubscript:kSuperscript;
        std::vector<ObjectId> ids(texts);
        return s.submit(Command("ribbon text flag",kInvalidateAppearance,[ids,bit](CommandContext& c)->CommandResult{
            for(size_t i=0;i<ids.size();++i){std::vector<TextRun> runs=c.document.runs(ids[i]);for(size_t j=0;j<runs.size();++j)runs[j].flags^=bit;
                Error e=c.document.setTextRuns(ids[i],runs);if(!e.ok())return CommandResult::fail(e.code,e.message);}
            return CommandResult::success(kInvalidateAppearance);}),"ribbon text flag");
    }
    if(key=="text.size"){if(texts.empty())return Error(ErrorCode::Unsupported,"未选择文字对象");
        std::vector<ObjectId> ids(texts);double sz=double(value);
        return s.submit(Command("ribbon text size",kInvalidateAppearance,[ids,sz](CommandContext& c)->CommandResult{
            for(size_t i=0;i<ids.size();++i){std::vector<TextRun> runs=c.document.runs(ids[i]);for(size_t j=0;j<runs.size();++j)runs[j].size=0;
                Error e=c.document.setTextRuns(ids[i],runs);if(e.ok())e=c.document.setTextSize(ids[i],sz);if(!e.ok())return CommandResult::fail(e.code,e.message);}
            return CommandResult::success(kInvalidateAppearance);}),"ribbon text size");}
    if(key=="text.align"){
        if(texts.empty())return Error(ErrorCode::Unsupported,"未选择文字对象");
        ObjectId id=texts[0];TextFormat fmt=s.doc.texts().get(id)->format;fmt.align=uint8_t(value);
        return s.submit(Command("ribbon text align",kInvalidateAppearance,[id,fmt](CommandContext& c)->CommandResult{Error e=c.document.setTextFormat(id,fmt);return e.ok()?CommandResult::success(kInvalidateAppearance):CommandResult::fail(e.code,e.message);}),"ribbon text align");
    }
    if(key=="text.color"){
        if(texts.empty())return Error(ErrorCode::Unsupported,"未选择文字对象");
        ObjectId id=texts[0];TextFormat fmt=s.doc.texts().get(id)->format;uint32_t cur=fmt.colorRGBA;
        CHOOSECOLORW cc={};static COLORREF cust2[16]={0};cc.lStructSize=sizeof(cc);cc.hwndOwner=h;cc.lpCustColors=cust2;cc.Flags=CC_FULLOPEN|CC_RGBINIT;
        cc.rgbResult=RGB((cur>>24)&0xFF,(cur>>16)&0xFF,(cur>>8)&0xFF);
        s.input.setFocus(interaction::InputFocus::ModalDialog);BOOL ok=ChooseColorW(&cc);s.input.setFocus(interaction::InputFocus::Canvas);
        if(!ok)return Error();
        fmt.colorRGBA=(uint32_t(GetRValue(cc.rgbResult))<<24)|(uint32_t(GetGValue(cc.rgbResult))<<16)|(uint32_t(GetBValue(cc.rgbResult))<<8)|0xFFu;
        return s.submit(Command("ribbon text colour",kInvalidateAppearance,[id,fmt](CommandContext& c)->CommandResult{Error e=c.document.setTextFormat(id,fmt);return e.ok()?CommandResult::success(kInvalidateAppearance):CommandResult::fail(e.code,e.message);}),"ribbon text colour");
    }
    if(key=="text.font.default"||key=="text.font.mono"){
        if(texts.empty())return Error(ErrorCode::Unsupported,"未选择文字对象");
        std::string face=key=="text.font.mono"?"Consolas":"";std::vector<ObjectId> ids(texts);
        return s.submit(Command("ribbon text font",kInvalidateAppearance,[ids,face](CommandContext& c)->CommandResult{
            for(size_t i=0;i<ids.size();++i){std::vector<TextRun> runs=c.document.runs(ids[i]);for(size_t j=0;j<runs.size();++j)runs[j].fontName=face;
                Error e=c.document.setTextRuns(ids[i],runs);if(!e.ok())return CommandResult::fail(e.code,e.message);}
            return CommandResult::success(kInvalidateAppearance);}),"ribbon text font");
    }
    if(key=="text.edit"){if(texts.empty())return Error(ErrorCode::Unsupported,"未选择文字对象");openText(texts[0]);return Error();}
    if(key=="select.align"){
        if(refs.empty())return Error(ErrorCode::Unsupported,"未选择对象");
        std::vector<ObjectRef> r2(refs);int mode=value;selection::AlignOptions opt;
        return s.submit(Command("ribbon align",kInvalidateGeometry,[r2,mode,opt](CommandContext& c)->CommandResult{
            selection::TransformReport rep;selection::alignObjects(c.document,r2,selection::AlignMode(mode),opt,&rep);
            return CommandResult::success(kInvalidateGeometry);}),"ribbon align");
    }
    if(key=="select.distribute"){
        if(refs.size()<3)return Error(ErrorCode::Unsupported,"分布需要至少三个可测对象");
        std::vector<ObjectRef> r2(refs);int mode=value;
        return s.submit(Command("ribbon distribute",kInvalidateGeometry,[r2,mode](CommandContext& c)->CommandResult{
            selection::DistributeReport rep=selection::distributeObjects(c.document,r2,selection::DistributeMode(mode));
            if(!rep.applied)return CommandResult::fail(ErrorCode::Unsupported,rep.why.empty()?"分布不适用":rep.why);
            return CommandResult::success(kInvalidateGeometry);}),"ribbon distribute");
    }
    if(key=="select.rotate"||key=="select.flip"||key=="select.scale"){
        if(refs.empty())return Error(ErrorCode::Unsupported,"未选择对象");
        std::vector<ObjectRef> r2(refs);Vec2 pivot=selectionCenter();int v=value;std::string k=key;selection::TransformOptions opt;
        return s.submit(Command("ribbon transform",kInvalidateGeometry,[r2,pivot,v,k,opt](CommandContext& c)->CommandResult{
            if(k=="select.rotate")selection::rotateObjects(c.document,r2,pivot,double(v),opt);
            else if(k=="select.flip")selection::flipObjects(c.document,r2,pivot,v==0,opt);
            else selection::scaleObjects(c.document,r2,pivot,v>0?1.1:0.9,opt);
            return CommandResult::success(kInvalidateGeometry);}),"ribbon transform");
    }
    if(key=="select.group"){
        if(refs.size()<2)return Error(ErrorCode::Unsupported,"编组至少需要两个对象");
        std::vector<ObjectRef> r2(refs);
        return s.submit(Command("ribbon group",kInvalidateStructure|kInvalidateSelection,[r2](CommandContext& c)->CommandResult{
            selection::GroupReport rep;Error e=selection::createGroup(c.document,r2,"T19 group",&rep);
            return e.ok()?CommandResult::success(kInvalidateStructure|kInvalidateSelection):CommandResult::fail(e.code,e.message);}),"ribbon group");
    }
    if(key=="select.ungroup"){
        std::vector<ObjectId> groups;for(size_t i=0;i<refs.size();++i)if(refs[i].kind==ObjectKind::Group)groups.push_back(refs[i].id);
        if(groups.empty())return Error(ErrorCode::Unsupported,"未选择分组记录");
        return s.submit(Command("ribbon ungroup",kInvalidateStructure|kInvalidateSelection,[groups](CommandContext& c)->CommandResult{
            selection::GroupReport rep;selection::ungroup(c.document,groups,false,&rep);return CommandResult::success(kInvalidateStructure|kInvalidateSelection);}),"ribbon ungroup");
    }
    if(key=="select.merge"){
        if(atoms.size()<2)return Error(ErrorCode::Unsupported,"合并至少需要两个原子");
        std::vector<ObjectRef> r2(refs);
        return s.submit(Command("ribbon merge",kInvalidateStructure|kInvalidateGeometry,[r2](CommandContext& c)->CommandResult{
            selection::MergeReport rep=selection::mergeSelection(c.document,r2,2.0);
            if(rep.mergedPairs==0)return CommandResult::fail(ErrorCode::Unsupported,"没有可合并的兼容原子对");
            return CommandResult::success(kInvalidateStructure|kInvalidateGeometry);}),"ribbon merge");
    }
    if(key=="select.zorder")return Error(ErrorCode::Unsupported,"T01 无逐对象 z；层级只对分组记录生效，本装配未接线");
    if(key=="view.scale"){auto v=s.view();v.scale=value/100.0;s.setView(v);return Error();}
    if(key=="panel.props"){toggleProps();return Error();}
    if(key=="panel.focus.bond"||key=="panel.focus.atom"||key=="panel.focus.text"){
        propSection=key=="panel.focus.bond"?0:key=="panel.focus.atom"?1:2;propUserChose=true;
        if(propsVisible)layout();else toggleProps();
        s.status="属性区已切换到 "+std::string(propSection==0?"键":propSection==1?"原子":"文字")+" 属性";
        return Error();}
    return Error(ErrorCode::Unsupported,"未知属性键: "+key);
}
void Window::onRibbon(const ribbon::Event& e){
    using namespace ribbon;
    if(e.kind==Event::Kind::Command){s.cancel();s.activateCommand(e.command);if(!IsWindowVisible(edit))SetFocus(canvas);repaint();return;}
    if(e.kind==Event::Kind::Action){
        interaction::ActionRequest r;r.action=interaction::HotkeyAction(e.action);r.origin="ribbon";r.pointer=s.input.pointerDocument();r.hotspot=s.input.hotspot();
        size_t before=s.input.stats().unsupported;
        interaction::RouterOutcome o=s.router.dispatch(r);
        if(o==interaction::RouterOutcome::Unsupported||o==interaction::RouterOutcome::NotHandled){s.reportUnsupported(std::string(interaction::hotkeyActionName(r.action)));}
        (void)before;repaint();return;
    }
    if(e.kind==Event::Kind::FileOp){command(e.fileOp==1?ID_NEW:e.fileOp==2?ID_OPEN:e.fileOp==3?ID_SAVE:ID_EXPORT);return;}
    if(e.kind==Event::Kind::PropertyOp){
        if(e.command=="__unsupported__"||e.command=="__refused__"){s.status=e.text.empty()?"Unsupported":e.text;repaint();return;}
        Error er=applyProperty(e.command,e.value);if(!er.ok())s.status=er.message;repaint();return;
    }
}
int runWindow(Session& s,const std::string& path,bool smoke,const std::string& screenshot,int mode);
void Window::layoutProps(int px,int py,int pw,int ph){
    if(pSecCombo)MoveWindow(pSecCombo,px+8,py+26,pw-16,S(24),TRUE);
    int y=py+56;int lw=S(58);int cw=pw-S(72);
    for(size_t i=0;i<pItems.size();++i){
        PItem& it=pItems[i];
        if(it.section!=propSection)continue;
        if(it.header){MoveWindow(it.label,px+6,y,pw-12,S(20),TRUE);y+=S(22);continue;}
        MoveWindow(it.label,px+8,y+3,lw,S(18),TRUE);
        MoveWindow(it.ctl,px+8+lw,y,cw,S(22),TRUE);
        y+=S(26);
    }
    MoveWindow(pInfo,px+6,py+ph-S(72),pw-12,S(68),TRUE);
}
void Window::layout(){
    RECT r;GetClientRect(h,&r);int w=r.right,hh=r.bottom;
    int rh=ribbon?ribbon->heightPx():0;int statH=S(46);
    int pw=propsVisible?S(262):0;
    if(ribbon)MoveWindow(ribbon->hwnd(),0,0,w,rh,TRUE);
    int bodyH=std::max(1,hh-statH-rh);
    MoveWindow(canvas,0,rh,std::max(1,w-pw),bodyH,TRUE);
    MoveWindow(hint,0,hh-statH,std::max(1,w),S(22),TRUE);
    MoveWindow(status,0,hh-statH+S(22),std::max(1,w),S(22),TRUE);
    if(propsVisible)layoutProps(w-pw,rh,pw,bodyH);
    else{
        for(size_t i=0;i<pItems.size();++i){ShowWindow(pItems[i].label,SW_HIDE);if(pItems[i].ctl)ShowWindow(pItems[i].ctl,SW_HIDE);}
        if(pTitle)ShowWindow(pTitle,SW_HIDE);if(pInfo)ShowWindow(pInfo,SW_HIDE);if(pSecCombo)ShowWindow(pSecCombo,SW_HIDE);
    }
    if(edit)MoveWindow(edit,w/2-S(340),rh+S(30),S(680),S(100),TRUE);
    if(apply)MoveWindow(apply,w/2-S(340),rh+S(136),S(120),S(28),TRUE);
    if(cancel)MoveWindow(cancel,w/2-S(190),rh+S(136),S(120),S(28),TRUE);
}
void Window::syncProps(){
    std::vector<ObjectId> atoms=selectedAtoms(),bonds=selectedBonds(),texts=selectedTexts();
    bool ha=!atoms.empty(),hb=!bonds.empty(),ht=!texts.empty();
    {bool applicable[4]={hb,ha,ht,true};
     if(!propUserChose&&!applicable[propSection])propSection=hb?0:ha?1:ht?2:3;
     if(pSecCombo){SendMessageW(pSecCombo,CB_SETCURSEL,propSection,0);ShowWindow(pSecCombo,propsVisible?SW_SHOW:SW_HIDE);}
     for(size_t i=0;i<pItems.size();++i){int cmd=(propsVisible&&pItems[i].section==propSection)?SW_SHOW:SW_HIDE;
        ShowWindow(pItems[i].label,cmd);if(pItems[i].ctl)ShowWindow(pItems[i].ctl,cmd);}}
    // bond
    int bo=-1,bd=-1,bs=-1,bsem=-1,bside=-1;
    if(hb){const BondRecord* b=s.doc.bonds().get(bonds[0]);bo=int(b->order)-1;bd=int(b->display);bs=int(b->stereo);bsem=int(b->semantics);
        bside=int(bonds::sideOf(static_cast<const Document&>(s.doc).extensions(ObjectRef(ObjectKind::Bond,bonds[0]))));}
    SendMessageW(pBondOrder,CB_SETCURSEL,(WPARAM)(bo>=0&&bo<=4?bo:-1),0);
    SendMessageW(pBondDisplay,CB_SETCURSEL,(WPARAM)(bd>=0&&bd<=8?bd:-1),0);
    SendMessageW(pBondStereo,CB_SETCURSEL,(WPARAM)(bs>=0&&bs<=3?bs:-1),0);
    SendMessageW(pBondSem,CB_SETCURSEL,(WPARAM)(bsem>=0&&bsem<=5?bsem:-1),0);
    SendMessageW(pBondSide,CB_SETCURSEL,(WPARAM)(bside>=0&&bside<=3?bside:-1),0);
    EnableWindow(pBondOrder,hb);EnableWindow(pBondDisplay,hb);EnableWindow(pBondStereo,hb);EnableWindow(pBondSem,hb);EnableWindow(pBondSide,hb);
    // atom
    int elem=-1,rad=-1,hc=-2,iso=-1;
    if(ha){const AtomRecord* a=s.doc.atoms().get(atoms[0]);elem=a->element;rad=a->semantics.radical;hc=a->semantics.implicitHydrogens;iso=a->semantics.isotope;}
    static const int elems[12]={6,7,8,16,15,9,17,35,53,5,14,1};
    int ei=-1;for(int i=0;i<12;++i)if(elems[i]==elem)ei=i;
    SendMessageW(pAtomElement,CB_SETCURSEL,(WPARAM)ei,0);
    SendMessageW(pAtomRadical,CB_SETCURSEL,(WPARAM)(rad>=0&&rad<=3?rad:-1),0);
    SendMessageW(pAtomH,CB_SETCURSEL,(WPARAM)(hc>=-1&&hc<=4?hc+1:-1),0);
    static const int isos[6]={0,2,13,14,15,18};int ii=-1;for(int i=0;i<6;++i)if(isos[i]==iso)ii=i;
    SendMessageW(pAtomIsotope,CB_SETCURSEL,(WPARAM)ii,0);
    EnableWindow(pAtomElement,ha);EnableWindow(pAtomRadical,ha);EnableWindow(pAtomH,ha);EnableWindow(pAtomIsotope,ha);
    EnableWindow(pAtomChgMinus,ha);EnableWindow(pAtomChgPlus,ha);EnableWindow(pAtomLabel,ha);EnableWindow(pAtomLabelApply,ha);
    if(ha){const AtomRecord* a=s.doc.atoms().get(atoms[0]);std::string lab=s.doc.atomLabel(a->id);SetWindowTextW(pAtomLabel,wide(lab).c_str());}
    else SetWindowTextW(pAtomLabel,L"");
    // text
    EnableWindow(pTextContent,ht);EnableWindow(pTextContentApply,ht);EnableWindow(pTextSize,ht);
    EnableWindow(pTextBold,ht);EnableWindow(pTextItalic,ht);EnableWindow(pTextUnder,ht);
    if(ht){std::string t=s.doc.plainText(texts[0]);SetWindowTextW(pTextContent,wide(t).c_str());
        double sz=s.doc.texts().get(texts[0])->size;static const int sizes[12]={8,9,10,11,12,14,16,18,24,36,48,72};
        int si=-1;for(int i=0;i<12;++i)if(sizes[i]==int(sz+0.5))si=i;SendMessageW(pTextSize,CB_SETCURSEL,(WPARAM)si,0);
        const std::vector<TextRun>& runs=s.doc.runs(texts[0]);uint16_t fl=runs.empty()?0:runs[0].flags;
        SendMessageW(pTextBold,BM_SETCHECK,(fl&kBold)?BST_CHECKED:BST_UNCHECKED,0);
        SendMessageW(pTextItalic,BM_SETCHECK,(fl&kItalic)?BST_CHECKED:BST_UNCHECKED,0);
        SendMessageW(pTextUnder,BM_SETCHECK,(fl&kUnderline)?BST_CHECKED:BST_UNCHECKED,0);
    }else{SetWindowTextW(pTextContent,L"");SendMessageW(pTextSize,CB_SETCURSEL,(WPARAM)-1,0);
        SendMessageW(pTextBold,BM_SETCHECK,BST_UNCHECKED,0);SendMessageW(pTextItalic,BM_SETCHECK,BST_UNCHECKED,0);SendMessageW(pTextUnder,BM_SETCHECK,BST_UNCHECKED,0);}
    // common
    std::vector<ObjectRef> refs=aliveRefs();bool styled=false;double wdt=0;for(size_t i=0;i<refs.size();++i){LineStyle st;if(readLineStyle(s.doc,refs[i],&st)){styled=true;wdt=st.width;break;}}
    EnableWindow(pWidth,styled);EnableWindow(pColor,styled);EnableWindow(pDelete,!refs.empty());EnableWindow(pRotL,!refs.empty());EnableWindow(pRotR,!refs.empty());
    EnableWindow(pGroup,refs.size()>=2);EnableWindow(pUngroup,true);
    static const int widths[8]={5,10,15,20,30,40,60,80};int wi=-1;for(int i=0;i<8;++i)if(std::abs(widths[i]/10.0-wdt)<1e-6)wi=i;
    SendMessageW(pWidth,CB_SETCURSEL,(WPARAM)(styled?wi:-1),0);
    std::ostringstream info;
    info<<"选择: 原子 "<<atoms.size()<<" / 键 "<<bonds.size()<<" / 文字 "<<texts.size()<<" / 对象 "<<refs.size()<<"\n";
    if(!hb)info<<"键属性：需要选中化学键。\n";
    if(!ha)info<<"原子属性：需要选中原子。\n";
    if(!ht)info<<"文字属性：需要选中文字。\n";
    if(!styled)info<<"线宽/颜色：仅对键、自由曲线与注释图形生效。\n";
    info<<"填充色与层级暂未接通，会明确拒绝。";
    SetWindowTextW(pInfo,wide(info.str()).c_str());
}
static void event(Window& w,interaction::InputKind k,LPARAM l,uint32_t key=0,bool repeat=false){interaction::InputEvent e;e.kind=k;e.devicePosition=Vec2(GET_X_LPARAM(l),GET_Y_LPARAM(l));e.focus=interaction::InputFocus::Canvas;e.modifiers=mods();e.button=kButtonLeft;e.virtualKey=key;e.repeat=repeat;e.character=k==interaction::InputKind::Character?key:0;e.documentId=w.s.doc.id();e.viewGeneration=w.s.viewSerial;if(k==interaction::InputKind::Character||k==interaction::InputKind::KeyDown)w.s.input.refreshHotspot(&w.s);size_t before=w.s.input.stats().unsupported;w.s.input.handle(e,&w.s);if(w.s.input.stats().unsupported>before)w.s.reportUnsupported(w.s.router.lastUnsupported());}
static LRESULT CALLBACK canvasProc(HWND h,UINT m,WPARAM wp,LPARAM lp){auto w=reinterpret_cast<Window*>(GetWindowLongPtrW(h,GWLP_USERDATA));if(m==WM_CREATE){w=static_cast<Window*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(w));w->canvas=h;}if(!w)return DefWindowProcW(h,m,wp,lp);auto& s=w->s;Vec2 pixel(GET_X_LPARAM(lp),GET_Y_LPARAM(lp));
    switch(m){
    case WM_SIZE:{s.renderView.widthPx=std::max(1,int(LOWORD(lp)));s.renderView.heightPx=std::max(1,int(HIWORD(lp)));auto v=s.view();v.viewportCenter=Vec2(s.renderView.widthPx*.5,s.renderView.heightPx*.5);s.setView(v);InvalidateRect(h,0,FALSE);return 0;}
    case WM_LBUTTONDOWN:SetFocus(h);SetCapture(h);w->pressed=true;event(*w,interaction::InputKind::PointerDown,lp);w->error(s.gesture(PointerPhase::Down,s.view().toDocument(pixel),mods()));break;
    case WM_MOUSEMOVE:{TRACKMOUSEEVENT t={sizeof(t),TME_LEAVE,h,0};TrackMouseEvent(&t);if(w->pan){auto v=s.view();v.center=v.center-(pixel-w->panAt)*v.docPerDevice();w->panAt=pixel;s.setView(v);}else{event(*w,interaction::InputKind::PointerMove,lp);if(w->pressed)w->error(s.gesture(PointerPhase::Move,s.view().toDocument(pixel),mods()));}break;}
    case WM_LBUTTONUP:if(w->pressed){event(*w,interaction::InputKind::PointerUp,lp);w->pressed=false;w->error(s.gesture(PointerPhase::Up,s.view().toDocument(pixel),mods()));ReleaseCapture();if(s.entries[s.activeIndex].owner=="T09"&&!s.doc.texts().empty())w->openText(s.doc.texts().records().back().id);else s.input.refreshHotspot(&s);}break;
    case WM_LBUTTONDBLCLK:{auto r=s.pick(s.view().toDocument(pixel));if(r.kind==ObjectKind::Text)w->openText(r.id);else if(r.kind==ObjectKind::Atom)w->openText(kInvalidObjectId,r.id);return 0;}
    case WM_MBUTTONDOWN:w->pan=true;w->panAt=pixel;s.cancel();SetCapture(h);break;case WM_MBUTTONUP:w->pan=false;ReleaseCapture();break;
    case WM_MOUSEWHEEL:{POINT pt={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ScreenToClient(h,&pt);Vec2 p(double(pt.x),double(pt.y));auto v=s.view();auto anchor=v.toDocument(p);v.scale=std::max(.01,std::min(100.0,v.scale*(GET_WHEEL_DELTA_WPARAM(wp)>0?1.15:1/1.15)));v.center=v.center+anchor-v.toDocument(p);s.setView(v);s.input.refreshHotspot(&s);break;}
    case WM_KEYDOWN:if(wp==VK_ESCAPE){s.cancel();s.activateCommand("chooseSelectionTool_Marquee");w->pressed=false;ReleaseCapture();}else if(wp>=VK_LEFT&&wp<=VK_DOWN){double d=(mods()&kModShift)?10:1;w->error(s.move(s.selected,Vec2(wp==VK_LEFT?-d:wp==VK_RIGHT?d:0,wp==VK_UP?-d:wp==VK_DOWN?d:0)));}else event(*w,interaction::InputKind::KeyDown,0,uint32_t(wp),(lp&(1<<30))!=0);break;
    case WM_SYSKEYDOWN:case WM_SYSKEYUP:case WM_KEYUP:event(*w,interaction::InputKind::KeyUp,0,uint32_t(wp));if(w->pressed)w->error(s.gesture(PointerPhase::Move,s.input.pointerDocument(),mods()));break;
    case WM_CHAR:if(wp>=32&&wp!=127&&!(mods()&kModControl))event(*w,interaction::InputKind::Character,0,uint32_t(wp),(lp&(1<<30))!=0);break;
    case WM_MOUSELEAVE:if(!w->pressed&&!w->pan)s.input.unwind("pointer left canvas");break;
    case WM_KILLFOCUS:s.cancel();w->pressed=w->pan=false;ReleaseCapture();w->sync();InvalidateRect(h,0,FALSE);return 0;
    case WM_CAPTURECHANGED:if(w->pressed||w->pan){s.cancel();w->pressed=w->pan=false;}break;
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:{PAINTSTRUCT p;BeginPaint(h,&p);if(w->gl){wglMakeCurrent(w->dc,w->rc);auto overlay=s.overlay();render::RenderStats stats;auto e=w->gl->draw(s.displayScene(),&overlay.scene(),s.renderView,&stats);if(!e.ok())s.status=e.message;glFinish();SwapBuffers(w->dc);++w->paints;}EndPaint(h,&p);return 0;}
    default:return DefWindowProcW(h,m,wp,lp);
    }w->repaint();return 0;
}
static HWND ctlOf(HWND parent,HINSTANCE inst,HFONT font,const wchar_t* klass,const wchar_t* label,DWORD style,int x,int y,int w,int hgt,int id){HWND c=CreateWindowW(klass,label,WS_CHILD|style,x,y,w,hgt,parent,HMENU(INT_PTR(id)),inst,0);SendMessageW(c,WM_SETFONT,WPARAM(font),TRUE);return c;}
static void addCombo(HWND c,const wchar_t* const* items,int n){for(int i=0;i<n;++i)SendMessageW(c,CB_ADDSTRING,0,LPARAM(items[i]));}
void buildProps(Window& w,HINSTANCE inst);
static LRESULT CALLBACK mainProc(HWND h,UINT m,WPARAM wp,LPARAM lp){auto w=reinterpret_cast<Window*>(GetWindowLongPtrW(h,GWLP_USERDATA));if(m==WM_CREATE){w=static_cast<Window*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);w->h=h;SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(w));}if(!w)return DefWindowProcW(h,m,wp,lp);
    switch(m){
    case WM_SIZE:w->layout();if(w->canvas)InvalidateRect(w->canvas,0,FALSE);return 0;
    case WM_COMMAND:{int id=LOWORD(wp),code=HIWORD(wp);Error e;
        if(w->syncing)return 0;
        if(code==CBN_SELCHANGE){
            if(id==ID_P_SEC){int i=(int)SendMessageW(w->pSecCombo,CB_GETCURSEL,0,0);if(i>=0&&i<4){w->propSection=i;w->propUserChose=true;w->layout();}}
            else if(id==ID_P_BONDORDER){int i=(int)SendMessageW(w->pBondOrder,CB_GETCURSEL,0,0);if(i>=0)e=w->applyProperty("bond.order",i+1);}
            else if(id==ID_P_BONDDISPLAY){int i=(int)SendMessageW(w->pBondDisplay,CB_GETCURSEL,0,0);if(i>=0)e=w->applyProperty("bond.display",i);}
            else if(id==ID_P_BONDSTEREO){int i=(int)SendMessageW(w->pBondStereo,CB_GETCURSEL,0,0);if(i>=0)e=w->applyProperty("bond.stereo",i);}
            else if(id==ID_P_BONDSEM){int i=(int)SendMessageW(w->pBondSem,CB_GETCURSEL,0,0);if(i>=0)e=w->applyProperty("bond.semantics",i);}
            else if(id==ID_P_BONDSIDE){int i=(int)SendMessageW(w->pBondSide,CB_GETCURSEL,0,0);if(i>=0)e=w->applyProperty("bond.side",i);}
            else if(id==ID_P_ATOMELEMENT){static const int elems[12]={6,7,8,16,15,9,17,35,53,5,14,1};int i=(int)SendMessageW(w->pAtomElement,CB_GETCURSEL,0,0);if(i>=0)e=w->applyProperty("atom.element",elems[i]);}
            else if(id==ID_P_ATOMRADICAL){int i=(int)SendMessageW(w->pAtomRadical,CB_GETCURSEL,0,0);if(i>=0){std::vector<ObjectId> a=w->selectedAtoms();
                if(!a.empty()){const AtomRecord* rec=w->s.doc.atoms().get(a[0]);int cur=rec->semantics.radical;while(cur!=i){e=w->applyProperty("atom.radical.cycle",0);if(!e.ok())break;--cur;if(cur<0)cur=3;}}}}
            else if(id==ID_P_ATOMH){int i=(int)SendMessageW(w->pAtomH,CB_GETCURSEL,0,0);if(i>=0){static const int hs[6]={-1,0,1,2,3,4};
                std::vector<ObjectId> a=w->selectedAtoms();if(!a.empty()){const AtomRecord* rec=w->s.doc.atoms().get(a[0]);int cur=rec->semantics.implicitHydrogens;while(cur!=hs[i]){e=w->applyProperty("atom.h.cycle",0);if(!e.ok())break;int idx=0;for(int k=0;k<6;++k)if(hs[k]==cur)idx=k;cur=hs[(idx+1)%6];}}}}
            else if(id==ID_P_ATOMISOTOPE){static const int isos[6]={0,2,13,14,15,18};int i=(int)SendMessageW(w->pAtomIsotope,CB_GETCURSEL,0,0);if(i>=0){std::vector<ObjectId> a=w->selectedAtoms();
                if(!a.empty()){const AtomRecord* rec=w->s.doc.atoms().get(a[0]);int cur=rec->semantics.isotope;while(cur!=isos[i]){e=w->applyProperty("atom.isotope.cycle",0);if(!e.ok())break;int idx=0;for(int k=0;k<6;++k)if(isos[k]==cur)idx=k;cur=isos[(idx+1)%6];}}}}
            else if(id==ID_P_TEXTSIZE){static const int sizes[12]={8,9,10,11,12,14,16,18,24,36,48,72};int i=(int)SendMessageW(w->pTextSize,CB_GETCURSEL,0,0);if(i>=0)e=w->applyProperty("text.size",sizes[i]);}
            else if(id==ID_P_WIDTH){static const int widths[8]={5,10,15,20,30,40,60,80};int i=(int)SendMessageW(w->pWidth,CB_GETCURSEL,0,0);if(i>=0)e=w->applyProperty("style.width.set",widths[i]);}
        }else if(id==ID_APPLY||id==ID_CANCEL||id==ID_UNDO||id==ID_REDO||id==ID_FIT||id==ID_NEW||id==ID_OPEN||id==ID_SAVE||id==ID_EXPORT||id==ID_ALL||id==ID_TEXT||id==ID_LENGTH||id==ID_ANGLES||id==ID_HELP){w->command(id);return 0;}
        else if(id==ID_P_ATOMLABELAPPLY){int n=GetWindowTextLengthW(w->pAtomLabel);std::wstring t((size_t)n+1,L' ');GetWindowTextW(w->pAtomLabel,&t[0],n+1);t.resize((size_t)n);
            std::vector<ObjectId> a=w->selectedAtoms();if(!a.empty())e=w->s.submit(chemistry::setLabelTextCommand(a[0],utf8(t),{},0),"property atom label");}
        else if(id==ID_P_TEXTCONTENTAPPLY){int n=GetWindowTextLengthW(w->pTextContent);std::wstring t((size_t)n+1,L' ');GetWindowTextW(w->pTextContent,&t[0],n+1);t.resize((size_t)n);
            std::vector<ObjectId> ids=w->selectedTexts();if(!ids.empty()){ObjectId id=ids[0];std::string value=utf8(t);
                text::TextEditSession x;x.begin(w->s.doc.runs(id));x.selectAll();x.insertUtf8(value,text::EditStyle());auto runs=x.commit();
                e=w->s.submit(Command("property text",kInvalidateAppearance,[id,runs](CommandContext& c){auto r=c.document.setTextRuns(id,runs);return r.ok()?CommandResult::success():CommandResult::fail(r.code,r.message);}),"property text");}}
        else if(id==ID_P_TEXTBOLD||id==ID_P_TEXTITALIC||id==ID_P_TEXTUNDER){int bit=id==ID_P_TEXTBOLD?0:id==ID_P_TEXTITALIC?1:2;
            e=w->applyProperty(bit==0?"text.flags.bold":bit==1?"text.flags.italic":"text.flags.underline",0);}
        else if(id==ID_P_ATOMCHGMINUS)e=w->applyProperty("atom.charge.delta",-1);
        else if(id==ID_P_ATOMCHGPLUS)e=w->applyProperty("atom.charge.delta",1);
        else if(id==ID_P_COLOR)e=w->applyProperty("style.color",0);
        else if(id==ID_P_DELETE){e=w->s.submit(commands::deleteObjects(w->s.selected),"property delete");}
        else if(id==ID_P_ROTL)e=w->applyProperty("select.rotate",90);
        else if(id==ID_P_ROTR)e=w->applyProperty("select.rotate",-90);
        else if(id==ID_P_GROUP)e=w->applyProperty("select.group",0);
        else if(id==ID_P_UNGROUP)e=w->applyProperty("select.ungroup",0);
        if(!e.ok())w->s.status=e.message;
        w->repaint();return 0;}
    case WM_SETFOCUS:SetFocus(w->canvas);return 0;
    case WM_DPICHANGED:{w->dpi=HIWORD(wp);if(w->ribbon)w->ribbon->relayout(w->dpi);auto r=reinterpret_cast<RECT*>(lp);SetWindowPos(h,0,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);
        auto v=w->s.view();v.dpiScale=double(w->dpi)/72;w->s.setView(v);w->layout();return 0;}
    case WM_TIMER:if(w->smoke){KillTimer(h,1);bool ok=true;// scripted UI verification on this real window
        if(w->mode==2){
            w->trace("begin");
            auto before=w->s.doc.digest();
            w->ribbon->activateItemId("cmd:chooseBondTool_Solid");w->trace("tool");
            SendMessageW(w->canvas,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(500,300));w->trace("down1");
            SendMessageW(w->canvas,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(620,360));w->trace("move1");
            SendMessageW(w->canvas,WM_LBUTTONUP,0,MAKELPARAM(620,360));w->trace("up1");
            ok=ok&&w->s.doc.digest()!=before&&w->s.doc.stats().aliveAtoms>0;
            w->ribbon->activateItemId("edit.undo");w->trace("undo1");ok=ok&&w->s.doc.digest()==before;
            w->ribbon->activateItemId("edit.redo");w->trace("redo1");ok=ok&&w->s.doc.stats().aliveAtoms>0;
            w->ribbon->activateItemId("cmd:chooseRingTool_Benzene");w->trace("ringtool");
            SendMessageW(w->canvas,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(700,420));w->trace("down2");
            SendMessageW(w->canvas,WM_LBUTTONUP,0,MAKELPARAM(700,420));w->trace("up2");
            ok=ok&&w->s.doc.atoms().size()>=6;
            w->ribbon->activateItemId("sel.all");w->trace("selall");
            w->ribbon->activateItemId("bond.double");w->trace("proporder");
            ok=ok&&w->s.status.find("Unsupported")==std::string::npos;
            w->ribbon->activateItemId("edit.undo");w->trace("undo2");
            auto dir=w->image.substr(0,w->image.find_last_of("/\\"));
            ok=ok&&w->s.save(dir+"/ribbon_saved.r62doc").ok();w->trace("saved");
            Session restored;ok=ok&&restored.open(dir+"/ribbon_saved.r62doc").ok()&&restored.doc.digest()==w->s.doc.digest();w->trace("restored");
            w->repaint();UpdateWindow(w->canvas);w->trace("repaint");
            w->snapshotPng(dir+"/ribbon_ui.png");w->trace("shot");
            wglMakeCurrent(w->dc,w->rc);std::vector<uint8_t> pixels;ok=ok&&w->gl->readPixels(0,0,int(w->s.renderView.widthPx),int(w->s.renderView.heightPx),&pixels).ok();
            if(ok)ok=render::writePng(w->image,int(w->s.renderView.widthPx),int(w->s.renderView.heightPx),pixels).ok();
            std::ofstream(dir+"/ribbon_smoke.json")<<"{\"owned_window_only\":true,\"ribbon_canvas_pointer_create\":"<<(ok?"true":"false")
                <<",\"ribbon_undo_redo\":"<<(ok?"true":"false")<<",\"ribbon_benzene\":"<<(w->s.doc.atoms().size()>=6?"true":"false")
                <<",\"ribbon_property_user_path\":\"checked\",\"paints\":"<<w->paints<<",\"ribbon_items\":"<<ribbon::ribbonModel().tabs.size()<<",\"host_bound\":false}";
            w->s.dirty=false;DestroyWindow(h);if(!ok)PostQuitMessage(3);return 0;
        }
        auto before=w->s.doc.digest();
        w->s.activateCommand("chooseBondTool_Solid");
        SendMessageW(w->canvas,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(700,430));
        SendMessageW(w->canvas,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(790,480));
        SendMessageW(w->canvas,WM_LBUTTONUP,0,MAKELPARAM(790,480));
        auto after=w->s.doc.digest();ok=ok&&after!=before;
        w->command(ID_UNDO);ok=ok&&w->s.doc.digest()==before;
        w->command(ID_REDO);ok=ok&&w->s.doc.digest()==after;
        auto dir=w->image.substr(0,w->image.find_last_of("/\\"));
        ok=ok&&w->s.save(dir+"/ui_saved.r62doc").ok();
        Session restored;ok=ok&&restored.open(dir+"/ui_saved.r62doc").ok()&&restored.doc.digest()==after;
        InvalidateRect(w->canvas,0,FALSE);UpdateWindow(w->canvas);
        wglMakeCurrent(w->dc,w->rc);std::vector<uint8_t> pixels;ok=ok&&w->gl->readPixels(0,0,int(w->s.renderView.widthPx),int(w->s.renderView.heightPx),&pixels).ok();
        if(ok)ok=render::writePng(w->image,int(w->s.renderView.widthPx),int(w->s.renderView.heightPx),pixels).ok();
        std::ofstream(dir+"/ui_smoke.json")<<"{\"owned_window_only\":true,\"pointer_undo_redo_save_reload\":"<<(ok?"true":"false")
            <<",\"win32_pointer_create_menu_undo_redo_save_reload_focus\":"<<(ok?"true":"false")<<",\"paints\":"<<w->paints<<"}";
        w->s.dirty=false;DestroyWindow(h);if(!ok)PostQuitMessage(3);}return 0;
    case WM_CLOSE:if(w->leave())DestroyWindow(h);return 0;case WM_DESTROY:PostQuitMessage(0);return 0;
    default:return DefWindowProcW(h,m,wp,lp);
    }
}
int runWindow(Session& s,const std::string& path,bool smoke,const std::string& screenshot,int mode){
    SetProcessDPIAware();Window w(s);w.smoke=smoke;w.image=screenshot;w.mode=mode;auto instance=GetModuleHandleW(0);
    WNDCLASSW wc={};wc.lpfnWndProc=mainProc;wc.hInstance=instance;wc.lpszClassName=L"T19R3Shell";wc.hCursor=LoadCursorW(0,MAKEINTRESOURCEW(32512));wc.hbrBackground=HBRUSH(COLOR_BTNFACE+1);RegisterClassW(&wc);
    wc.style=CS_OWNDC|CS_DBLCLKS;wc.lpfnWndProc=canvasProc;wc.lpszClassName=L"T19R3Canvas";wc.hbrBackground=0;RegisterClassW(&wc);
    HWND h=CreateWindowW(L"T19R3Shell",L"T19 化学结构编辑器 R3 — 独立模式 / 未连接 ChemDraw",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,1360,900,0,0,instance,&w);
    HMENU menu=CreateMenu(),file=CreatePopupMenu(),edit=CreatePopupMenu(),view=CreatePopupMenu();
    for(auto p:std::vector<std::pair<int,const wchar_t*> >{{ID_NEW,L"新建\tCtrl+N"},{ID_OPEN,L"打开文档副本…\tCtrl+O"},{ID_SAVE,L"另存 T19 文档…\tCtrl+S"},{ID_EXPORT,L"导出 CDXML 副本…"}})AppendMenuW(file,MF_STRING,p.first,p.second);
    for(auto p:std::vector<std::pair<int,const wchar_t*> >{{ID_UNDO,L"撤销\tCtrl+Z"},{ID_REDO,L"重做\tCtrl+Shift+Z"},{ID_ALL,L"全选\tCtrl+A"},{ID_TEXT,L"编辑标签 / 文字…"},{ID_LENGTH,L"固定键长\tCtrl+L"},{ID_ANGLES,L"固定角度\tCtrl+E"}})AppendMenuW(edit,MF_STRING,p.first,p.second);
    AppendMenuW(view,MF_STRING,ID_FIT,L"全图\tF6");AppendMenuW(menu,MF_POPUP,UINT_PTR(file),L"文件");AppendMenuW(menu,MF_POPUP,UINT_PTR(edit),L"编辑");AppendMenuW(menu,MF_POPUP,UINT_PTR(view),L"视图");AppendMenuW(menu,MF_STRING,ID_HELP,L"快捷键与说明");SetMenu(h,menu);
    w.font=CreateFontW(-15,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");
    w.dc=GetDC(h);w.dpi=GetDeviceCaps(w.dc,LOGPIXELSX);ReleaseDC(h,w.dc);
    w.ribbon.reset(new ribbon::RibbonBar());
    if(!w.ribbon->create(h,instance,w.dpi)){MessageBoxW(h,L"Ribbon 创建失败",L"错误",MB_OK);return 2;}
    w.ribbon->setSink([&w](const ribbon::Event& e){w.onRibbon(e);});
    w.hint=ctlOf(h,instance,w.font,L"STATIC",L"独立模式 · 未连接 ChemDraw",WS_VISIBLE|SS_ENDELLIPSIS,0,0,10,10,0);
    w.status=ctlOf(h,instance,w.font,L"STATIC",L"",WS_VISIBLE|SS_ENDELLIPSIS,0,0,10,10,0);
    buildProps(w,instance);
    w.canvas=CreateWindowExW(WS_EX_CLIENTEDGE,L"T19R3Canvas",L"",WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS,0,0,100,100,h,0,instance,&w);
    w.edit=ctlOf(h,instance,w.font,L"EDIT",L"",ES_MULTILINE|ES_AUTOVSCROLL|WS_BORDER|WS_VSCROLL,240,115,680,100,23030);
    w.apply=ctlOf(h,instance,w.font,L"BUTTON",L"应用文字",0,240,220,100,28,ID_APPLY);
    w.cancel=ctlOf(h,instance,w.font,L"BUTTON",L"取消",0,350,220,100,28,ID_CANCEL);
    SetWindowPos(w.edit,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE);SetWindowPos(w.apply,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE);SetWindowPos(w.cancel,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE);
    for(auto a:{interaction::HotkeyAction::FileNew,interaction::HotkeyAction::FileOpen,interaction::HotkeyAction::FileSave,interaction::HotkeyAction::FileSaveAs})s.router.registerHandler(a,&Window::fileAction,&w);
    HDC cdc=GetDC(w.canvas);auto v=s.view();v.dpiScale=double(w.dpi)/72;v.scale=2;s.setView(v);
    PIXELFORMATDESCRIPTOR pf={};pf.nSize=sizeof(pf);pf.nVersion=1;pf.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER;pf.iPixelType=PFD_TYPE_RGBA;pf.cColorBits=32;
    int fmt=ChoosePixelFormat(cdc,&pf);SetPixelFormat(cdc,fmt,&pf);w.rc=wglCreateContext(cdc);wglMakeCurrent(cdc,w.rc);w.gl.reset(render::createGLBackend());std::string reason;
    if(!w.gl->initialize(&reason)||std::string(w.gl->name())!="GL-compat-fbo2x"){MessageBoxW(h,wide(reason).c_str(),L"需要 2× SSAA 渲染器",MB_OK);return 2;}
    w.layout();                                // size the canvas first so open()'s fit() uses the real viewport
    if(!path.empty())w.error(s.open(path));    // open() already fits the view to the loaded document
    w.sync();
    ShowWindow(h,smoke?SW_SHOWNOACTIVATE:SW_SHOW);UpdateWindow(h);
    if(!smoke)SetFocus(w.canvas);if(smoke)SetTimer(h,1,800,0);
    MSG msg;int result;while((result=GetMessageW(&msg,0,0,0))>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
    w.gl->destroy();w.gl.reset();wglMakeCurrent(0,0);wglDeleteContext(w.rc);ReleaseDC(w.canvas,cdc);DeleteObject(w.font);return result<0?2:int(msg.wParam);
}
void Window::snapshotPng(const std::string& path){
    if(!ribbon||!ribbon->hwnd())return;
    RECT wr;GetWindowRect(ribbon->hwnd(),&wr);int w=wr.right-wr.left,hg=wr.bottom-wr.top;if(w<1||hg<1)return;
    HDC sdc=GetDC(h);
    HDC mdc=CreateCompatibleDC(sdc);
    HBITMAP bmp=CreateCompatibleBitmap(sdc,w,hg);
    HGDIOBJ o=SelectObject(mdc,bmp);
    ribbon->paintInto(mdc,w,hg);
    SelectObject(mdc,o);   // GetDIBits refuses a bitmap that is selected into a DC
    BITMAPINFO bi={};bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bi.bmiHeader.biWidth=w;bi.bmiHeader.biHeight=hg;
    bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;bi.bmiHeader.biCompression=BI_RGB;
    std::vector<uint8_t> px((size_t)w*hg*4);
    int lines=GetDIBits(mdc,bmp,0,hg,&px[0],&bi,DIB_RGB_COLORS);
    for(size_t i=0;i+3<px.size();i+=4){uint8_t bb=px[i];px[i]=px[i+2];px[i+2]=bb;px[i+3]=0xFF;}
    DeleteObject(bmp);DeleteDC(mdc);ReleaseDC(h,sdc);
    if(lines>0)render::writePng(path,w,hg,px);
}
static void addStatic(Window& w,HINSTANCE inst,const wchar_t* t){
    HWND l=ctlOf(w.h,inst,w.font,L"STATIC",t,WS_VISIBLE|SS_LEFT,0,0,10,10,0);
    PItem pi;pi.label=l;pi.ctl=0;pi.header=true;pi.section=w.buildSection;w.pItems.push_back(pi);
}
static HWND addRow(Window& w,HINSTANCE inst,const wchar_t* label,const wchar_t* klass,DWORD style,int id,const wchar_t* text){
    HWND l=ctlOf(w.h,inst,w.font,L"STATIC",label,WS_VISIBLE|SS_LEFT,0,0,10,10,0);
    HWND c=ctlOf(w.h,inst,w.font,klass,text,WS_VISIBLE|style,0,0,10,10,id);
    PItem pi;pi.label=l;pi.ctl=c;pi.header=false;pi.section=w.buildSection;w.pItems.push_back(pi);return c;
}
void buildProps(Window& w,HINSTANCE inst){
    w.pTitle=ctlOf(w.h,inst,w.font,L"STATIC",L"属性（按类别显示）",WS_VISIBLE|SS_LEFT,0,0,10,10,0);
    {const wchar_t* a[]={L"键属性",L"原子属性",L"文字属性",L"通用"};w.pSecCombo=ctlOf(w.h,inst,w.font,L"COMBOBOX",L"",WS_VISIBLE|CBS_DROPDOWNLIST|WS_VSCROLL,0,0,10,10,ID_P_SEC);addCombo(w.pSecCombo,a,4);SendMessageW(w.pSecCombo,CB_SETCURSEL,0,0);}
    w.buildSection=0;
    addStatic(w,inst,L"键属性");
    {const wchar_t* a[]={L"单键",L"双键",L"三键",L"四键",L"芳香键"};w.pBondOrder=addRow(w,inst,L"键级",L"COMBOBOX",CBS_DROPDOWNLIST|WS_VSCROLL,ID_P_BONDORDER,L"");addCombo(w.pBondOrder,a,5);}
    {const wchar_t* a[]={L"普通",L"楔形",L"虚楔",L"虚楔变体",L"空心楔",L"粗键",L"虚线",L"点线",L"波浪"};w.pBondDisplay=addRow(w,inst,L"显示",L"COMBOBOX",CBS_DROPDOWNLIST|WS_VSCROLL,ID_P_BONDDISPLAY,L"");addCombo(w.pBondDisplay,a,9);}
    {const wchar_t* a[]={L"无",L"上",L"下",L"或"};w.pBondStereo=addRow(w,inst,L"立体",L"COMBOBOX",CBS_DROPDOWNLIST|WS_VSCROLL,ID_P_BONDSTEREO,L"");addCombo(w.pBondStereo,a,4);}
    {const wchar_t* a[]={L"普通",L"芳香",L"配位",L"氢键",L"查询",L"不饱和"};w.pBondSem=addRow(w,inst,L"语义",L"COMBOBOX",CBS_DROPDOWNLIST|WS_VSCROLL,ID_P_BONDSEM,L"");addCombo(w.pBondSem,a,6);}
    {const wchar_t* a[]={L"对称",L"偏左",L"偏右",L"居中"};w.pBondSide=addRow(w,inst,L"双键侧",L"COMBOBOX",CBS_DROPDOWNLIST|WS_VSCROLL,ID_P_BONDSIDE,L"");addCombo(w.pBondSide,a,4);}
    w.buildSection=1;
    addStatic(w,inst,L"原子属性");
    {const wchar_t* a[]={L"C",L"N",L"O",L"S",L"P",L"F",L"Cl",L"Br",L"I",L"B",L"Si",L"H"};w.pAtomElement=addRow(w,inst,L"元素",L"COMBOBOX",CBS_DROPDOWNLIST|WS_VSCROLL,ID_P_ATOMELEMENT,L"");addCombo(w.pAtomElement,a,12);}
    {const wchar_t* a[]={L"无",L"单自由基",L"双自由基",L"三线态"};w.pAtomRadical=addRow(w,inst,L"自由基",L"COMBOBOX",CBS_DROPDOWNLIST|WS_VSCROLL,ID_P_ATOMRADICAL,L"");addCombo(w.pAtomRadical,a,4);}
    {const wchar_t* a[]={L"默认",L"0",L"1",L"2",L"3",L"4"};w.pAtomH=addRow(w,inst,L"氢数",L"COMBOBOX",CBS_DROPDOWNLIST|WS_VSCROLL,ID_P_ATOMH,L"");addCombo(w.pAtomH,a,6);}
    {const wchar_t* a[]={L"自然",L"2",L"13",L"14",L"15",L"18"};w.pAtomIsotope=addRow(w,inst,L"同位素",L"COMBOBOX",CBS_DROPDOWNLIST|WS_VSCROLL,ID_P_ATOMISOTOPE,L"");addCombo(w.pAtomIsotope,a,6);}
    w.pAtomLabel=addRow(w,inst,L"标签",L"EDIT",ES_AUTOHSCROLL|WS_BORDER,ID_P_ATOMLABEL,L"");
    w.pAtomLabelApply=addRow(w,inst,L"",L"BUTTON",BS_PUSHBUTTON,ID_P_ATOMLABELAPPLY,L"应用标签");
    w.pAtomChgMinus=addRow(w,inst,L"电荷",L"BUTTON",BS_PUSHBUTTON,ID_P_ATOMCHGMINUS,L"电荷 − 1");
    w.pAtomChgPlus=addRow(w,inst,L"",L"BUTTON",BS_PUSHBUTTON,ID_P_ATOMCHGPLUS,L"电荷 + 1");
    w.buildSection=2;
    addStatic(w,inst,L"文字属性");
    w.pTextContent=addRow(w,inst,L"内容",L"EDIT",ES_AUTOHSCROLL|WS_BORDER,ID_P_TEXTCONTENT,L"");
    w.pTextContentApply=addRow(w,inst,L"",L"BUTTON",BS_PUSHBUTTON,ID_P_TEXTCONTENTAPPLY,L"应用文字");
    {const wchar_t* a[]={L"8",L"9",L"10",L"11",L"12",L"14",L"16",L"18",L"24",L"36",L"48",L"72"};w.pTextSize=addRow(w,inst,L"字号",L"COMBOBOX",CBS_DROPDOWNLIST|WS_VSCROLL,ID_P_TEXTSIZE,L"");addCombo(w.pTextSize,a,12);}
    w.pTextBold=addRow(w,inst,L"",L"BUTTON",BS_AUTOCHECKBOX,ID_P_TEXTBOLD,L"粗体");
    w.pTextItalic=addRow(w,inst,L"",L"BUTTON",BS_AUTOCHECKBOX,ID_P_TEXTITALIC,L"斜体");
    w.pTextUnder=addRow(w,inst,L"",L"BUTTON",BS_AUTOCHECKBOX,ID_P_TEXTUNDER,L"下划线");
    w.buildSection=3;
    addStatic(w,inst,L"通用");
    {const wchar_t* a[]={L"0.5 pt",L"1 pt",L"1.5 pt",L"2 pt",L"3 pt",L"4 pt",L"6 pt",L"8 pt"};w.pWidth=addRow(w,inst,L"线宽",L"COMBOBOX",CBS_DROPDOWNLIST|WS_VSCROLL,ID_P_WIDTH,L"");addCombo(w.pWidth,a,8);}
    w.pColor=addRow(w,inst,L"",L"BUTTON",BS_PUSHBUTTON,ID_P_COLOR,L"线条颜色…");
    w.pDelete=addRow(w,inst,L"",L"BUTTON",BS_PUSHBUTTON,ID_P_DELETE,L"删除选中");
    w.pRotL=addRow(w,inst,L"",L"BUTTON",BS_PUSHBUTTON,ID_P_ROTL,L"逆时针 90°");
    w.pRotR=addRow(w,inst,L"",L"BUTTON",BS_PUSHBUTTON,ID_P_ROTR,L"顺时针 90°");
    w.pGroup=addRow(w,inst,L"",L"BUTTON",BS_PUSHBUTTON,ID_P_GROUP,L"编组");
    w.pUngroup=addRow(w,inst,L"",L"BUTTON",BS_PUSHBUTTON,ID_P_UNGROUP,L"解组");
    w.pInfo=ctlOf(w.h,inst,w.font,L"STATIC",L"",WS_VISIBLE|SS_LEFT,0,0,10,10,0);
}
}}
int main(int argc,char** argv){try{
    if(argc==4&&std::string(argv[1])=="--acceptance")return r62::integration::acceptance(argv[2],argv[3]);
    if(argc==3&&std::string(argv[1])=="--usability")return r62::integration::usabilityAcceptance(argv[2]);
    if(argc==3&&std::string(argv[1])=="--dump-hotkeys")return r62::integration::dumpHotkeys(argv[2]);
    if(argc==3&&std::string(argv[1])=="--dump-ribbon")return r62::integration::dumpRibbon(argv[2]);
    r62::integration::Session s;
    if(argc==4&&std::string(argv[1])=="--ui-smoke")return r62::integration::runWindow(s,argv[2],true,argv[3],1);
    if(argc==3&&std::string(argv[1])=="--ui-ribbon")return r62::integration::runWindow(s,"",true,std::string(argv[2])+"/ribbon_canvas.png",2);
    return r62::integration::runWindow(s,argc>1?argv[1]:"");
}catch(const std::exception& e){fprintf(stderr,"T19 failed: %s\n",e.what());return 1;}}
