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
for(int preset:{49,57})for(bool single:{true,false})for(double dx:{71.,74.9,78.}){
 doc=orig;
 if(single)for(auto a:orig.arrows().records())if(a.isAlive()&&a.id!=13)doc.removeObject(ObjectRef(ObjectKind::Arrow,a.id));
 ArrowParams p;bool ok;readArrow(doc,13,&p,&ok);
 if(preset==57){auto hook=resolvePreset(57,p.begin,p.end,.6);p.presetId=57;p.head=hook.head;writeArrow(doc,13,p,.6);}
 s.refresh();auto old=doc.digest();auto e=s.move({ObjectRef(ObjectKind::Atom,53)},Vec2(dx,-8.1));
 if(!e.ok())return 2;readArrow(doc,13,&p,&ok);if(!ok||p.electronSource.id!=53||p.electronTarget.id!=54)return 3;
 std::cout<<preset<<" move "<<dx<<" single "<<single;report(p);std::cout<<"\n";
 if(single&&dx==74.9&&preset==49){auto refs=s.all();for(auto ref:refs){
 if(ref.kind==ObjectKind::Atom&&ref.id>=40&&ref.id<=54)continue;
 if(ref.kind==ObjectKind::Arrow&&ref.id==13)continue;
 if(ref.kind==ObjectKind::Bond){auto* b=doc.bonds().alive(ref.id);if(b&&b->beginAtom>=40&&b->beginAtom<=54&&b->endAtom>=40&&b->endAtom<=54)continue;}
 doc.removeObject(ref);}
 s.refresh();s.save("work/r61-sample.cdxml",true);
 }else if(!s.undo().ok()||doc.digest()!=old)return 4;
}
}