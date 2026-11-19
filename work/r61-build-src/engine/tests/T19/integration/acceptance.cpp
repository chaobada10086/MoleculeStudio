#include "next/integration/session.hpp"
#include "next/render/offscreen.hpp"
#include "next/text/text_edit.hpp"
#include <GL/gl.h>
#include <fstream>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <algorithm>
#include <cstdlib>
namespace r62 { namespace integration {
typedef std::chrono::steady_clock Clock;
static double ms(Clock::time_point t){return std::chrono::duration<double,std::milli>(Clock::now()-t).count();}
static void need(bool ok,const std::string& s){if(!ok)throw std::runtime_error(s);}
static void need(Error e){need(e.ok(),e.message);}
static std::string quoted(std::string s){size_t p=0;while((p=s.find('"',p))!=std::string::npos){s.insert(p,"\"");p+=2;}return "\""+s+"\"";}
static std::string native(const Document& d){std::ostringstream out;need(saveSafeNative(d,out));return out.str();}
static std::string evidenceDir;
static void roundtrip(const Document& d){auto bytes=native(d);Document restored;std::istringstream in(bytes);need(loadSafeNative(restored,in));
    if(restored.digest()!=d.digest()){std::ofstream(evidenceDir+"/native_before.txt")<<bytes;std::ofstream(evidenceDir+"/native_after.txt")<<native(restored);}
    need(restored.digest()==d.digest(),"native digest changed");}
static size_t ink(const std::vector<uint8_t>& pix){size_t n=0;for(size_t i=0;i+3<pix.size();i+=4)if(pix[i]<240||pix[i+1]<240||pix[i+2]<240)++n;return n;}
static render::RenderStats draw(Session& s,render::Backend& gl,int w=960,int h=640){s.renderView.widthPx=w;s.renderView.heightPx=h;auto v=s.view();v.viewportCenter=Vec2(w*.5,h*.5);s.setView(v);render::RenderStats stats;need(gl.draw(s.displayScene(),0,s.renderView,&stats));glFinish();need(glGetError()==GL_NO_ERROR,"GL error");return stats;}
static void gesture(Session& s,Vec2 a,Vec2 b){
    bool tool=s.entries[s.activeIndex].tool.get()!=0;auto before=s.doc.digest();size_t cursor=s.history.cursor();
    need(s.gesture(PointerPhase::Down,a));need(s.gesture(PointerPhase::Move,b));
    if(tool)need(s.doc.digest()==before&&s.history.cursor()==cursor,"tool preview modified document/history");
    need(s.gesture(PointerPhase::Up,b));
    if(tool&&s.doc.digest()!=before){auto after=s.doc.digest();need(s.history.cursor()==cursor+1,"one gesture must be one undo entry");need(s.undo());need(s.doc.digest()==before,"creation undo");need(s.undo(true));need(s.doc.digest()==after,"creation redo");}
}
static void perfrow(std::ofstream& out,int n,const std::string& stage,std::vector<double> v,const render::RenderStats& st,size_t rebuilds,size_t glyphs){
    std::sort(v.begin(),v.end());size_t p=std::min(v.size()-1,size_t(std::ceil(v.size()*.95))-1);
    out<<n<<','<<stage<<','<<v.size()<<','<<v[v.size()/2]<<','<<v[p]<<','<<rebuilds<<','<<glyphs<<','<<st.meshRebuilds<<','<<st.bvhRebuilds<<','<<st.drawCalls<<','<<st.triangles<<"\n";
}
int acceptance(const std::string& root,const std::string& out){
    evidenceDir=out;
    render::OffscreenSurface surf;std::string reason;need(surf.create(960,640,&reason),reason);
    std::unique_ptr<render::Backend> gl(render::createGLBackend());need(gl->initialize(&reason),reason);
    need(std::string(gl->name())=="GL-compat-fbo2x","2x SSAA is mandatory; no direct-render fallback accepted");
    // Absolute geometry assertion: SSAA must preserve device scale and y-down.
    // A non-empty image alone cannot detect half-size or mirrored rendering.
    render::Scene probe;render::Primitive mark;mark.kind=render::PrimKind::Rect;
    mark.points={Vec2(70,50),Vec2(90,70)};mark.style.fill=render::Paint(0xFF0000FF,true);mark.style.stroke.enabled=false;mark.bounds=Box(mark.points[0],mark.points[1]);probe.add(mark);
    render::View probeView;probeView.widthPx=960;probeView.heightPx=640;render::RenderStats probeStats;
    need(gl->draw(probe,0,probeView,&probeStats));std::vector<uint8_t> probePixels;need(gl->readPixels(0,0,960,640,&probePixels));
    auto pixel=[&](int x,int y,int component){return probePixels[((639-y)*960+x)*4+component];};
    need(pixel(80,60,0)>240&&pixel(80,60,1)<20,"GL projection does not match ViewTransform device coordinate");
    need(pixel(40,30,1)>240&&pixel(80,580,1)>240,"GL rendering is half-scaled or mirrored");
    need(render::writePng(out+"/coordinate_probe.png",960,640,probePixels));
    std::ofstream device(out+"/device.txt");device<<"GL_VENDOR="<<glGetString(GL_VENDOR)<<"\nGL_RENDERER="<<glGetString(GL_RENDERER)<<"\nGL_VERSION="<<glGetString(GL_VERSION)<<"\nbackend="<<gl->name()<<"\nviewport=960x640\nSSAA=2x\n";device.close();
    std::ofstream matrix(out+"/feature_matrix.csv");matrix<<"SourceCommand,OwnerTask,Status,Data,Render,HitEdit,Undo,Persistence,RoundTrip,Host,BusinessParameters,TestEvidence,Limitations\n";
    Session catalog;need(catalog.entries.size()==275,"275 source rows");size_t passed=0,unsupported=0,failed=0;
    std::ofstream details(out+"/tool_tests.txt");
    for(size_t i=0;i<catalog.entries.size();++i){
        Session s;auto& entry=s.entries[i];std::string limits=entry.limitation;bool data=false,rendered=false,edited=false,undo=false,persist=false;
        if(limits.empty())try{
            need(s.activate(i),"activate");
            if(entry.owner=="T05"){
                ObjectId a,b;need(s.doc.addAtom(Vec2(50,50),6,&a));need(s.doc.addAtom(Vec2(70,50),8,&b));s.refresh();
                if(entry.command.find("Eraser")!=std::string::npos){gesture(s,Vec2(50,50),Vec2(50,50));need(!s.doc.atoms().exists(a),"erase must remove atom");need(s.undo());need(s.doc.atoms().exists(a),"erase undo");}
                else if(entry.command.find("Lasso")!=std::string::npos){need(s.gesture(PointerPhase::Down,Vec2(20,20)));need(s.gesture(PointerPhase::Move,Vec2(90,20)));need(s.gesture(PointerPhase::Move,Vec2(90,90)));need(s.gesture(PointerPhase::Move,Vec2(20,90)));need(s.gesture(PointerPhase::Up,Vec2(20,20)));need(s.selected.size()==2,"lasso selection");}
                else {gesture(s,Vec2(20,20),Vec2(90,90));need(s.selected.size()==2,"marquee selection");}
            }else if(entry.command=="choosePenTool_EditCurve"){
                for(size_t k=0;k<s.entries.size();++k)if(s.entries[k].command=="choosePenTool_DrawCurve"){s.activate(k);gesture(s,Vec2(60,70),Vec2(145,125));break;}
                s.activate(i);gesture(s,Vec2(60,70),Vec2(60,70));gesture(s,Vec2(60,70),Vec2(65,75));
            }else gesture(s,Vec2(60,70),Vec2(145,125));
            need(s.doc.aliveObjectCount()>0,"gesture created no data");need(s.doc.validate());data=true;
            if(entry.owner=="T09"){
                need(s.doc.texts().size()>0,"text placement");text::TextEditSession edit;edit.begin(s.doc.runs(0));edit.selectAll();need(edit.insertUtf8("Cl Br 中文 H2O",text::EditStyle()),"T09 input");auto runs=edit.commit();
                need(s.submit(Command("T09 rich edit",kInvalidateAppearance,[runs](CommandContext& c){auto e=c.document.setTextRuns(0,runs);return e.ok()?CommandResult::success():CommandResult::fail(e.code,e.message);}),"text semantic edit"));
            }
            uint64_t before=s.doc.digest();size_t count=s.doc.aliveObjectCount();auto refs=s.all();
            Vec2 point;bool found=false;for(const auto& p:s.scene.primitives())if(p.source.valid()){point=(p.bounds.min+p.bounds.max)*.5;found=true;break;}
            if(found)need(s.pick(point).valid(),"hit query missed");
            need(s.move(refs,Vec2(9,7)));need(s.doc.digest()!=before,"move did not edit data");need(s.doc.aliveObjectCount()==count,"move changed object count");edited=true;
            uint64_t after=s.doc.digest();need(s.undo());need(s.doc.digest()==before,"undo must restore exact digest");need(s.undo(true));need(s.doc.digest()==after,"redo must restore exact digest");undo=true;
            roundtrip(s.doc);persist=true;
            std::ofstream sample(out+"/tools/item_"+std::to_string(i)+".r62doc",std::ios::binary);sample<<native(s.doc);need(bool(sample),"write per-tool parameter/semantic evidence");sample.close();
            s.fit();auto stats=draw(s,*gl);
            std::vector<uint8_t> pix;need(gl->readPixels(0,0,960,640,&pix));need(ink(pix)>0,"GPU frame blank");rendered=true;
            details<<i<<' '<<entry.command<<" PASS count="<<count<<" digest="<<after<<" primitives="<<s.scene.size()<<" triangles="<<stats.triangles<<" ink="<<ink(pix)<<"\n";
            ++passed;limits="Partial: translated-object editing; hit uses conservative bounds; detailed properties/handles and native parity not accepted; CDXML export may refuse";
        }catch(const std::exception& e){limits=std::string("FAILED / isolated: ")+e.what();++failed;details<<i<<' '<<entry.command<<' '<<limits<<"\n";}
        else {auto old=s.doc.digest();need(!s.activate(i),"unsupported tool activated");auto e=s.gesture(PointerPhase::Down,Vec2(10,10));need(!e.ok()&&s.doc.digest()==old&&s.history.entries()==0,"unsupported command changed data");details<<i<<' '<<entry.command<<" UNSUPPORTED refusal verified; data unchanged\n";++unsupported;}
        std::string status=entry.limitation.empty()?(data&&rendered&&edited&&undo&&persist?"Partial":"Unsupported"):"Unsupported";
        auto flag=[](bool v){return v?"Verified":"Unverified";};
        matrix<<quoted(entry.command)<<','<<entry.owner<<','<<status<<','<<flag(data)<<','<<flag(rendered)<<','<<flag(edited)<<','<<flag(undo)<<','<<flag(persist)<<','<<flag(persist)<<",Unverified,"<<quoted(persist?"out/T19/evidence/tools/item_"+std::to_string(i)+".r62doc (actual tested public records + preset extension parameters)":"Unsupported; no guessed parameter mapping")<<','<<quoted("tool_tests.txt row "+std::to_string(i)+"; acceptance.cpp per-source gesture/digest/GPU/native")<<','<<quoted(limits)<<"\n";
    }
    matrix.close();details.close();
    std::cout<<"PER SOURCE: "<<passed<<" passed partial, "<<unsupported<<" unsupported, "<<failed<<" failed\n";
    // One real mixed document, one model, one renderer and one transaction history.
    Session mixed;std::vector<std::string> wanted={"chooseBondTool_Solid","chooseEdittingTool_Text","chooseArrowTool_Large_Solid","chooseTableTool"};
    for(size_t i=0;i<mixed.entries.size();++i){const auto& e=mixed.entries[i];bool use=std::find(wanted.begin(),wanted.end(),e.command)!=wanted.end();
        use=use||(e.owner=="T11"&&e.command.find("Rectangle")!=std::string::npos)||(e.owner=="T11"&&e.command.find("Square")!=std::string::npos)||(e.owner=="T12"&&e.command.find("Orbital")!=std::string::npos)||(e.owner=="T14");
        if(!use)continue;
        bool tooMany=false;for(const auto& a:mixed.doc.bioShapes().records())if(a.isAlive()&&e.owner=="T14")tooMany=true;
        if(e.owner=="T12"&&mixed.doc.orbitals().size())tooMany=true;
        if(e.owner=="T11"&&e.command.find("Rectangle")!=std::string::npos&&mixed.doc.arrows().size()>2)tooMany=true;
        if(tooMany)continue;
        size_t k=mixed.history.entries();mixed.activate(i);Vec2 p(40+(k%4)*150,40+(k/4)*120);gesture(mixed,p,p+Vec2(100,70));
    }
    need(mixed.doc.atoms().size()>0&&mixed.doc.texts().size()>0&&mixed.doc.arrows().size()>0&&mixed.doc.tables().size()>0&&mixed.doc.orbitals().size()>0&&mixed.doc.bioShapes().size()>0,"mixed document classes missing");
    auto before=mixed.doc.digest();need(mixed.move(mixed.all(),Vec2(5,7)));auto after=mixed.doc.digest();need(mixed.undo());need(mixed.doc.digest()==before,"mixed undo");need(mixed.undo(true));need(mixed.doc.digest()==after,"mixed redo");
    roundtrip(mixed.doc);need(mixed.save(out+"/mixed.r62doc"));Session reopened;need(reopened.open(out+"/mixed.r62doc"));need(reopened.doc.digest()==after,"mixed saved digest");
    auto compact=draw(reopened,*gl);std::vector<uint8_t> pix;need(gl->readPixels(0,0,960,640,&pix));need(render::writePng(out+"/mixed.png",960,640,pix));
    _putenv("R62_T19_DISABLE_COALESCE=1");auto reference=draw(reopened,*gl);std::vector<uint8_t> refPixels;need(gl->readPixels(0,0,960,640,&refPixels));_putenv("R62_T19_DISABLE_COALESCE=");
    need(pix==refPixels,"coalescing changed mixed document pixels");
    std::ofstream(out+"/coalescing_pixel_equivalence.txt")<<"mixed RGBA bytes identical; calls="<<compact.drawCalls<<" vs "<<reference.drawCalls<<"; triangles="<<compact.triangles<<" vs "<<reference.triangles<<"\n";
    std::ofstream geom(out+"/mixed_geometry.txt");for(const auto& p:reopened.scene.primitives())geom<<int(p.source.kind)<<':'<<p.source.id<<' '<<p.bounds.min.x<<','<<p.bounds.min.y<<' '<<p.bounds.max.x<<','<<p.bounds.max.y<<' '<<int(p.kind)<<"\n";
    // Failed open must retain the current document and history.
    uint64_t digest=reopened.doc.digest();auto invalid=reopened.open(root+"/tests/T02/persistence/fixtures/truncated.cdxml");need(!invalid.ok()&&reopened.doc.digest()==digest,"failed open changed current document");
    // Unknown CDXML bytes survive native storage; writing edited unsupported content refuses.
    Session unknown;std::string path=root+"/tests/T02/persistence/fixtures/unknown.cdxml",bytes;need(persistence::readFileBytes(path,&bytes));need(unknown.open(path));roundtrip(unknown.doc);
    std::ostringstream exported;need(persistence::writeCdxml(unknown.doc,exported));need(exported.str()==bytes,"unknown source bytes not preserved");
    // T04 focus and stale gesture gates, using the real assembled session.
    auto old=mixed.doc.digest();mixed.input.setFocus(interaction::InputFocus::TextEntry);interaction::InputEvent evt;evt.kind=interaction::InputKind::KeyDown;evt.virtualKey=interaction::vk::Z;evt.modifiers=kModControl;evt.focus=interaction::InputFocus::TextEntry;mixed.input.handle(evt,&mixed);need(mixed.doc.digest()==old,"text focus leaked canvas undo");mixed.input.setFocus(interaction::InputFocus::Canvas);
    mixed.cancel();need(mixed.input.phase()==interaction::GesturePhase::Idle&&mixed.ghost.empty(),"cancel failed");
    std::ofstream perf(out+"/performance.csv");perf<<"Atoms,Operation,Samples,MedianMs,P95Ms,SceneRebuilds,GlyphSamples,MeshRebuildsLast,BVHRebuildsLast,DrawCallsLast,TrianglesLast\n";perf<<std::fixed<<std::setprecision(4);
    for(int n:{5000,50000}){
        Session s;std::string input=out+"/../fixtures/labeled"+std::to_string(n)+".cdxml";
        std::vector<double> times,firstTimes;render::RenderStats st;
        for(int k=0;k<7;++k){auto t=Clock::now();need(s.open(input));times.push_back(ms(t));auto first=Clock::now();st=draw(s,*gl);firstTimes.push_back(ms(first));}
        need(s.doc.stats().aliveAtoms==uint64_t(n),"large input count");
        need(s.doc.stats().aliveTexts>0,"large input must retain visible text");
        std::set<ObjectId> visibleBonds;for(const auto& p:s.scene.primitives())if(p.source.kind==ObjectKind::Bond)visibleBonds.insert(p.source.id);
        need(visibleBonds.size()==uint64_t(n-1),"every input bond must have geometry; no reduced scene allowed");
        perfrow(perf,n,"open_parse_scene_index",times,st,s.rebuilds,s.glyphs.stats().misses);
        perfrow(perf,n,"first_GL_frame_after_open",firstTimes,st,0,0);
        std::vector<uint8_t> p;need(gl->readPixels(0,0,960,640,&p));need(render::writePng(out+"/chain"+std::to_string(n)+".png",960,640,p));
        for(const std::string stage:{"static_GL","pan_GL","zoom_GL","uncoalesced_reference_GL"}){
            times.clear();size_t rebuild=s.rebuilds,glyph=s.glyphs.stats().misses;
            if(stage=="uncoalesced_reference_GL")_putenv("R62_T19_DISABLE_COALESCE=1");
            for(int k=0;k<30;++k){auto v=s.view();if(stage=="pan_GL")v.center.x+=k%2?1:-1;if(stage=="zoom_GL")v.scale*=k%2?1.01:1/1.01;s.setView(v);auto start=Clock::now();st=draw(s,*gl);times.push_back(ms(start));}
            _putenv("R62_T19_DISABLE_COALESCE=");
            perfrow(perf,n,stage,times,st,s.rebuilds-rebuild,s.glyphs.stats().misses-glyph);
        }
        for(size_t i=0;i<s.entries.size();++i)if(s.entries[i].command=="chooseSelectionTool_Marquee")s.activate(i);
        for(bool all:{false,true}){
            s.selected=all?s.all():std::vector<ObjectRef>{ObjectRef(ObjectKind::Atom,0)};Vec2 a=s.doc.atoms().get(0)->position;auto initial=s.doc.digest();auto count=s.history.entries();
            need(s.gesture(PointerPhase::Down,a));times.clear();size_t rebuild=s.rebuilds,glyph=s.glyphs.stats().misses;
            for(int k=0;k<30;++k){auto start=Clock::now();need(s.gesture(PointerPhase::Move,a+Vec2(8+k*.03,5)*s.view().docPerDevice()));st=draw(s,*gl);times.push_back(ms(start));}
            need(s.doc.digest()==initial&&s.history.entries()==count,"preview mutated document or history");
            Box previewBounds=s.displayScene().bounds();
            perfrow(perf,n,all?"all_drag_input_preview_GL":"single_drag_input_preview_GL",times,st,s.rebuilds-rebuild,s.glyphs.stats().misses-glyph);
            need(s.gesture(PointerPhase::Up,a+Vec2(8.87,5)*s.view().docPerDevice()));Box committed=s.scene.bounds();
            std::cout<<"preview bounds "<<n<<" all="<<all<<" preview="<<previewBounds.min.x<<','<<previewBounds.min.y<<','<<previewBounds.max.x<<','<<previewBounds.max.y<<" commit="<<committed.min.x<<','<<committed.min.y<<','<<committed.max.x<<','<<committed.max.y<<"\n";
            need(std::abs(previewBounds.min.x-committed.min.x)<.001&&std::abs(previewBounds.max.x-committed.max.x)<.001,"preview/commit bounds differ");
            need(s.undo());need(s.doc.digest()==initial,"pointer drag undo");
        }
        for(bool all:{false,true}){
            times.clear();auto refs=all?s.all():std::vector<ObjectRef>{ObjectRef(ObjectKind::Atom,0)};uint64_t initial=s.doc.digest();size_t rebuild=s.rebuilds,glyph=s.glyphs.stats().misses;
            for(int k=0;k<9;++k){auto start=Clock::now();need(s.move(refs,Vec2(1,2)));times.push_back(ms(start));need(s.undo());need(s.doc.digest()==initial,"large move undo digest");}
            perfrow(perf,n,all?"all_drag_release_scene_index":"single_drag_release_scene_index",times,render::RenderStats(),s.rebuilds-rebuild,s.glyphs.stats().misses-glyph);
        }
        times.clear();for(int k=0;k<9;++k){need(s.move({ObjectRef(ObjectKind::Atom,0)},Vec2(2,3)));auto start=Clock::now();need(s.undo());times.push_back(ms(start));need(s.undo(true));need(s.undo());}perfrow(perf,n,"undo_scene_index",times,render::RenderStats(),9,0);
        times.clear();for(int k=0;k<9;++k){need(s.move({ObjectRef(ObjectKind::Atom,0)},Vec2(2,3)));need(s.undo());auto start=Clock::now();need(s.undo(true));times.push_back(ms(start));need(s.undo());}perfrow(perf,n,"redo_scene_index",times,render::RenderStats(),9,0);
        times.clear();for(int k=0;k<7;++k){auto start=Clock::now();need(s.save(out+"/chain"+std::to_string(n)+".r62doc"));times.push_back(ms(start));}perfrow(perf,n,"save_atomic_verify",times,render::RenderStats(),0,0);
        Session loaded;need(loaded.open(out+"/chain"+std::to_string(n)+".r62doc"));need(loaded.doc.digest()==s.doc.digest(),"large saved digest");
        std::ostringstream unchanged;need(persistence::writeCdxml(s.doc,unchanged));std::string original;need(persistence::readFileBytes(input,&original));need(unchanged.str()==original,"unchanged large CDXML bytes");
    }
    perf.close();
    std::ofstream summary(out+"/summary.json");summary<<"{\"source_rows\":275,\"partial_passed\":"<<passed<<",\"unsupported\":"<<unsupported<<",\"failed\":"<<failed<<",\"mixed\":true,\"large_semantics\":true,\"host_bound\":false,\"full_input_latency_measured\":false}";
    gl->destroy();std::cout<<"MIXED / UNKNOWN / FOCUS / LARGE SEMANTICS: PASS\n";return failed?1:0;
}
} }

