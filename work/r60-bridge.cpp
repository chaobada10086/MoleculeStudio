#include <windows.h>
#include <gdiplus.h>
#include <iostream>
#include "next/integration/session.hpp"
#include "engine/mechanism_infer.hpp"
int main(){
using namespace r62;
Gdiplus::GdiplusStartupInput startup;ULONG_PTR token;Gdiplus::GdiplusStartup(&token,&startup,nullptr);
integration::Session s;s.open("work/r60-accept/mechanism-step/A064-A-input.cdxml");
std::vector<arrows::ArrowParams> inputs;
for(auto a:s.doc.arrows().records())if(a.isAlive()){arrows::ArrowParams p;bool ok=false;arrows::readArrow(s.doc,a.id,&p,&ok);if(ok&&p.mechanism)inputs.push_back(p);}
std::vector<mechanism::ElectronMove> moves;auto err=mechanism::inferElectronMoves(s.doc,inputs,&moves);
std::cout<<"infer "<<err.ok()<<" "<<err.message<<"\n";if(err.ok()){std::vector<core::ObjectRef> refs;err=mechanism::generateNextStep(s.doc,moves,&refs);std::cout<<"generate "<<err.ok()<<" "<<err.message<<"\n";}}
