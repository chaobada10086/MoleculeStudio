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
    if(auto value=doc.documentExtensions().findValue("studio.style.BoldWidth"))opts.boldWidth=std::atof(value->c_str());
    if(auto value=doc.documentExtensions().findValue("studio.style.HashSpacing"))opts.hashSpacing=std::atof(value->c_str());
    if(auto value=ext.findValue("pst.attr.LineWidth"))opts.lineWidth=std::atof(value->c_str());
    if(auto value=ext.findValue("pst.attr.BoldWidth"))opts.boldWidth=std::atof(value->c_str());
    if(auto value=ext.findValue("pst.attr.HashSpacing"))opts.hashSpacing=std::atof(value->c_str());
    if(auto value=doc.documentExtensions().findValue("studio.style.BondSpacing"))opts.spacingPercent=std::atof(value->c_str());
    if(auto value=ext.findValue("pst.attr.DoubleBondSpacing"))opts.spacingPercent=std::atof(value->c_str());
    auto explicitSide=ext.findValue("pst.attr.DoublePosition");
    if(!ext.findValue(bonds::kSideExtensionKey)&&explicitSide){
        if(*explicitSide=="Center"||*explicitSide=="0"||*explicitSide=="256")axes.side=bonds::DoubleSide::Symmetric;
        if(*explicitSide=="Left"||*explicitSide=="1"||*explicitSide=="257")axes.side=bonds::DoubleSide::Right;
        if(*explicitSide=="Right"||*explicitSide=="2"||*explicitSide=="258")axes.side=bonds::DoubleSide::Left;
    }
    Vec2 axis=z-a;double axisLength=length(axis);
    bool plainDouble=b.order==BondOrder::Double&&b.display==BondDisplay::Normal&&b.stereo==BondStereo::None;
    if(plainDouble&&axisLength>1e-6){
        Vec2 u=axis*(1/axisLength),n(-u.y,u.x);
        // Automatic terminal doubles (e.g. aldehydes) follow the adjacent bond.
        // Branched carbonyls remain centered; a collinear cumulene stays centered.
        if(!explicitSide&&axes.side==bonds::DoubleSide::Symmetric){
            ObjectId junction=kInvalidObjectId;
            if(edges[b.beginAtom].size()==2&&edges[b.endAtom].size()==1)junction=b.beginAtom;
            if(edges[b.endAtom].size()==2&&edges[b.beginAtom].size()==1)junction=b.endAtom;
            if(junction!=kInvalidObjectId)for(auto e:edges[junction])if(e.second!=b.id){double side=dot(pos(e.first)-pos(junction),n);if(std::abs(side)>1e-6)axes.side=side>0?bonds::DoubleSide::Right:bonds::DoubleSide::Left;}
        }
        // An automatic unbranched alkene keeps one stroke on the skeleton.
        // Centering both strokes leaves the adjoining single bonds dangling.
        // Preserve explicit placement and let the ring projection below handle
        // cycles; carbonyls and cumulenes have different connection rules.
        if(!explicitSide&&!ext.findValue(bonds::kSideExtensionKey)&&axes.side==bonds::DoubleSide::Symmetric&&
           edges[b.beginAtom].size()==2&&edges[b.endAtom].size()==2&&
           doc.atoms().get(b.beginAtom)->element==6&&doc.atoms().get(b.endAtom)->element==6){
            std::set<ObjectId> seen;std::deque<ObjectId> queue;queue.push_back(b.beginAtom);seen.insert(b.beginAtom);
            while(!queue.empty()&&seen.size()<256&&!seen.count(b.endAtom)){
                auto id=queue.front();queue.pop_front();
                for(auto edge:edges[id])if(edge.second!=b.id&&seen.insert(edge.first).second)queue.push_back(edge.first);
            }
            bool singles=true;double side=0;
            for(auto id:{b.beginAtom,b.endAtom})for(auto edge:edges[id])if(edge.second!=b.id){
                if(doc.bonds().get(edge.second)->order!=BondOrder::Single)singles=false;
                double d=dot(pos(edge.first)-pos(id),n);if(std::abs(d)>1e-6)side=d;
            }
            if(queue.empty()&&!seen.count(b.endAtom)&&singles&&std::abs(side)>1e-6)
                axes.side=side>0?bonds::DoubleSide::Right:bonds::DoubleSide::Left;
        }
    }
    auto data=bonds::buildBondGeometry(a,z,axes,opts);
    for(auto& shape:data.shapes)if(shape.strokeWidth<=0)shape.strokeWidth=opts.lineWidth;
    if(plainDouble&&axisLength>1e-6){
        Vec2 u=axis*(1/axisLength),n(-u.y,u.x);
        const double gap=bonds::multipleBondSpacing(axisLength,opts);
        double offsets[2]={-gap*.5,gap*.5};
        if(axes.side==bonds::DoubleSide::Left){offsets[0]=0;offsets[1]=-gap;}
        if(axes.side==bonds::DoubleSide::Right){offsets[0]=0;offsets[1]=gap;}
        // "centered" is the editor's explicit centered placement too.
        data.shapes.clear();
        for(double offset:offsets){
            bonds::BondShape line;line.strokeWidth=opts.lineWidth;
            Vec2 ends[2]={a+n*offset,z+n*offset};ObjectId ids[2]={b.beginAtom,b.endAtom};
            for(int end=0;end<2;++end){
                double nearest=axisLength;bool found=false;
                for(auto edge:edges[ids[end]])if(edge.second!=b.id){
                    Vec2 v=pos(edge.first)-pos(ids[end]);double den=cross(u,v);
                    if(dot(v,n)*offset<=0||std::abs(den)<1e-6)continue;
                    double t=cross(n*(-offset),v)/den;
                    if(axes.side==bonds::DoubleSide::Left||axes.side==bonds::DoubleSide::Right){
                        Vec2 inward=end?u*(-1):u;double l=length(v);double c=std::abs(cross(inward,v*(1/l)));
                        if(c>1e-6)t=(end?-1:1)*std::abs(offset)*(1+dot(inward,v*(1/l)))/c;
                    }
                    if(std::abs(t)<axisLength*.45&&(!found||std::abs(t)<std::abs(nearest))){nearest=t;found=true;}
                }
                if(found)ends[end]=ends[end]+u*nearest;
            }
            line.points={ends[0],ends[1]};data.shapes.push_back(line);
        }
        data.note="ChemDraw double: full spacing between strokes, neighbor intersections";
    }
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
