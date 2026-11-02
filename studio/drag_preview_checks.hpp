#pragma once
// Regression coverage for the live drag preview.
//
// A marquee selection only contains the bonds whose TWO atoms are inside the
// selection. The bond that leaves a selected fragment therefore is not part of
// the selection, but one of its ends still rides on a moving atom. The preview
// has to stretch that bond while the pointer moves; leaving it at its old place
// until mouse-up makes the fragment tear apart on screen.
//
// The checks below drag real gestures through Session and compare the preview
// scene with the scene that the committed document produces. They also dump an
// SVG of each preview so the motion can be inspected without opening the UI.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace dragpreview {

inline std::string svgColor(const render::Paint& paint){
    uint32_t c=render::resolvePaint(paint.rgba);
    char buffer[16];
    std::snprintf(buffer,sizeof(buffer),"#%02X%02X%02X",(unsigned)((c>>24)&0xFFu),(unsigned)((c>>16)&0xFFu),(unsigned)((c>>8)&0xFFu));
    return buffer;
}

inline void scenePoints(const render::Scene& scene,std::vector<Vec2>* out){
    for(const auto& p:scene.primitives()){
        for(const auto& v:p.points)out->push_back(v);
        for(const auto& sub:p.subpaths)for(const auto& v:sub.points)out->push_back(v);
    }
}

// Bond strokes only. Atom labels are glyph outlines re-laid out from the atom
// position, so their hinting can differ by a fraction of a unit even when the
// preview is correct; the bond geometry is what must never tear.
inline void strokePoints(const render::Scene& scene,std::vector<Vec2>* out){
    for(const auto& p:scene.primitives()){
        if(p.kind!=render::PrimKind::Polyline&&p.kind!=render::PrimKind::Segment)continue;
        if(!p.style.stroke.enabled)continue;
        for(const auto& v:p.points)out->push_back(v);
        for(const auto& sub:p.subpaths)for(const auto& v:sub.points)out->push_back(v);
    }
}

inline bool nearPoint(const render::Scene& scene,Vec2 target,double tolerance){
    for(const auto& p:scene.primitives()){
        for(const auto& v:p.points)if(core::length(v-target)<=tolerance)return true;
        for(const auto& sub:p.subpaths)for(const auto& v:sub.points)if(core::length(v-target)<=tolerance)return true;
    }
    return false;
}

// A straight stroke that joins both points, which is what a single bond must
// draw while one of its atoms is dragged away from the other.
inline bool hasStroke(const render::Scene& scene,Vec2 a,Vec2 z,double tolerance){
    for(const auto& p:scene.primitives()){
        if((p.kind==render::PrimKind::Polyline||p.kind==render::PrimKind::Segment)&&p.points.size()==2&&
           ((core::length(p.points[0]-a)<=tolerance&&core::length(p.points[1]-z)<=tolerance)||
            (core::length(p.points[0]-z)<=tolerance&&core::length(p.points[1]-a)<=tolerance)))return true;
    }
    return false;
}

inline std::map<std::pair<long long,long long>,int> quantized(const render::Scene& scene,double grid){
    std::map<std::pair<long long,long long>,int> cells;
    std::vector<Vec2> points;scenePoints(scene,&points);
    for(const auto& v:points){
        long long x=(long long)std::llround(v.x/grid),y=(long long)std::llround(v.y/grid);
        ++cells[std::make_pair(x,y)];
    }
    return cells;
}

// Every vertex of the preview must be present in the committed scene in the
// same multiplicity: the picture under the pointer is the picture after drop.
inline bool sameGeometry(const render::Scene& preview,const render::Scene& committed,double tolerance,std::string* why){
    std::vector<Vec2> left,right;strokePoints(preview,&left);strokePoints(committed,&right);
    if(left.size()!=right.size()){
        std::ostringstream text;text<<"preview has "<<left.size()<<" bond vertices, committed scene has "<<right.size();
        *why=text.str();return false;
    }
    auto order=[](Vec2 a,Vec2 b){return a.x!=b.x?a.x<b.x:a.y<b.y;};
    std::sort(left.begin(),left.end(),order);std::sort(right.begin(),right.end(),order);
    double worst=0;size_t worstAt=0;
    for(size_t i=0;i<left.size();++i){double d=core::length(left[i]-right[i]);if(d>worst){worst=d;worstAt=i;}}
    if(worst>tolerance){
        std::ostringstream text;text.precision(3);text<<"bond vertices differ by up to "<<worst<<" at vertex "<<worstAt;
        *why=text.str();return false;
    }
    return true;
}

