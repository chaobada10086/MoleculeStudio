#include "session.hpp"
#include "ring_geometry.hpp"
#include "source_tools.hpp"
#include "next/arrows_curves/render_bridge.hpp"
#include "next/graphics/render_bridge.hpp"
#include "next/biodraw/render_bridge.hpp"
#include "next/symbols_orbitals/render_bridge.hpp"
#include "next/experimental/render_bridge.hpp"
#include "next/experimental/model.hpp"
#include "next/selection/selection_set.hpp"
#include "next/selection/transform.hpp"
#include "next/chemistry/edit.hpp"
#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>
#include <cmath>
#include <cstring>
#include <stdexcept>
namespace r62 { namespace integration {
static void checked(Error e) {if(!e.ok()) throw std::runtime_error(e.message);}
static Box box(Vec2 a,Vec2 b) {return Box(Vec2(std::min(a.x,b.x),std::min(a.y,b.y)),Vec2(std::max(a.x,b.x),std::max(a.y,b.y)));}
static spatial::Box sb(const Box& b){return spatial::Box{b.min.x,b.min.y,b.max.x,b.max.y};}
static double dist(Vec2 a,Vec2 b){return std::hypot(a.x-b.x,a.y-b.y);}
static double lineDist(Vec2 p,Vec2 a,Vec2 b){Vec2 v=b-a; double n=v.x*v.x+v.y*v.y; double t=n?((p.x-a.x)*v.x+(p.y-a.y)*v.y)/n:0; return dist(p,a+v*std::max(0.0,std::min(1.0,t)));}
static Error unavailable(const std::string& x){return Error(ErrorCode::NotImplemented,x);}
// The delete half of a cut. T05 publishes first and only then calls this, so a
// refused clipboard publish never reaches the document.
static Error cutDelete(void* userData,const std::vector<ObjectRef>& refs){
    Session& s=*static_cast<Session*>(userData);
    return s.submit(commands::deleteObjects(refs),"cut selection");
}
static void appendRich(const text::RichLayout& layout,ObjectRef ref,uint32_t color,uint32_t layer,int z,std::vector<render::Primitive>* out){
    render::Primitive p;p.kind=render::PrimKind::Polygon;p.source=ref;p.layer=render::LayerId(layer);p.z=z;
    p.style.fill=render::Paint(color,true);p.style.stroke.enabled=false;p.style.fillRule=render::FillRule::NonZero;
    // T03 v10000 expects one outer contour followed by its holes. Separate
    // disconnected glyph components (Latin letters / CJK strokes / dots), then
    // feed each containment group through the existing T03 triangulator.
    for(const auto& glyph:layout.glyphs){const auto& cs=glyph.contours;
        std::vector<double> area(cs.size());std::vector<int> parent(cs.size(),-1),depth(cs.size(),0);
        for(size_t i=0;i<cs.size();++i){double a=0;for(size_t j=0;j<cs[i].size();++j){auto x=cs[i][j],y=cs[i][(j+1)%cs[i].size()];a+=x.x*y.y-y.x*x.y;}area[i]=std::abs(a);}
        for(size_t i=0;i<cs.size();++i)for(size_t j=0;j<cs.size();++j)if(area[j]>area[i]&&!cs[i].empty()&&selection::polygonContainsPoint(cs[j],cs[i][0]))
            if(parent[i]<0||area[j]<area[size_t(parent[i])])parent[i]=int(j);
        for(size_t i=0;i<cs.size();++i)for(int j=parent[i];j>=0;j=parent[size_t(j)])++depth[i];
        for(size_t i=0;i<cs.size();++i)if(depth[i]%2==0){auto q=p;q.style.fillRule=render::FillRule::EvenOdd;
            for(size_t j=0;j<cs.size();++j)if(j==i||(parent[j]==int(i)&&depth[j]%2==1)){
                render::Subpath sub;sub.closed=true;for(auto v:cs[j])sub.points.push_back(v+layout.anchor);q.subpaths.push_back(sub);}
            q.bounds=render::geom::computeBounds(q,.6);out->push_back(q);
        }
    }
    for(const auto& d:layout.decorations){render::Primitive q;q.kind=render::PrimKind::Rect;q.source=ref;q.layer=render::LayerId(layer);q.z=z+1;
        q.style=p.style;q.points={Vec2(d.x0+layout.anchor.x,d.y+layout.anchor.y),Vec2(d.x1+layout.anchor.x,d.y+d.thickness+layout.anchor.y)};q.bounds=render::geom::computeBounds(q,.6);out->push_back(q);}
}
static uint64_t versionOf(const render::Primitive& p,DocumentId doc){
    uint64_t h=1469598103934665603ull^doc;
    auto add=[&h](const void* data,size_t n){const unsigned char* b=static_cast<const unsigned char*>(data);for(size_t i=0;i<n;++i){h^=b[i];h*=1099511628211ull;}};
    add(&p.kind,sizeof(p.kind));add(&p.source.kind,sizeof(p.source.kind));add(&p.source.id,sizeof(p.source.id));
    for(auto v:p.points){add(&v.x,8);add(&v.y,8);}
    for(const auto& sub:p.subpaths)for(auto v:sub.points){add(&v.x,8);add(&v.y,8);}
    add(&p.bounds.min.x,8);add(&p.bounds.min.y,8);add(&p.bounds.max.x,8);add(&p.bounds.max.y,8);
    add(&p.style.strokeWidth,8);add(&p.style.stroke.rgba,4);add(&p.style.fill.rgba,4);
    add(&p.style.dash.on,8);add(&p.style.dash.off,8);add(&p.arrow,sizeof(p.arrow));add(&p.size,8);
    add(&p.style.fill.enabled,sizeof(bool));add(&p.style.stroke.enabled,sizeof(bool));
    return h;
}
Session::Session():dirty(false),fixedLen(true),fixedAngle(true),dragFixedLength(false),serial(1),viewSerial(1),activeIndex(0),rebuilds(0),
    font(text::createGdiGlyphProvider("Arial","Microsoft YaHei")),glyphs(font.get()),
    measurer(text::createMeasurerAdapter(&glyphs,text::FontSpec())),editAtom(kInvalidObjectId),createdTextId(kInvalidObjectId),
    bondDrawing(false),clickBond(kInvalidObjectId),selecting(false),moving(false),tlcSpotting(false),tlcFigure(kInvalidObjectId) {
    selection::registerSelectionClipboardFormat();
    if(!requestCompatibility(1,0)||!render::requestRenderCompatibility(1,0)||!interaction::requestInteractionCompatibility(1,0))
        throw std::runtime_error("Interface version mismatch");
    renderView.widthPx=1000; renderView.heightPx=700;
    renderView.transform.viewportCenter=Vec2(500,350); renderView.transform.dpiScale=96.0/72;
    builder.setTextMeasurer(measurer.get());
    for(size_t i=0;i<sizeof(sourceTools)/sizeof(sourceTools[0]);++i){
        Entry e; e.command=sourceTools[i].command; e.category=sourceTools[i].category; e.owner="T19";
        const std::string& c=e.command;
        if(const bonds::BondPreset* p=bonds::presetForCommand(c)){e.owner="T07"; e.tool.reset(new bonds::BondStyleTool(*p));}
        else if(const rings_chains::RingPresetSpec* p=rings_chains::ringPresetForCommand(c)){
            e.owner="T08"; auto t=new rings_chains::RingTool();t->setPreset(p->id);e.tool.reset(t);
        } else if(const rings_chains::ChainPresetSpec* p=rings_chains::chainPresetForCommand(c)){
            e.owner="T08";auto t=new rings_chains::ChainTool();t->setPreset(p->id);e.tool.reset(t);
        } else if(const arrows::PresetSpec* p=arrows::presetByCommand(c)){e.owner="T10";e.tool.reset(new arrows::ArrowTool(p->presetId));}
        else if(const graphics::ShapePresetSpec* p=graphics::shapePresetByCommand(c)){
            e.owner="T11";auto t=new graphics::PlaceShapeTool();t->activatePreset(p->presetId);e.tool.reset(t);
        } else if(const graphics::BracketPresetSpec* p=graphics::bracketPresetByCommand(c)){
            e.owner="T11";auto t=new graphics::PlaceBracketTool();t->activatePreset(p->presetId);e.tool.reset(t);
        } else if(const biodraw::BioPresetSpec* p=biodraw::bioPresetByCommand(c)){
            e.owner="T14";auto t=new biodraw::BioDrawPlaceTool();t->activatePreset(p->presetId);e.tool.reset(t);
        } else if(const symbols::PushPresetSpec* p=symbols::pushPresetByCommand(c)){
            e.owner="T12";e.tool.reset(new symbols::PushTool(p->presetId));
        } else if(const symbols::SymbolPresetSpec* p=symbols::symbolPresetByCommand(c)){
            e.owner="T12";e.tool.reset(new symbols::SymbolTool(p->presetId));
        } else if(const symbols::OrbitalPresetSpec* p=symbols::orbitalPresetByCommand(c)){
            e.owner="T12";e.tool.reset(new symbols::OrbitalTool(p->presetId));
        } else if(const symbols::PushPresetSpec* p=symbols::pushPresetByCommand(c)){
            e.owner="T12";e.tool.reset(new symbols::PushTool(p->presetId));
        } else if(const experimental::PlatePresetSpec* p=experimental::platePresetByCommand(c)){
            e.owner="T16";auto t=new experimental::PlacePlateTool();t->activatePreset(p->presetId);e.tool.reset(t);
        } else if(c=="chooseTableTool"){e.owner="T11";e.tool.reset(new graphics::PlaceTableTool());}
        else if(c=="choosePenTool_DrawCurve"){e.owner="T10";e.tool.reset(new arrows::DrawCurveTool());}
        else if(c=="choosePenTool_EditCurve"){e.owner="T10";e.tool.reset(new arrows::EditCurveTool());}
        else if(c.find("SelectionTool")!=std::string::npos||c.find("Eraser")!=std::string::npos){e.owner="T05";}
        else if(c.find("EdittingTool_Text")!=std::string::npos){e.owner="T09";}
        else {
            e.owner=c.find("Fragment")!=std::string::npos||c.find("Perspective")!=std::string::npos?"T17":"T13";
            e.limitation="Service delivered separately; interactive adapter not accepted in T19";
        }
        ToolPreset p;p.id="r62.integration.source."+std::to_string(i);p.sourceCommand=c;p.category=e.category;p.displayName=c;
        p.implemented=e.limitation.empty();p.behavior=e.tool?e.tool->behavior():ToolBehavior::Select;
        uint32_t id;checked(registry.registerPreset(p,&id));checked(registry.bindPlatformCommand(p.id,platform_command::ToolBand+id));
        entries.push_back(std::move(e));
    }
    input.setRouter(&router);input.setKeymap(&interaction::Keymap::defaults());input.bindDocument(doc.id());setView(view());
    for(auto a:{interaction::HotkeyAction::Undo,interaction::HotkeyAction::Redo,interaction::HotkeyAction::RepeatLastAction,
      interaction::HotkeyAction::EditSelectAll,interaction::HotkeyAction::EditDelete,interaction::HotkeyAction::ActivateMarqueeTool,
      interaction::HotkeyAction::ActivateBondTool,interaction::HotkeyAction::ActivateTextTool,
      interaction::HotkeyAction::ActivateArrowTool,interaction::HotkeyAction::ZoomFit,
      interaction::HotkeyAction::ZoomActualSize,interaction::HotkeyAction::ZoomIn,interaction::HotkeyAction::ZoomOut})
        router.registerHandler(a,&Session::action,this);
    // Every cited action enters one router; unsupported actions remain explicit.
    for(const auto& binding:interaction::Keymap::defaults().bindings())
        router.registerHandler(binding.action,&Session::action,this);
    for(const auto& accel:interaction::defaultAccelerators())
        router.registerHandler(accel.action,&Session::action,this);
    refresh();activateCommand("chooseSelectionTool_Marquee");
}
void Session::addToSelection(const std::vector<ObjectRef>& x){for(auto r:x)if(std::find(selected.begin(),selected.end(),r)==selected.end())selected.push_back(r);}
void Session::setView(const ViewTransform& x){renderView.transform=x;++viewSerial;
    // T04 v10000 uses scale alone for pixel radii. Flatten DPI in its adapter
    // while preserving the identical document <-> device transform.
    auto inputView=x;inputView.scale*=x.dpiScale;inputView.dpiScale=1;
    input.setView(inputView);input.notifyViewChanged(viewSerial);}
Error Session::submit(const Command& c,const std::string& tag){
    CommandContext ctx(doc);ctx.view=&renderView.transform;ctx.tag=tag;Error e=history.execute(doc,c,&ctx);
    if(e.ok()){dirty=true;refresh();}else status=e.message;return e;
}
Error Session::undo(bool redo){cancel();Error e=redo?history.redo(doc):history.undo(doc);if(e.ok()){dirty=true;refresh();}return e;}
std::vector<ObjectRef> Session::all() const {
    std::vector<ObjectRef> r;
#define ADD(s,k) for(const auto& x:doc.s().records())if(x.isAlive())r.push_back(ObjectRef(ObjectKind::k,x.id));
    ADD(atoms,Atom) ADD(bonds,Bond) ADD(texts,Text) ADD(paths,Path) ADD(arrows,Arrow) ADD(brackets,Bracket)
    ADD(orbitals,Orbital) ADD(symbols,Symbol) ADD(bioShapes,BioShape) ADD(tables,Table)
#undef ADD
    return r;
}
Error Session::buildScene(){
    render::Scene basic;render::BuildStats bs;builder.rebuildContent(doc,&basic,view(),render::LayerId(100),&bs);
    scene.clear();
    // Keep base atom labels; business modules own every decorated bond and extended object.
    for(const auto& p:basic.primitives())if(p.source.kind==ObjectKind::Atom)scene.add(p);
    std::vector<render::Primitive> ps;
    RingGeometry rings(doc);
    for(const auto& b:doc.bonds().records())if(b.isAlive()){
        const auto* a=doc.atoms().alive(b.beginAtom);const auto* z=doc.atoms().alive(b.endAtom);if(!a||!z)return unavailable("dangling bond");
        bonds::BondAxes axes;axes.order=b.order;axes.display=b.display;axes.semantics=b.semantics;axes.stereo=b.stereo;
        axes.side=bonds::sideOf(static_cast<const Document&>(doc).extensions(ObjectRef(ObjectKind::Bond,b.id)));
        auto data=rings.build(b);
        bonds::RenderAdapterOptions opts;
        std::vector<render::Primitive> bondPrimitives;
        bonds::toPrimitives(data,ObjectRef(ObjectKind::Bond,b.id),opts,&bondPrimitives);
        ps.insert(ps.end(),bondPrimitives.begin(),bondPrimitives.end());
    }
    // T09 emits real font contours including rich runs (the GL backend has no injected font API).
    text::LayoutOptions lo;
    for(const auto& t:doc.texts().records())if(t.isAlive()){
        lo.defaultSize=t.size?t.size:doc.defaultTextSize();
        text::RichLayout layout;checked(text::layoutRichText(doc.runs(t.id),t.position,lo,&glyphs,&layout));
        size_t start=ps.size();appendRich(layout,ObjectRef(ObjectKind::Text,t.id),0x000000FF,100,10,&ps);
        for(size_t j=start;j<ps.size();++j)ps[j].source=ObjectRef(ObjectKind::Text,t.id);
    }
    for(const auto& a:doc.arrows().records())if(a.isAlive()){
        ObjectRef r(ObjectKind::Arrow,a.id);const auto& ex=static_cast<const Document&>(doc).extensions(r);
        if(ex.find("gfx.shape.params.1"))checked(graphics::appendShapePrimitivesFor(doc,a.id,100,10,&ps));
        else if(ex.find("so.push.params.1")){symbols::PushParams p;bool ok=false;checked(symbols::readPush(doc,a.id,&p,&ok));if(!ok)return unavailable("Unknown push");checked(symbols::appendPushPrimitives(p,.6,r,100,&ps));}
        else {arrows::ArrowParams p;bool ok=false;checked(arrows::readArrow(doc,a.id,&p,&ok));if(!ok)return unavailable("Unknown arrow preserved; rendering refused");
            checked(arrows::appendArrow(p,.6,arrows::Tessellation(),r,100,&ps));}
    }
    for(const auto& p:doc.paths().records())if(p.isAlive()){
        std::vector<Vec2> pts;std::vector<uint8_t> kinds;bool closed=false,ok=false;arrows::CurveStyleSpec style;
        checked(arrows::readCurve(doc,p.id,&pts,&kinds,&closed,&style,&ok));
        if(!ok)return unavailable("Unknown path preserved; rendering refused");
        checked(arrows::appendCurve(pts,kinds,closed,style,.6,arrows::Tessellation(),ObjectRef(ObjectKind::Path,p.id),100,&ps));
    }
    for(const auto& a:doc.brackets().records())if(a.isAlive())checked(graphics::appendBracketPrimitivesFor(doc,a.id,100,10,10,&ps));
    for(const auto& a:doc.orbitals().records())if(a.isAlive()){
        symbols::OrbitalParams p;bool ok=false;checked(symbols::readOrbital(doc,a.id,&p,&ok));if(!ok)return unavailable("Unknown orbital");
        checked(symbols::appendOrbital(p,.6,symbols::Tessellation(),ObjectRef(ObjectKind::Orbital,a.id),100,&ps));
    }
    for(const auto& a:doc.symbols().records())if(a.isAlive()){
        ObjectRef r(ObjectKind::Symbol,a.id);
        if(experimental::isFigureObject(doc,r))checked(experimental::appendFigurePrimitivesFor(doc,a.id,100,10,0,lo,&ps));
        else if(static_cast<const Document&>(doc).extensions(r).find("so.push.params.1")){
            symbols::PushParams p;bool ok=false;checked(symbols::readPush(doc,a.id,&p,&ok));if(!ok)return unavailable("Unknown push");
            checked(symbols::appendPushPrimitives(p,.6,r,100,&ps));
        } else {symbols::SymbolParams p;bool ok=false;checked(symbols::readSymbol(doc,a.id,&p,&ok));if(!ok)return unavailable("Unknown symbol");
            checked(symbols::appendSymbol(p,.6,symbols::Tessellation(),r,100,&ps));}
    }
    for(const auto& a:doc.tables().records())if(a.isAlive())checked(graphics::appendTablePrimitivesFor(doc,a.id,100,10,&ps));
    for(auto& p:ps){p.bounds=render::geom::computeBounds(p,.6);scene.add(p);}
    for(const auto& a:doc.bioShapes().records())if(a.isAlive())checked(biodraw::appendBioPrimitivesFor(doc,a.id,&scene));
    // All remaining Text primitives (element labels and table labels) also use T09.
    render::Scene resolved;
    for(const auto& p:scene.primitives()){
        if(p.kind!=render::PrimKind::Text){resolved.add(p);continue;}
        text::RichLayout l;text::LayoutOptions opt;opt.defaultSize=p.textSize>0?p.textSize:10;
        opt.defaultFont=p.fontName;
        checked(text::layoutPlainText(p.text,p.points.empty()?Vec2():p.points[0],opt,&glyphs,&l));
        std::vector<render::Primitive> v;appendRich(l,p.source,p.style.fill.enabled?p.style.fill.rgba:p.style.stroke.rgba,p.layer.value,p.z,&v);
        for(auto& q:v){q.source=p.source;q.bounds=render::geom::computeBounds(q,.6);resolved.add(q);}
    }
    scene=std::move(resolved);
    return Error();
}
void Session::refresh(){
    ++serial;++rebuilds;input.notifyDocumentChanged(serial);
    Error e=buildScene();if(!e.ok())status="Partial display: "+e.message;
    // T03 v10000 keys all same-kind primitives of one ObjectRef as ordinal 0.
    // Expanded business objects need distinct cache slots. Preserve original refs
    // in scene for editing, and use anonymous ordinal primitives for GPU submission.
    gpuScene.clear();
    for(auto p:scene.primitives()){
        p.version=versionOf(p,doc.id());p.source=ObjectRef();gpuScene.add(p);
    }
    std::vector<spatial::Box> ob,cb;objectRefs.clear();candidates.clear();
    for(const auto& p:scene.primitives())if(p.source.valid()&&!p.bounds.empty()) {ob.push_back(sb(p.bounds));objectRefs.push_back(p.source);}
    for(const auto& a:doc.atoms().records())if(a.isAlive()){
        auto r=ObjectRef(ObjectKind::Atom,a.id);Box b(a.position,a.position);ob.push_back(sb(b));objectRefs.push_back(r);
        candidates.push_back(interaction::SnapCandidate(r,a.position));cb.push_back(sb(b));
    }
    for(const auto& b:doc.bonds().records())if(b.isAlive()){
        auto a=doc.atoms().alive(b.beginAtom),z=doc.atoms().alive(b.endAtom);if(!a||!z)continue;
        Vec2 p=(a->position+z->position)*.5; candidates.push_back(interaction::SnapCandidate(ObjectRef(ObjectKind::Bond,b.id),p,interaction::CandidateKind::Bond));cb.push_back(sb(box(a->position,z->position)));
    }
    objectIndex.build(ob);candidateIndex.build(cb);
}
ObjectId Session::nearestAtom(const Vec2& p,double rad) const{
    double r=view().deviceRadiusToDocument(rad),best=r;ObjectId id=kInvalidObjectId;
    for(size_t k:candidateIndex.query(sb(Box(p-Vec2(r,r),p+Vec2(r,r))))){const auto& c=candidates[k];
        if(c.ref.kind==ObjectKind::Atom&&dist(p,c.point)<=best){best=dist(p,c.point);id=c.ref.id;}}
    return id;
}
ObjectId Session::nearestBond(const Vec2& p,double rad) const{
    double r=view().deviceRadiusToDocument(rad),best=r;ObjectId id=kInvalidObjectId;
    for(ObjectRef ref:objectsInBox(Box(p-Vec2(r,r),p+Vec2(r,r))))if(ref.kind==ObjectKind::Bond){
        const auto* b=doc.bonds().alive(ref.id);if(!b)continue;
        double d=lineDist(p,doc.atoms().get(b->beginAtom)->position,doc.atoms().get(b->endAtom)->position);
        if(d<=best){best=d;id=ref.id;}}
    return id;
}
std::vector<ObjectRef> Session::objectsInBox(const Box& b) const {std::set<ObjectRef> refs;for(size_t k:objectIndex.query(sb(b)))refs.insert(objectRefs[k]);return std::vector<ObjectRef>(refs.begin(),refs.end());}
std::vector<ObjectRef> Session::objectsInPolygon(const std::vector<Vec2>& pts) const {
    if(pts.empty()){return {};} Box b(pts[0],pts[0]);for(auto p:pts)b.add(p);auto refs=objectsInBox(b);std::vector<ObjectRef> out;
    for(auto r:refs){Vec2 center;
        if(r.kind==ObjectKind::Atom)center=doc.atoms().get(r.id)->position;
        else {bool found=false;for(const auto& p:scene.primitives())if(p.source==r){center=(p.bounds.min+p.bounds.max)*.5;found=true;break;}if(!found)continue;}
        if(selection::polygonContainsPoint(pts,center))out.push_back(r);
    }return out;
}
void Session::query(const Box& b,std::vector<interaction::SnapCandidate>* out,interaction::QueryStats* qs){
    spatial::QueryStats s;Vec2 p=(b.min+b.max)*.5;
    auto nearby=candidateIndex.query(sb(b),&s);bool atomPriority=false;
    for(size_t k:nearby)if(candidates[k].ref.kind==ObjectKind::Atom&&dist(candidates[k].point,p)<=view().deviceRadiusToDocument(snapRadiusDevice())){atomPriority=true;break;}
    for(size_t k:nearby){auto c=candidates[k];
        if(atomPriority&&c.ref.kind==ObjectKind::Bond)continue;
        if(c.ref.kind==ObjectKind::Bond){auto bond=doc.bonds().alive(c.ref.id);if(!bond)continue;
            Vec2 a=doc.atoms().get(bond->beginAtom)->position,z=doc.atoms().get(bond->endAtom)->position,v=z-a;
            double n=v.x*v.x+v.y*v.y,t=n?((p.x-a.x)*v.x+(p.y-a.y)*v.y)/n:0;
            c.point=a+v*std::max(0.0,std::min(1.0,t));}
        out->push_back(c);
    }if(qs){qs->nodesVisited+=s.visited_nodes;qs->candidatesTested+=s.tested_boxes;}
}
ObjectRef Session::pick(Vec2 p) const {
    auto a=nearestAtom(p,8);if(a!=kInvalidObjectId)return ObjectRef(ObjectKind::Atom,a);
    auto b=nearestBond(p,7);if(b!=kInvalidObjectId)return ObjectRef(ObjectKind::Bond,b);
    double r=view().deviceRadiusToDocument(4);auto refs=objectsInBox(Box(p-Vec2(r,r),p+Vec2(r,r)));
    if(!refs.empty())return refs.back();
    return ObjectRef();
}
void Session::fit(){auto b=scene.bounds();if(doc.atoms().size())for(const auto& a:doc.atoms().records())if(a.isAlive())b.add(a.position);
    ViewTransform v=view();v.center=(b.min+b.max)*.5;v.scale=std::min((renderView.widthPx-40)/std::max(20.0,b.width()),(renderView.heightPx-40)/std::max(20.0,b.height()))/v.dpiScale;setView(v);}
void Session::cancel(){if(activeIndex<entries.size()&&entries[activeIndex].tool)entries[activeIndex].tool->cancelGesture(*this);selecting=moving=bondDrawing=false;bondHysteresis.clear();ghost.clear();input.unwind("T19 cancel");}
bool Session::activateCommand(const std::string& c){for(size_t i=0;i<entries.size();++i)if(entries[i].command==c)return activate(i);reportUnsupported(c);return false;}
bool Session::activate(size_t n){if(n>=entries.size())return false;cancel();activeIndex=n;auto& e=entries[n];input.notifyToolChanged(e.command);
    if(!e.limitation.empty()){reportUnsupported(e.command+": "+e.limitation);return false;}
    if(e.tool){e.tool->activate(*this);} status=e.command+" | standalone; host unbound";return true;}
Error Session::gesture(PointerPhase phase,Vec2 p,uint32_t mods){
    Entry& e=entries[activeIndex];if(!e.limitation.empty())return unavailable(e.limitation);
    if(const bonds::BondPreset* preset=bonds::presetForCommand(e.command)){
        if(phase==PointerPhase::Down){
            press=p;bondDrawing=true;bondHysteresis.clear();bondOrigin=bonds::BondEndpoint();bondOrigin.point=p;
            auto id=nearestAtom(p,snapRadiusDevice());
            if(id!=kInvalidObjectId){bondOrigin.joined=true;bondOrigin.target=ObjectRef(ObjectKind::Atom,id);bondOrigin.point=doc.atoms().get(id)->position;}
            clickBond=bondOrigin.joined?kInvalidObjectId:nearestBond(p,7);
        }
        if(!bondDrawing)return Error();
        bool click=dist(p,press)<view().deviceRadiusToDocument(4);
        Vec2 target=p;
        if(click){double angle=-30;
            if(bondOrigin.joined){auto env=rings_chains::buildSproutEnvironment(doc,bondOrigin.target.id,25);auto direction=rings_chains::resolveSproutDirection(env,angle);if(direction.ok)angle=direction.directionDeg;}
            target=bondOrigin.point+core::direction(angle)*doc.defaultBondLength();
        }
        auto profile=input.profile();profile.fixedLength=fixedLen;profile.fixedAngles=fixedAngle;
        bondSnap=interaction::SnapResolver::resolveBond(profile,bondOrigin.point,target,doc.defaultBondLength(),1/view().docPerDevice(),*this,mods,bondOrigin.target,bondHysteresis,0);
        bondHysteresis.previous=bondSnap.target;bondHysteresis.previousKind=bondSnap.joined?interaction::HotspotKind::Atom:interaction::HotspotKind::None;
        bondHysteresis.documentId=doc.id();bondHysteresis.geometrySerial=serial;
        ghost.clear();
        auto data=bonds::buildBondGeometry(bondOrigin.point,bondSnap.endpoint,preset->axes,bonds::BondGeometryOptions());
        for(const auto& shape:data.shapes){PreviewItem q;q.points=shape.fill?shape.outline:shape.points;q.primitive=shape.fill?PreviewPrimitive::Polygon:PreviewPrimitive::Polyline;q.style.width=shape.strokeWidth;q.style.colorRGBA=0x2563EBFF;q.fillEnabled=shape.fill;ghost.add(q);}
        if(phase==PointerPhase::Up){bondDrawing=false;ghost.clear();
            if(click&&clickBond!=kInvalidObjectId){
                if(e.command=="chooseBondTool_Solid"){
                    const auto& b=*doc.bonds().alive(clickBond);bonds::BondAxes axes;axes.order=b.order;axes.display=b.display;axes.semantics=b.semantics;axes.stereo=b.stereo;axes.side=bonds::sideOf(static_cast<const Document&>(doc).extensions(ObjectRef(ObjectKind::Bond,b.id)));
                    return submit(bonds::setBondAxesCommand({clickBond},bonds::nextClickAxes(axes)),"click bond");
                }
                return submit(bonds::applyBondPresetCommand({clickBond},e.command),"apply bond preset");
            }
            return submit(bonds::addBondFromSnapCommand(bondOrigin,bondSnap,preset->axes,6,0),"resolved bond");
        }
        status=bondSnap.joined?"Snap: exact atom join (green)":"Preview: fixed length / 15 degree angle; Alt releases constraints";
        return Error();
    }
    // T16 owns plate placement and spot placement. A press inside an existing
    // board deposits a spot at that relative coordinate; a press anywhere else
    // falls through to the plate tool so new boards can still be created.
    if(e.owner=="T16"){
        bool insideBoard=false;
        if(phase==PointerPhase::Down){
            ObjectRef hit=pick(p);
            if(hit.kind==ObjectKind::Symbol&&experimental::isFigureObject(doc,hit)){
                experimental::PlateFigure figure;experimental::PlateFrame frame;bool used=false;
                if(experimental::readFigureAndFrame(doc,hit.id,&figure,&frame,&used).ok())
                    insideBoard=experimental::boardHitTest(figure,frame,p,view().deviceRadiusToDocument(6));
            }
            tlcSpotting=insideBoard;
            if(!insideBoard)tlcFigure=kInvalidObjectId;else tlcFigure=hit.id;
        }
        if(tlcSpotting){
            if(phase==PointerPhase::Up){
                tlcSpotting=false;
                if(tlcFigure==kInvalidObjectId)return Error();
                experimental::PlateFigure figure;experimental::PlateFrame frame;
                if(!experimental::readFigureAndFrame(doc,tlcFigure,&figure,&frame,0).ok())return Error();
                double u=0,v=0;if(!frame.relativeOf(p,&u,&v))return Error();
                auto element=experimental::pickElement(figure,frame,p,view().deviceRadiusToDocument(6));
                uint32_t lane=element.kind==experimental::ElementKind::Lane?element.id:0u;
                return submit(experimental::addSpotCommand(tlcFigure,lane,u,v,0),"TLC spot");
            }
            return Error();   // the drag is a spot, never a second board
        }
    }
    if(e.tool){PointerState s;s.position=p;s.rawPosition=view().toDevice(p);s.modifiers=mods;s.button=kButtonLeft;s.phase=phase;
        CommandResult r=phase==PointerPhase::Down?e.tool->beginGesture(*this,s,*this):phase==PointerPhase::Up?e.tool->endGesture(*this,s,*this):e.tool->updateGesture(*this,s,*this);
        if(phase!=PointerPhase::Up)if(auto ring=dynamic_cast<rings_chains::RingTool*>(e.tool.get())){
            const auto& plan=ring->pendingPlacement();
            if(plan.valid){
                // A six-atom scratch projection of the SAME immutable placement;
                // no public document mutation and no second placement resolver.
                Document projection;std::vector<ObjectId> ids;
                for(auto p:plan.worldPosition){ObjectId id;checked(projection.addAtom(p,6,&id));ids.push_back(id);}
                for(const auto& b:plan.geometry.bonds){ObjectId id;BondRecord rec;rec.beginAtom=ids[b.from];rec.endAtom=ids[b.to];rec.order=b.order;checked(projection.addBondRecord(rec,&id));}
                RingGeometry geometry(projection);ghost.clear();
                for(size_t i=0;i<plan.geometry.bonds.size();++i){if(plan.reuseBond[i]!=kInvalidObjectId)continue;
                    auto data=geometry.build(*projection.bonds().get(ObjectId(i)));
                    for(const auto& shape:data.shapes){PreviewItem q;q.primitive=PreviewPrimitive::Polyline;q.points=shape.points;q.style.width=doc.defaultLineWidth();q.style.colorRGBA=0x2563EBFF;ghost.add(q);}
                }
            }
        }
        if(!r.error.ok()){status=r.error.message;} return r.error;}
    if(e.owner=="T09"){
        if(phase!=PointerPhase::Up)return Error();
        ObjectId created=kInvalidObjectId;
        Error placed=submit(commands::addText(p,12,"",&created),"T09 text placement");
        if(placed.ok()){createdTextId=created;selected={ObjectRef(ObjectKind::Text,created)};refresh();}
        return placed;
    }
    if(e.owner!="T05")return unavailable(e.command);
    if(phase==PointerPhase::Down){press=p;lasso.clear();lasso.push_back(p);ObjectRef hit=pick(p);
        if(e.command.find("Eraser")!=std::string::npos){if(!hit.valid())return Error();return submit(commands::deleteObjects(std::vector<ObjectRef>(1,hit)),"erase");}
        moving=hit.valid();selecting=!moving;
        if(moving&&std::find(selected.begin(),selected.end(),hit)==selected.end()){if(!(mods&kModShift))selected.clear();addToSelection({hit});}
        else if(selecting&&!(mods&kModShift))selected.clear();
        if(moving){movingRefs=std::set<ObjectRef>(selected.begin(),selected.end());
            for(auto r:selected)if(r.kind==ObjectKind::Bond){const auto* b=doc.bonds().alive(r.id);if(b){movingRefs.insert(ObjectRef(ObjectKind::Atom,b->beginAtom));movingRefs.insert(ObjectRef(ObjectKind::Atom,b->endAtom));}}
            for(const auto& b:doc.bonds().records())if(b.isAlive()&&(movingRefs.count(ObjectRef(ObjectKind::Atom,b.beginAtom))||movingRefs.count(ObjectRef(ObjectKind::Atom,b.endAtom))))movingRefs.insert(ObjectRef(ObjectKind::Bond,b.id));
            dragScene=gpuScene;
        }
    } else if(phase==PointerPhase::Move){lasso.push_back(p);ghost.clear();
        if(selecting){PreviewItem x;x.primitive=PreviewPrimitive::Rect;x.points={press,p};ghost.add(x);}
        if(moving){dragScene.clear();Vec2 delta=p-press;
            if(dist(p,press)<view().deviceRadiusToDocument(2))delta=Vec2();
            std::set<ObjectRef> emitted;RingGeometry rings(doc);
            for(size_t i=0;i<scene.size();++i){const auto& original=scene.primitives()[i];auto ref=original.source;
                if(ref.kind==ObjectKind::Bond&&movingRefs.count(ref)){
                    if(!emitted.insert(ref).second)continue;
                    const auto& b=*doc.bonds().alive(ref.id);Vec2 a=doc.atoms().get(b.beginAtom)->position,z=doc.atoms().get(b.endAtom)->position;
                    if(movingRefs.count(ObjectRef(ObjectKind::Atom,b.beginAtom)))a=a+delta;
                    if(movingRefs.count(ObjectRef(ObjectKind::Atom,b.endAtom)))z=z+delta;
                    bonds::BondAxes axes;axes.order=b.order;axes.display=b.display;axes.semantics=b.semantics;axes.stereo=b.stereo;axes.side=bonds::sideOf(static_cast<const Document&>(doc).extensions(ref));
                    auto data=rings.build(b,&movingRefs,delta);std::vector<render::Primitive> v;
                    bonds::toPrimitives(data,ref,bonds::RenderAdapterOptions(),&v);
                    for(auto q:v){q.version=versionOf(q,doc.id());q.source=ObjectRef();dragScene.add(q);}
                }else {auto q=original;if(movingRefs.count(ref)){
                        for(auto& v:q.points)v=v+delta;
                        for(auto& sub:q.subpaths)for(auto& v:sub.points)v=v+delta;
                        q.bounds.min=q.bounds.min+delta;q.bounds.max=q.bounds.max+delta;
                    }q.version=versionOf(q,doc.id());q.source=ObjectRef();dragScene.add(q);}
            }
        }
    } else if(phase==PointerPhase::Up){
        bool m=moving;moving=selecting=false;ghost.clear();
        if(m){Vec2 delta=p-press;if(dist(p,press)<view().deviceRadiusToDocument(2))return Error();return dragFixedLength?moveFixedLength(selected,delta):move(selected,delta);}
        lasso.push_back(p);addToSelection(e.command.find("Lasso")!=std::string::npos&&lasso.size()>2?objectsInPolygon(lasso):objectsInBox(box(press,p)));
    }return Error();
}
Error Session::move(const std::vector<ObjectRef>& refs,Vec2 delta){
    return submit(Command("T19 move",kInvalidateGeometry,[refs,delta](CommandContext& c){Error e=translateDocumentObjects(c.document,refs,delta);return e.ok()?CommandResult::success():CommandResult::fail(e.code,e.message);}),"unified move");
}
// Ctrl+L / toolbar "锁定键长": rotate the selected fragment around its single
// external attachment instead of translating it.  Every internal bond and the
// attachment bond keep their original length; only angles and group position
// change.  Whole molecules (no external bond) still translate rigidly.
Error Session::moveFixedLength(const std::vector<ObjectRef>& refs,Vec2 delta){
    if(!dragFixedLength||refs.empty()||delta.x==0&&delta.y==0)return move(refs,delta);
    std::set<ObjectId> atoms;std::vector<ObjectRef> extra;
    for(auto r:refs){if(r.kind==ObjectKind::Atom)atoms.insert(r.id);else if(r.kind==ObjectKind::Bond){auto b=doc.bonds().alive(r.id);if(b){atoms.insert(b->beginAtom);atoms.insert(b->endAtom);}}else extra.push_back(r);}
    if(atoms.empty())return move(refs,delta);
    std::vector<ObjectId> externalAtoms;
    for(const auto& b:doc.bonds().records())if(b.isAlive()){
        bool a=atoms.count(b.beginAtom)!=0,z=atoms.count(b.endAtom)!=0;
        if(a!=z)externalAtoms.push_back(a?b.endAtom:b.beginAtom);
    }
    if(externalAtoms.size()!=1||!extra.empty())return move(refs,delta);
    Vec2 pivot=doc.atoms().get(externalAtoms.front())->position;
    ObjectId handleId=kInvalidObjectId;double best=1e100;
    for(auto id:atoms){auto a=doc.atoms().alive(id);if(!a)continue;double d=dist(a->position,pivot);if(d>1e-9&&d<best){best=d;handleId=id;}}
    if(handleId==kInvalidObjectId)return move(refs,delta);
    Vec2 v=doc.atoms().get(handleId)->position-pivot,moved=v+delta;
    double cross=v.x*moved.y-v.y*moved.x,dot=v.x*moved.x+v.y*moved.y;
    if(!std::isfinite(cross)||!std::isfinite(dot)||(std::abs(cross)<1e-12&&dot>0))return Error();
    const double angle=std::atan2(cross,dot)*180.0/3.14159265358979323846;
    std::vector<ObjectRef> transform;
    for(auto id:atoms)transform.push_back(ObjectRef(ObjectKind::Atom,id));
    for(const auto& b:doc.bonds().records())if(b.isAlive()&&atoms.count(b.beginAtom)&&atoms.count(b.endAtom))transform.push_back(ObjectRef(ObjectKind::Bond,b.id));
    for(auto r:extra)transform.push_back(r);
    return submit(Command("锁定键长拖动",kInvalidateGeometry,[transform,pivot,angle](CommandContext& c){
        selection::TransformOptions opts;selection::rotateObjects(c.document,transform,pivot,angle,opts);return CommandResult::success();
    }),"锁定键长拖动");
}
render::OverlayScene Session::overlay(){
    render::OverlayScene o;if(!moving)builder.buildSelection(doc,selected,view(),&o,Vec2());builder.buildPreview(ghost,view(),&o);
    auto ring=[&](Vec2 p,bool joined){
        render::Primitive q;q.kind=render::PrimKind::Polyline;q.style.stroke=render::Paint(joined?0x15803DFF:0x2563EBFF,true);q.style.fill.enabled=false;q.style.strokeWidth=view().deviceRadiusToDocument(1.5);q.layer=render::LayerId(900);q.z=100;
        double r=view().deviceRadiusToDocument(joined?8:6);
        for(int i=0;i<=40;++i)q.points.push_back(p+Vec2(std::cos(i*6.283185307/40),std::sin(i*6.283185307/40))*r);
        q.bounds=render::geom::computeBounds(q,.6);o.scene().add(q);
        if(joined){q.kind=render::PrimKind::Segment;q.points={p-Vec2(r*.5,0),p+Vec2(r*.5,0)};o.scene().add(q);q.points={p-Vec2(0,r*.5),p+Vec2(0,r*.5)};o.scene().add(q);}
    };
    const auto& hs=input.hotspot();
    if(hs.valid()&&!moving){
        if(hs.kind==interaction::HotspotKind::Atom)ring(hs.point,false);
        else if(auto b=doc.bonds().alive(hs.object.id)){
            Vec2 a=doc.atoms().get(b->beginAtom)->position,z=doc.atoms().get(b->endAtom)->position,u=z-a;double len=dist(a,z);
            if(len>0){Vec2 n(-u.y/len,u.x/len);double r=view().deviceRadiusToDocument(4);
                render::Primitive q;q.kind=render::PrimKind::Polyline;q.points={a+n*r,z+n*r,z-n*r,a-n*r,a+n*r};q.style.stroke=render::Paint(0x2563EBFF,true);q.style.fill.enabled=false;q.style.strokeWidth=view().deviceRadiusToDocument(1);q.layer=render::LayerId(900);q.z=100;q.bounds=render::geom::computeBounds(q,.6);o.scene().add(q);}
        }
    }
    if(bondDrawing){ring(bondOrigin.point,false);ring(bondSnap.endpoint,bondSnap.joined);}
    // Overlay cache has the same ordinal issue as content; geometry is the key.
    render::OverlayScene resolved;for(auto q:o.scene().primitives()){q.version=versionOf(q,doc.id());q.source=ObjectRef();resolved.scene().add(q);}return resolved;
}
Error Session::open(const std::string& path){
    Document scratch;std::string bytes;Error e=persistence::readFileBytes(path,&bytes);if(!e.ok())return e;
    if(bytes.find("<CDXML")!=std::string::npos)e=persistence::readCdxml(bytes,scratch);
    else {std::istringstream in(bytes);e=loadSafeNative(scratch,in);}
    if(!e.ok())return e;
    cancel();doc=std::move(scratch);sourcePath=path;history.clear();selected.clear();dirty=false;
    input.bindDocument(doc.id());refresh();fit();return Error();
}
Error Session::save(const std::string& path,bool cdxml){
    std::ostringstream out;Error e=cdxml?persistence::writeCdxml(doc,out):saveSafeNative(doc,out);
    if(!e.ok()){return e;} persistence::AtomicWriteOptions opts;opts.sourcePath=sourcePath;
    e=persistence::writeFileAtomic(path,out.str(),opts);if(e.ok())dirty=false;return e;
}
interaction::RouterOutcome Session::action(void* v,const interaction::ActionRequest& req){
    Session& s=*static_cast<Session*>(v);using A=interaction::HotkeyAction;Error err;
    auto chemistry=chemistryAction(s,req);if(chemistry!=interaction::RouterOutcome::NotHandled)return chemistry;
    switch(req.action){
    case A::Undo:err=s.undo();break;case A::Redo:err=s.undo(true);break;
    case A::EditCopy:{
        if(s.selected.empty()){s.status="没有选中对象";return interaction::RouterOutcome::Rejected;}
        selection::PublishReport rep=selection::copySelection(s.doc,s.selected,s.clipboard,true);
        if(!rep.published){s.status=rep.why;return interaction::RouterOutcome::Rejected;}
        s.status="已复制 "+std::to_string(rep.objectsEncoded)+" 个对象";break;}
    case A::EditCut:{
        if(s.selected.empty()){s.status="没有选中对象";return interaction::RouterOutcome::Rejected;}
        // Publish first: a clipboard refusal must leave the document untouched.
        selection::CutReport rep=selection::cutSelection(s.doc,s.selected,s.clipboard,&cutDelete,&s,0);
        if(!rep.ok()){s.status=rep.why;return interaction::RouterOutcome::Rejected;}
        s.selected.clear();s.status="已剪切 "+std::to_string(rep.deletedObjects)+" 个对象";break;}
    case A::EditPaste:{
        selection::PasteReport rep=selection::pasteSelection(s.doc,s.clipboard,Vec2(12,-12));
        if(!rep.pasted){s.status=rep.why;return interaction::RouterOutcome::Rejected;}
        s.selected=rep.inserted;s.status="已粘贴 "+std::to_string(rep.objectsInserted)+" 个对象";break;}
    case A::RepeatLastAction:err=s.history.repeat(s.doc);if(err.ok()){s.dirty=true;s.refresh();}break;
    case A::EditSelectAll:s.selected=s.all();break;
    case A::EditDelete:err=s.submit(commands::deleteObjects(s.selected),"delete selection");break;
    case A::ZoomFit:s.fit();break;
    case A::ZoomActualSize:{auto v=s.view();v.scale=1;s.setView(v);break;}
    case A::ZoomIn:case A::ZoomOut:{auto v=s.view();v.scale*=req.action==A::ZoomIn?1.25:.8;s.setView(v);break;}
    case A::ActivateMarqueeTool:case A::ActivateBondTool:case A::ActivateTextTool:case A::ActivateArrowTool:{
        std::string c=req.action==A::ActivateMarqueeTool?"chooseSelectionTool_Marquee":req.action==A::ActivateBondTool?"chooseBondTool_Solid":req.action==A::ActivateTextTool?"chooseEdittingTool_Text":"chooseArrowTool_Large_Solid";
        bool found=false;for(size_t i=0;i<s.entries.size();++i)if(s.entries[i].command==c){s.activate(i);found=true;break;}
        if(!found){return interaction::RouterOutcome::Unsupported;} break;
    }
    default:return interaction::RouterOutcome::Unsupported;
    }
    if(!err.ok()){s.status=err.message;return interaction::RouterOutcome::Rejected;}return interaction::RouterOutcome::Handled;
}
} }




