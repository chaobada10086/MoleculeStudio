#include "session.hpp"
#include "interop.hpp"
#include "ring_geometry.hpp"
#include "engine/molecular_render.hpp"
#include "engine/cdxml_graphics.hpp"
#include "source_tools.hpp"
#include "engine/local_scene_update.hpp"
#include "engine/valence_warnings.hpp"
#include "engine/graphic_style.hpp"
#include "tlc_preferences.hpp"
#include "alkyne_sprout.hpp"
#include "engine/sprout_layout.hpp"
#include "next/arrows_curves/render_bridge.hpp"
#include "next/graphics/render_bridge.hpp"
#include "next/biodraw/render_bridge.hpp"
#include "next/symbols_orbitals/render_bridge.hpp"
#include "next/experimental/render_bridge.hpp"
#include "next/experimental/model.hpp"
#include "next/selection/selection_set.hpp"
#include "next/selection/transform.hpp"
#include "next/chemistry/edit.hpp"
#include "next/advanced/perspective.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <set>
#include <unordered_map>
#include <queue>
#include <sstream>
#include <cmath>
#include <cstring>
#include <stdexcept>
namespace r62 { namespace integration {
static double traceNow(){return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();}
static bool traceOn(){static const bool on=std::getenv("MOLECULE_STUDIO_PERF_TRACE")!=nullptr;return on;}
static void checked(Error e) {if(!e.ok()) throw std::runtime_error(e.message);}
static Box box(Vec2 a,Vec2 b) {return Box(Vec2(std::min(a.x,b.x),std::min(a.y,b.y)),Vec2(std::max(a.x,b.x),std::max(a.y,b.y)));}
static spatial::Box sb(const Box& b){return spatial::Box{b.min.x,b.min.y,b.max.x,b.max.y};}
static double dist(Vec2 a,Vec2 b){return std::hypot(a.x-b.x,a.y-b.y);}
static double lineDist(Vec2 p,Vec2 a,Vec2 b){Vec2 v=b-a; double n=v.x*v.x+v.y*v.y; double t=n?((p.x-a.x)*v.x+(p.y-a.y)*v.y)/n:0; return dist(p,a+v*std::max(0.0,std::min(1.0,t)));}
#include "bond_hydrogens.hpp"
static Error unavailable(const std::string& x){return Error(ErrorCode::NotImplemented,x);}
// Work from the unmodified drag-start document for both preview and commit.
// Selected atoms are handles; bond lengths come from the existing drawing.
static Error fixedLengthDrag(Document& d,const std::vector<ObjectRef>& refs,Vec2 delta){
    auto expanded=studioio::expanded(d,refs);
    std::set<ObjectId> selectedAtoms;
    std::vector<ObjectRef> extras;
    for(auto r:expanded){
        if(r.kind==ObjectKind::Atom&&d.atoms().alive(r.id))selectedAtoms.insert(r.id);
        else if(r.kind!=ObjectKind::Atom&&r.kind!=ObjectKind::Bond&&r.kind!=ObjectKind::Group)extras.push_back(r);
    }
    if(selectedAtoms.empty())return translateDocumentObjects(d,refs,delta);
    struct Edge {ObjectId a,b;double length;};
    std::vector<Edge> allEdges;
    std::map<ObjectId,std::vector<ObjectId>> adjacency;
    for(const auto& b:d.bonds().records())if(b.isAlive()){
        auto a=d.atoms().alive(b.beginAtom),z=d.atoms().alive(b.endAtom);if(!a||!z)continue;
        allEdges.push_back({b.beginAtom,b.endAtom,dist(a->position,z->position)});
        adjacency[b.beginAtom].push_back(b.endAtom);adjacency[b.endAtom].push_back(b.beginAtom);
    }
    std::set<ObjectId> component=selectedAtoms;std::queue<ObjectId> queue;
    for(auto id:selectedAtoms)queue.push(id);
    while(!queue.empty()){auto id=queue.front();queue.pop();for(auto n:adjacency[id])if(component.insert(n).second)queue.push(n);}
    std::vector<Edge> edges,boundary;
    std::map<ObjectId,Vec2> original,pos;
    for(auto id:component)original[id]=pos[id]=d.atoms().alive(id)->position;
    for(auto e:allEdges)if(component.count(e.a)&&component.count(e.b)){
        edges.push_back(e);if(bool(selectedAtoms.count(e.a))!=bool(selectedAtoms.count(e.b)))boundary.push_back(e);
    }
    if(boundary.size()==1){
        // A freely rotating branch should leave its attachment atom stationary.
        auto e=boundary.front();auto inside=selectedAtoms.count(e.a)?e.a:e.b;auto outside=inside==e.a?e.b:e.a;
        Vec2 pivot=original[outside],v=original[inside]-pivot,target=v+delta;
        double angle=std::atan2(v.x*target.y-v.y*target.x,v.x*target.x+v.y*target.y);
        double c=std::cos(angle),s=std::sin(angle);
        for(auto id:selectedAtoms){Vec2 q=original[id]-pivot;pos[id]=pivot+Vec2(q.x*c-q.y*s,q.x*s+q.y*c);}
    }else if(boundary.empty()){
        for(auto id:selectedAtoms)pos[id]=original[id]+delta;
    }else{
        // Multiple attachments, including rings: hold the dragged atoms at the
        // requested offset and relax connected atoms against every bond length.
        for(auto id:selectedAtoms)pos[id]=original[id]+delta;
        double scale=1;for(auto e:edges)scale=std::max(scale,e.length);
        const double tolerance=scale*1e-8;
        bool converged=false;
        for(int iteration=0;iteration<1200;++iteration){
            for(auto e:edges){
                bool pinA=selectedAtoms.count(e.a)!=0,pinB=selectedAtoms.count(e.b)!=0;
                if(pinA&&pinB)continue;
                Vec2 v=pos[e.b]-pos[e.a];double n=core::length(v);
                if(n<1e-14){v=original[e.b]-original[e.a];n=core::length(v);if(n<1e-14){v=Vec2(1,0);n=1;}}
                Vec2 correction=v*((core::length(pos[e.b]-pos[e.a])-e.length)/n);
                if(!pinA&&!pinB){pos[e.a]=pos[e.a]+correction*.5;pos[e.b]=pos[e.b]-correction*.5;}
                else if(!pinA)pos[e.a]=pos[e.a]+correction;
                else pos[e.b]=pos[e.b]-correction;
            }
            double worst=0;for(auto e:edges)worst=std::max(worst,std::abs(core::length(pos[e.b]-pos[e.a])-e.length));
            if(worst<=tolerance){converged=true;break;}
        }
        // Translation of the connected component is always a valid solution.
        // Never silently commit stretched bonds if relaxation cannot converge.
        if(!converged)for(auto id:component)pos[id]=original[id]+delta;
    }
    if(!extras.empty()){Error e=translateDocumentObjects(d,extras,delta);if(!e.ok())return e;}
    for(auto pair:pos)if(dist(pair.second,original[pair.first])>1e-12){Error e=d.setAtomPosition(pair.first,pair.second);if(!e.ok())return e;}
    return Error();
}
// The delete half of a cut. T05 publishes first and only then calls this, so a
// refused clipboard publish never reaches the document.
static Error cutDelete(void* userData,const std::vector<ObjectRef>& refs){
    Session& s=*static_cast<Session*>(userData);
    return s.submit(commands::deleteObjects(refs),"cut selection");
}
static void appendRich(const text::RichLayout& layout,ObjectRef ref,uint32_t color,uint32_t layer,int z,std::vector<render::Primitive>* out,const std::vector<uint32_t>* colors=nullptr){
    render::Primitive p;p.kind=render::PrimKind::Polygon;p.source=ref;p.layer=render::LayerId(layer);p.z=z;
    p.style.fill=render::Paint(color,true);p.style.stroke.enabled=false;p.style.fillRule=render::FillRule::NonZero;
    // T03 v10000 expects one outer contour followed by its holes. Separate
    // disconnected glyph components (Latin letters / CJK strokes / dots), then
    // feed each containment group through the existing T03 triangulator.
    for(const auto& glyph:layout.glyphs){if(colors&&glyph.runIndex<colors->size())p.style.fill.rgba=(*colors)[glyph.runIndex];const auto& cs=glyph.contours;
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
Session::Session():dirty(false),fixedLen(true),fixedAngle(true),dragFixedLength(false),whiteCanvas(false),serial(1),viewSerial(1),activeIndex(0),rebuilds(0),
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
    const bool orderOnly=tag=="堆叠顺序";
    std::map<ObjectRef,int> oldOrder;
    if(orderOnly)for(const auto& p:scene.primitives())if(p.source.valid()&&!oldOrder.count(p.source)){
        auto value=static_cast<const Document&>(doc).extensions(p.source).findValue("studio.z");oldOrder[p.source]=value?std::atoi(value->c_str()):0;
    }
    Command effective;const Command* command=&c;
    if(tag=="graphics.placeShape"||tag=="graphics.placeBracket"||tag=="graphics.placeTable"||tag=="arrows.add"||tag=="curves.draw"||tag=="experimental.placeFigure"||tag=="resolved bond"||tag=="bond.tool"||tag=="rings_chains.ring.create"||tag=="rings_chains.chain.create"||tag=="T09 text placement"||tag=="原位文字"||tag=="symbols.add"||tag=="orbitals.add"||tag=="push.add"||tag=="biodraw.place"||tag=="稠合环"||tag=="添加结构片段"||tag=="插入缩写"){
        const double width=graphicsWidth;const uint32_t color=graphicsColor;
        effective=Command(c.name(),c.invalidation(),[c,width,color](CommandContext& context){studio::graphic_style::Counts before(context.document);auto result=c.execute(context);if(!result.ok())return result;auto e=before.styleNew(context.document,width,color);return e.ok()?result:CommandResult::fail(e.code,e.message);});
        command=&effective;
    }
    if(bond_hydrogens::editing(tag)){
        const Command base=*command;
        effective=Command(base.name(),base.invalidation()|kInvalidateSemantics|kInvalidateAppearance,[base](CommandContext& context){
            bond_hydrogens::Before before(context.document);auto result=base.execute(context);if(!result.ok())return result;
            auto e=before.apply(context.document);return e.ok()?result:CommandResult::fail(e.code,e.message);
        });command=&effective;
    }
    CommandContext ctx(doc);ctx.view=&renderView.transform;ctx.tag=tag;const size_t entriesBefore=history.entries();const double tExec0=traceNow();Error e=history.execute(doc,*command,&ctx);const double tExec=traceNow()-tExec0;
    if(e.ok()){dirty=true;
        if(orderOnly){
            local_scene::orderCache(*this).owner=nullptr;
            for(auto& p:scene.mutablePrimitives())if(p.source.valid()){
                auto value=static_cast<const Document&>(doc).extensions(p.source).findValue("studio.z");p.z+=(value?std::atoi(value->c_str()):0)-oldOrder[p.source];
            }
            std::stable_sort(scene.mutablePrimitives().begin(),scene.mutablePrimitives().end(),[](const render::Primitive& a,const render::Primitive& b){return a.layer.value!=b.layer.value?a.layer.value<b.layer.value:a.z<b.z;});
        }
        const double tRefresh0=traceNow();
        // A command that changed nothing (for example pressing the element key
        // that is already set) produces no patch.  The scene is untouched, so
        // rebuilding it here would only freeze the UI; skip it unless the
        // command claims it changed structure/geometry without a patch.
        // A no-op command does not create a history entry, so lastChangeSet()
        // still points at the PREVIOUS edit.  Compare the log length instead of
        // looking at the patch.
        const ChangeSet& patch=history.lastChangeSet();
        const bool noop=history.entries()==entriesBefore;
        const uint32_t invalidation=command->invalidation();
        if(orderOnly)refresh(false);
        else if(!(noop&&!(invalidation&(kInvalidateStructure|kInvalidateGeometry))))local_scene::refresh(*this,patch);
        if(traceOn())std::fprintf(stderr,"[submit] tag=%s exec=%.1f refresh=%.1f\n",tag.c_str(),tExec,traceNow()-tRefresh0);
    }else status=e.message;return e;
}
Error Session::undo(bool redo){
    cancel();
    // Undo moves the history cursor, so preserve the patch before moving it.
    ChangeSet change;if(!redo)change=history.lastChangeSet();
    Error e=redo?history.redo(doc):history.undo(doc);
    if(e.ok()){dirty=true;local_scene::refresh(*this,redo?history.lastChangeSet():change);}return e;
}
std::vector<ObjectRef> Session::all() const {
    std::vector<ObjectRef> r;
#define ADD(s,k) for(const auto& x:doc.s().records())if(x.isAlive())r.push_back(ObjectRef(ObjectKind::k,x.id));
    ADD(atoms,Atom) ADD(bonds,Bond) ADD(texts,Text) ADD(paths,Path) ADD(arrows,Arrow) ADD(brackets,Bracket)
    ADD(orbitals,Orbital) ADD(symbols,Symbol) ADD(bioShapes,BioShape) ADD(tables,Table) ADD(groups,Group)
#undef ADD
    // Select All means every OBJECT. A group is one object, so its members are
    // collapsed to the group; otherwise deleting "everything" would leave the
    // group holding dead member references.
    return topLevelSelection(r);
}
template<class T> static void captionKeyScalar(std::string& key,const T& value){key.append(reinterpret_cast<const char*>(&value),sizeof(value));}
Error Session::buildScene(){
    auto& orderCache=local_scene::orderCache(*this);
    const auto* update=local_scene::pending();
    const bool partial=update&&update->owner==this&&orderCache.matches(*this);
    std::vector<std::pair<uint64_t,render::Primitive>> ordered;
    if(partial){ordered.reserve(scene.size()+32);for(size_t i=0;i<scene.size();++i)
        if(!update->contains(scene.primitives()[i].source))ordered.emplace_back(orderCache.keys[i],std::move(scene.mutablePrimitives()[i]));}
    scene.clear();
    std::vector<render::Primitive> ps;
    std::vector<uint64_t> psOrder,sceneOrder,resolvedOrder;
    std::map<ObjectRef,uint32_t> graphicOrdinals;
    auto stamp=[&](uint64_t stage){while(psOrder.size()<ps.size()){size_t i=psOrder.size();auto ref=ps[i].source;
        uint64_t key=(stage<<56)|(uint64_t(ref.id)<<20);
        if(stage==4){uint64_t kind=ref.kind==ObjectKind::Text?0:ref.kind==ObjectKind::Arrow?1:ref.kind==ObjectKind::Path?2:ref.kind==ObjectKind::Bracket?3:ref.kind==ObjectKind::Orbital?4:ref.kind==ObjectKind::Symbol?5:6;key|=(kind<<52)|graphicOrdinals[ref]++;}
        psOrder.push_back(key);}};
    auto wanted=[&](ObjectKind kind,ObjectId id){return !partial||update->contains(ObjectRef(kind,id));};
    // Captions do not change when a bond or an unrelated atom is edited. Keep
    // their original contours, before visibility, rotation and camera passes.
    // An exact binary key avoids accepting a hash collision as a cache hit.
    struct CaptionCacheEntry {std::string key;std::vector<render::Primitive> primitives;};
    struct CaptionCache {const Session* owner=nullptr;DocumentId document=0;std::map<ObjectId,CaptionCacheEntry> entries;};
    static thread_local CaptionCache captions;
    if(captions.owner!=this||captions.document!=doc.id()){captions.entries.clear();captions.owner=this;captions.document=doc.id();}
    if(captions.entries.size()>doc.texts().size()+256)captions.entries.clear();
    auto colorsFor=[&](ObjectRef r){std::vector<uint32_t> colors;auto v=static_cast<const Document&>(doc).extensions(r).findValue("studio.text.colors");if(v){std::istringstream in(*v);uint32_t c;while(in>>c)colors.push_back(c);}return colors;};
    const double tScene0=traceNow();
    Document noMolecule;const bool graphicsOnly=partial&&update->atoms.empty()&&update->bonds.empty();
    RingGeometry rings(graphicsOnly?noMolecule:doc);
    const double tRings=traceNow()-tScene0;
    auto atomLabels=molecular::labels(doc,glyphs,rings,partial?&update->atoms:nullptr);
    const double tAtomLabels=traceNow()-tScene0;
    auto bondLabels=molecular::clippingLabels(doc,atomLabels);
    const double tLabels=traceNow()-tScene0;
    for(const auto& atom:doc.atoms().records())if(atom.isAlive()&&wanted(ObjectKind::Atom,atom.id)){
        ObjectRef ref(ObjectKind::Atom,atom.id);auto raw=static_cast<const Document&>(doc).extensions(ref).findValue("studio.atom.tags");
        if(!raw)continue;tinyxml2::XMLDocument tags;if(tags.Parse(raw->c_str())!=tinyxml2::XML_SUCCESS)continue;
        for(auto tag=tags.RootElement()->FirstChildElement("tag");tag;tag=tag->NextSiblingElement("tag")){
            std::vector<TextRun> runs;for(auto node=tag->FirstChildElement("s");node;node=node->NextSiblingElement("s")){
                TextRun run;run.utf8=node->GetText()?node->GetText():"";run.fontName=node->Attribute("font")?node->Attribute("font"):"Arial";
                run.size=node->DoubleAttribute("size");run.flags=node->UnsignedAttribute("flags");runs.push_back(run);
            }
            text::LayoutOptions options;options.triangulateGlyphs=false;options.defaultFont=doc.defaultFont();text::RichLayout measured,placed;
            checked(text::layoutRichText(runs,Vec2(),options,&glyphs,&measured));
            Vec2 baseline=atom.position+Vec2(tag->DoubleAttribute("dx"),tag->DoubleAttribute("dy"));
            checked(text::layoutRichText(runs,baseline-Vec2(0,measured.metrics.firstBaseline),options,&glyphs,&placed));
            auto name=tag->Attribute("name");if(name&&std::string(name).find("MoleculeStudio.NMR.")==0&&!runs.empty()&&runs.front().utf8.find("#")==0){
                auto ink=measured.metrics.inkBox;if(!ink.empty()){ink.min=ink.min+placed.anchor;ink.max=ink.max+placed.anchor;Vec2 endpoint(std::max(ink.min.x,std::min(atom.position.x,ink.max.x)),std::max(ink.min.y,std::min(atom.position.y,ink.max.y)));Vec2 direction=endpoint-atom.position;double distance=length(direction);if(distance>5){direction=direction*(1/distance);render::Primitive leader;leader.kind=render::PrimKind::Segment;leader.source=ref;leader.layer=render::LayerId(100);leader.z=9;leader.points={atom.position+direction*2,endpoint-direction*1.5};leader.style.stroke=render::Paint(0x89959dff,true);leader.style.strokeWidth=.35;leader.style.fill.enabled=false;ps.push_back(leader);}}
            }
            appendRich(placed,ref,tag->UnsignedAttribute("color",0x000000ff),100,11,&ps);
        }
    }
    stamp(0);
    for(const auto& label:atomLabels)if(wanted(ObjectKind::Atom,label.first)){ObjectRef ref(ObjectKind::Atom,label.first);auto colors=colorsFor(ref);uint32_t color=0x000000ff;if(auto value=static_cast<const Document&>(doc).extensions(ref).findValue("studio.color"))color=uint32_t(std::strtoull(value->c_str(),0,10));appendRich(label.second.layout,ref,color,100,10,&ps,&colors);}
    // Radical electrons are geometry, not a text glyph: one filled dot for a
    // monoradical, two for a diradical. The dot is derived from atom semantics
    // every rebuild, so it follows the atom and cannot be left behind by a move.
    const double radicalEm=molecular::setting(doc,"LabelSize",10);
    const double radicalRadius=std::max(0.65,radicalEm*0.13);
    for(const auto& atom:doc.atoms().records())if(atom.isAlive()&&atom.semantics.radical&&wanted(ObjectKind::Atom,atom.id)){
        ObjectRef ref(ObjectKind::Atom,atom.id);
        uint32_t color=0x000000ff;if(auto value=static_cast<const Document&>(doc).extensions(ref).findValue("studio.color"))color=uint32_t(std::strtoull(value->c_str(),0,10));
        Vec2 center=atom.position+Vec2(radicalEm*0.55,-radicalEm*0.55);
        auto labelled=atomLabels.find(atom.id);
        if(labelled!=atomLabels.end()&&!labelled->second.layout.metrics.inkBox.empty()){
            const Box& ink=labelled->second.layout.metrics.inkBox;
            center=Vec2(ink.max.x+radicalRadius*1.4,ink.min.y+radicalRadius*0.2);
        }
        const bool pair=atom.semantics.radical==uint8_t(Radical::Triplet)||atom.semantics.radical==uint8_t(Radical::Singlet);
        for(int dot=0;dot<(pair?2:1);++dot){
            Vec2 c=center;if(pair)c.x+=(dot==0?-radicalRadius*1.65:radicalRadius*1.65);
            render::Primitive p;p.kind=render::PrimKind::Polygon;p.source=ref;p.layer=render::LayerId(100);p.z=12;
            p.style.fill=render::Paint(color,true);p.style.stroke.enabled=false;
            for(int i=0;i<32;++i){double t=6.283185307179586*double(i)/32.0;p.points.push_back(c+Vec2(std::cos(t)*radicalRadius,std::sin(t)*radicalRadius));}
            p.bounds=render::geom::computeBounds(p,0.6);ps.push_back(p);
        }
    }
    stamp(1);
    const double tAtomStage=traceNow()-tScene0;
    for(const auto& b:doc.bonds().records())if(b.isAlive()&&wanted(ObjectKind::Bond,b.id)){
        const auto* a=doc.atoms().alive(b.beginAtom);const auto* z=doc.atoms().alive(b.endAtom);if(!a||!z)return unavailable("dangling bond");
        bonds::BondAxes axes;axes.order=b.order;axes.display=b.display;axes.semantics=b.semantics;axes.stereo=b.stereo;
        axes.side=bonds::sideOf(static_cast<const Document&>(doc).extensions(ObjectRef(ObjectKind::Bond,b.id)));
        auto data=rings.build(b);
        molecular::clip(data,b,bondLabels,molecular::setting(doc,"MarginWidth",1.6));
        bonds::RenderAdapterOptions opts;opts.style.width=opts.viewDefaultWidth=doc.defaultLineWidth();
        if(auto c=static_cast<const Document&>(doc).extensions(ObjectRef(ObjectKind::Bond,b.id)).findValue("studio.color"))opts.color=uint32_t(std::strtoull(c->c_str(),0,10));
        std::vector<render::Primitive> bondPrimitives;
        bonds::toPrimitives(data,ObjectRef(ObjectKind::Bond,b.id),opts,&bondPrimitives);
        ps.insert(ps.end(),bondPrimitives.begin(),bondPrimitives.end());
    }
    stamp(2);
    const double tBondStage=traceNow()-tScene0;
    // T09 emits real font contours including rich runs (the GL backend has no injected font API).
    text::LayoutOptions lo;
    for(const auto& a:doc.atoms().records())if(a.isAlive()&&wanted(ObjectKind::Atom,a.id)){
        auto label=static_cast<const Document&>(doc).extensions(ObjectRef(ObjectKind::Atom,a.id)).findValue("studio.atom.number");if(!label)continue;
        render::Primitive p;p.kind=render::PrimKind::Text;p.source=ObjectRef(ObjectKind::Atom,a.id);p.layer=render::LayerId(100);p.z=20;p.text=*label;p.textSize=8;p.fontName="Arial";p.points={a.position+Vec2(4,12)};p.style.fill=render::Paint(0x555555FF,true);p.style.stroke.enabled=false;ps.push_back(p);
    }
    stamp(3);
    if(!partial)for(const auto& t:doc.texts().records())if(t.isAlive()){
        lo=text::LayoutOptions();lo.triangulateGlyphs=false;lo.defaultSize=t.size?t.size:doc.defaultTextSize();lo.defaultFont=t.format.fontName.empty()?doc.defaultFont():t.format.fontName;lo.defaultFlags=t.format.flags;lo.align=core::TextAlign(t.format.align>3?0:t.format.align);
        auto width=static_cast<const Document&>(doc).extensions(ObjectRef(ObjectKind::Text,t.id)).findValue("studio.text.width");if(width)lo.widthLimit=std::atof(width->c_str());
        auto lineHeight=static_cast<const Document&>(doc).extensions(ObjectRef(ObjectKind::Text,t.id)).findValue("studio.text.LineHeight");if(lineHeight&&std::atof(lineHeight->c_str())>0){lo.spacing=text::LineSpacing::Fixed;lo.fixedLineHeight=std::atof(lineHeight->c_str());}
        std::string key;
        auto string=[&](const std::string& value){size_t n=value.size();captionKeyScalar(key,n);key.append(value);};
        captionKeyScalar(key,t.position.x);captionKeyScalar(key,t.position.y);captionKeyScalar(key,t.format.align);captionKeyScalar(key,t.format.colorRGBA);
        captionKeyScalar(key,lo.defaultSize);string(lo.defaultFont);captionKeyScalar(key,lo.defaultFlags);captionKeyScalar(key,lo.widthLimit);captionKeyScalar(key,lo.spacing);captionKeyScalar(key,lo.fixedLineHeight);
        const auto& runs=doc.runs(t.id);captionKeyScalar(key,runs.size());
        for(const auto& run:runs){string(run.utf8);string(run.fontName);captionKeyScalar(key,run.size);captionKeyScalar(key,run.flags);}
        auto colors=colorsFor(ObjectRef(ObjectKind::Text,t.id));captionKeyScalar(key,colors.size());for(auto color:colors)captionKeyScalar(key,color);
        auto& cached=captions.entries[t.id];
        if(cached.key==key){ps.insert(ps.end(),cached.primitives.begin(),cached.primitives.end());continue;}
        text::RichLayout measured;checked(text::layoutRichText(doc.runs(t.id),Vec2(),lo,&glyphs,&measured));Vec2 baseline=t.position;baseline.y-=measured.metrics.firstBaseline;if(t.format.align==1)baseline.x-=measured.metrics.width*.5;else if(t.format.align==2)baseline.x-=measured.metrics.width;
        text::RichLayout layout=std::move(measured);layout.anchor=baseline;
        if(!layout.metrics.inkBox.empty()){layout.metrics.inkBox.min=layout.metrics.inkBox.min+baseline;layout.metrics.inkBox.max=layout.metrics.inkBox.max+baseline;}
        uint32_t color=t.format.colorRGBA;if((color&255)==0)color=0x000000FF;
        size_t start=ps.size();appendRich(layout,ObjectRef(ObjectKind::Text,t.id),color,100,10,&ps,&colors);
        // A caption with a fixed wrap width is a TEXT BOX: the whole frame,
        // including the empty part, is the object. An invisible rectangle carries
        // that frame into picking, marquee selection and the selection overlay
        // without adding a border to the drawing itself.
        if(lo.widthLimit>0.0&&!layout.metrics.inkBox.empty()){
            const Box& ink=layout.metrics.inkBox;
            const double left=t.position.x-(t.format.align==1?lo.widthLimit*.5:t.format.align==2?lo.widthLimit:0.0);
            const double inset=std::max(1.0,lo.defaultSize*0.12);
            render::Primitive frame;frame.kind=render::PrimKind::Rect;frame.source=ObjectRef(ObjectKind::Text,t.id);
            frame.layer=render::LayerId(100);frame.z=0;
            frame.points={Vec2(left,ink.min.y-inset),Vec2(left+lo.widthLimit,ink.max.y+inset)};
            frame.style.fill.enabled=false;frame.style.stroke.enabled=false;
            frame.bounds=render::geom::computeBounds(frame,0.0);
            ps.push_back(frame);
        }
        for(size_t j=start;j<ps.size();++j)ps[j].source=ObjectRef(ObjectKind::Text,t.id);
        cached.key=std::move(key);cached.primitives.assign(ps.begin()+start,ps.end());
    }
    // A single preserved object without recognised render parameters must not
    // blank the whole page. Unrenderable objects are skipped and counted; every
    // other object still draws.
    size_t skipped=0;
    for(const auto& a:doc.arrows().records())if(a.isAlive()&&wanted(ObjectKind::Arrow,a.id)){
        ObjectRef r(ObjectKind::Arrow,a.id);if(cdgraphics::append(doc,r,a.geometry[0],&ps))continue;const auto& ex=static_cast<const Document&>(doc).extensions(r);
        if(ex.find("gfx.shape.params.1")){Error e=graphics::appendShapePrimitivesFor(doc,a.id,100,10,&ps);if(!e.ok())++skipped;}
        else if(ex.find("so.push.params.1")){symbols::PushParams p;bool ok=false;checked(symbols::readPush(doc,a.id,&p,&ok));if(!ok){++skipped;continue;}Error e=symbols::appendPushPrimitives(p,.6,r,100,&ps);if(!e.ok())++skipped;}
        else {arrows::ArrowParams p;bool ok=false;checked(arrows::readArrow(doc,a.id,&p,&ok));if(!ok){++skipped;continue;}Error e=arrows::appendArrow(p,doc.defaultLineWidth(),arrows::Tessellation(),r,100,&ps);if(!e.ok())++skipped;}
    }
    for(const auto& p:doc.paths().records())if(p.isAlive()&&wanted(ObjectKind::Path,p.id)){
        if(cdgraphics::append(doc,ObjectRef(ObjectKind::Path,p.id),p.points.empty()?Vec2():p.points[0],&ps))continue;
        std::vector<Vec2> pts;std::vector<uint8_t> kinds;bool closed=false,ok=false;arrows::CurveStyleSpec style;
        checked(arrows::readCurve(doc,p.id,&pts,&kinds,&closed,&style,&ok));
        ObjectRef ref(ObjectKind::Path,p.id);
        if(!ok){
            // Imported ChemDraw paths (group borders, lines, arcs) carry their
            // geometry directly on the PathRecord and have no arrows-curves
            // extension bag. Draw that geometry instead of refusing the ENTIRE
            // document: one preserved path used to blank every other object.
            if(p.points.size()<2)continue;
            render::Primitive q;q.kind=p.closed?render::PrimKind::Polygon:render::PrimKind::Polyline;
            q.points=p.points;q.source=ref;q.layer=render::LayerId(100);q.z=p.closed&&p.fillEnabled?5:10;
            q.style=render::styleFromLineStyle(p.style,doc.defaultLineWidth());
            q.style.fill.enabled=p.closed&&p.fillEnabled!=0;
            if(q.style.fill.enabled)q.style.fill=render::Paint(p.fillRGBA,true);
            ps.push_back(q);
            continue;
        }
        checked(arrows::appendCurve(pts,kinds,closed,style,doc.defaultLineWidth(),arrows::Tessellation(),ref,100,&ps));
    }
    for(const auto& a:doc.brackets().records())if(a.isAlive()&&wanted(ObjectKind::Bracket,a.id)){if(cdgraphics::append(doc,ObjectRef(ObjectKind::Bracket,a.id),a.geometry[0],&ps))continue;Error e=graphics::appendBracketPrimitivesFor(doc,a.id,100,10,10,&ps);if(!e.ok())++skipped;}
    if(!partial)for(const auto& a:doc.orbitals().records())if(a.isAlive()){
        if(cdgraphics::append(doc,ObjectRef(ObjectKind::Orbital,a.id),a.geometry[0],&ps))continue;
        symbols::OrbitalParams p;bool ok=false;checked(symbols::readOrbital(doc,a.id,&p,&ok));if(!ok){++skipped;continue;}
        Error e=symbols::appendOrbital(p,.6,symbols::Tessellation(),ObjectRef(ObjectKind::Orbital,a.id),100,&ps);if(!e.ok())++skipped;
    }
    if(!partial)for(const auto& a:doc.symbols().records())if(a.isAlive()){
        ObjectRef r(ObjectKind::Symbol,a.id);
        if(cdgraphics::append(doc,r,a.geometry[0],&ps))continue;
        if(experimental::isFigureObject(doc,r)){Error e=experimental::appendFigurePrimitivesFor(doc,a.id,100,10,0,lo,&ps);if(!e.ok())++skipped;}
        else if(static_cast<const Document&>(doc).extensions(r).find("so.push.params.1")){
            symbols::PushParams p;bool ok=false;checked(symbols::readPush(doc,a.id,&p,&ok));if(!ok){++skipped;continue;}
            Error e=symbols::appendPushPrimitives(p,.6,r,100,&ps);if(!e.ok())++skipped;
        } else {symbols::SymbolParams p;bool ok=false;checked(symbols::readSymbol(doc,a.id,&p,&ok));if(!ok){++skipped;continue;}
            Error e=symbols::appendSymbol(p,.6,symbols::Tessellation(),r,100,&ps);if(!e.ok())++skipped;}
    }
    if(!partial)for(const auto& a:doc.tables().records())if(a.isAlive()){Error e=graphics::appendTablePrimitivesFor(doc,a.id,100,10,&ps);if(!e.ok())++skipped;}
    stamp(4);
    const double tOtherStage=traceNow()-tScene0;
    for(size_t i=0;i<ps.size();++i){auto& p=ps[i];const auto& bag=static_cast<const Document&>(doc).extensions(p.source);auto visible=bag.findValue("studio.visible"),tv=bag.findValue("studio.text.Visible");if((visible&&*visible=="no")||(tv&&*tv=="no"))continue;p.bounds=render::geom::computeBounds(p,.6);scene.add(p);sceneOrder.push_back(psOrder[i]);}
    if(!partial)for(const auto& a:doc.bioShapes().records())if(a.isAlive()){Error e=biodraw::appendBioPrimitivesFor(doc,a.id,&scene);if(!e.ok())++skipped;}
    while(sceneOrder.size()<scene.size())sceneOrder.push_back((uint64_t(5)<<56)|sceneOrder.size());
    // All remaining Text primitives (element labels and table labels) also use T09.
    render::Scene resolved;
    for(size_t i=0;i<scene.size();++i){const auto& p=scene.primitives()[i];
        if(p.kind!=render::PrimKind::Text){resolved.add(p);resolvedOrder.push_back(sceneOrder[i]);continue;}
        text::RichLayout l;text::LayoutOptions opt;opt.triangulateGlyphs=false;opt.defaultSize=p.textSize>0?p.textSize:10;
        opt.defaultFont=p.fontName;
        checked(text::layoutPlainText(p.text,p.points.empty()?Vec2():p.points[0],opt,&glyphs,&l));
        std::vector<render::Primitive> v;appendRich(l,p.source,p.style.fill.enabled?p.style.fill.rgba:p.style.stroke.rgba,p.layer.value,p.z,&v);
        for(auto& q:v){q.source=p.source;q.bounds=render::geom::computeBounds(q,.6);resolved.add(q);resolvedOrder.push_back(sceneOrder[i]);}
    }
    auto camera=advanced::readCamera(doc);
    for(auto& p:resolved.mutablePrimitives()){
        if(p.source.kind==ObjectKind::Arrow){auto c=static_cast<const Document&>(doc).extensions(p.source).findValue("studio.color");if(c){uint32_t rgba=uint32_t(std::strtoull(c->c_str(),0,10));p.style.stroke.rgba=rgba;p.style.fill.rgba=rgba;}}
        if(auto color=static_cast<const Document&>(doc).extensions(p.source).findValue("studio.color.override")){uint32_t value=uint32_t(std::strtoull(color->c_str(),0,10));p.style.stroke.rgba=value;if(p.source.kind!=ObjectKind::Path&&(p.source.kind!=ObjectKind::Symbol||(!experimental::isFigureObject(doc,p.source)&&p.style.fill.rgba!=0xFFFFFFFFu)))p.style.fill.rgba=value;}
        if(p.source.kind!=ObjectKind::Bond)if(auto width=static_cast<const Document&>(doc).extensions(p.source).findValue("studio.graphics.width")){double value=std::atof(width->c_str());if(value>0){p.style.strokeWidth=value;p.bounds=render::geom::computeBounds(p,value);}}
        auto z=static_cast<const Document&>(doc).extensions(p.source).findValue("studio.z");if(z)p.z+=std::atoi(z->c_str());
        auto rotation=static_cast<const Document&>(doc).extensions(p.source).findValue("studio.text.RotationAngle");
        if(rotation&&(p.source.kind==ObjectKind::Text||p.source.kind==ObjectKind::Atom)){
            Vec2 origin=p.source.kind==ObjectKind::Text?doc.texts().get(p.source.id)->position:doc.atoms().get(p.source.id)->position;double angle=std::atof(rotation->c_str())/65536.*3.141592653589793/180.;
            auto rotate=[&](Vec2 v){v=v-origin;return origin+Vec2(v.x*cos(angle)-v.y*sin(angle),v.x*sin(angle)+v.y*cos(angle));};for(auto& v:p.points)v=rotate(v);for(auto& sub:p.subpaths)for(auto& v:sub.points)v=rotate(v);p.bounds=render::geom::computeBounds(p,.6);
        }
        if(camera.enabled){double depth=advanced::readObjectDepth(doc,p.source);for(auto& pt:p.points)pt=advanced::projectPoint(pt,depth,camera,camera.pivot);for(auto& sub:p.subpaths)for(auto& pt:sub.points)pt=advanced::projectPoint(pt,depth,camera,camera.pivot);p.bounds=render::geom::computeBounds(p,.6);}
    }
    // Batcher preserves scene order; it does not sort by layer or source Z.
    // Emit the painter's order here so shaded regions do not erase labels.
    const size_t retainedCount=ordered.size();
    for(size_t i=0;i<resolved.size();++i)ordered.emplace_back(resolvedOrder[i],std::move(resolved.mutablePrimitives()[i]));
    auto painterLess=[](const std::pair<uint64_t,render::Primitive>& a,const std::pair<uint64_t,render::Primitive>& b){
        if(a.second.layer.value!=b.second.layer.value)return a.second.layer.value<b.second.layer.value;
        if(a.second.z!=b.second.z)return a.second.z<b.second.z;
        return a.first<b.first;
    };
    // Retained primitives are already in painter order. Sort only the newly
    // generated portion and merge stably so equal keys preserve the old order.
    if(partial){
        auto middle=ordered.begin()+retainedCount;
        std::stable_sort(middle,ordered.end(),painterLess);
        std::inplace_merge(ordered.begin(),middle,ordered.end(),painterLess);
    }else std::stable_sort(ordered.begin(),ordered.end(),painterLess);
    scene.clear();scene.reserve(ordered.size());orderCache.keys.clear();orderCache.keys.reserve(ordered.size());
    for(auto& item:ordered){orderCache.keys.push_back(item.first);scene.mutablePrimitives().push_back(std::move(item.second));}
    scene.touch();orderCache.owner=this;orderCache.document=doc.id();orderCache.generation=scene.generation();
    if(traceOn())std::fprintf(stderr,"[scene] partial=%d rings=%.1f labels=%.1f clip=%.1f atoms=%.1f bonds=%.1f other=%.1f rest=%.1f total=%.1f\n",partial?1:0,tRings,tAtomLabels,tLabels,tAtomStage,tBondStage,tOtherStage,traceNow()-tScene0-tOtherStage,traceNow()-tScene0);
    if(skipped)status="Partial display: "+std::to_string(skipped)+" object(s) have no renderable geometry";
    return Error();
}
void Session::refresh(bool rebuildGeometry){
    ++serial;++rebuilds;input.notifyDocumentChanged(serial);
    const double tBuild0=traceNow();
    if(rebuildGeometry){Error e=buildScene();if(!e.ok())status="Partial display: "+e.message;}
    const double tBuild=traceNow()-tBuild0;
    // T03 v10000 keys all same-kind primitives of one ObjectRef as ordinal 0.
    // Expanded business objects need distinct cache slots. Preserve original refs
    // in scene for editing, and use anonymous ordinal primitives for GPU submission.
    // Only lookups are used; the counter iteration order never affects the
    // generated cache keys. Avoid a tree lookup per primitive on large pages.
    const double tKeys0=traceNow();
    std::unordered_map<uint64_t,uint32_t> cacheCounts;
    cacheCounts.reserve(scene.size());
    const auto* localUpdate=local_scene::pending();
    const bool reuseVersions=localUpdate&&localUpdate->owner==this&&local_scene::orderCache(*this).matches(*this);
    for(auto& p:scene.mutablePrimitives()){
        // Partial rebuilds preserve untouched primitives verbatim, including
        // their content hash. New or changed objects are always rehashed.
        if(!reuseVersions||localUpdate->contains(p.source)||!p.version)p.version=versionOf(p,doc.id());
        if(!p.source.valid()){p.cacheKey=0;continue;}
        uint64_t base=(uint64_t(p.source.kind)<<32)^p.source.id;
        uint32_t sub=cacheCounts[base]++;
        p.cacheKey=(base*0x9E3779B97F4A7C15ull)^(uint64_t(sub)+1);
    }
    const double tKeys=traceNow()-tKeys0;
    const double tGpu0=traceNow();
    // Rebuild the submission scene in place. Keeping the vector's elements alive
    // lets the copy p reuse each primitive's point/subpath capacity, which avoids
    // ~90k allocations per edit on a large document.
    {
        auto& dst=gpuScene.mutablePrimitives();
        dst.resize(scene.size()+2);
        auto paper=studioio::Page::get(doc);
        auto backdrop=[&](size_t at,Vec2 a,Vec2 z,uint32_t color,int order){render::Primitive p;p.kind=render::PrimKind::Rect;p.layer=render::LayerId(0);p.z=order;p.points={a,z};p.bounds=Box(a,z);p.style.fill=render::Paint(color,true);p.style.stroke.enabled=false;p.version=versionOf(p,doc.id());dst[at]=std::move(p);};
        backdrop(0,Vec2(-1e6,-1e6),Vec2(1e6,1e6),whiteCanvas?0xFFFFFFFFu:0xEEF0F2FFu,0);
        backdrop(1,Vec2(paper.x,paper.y),Vec2(paper.x+paper.width*paper.across,paper.y+paper.height*paper.down),0xFFFFFFFF,1);
        for(size_t i=0;i<scene.size();++i){dst[i+2]=scene.primitives()[i];dst[i+2].source=ObjectRef();}
        gpuScene.touch();
    }
    const double tGpu=traceNow()-tGpu0;
    const double tSpatial0=traceNow();
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
    const double tOb=traceNow();objectIndex.build(ob);const double tOa=traceNow();candidateIndex.build(cb);const double tCa=traceNow();
    if(traceOn())std::fprintf(stderr,"[spatial] loop=%.1f object=%.1f candidate=%.1f\n",tOb-tSpatial0,tOa-tOb,tCa-tOa);
    if(traceOn())std::fprintf(stderr,"[refresh] build=%.1f keys=%.1f gpu=%.1f total=%.1f prims=%zu\n",tBuild,tKeys,tGpu,traceNow()-tBuild0,scene.size());
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
    // Each non-atom uses the centre of its first primitive, in painter order.
    // Resolve all requested centres in one pass instead of rescanning the
    // entire scene for every candidate in a large lasso.
    std::set<ObjectRef> pending;
    std::map<ObjectRef,Vec2> centers;
    for(auto r:refs)if(r.kind!=ObjectKind::Atom)pending.insert(r);
    if(!pending.empty())for(const auto& p:scene.primitives()){
        auto it=pending.find(p.source);if(it==pending.end())continue;
        centers.emplace(p.source,(p.bounds.min+p.bounds.max)*.5);pending.erase(it);
        if(pending.empty())break;
    }
    for(auto r:refs){Vec2 center;
        if(r.kind==ObjectKind::Atom)center=doc.atoms().get(r.id)->position;
        else {auto it=centers.find(r);if(it==centers.end())continue;center=it->second;}
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
ObjectRef Session::topLevelRef(const ObjectRef& ref) const {
    if(!ref.valid())return ref;
    ObjectRef cursor=ref;std::set<ObjectId> visited;
    for(int depth=0;depth<128;++depth){
        ObjectId owner=kInvalidObjectId;
        for(const auto& group:doc.groups().records()){
            if(!group.isAlive())continue;
            if(std::find(group.members.begin(),group.members.end(),cursor)!=group.members.end()){owner=group.id;break;}
        }
        if(owner==kInvalidObjectId)break;
        if(!visited.insert(owner).second)break;
        cursor=ObjectRef(ObjectKind::Group,owner);
    }
    return cursor;
}
std::vector<ObjectRef> Session::topLevelSelection(const std::vector<ObjectRef>& refs) const {
    std::vector<ObjectRef> out;std::set<ObjectRef> seen;
    for(const auto& ref:refs){auto top=topLevelRef(ref);if(top.valid()&&seen.insert(top).second)out.push_back(top);}
    return out;
}
ObjectRef Session::pick(Vec2 p) const {
    auto a=nearestAtom(p,8);if(a!=kInvalidObjectId)return topLevelRef(ObjectRef(ObjectKind::Atom,a));
    auto b=nearestBond(p,7);if(b!=kInvalidObjectId)return topLevelRef(ObjectRef(ObjectKind::Bond,b));
    double r=view().deviceRadiusToDocument(4);auto refs=objectsInBox(Box(p-Vec2(r,r),p+Vec2(r,r)));
    // Atom/bond bounds are only a broad-phase box. A diagonal bond's empty
    // rectangle must not become a hit after the precise distance test failed.
    refs.erase(std::remove_if(refs.begin(),refs.end(),[](ObjectRef r){return r.kind==ObjectKind::Atom||r.kind==ObjectKind::Bond;}),refs.end());
    if(!refs.empty())return topLevelRef(refs.back());
    return ObjectRef();
}
void Session::fit(){auto b=scene.bounds();if(doc.atoms().size())for(const auto& a:doc.atoms().records())if(a.isAlive())b.add(a.position);
    ViewTransform v=view();v.center=(b.min+b.max)*.5;v.scale=std::min((renderView.widthPx-40)/std::max(20.0,b.width()),(renderView.heightPx-40)/std::max(20.0,b.height()))/v.dpiScale;setView(v);}
void Session::cancel(){if(activeIndex<entries.size()&&entries[activeIndex].tool)entries[activeIndex].tool->cancelGesture(*this);selecting=moving=bondDrawing=false;bondHysteresis.clear();ghost.clear();if(dragBaseActive){doc=dragBase;dragBaseActive=false;refresh();}input.unwind("T19 cancel");}
bool Session::activateCommand(const std::string& c){for(size_t i=0;i<entries.size();++i)if(entries[i].command==c)return activate(i);reportUnsupported(c);return false;}
bool Session::activate(size_t n){if(n>=entries.size())return false;cancel();activeIndex=n;auto& e=entries[n];input.notifyToolChanged(e.command);
    if(!e.limitation.empty()){reportUnsupported(e.command+": "+e.limitation);return false;}
    if(e.tool){e.tool->activate(*this);} status=e.command+" | standalone; host unbound";return true;}
Error Session::gesture(PointerPhase phase,Vec2 p,uint32_t mods){
    if(auto tool=dynamic_cast<arrows::ArrowTool*>(entries[activeIndex].tool.get()))tool->setStrokeWidth(graphicsWidth);
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
            if(bondOrigin.joined){auto direction=sprout_layout::resolve(doc,bondOrigin.target.id,angle);if(!direction.ok){ghost.clear();status=direction.rejectReason;if(phase==PointerPhase::Up)bondDrawing=false;return unavailable(status);}angle=direction.directionDeg;}
            target=bondOrigin.point+core::direction(angle)*doc.defaultBondLength();
        }
        auto profile=input.profile();profile.fixedLength=fixedLen;profile.fixedAngles=fixedAngle;
        // Automatic growth must not be rounded into an obstacle or snapped to
        // an unintended atom after the placement algorithm has checked it.
        if(click&&bondOrigin.joined){bondSnap=interaction::SnapResult();bondSnap.endpoint=target;}
        else bondSnap=interaction::SnapResolver::resolveBond(profile,bondOrigin.point,target,doc.defaultBondLength(),1/view().docPerDevice(),*this,mods,bondOrigin.target,bondHysteresis,0);
        bondHysteresis.previous=bondSnap.target;bondHysteresis.previousKind=bondSnap.joined?interaction::HotspotKind::Atom:interaction::HotspotKind::None;
        bondHysteresis.documentId=doc.id();bondHysteresis.geometrySerial=serial;
        ghost.clear();
        bonds::BondGeometryOptions bondOptions;bondOptions.lineWidth=doc.defaultLineWidth();bondOptions.boldWidth=molecular::setting(doc,"BoldWidth",2);bondOptions.hashSpacing=molecular::setting(doc,"HashSpacing",2.5);
        auto data=bonds::buildBondGeometry(bondOrigin.point,bondSnap.endpoint,preset->axes,bondOptions);
        for(const auto& shape:data.shapes){PreviewItem q;q.points=shape.fill?shape.outline:shape.points;q.primitive=shape.fill?PreviewPrimitive::Polygon:PreviewPrimitive::Polyline;q.style.width=shape.strokeWidth;q.style.colorRGBA=graphicsColor;q.fillRGBA=graphicsColor;q.fillEnabled=shape.fill;ghost.add(q);}
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
                if(!lane&&!figure.lanes.empty())lane=std::min_element(figure.lanes.begin(),figure.lanes.end(),[u](const experimental::Lane& a,const experimental::Lane& b){return std::abs(a.u-u)<std::abs(b.u-u);})->id;
                uint32_t spotId=0;auto added=experimental::figureAddSpot(&figure,lane,u,v,&spotId);if(!added.ok())return added;
                auto spot=experimental::mutableSpot(&figure,spotId);
                double diameter=studio_tlc::preferences().diameter;if(auto saved=static_cast<const Document&>(doc).extensions(experimental::figureRef(tlcFigure)).findValue("studio.tlc.spotDiameter"))diameter=std::atof(saved->c_str());
                if(figure.spots.size()>1){spot->radiusU=figure.spots.front().radiusU;spot->radiusV=figure.spots.front().radiusV;spot->fillRGBA=figure.spots.front().fillRGBA;spot->opacity=figure.spots.front().opacity;}else studio_tlc::resizeSpots(*spot,frame,diameter);
                ObjectId id=tlcFigure;return submit(Command("薄层板加点",kInvalidateStructure|kInvalidateGeometry,[id,figure](CommandContext& context){auto e=experimental::writeFigure(context.document,id,figure);return e.ok()?CommandResult::success():CommandResult::fail(e.code,e.message);}),"薄层板加点");
            }
            return Error();   // the drag is a spot, never a second board
        }
    }
    if(e.tool){PointerState s;s.position=p;s.rawPosition=view().toDevice(p);s.modifiers=mods;s.button=kButtonLeft;s.phase=phase;
        std::set<ObjectId> radicalBefore;
        if(phase==PointerPhase::Up&&e.command=="chooseSymbolTool_Radical")
            for(const auto& symbol:doc.symbols().records())if(symbol.isAlive())radicalBefore.insert(symbol.id);
        CommandResult r=phase==PointerPhase::Down?e.tool->beginGesture(*this,s,*this):phase==PointerPhase::Up?e.tool->endGesture(*this,s,*this):e.tool->updateGesture(*this,s,*this);
        // A radical dot is meant to be dragged like a text box. After placing
        // one, select it and hand control back to the selection tool so the very
        // next gesture moves the dot instead of dropping a second one. A dead
        // slot can be reused, so identify the dot by "alive now, not alive
        // before" rather than by the highest id.
        if(phase==PointerPhase::Up&&e.command=="chooseSymbolTool_Radical"&&r.error.ok()){
            ObjectId created=kInvalidObjectId;
            for(const auto& symbol:doc.symbols().records())
                if(symbol.isAlive()&&!radicalBefore.count(symbol.id)){created=symbol.id;break;}
            if(created!=kInvalidObjectId){
                selected={ObjectRef(ObjectKind::Symbol,created)};
                activateCommand("chooseSelectionTool_Marquee");
                status="自由基已放置，可直接拖动";
            }
        }
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
        Error placed=submit(commands::addText(p,doc.defaultTextSize(),"",&created),"T09 text placement");
        if(placed.ok()){createdTextId=created;selected={ObjectRef(ObjectKind::Text,created)};refresh();}
        return placed;
    }
    if(e.owner!="T05")return unavailable(e.command);
    if(phase==PointerPhase::Down){press=p;lasso.clear();lasso.push_back(p);ObjectRef hit=pick(p);
        if(e.command.find("Eraser")!=std::string::npos){if(!hit.valid())return Error();return submit(commands::deleteObjects(std::vector<ObjectRef>(1,hit)),"erase");}
        // The interior of the visible selection frame is draggable, including
        // empty space between atoms. An outside hit still starts a new selection.
        Box selectedBox=studioio::selectionBounds(*this);bool within=!selected.empty()&&studioio::inside(selectedBox,p,view().deviceRadiusToDocument(3));
        moving=hit.valid()||(within&&!(mods&kModShift));selecting=!moving;
        if(moving&&hit.valid()&&!within&&std::find(selected.begin(),selected.end(),hit)==selected.end()){if(!(mods&kModShift))selected.clear();addToSelection({hit});}
        else if(selecting&&!(mods&kModShift))selected.clear();
        if(moving){movingRefs=studioio::expanded(doc,selected);
            dragScene=gpuScene;if(dragFixedLength){dragBase=doc;dragBaseActive=true;}
            movingSlots.clear();movingBoundary=false;
            std::set<ObjectId> movingAtoms;
            for(auto ref:movingRefs)if(ref.kind==ObjectKind::Atom)movingAtoms.insert(ref.id);
            for(size_t i=0;i<scene.size();++i)if(movingRefs.count(scene.primitives()[i].source))
                movingSlots[scene.primitives()[i].source].push_back(i);
            // expanded() closes the selection over its bonds, so every bond that
            // is selected has both ends selected. The bond that leaves a dragged
            // fragment is therefore NOT selected, although one of its atoms is
            // moving: it has to be stretched during the drag, not left behind.
            for(const auto& b:doc.bonds().records())if(b.isAlive()&&
                bool(movingAtoms.count(b.beginAtom))!=bool(movingAtoms.count(b.endAtom))){movingBoundary=true;break;}
        }
    } else if(phase==PointerPhase::Move){if(selecting){if(e.command.find("Lasso")==std::string::npos&&lasso.size()>1)lasso.back()=p;else lasso.push_back(p);}ghost.clear();
        if(selecting){PreviewItem x;x.primitive=PreviewPrimitive::Rect;x.points={press,p};ghost.add(x);}
        if(moving){Vec2 delta=p-press;
            if(dist(p,press)<view().deviceRadiusToDocument(2))delta=Vec2();
            if(dragFixedLength&&dragBaseActive&&movingBoundary){
                doc=dragBase;
                Error result=fixedLengthDrag(doc,selected,delta);
                if(!result.ok()){doc=dragBase;refresh();dragScene=gpuScene;return result;}
                refresh();dragScene=gpuScene;
                return Error();
            }
            std::set<ObjectRef> emitted;
            const bool boundary=movingBoundary;
            if(!boundary&&dragScene.size()==gpuScene.size()){
                // The stationary scene was copied once on mouse-down. Update only
                // moving primitives from their original coordinates, with no drift.
                for(const auto& entry:movingSlots)for(size_t i:entry.second){
                    auto q=scene.primitives()[i];for(auto& v:q.points)v=v+delta;
                    for(auto& sub:q.subpaths)for(auto& v:sub.points)v=v+delta;
                    q.bounds.min=q.bounds.min+delta;q.bounds.max=q.bounds.max+delta;
                    q.version=versionOf(q,doc.id());q.source=ObjectRef();dragScene.mutablePrimitives()[i+2]=std::move(q);
                }
                dragScene.touch();return Error();
            }
            static const Session* ringOwner=nullptr;static DocumentId ringDocument=0;static uint64_t ringSerial=0;
            static std::unique_ptr<RingGeometry> cachedRings;
            if(ringOwner!=this||ringDocument!=doc.id()||ringSerial!=serial){
                cachedRings.reset(new RingGeometry(doc));ringOwner=this;ringDocument=doc.id();ringSerial=serial;
            }
            RingGeometry& rings=*cachedRings;
            // Rigid translations (including images) reuse all existing glyphs
            // and bond geometry. Only a stretched boundary bond needs labels.
            static const Session* labelOwner=nullptr;static DocumentId labelDocument=0;static uint64_t labelSerial=0;static molecular::Labels atomLabels;
            if(boundary&&(labelOwner!=this||labelDocument!=doc.id()||labelSerial!=serial)){
                // Only the labels around the stretched (boundary) bonds are
                // consulted by clip().  Laying out every label in the document
                // cost ~0.5 s on the first move of each drag of a large file.
                std::vector<ObjectId> stationary;
                for(const auto& b:doc.bonds().records())if(b.isAlive()){
                    bool begin=movingRefs.count(ObjectRef(ObjectKind::Atom,b.beginAtom))!=0;
                    bool end=movingRefs.count(ObjectRef(ObjectKind::Atom,b.endAtom))!=0;
                    if(begin!=end)stationary.push_back(begin?b.endAtom:b.beginAtom);
                }
                std::set<ObjectId> local;
                if(stationary.size()>48){
                    atomLabels=molecular::clippingLabels(doc,molecular::labels(doc,glyphs,rings));
                }else{
                    double cell=std::max(8.,doc.defaultTextSize());double radius=cell*2.2;double radius2=radius*radius;
                    for(const auto& atom:doc.atoms().records())if(atom.isAlive()){
                        bool keep=movingRefs.count(ObjectRef(ObjectKind::Atom,atom.id))!=0;
                        for(size_t i=0;!keep&&i<stationary.size();++i){
                            const auto* end=doc.atoms().alive(stationary[i]);
                            if(!end)continue;
                            Vec2 d=atom.position-end->position;
                            keep=d.x*d.x+d.y*d.y<=radius2;
                        }
                        if(keep)local.insert(atom.id);
                    }
                    for(size_t i=0;i<stationary.size();++i)local.insert(stationary[i]);
                    atomLabels=molecular::clippingLabels(doc,molecular::labels(doc,glyphs,rings,&local));
                }
                labelOwner=this;labelDocument=doc.id();labelSerial=serial;
            }
            // Keep stationary primitives in place while stretching boundary bonds.
            // If a bond changes primitive count, fall back to the general builder.
            if(dragScene.size()==gpuScene.size()){
                // A previous general rebuild may have regrouped bond primitives.
                // Restore the original slots before resuming sparse updates.
                for(size_t i=0;i<scene.size();++i)if(dragScene.primitives()[i+2].cacheKey!=scene.primitives()[i].cacheKey){
                    dragScene=gpuScene;break;
                }
                // Boundary bonds are not part of the selection, so movingSlots
                // does not know their primitives. Collect their slots from the
                // untouched scene; the dragged end of each one is re-built below.
                std::map<ObjectRef,std::vector<size_t>> boundarySlots;
                for(size_t i=0;i<scene.size();++i){
                    const auto& ref=scene.primitives()[i].source;
                    if(ref.kind!=ObjectKind::Bond||movingSlots.count(ref))continue;
                    const auto* bond=doc.bonds().alive(ref.id);if(!bond)continue;
                    if(bool(movingRefs.count(ObjectRef(ObjectKind::Atom,bond->beginAtom)))!=
                       bool(movingRefs.count(ObjectRef(ObjectKind::Atom,bond->endAtom))))boundarySlots[ref].push_back(i);
                }
                std::map<ObjectRef,std::vector<render::Primitive>> replacements;bool sameLayout=true;
                for(const auto& entry:movingSlots)if(entry.first.kind==ObjectKind::Bond){
                    const auto* bond=doc.bonds().alive(entry.first.id);if(!bond){sameLayout=false;break;}
                    const auto& b=*bond;
                    if(bool(movingRefs.count(ObjectRef(ObjectKind::Atom,b.beginAtom)))==bool(movingRefs.count(ObjectRef(ObjectKind::Atom,b.endAtom))))continue;
                    auto data=rings.build(b,&movingRefs,delta);
                    molecular::clip(data,b,atomLabels,molecular::setting(doc,"MarginWidth",1.6),&movingRefs,delta);
                    auto& v=replacements[entry.first];bonds::toPrimitives(data,entry.first,bonds::RenderAdapterOptions(),&v);
                    if(v.size()!=entry.second.size()){sameLayout=false;break;}
                }
                for(const auto& entry:boundarySlots){
                    if(!sameLayout)break;
                    const auto* bond=doc.bonds().alive(entry.first.id);if(!bond){sameLayout=false;break;}
                    auto data=rings.build(*bond,&movingRefs,delta);
                    molecular::clip(data,*bond,atomLabels,molecular::setting(doc,"MarginWidth",1.6),&movingRefs,delta);
                    auto& v=replacements[entry.first];bonds::toPrimitives(data,entry.first,bonds::RenderAdapterOptions(),&v);
                    if(v.size()!=entry.second.size()){sameLayout=false;break;}
                }
                if(sameLayout){
                    auto write=[&](const std::map<ObjectRef,std::vector<size_t>>& slots){
                        for(const auto& entry:slots)for(size_t k=0;k<entry.second.size();++k){
                            size_t i=entry.second[k];auto found=replacements.find(entry.first);
                            auto q=found!=replacements.end()?found->second[k]:scene.primitives()[i];
                            if(found==replacements.end()){
                                for(auto& point:q.points)point=point+delta;
                                for(auto& sub:q.subpaths)for(auto& point:sub.points)point=point+delta;
                                q.bounds.min=q.bounds.min+delta;q.bounds.max=q.bounds.max+delta;
                            }
                            q.cacheKey=scene.primitives()[i].cacheKey;q.version=versionOf(q,doc.id());q.source=ObjectRef();
                            dragScene.mutablePrimitives()[i+2]=std::move(q);
                        }
                    };
                    write(movingSlots);write(boundarySlots);
                    dragScene.touch();return Error();
                }
            }
            dragScene.clear();for(const auto& bg:gpuScene.primitives())if(bg.layer.value==0)dragScene.add(bg);
            for(size_t i=0;i<scene.size();++i){const auto& original=scene.primitives()[i];auto ref=original.source;
                const BondRecord* bond=ref.kind==ObjectKind::Bond?doc.bonds().alive(ref.id):nullptr;
                if(bond&&bool(movingRefs.count(ObjectRef(ObjectKind::Atom,bond->beginAtom)))!=
                         bool(movingRefs.count(ObjectRef(ObjectKind::Atom,bond->endAtom)))){
                    if(!emitted.insert(ref).second)continue;
                    const auto& b=*bond;
                    bonds::BondAxes axes;axes.order=b.order;axes.display=b.display;axes.semantics=b.semantics;axes.stereo=b.stereo;axes.side=bonds::sideOf(static_cast<const Document&>(doc).extensions(ref));
                    auto data=rings.build(b,&movingRefs,delta);molecular::clip(data,b,atomLabels,molecular::setting(doc,"MarginWidth",1.6),&movingRefs,delta);std::vector<render::Primitive> v;
                    bonds::toPrimitives(data,ref,bonds::RenderAdapterOptions(),&v);
                    for(auto q:v){q.version=versionOf(q,doc.id());q.source=ObjectRef();dragScene.add(q);}
                }else {auto q=original;if(movingRefs.count(ref)){
                        for(auto& v:q.points)v=v+delta;
                        for(auto& sub:q.subpaths)for(auto& v:sub.points)v=v+delta;
                        q.bounds.min=q.bounds.min+delta;q.bounds.max=q.bounds.max+delta;
                    }q.version=movingRefs.count(ref)?versionOf(q,doc.id()):gpuScene.primitives()[i+2].version;q.source=ObjectRef();dragScene.add(q);}
            }
        }
    } else if(phase==PointerPhase::Up){
        bool m=moving;moving=selecting=false;ghost.clear();
        if(m){if(dragFixedLength&&dragBaseActive){doc=dragBase;dragBaseActive=false;refresh();}Vec2 delta=p-press;if(dist(p,press)<view().deviceRadiusToDocument(2)){if(!pick(press).valid()&&!(mods&(kModShift|kModControl)))selected.clear();return Error();}return dragFixedLength?moveFixedLength(selected,delta):move(selected,delta);}
        lasso.push_back(p);bool polygon=e.command.find("Lasso")!=std::string::npos&&lasso.size()>2;Box region=box(press,p);auto contains=[&](Vec2 pt){return polygon?selection::polygonContainsPoint(lasso,pt):studioio::inside(region,pt);};auto candidates=polygon?objectsInPolygon(lasso):objectsInBox(region);std::vector<ObjectRef> chosen;for(auto r:candidates){if(r.kind==ObjectKind::Atom){if(contains(doc.atoms().get(r.id)->position))chosen.push_back(r);}else if(r.kind==ObjectKind::Bond){auto b=doc.bonds().alive(r.id);if(b&&contains(doc.atoms().get(b->beginAtom)->position)&&contains(doc.atoms().get(b->endAtom)->position))chosen.push_back(r);}else chosen.push_back(r);}addToSelection(topLevelSelection(chosen));
    }return Error();
}
Error Session::move(const std::vector<ObjectRef>& refs,Vec2 delta){
    return submit(Command("T19 move",kInvalidateGeometry,[refs,delta](CommandContext& c){Error e=translateDocumentObjects(c.document,refs,delta);return e.ok()?CommandResult::success():CommandResult::fail(e.code,e.message);}),"unified move");
}
Error Session::moveFixedLength(const std::vector<ObjectRef>& refs,Vec2 delta){
    if(!dragFixedLength)return move(refs,delta);
    return submit(Command("锁定键长拖动",kInvalidateGeometry,[refs,delta](CommandContext& c){
        Error e=fixedLengthDrag(c.document,refs,delta);
        return e.ok()?CommandResult::success():CommandResult::fail(e.code,e.message);
    }),"锁定键长拖动");
}
render::OverlayScene Session::overlay(){
    render::OverlayScene o;if(!moving)studioio::selectionOverlay(*this,o);size_t previewBegin=o.scene().size();builder.buildPreview(ghost,view(),&o);
    const auto& command=entries[activeIndex].command;
    bool graphic=command.find("chooseArrowTool_")==0||command.find("chooseDrawingTool_")==0||command.find("choosePenTool_")==0||command.find("chooseBracketTool_")==0||command=="chooseTableTool"||command=="chooseTLCPlateTool"||(!bondDrawing&&entries[activeIndex].owner!="T05"&&entries[activeIndex].owner!="T09");
    if(graphic)for(size_t i=previewBegin;i<o.scene().size();++i){auto& p=o.scene().mutablePrimitives()[i];p.style.stroke.rgba=graphicsColor;if(command!="chooseTLCPlateTool")p.style.fill.rgba=graphicsColor;if(graphicsWidth>0&&entries[activeIndex].owner!="T07"&&entries[activeIndex].owner!="T08")p.style.strokeWidth=graphicsWidth;p.bounds=render::geom::computeBounds(p,.6);}
    if(bondDrawing)for(auto& p:o.scene().mutablePrimitives())if(p.kind==render::PrimKind::Polygon&&p.style.fill.enabled)p.style.stroke.enabled=false;
    auto ring=[&](Vec2 p,bool joined){
        render::Primitive q;q.kind=render::PrimKind::Polyline;q.style.stroke=render::Paint(joined?0x15803DFF:0x2563EBFF,true);q.style.fill.enabled=false;q.style.strokeWidth=view().deviceRadiusToDocument(1.5);q.layer=render::LayerId(900);q.z=100;
        double r=view().deviceRadiusToDocument(joined?8:6);
        for(int i=0;i<=40;++i)q.points.push_back(p+Vec2(std::cos(i*6.283185307/40),std::sin(i*6.283185307/40))*r);
        q.bounds=render::geom::computeBounds(q,.6);o.scene().add(q);
        if(joined){q.kind=render::PrimKind::Segment;q.points={p-Vec2(r*.5,0),p+Vec2(r*.5,0)};o.scene().add(q);q.points={p-Vec2(0,r*.5),p+Vec2(0,r*.5)};o.scene().add(q);}
    };
    const auto& hs=input.hotspot();
    if(hs.valid()&&!moving&&!selecting){
        if(hs.kind==interaction::HotspotKind::Atom)ring(hs.point,false);
        else if(auto b=doc.bonds().alive(hs.object.id)){
            Vec2 a=doc.atoms().get(b->beginAtom)->position,z=doc.atoms().get(b->endAtom)->position,u=z-a;double len=dist(a,z);
            if(len>0){Vec2 n(-u.y/len,u.x/len);double r=view().deviceRadiusToDocument(4);
                render::Primitive q;q.kind=render::PrimKind::Polyline;q.points={a+n*r,z+n*r,z-n*r,a-n*r,a+n*r};q.style.stroke=render::Paint(0x2563EBFF,true);q.style.fill.enabled=false;q.style.strokeWidth=view().deviceRadiusToDocument(1);q.layer=render::LayerId(900);q.z=100;q.bounds=render::geom::computeBounds(q,.6);o.scene().add(q);}
        }
    }
    if(bondDrawing){ring(bondOrigin.point,false);ring(bondSnap.endpoint,bondSnap.joined);}
    // Recheck only after document edits, not on every mouse move or zoom.
    static const Session* warningOwner=nullptr;static DocumentId warningDocument=0;
    static uint64_t warningSerial=0;static std::vector<ObjectId> warningAtoms;
    static std::map<ObjectId,Box> warningBounds;
    if(warningOwner!=this||warningDocument!=doc.id()||warningSerial!=serial){
        warningAtoms=studio::validation::overValentAtoms(doc);
        warningBounds.clear();
        for(auto id:warningAtoms){Vec2 p=doc.atoms().alive(id)->position;warningBounds.emplace(id,Box(p,p));}
        // Include the actual element glyphs (and attached charge/isotope) so
        // the hollow square encloses labels as well as unlabelled vertices.
        for(const auto& q:scene.primitives())if(q.source.kind==ObjectKind::Atom){
            auto found=warningBounds.find(q.source.id);if(found!=warningBounds.end()){
                found->second.add(q.bounds.min);found->second.add(q.bounds.max);
            }
        }
        warningOwner=this;warningDocument=doc.id();warningSerial=serial;
    }
    for(auto id:warningAtoms){
        const auto* atom=doc.atoms().alive(id);if(!atom)continue;
        const Box bounds=studio::validation::warningSquare(warningBounds.at(id));
        Vec2 p=(bounds.min+bounds.max)*.5;
        if(moving&&movingRefs.count(ObjectRef(ObjectKind::Atom,id))&&!lasso.empty())p=p+lasso.back()-press;
        double radius=(bounds.max.x-bounds.min.x)*.5;
        render::Primitive q;q.kind=render::PrimKind::Polyline;
        q.points={p+Vec2(-radius,-radius),p+Vec2(radius,-radius),p+Vec2(radius,radius),p+Vec2(-radius,radius),p+Vec2(-radius,-radius)};
        q.style.fill.enabled=false;q.style.stroke=render::Paint(0xE02020FF,true);q.style.strokeWidth=.65;
        q.layer=render::LayerId(950);q.z=200;q.bounds=render::geom::computeBounds(q,q.style.strokeWidth);
        o.scene().add(q);
    }
    // Overlay cache has the same ordinal issue as content; geometry is the key.
    for(auto& q:o.scene().mutablePrimitives()){q.version=versionOf(q,doc.id());q.source=ObjectRef();}return o;
}
Error Session::open(const std::string& path){
    Document scratch;std::string bytes;Error e=persistence::readFileBytes(path,&bytes);if(!e.ok())return e;
    // ChemDraw XML may arrive as UTF-16 from Windows editors.
    if(bytes.size()>2 && ((unsigned char)bytes[0]==0xFF||(unsigned char)bytes[0]==0xFE)){
        bool little=(unsigned char)bytes[0]==0xFF;std::wstring wide;for(size_t i=2;i+1<bytes.size();i+=2){unsigned a=(unsigned char)bytes[i],b=(unsigned char)bytes[i+1];wide.push_back(wchar_t(little?(a|(b<<8)):(b|(a<<8))));}
        int n=WideCharToMultiByte(CP_UTF8,0,wide.data(),int(wide.size()),0,0,0,0);bytes.resize(n);WideCharToMultiByte(CP_UTF8,0,wide.data(),int(wide.size()),&bytes[0],n,0,0);
        auto enc=bytes.find("UTF-16");if(enc==std::string::npos)enc=bytes.find("utf-16");if(enc!=std::string::npos)bytes.replace(enc,6,"UTF-8");
    }
    if(bytes.find("<CDXML")!=std::string::npos){persistence::repairUnreferencedArrowIds(bytes);e=persistence::readCdxml(bytes,scratch);if(e.ok()){studioio::readPage(bytes,scratch);e=studioio::readArrows(bytes,scratch);}}
    else {std::istringstream in(bytes);e=loadSafeNative(scratch,in);}
    if(!e.ok())return e;
    cancel();doc=std::move(scratch);sourcePath=path;history.clear();selected.clear();dirty=false;
    input.bindDocument(doc.id());refresh();fit();return Error();
}
Error Session::save(const std::string& path,bool cdxml){
    std::ostringstream out;std::string xml;Error e;if(cdxml){e=studioio::exportXml(doc,&xml);out<<xml;}else e=saveSafeNative(doc,out);
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