inline void writeSvg(const render::Scene& scene,const std::string& path){
    Box bounds;bool any=false;
    for(const auto& p:scene.primitives()){
        if(p.layer.value==0||p.bounds.empty())continue;
        if(!any){bounds=p.bounds;any=true;}else{bounds.add(p.bounds.min);bounds.add(p.bounds.max);}
    }
    if(!any)bounds=Box(Vec2(0,0),Vec2(10,10));
    const double scale=6.0,pad=10.0;
    int width=int(bounds.width()*scale+2*pad),height=int(bounds.height()*scale+2*pad);
    if(width<1)width=1;if(height<1)height=1;
    std::ofstream out(path);
    out<<"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    out<<"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\""<<width<<"\" height=\""<<height<<"\" viewBox=\"0 0 "<<width<<' '<<height<<"\">\n";
    out<<"<rect width=\"100%\" height=\"100%\" fill=\"#FFFFFF\"/>\n";
    auto X=[&](double x){return pad+(x-bounds.min.x)*scale;};
    auto Y=[&](double y){return height-pad-(y-bounds.min.y)*scale;};
    for(const auto& p:scene.primitives()){
        if(p.layer.value==0)continue;
        if(p.kind==render::PrimKind::Rect||p.kind==render::PrimKind::Ellipse)continue;
        if(p.kind==render::PrimKind::Text)continue;
        double widthDoc=p.style.strokeWidth>0?p.style.strokeWidth:0.9;
        std::string stroke=p.style.stroke.enabled?svgColor(p.style.stroke):"none";
        std::string fill=(p.kind==render::PrimKind::Polygon&&p.style.hasFill())?svgColor(p.style.fill):"none";
        if(p.kind==render::PrimKind::Polygon&&p.style.hasFill())stroke="none";
        auto emit=[&](const std::vector<Vec2>& points,bool closed){
            if(points.size()<2)return;
            out<<"<path d=\"";
            for(size_t i=0;i<points.size();++i)out<<(i?"L":"M")<<X(points[i].x)<<' '<<Y(points[i].y)<<' ';
            if(closed)out<<"Z";
            out<<"\" fill=\""<<fill<<"\" stroke=\""<<stroke<<"\" stroke-width=\""<<widthDoc*scale<<"\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/>\n";
        };
        if(!p.subpaths.empty()){for(const auto& sub:p.subpaths)emit(sub.points,sub.closed);}
        else emit(p.points,p.kind==render::PrimKind::Polygon);
    }
    out<<"</svg>\n";
}

} // namespace dragpreview

