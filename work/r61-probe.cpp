#include <windows.h>
#include <gdiplus.h>
#include <iostream>
#include "next/integration/session.hpp"
#include "next/arrows_curves/preset.hpp"
#include "engine/mechanism_layout.hpp"
int main(){
using namespace r62;using namespace core;using namespace arrows;
Gdiplus::GdiplusStartupInput startup;ULONG_PTR token;Gdiplus::GdiplusStartup(&token,&startup,nullptr);
integration::Session s;auto& doc=s.doc;
s.open("work/r61-routing-original.r62doc");
auto report=[&](const ArrowParams& p){ auto pts=routedPoints(p);if(pts.empty()){Tessellation t;t.tolerance=.01;flattenArc(p.begin,p.end,p.arcSagitta,t,&pts);}
auto cross=[](Vec2 a,Vec2 b,Vec2 c){auto u=b-a,v=c-a;return u.x*v.y-u.y*v.x;};
for(auto bond:doc.bonds().records())if(bond.isAlive()){auto a=doc.atoms().alive(bond.beginAtom)->position,b=doc.atoms().alive(bond.endAtom)->position;
for(size_t i=1;i<pts.size();++i)if(cross(pts[i-1],pts[i],a)*cross(pts[i-1],pts[i],b)<0&&cross(a,b,pts[i-1])*cross(a,b,pts[i])<0){std::cout<<" crosses "<<bond.id;break;}}};
Document orig=doc;
for(bool single:{true,false})for(double dx:{71.,74.9,78.}){doc=orig;if(single){for(auto a:orig.arrows().records())if(a.isAlive()&&a.id!=13)doc.removeObject(ObjectRef(ObjectKind::Arrow,a.id));}
s.refresh();s.move({ObjectRef(ObjectKind::Atom,53)},Vec2(dx,-8.1));ArrowParams p;bool ok;readArrow(doc,13,&p,&ok);std::cout<<"dx "<<dx<<" single "<<single;report(p);Document q=doc;q.removeObject(ObjectRef(ObjectKind::Arrow,13));avoidBondAtLabel(q,p,mechanismObstacles(q));std::cout<<" repaired ";report(p);std::cout<<" begin "<<p.begin.x<<","<<p.begin.y<<" end "<<p.end.x<<","<<p.end.y<<" samples "<<p.route.size()<<"\n";}
}