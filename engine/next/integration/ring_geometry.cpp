#include "ring_geometry.hpp"
#include "next/bonds/edit.hpp"
#include <map>
#include <deque>
#include <cmath>
namespace r62 { namespace integration {
using namespace core;
static double cross(Vec2 a,Vec2 b){return a.x*b.y-a.y*b.x;}
RingGeometry::RingGeometry(const Document& d):doc(d),edges(d.atoms().size()){
    for(const auto& b:d.bonds().records())if(b.isAlive()){
        edges[b.beginAtom].push_back({b.endAtom,b.id});edges[b.endAtom].push_back({b.beginAtom,b.id});
    }
}
bonds::BondRenderData RingGeometry::build(const BondRecord& b,const std::set<ObjectRef>* moving,Vec2 delta) const{
    auto pos=[&](ObjectId id){Vec2 p=doc.atoms().get(id)->position;return moving&&moving->count(ObjectRef(ObjectKind::Atom,id))?p+delta:p;};
    Vec2 a=pos(b.beginAtom),z=pos(b.endAtom);
    bonds::BondAxes axes;axes.order=b.order;axes.display=b.display;axes.semantics=b.semantics;axes.stereo=b.stereo;
    const auto& ext=doc.extensions(ObjectRef(ObjectKind::Bond,b.id));axes.side=bonds::sideOf(ext);
    bonds::BondGeometryOptions opts;opts.lineWidth=doc.defaultLineWidth();
    auto data=bonds::buildBondGeometry(a,z,axes,opts);
    if(b.order!=BondOrder::Double||b.display!=BondDisplay::Normal||b.stereo!=BondStereo::None||axes.side!=bonds::DoubleSide::Symmetric)return data;
    // Shortest alternate path, bounded to 8-membered rings / 256 local vertices.
    // No whole-document traversal on a pointer move, including acyclic 50k files.
    std::map<ObjectId,std::pair<ObjectId,int> > visited;
    std::deque<ObjectId> todo;todo.push_back(b.beginAtom);visited[b.beginAtom]={b.beginAtom,0};
    while(!todo.empty()&&visited.size()<256&&!visited.count(b.endAtom)){
        auto id=todo.front();todo.pop_front();int depth=visited[id].second;if(depth>=7)continue;
        for(auto e:edges[id])if(e.second!=b.id&&!visited.count(e.first)){visited[e.first]={id,depth+1};todo.push_back(e.first);}
    }
    if(!visited.count(b.endAtom)||visited[b.endAtom].second<2)return data;
    std::vector<ObjectId> path;for(ObjectId id=b.endAtom;;id=visited[id].first){path.push_back(id);if(id==b.beginAtom)break;}
    Vec2 u=z-a;double len=length(u);if(len<1e-6)return data;u=u*(1/len);
    Vec2 center;for(auto id:path)center=center+pos(id);center=center*(1.0/path.size());
    double sign=cross(u,center-a)>0?1:-1;Vec2 n(-u.y*sign,u.x*sign);
    // Only an unambiguous convex face; do not invent an inward side for crossings.
    for(auto id:path)if(dot(pos(id)-a,n)<-1e-6)return data;
    double spacing=bonds::multipleBondSpacing(len,opts);
    Vec2 va=pos(path[path.size()-2])-a,vz=pos(path[1])-z;
    if(length(va)<1e-6||length(vz)<1e-6)return data;
    va=va*(1/length(va));vz=vz*(1/length(vz));
    // Inner line ends sit on the interior angle bisector (radius) through each
    // vertex, i.e. trimmed by spacing*cot(angle/2). This is general for every
    // ring size.
    double ca=std::abs(cross(u,va)),cz=std::abs(cross(u*(-1),vz));
    if(ca<.1||cz<.1)return data;
    double ta=spacing*(1+dot(u,va))/ca,tz=spacing*(1+dot(u*(-1),vz))/cz;
    if(ta+tz>len*.8)return data;
    data.shapes.clear();bonds::BondShape outer;outer.strokeWidth=opts.lineWidth;outer.points={a,z};
    bonds::BondShape inner=outer;inner.points={a+n*spacing+u*ta,z+n*spacing-u*tz};
    data.shapes={outer,inner};data.note="T19 convex ring projection: continuous axis and inset inner double line";
    return data;
}
} }
