from pathlib import Path
p=Path('studio/engine/projection_molecule.hpp');p.write_text(r'''#pragma once
#include "next/core/document.hpp"
#include <cmath>
#include <sstream>
#include <set>
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
} }
''',encoding='utf-8')
p=Path('studio/projection_templates.hpp');s=p.read_text(encoding='utf-8');a=s.index('inline std::shared_ptr<Session> sugar(');s=s[:a]+r'''
struct MoleculeBuilder {
    std::shared_ptr<Session> s;double L;
    MoleculeBuilder(const Document& style):s(std::make_shared<Session>()),L(style.defaultBondLength()){s->doc.setDefaultBondLength(L);s->doc.setDefaultFont(style.defaultFont());s->doc.setDefaultTextSize(style.defaultTextSize());s->doc.setDefaultLineWidth(style.defaultLineWidth());}
    ObjectId atom(Vec2 p,const std::string& label=""){ObjectId id;auto e=s->doc.addAtom(p,6,&id);if(!e.ok())throw std::runtime_error(e.message);if(!label.empty()){e=nicknames::apply(s->doc,id,label);if(!e.ok())throw std::runtime_error(e.message);}return id;}
    ObjectId core(Vec2 p,int kind,int plane=0){auto id=atom(p);projection_molecule::mark(s->doc,id,kind,plane);return id;}
    void bond(ObjectId a,ObjectId b,int order=1){ObjectId id;auto e=s->doc.addBond(a,b,&id);if(!e.ok())throw std::runtime_error(e.message);if(order!=1)s->doc.setBondOrder(id,BondOrder(order));}
    void tip(ObjectId a,Vec2 p,const std::string& label="",int order=1){bond(a,atom(p,label),order);}
    std::shared_ptr<Session> done(){s->refresh();return s;}
};
inline std::shared_ptr<Session> sugar(const Document& style,int index,bool mirror){
    const auto& item=sugars().at(index);MoleculeBuilder b(style);double L=b.L;int n=int(std::string(item.hydroxyl).size());double first=-(n-1)*L*.75,last=-first;
    auto top=b.atom(Vec2(0,first-L*(item.ketose?2.5:1.)),item.ketose?"CH2OH":"CHO");
    if(item.ketose){auto ketone=b.atom(Vec2(0,first-1.5*L));b.bond(top,ketone);b.tip(ketone,Vec2(L*.85,first-1.5*L),"O",2);top=ketone;}
    for(int i=0;i<n;++i){auto c=b.core(Vec2(0,first+i*L*1.5),0);b.bond(top,c);top=c;bool right=(item.hydroxyl[i]=='R')!=mirror;b.tip(c,Vec2(-L,first+i*L*1.5),right?"H":"OH");b.tip(c,Vec2(L,first+i*L*1.5),right?"OH":"H");}
    b.tip(top,Vec2(0,last+L),"CH2OH");return b.done();
}
inline std::shared_ptr<Session> make(const Document& style,int kind,int variant){
    MoleculeBuilder b(style);double L=b.L;
    if(kind==Fischer){int n=std::max(1,std::min(12,variant));double first=-(n-1)*L*.75;auto previous=b.atom(Vec2(0,first-L));
        for(int i=0;i<n;++i){Vec2 p(0,first+i*1.5*L);auto c=b.core(p,0);b.bond(previous,c);previous=c;b.tip(c,p+Vec2(-L,0));b.tip(c,p+Vec2(L,0));}b.tip(previous,Vec2(0,-first+L));
    }else{
        Vec2 front=kind==Sawhorse?Vec2(-1.4*L,.65*L):Vec2(),back=kind==Sawhorse?Vec2(1.4*L,-.65*L):Vec2();auto a=b.core(front,kind,0),z=b.core(back,kind,1);b.bond(a,z);
        for(int plane=0;plane<2;++plane)for(int i=0;i<3;++i){double angle=kind==Sawhorse?i*120.+(plane&&variant==0?60.:0.):-90.+120.*i+(variant==0?(plane?60.:0.):(plane?8.:-8.));double t=angle*3.141592653589793/180.;Vec2 ray(std::cos(t),std::sin(t));if(kind==Sawhorse){ray.x+=.35*ray.y;ray=ray*(1/length(ray));}b.tip(plane?z:a,(plane?back:front)+ray*(L*(kind==Newman?1.12:plane?.82:.9)));}
    }
    return b.done();
}
}
''';s=s.replace('#include <cmath>','#include <cmath>\n#include "engine/projection_molecule.hpp"');p.write_text(s,encoding='utf-8')
p=Path('studio/interop.hpp');s=p.read_text(encoding='utf-8').replace('v.key=="pst.attr.MoleculeStudioRole"','v.key.compare(0,29,"pst.attr.MoleculeStudioProject")==0||v.key=="pst.attr.MoleculeStudioRole"');p.write_text(s,encoding='utf-8')
p=Path('studio/engine/session.cpp');s=p.read_text(encoding='utf-8');s='#include "projection_molecule.hpp"\n'+s
needle='        auto b=storedBond;';s=s.replace(needle,needle+r'''
        if(projection_molecule::axis(doc,b)&&projection_molecule::kind(doc,b.beginAtom)==2)continue;
''',1)
needle='        clipBridgeCrossing(doc,b,data);';s=s.replace(needle,needle+r'''
        for(auto id:{b.beginAtom,b.endAtom})if(projection_molecule::rear(doc,id)){
            auto center=doc.atoms().alive(id)->position;double radius=projection_molecule::radius(doc);
            for(auto& shape:data.shapes)for(auto& point:shape.points)if(length(point-center)<radius){auto other=doc.atoms().alive(id==b.beginAtom?b.endAtom:b.beginAtom)->position;auto direction=other-center;double span=length(direction);if(span>1e-8)point=center+direction*(radius/span);}
        }
''',1)
needle='    stamp(1);';s=s.replace(needle,r'''
    for(const auto& atom:doc.atoms().records())if(atom.isAlive()&&projection_molecule::kind(doc,atom.id)==2&&wanted(ObjectKind::Atom,atom.id)){
        bool rear=projection_molecule::rear(doc,atom.id);double radius=rear?projection_molecule::radius(doc):std::max(doc.defaultLineWidth(),doc.defaultTextSize()*.055);
        render::Primitive circle;circle.source=ObjectRef(ObjectKind::Atom,atom.id);circle.kind=rear?render::PrimKind::Polyline:render::PrimKind::Polygon;circle.layer=render::LayerId(100);circle.z=12;circle.style.stroke=render::Paint(0x000000FF,true);circle.style.strokeWidth=doc.defaultLineWidth();circle.style.fill=render::Paint(0x000000FF,!rear);
        for(int i=0;i<=48;++i){double t=i*6.28318530718/48;circle.points.push_back(atom.position+Vec2(cos(t),sin(t))*radius);}circle.bounds=render::geom::computeBounds(circle,.6);ps.push_back(circle);
    }
'''+needle,1);p.write_text(s,encoding='utf-8')
