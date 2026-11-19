#include <windows.h>
#include <gdiplus.h>
#include <iostream>
#include "next/integration/session.hpp"
#include "engine/mechanism_infer.hpp"
int main(){
using namespace r62;
Gdiplus::GdiplusStartupInput startup;ULONG_PTR token;Gdiplus::GdiplusStartup(&token,&startup,nullptr);
integration::Session s;s.open("work/r60-original.r62doc");
std::vector<arrows::ArrowParams> inputs;
for(int id:{17,19,20}){arrows::ArrowParams p;bool ok=false;arrows::readArrow(s.doc,id,&p,&ok);inputs.push_back(p);}
std::vector<mechanism::ElectronMove> result;auto err=mechanism::inferElectronMoves(s.doc,inputs,&result);
std::cout<<err.ok()<<" "<<err.message<<"\n";
for(auto a:s.doc.atoms().records())if(a.isAlive()&&a.id>=57)std::cout<<a.id<<" elem "<<int(a.element)<<" h "<<int(a.semantics.implicitHydrogens)<<" q "<<int(a.semantics.charge)<<" type "<<int(a.semantics.nodeType)<<" label "<<s.doc.atomLabel(a.id)<<"\n";
std::vector<mechanism::ElectronMove> moves;for(auto p:inputs)moves.emplace_back(p.electronSource,p.electronTarget,2);
std::vector<core::ObjectRef> made;auto e=mechanism::generateNextStep(s.doc,moves,&made);std::cout<<e.ok()<<" direct "<<e.message<<"\n";}