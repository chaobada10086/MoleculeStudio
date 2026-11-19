#include <windows.h>
#include <gdiplus.h>
#include <iostream>
#include "next/integration/session.hpp"
#include "r60-diagnostic/mechanism_step.hpp"
int main(){
using namespace r62;
Gdiplus::GdiplusStartupInput startup;ULONG_PTR token;Gdiplus::GdiplusStartup(&token,&startup,nullptr);
using namespace core;
integration::Session s;s.open("work/r60-cases/mechanism-step/user-hydrazine-input.r62doc");auto& d=s.doc;
auto A=[](ObjectId id){return ObjectRef(ObjectKind::Atom,id);};auto B=[&](ObjectId a,ObjectId b){return ObjectRef(ObjectKind::Bond,d.bondBetween(a,b));};
ObjectId leave=kInvalidObjectId;for(auto bid:d.incidentBonds(209)){auto b=d.bonds().alive(bid);auto other=b->beginAtom==209?b->endAtom:b->beginAtom;if(d.atoms().alive(other)->element==7)leave=other;}
std::vector<mechanism::ElectronMove> m={{B(209,211),A(211),2},{A(211),B(209,211),2},{A(220),A(209),2},{B(209,leave),A(leave),2}};
std::vector<ObjectRef> made;auto err=mechanism::generateNextStep(d,m,&made);std::cout<<err.message<<"\n";}