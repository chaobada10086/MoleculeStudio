#pragma once
#include "next/arrows_curves/model.hpp"
#include "virtual_bonds.hpp"
#include "next/arrows_curves/geometry.hpp"
#include <cmath>
#include <algorithm>
#include <queue>
#include <limits>
#include <functional>
#include "mechanism_visual_anchors.hpp"
#include "mechanism_bond_clearance.hpp"

// Shared by pointer gestures and AI actions. Coordinates are document points.
namespace r62 { namespace arrows {
inline bool mechanismHasLabel(const core::Document& doc,const core::AtomRecord& a){
    return a.element!=6||doc.incidentBonds(a.id).empty()||!doc.atomLabel(a.id).empty()||a.semantics.charge||a.semantics.radical||a.semantics.showLabel;
}
inline core::Box mechanismLabelBox(const core::Document& doc,const core::AtomRecord& a,double pad=1.1){
    double size=doc.defaultTextSize();if(auto v=doc.documentExtensions().findValue("studio.style.LabelSize")){double n=std::atof(v->c_str());if(n>0&&n<100)size=n;}
    auto text=doc.atomLabel(a.id);double halfWidth=text.empty()?size*.40:std::max(size*.40,std::min(size*4.,text.size()*size*.29));
    if(a.semantics.implicitHydrogens>0)halfWidth+=size*.25;
    if(a.semantics.charge||a.semantics.radical)halfWidth+=size*.22;
    double below=size*(a.semantics.implicitHydrogens>0?1.5:.55)+pad;
    return core::Box(a.position-core::Vec2(halfWidth+pad,size*.65+pad),a.position+core::Vec2(halfWidth+pad,below));
}
// Shared capture extent for pointer binding and validation of explicit bindings.
inline core::Box mechanismCaptureLabelBox(const core::Document& doc,const core::AtomRecord& a,double pad=0){
    auto box=mechanismLabelBox(doc,a,pad);
    if(a.semantics.charge){double em=doc.defaultTextSize();if(auto value=doc.documentExtensions().findValue("studio.style.LabelSize")){double size=std::atof(value->c_str());if(size>0&&size<100)em=size;}
        double scriptAdvance=em*.65*(std::abs(int(a.semantics.charge))>1?1.2:.75);
        box.max.x+=scriptAdvance;box.min.y-=em*.20;
    }
    return box;
}
inline bool mechanismAnchorPoint(const core::Document& doc,core::ObjectRef ref,core::Vec2* point) {
    core::ObjectId x,y;
    if(virtual_bonds::endpoints(doc,ref,x,y)){*point=(doc.atoms().alive(x)->position+doc.atoms().alive(y)->position)*.5;return true;}
    if(ref.kind==core::ObjectKind::Atom){auto a=doc.atoms().alive(ref.id);if(a){
        *point=a->position;
        if(mechanism::triphenylphosphineLabel(doc.atomLabel(a->id))){
            const auto body=mechanismAtomBodyBox(doc,*a);
            *point=(body.min+body.max)*.5;
        }
        return true;
    }}
    if(ref.kind==core::ObjectKind::Bond){auto b=doc.bonds().alive(ref.id);if(b){auto a=doc.atoms().alive(b->beginAtom),z=doc.atoms().alive(b->endAtom);if(a&&z){*point=(a->position+z->position)*.5;return true;}}}
    return false;
}
inline core::ObjectRef mechanismAnchor(const core::Document& doc,core::Vec2 point,double radius) {
    core::ObjectRef best;double distance=radius,second=radius;
    auto consider=[&](core::ObjectRef ref){core::Vec2 p;if(!mechanismAnchorPoint(doc,ref,&p))return;double d=core::length(p-point);if(d<distance){second=distance;distance=d;best=ref;}else if(d<second)second=d;};
    for(const auto& a:doc.atoms().records())if(a.isAlive())consider(core::ObjectRef(core::ObjectKind::Atom,a.id));
    for(const auto& b:doc.bonds().records())if(b.isAlive())consider(core::ObjectRef(core::ObjectKind::Bond,b.id));
    return best.valid()&&second-distance>.1?best:core::ObjectRef();
}
inline bool mechanismEndpointsValid(const core::Document& doc,const ArrowParams& p) {
    core::Vec2 a,b;if(!p.mechanism||!mechanismAnchorPoint(doc,p.electronSource,&a)||!mechanismAnchorPoint(doc,p.electronTarget,&b))return false;
    // Printed element labels may clip the visible endpoint by several points.
    double tolerance=std::max(8.0,doc.defaultTextSize());
    return core::length(a-p.begin)<=tolerance&&core::length(b-p.end)<=tolerance;
}
inline double mechanismDistance(core::Vec2 p, core::Vec2 a, core::Vec2 b) {
    core::Vec2 d=b-a; double n=d.x*d.x+d.y*d.y;
    double t=n>1e-12?((p.x-a.x)*d.x+(p.y-a.y)*d.y)/n:0;
    t=std::max(0.0,std::min(1.0,t)); return core::length(p-(a+d*t));
}
inline core::Vec2 mechanismSnap(const core::Document& doc,core::Vec2 p,double radius) {
    core::Vec2 best=p; double distance=radius;
    for(const auto& a:doc.atoms().records())if(a.isAlive()) {
        double d=core::length(p-a.position);if(d<distance){distance=d;best=a.position;}
    }
    // Bond centres are deliberate electron-source/target anchors, not arbitrary
    // points along the bond. The closest candidate wins.
    for(const auto& b:doc.bonds().records())if(b.isAlive()) {
        auto a=doc.atoms().alive(b.beginAtom),z=doc.atoms().alive(b.endAtom);if(!a||!z)continue;
        core::Vec2 c=(a->position+z->position)*.5;double d=core::length(p-c);
        if(d<distance){distance=d;best=c;}
    }
    return best;
}
struct MechanismObstacle {
    core::Vec2 a,b;
    double radius;
    core::ObjectRef glyphOwner;
    uint8_t glyphSite;
    core::ObjectRef geometryOwner;
    MechanismObstacle(core::Vec2 a,core::Vec2 b,double radius,core::ObjectRef owner={},uint8_t site=0,core::ObjectRef geometry={}):
        a(a),b(b),radius(radius),glyphOwner(owner),glyphSite(site),geometryOwner(geometry){}
};
inline double mechanismObstacleDistance(core::Vec2 q,const MechanismObstacle& o){
    return o.glyphOwner.valid()?mechanismBoxDistance(core::Box(o.a,o.b),q):mechanismDistance(q,o.a,o.b);
}
inline bool mechanismGlyphContact(const MechanismObstacle& o,const ArrowParams& p,core::Vec2 q){
    auto approaching=[&](core::ObjectRef ref,uint8_t site,core::Vec2 endpoint){
        return o.glyphOwner==ref&&(site==o.glyphSite||site==0)&&
            mechanismObstacleDistance(endpoint,o)<=.75&&core::length(q-endpoint)<=core::length(o.b-o.a)+1.;
    };
    return approaching(p.electronSource,p.sourceVisualAnchor,p.begin)||
           approaching(p.electronTarget,p.targetVisualAnchor,p.end);
}
inline bool mechanismGeometryContact(const MechanismObstacle& o,const ArrowParams& p,core::Vec2 q){
    if(!o.geometryOwner.valid())return false;
    auto contact=[&](core::ObjectRef ref,uint8_t site,core::Vec2 point){return ref==o.geometryOwner&&!(ref.kind==core::ObjectKind::Bond&&site==5)&&core::length(q-point)<std::max(3.,o.radius+1.);};
    return contact(p.electronSource,p.sourceVisualAnchor,p.begin)||contact(p.electronTarget,p.targetVisualAnchor,p.end);
}
inline std::vector<MechanismObstacle> mechanismObstacles(const core::Document& doc) {
    std::vector<MechanismObstacle> result;
    for(const auto& b:doc.bonds().records())if(b.isAlive()) {
        auto a=doc.atoms().alive(b.beginAtom),z=doc.atoms().alive(b.endAtom);
        double width=2.;if(b.display==core::BondDisplay::Wedge||b.display==core::BondDisplay::WedgeHash){const auto& envelopes=mechanismBondEnvelopes(doc);auto found=envelopes.find(b.id);if(found!=envelopes.end()&&found->second.valid)width=std::max(width,std::max(std::abs(found->second.low),std::abs(found->second.high))+1.);}
        if(a&&z)result.push_back({a->position,z->position,width,{},0,core::ObjectRef(core::ObjectKind::Bond,b.id)});
    }
    for(const auto& a:doc.atoms().records())if(a.isAlive())
        result.push_back({a.position,a.position,mechanismHasLabel(doc,a)?5.0:2.0,{},0,core::ObjectRef(core::ObjectKind::Atom,a.id)});
    for(const auto& entry:mechanismVisualBoxes(doc)){
        const auto owner=core::ObjectRef(core::ObjectKind::Atom,entry.first);
        for(const auto& glyph:entry.second.labelGlyphs)result.emplace_back(glyph.min,glyph.max,.60,owner,1);
        if(entry.second.hasCharge){
            const auto& box=entry.second.charge;
            result.emplace_back(box.min,box.max,.60,owner,2);
        }
    }
    for(const auto& t:doc.texts().records())if(t.isAlive()) {
        double size=t.size>0?t.size:doc.defaultTextSize();
        double width=std::max(size*.5,doc.plainText(t.id).size()*size*.55);
        result.push_back({t.position+core::Vec2(0,-size*.4),t.position+core::Vec2(width,-size*.4),size*.7});
    }
    for(const auto& path:doc.paths().records())if(path.isAlive())
        for(size_t i=1;i<path.points.size();++i)result.push_back({path.points[i-1],path.points[i],1.5});
    for(const auto& arrow:doc.arrows().records())if(arrow.isAlive()) {
        ArrowParams p;bool valid=false;
        if(!readArrow(doc,arrow.id,&p,&valid).ok()||!valid)continue;
        std::vector<core::Vec2> points;Tessellation t;if(p.route.size()>1)points=routedPoints(p);else flattenArc(p.begin,p.end,p.arcSagitta,t,&points);
        for(size_t i=1;i<points.size();++i)result.push_back({points[i-1],points[i],1.5});
    }
    return result;
}
inline double mechanismRouteScore(const ArrowParams& p,const std::vector<MechanismObstacle>& obstacles,bool glyphsOnly=false) {
    std::vector<core::Vec2> points;Tessellation t;t.tolerance=.25;if(p.route.size()>1)points=routedPoints(p);else flattenArc(p.begin,p.end,p.arcSagitta,t,&points);
    double score=0;
    for(size_t i=1;i<points.size();++i) {
      const double span=core::length(points[i]-points[i-1]);
      int count=std::max(1,std::min(512,int(std::ceil(span*2))));
      for(int j=0;j<count;++j) {
        core::Vec2 sample=points[i-1]+(points[i]-points[i-1])*(double(j)/count);
        // Contact with the source/target is intentional; interior collisions aren't.
        for(const auto& o:obstacles) {
            const bool glyph=o.glyphOwner.valid();
            if(glyph?mechanismGlyphContact(o,p,sample):(glyphsOnly||mechanismGeometryContact(o,p,sample)))continue;
            const double d=mechanismObstacleDistance(sample,o),weight=span/count;
            if(d<o.radius)score+=(glyph?3000.:100.+(o.radius-d)*20)*weight;
            else if(!glyphsOnly&&d<o.radius+3)score+=.15*(o.radius+3-d)*(o.radius+3-d)*weight/std::max(1.0,core::length(p.end-p.begin));
        }
      }
    }
    return score;
}
#include "mechanism_search.inc"
// Visual clearance only; electronSource/electronTarget remain chemical anchors.
inline void finishAutomaticMechanismEndpoints(const core::Document& doc,ArrowParams& p){
    std::vector<core::Vec2> points;Tessellation tess;tess.tolerance=.005;
    if(p.route.size()>1)points=routedPoints(p);else flattenArc(p.begin,p.end,p.arcSagitta,tess,&points);
    if(points.size()<2)return;
    auto clearance=[&](bool source){
        if(!source)std::reverse(points.begin(),points.end());
        auto ref=source?p.electronSource:p.electronTarget;
        double distance=0,total=0;for(size_t i=1;i<points.size();++i)total+=core::length(points[i]-points[i-1]);
        double fixed=0;
        auto atom=ref.kind==core::ObjectKind::Atom?doc.atoms().alive(ref.id):nullptr;
        const auto site=source?p.sourceVisualAnchor:p.targetVisualAnchor;
        const auto pair=atom&&site==3?mechanismClosestLonePair(doc,atom->id,points.front()):nullptr;
        if(source&&atom&&p.sourceVisualAnchor==0&&core::length(points.front()-atom->position)>doc.defaultTextSize()*.3)fixed=1.25;
        auto bond=ref.kind==core::ObjectKind::Bond?doc.bonds().alive(ref.id):nullptr;
        bool radicalSite=atom&&(source?p.sourceVisualAnchor:p.targetVisualAnchor)==4;core::Vec2 radicalCenter=points.front();
        if(radicalSite){double best=1e100;for(auto center:mechanismRadicalPoints(doc,atom->id)){double d=core::length(center-points.front());if(d<best){best=d;radicalCenter=center;}}}
        auto clear=[&](core::Vec2 q){
            // A captured electron marker is the endpoint, not the neighbouring
            // text. Keep docking near that marker when it sits beside a glyph.
            if(radicalSite)return core::length(q-radicalCenter)>=mechanismRadicalRadius(doc)+.65;
            if(pair)return mechanismBoxDistance(*pair,q)>=.65;
            if(atom){
                const double gap=source?std::max(.8,p.strokeWidth*.5+.5):std::max(1.1,p.head.width*.5+.6);
                const auto& boxes=mechanismVisualBoxes(doc);auto found=boxes.find(atom->id);
                if(found!=boxes.end()){
                    if(found->second.hasCharge&&mechanismBoxDistance(found->second.charge,q)<gap)return false;
                    // Body-site docking is clipped against the chosen element
                    // by clearMechanismLabels. Do not trim it past adjacent H
                    // glyphs and redirect an O-bound arrow toward the whole label.
                    if(site!=1)for(const auto& box:found->second.labelGlyphs)if(mechanismBoxDistance(box,q)<gap)return false;
                }
            }
            if(bond)return mechanismOutsideBondInk(doc,bond->id,q,1.1);
            if(!source&&atom&&!mechanismHasLabel(doc,*atom))return core::length(q-atom->position)>=1.4;
            return true;
        };
        while(points.size()>2 && distance<total*.35 && (distance<fixed || !clear(points.front()))){
            double step=core::length(points[1]-points[0]);
            if(distance>=fixed&&!clear(points.front())&&clear(points[1])){auto a=points[0],b=points[1];double low=0,high=1;for(int i=0;i<24;++i){double t=(low+high)*.5;if(clear(a+(b-a)*t))high=t;else low=t;}points[0]=a+(b-a)*high;break;}
            if(distance<fixed && clear(points.front()) && distance+step>fixed){points[0]=points[0]+(points[1]-points[0])*((fixed-distance)/step);distance=fixed;break;}
            distance+=step;points.erase(points.begin());
        }
        if(!source)std::reverse(points.begin(),points.end());
    };
    clearance(true);clearance(false);
    p.begin=points.front();p.end=points.back();auto d=p.end-p.begin;core::Vec2 n(-d.y,d.x);double square=d.x*d.x+d.y*d.y;
    if(square<1e-8)return;p.route.clear();for(auto q:points){q=q-p.begin;p.route.push_back(core::Vec2((q.x*d.x+q.y*d.y)/square,(q.x*n.x+q.y*n.y)/square));}
}
// Rigid following is safe only while the translated curve still clears ink.
// Glyphs may change sides when neighbouring atoms move, even if both anchors
// have the same displacement. Include the arrowhead footprint at the tip.
inline bool mechanismOverlapsGlyphs(const core::Document& doc,const ArrowParams& p){
    std::vector<core::Vec2> points;Tessellation tess;tess.tolerance=.05;if(p.route.size()>1)points=routedPoints(p);else flattenArc(p.begin,p.end,p.arcSagitta,tess,&points);
    const auto& all=mechanismVisualBoxes(doc);
    for(size_t k=0;k<points.size();++k){double gap=k+1==points.size()?std::max(1.0,p.head.width*.5+.5):.55;
        for(const auto& entry:all){if(entry.second.hasCharge&&mechanismBoxDistance(entry.second.charge,points[k])<gap)return true;for(const auto& box:entry.second.labelGlyphs)if(mechanismBoxDistance(box,points[k])<gap)return true;}
    }
    return false;
}
// Distinct atom/charge sites must remain visually distinct too: avoid the
// other glyph belonging to the same atom, without moving the chosen start.
inline double mechanismOtherSitePenalty(const core::Document& doc,const ArrowParams& p){
    std::vector<core::Box> boxes;
    auto add=[&](core::ObjectRef ref,uint8_t site){if(ref.kind!=core::ObjectKind::Atom)return;auto a=doc.atoms().alive(ref.id);if(!a)return;
        if(site==1&&a->semantics.charge)boxes.push_back(mechanismChargeBox(doc,*a,.65));
        if(site==2&&mechanismHasLabel(doc,*a))boxes.push_back(mechanismAtomBodyBox(doc,*a,.65));};
    add(p.electronSource,p.sourceVisualAnchor);add(p.electronTarget,p.targetVisualAnchor);
    if(boxes.empty())return 0;auto points=routedPoints(p);double score=0;
    for(size_t i=1;i<points.size();++i){auto mid=(points[i]+points[i-1])*.5;double step=core::length(points[i]-points[i-1]);
        for(const auto& box:boxes)if(mid.x>=box.min.x&&mid.x<=box.max.x&&mid.y>=box.min.y&&mid.y<=box.max.y)score+=step*1000.;}
    return score;
}
inline void detourMechanismGlyphs(ArrowParams& p,const std::vector<MechanismObstacle>& obstacles){
    const auto collisions=mechanismRouteScore(p,obstacles,true);
    if(collisions<=0)return;
    auto detour=p;
    if(findMechanismRoute(detour,obstacles)&&mechanismRouteScore(detour,obstacles,true)<collisions)p=std::move(detour);
}
// C1 continuity alone can still hide an almost pointed reversal on a short
// cubic. Penalize tight curvature in document units, independently of sampling.
inline double mechanismCurvaturePenalty(const ArrowParams& p,double minimumRadius){
    auto points=routedPoints(p);double penalty=0;
    for(size_t i=1;i+1<points.size();++i){auto a=points[i]-points[i-1],b=points[i+1]-points[i];double x=core::length(a),y=core::length(b);if(x<1e-8||y<1e-8)continue;
        double angle=std::acos(std::max(-1.,std::min(1.,core::dot(a,b)/(x*y))));if(angle<1e-5)continue;
        double radius=(x+y)*.5/angle,excess=std::max(0.,minimumRadius/std::max(.01,radius)-1.);
        penalty+=angle*excess*excess*12.;
    }return penalty;
}
// The final control point lies on the ray from the element glyph center
// through the tip. This makes the drawn head aim at the element itself.
inline double mechanismSidePenalty(const ArrowParams& p){
    if(!p.mechanismSide)return 0;
    double bow=0;
    if(p.route.size()>1){for(auto q:p.route)bow+=q.y;}
    else bow=p.arcSagitta;
    return bow*p.mechanismSide<0?80.:0.;
}
inline bool routeShortBondReturn(const core::Document& doc,ArrowParams& p,
                                core::Vec2 source,const std::vector<MechanismObstacle>& obstacles){
    const auto* bond=doc.bonds().alive(p.electronSource.id);
    const auto* target=doc.atoms().alive(p.electronTarget.id);
    if(!bond||!target)return false;
    const auto* other=doc.atoms().alive(bond->beginAtom==target->id?bond->endAtom:bond->beginAtom);
    if(!other)return false;
    const auto axis=target->position-other->position;
    const double span=core::length(axis);
    if(span<1e-8)return false;
    const auto u=axis*(1./span);
    const core::Vec2 normal(-u.y,u.x);
    const auto box=mechanismAtomBodyBox(doc,*target);
    const auto center=(box.min+box.max)*.5,half=(box.max-box.min)*.5;
    const double em=integration::molecular::setting(doc,"LabelSize",10);
    const bool fishhook=p.head.sides==HeadSides::Left||p.head.sides==HeadSides::Right;
    auto archPenalty=[&](const ArrowParams& candidate){
        if(fishhook)return 0.;
        // Textbook bond returns are tall hairpins, not shallow caps. Measure
        // fullness against the visible chord rather than the chemical bond.
        double bow=0;for(auto q:candidate.route)bow=std::max(bow,std::abs(q.y));
        return std::pow(std::max(0.,.90-bow),2)*30.;
    };
    ArrowParams best=p;
    double bestCost=std::numeric_limits<double>::infinity();
    for(double side:{1.,-1.}){
        const auto n=normal*side;
        const auto begin=mechanismBondOutwardContact(doc,bond->id,source,n,true);
        for(double tilt:{.40,.75,1.15}){
            auto outward=n-u*tilt;
            outward=outward*(1./core::length(outward));
            const double dx=std::fabs(outward.x),dy=std::fabs(outward.y);
            const double edge=std::min(dx>1e-8?half.x/dx:1e30,dy>1e-8?half.y/dy:1e30);
            const auto end=mechanismHasLabel(doc,*target)||!fishhook?center+outward*(edge+.9):target->position+n*1.4-u*1.4;
            const auto chord=end-begin;
            const core::Vec2 perpendicular(-chord.y,chord.x);
            const double squared=core::dot(chord,chord);
            if(squared<1e-8)continue;
            if(!fishhook)for(double height:{.85,.95,1.10})for(double skew:{-.10,0.}){
                const double spread=std::max(0.,(std::sqrt(std::max(1.8,em*.20)*height/core::length(chord))-.5)*.5);
                if(spread>.28)continue;
                // Beside a charged label, keep the established radial docking:
                // flaring a tight arch changes its arrival heading toward the
                // neighbouring charge instead of the chosen element glyph.
                if(spread>0&&target->semantics.charge)continue;
                if(skew!=0&&target->semantics.charge)continue;
                // A half ellipse gives independent height and width with a
                // single convex arch. Font-sized cubic handles can fold when
                // the printed element leaves only a very short visible chord.
                auto candidate=p;candidate.begin=begin;candidate.end=end;candidate.targetVisualAnchor=1;candidate.route.clear();
                const double sign=core::dot(perpendicular,n)>=0?1.:-1.;
                // Shift the belly toward the source without changing either
                // endpoint or its tangent. The sin^3 term vanishes through
                // second order at the ends: a long flowing entry, a compact
                // return, and no extra terminal neck or angular join.
                for(int i=0;i<=128;++i){double angle=3.141592653589793*i/128.,sine=std::sin(angle);candidate.route.emplace_back((1.-std::cos(angle))*.5-spread*std::sin(2.*angle)+skew*sine*sine*sine,sign*height*sine);}
                if(skew!=0&&mechanismCurvaturePenalty(candidate,1.65)>1e-5)continue;
                const double cost=mechanismRouteScore(candidate,obstacles)+mechanismCurvaturePenalty(candidate,std::max(2.,em*.22))+mechanismSidePenalty(candidate)+archPenalty(candidate)+std::abs(height-.95)*.1+std::abs(tilt-.40)*.25+(skew==0&&!target->semantics.charge?2.:0.);
                if(cost<bestCost){bestCost=cost;best=std::move(candidate);}
            }
            for(double scale:{1.,1.35,1.75}){
                const double bend=fishhook?std::max(4.,em*.75)*scale:std::max(em*1.15,core::length(chord)*1.35)*scale;
                const auto c1=begin+n*bend;
                const auto c2=end+outward*(bend*(fishhook?.72:.95));
                auto candidate=p;
                candidate.begin=begin;candidate.end=end;candidate.targetVisualAnchor=1;candidate.route.clear();
                for(int i=0;i<=80;++i){
                    const double t=i/80.,v=1.-t;
                    const auto offset=begin*(v*v*v)+c1*(3*v*v*t)+c2*(3*v*t*t)+end*(t*t*t)-begin;
                    candidate.route.emplace_back(core::dot(offset,chord)/squared,core::dot(offset,perpendicular)/squared);
                }
                const double cost=mechanismRouteScore(candidate,obstacles)+mechanismCurvaturePenalty(candidate,std::max(2.,em*.35))+mechanismSidePenalty(candidate)+archPenalty(candidate)+(fishhook?.35:0.)+
                    (scale-1.)*.35+std::fabs(tilt-(fishhook?.75:.40))*.25+(side*p.arcSagitta<0?.1:0.);
                if(cost<bestCost){bestCost=cost;best=std::move(candidate);}
            }
            // A circular return offers finite, uniform curvature even when the
            // atom and the bond midpoint are only a few points apart. Cubic
            // handles sized from the font can otherwise fold into a sharp tip.
            for(double bow:{.36,.50,.65}){
                auto candidate=p;candidate.begin=begin;candidate.end=end;candidate.targetVisualAnchor=1;candidate.route.clear();
                candidate.arcSagitta=core::length(chord)*bow*(core::dot(perpendicular,n)>=0?1.:-1.);
                Tessellation tess;tess.tolerance=.005;std::vector<core::Vec2> points;flattenArc(begin,end,candidate.arcSagitta,tess,&points);
                for(auto point:points){auto offset=point-begin;candidate.route.emplace_back(core::dot(offset,chord)/squared,core::dot(offset,perpendicular)/squared);}
                const double cost=mechanismRouteScore(candidate,obstacles)+mechanismCurvaturePenalty(candidate,std::max(2.,em*.35))+mechanismSidePenalty(candidate)+archPenalty(candidate)+std::fabs(tilt-.75)*.25+(side*p.arcSagitta<0?.1:0.);
                if(cost<bestCost){bestCost=cost;best=std::move(candidate);}
            }
        }
    }
    if(!std::isfinite(bestCost))return false;
    p=std::move(best);
    return true;
}
// Store the outer barb chosen from the final curve, rather than making the
// renderer silently invert Left/Right. The chemical tip and travel direction
// stay unchanged. On a tight return an inner barb can fold across the shaft.
inline void orientAutomaticFishhook(ArrowParams& p){
    if(!p.mechanism||p.presetId!=57||(p.head.sides!=HeadSides::Left&&p.head.sides!=HeadSides::Right))return;
    auto points=routedPoints(p);if(points.size()<3){Tessellation tess;tess.tolerance=.005;flattenArc(p.begin,p.end,p.arcSagitta,tess,&points);}if(points.size()<3)return;
    auto direction=points.back()-points[points.size()-2];double size=core::length(direction);if(size<1e-8)return;direction=direction*(1./size);
    double lookback=std::max(1.,p.head.length);auto back=points.front();
    for(size_t i=points.size()-1;i>0;--i){double span=core::length(points[i]-points[i-1]);if(span>=lookback){back=points[i]+(points[i-1]-points[i])*(lookback/span);break;}lookback-=span;}
    auto delta=back-points.back();double bend=direction.x*delta.y-direction.y*delta.x;
    if(std::abs(bend)>.02)p.head.sides=bend>0?HeadSides::Left:HeadSides::Right;
}
inline void routeMechanism(const core::Document& doc,ArrowParams& p,bool automatic=true) {
    if(automatic){p.head.length=2.8;p.head.width=1.95;}
    if(automatic&&p.electronSource.kind==core::ObjectKind::Atom){auto atom=doc.atoms().alive(p.electronSource.id);if(atom){
        if(p.sourceVisualAnchor==0){auto charge=mechanismChargeBox(doc,*atom,1.);bool onCharge=atom->semantics.charge&&p.begin.x>=charge.min.x&&p.begin.x<=charge.max.x&&p.begin.y>=charge.min.y&&p.begin.y<=charge.max.y;p.sourceVisualAnchor=onCharge?2:1;}
        if(p.sourceVisualAnchor==1&&mechanismHasLabel(doc,*atom)){auto body=mechanismAtomBodyBox(doc,*atom);p.begin=(body.min+body.max)*.5;}
        if(p.sourceVisualAnchor==3)if(auto pair=mechanismClosestLonePair(doc,atom->id,p.begin))p.begin=(pair->min+pair->max)*.5;
        if(p.sourceVisualAnchor==4){double best=1e100;auto old=p.begin;for(auto point:mechanismRadicalPoints(doc,atom->id)){double gap=core::length(point-old);if(gap<best){best=gap;p.begin=point;}}}
    }}
    if(automatic){auto dockCharge=[&](bool source){auto ref=source?p.electronSource:p.electronTarget;auto site=source?p.sourceVisualAnchor:p.targetVisualAnchor;if(site!=2||ref.kind!=core::ObjectKind::Atom)return;auto atom=doc.atoms().alive(ref.id);if(!atom||!atom->semantics.charge)return;
        auto charge=mechanismChargeBox(doc,*atom),body=mechanismAtomBodyBox(doc,*atom);auto center=(charge.min+charge.max)*.5;auto outward=center-(body.min+body.max)*.5;auto direction=std::abs(outward.x)>=std::abs(outward.y)?core::Vec2(outward.x<0?-1.:1.,0):core::Vec2(0,outward.y<0?-1.:1.);auto half=(charge.max-charge.min)*.5;double edge=direction.x?half.x:half.y;double gap=source?.8:std::max(1.1,std::min(1.8,p.head.width*.5+.6));auto point=center+direction*(edge+gap);if(source)p.begin=point;else p.end=point;};dockCharge(true);dockCharge(false);}
    const auto originalSource=p.begin;
    if(automatic)for(bool source:{true,false}){auto ref=source?p.electronSource:p.electronTarget;if(ref.kind!=core::ObjectKind::Atom||!projection_molecule::rear(doc,ref.id))continue;auto center=doc.atoms().alive(ref.id)->position;auto ray=(source?p.end:p.begin)-center;double span=core::length(ray);if(span>1e-8){auto point=center+ray*((projection_molecule::radius(doc)+1.1)/span);if(source)p.begin=point;else p.end=point;}}
    // A base attacking an exposed projection H is an inter-atom transfer,
    // not a local bond-return hairpin. Dock toward the target and compare
    // compact curves on both sides rather than locking in a mouse U-turn.
    if(automatic&&p.electronSource.kind==core::ObjectKind::Atom&&p.electronTarget.kind==core::ObjectKind::Atom){
        auto target=doc.atoms().alive(p.electronTarget.id);auto source=doc.atoms().alive(p.electronSource.id);
        if(source&&target&&source->semantics.charge<0&&target->element==1&&projection_molecule::coreFor(doc,target->id)!=core::kInvalidObjectId){
            auto box=p.sourceVisualAnchor==2?mechanismChargeBox(doc,*source,.8):mechanismAtomBodyBox(doc,*source,.8);auto goal=mechanismAtomBodyBox(doc,*target,1.6);
            auto boundary=[](core::Box box,core::Vec2 toward){auto center=(box.min+box.max)*.5,half=(box.max-box.min)*.5;auto ray=toward-center;double scale=1e100;if(std::abs(ray.x)>1e-8)scale=std::min(scale,half.x/std::abs(ray.x));if(std::abs(ray.y)>1e-8)scale=std::min(scale,half.y/std::abs(ray.y));return center+ray*scale;};
            auto begin=boundary(box,(goal.min+goal.max)*.5);auto core=doc.atoms().alive(projection_molecule::coreFor(doc,target->id));auto outward=target->position-core->position;double outwardLength=core::length(outward);outward=outwardLength>1e-8?outward*(1/outwardLength):core::Vec2(1,0);
            // Arrive at the exposed side of H, away from its C-H bond. A tip
            // on the inward edge can make the route search's goal inaccessible.
            auto end=boundary(goal,(goal.min+goal.max)*.5+outward*100.);auto chord=end-begin;core::Vec2 normal(-chord.y,chord.x);double square=core::dot(chord,chord);
            if(square>1){ArrowParams best=p;double cost=1e100;auto obstacles=mechanismObstacles(doc);
                auto crossings=[&](const ArrowParams& curve){auto points=routedPoints(curve);int count=0;auto cross=[](core::Vec2 a,core::Vec2 b,core::Vec2 c){auto u=b-a,v=c-a;return u.x*v.y-u.y*v.x;};
                    for(size_t i=1;i<points.size();++i)for(const auto& bond:doc.bonds().records())if(bond.isAlive()){auto x=doc.atoms().alive(bond.beginAtom)->position,y=doc.atoms().alive(bond.endAtom)->position;if(cross(points[i-1],points[i],x)*cross(points[i-1],points[i],y)<-1e-8&&cross(x,y,points[i-1])*cross(x,y,points[i])<-1e-8)++count;}return count;
                };
                for(double bow:{-.75,-.5,-.35,-.2,-.1,.1,.2,.35,.5,.75}){auto candidate=p;candidate.begin=begin;candidate.end=end;candidate.route.clear();candidate.mechanismSide=0;
                    auto c1=begin+chord*.32+normal*bow,c2=end-chord*.32+normal*bow;
                    for(int i=0;i<=96;++i){double t=i/96.,v=1-t;auto q=begin*(v*v*v)+c1*(3*v*v*t)+c2*(3*v*t*t)+end*(t*t*t)-begin;candidate.route.emplace_back(core::dot(q,chord)/square,core::dot(q,normal)/square);}
                    double score=crossings(candidate)*1e8+mechanismRouteScore(candidate,obstacles)+mechanismOtherSitePenalty(doc,candidate)+std::abs(bow)*2.;if(score<cost){cost=score;best=std::move(candidate);}
                }p=std::move(best);
                auto points=routedPoints(p);bool blocked=false;for(size_t i=1;i<points.size();++i)if(!routeClear(points[i-1],points[i],p,obstacles,0)){blocked=true;break;}
                if(blocked){auto detour=p;if(findMechanismRoute(detour,obstacles)&&crossings(detour)<=crossings(p))p=std::move(detour);}
                finishAutomaticMechanismEndpoints(doc,p);return;
            }
        }
    }
    if(automatic&&p.electronSource.kind==core::ObjectKind::Bond&&p.sourceVisualAnchor==5)p.begin=mechanismBondOutwardContact(doc,p.electronSource.id,p.begin,p.end-p.begin,true);
    if(automatic&&p.electronTarget.kind==core::ObjectKind::Bond&&p.targetVisualAnchor==5)p.end=mechanismBondOutwardContact(doc,p.electronTarget.id,p.end,p.begin-p.end,true);
    double length=core::length(p.end-p.begin);if(length<1)return;
    const bool shortSpan=length<std::max(18.,std::min(22.,doc.defaultBondLength()*1.25));
    bool sourceLabel=false,targetLabel=false;
    for(const auto& a:doc.atoms().records())if(a.isAlive()&&mechanismHasLabel(doc,a)){auto box=mechanismLabelBox(doc,a);auto covered=[&](core::Vec2 q){return q.x>=box.min.x&&q.x<=box.max.x&&q.y>=box.min.y&&q.y<=box.max.y;};sourceLabel|=covered(p.begin);targetLabel|=covered(p.end);}
    bool localBondTransfer=false,localPiReturn=false;
    if(p.electronSource.kind==core::ObjectKind::Bond && p.electronTarget.kind==core::ObjectKind::Atom){
        auto bond=doc.bonds().alive(p.electronSource.id);
        localBondTransfer=bond&&(bond->beginAtom==p.electronTarget.id||bond->endAtom==p.electronTarget.id);
        localPiReturn=localBondTransfer&&(bond->order==core::BondOrder::Double||bond->order==core::BondOrder::Triple);
    }
    const bool closeLabels=shortSpan&&targetLabel&&(sourceLabel||localBondTransfer);
    // A bond-to-its-own-atom transfer is a local U-turn around the bond.
    // Keep its analytic arc; grid detours produce conspicuous elbows at this scale.
    if(automatic && localBondTransfer)p.route.clear();
    const double preferred=closeLabels?std::max(.7,std::min(1.4,(doc.defaultTextSize()+2.)/length)):shortSpan?.42:.22;
    if(shortSpan&&p.strokeWidth<=0)p.strokeWidth=std::min(.45,doc.defaultLineWidth());
    const auto obstacles=automatic?mechanismObstacles(doc):std::vector<MechanismObstacle>{};
    if(automatic&&shortSpan&&localBondTransfer&&(targetLabel||localPiReturn||p.head.sides==HeadSides::Left||p.head.sides==HeadSides::Right)&&p.targetVisualAnchor<2&&
       routeShortBondReturn(doc,p,originalSource,obstacles)){
        p.targetVisualAnchor=1;
        p.head.length=2.8;p.head.width=1.95;p.strokeWidth=std::min(.5,doc.defaultLineWidth());
        finishAutomaticMechanismEndpoints(doc,p);orientAutomaticFishhook(p);return;
    }
    if(automatic) {
        ArrowParams best=p;
        double score=closeLabels?std::numeric_limits<double>::infinity():mechanismRouteScore(p,obstacles)+mechanismSidePenalty(p)+std::fabs(std::fabs(p.arcSagitta)/length-preferred)*.8;
        for(double ratio:{.18,.28,.42,.6,.85,preferred})for(double sign:{1.0,-1.0}) {
            if(closeLabels&&ratio<preferred*.85)continue;
            ArrowParams candidate=p;candidate.arcSagitta=length*ratio*sign;
            double cost=mechanismRouteScore(candidate,obstacles)+mechanismSidePenalty(candidate)+std::fabs(ratio-preferred)*.8+
                (candidate.arcSagitta*p.arcSagitta<0?.05:0);
            if(closeLabels){std::vector<core::Vec2> pts;Tessellation tess;tess.tolerance=.1;flattenArc(candidate.begin,candidate.end,candidate.arcSagitta,tess,&pts);double outside=0;
                for(size_t i=1;i<pts.size();++i){auto mid=(pts[i-1]+pts[i])*.5;bool covered=false;for(const auto& a:doc.atoms().records())if(a.isAlive()&&mechanismHasLabel(doc,a)){auto box=mechanismLabelBox(doc,a);if(mid.x>=box.min.x&&mid.x<=box.max.x&&mid.y>=box.min.y&&mid.y<=box.max.y){covered=true;break;}}if(!covered)outside+=core::length(pts[i]-pts[i-1]);}
                const double minimumVisible=localBondTransfer?std::max(8.,doc.defaultTextSize()*.85):6.;
                if(outside<minimumVisible)cost+=(minimumVisible-outside)*1000;
            }
            if(cost<score){score=cost;best=candidate;}
        }
        if(!shortSpan&&!localBondTransfer){
            // A cubic can ease out of the donor and into the acceptor with
            // different curvatures, including an inflection (a flowing S).
            const auto chord=p.end-p.begin;const core::Vec2 normal(-chord.y,chord.x);
            for(double sign:{1.,-1.})for(double reach:{.30,.42,.55}){
                auto departure=p.mechanismDeparture,arrival=p.mechanismArrival;
                const bool guided=core::length(departure)>.1&&core::length(arrival)>.1;
                if(!guided){departure=core::Vec2(.65,sign*.76);arrival=core::Vec2(.94,sign*.34);}
                else {
                    if(sign<0)continue;
                    // A mouse path supplies the side and broad end headings;
                    // soften extreme headings so a long flat stroke followed
                    // by a final downward flick does not become an elbow.
                    const double side=p.mechanismSide?p.mechanismSide:(departure.y>=0?1.:-1.);
                    departure=departure*.6+core::Vec2(.65,side*.76)*.4;
                    arrival=arrival*.6+core::Vec2(.94,-side*.34)*.4;
                    departure=departure*(1./core::length(departure));arrival=arrival*(1./core::length(arrival));
                }
                // Turn earlier after departure, then ease into a longer,
                // straighter approach to the chemical target. Unequal handle
                // lengths distribute curvature without changing either heading.
                auto c1=p.begin+(chord*departure.x+normal*departure.y)*(reach*.78);
                auto c2=p.end-(chord*arrival.x+normal*arrival.y)*(reach*1.22);
                auto candidate=p;candidate.route.clear();const double square=core::dot(chord,chord);
                for(int i=0;i<=128;++i){double t=i/128.,v=1-t;auto q=p.begin*(v*v*v)+c1*(3*v*v*t)+c2*(3*v*t*t)+p.end*(t*t*t)-p.begin;candidate.route.emplace_back(core::dot(q,chord)/square,core::dot(q,normal)/square);}
                double cost=mechanismRouteScore(candidate,obstacles)+mechanismSidePenalty(candidate)+mechanismCurvaturePenalty(candidate,std::max(3.,length*.13))-.25+std::fabs(reach-.42)*.1;
                if(cost<score){score=cost;best=std::move(candidate);}
            }
        }
        p=best;
        // Keep a compact unobstructed arc; grid search is for blocked spans or
        // long detours, not tiny local electron transfers around one bond.
        if(!closeLabels&&!localBondTransfer){
            auto points=routedPoints(p);if(points.size()<2){Tessellation tess;tess.tolerance=.05;flattenArc(p.begin,p.end,p.arcSagitta,tess,&points);}
            bool blocked=false;for(size_t i=1;i<points.size();++i)if(!routeClear(points[i-1],points[i],p,obstacles,0)){blocked=true;break;}
            if(blocked)findMechanismRoute(p,obstacles);
        }
    }
    if(automatic && shortSpan && !(localBondTransfer&&targetLabel&&p.targetVisualAnchor<2) && (p.electronSource.valid()||p.electronTarget.valid())) {
        const auto u=(p.end-p.begin)*(1.0/length);const core::Vec2 normal(-u.y,u.x);
        const auto originalBegin=p.begin,originalEnd=p.end;
        ArrowParams best=p;double bestCost=std::numeric_limits<double>::infinity();
        auto boundary=[&](const core::AtomRecord& atom,core::Vec2 n){auto box=mechanismAtomBodyBox(doc,atom,.65);auto center=(box.min+box.max)*.5;double edge=1e30;
            if(std::fabs(n.x)>1e-8)edge=std::min(edge,(n.x>0?box.max.x-center.x:center.x-box.min.x)/std::fabs(n.x));
            if(std::fabs(n.y)>1e-8)edge=std::min(edge,(n.y>0?box.max.y-center.y:center.y-box.min.y)/std::fabs(n.y));return center+n*edge;};
        for(double side:{1.,-1.}){
            auto n=normal*side;auto begin=originalBegin,end=originalEnd;
            if(p.electronSource.kind==core::ObjectKind::Bond&&p.sourceVisualAnchor!=5)begin=begin+n*1.4;
            if(p.electronSource.kind==core::ObjectKind::Atom){auto a=doc.atoms().alive(p.electronSource.id);
                if(a&&mechanismHasLabel(doc,*a)&&p.sourceVisualAnchor==1)begin=boundary(*a,n);}
            if(p.electronTarget.kind==core::ObjectKind::Bond&&p.targetVisualAnchor!=5)end=end+n*1.4;
            if(p.electronTarget.kind==core::ObjectKind::Atom){auto a=doc.atoms().alive(p.electronTarget.id);
                if(a&&mechanismHasLabel(doc,*a)){if(p.targetVisualAnchor<2)end=boundary(*a,n);}else end=end+n*1.2-u*.6;}
            const double height=std::max(doc.defaultTextSize()*.70,length*.38);
            const auto c1=begin+u*(length*.18)+n*height;
            const auto c2=end-u*(length*.30)+n*(height*.72);
            ArrowParams candidate=p;candidate.begin=begin;candidate.end=end;candidate.route.clear();
            auto chord=end-begin;core::Vec2 perpendicular(-chord.y,chord.x);double square=chord.x*chord.x+chord.y*chord.y;if(square<1e-6)continue;
            for(int i=0;i<=96;++i){double t=i/96.,v=1-t;auto q=begin*(v*v*v)+c1*(3*v*v*t)+c2*(3*v*t*t)+end*(t*t*t)-begin;
                candidate.route.push_back(core::Vec2((q.x*chord.x+q.y*chord.y)/square,(q.x*perpendicular.x+q.y*perpendicular.y)/square));}
            if(localBondTransfer&&p.head.sides==HeadSides::Both){
                candidate.arcSagitta=core::length(chord)*.70*side;
                Tessellation tess;tess.tolerance=.005;std::vector<core::Vec2> points;flattenArc(begin,end,candidate.arcSagitta,tess,&points);candidate.route.clear();
                for(auto point:points){auto q=point-begin;candidate.route.emplace_back(core::dot(q,chord)/square,core::dot(q,perpendicular)/square);}
            }
            double cost=mechanismRouteScore(candidate,obstacles)+mechanismCurvaturePenalty(candidate,std::max(2.,doc.defaultTextSize()*.35))+mechanismSidePenalty(candidate)+mechanismOtherSitePenalty(doc,candidate)+(side*p.arcSagitta<0?.15:0.);
            if(cost<bestCost){bestCost=cost;best=candidate;}
        }
        p=best;detourMechanismGlyphs(p,obstacles);p.head.length=2.8;p.head.width=1.95;p.strokeWidth=std::min(.5,doc.defaultLineWidth());finishAutomaticMechanismEndpoints(doc,p);orientAutomaticFishhook(p);return;
    }
    // Small, tangent-aligned heads remain readable at normal bond scale.
    if(automatic){detourMechanismGlyphs(p,obstacles);p.head.length=2.8;p.head.width=1.95;p.strokeWidth=std::min(.5,doc.defaultLineWidth());finishAutomaticMechanismEndpoints(doc,p);orientAutomaticFishhook(p);}
}
// Leave a deliberate gap around printed element labels. The points still aim
// at the requested atom; the visible stroke does not run through its glyph.
inline void clearMechanismLabels(const core::Document& doc,ArrowParams& p) {
    std::vector<core::Vec2> points;Tessellation t;t.tolerance=.005;
    if(p.route.size()>1)points=routedPoints(p);else flattenArc(p.begin,p.end,p.arcSagitta,t,&points);if(points.size()<2)return;
    double total=0;for(size_t i=1;i<points.size();++i)total+=core::length(points[i]-points[i-1]);
    if(total<1e-6)return;
    double gapBegin=0,gapEnd=0;
    auto inside=[](const core::Box& box,core::Vec2 q){return q.x>=box.min.x&&q.x<=box.max.x&&q.y>=box.min.y&&q.y<=box.max.y;};
    auto exitDistance=[&](const core::Box& box,bool reverse){double distance=0;for(size_t k=1;k<points.size();++k){size_t i=reverse?points.size()-k:k;auto from=points[reverse?i:i-1],to=points[reverse?i-1:i];double segment=core::length(to-from);if(!inside(box,to)){double lo=0,hi=1;for(int j=0;j<18;++j){double mid=(lo+hi)*.5;if(inside(box,from+(to-from)*mid))lo=mid;else hi=mid;}return distance+segment*hi;}distance+=segment;}return distance;};
    for(const auto& a:doc.atoms().records())if(a.isAlive()&&mechanismHasLabel(doc,a)) {
        // Cut at the glyph boundary, not at an arbitrary percentage of the
        // chord. Charged and radical carbons also have visible atom labels.
        auto box=mechanismAtomBodyBox(doc,a,.65);
        if(a.semantics.charge){auto charge=mechanismChargeBox(doc,a,.65);box.add(charge.min);box.add(charge.max);}
        auto sourceBox=box,targetBox=box;
        bool isSource=p.electronSource.kind==core::ObjectKind::Atom&&p.electronSource.id==a.id;
        bool isTarget=p.electronTarget.kind==core::ObjectKind::Atom&&p.electronTarget.id==a.id;
        if(isSource&&p.sourceVisualAnchor==1)sourceBox=mechanismAtomBodyBox(doc,a,.65);
        if(isSource&&p.sourceVisualAnchor==2)sourceBox=mechanismChargeBox(doc,a,.65);
        if(isTarget&&p.targetVisualAnchor==1)targetBox=mechanismAtomBodyBox(doc,a,.65);
        if(isTarget&&p.targetVisualAnchor==2)targetBox=mechanismChargeBox(doc,a,.65);
        bool deliberateSource=isSource&&(p.sourceVisualAnchor==3||p.sourceVisualAnchor==4||(p.sourceVisualAnchor==0&&core::length(p.begin-a.position)>doc.defaultTextSize()*.3));
        if(inside(sourceBox,p.begin)&&!deliberateSource)gapBegin=std::max(gapBegin,exitDistance(sourceBox,false));
        if(!(isTarget&&p.targetVisualAnchor==4)&&inside(targetBox,p.end))gapEnd=std::max(gapEnd,exitDistance(targetBox,true));
    }
    if(gapBegin+gapEnd>=total*.95)return; // No visible outside segment: preserve editable geometry.
    if(gapBegin>0||gapEnd>0) {
        auto trim=[](std::vector<core::Vec2>& pts,double gap){while(gap>0&&pts.size()>1){double d=core::length(pts[1]-pts[0]);if(d>gap){pts[0]=pts[0]+(pts[1]-pts[0])*(gap/d);break;}gap-=d;pts.erase(pts.begin());}};
        trim(points,gapBegin);std::reverse(points.begin(),points.end());trim(points,gapEnd);std::reverse(points.begin(),points.end());
        p.begin=points.front();p.end=points.back();auto d=p.end-p.begin;core::Vec2 n(-d.y,d.x);double length=d.x*d.x+d.y*d.y;if(length<1e-8)return;p.route.clear();
        for(auto q:points){q=q-p.begin;p.route.push_back(core::Vec2((q.x*d.x+q.y*d.y)/length,(q.x*n.x+q.y*n.y)/length));}
    }
    double visible=0;for(size_t i=1;i<points.size();++i)visible+=core::length(points[i]-points[i-1]);
    if(p.mechanism && visible>3.){
        // Keep the visual weight of electron heads consistent across the page.
        // Only truly tiny remnants need a cap; short chords no longer erase tips.
        p.head.length=std::min(2.8,visible*.30);
        p.head.width=p.head.length*.70;
    }
    orientAutomaticFishhook(p);
}
} }