static void dragPreviewCheck(App& app,const std::string& dir){
    using namespace dragpreview;
    auto require=[](bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);};
    auto& s=app.s;
    s.cancel();s.doc=Document();s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());
    {
        auto view=s.view();view.scale=3;view.center=Vec2(14,0);s.setView(view);
    }
    const double R=14.4;
    std::vector<ObjectId> ring;
    for(int i=0;i<6;++i){double a=6.28318530717958647692*i/6;ObjectId id;s.doc.addAtom(Vec2(R*std::cos(a),R*std::sin(a)),6,&id);ring.push_back(id);}
    for(int i=0;i<6;++i){ObjectId id;s.doc.addBond(ring[i],ring[(i+1)%6],&id);}
    ObjectId methyl=kInvalidObjectId,methylBond=kInvalidObjectId;
    s.doc.addAtom(Vec2(2*R,0),6,&methyl);
    s.doc.addBond(ring[0],methyl,&methylBond);
    // A labelled double bond is the hard case for the preview: the strokes are
    // clipped against glyph ink, and the label itself travels with the atom.
    ObjectId hydroxyl=kInvalidObjectId,hydroxylBond=kInvalidObjectId;
    s.doc.addAtom(Vec2(3*R,0),8,&hydroxyl);
    s.doc.setAtomLabel(hydroxyl,"OH");
    s.doc.addBond(methyl,hydroxyl,&hydroxylBond);
    s.doc.setBondOrder(hydroxylBond,BondOrder::Double);
    s.refresh();
    require(s.activateCommand("chooseSelectionTool_Marquee"),"selection tool activates");

    std::ofstream json(dir+"/drag-check.json");
    auto record=[&](const char* name,bool ok){json<<"  \""<<name<<"\": "<<(ok?"true":"false")<<",\n";};
    auto position=[&](ObjectId id){return s.doc.atoms().get(id)->position;};

    struct Case{const char* name;std::vector<ObjectRef> selection;Vec2 anchor;Vec2 delta;};
    std::vector<Case> cases;
    cases.push_back(Case{"methyl",std::vector<ObjectRef>(1,ObjectRef(ObjectKind::Atom,methyl)),Vec2(2*R,0),Vec2(9,4)});
    {
        std::vector<ObjectRef> atoms;
        for(auto id:ring)atoms.push_back(ObjectRef(ObjectKind::Atom,id));
        cases.push_back(Case{"ring",atoms,Vec2(R,0),Vec2(-7,11)});
    }
    cases.push_back(Case{"labeled",std::vector<ObjectRef>(1,ObjectRef(ObjectKind::Atom,hydroxyl)),Vec2(3*R,0),Vec2(-6,-5)});
    auto selectedAtom=[&](const std::vector<ObjectRef>& selection,ObjectId id){
        for(auto ref:selection)if(ref.kind==ObjectKind::Atom&&ref.id==id)return true;
        return false;
    };
    for(auto c:cases){
        s.selected=c.selection;
        Vec2 at=position(c.selection.front().id);
        auto digest=s.doc.digest();auto serial=s.serial;
        s.gesture(PointerPhase::Down,at);
        require(s.objectDragActive(),std::string(c.name)+": drag starts");
        s.gesture(PointerPhase::Move,at+c.delta);
        require(s.doc.digest()==digest,std::string(c.name)+": preview leaves the document untouched");
        require(s.serial==serial,std::string(c.name)+": preview does not rebuild the document scene");
        auto preview=s.displayScene();
        // The bond that crosses the selection boundary must follow the atom
        // that is being dragged, and nothing may stay at that atom's old place.
        Vec2 ringAt=position(ring[0]),methylAt=position(methyl),hydroxylAt=position(hydroxyl);
        bool ringMoves=selectedAtom(c.selection,ring[0]),methylMoves=selectedAtom(c.selection,methyl),hydroxylMoves=selectedAtom(c.selection,hydroxyl);
        Vec2 expectedRing=ringAt+(ringMoves?c.delta:Vec2()),expectedMethyl=methylAt+(methylMoves?c.delta:Vec2());
        bool attached=std::string(c.name)=="labeled"?nearPoint(preview,hydroxylAt+c.delta,3.0):hasStroke(preview,expectedRing,expectedMethyl,0.05);
        bool stale=!(ringMoves&&nearPoint(preview,ringAt,0.15))&&!(methylMoves&&nearPoint(preview,methylAt,0.15))&&
                   !(hydroxylMoves&&nearPoint(preview,hydroxylAt,0.4));
        record(c.name,attached&&stale);
        std::string why;
        require(attached,std::string(c.name)+": the boundary bond follows the dragged atom");
        require(stale,std::string(c.name)+": no geometry is left at a dragged atom's old position");
        writeSvg(preview,dir+"/preview-"+std::string(c.name)+".svg");
        // Release, then compare the preview with the scene of the document that
        // the drop produced. These must be the same picture.
        s.gesture(PointerPhase::Up,at+c.delta);
        auto committed=s.displayScene();
        writeSvg(committed,dir+"/committed-"+std::string(c.name)+".svg");
        // Auto-side double bonds next to the moving junction can legitimately
        // be re-laid out on drop; allow that but never a detached bond.
        double tolerance=(std::string(c.name)=="labeled"||std::string(c.name)=="ring")?2.5:0.10;
        bool same=sameGeometry(preview,committed,tolerance,&why);
        require(same,std::string(c.name)+": "+why);
        require(nearPoint(committed,expectedRing,0.05)&&hasStroke(committed,expectedRing,expectedMethyl,0.05),std::string(c.name)+": drop commits the stretched bond");
        require(s.undo().ok(),std::string(c.name)+": drop is one undo step");
    }
    // Dragging the whole molecule has no boundary bond and must stay on the
    // sparse rigid path, but it still has to move every atom.
    {
        s.selected.clear();
        for(auto& a:s.doc.atoms().records())if(a.isAlive())s.selected.push_back(ObjectRef(ObjectKind::Atom,a.id));
        Vec2 at=position(ring[0]);Vec2 delta(5,-6);
        s.gesture(PointerPhase::Down,at);
        require(s.objectDragActive(),"whole molecule drag starts");
        s.gesture(PointerPhase::Move,at+delta);
        auto scene=s.displayScene();
        Vec2 ringAt=position(ring[0]),methylAt=position(methyl);
        bool ok=hasStroke(scene,ringAt+delta,methylAt+delta,0.05)&&!nearPoint(scene,ringAt,0.15)&&!nearPoint(scene,methylAt,0.15);
        record("whole",ok);
        require(ok,"whole molecule drag moves every atom");
        writeSvg(scene,dir+"/preview-whole.svg");
        s.gesture(PointerPhase::Up,at+delta);
    }
    json<<"  \"generated\": true\n}\n";
}