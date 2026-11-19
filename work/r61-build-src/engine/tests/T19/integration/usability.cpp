#include "next/integration/session.hpp"
#include "next/integration/ring_geometry.hpp"
#include "next/render/offscreen.hpp"
#include <GL/gl.h>
#include <fstream>
#include <sstream>
#include <iostream>
#include <cmath>
namespace r62 { namespace integration {
using namespace interaction;
static size_t checks;
static void expect(bool v,const std::string& msg){++checks;if(!v)throw std::runtime_error("R2: "+msg);}
static void good(Error e){expect(e.ok(),e.message);}
static bool close(Vec2 a,Vec2 b){return core::length(a-b)<1e-7;}
static void pointer(Session& s,Vec2 p){InputEvent e;e.kind=InputKind::PointerMove;e.focus=InputFocus::Canvas;e.devicePosition=s.view().toDevice(p);e.documentId=s.doc.id();e.viewGeneration=s.viewSerial;s.input.handle(e,&s);}
static void key(Session& s,uint32_t c,uint32_t modifiers=0,bool vk=false){InputEvent e;e.kind=vk?InputKind::KeyDown:InputKind::Character;e.character=vk?0:c;e.virtualKey=vk?c:0;e.modifiers=modifiers;e.focus=InputFocus::Canvas;e.documentId=s.doc.id();s.input.refreshHotspot(&s);s.input.handle(e,&s);}
static void nativeRoundtrip(Session& s,const std::string& p){auto digest=s.doc.digest();good(s.save(p));Session check;good(check.open(p));expect(check.doc.digest()==digest,"native reopened digest");}
static void benzene(Session& s,Vec2 pos){expect(s.activateCommand("chooseRingTool_Benzene"),"benzene command");good(s.gesture(PointerPhase::Down,pos));good(s.gesture(PointerPhase::Move,pos));expect(s.doc.stats().aliveAtoms==0,"preview must not mutate");expect(s.ghost.items().size()==9,"preview includes 6 outer and 3 inner lines");good(s.gesture(PointerPhase::Up,pos));}
static void verifyRing(Session& s){
    expect(s.doc.stats().aliveAtoms==6&&s.doc.stats().aliveBonds==6,"benzene topology 6/6");
    RingGeometry geo(s.doc);size_t doubles=0;Vec2 center;for(auto a:s.doc.atoms().records())if(a.isAlive())center=center+a.position;center=center*(1./6);
    for(auto b:s.doc.bonds().records())if(b.isAlive()){
        auto data=geo.build(b);auto a=s.doc.atoms().get(b.beginAtom)->position,z=s.doc.atoms().get(b.endAtom)->position;
        expect(close(data.shapes[0].points[0],a)&&close(data.shapes[0].points[1],z),"continuous outer edge at exact endpoints");
        if(b.order==BondOrder::Double){++doubles;expect(data.shapes.size()==2,"double two lines");auto mid=(data.shapes[1].points[0]+data.shapes[1].points[1])*.5;
            expect(core::length(mid-center)<core::length((a+z)*.5-center),"double is inward");expect(core::length(data.shapes[1].points[1]-data.shapes[1].points[0])<core::length(z-a),"inner ends shortened");}
    }expect(doubles==3,"three real alternating doubles");
}
static void picture(Session& s,render::Backend& gl,const std::string& path,bool overlay=false){render::RenderStats st;auto over=s.overlay();good(gl.draw(s.displayScene(),overlay?&over.scene():0,s.renderView,&st));glFinish();std::vector<uint8_t> pix;good(gl.readPixels(0,0,960,640,&pix));good(render::writePng(path,960,640,pix));expect(glGetError()==GL_NO_ERROR,"OpenGL no errors");}
int usabilityAcceptance(const std::string& out){
    checks=0;Session s;s.renderView.widthPx=960;s.renderView.heightPx=640;auto v=s.view();v.viewportCenter=Vec2(480,320);v.scale=3;s.setView(v);
    pointer(s,Vec2(150,150));key(s,'j');expect(s.entries[s.activeIndex].command=="chooseRingTool_Benzene","generic j tool");
    auto before=s.doc.digest();benzene(s,Vec2());verifyRing(s);auto ringDigest=s.doc.digest();good(s.undo());expect(s.doc.digest()==before,"ring single undo");good(s.undo(true));expect(s.doc.digest()==ringDigest,"ring redo");verifyRing(s);nativeRoundtrip(s,out+"/benzene.r62doc");
    render::OffscreenSurface surface;std::string reason;expect(surface.create(960,640,&reason),reason);std::unique_ptr<render::Backend> gl(render::createGLBackend());expect(gl->initialize(&reason),reason);expect(std::string(gl->name())=="GL-compat-fbo2x","2x SSAA unchanged");
    // Recreate previous geometry from the identical document/view/quality for A/B.
    render::Scene fixed=s.gpuScene,previous;
    for(auto b:s.doc.bonds().records())if(b.isAlive()){
        bonds::BondAxes axes;axes.order=b.order;axes.semantics=b.semantics;auto data=bonds::buildBondGeometry(s.doc.atoms().get(b.beginAtom)->position,s.doc.atoms().get(b.endAtom)->position,axes,bonds::BondGeometryOptions());
        std::vector<render::Primitive> ps;bonds::toPrimitives(data,ObjectRef(),bonds::RenderAdapterOptions(),&ps);for(auto p:ps)previous.add(p);
    }
    s.gpuScene=previous;picture(s,*gl,out+"/benzene_before.png");s.gpuScene=fixed;picture(s,*gl,out+"/benzene_after.png");
    std::ofstream geometry(out+"/benzene_geometry.csv");geometry<<"bond,order,line,x1,y1,x2,y2\n";RingGeometry geo(s.doc);for(auto b:s.doc.bonds().records())if(b.isAlive()){auto data=geo.build(b);for(size_t i=0;i<data.shapes.size();++i){auto a=data.shapes[i].points[0],z=data.shapes[i].points[1];geometry<<b.id<<','<<int(b.order)<<','<<i<<','<<a.x<<','<<a.y<<','<<z.x<<','<<z.y<<'\n';}}
    // All-select drag uses the same ring projection; preview is read-only.
    auto atom=s.doc.atoms().get(0)->position;s.selected=s.all();s.activateCommand("chooseSelectionTool_Marquee");good(s.gesture(PointerPhase::Down,atom));good(s.gesture(PointerPhase::Move,atom+Vec2(8,6)));expect(s.doc.digest()==ringDigest,"drag preview read-only");picture(s,*gl,out+"/benzene_drag.png");good(s.gesture(PointerPhase::Up,atom+Vec2(8,6)));verifyRing(s);good(s.undo());expect(s.doc.digest()==ringDigest,"drag exact undo");
    // Reversing endpoints must not put the inner double outside the ring.
    std::vector<ObjectId> ids;for(auto b:s.doc.bonds().records())if(b.isAlive())ids.push_back(b.id);good(s.submit(bonds::reverseBondDirectionCommand(ids),"reverse test"));verifyRing(s);good(s.undo());
    pointer(s,atom+Vec2(1,0));expect(s.input.hotspot().kind==HotspotKind::Atom,"atom capture wins over adjacent edge");expect(!s.overlay().scene().empty(),"visible atom hover");picture(s,*gl,out+"/atom_hotspot.png",true);
    key(s,'n');expect(s.doc.atoms().get(0)->element==7,"n is nitrogen");key(s,'C',kModShift);expect(s.doc.atoms().get(0)->element==17,"C is chlorine");key(s,'c');expect(s.doc.atoms().get(0)->element==6,"c is carbon");
    key(s,' ' ,0,true);expect(s.selected.size()==12,"hotspot space selects connected molecule");s.selected.clear();
    auto bond=s.doc.bonds().get(0);auto a=s.doc.atoms().get(bond->beginAtom)->position,z=s.doc.atoms().get(bond->endAtom)->position;
    pointer(s,a*.7+z*.3);expect(s.input.hotspot().kind==HotspotKind::Bond,"whole segment hotspot away from midpoint");picture(s,*gl,out+"/bond_hotspot.png",true);
    key(s,'3');expect(s.doc.bonds().get(0)->order==BondOrder::Triple,"bond 3 edits order");good(s.undo());key(s,'2');expect(s.doc.bonds().get(0)->order==BondOrder::Double,"bond 2 edits order");
    auto digest=s.doc.digest();key(s,'k');expect(s.doc.digest()==digest,"unknown bond k leaves data");
    pointer(s,atom);digest=s.doc.digest();key(s,'k');expect(s.doc.digest()==digest&&s.input.stats().unsupported>0,"ambiguous atom k explicitly unsupported");
    s.input.setFocus(InputFocus::TextEntry);InputEvent text;text.kind=InputKind::Character;text.character='n';text.focus=InputFocus::TextEntry;s.input.handle(text,&s);expect(s.doc.digest()==digest,"text focus never changes element");s.input.setFocus(InputFocus::Canvas);
    s.input.unwind("focus lost test");expect(!s.input.hotspot().valid(),"focus clears hotspot");
    // Exact existing coordinate wins over fixed length / angle in BOTH preview and commit.
    Session join;join.renderView=s.renderView;ObjectId j0,j1;good(join.doc.addAtom(Vec2(0,0),6,&j0));good(join.doc.addAtom(Vec2(31,7),8,&j1));join.refresh();join.activateCommand("chooseBondTool_Solid");
    good(join.gesture(PointerPhase::Down,Vec2(.5,.2)));good(join.gesture(PointerPhase::Move,Vec2(31.5,7.4)));expect(join.bondSnap.joined&&close(join.bondSnap.endpoint,Vec2(31,7)),"exact joined preview");picture(join,*gl,out+"/green_join.png",true);auto target=join.bondSnap.endpoint;
    good(join.gesture(PointerPhase::Up,Vec2(31.5,7.4)));expect(join.doc.stats().aliveAtoms==2&&join.doc.stats().aliveBonds==1,"join reuses two stable atoms");expect(close(join.doc.atoms().get(j1)->position,target),"commit equals resolved preview");nativeRoundtrip(join,out+"/joined.r62doc");good(join.undo());expect(join.doc.stats().aliveBonds==0,"joined bond undo");
    Session constraints;constraints.renderView=s.renderView;constraints.activateCommand("chooseBondTool_Solid");good(constraints.gesture(PointerPhase::Down,Vec2()));good(constraints.gesture(PointerPhase::Move,Vec2(50,18)));expect(std::abs(core::length(constraints.bondSnap.endpoint)-constraints.doc.defaultBondLength())<1e-7,"fixed length preview");auto endpoint=constraints.bondSnap.endpoint;good(constraints.gesture(PointerPhase::Up,Vec2(50,18)));expect(close(constraints.doc.atoms().get(1)->position,endpoint),"fixed preview commit same");key(constraints,'L',kModControl,true);expect(!constraints.fixedLen,"Ctrl L toggle");key(constraints,'E',kModControl,true);expect(!constraints.fixedAngle,"Ctrl E toggle");
    // Fused benzene has ten atoms / eleven edges, never twelve stacked atoms.
    Session fused;fused.renderView=s.renderView;fused.setView(fused.view());benzene(fused,Vec2());auto fb=fused.doc.bonds().get(0);auto fa=fused.doc.atoms().get(fb->beginAtom)->position,fz=fused.doc.atoms().get(fb->endAtom)->position;
    pointer(fused,(fa+fz)*.5);auto fd=fused.doc.digest();key(fused,'a');
    expect(fused.doc.digest()==fd&&fused.status.find("valence")!=std::string::npos,"aromatic fusion needs re-Kekulization: explicit refusal preserves data");
    // T08 currently supports fusion on a saturated edge. Keep the aromatic
    // refusal above in the results; never label naphthalene as implemented.
    Session saturated;saturated.renderView=s.renderView;saturated.setView(saturated.view());saturated.activateCommand("chooseRingTool_Cyclohexane");
    good(saturated.gesture(PointerPhase::Down,Vec2()));good(saturated.gesture(PointerPhase::Up,Vec2()));
    auto sb=saturated.doc.bonds().get(0);auto sa=saturated.doc.atoms().get(sb->beginAtom)->position,sz=saturated.doc.atoms().get(sb->endAtom)->position;
    pointer(saturated,(sa+sz)*.5);fd=saturated.doc.digest();key(saturated,'a');expect(saturated.doc.stats().aliveAtoms==10&&saturated.doc.stats().aliveBonds==11,"benzene fused on saturated edge reuses two atoms and one edge");
    picture(saturated,*gl,out+"/saturated_edge_fusion.png");nativeRoundtrip(saturated,out+"/saturated_edge_fusion.r62doc");good(saturated.undo());expect(saturated.doc.digest()==fd,"supported fusion one undo");
    std::ofstream result(out+"/usability.json");result<<"{\"passed\":true,\"checks\":"<<checks<<",\"backend\":\"GL-compat-fbo2x\",\"host_bound\":false}";gl->destroy();std::cout<<"R2 usability PASS "<<checks<<" checks\n";return 0;
}
} }
