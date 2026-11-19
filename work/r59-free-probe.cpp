#include <windows.h>
#include <gdiplus.h>
#include <iostream>
#include "next/integration/session.hpp"
#include "engine/mechanism_infer.hpp"
int main(){
using namespace r62;
Gdiplus::GdiplusStartupInput startup;ULONG_PTR token;Gdiplus::GdiplusStartup(&token,&startup,nullptr);
integration::Session s;s.open("work/r59-final-mechanism-infer/input.cdxml");
std::vector<arrows::ArrowParams> inputs;
for(int id:{4,5,6}){arrows::ArrowParams p;bool ok=false;arrows::readArrow(s.doc,id,&p,&ok);inputs.push_back(p);}
std::vector<mechanism::ElectronMove> result;auto err=mechanism::inferElectronMoves(s.doc,inputs,&result);
std::cout<<err.ok()<<" "<<err.message<<"\n";
for(auto m:result)std::cout<<int(m.source.kind)<<" "<<(m.source.kind==core::ObjectKind::Atom?s.doc.atoms().alive(m.source.id)->sourceId:s.doc.bonds().alive(m.source.id)->sourceId)<<"\n";
}
