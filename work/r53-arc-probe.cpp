#include <windows.h>
#include <gdiplus.h>
#include <fstream>
#include <iostream>
#include "next/integration/session.hpp"
#include "engine/mechanism_layout.hpp"
#include "engine/catalytic.hpp"
#include "next/persistence/persistence.hpp"
using namespace r62;
int main(){std::cout<<std::unitbuf; Gdiplus::GdiplusStartupInput startup;ULONG_PTR token;Gdiplus::GdiplusStartup(&token,&startup,nullptr);
integration::Session session;auto res=session.open("work/r53-michael/native-composition.cdxml");auto& d=session.doc;std::cout<<"open "<<res.ok()<<" "<<res.message<<" arrows "<<d.arrows().size()<<"\n";
arrows::ArrowParams p;bool ok=false;int id=-1;for(auto &r:d.arrows().records()){arrows::readArrow(d,r.id,&p,&ok);if(ok&&p.mechanism){id=r.id;break;}}
std::cout<<"sites "<<int(p.sourceVisualAnchor)<<" "<<int(p.targetVisualAnchor)<<" begin "<<p.begin.x<<","<<p.begin.y<<" end "<<p.end.x<<","<<p.end.y<<" route "<<p.route.size()<<"\n";
if(!ok)return 4;auto box=arrows::mechanismChargeBox(d,*d.atoms().alive(p.electronSource.id));std::cout<<"box "<<box.min.x<<","<<box.min.y<<" "<<box.max.x<<","<<box.max.y<<"\n";d.removeObject({core::ObjectKind::Arrow,core::ObjectId(id)});auto obstacles=arrows::mechanismObstacles(d);std::cout<<"start score "<<arrows::mechanismRouteScore(p,obstacles)<<"\n";
auto half=(box.max-box.min)*.5,center=(box.min+box.max)*.5,ray=p.end-center;auto scale=std::max(std::abs(ray.x)/(half.x+.95),std::abs(ray.y)/(half.y+.95));p.begin=center+ray*(1./scale);p.route.clear();
auto prev=p.end;for(double offset:{0.,.55,1.,1.5,2.}){p.end=prev+(p.begin-prev)*(offset/core::length(p.begin-prev));std::cout<<"offset "<<offset<<" ";for(double amt:{.1,-.1,.17,-.17,.25,-.25}){p.arcSagitta=amt*core::length(p.end-p.begin);std::cout<<amt<<"="<<arrows::mechanismRouteScore(p,obstacles)<<" ";}std::cout<<"\n";}}
