#pragma once
#include "next/core/document.hpp"
#include <cmath>
#include <sstream>
#include <set>
#include <algorithm>
#include <cstdlib>
namespace r62 { namespace projection_molecule {
using namespace core;
inline const char* key(){return "pst.attr.MoleculeStudioProjection";}
inline int role(const Document& d,ObjectId a){auto v=d.extensions(ObjectRef(ObjectKind::Atom,a)).findValue(key());return v?std::atoi(v->c_str()):-1;}
inline int kind(const Document& d,ObjectId a){int r=role(d,a);return r<0?-1:r/10;}
inline bool rear(const Document& d,ObjectId a){return role(d,a)==21;}
inline bool marked(const Document& d,ObjectId a){return role(d,a)>=0;}
inline void mark(Document& d,ObjectId a,int kind,int plane){d.extensions(ObjectRef(ObjectKind::Atom,a)).set(key(),std::to_string(kind*10+plane));}
inline ObjectId coreFor(const Document& d,ObjectId a){if(marked(d,a))return a;for(auto bid:d.incidentBonds(a)){auto b=d.bonds().alive(bid);auto n=b->beginAtom==a?b->endAtom:b->beginAtom;if(marked(d,n))return n;}return kInvalidObjectId;}
inline bool axis(const Document& d,const BondRecord& b){return marked(d,b.beginAtom)&&marked(d,b.endAtom);}
inline double radius(const Document& d){return d.defaultBondLength()*.44;}
inline std::vector<ObjectId> component(const Document& d,ObjectId start,ObjectId blocked=kInvalidObjectId){std::vector<ObjectId> result;std::set<ObjectId> seen;if(start==blocked)return result;result.push_back(start);seen.insert(start);for(size_t i=0;i<result.size();++i)for(auto bid:d.incidentBonds(result[i])){auto b=d.bonds().alive(bid);auto n=b->beginAtom==result[i]?b->endAtom:b->beginAtom;if(n!=blocked&&seen.insert(n).second)result.push_back(n);}return result;}

struct V3{double x,y,z;V3(double a=0,double b=0,double c=0):x(a),y(b),z(c){}};
inline V3 sub(V3 a,V3 b){return V3(a.x-b.x,a.y-b.y,a.z-b.z);}
inline double parity(const std::vector<V3>& v){if(v.size()!=4)return 0;auto a=sub(v[0],v[3]),b=sub(v[1],v[3]),c=sub(v[2],v[3]);return a.x*(b.y*c.z-b.z*c.y)-a.y*(b.x*c.z-b.z*c.x)+a.z*(b.x*c.y-b.y*c.x);}
inline const char* vectorKey(const BondRecord& b,ObjectId core){return b.beginAtom==core?"pst.attr.MoleculeStudioProjectionBegin":"pst.attr.MoleculeStudioProjectionEnd";}
inline V3 vector(const Document& d,const BondRecord& b,ObjectId c){auto v=d.extensions(ObjectRef(ObjectKind::Bond,b.id)).findValue(vectorKey(b,c));V3 result;if(v){std::istringstream in(*v);in>>result.x>>result.y>>result.z;}return result;}
inline void seed(Document& d){for(const auto& a:d.atoms().records())if(a.isAlive()&&marked(d,a.id))for(auto bid:d.incidentBonds(a.id)){auto b=d.bonds().alive(bid);if(d.extensions(ObjectRef(ObjectKind::Bond,bid)).findValue(vectorKey(*b,a.id)))continue;auto n=b->beginAtom==a.id?b->endAtom:b->beginAtom;auto p=d.atoms().alive(n)->position-a.position;double span=length(p);if(span>1e-8)p=p*(1/span);int k=kind(d,a.id),plane=role(d,a.id)%10;V3 v;
    if(k==0)v=V3(p.x,-p.y,std::abs(p.x)>.5?1:-1);else if(marked(d,n))v=V3(0,0,plane?1:-1);else v=V3(k==1?p.x-.35*p.y:p.x,-p.y,plane?-.6:.6);
    std::ostringstream out;out.precision(17);out<<v.x<<' '<<v.y<<' '<<v.z;d.extensions(ObjectRef(ObjectKind::Bond,bid)).set(vectorKey(*b,a.id),out.str());}}
inline Error convertInPlace(Document& d,ObjectId axisId,int k,int variant){
    if(k<0||k>2)return Error(ErrorCode::InvalidArgument,"未知投影类型");
    auto axisBond=d.bonds().alive(axisId);if(!axisBond)return Error(ErrorCode::NotFound,"请选择投影中的碳碳单键");auto a=axisBond->beginAtom,b=axisBond->endAtom;
    if(d.atoms().alive(a)->element!=6||d.atoms().alive(b)->element!=6||axisBond->order!=BondOrder::Single)return Error(ErrorCode::InvalidArgument,"投影转换需要碳碳单键");
    // Make implicit hydrogens explicit when a one-centre Fischer projection is
    // viewed along a terminal C-C bond. Atom identity and formula stay chemical.
    for(auto c:{a,b}){auto atom=d.atoms().alive(c);if(atom->semantics.nodeType!=uint8_t(NodeType::Element)||atom->semantics.charge||atom->semantics.radical)return Error(ErrorCode::InvalidArgument,"转换轴需为中性饱和碳；请先展开该碳的缩写");auto bonds=d.incidentBonds(c);if(bonds.size()>4)return Error(ErrorCode::InvalidArgument,"投影碳的连接数超过四个");for(auto id:bonds)if(d.bonds().alive(id)->order!=BondOrder::Single)return Error(ErrorCode::InvalidArgument,"投影轴的两个碳需为四面体构型");
        if(atom->semantics.implicitHydrogens>=0&&int(bonds.size())+atom->semantics.implicitHydrogens!=4)return Error(ErrorCode::InvalidArgument,"显式氢数与四面体碳的价态不匹配");
        auto position=atom->position;auto sem=atom->semantics;if(bonds.size()<4){sem.implicitHydrogens=0;d.setAtomSemantics(c,sem);d.extensions(ObjectRef(ObjectKind::Atom,c)).erase("studio.hydrogens.manual");if(d.atomLabel(c).compare(0,2,"CH")==0){d.setAtomLabel(c,"");d.extensions(ObjectRef(ObjectKind::Atom,c)).erase("studio.atom.runs");}}
        for(size_t i=bonds.size();i<4;++i){ObjectId h,edge;double t=i*2.09439510239;auto e=d.addAtom(position+Vec2(cos(t),sin(t))*d.defaultBondLength(),1,&h);if(!e.ok())return e;e=d.addBond(c,h,&edge);if(!e.ok())return e;}if(!marked(d,c))mark(d,c,0,0);
    }
    seed(d);axisBond=d.bonds().alive(axisId);
    std::vector<ObjectId> roots[2];std::vector<std::vector<ObjectId>> branches[2];std::set<ObjectId> used;
    for(int plane=0;plane<2;++plane){auto c=plane?b:a,other=plane?a:b;for(auto bid:d.incidentBonds(c)){auto edge=d.bonds().alive(bid);auto n=edge->beginAtom==c?edge->endAtom:edge->beginAtom;if(n==other)continue;auto branch=component(d,n,c);for(auto id:branch)if(id==other||!used.insert(id).second)return Error(ErrorCode::InvalidArgument,"环连接不能直接展开为此投影");roots[plane].push_back(n);branches[plane].push_back(branch);}}
    for(auto id:component(d,a))if(id!=a&&id!=b&&marked(d,id))mark(d,id,3,0);
    const double L=d.defaultBondLength();auto origin=(d.atoms().alive(a)->position+d.atoms().alive(b)->position)*.5;
    Vec2 centres[2]={origin,origin};if(k==0){centres[0]=origin+Vec2(0,-.75*L);centres[1]=origin+Vec2(0,.75*L);}if(k==1){centres[0]=origin+Vec2(-1.4*L,.65*L);centres[1]=origin+Vec2(1.4*L,-.65*L);}
    for(int plane=0;plane<2;++plane){auto c=plane?b:a;std::vector<Vec2> rays;std::vector<V3> oldV={vector(d,*axisBond,c)},newV;
        if(k==0){rays={Vec2(0,plane?1:-1),Vec2(-1,0),Vec2(1,0)};newV={V3(0,plane?1:-1,-1),V3(0,plane?-1:1,-1),V3(-1,0,1),V3(1,0,1)};}
        else{newV.push_back(V3(0,0,plane?1:-1));for(int i=0;i<3;++i){double angle=k==1?i*120.+(plane&&variant==0?60.:0.):-90.+i*120.+(variant==0?(plane?60.:0.):(plane?8.:-8.));double t=angle*3.141592653589793/180.;Vec2 ray(cos(t),sin(t));newV.push_back(V3(ray.x,-ray.y,plane?-.6:.6));if(k==1){ray.x+=.35*ray.y;ray=ray*(1/length(ray));}rays.push_back(ray);}}
        for(auto n:roots[plane])oldV.push_back(vector(d,*d.bonds().alive(d.bondBetween(c,n)),c));if(parity(oldV)*parity(newV)<0)std::swap(rays[1],rays[2]);
        for(size_t i=0;i<roots[plane].size();++i){
            const auto root=d.atoms().alive(roots[plane][i])->position;
            auto target=centres[plane]+rays[i]*(L*(k==0?1:k==1?(plane?.82:.9):1.12));
            // A substituent is a rigid fragment, not a point label. Rotate its
            // geometry outwards when the projection spoke changes direction.
            // Translation alone left attached rings folded into the new core.
            Vec2 outward;for(auto id:branches[plane][i])outward=outward+(d.atoms().alive(id)->position-root);
            if(length(outward)<1e-8)outward=root-d.atoms().alive(c)->position;
            double angle=std::atan2(rays[i].y,rays[i].x)-std::atan2(outward.y,outward.x),cs=std::cos(angle),sn=std::sin(angle);
            for(auto id:branches[plane][i]){auto p=d.atoms().alive(id)->position-root;d.setAtomPosition(id,target+Vec2(cs*p.x-sn*p.y,sn*p.x+cs*p.y));}
        }
        d.setAtomPosition(c,centres[plane]);mark(d,c,k,plane);
    }
    // Sawhorse spokes may point across the central axis. Choose an outward
    // rigid orientation that also clears the rest of the molecule.
    for(int plane=0;plane<2;++plane)for(size_t i=0;i<branches[plane].size();++i){
        const auto& branch=branches[plane][i];if(branch.size()<2)continue;
        std::set<ObjectId> members(branch.begin(),branch.end());auto root=roots[plane][i],c=plane?b:a;
        Vec2 origin=d.atoms().alive(root)->position,ray=origin-d.atoms().alive(c)->position;ray=ray*(1/length(ray));
        std::vector<Vec2> original;for(auto id:branch)original.push_back(d.atoms().alive(id)->position-origin);
        auto crosses=[](Vec2 a,Vec2 b,Vec2 c,Vec2 d){auto cross=[](Vec2 x,Vec2 y){return x.x*y.y-x.y*y.x;};return cross(b-a,c-a)*cross(b-a,d-a)<-1e-9&&cross(d-c,a-c)*cross(d-c,b-c)<-1e-9;};
        bool clear=false;
        for(int extension=0;extension<13&&!clear;++extension)for(int step=0;step<13&&!clear;++step){
            double angle=(step%2?1.:-1.)*((step+1)/2)*15.*3.141592653589793/180.,cs=cos(angle),sn=sin(angle);auto offset=ray*(extension*L*.25);bool outward=true;
            for(size_t j=0;j<branch.size();++j){auto p=original[j];Vec2 rotated(cs*p.x-sn*p.y,sn*p.x+cs*p.y);if(dot(rotated,ray)<-1e-6)outward=false;d.setAtomPosition(branch[j],origin+offset+rotated);}if(!outward)continue;
            clear=true;
            for(const auto& edge:d.bonds().records())if(edge.isAlive()&&members.count(edge.beginAtom)&&members.count(edge.endAtom)){
                auto x=d.atoms().alive(edge.beginAtom)->position,y=d.atoms().alive(edge.endAtom)->position;
                for(const auto& other:d.bonds().records())if(other.isAlive()&&!members.count(other.beginAtom)&&!members.count(other.endAtom)){
                    if(crosses(x,y,d.atoms().alive(other.beginAtom)->position,d.atoms().alive(other.endAtom)->position)){clear=false;break;}
                }if(!clear)break;
            }
        }
        if(!clear)for(size_t j=0;j<branch.size();++j)d.setAtomPosition(branch[j],origin+original[j]);
    }
    return Error::success();
}
// Conversion is atomic even when called outside the UI undo transaction.
inline Error convert(Document& d,ObjectId axisId,int k,int variant){Document candidate=d;auto e=convertInPlace(candidate,axisId,k,variant);if(e.ok())d=std::move(candidate);return e;}
} }
