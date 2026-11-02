#include "engine/cdxml_id_repair.hpp"
#include "rotation_glyph.hpp"
#ifndef STUDIO_INTEROP_HPP
#define STUDIO_INTEROP_HPP
#include "next/integration/session.hpp"
#include "engine/table_layout.hpp"
#include "engine/atom_runs.hpp"
#include "third_party/tinyxml2/tinyxml2.h"
#include <windows.h>
#include <functional>
#include "cdx_binary.hpp"
#include "page_defaults.hpp"
namespace studioio {
using namespace r62; using namespace core;
struct Page {
    double width=595.2756,height=841.8898,x=-297.6378,y=-420.9449; int across=1,down=1;
    // A document without its own studio.page (a fresh document) follows the
    // default chosen in 设置 · 页面与排版; the sheet stays centred on the origin.
    static Page get(const Document& d){
        Page p;auto v=d.documentExtensions().findValue("studio.page");
        if(v){std::istringstream in(*v);in>>p.width>>p.height>>p.across>>p.down;in>>p.x>>p.y;}
        else{const auto& defaults=pagedefaults::current();if(defaults.enabled){p.width=defaults.width;p.height=defaults.height;p.across=defaults.across;p.down=defaults.down;p.x=-p.width*.5;p.y=-p.height*.5;}}
        return p;
    }
    bool valid()const{return std::isfinite(width)&&std::isfinite(height)&&width>=72&&height>=72&&width<=14400&&height<=14400&&across>=1&&across<=20&&down>=1&&down<=20;}
    void store(Document& d)const{if(d.session())d.session()->noteExtensions(ObjectRef(),true);std::ostringstream o;o.precision(12);o<<width<<' '<<height<<' '<<across<<' '<<down<<' '<<x<<' '<<y;d.documentExtensions().set("studio.page",o.str());}
};
#include "arrow_interop.hpp"
inline void readPage(const std::string& bytes,Document& d){
    tinyxml2::XMLDocument x;if(x.Parse(bytes.c_str())!=tinyxml2::XML_SUCCESS)return;
    auto root=x.RootElement();auto e=root?root->FirstChildElement("page"):nullptr;if(!e)return;
    Page p;e->QueryIntAttribute("WidthPages",&p.across);e->QueryIntAttribute("HeightPages",&p.down);
    const char* bb=e->Attribute("BoundingBox");double a,b,c,f;if(bb){std::istringstream in(bb);if(in>>a>>b>>c>>f){p.width=(c-a)/std::max(1,p.across);p.height=(f-b)/std::max(1,p.down);p.x=a;p.y=b;}}
    if(p.valid())p.store(d);
}
inline Error exportXml(const Document& d,std::string* bytes){
    std::ostringstream out;auto e=persistence::writeCdxml(d,out);if(!e.ok())return e;
    tinyxml2::XMLDocument xml;if(xml.Parse(out.str().c_str())!=tinyxml2::XML_SUCCESS)return Error(ErrorCode::ParseError,std::string("无法生成 CDXML: ")+xml.ErrorStr());
    auto root=xml.RootElement();if(!root)return Error(ErrorCode::ParseError,"CDXML 根节点缺失");
    // Font attributes are table IDs in CDXML, never font family names.
    auto ft=root->FirstChildElement("fonttable");int font=3;
    if(ft&&ft->FirstChildElement("font"))font=ft->FirstChildElement("font")->IntAttribute("id",3);
    if(root->IntAttribute("CaptionFont",0)<=0)root->SetAttribute("CaptionFont",font);if(root->IntAttribute("LabelFont",0)<=0)root->SetAttribute("LabelFont",font);
    root->SetAttribute("CreationProgram","Molecule Studio");
    double labelSize=10;auto label=d.documentExtensions().findValue("studio.style.LabelSize");if(label){double n=std::atof(label->c_str());if(std::isfinite(n)&&n>0)labelSize=n;}
    root->SetAttribute("LabelSize",labelSize);root->SetAttribute("CaptionSize",d.defaultTextSize());
    for(const char* key:{"BondSpacing","MarginWidth","LabelFace","BoldWidth","HashSpacing"})if(auto value=d.documentExtensions().findValue(std::string("studio.style.")+key))root->SetAttribute(key,value->c_str());
    if(ft)for(auto f=ft->FirstChildElement("font");f;f=f->NextSiblingElement("font"))f->SetAttribute("charset","iso-8859-1");
    Page p=Page::get(d);if(!p.valid())return Error(ErrorCode::InvalidArgument,"纸张设置无效");
    // Keep world coordinates, including negative coordinates. The sheet origin
    // follows the document's paper origin rather than clipping at (0,0).
    double ox=p.x,oy=p.y;
    std::ostringstream bb;bb<<0<<' '<<0<<' '<<p.width*p.across<<' '<<p.height*p.down;
    root->SetAttribute("BoundingBox",bb.str().c_str());root->DeleteAttribute("WindowPosition");root->DeleteAttribute("WindowSize");
    for(auto pg=root->FirstChildElement("page");pg;pg=pg->NextSiblingElement("page")){
        pg->SetAttribute("BoundingBox",bb.str().c_str());pg->SetAttribute("WidthPages",p.across);pg->SetAttribute("HeightPages",p.down);
        pg->SetAttribute("HeaderPosition",36);pg->SetAttribute("FooterPosition",36);
        // Captions and graphics belong to the page, not to a molecule fragment.
        for(auto frag=pg->FirstChildElement("fragment");frag;frag=frag->NextSiblingElement("fragment")){
            for(auto child=frag->FirstChildElement();child;){auto next=child->NextSiblingElement();std::string n=child->Name();if(n!="n"&&n!="b"&&n!="fragment"){auto clone=child->DeepClone(&xml);pg->InsertEndChild(clone);frag->DeleteChild(child);}child=next;}
        }
        // ChemDraw clips negative drawing coordinates at the printable origin.
        // Translate the exported page, never the live document, into that space.
        double minX=1e30,minY=1e30;
        std::function<void(tinyxml2::XMLElement*,bool,double,double)> walk;
        walk=[&](tinyxml2::XMLElement* node,bool move,double dx,double dy){
            for(auto e=node->FirstChildElement();e;e=e->NextSiblingElement()){
                for(const char* name:{"p","xyz","BoundingBox","CurvePoints","Head3D","Tail3D","Center3D","MajorAxisEnd3D","MinorAxisEnd3D"})if(auto attr=e->Attribute(name)){
                    std::istringstream in(attr);std::vector<double> v;double n;while(in>>n)v.push_back(n);if(v.size()<2)continue;
                    if(!move){minX=std::min(minX,v[0]);minY=std::min(minY,v[1]);}
                    else{v[0]+=dx;v[1]+=dy;if(std::string(name)=="BoundingBox"||std::string(name)=="CurvePoints")for(size_t k=2;k+1<v.size();k+=2){v[k]+=dx;v[k+1]+=dy;}std::ostringstream o;o.precision(12);for(size_t i=0;i<v.size();i++){if(i)o<<' ';o<<v[i];}e->SetAttribute(name,o.str().c_str());}
                }
                walk(e,move,dx,dy);
            }
        };
        walk(pg,false,0,0);double dx=-ox,dy=-oy;if(minX<1e29&&minX+dx<24)dx+=24-minX-dx;if(minY<1e29&&minY+dy<24)dy+=24-minY-dy;walk(pg,true,dx,dy);
    }
    tinyxml2::XMLPrinter printer;xml.Print(&printer);*bytes=printer.CStr();return Error();
}
// Copy selected objects through the model API so every insertion is recorded by
// the caller's transaction. Source IDs are cleared to avoid cross-file collisions.
inline std::set<ObjectRef> expanded(const Document& doc,const std::vector<ObjectRef>& refs){std::set<ObjectRef> out(refs.begin(),refs.end());bool changed=true;while(changed){auto size=out.size();auto scan=out;for(auto r:scan){if(r.kind==ObjectKind::Group){auto g=doc.groups().alive(r.id);if(g)out.insert(g->members.begin(),g->members.end());}if(r.kind==ObjectKind::Bond){auto b=doc.bonds().alive(r.id);if(b){out.insert(ObjectRef(ObjectKind::Atom,b->beginAtom));out.insert(ObjectRef(ObjectKind::Atom,b->endAtom));}}}for(auto& b:doc.bonds().records())if(b.isAlive()&&out.count(ObjectRef(ObjectKind::Atom,b.beginAtom))&&out.count(ObjectRef(ObjectKind::Atom,b.endAtom)))out.insert(ObjectRef(ObjectKind::Bond,b.id));changed=out.size()!=size;}return out;}
inline Box selectionBounds(integration::Session& s){
    // An empty selection has no bounds. Keep the last nonempty selection cached
    // instead of scanning the page and evicting it on every selection clear.
    if(s.selected.empty())return Box(Vec2(1,1),Vec2(-1,-1));
    static const integration::Session* owner=nullptr;static DocumentId document=0;static uint64_t serial=0;
    static std::vector<ObjectRef> selection;static Box cached;
    if(owner==&s&&document==s.doc.id()&&serial==s.serial&&selection==s.selected)return cached;
    auto refs=expanded(s.doc,s.selected);Box b(Vec2(1,1),Vec2(-1,-1));
    auto include=[&](Vec2 p){if(!p.finite())return;if(b.empty())b=Box(p,p);else b.add(p);};
    for(auto& p:s.scene.primitives())if(refs.count(p.source)&&!p.bounds.empty()){include(p.bounds.min);include(p.bounds.max);}
    for(auto r:refs)if(r.kind==ObjectKind::Atom&&s.doc.atoms().alive(r.id))include(s.doc.atoms().get(r.id)->position);
    owner=&s;document=s.doc.id();serial=s.serial;selection=s.selected;cached=b;return b;
}
inline bool inside(const Box& b,Vec2 p,double pad=0){return !b.empty()&&p.x>=b.min.x-pad&&p.x<=b.max.x+pad&&p.y>=b.min.y-pad&&p.y<=b.max.y+pad;}
inline Error append(const Document& src,Document& dst,const std::vector<ObjectRef>& refs,Vec2 delta,std::vector<ObjectRef>* inserted){
    std::set<ObjectRef> chosen(refs.begin(),refs.end());
    bool changed=true;while(changed){size_t n=chosen.size();auto scan=chosen;
        for(auto r:scan){if(r.kind==ObjectKind::Group){auto g=src.groups().alive(r.id);if(g)chosen.insert(g->members.begin(),g->members.end());}
            if(r.kind==ObjectKind::Bond){auto b=src.bonds().alive(r.id);if(b){chosen.insert(ObjectRef(ObjectKind::Atom,b->beginAtom));chosen.insert(ObjectRef(ObjectKind::Atom,b->endAtom));}}
            if(r.kind==ObjectKind::Table){auto t=src.tables().alive(r.id);if(t)for(auto id:t->cells)if(id!=kInvalidObjectId)chosen.insert(ObjectRef(ObjectKind::Text,id));}}
        for(auto& b:src.bonds().records())if(b.isAlive()&&chosen.count(ObjectRef(ObjectKind::Atom,b.beginAtom))&&chosen.count(ObjectRef(ObjectKind::Atom,b.endAtom)))chosen.insert(ObjectRef(ObjectKind::Bond,b.id));
        changed=n!=chosen.size();
    }
    std::map<ObjectRef,ObjectRef> map;Error error;
    auto add=[&](ObjectRef old,ObjectId id,Error e){if(!e.ok()){error=e;return;}ObjectRef fresh(old.kind,id);map[old]=fresh;if(inserted)inserted->push_back(fresh);
        // Preserve non-source custom data; stale source snapshots must not be
        // re-emitted at their pre-paste coordinates on a later CDXML save.
        for(auto& v:src.extensions(old).entries())if(v.key.compare(0,4,"pst.")!=0)dst.extensions(fresh).set(v.key,v.value);
        if(old.kind==ObjectKind::Bond&&!dst.extensions(fresh).findValue("studio.graphics.width")){
            double width=src.defaultLineWidth();if(auto v=src.extensions(old).findValue("pst.attr.LineWidth"))width=std::atof(v->c_str());
            if(std::isfinite(width)&&width>0)dst.extensions(fresh).set("studio.graphics.width",std::to_string(width));
        }
        if(old.kind==ObjectKind::Atom){auto runs=studioatom::runs(src,old.id);double size=10;if(auto v=src.documentExtensions().findValue("studio.style.LabelSize"))size=std::atof(v->c_str());for(auto& run:runs){if(run.size<=0)run.size=size;if(run.fontName.empty())run.fontName=src.defaultFont();}if(!runs.empty())studioatom::set(dst,fresh.id,runs);}

    };
    for(auto r:chosen)if(r.kind==ObjectKind::Atom){auto a=src.atoms().alive(r.id);if(!a)continue;auto c=*a;c.sourceId=0;c.position=c.position+delta;ObjectId id;auto e=dst.addAtomRecord(c,&id);add(r,id,e);if(e.ok()){if(!src.atomLabel(r.id).empty())dst.setAtomLabel(id,src.atomLabel(r.id));}}
    for(auto r:chosen)if(r.kind==ObjectKind::Text){auto a=src.texts().alive(r.id);if(!a)continue;auto c=*a;c.sourceId=0;c.position=c.position+delta;if(c.size<=0)c.size=src.defaultTextSize();if(c.format.fontName.empty())c.format.fontName=src.defaultFont();ObjectId id;auto e=dst.addTextRecord(c,&id);add(r,id,e);if(e.ok())dst.setTextRuns(id,src.runs(r.id));}
    for(auto r:chosen)if(r.kind==ObjectKind::Bond){auto a=src.bonds().alive(r.id);if(!a)continue;auto c=*a;c.sourceId=0;auto b=map.find(ObjectRef(ObjectKind::Atom,c.beginAtom)),z=map.find(ObjectRef(ObjectKind::Atom,c.endAtom));if(b==map.end()||z==map.end())return Error(ErrorCode::NotFound,"复制的化学键缺少端点");c.beginAtom=b->second.id;c.endAtom=z->second.id;ObjectId id;auto e=dst.addBondRecord(c,&id);add(r,id,e);}
    for(auto r:chosen){ObjectId id=0;Error e;
        if(r.kind==ObjectKind::Path){auto a=src.paths().alive(r.id);if(!a)continue;auto c=*a;c.sourceId=0;for(auto& pt:c.points)pt=pt+delta;e=dst.addPath(c,&id);}
        else if(r.kind==ObjectKind::Table){auto a=src.tables().alive(r.id);if(!a)continue;auto c=*a;c.sourceId=0;c.frame.min=c.frame.min+delta;c.frame.max=c.frame.max+delta;for(auto& cell:c.cells){auto it=map.find(ObjectRef(ObjectKind::Text,cell));cell=it==map.end()?kInvalidObjectId:it->second.id;}e=dst.addTable(c,&id);}
        else {const ObjectSlab<AnnotationRecord>* slab=r.kind==ObjectKind::Arrow?&src.arrows():r.kind==ObjectKind::Bracket?&src.brackets():r.kind==ObjectKind::Orbital?&src.orbitals():r.kind==ObjectKind::Symbol?&src.symbols():r.kind==ObjectKind::BioShape?&src.bioShapes():nullptr;if(!slab)continue;auto a=slab->alive(r.id);if(!a)continue;auto c=*a;c.sourceId=0;for(auto& pt:c.geometry)pt=pt+delta;for(auto& anchor:c.anchors){auto it=map.find(ObjectRef(ObjectKind::Atom,anchor));anchor=it==map.end()?kInvalidObjectId:it->second.id;}e=dst.addAnnotation(r.kind,c,&id);}
        add(r,id,e);
        if(e.ok()&&r.kind==ObjectKind::Table){graphics::TableData data;bool used=false;e=graphics::readTable(src,r.id,&data,&used);if(!e.ok())return e;
            data.grid.frame.min=data.grid.frame.min+delta;data.grid.frame.max=data.grid.frame.max+delta;
            for(auto& cell:data.cells)if(cell.kind==graphics::CellKind::Text){auto found=map.find(ObjectRef(ObjectKind::Text,cell.text));if(found==map.end())return Error(ErrorCode::NotFound,"Table cell copy is missing");cell.text=found->second.id;}
            e=graphics::writeTable(dst,id,data);if(!e.ok())return e;
        }
    }
    for(auto r:chosen)if(r.kind==ObjectKind::Group){auto a=src.groups().alive(r.id);if(!a)continue;auto c=*a;c.sourceId=0;c.members.clear();for(auto m:a->members){auto it=map.find(m);if(it!=map.end())c.members.push_back(it->second);}ObjectId id;auto e=dst.addGroup(c,&id);add(r,id,e);}
    return error;
}
inline UINT nativeFormat(){return RegisterClipboardFormatW(L"MoleculeStudio.DocumentSelection.v2");}
// Host hook for OLE publishing. When set, copy() hands the selection to the
// OLE data object (Embed Source + EMF) instead of writing raw clipboard formats.
inline std::function<bool(const Document&,const std::string&,const std::string&)>& olePublisher(){static std::function<bool(const Document&,const std::string&,const std::string&)> fn;return fn;}
inline Error put(UINT fmt,const void* bytes,size_t size){HGLOBAL h=GlobalAlloc(GMEM_MOVEABLE|GMEM_ZEROINIT,size);if(!h)return Error(ErrorCode::IoError,"剪贴板内存不足");void* p=GlobalLock(h);if(!p){GlobalFree(h);return Error(ErrorCode::IoError,"剪贴板分配失败");}memcpy(p,bytes,size);GlobalUnlock(h);if(!SetClipboardData(fmt,h)){GlobalFree(h);return Error(ErrorCode::IoError,"剪贴板写入失败");}return Error();}
inline Error copy(integration::Session& s,HWND owner,bool cut){
    if(s.selected.empty())return Error(ErrorCode::InvalidArgument,"请先选择对象");
    Document subset;subset.setDefaultBondLength(s.doc.defaultBondLength());subset.setDefaultLineWidth(s.doc.defaultLineWidth());subset.setDefaultFont(s.doc.defaultFont());subset.setDefaultTextSize(s.doc.defaultTextSize());
    for(const auto& entry:s.doc.documentExtensions().entries())if(entry.key.compare(0,13,"studio.style.")==0||entry.key.compare(0,15,"studio.palette.")==0)subset.documentExtensions().set(entry.key,entry.value);
    auto e=append(s.doc,subset,s.selected,Vec2(),nullptr);if(!e.ok())return e;std::ostringstream out;e=integration::saveSafeNative(subset,out);if(!e.ok())return e;auto bytes=out.str();std::string xml;e=exportXml(subset,&xml); // Native clipboard remains usable for objects not exportable as CDXML.
    if(olePublisher()){
        if(!olePublisher()(subset,bytes,xml))return Error(ErrorCode::IoError,s.status.empty()?"无法复制可编辑的 OLE 对象，请重试":s.status);
        if(cut){e=s.submit(commands::deleteObjects(s.selected),"剪切");if(e.ok())s.selected.clear();}
        s.status=cut?"已剪切":"已复制";return e;
    }
    if(!OpenClipboard(owner))return Error(ErrorCode::Locked,"剪贴板被占用，请重试");
    if(!EmptyClipboard()){CloseClipboard();return Error(ErrorCode::IoError,"无法清空剪贴板");}
    e=put(nativeFormat(),bytes.c_str(),bytes.size()+1);
    if(e.ok()&&!xml.empty()){put(RegisterClipboardFormatW(L"ChemDraw XML"),xml.c_str(),xml.size()+1);std::string cdx,why;if(cdxbinary::fromXml(xml,cdx,why))put(cdxbinary::format(),cdx.data(),cdx.size());int n=MultiByteToWideChar(CP_UTF8,0,xml.c_str(),-1,0,0);std::wstring wide(n,L'\0');MultiByteToWideChar(CP_UTF8,0,xml.c_str(),-1,&wide[0],n);put(CF_UNICODETEXT,wide.c_str(),wide.size()*sizeof(wchar_t));}
    CloseClipboard();if(!e.ok())return e;
    if(cut){e=s.submit(commands::deleteObjects(s.selected),"剪切");if(e.ok())s.selected.clear();}s.status=cut?"已剪切":"已复制";return e;
}
inline bool readOleClipboard(UINT& fmt,std::string& bytes){
    IDataObject* data=nullptr;if(FAILED(OleGetClipboard(&data))||!data)return false;
    struct Query{UINT cf;bool text;} queries[]={{nativeFormat(),false},{RegisterClipboardFormatW(L"ChemDraw XML"),false},{cdxbinary::format(),false},{CF_UNICODETEXT,true}};
    bool ok=false;
    for(const auto& q:queries){
        FORMATETC f={};f.cfFormat=CLIPFORMAT(q.cf);f.dwAspect=DVASPECT_CONTENT;f.lindex=-1;f.tymed=TYMED_HGLOBAL;
        STGMEDIUM m={};HRESULT hr=data->GetData(&f,&m);
        if(SUCCEEDED(hr)&&m.tymed==TYMED_HGLOBAL){
            size_t n=GlobalSize(m.hGlobal);auto p=GlobalLock(m.hGlobal);
            if(p&&n<=128*1024*1024){
                if(q.text){auto w=static_cast<const wchar_t*>(p);size_t len=0;while(len<n/2&&w[len])++len;int z=WideCharToMultiByte(CP_UTF8,0,w,int(len),0,0,0,0);bytes.resize(z);WideCharToMultiByte(CP_UTF8,0,w,int(len),&bytes[0],z,0,0);}
                else{auto c=static_cast<const char*>(p);bytes.assign(c,c+n);if(q.cf!=cdxbinary::format())while(!bytes.empty()&&bytes.back()==0)bytes.pop_back();}
                ok=!bytes.empty();
            }
            if(p)GlobalUnlock(m.hGlobal);
            ReleaseStgMedium(&m);
            if(ok){fmt=q.cf;break;}
        }
    }
    data->Release();return ok;
}
inline Error paste(integration::Session& s,HWND owner){
    UINT fmt=0;std::string bytes;
    if(!readOleClipboard(fmt,bytes)){
        if(!OpenClipboard(owner))return Error(ErrorCode::Locked,"剪贴板被占用，请重试");
        fmt=IsClipboardFormatAvailable(nativeFormat())?nativeFormat():IsClipboardFormatAvailable(RegisterClipboardFormatW(L"ChemDraw XML"))?RegisterClipboardFormatW(L"ChemDraw XML"):IsClipboardFormatAvailable(cdxbinary::format())?cdxbinary::format():CF_UNICODETEXT;
        HANDLE h=GetClipboardData(fmt);if(h){size_t n=GlobalSize(h);auto p=GlobalLock(h);if(p&&n<=128*1024*1024){if(fmt==CF_UNICODETEXT){auto w=static_cast<const wchar_t*>(p);size_t len=0;while(len<n/2&&w[len])++len;int z=WideCharToMultiByte(CP_UTF8,0,w,int(len),0,0,0,0);bytes.resize(z);WideCharToMultiByte(CP_UTF8,0,w,int(len),&bytes[0],z,0,0);}else{auto c=static_cast<const char*>(p);bytes.assign(c,c+n);if(fmt!=cdxbinary::format())while(!bytes.empty()&&bytes.back()==0)bytes.pop_back();}}if(p)GlobalUnlock(h);}
        CloseClipboard();
    }
    if(bytes.empty())return Error(ErrorCode::NotFound,"剪贴板中没有可粘贴的内容");
    if(fmt==cdxbinary::format()){std::string xml,why;if(!cdxbinary::toXml(bytes,xml,why))return Error(ErrorCode::ParseError,"ChemDraw CDX: "+why);bytes=std::move(xml);}

    auto source=std::make_shared<integration::Session>();Error e;if(fmt==nativeFormat()){std::istringstream in(bytes);e=integration::loadSafeNative(source->doc,in);}else if(bytes.find("<CDXML")!=std::string::npos)e=persistence::readCdxml(bytes,source->doc);else{ObjectId t;e=source->doc.addText(Vec2(),s.doc.defaultTextSize(),bytes,&t);}
    if(!e.ok())return e;source->refresh();auto refs=source->all();if(refs.empty())return Error(ErrorCode::NotFound,"剪贴板中没有可编辑的对象");
    static std::string previous;static DocumentId document=0;static int count=0;if(previous==bytes&&document==s.doc.id())++count;else{count=1;previous=bytes;document=s.doc.id();}
    Vec2 delta(12*count,12*count);Box bounds;bool first=true;for(auto& p:source->scene.primitives()){if(first){bounds=p.bounds;first=false;}else{bounds.add(p.bounds.min);bounds.add(p.bounds.max);}}if(!first){auto center=(bounds.min+bounds.max)*.5;auto pixel=s.view().toDevice(center);if(pixel.x<0||pixel.y<0||pixel.x>s.renderView.widthPx||pixel.y>s.renderView.heightPx)delta=delta+s.view().center-center;}
    auto inserted=std::make_shared<std::vector<ObjectRef>>();s.cancel();e=s.submit(Command("粘贴",kInvalidateStructure|kInvalidateGeometry|kInvalidateAppearance,[source,refs,delta,inserted](CommandContext& c){inserted->clear();auto e=append(source->doc,c.document,refs,delta,inserted.get());return e.ok()?CommandResult::success():CommandResult::fail(e.code,e.message);}),"粘贴");
    if(e.ok()){s.selected=*inserted;s.activateCommand("chooseSelectionTool_Marquee");s.status="已粘贴";}return e;
}
inline void selectionOverlay(integration::Session& s,render::OverlayScene& out){
    using namespace render;auto v=s.view();double unit=v.deviceRadiusToDocument(v.dpiScale*.75);
    auto segment=[&](Vec2 a,Vec2 b,uint32_t color,double width){Primitive p;p.kind=PrimKind::Segment;p.points={a,b};p.layer=LayerId(900);p.z=100;p.style.stroke=Paint(color,true);p.style.strokeWidth=unit*width;p.style.fill.enabled=false;p.bounds=geom::computeBounds(p,.6);out.scene().add(p);};
    Page page=Page::get(s.doc);double x=page.x,y=page.y;
    for(int c=0;c<=page.across;c++)segment(Vec2(x+c*page.width,y),Vec2(x+c*page.width,y+page.down*page.height),0xD9DEE3FF,1);
    for(int r=0;r<=page.down;r++)segment(Vec2(x,y+r*page.height),Vec2(x+page.across*page.width,y+r*page.height),0xD9DEE3FF,1);
    // Arrow editing uses its geometric endpoints and bend/head handles rather
    // than unrelated rectangle corners. These are the same handles used by T10.
    for(auto r:s.selected)if(r.kind==ObjectKind::Arrow){arrows::ArrowParams params;bool ok=false;if(arrows::readArrow(s.doc,r.id,&params,&ok).ok()){for(auto h:arrows::arrowHandles(params,s.doc.defaultLineWidth(),arrows::Tessellation())){Primitive p;p.kind=PrimKind::Rect;p.points={h.point-Vec2(unit*2,unit*2),h.point+Vec2(unit*2,unit*2)};p.layer=LayerId(900);p.z=102;p.style.fill=Paint(0x7CB9E8FF,true);p.style.stroke.enabled=false;p.bounds=Box(p.points[0],p.points[1]);out.scene().add(p);}}}
    // One frame for the entire selection; hit testing uses the identical frame.
    std::map<ObjectRef,Box> boxes;auto selectedBounds=selectionBounds(s);
    if(!selectedBounds.empty()&&!s.selected.empty()&&!(s.selected.size()==1&&(s.selected[0].kind==ObjectKind::Arrow||s.selected[0].kind==ObjectKind::Table)))boxes[ObjectRef(ObjectKind::Atom,0)]=selectedBounds;
    for(auto kv:boxes){Box b=kv.second;double pad=unit*3;b.min=b.min-Vec2(pad,pad);b.max=b.max+Vec2(pad,pad);double cx=(b.min.x+b.max.x)*.5,cy=(b.min.y+b.max.y)*.5;uint32_t color=0x7CB9E8FF;
        segment(b.min,Vec2(b.max.x,b.min.y),color,1);segment(Vec2(b.max.x,b.min.y),b.max,color,1);segment(b.max,Vec2(b.min.x,b.max.y),color,1);segment(Vec2(b.min.x,b.max.y),b.min,color,1);
        std::vector<Vec2> handles={b.min,Vec2(cx,b.min.y),Vec2(b.max.x,b.min.y),Vec2(b.min.x,cy),Vec2(b.max.x,cy),Vec2(b.min.x,b.max.y),Vec2(cx,b.max.y),b.max};
        if(kv.first.kind!=ObjectKind::Text){segment(Vec2(cx-unit*3,cy),Vec2(cx+unit*3,cy),color,1);segment(Vec2(cx,cy-unit*3),Vec2(cx,cy+unit*3),color,1);}
        Vec2 rotation(cx,b.min.y-unit*12);segment(Vec2(cx,b.min.y),rotation+Vec2(0,unit*8),0x686868FF,1);
        auto rotationShape=[&](const std::vector<Vec2>& points,bool filled){Primitive p;p.kind=filled?PrimKind::Polygon:PrimKind::Polyline;for(auto pt:points)p.points.push_back(rotation+pt*unit);p.layer=LayerId(900);p.z=103;p.style.stroke=Paint(0x686868FF,!filled);p.style.strokeWidth=unit*1.8;p.style.fill=Paint(0x686868FF,filled);p.bounds=geom::computeBounds(p,p.style.strokeWidth);out.scene().add(p);};
        rotationShape(studio_rotation::arc(9),false);rotationShape(studio_rotation::arc(5),false);rotationShape(studio_rotation::arrow(),true);
        for(auto pt:handles){Primitive p;p.kind=PrimKind::Rect;p.points={pt-Vec2(unit*1.7,unit*1.7),pt+Vec2(unit*1.7,unit*1.7)};p.layer=LayerId(900);p.z=101;p.style.fill=Paint(color,true);p.style.stroke.enabled=false;p.bounds=Box(p.points[0],p.points[1]);out.scene().add(p);}
    }
}
}
#endif
