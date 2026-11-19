#include <windows.h>
#include <gdiplus.h>
#include <iostream>
#include "next/integration/session.hpp"
#include "engine/mechanism_infer.hpp"
int main(){
using namespace r62;
Gdiplus::GdiplusStartupInput startup;ULONG_PTR token;Gdiplus::GdiplusStartup(&token,&startup,nullptr);
using namespace core;auto require=[](bool ok,const std::string& msg){if(!ok)throw std::runtime_error(msg);};
    for(int mode=0;mode<7;++mode){
        Document d;d.setDefaultBondLength(30);
        std::array<Vec2,6> points={Vec2(0,0),Vec2(-26,15),Vec2(-26,45),Vec2(0,60),Vec2(38,45),Vec2(38,15)};
        if(mode==3){points[0]=Vec2(-52,0);points[4]=Vec2(125,45);points[5]=Vec2(125,15);}
        std::array<ObjectId,6> atoms;
        for(size_t i=0;i<6;++i)d.addAtom(points[i],6,&atoms[i]);
        auto bond=[&](int a,int b,int order){ObjectId id;d.addBond(atoms[a],atoms[b],&id);d.setBondOrder(id,BondOrder(order));return ObjectRef(ObjectKind::Bond,id);};
        const auto ab=bond(0,1,2),bc=bond(1,2,1),cd=bond(2,3,2),ef=bond(4,5,mode==4?3:2);
        if(mode==2||mode==5){
            // Distinguishable termini on both partners make the two pairings different.
            for(auto i:{0,4}){auto state=d.atoms().alive(atoms[i])->semantics;state.isotope=13;d.setAtomSemantics(atoms[i],state);}
        }
        const int e=mode==2?5:4,f=e==4?5:4;
        auto atom=[&](int i){return ObjectRef(ObjectKind::Atom,atoms[i]);};
        auto arrow=[&](ObjectRef src,ObjectRef dst,Vec2 head){
            Vec2 tail;arrows::mechanismAnchorPoint(d,src,&tail);
            auto p=arrows::resolvePreset(49,tail,head,.6);p.mechanism=true;p.electronSource=src;p.electronTarget=dst;return p;
        };
        auto toward=[&](int a,int b){return (points[a]+points[b])*.5;};
        std::vector<arrows::ArrowParams> input={
            arrow(ab,bc,toward(1,2)),arrow(cd,atom(e),points[e]),arrow(ef,atom(0),points[0])};
        if(mode==1){
            input={arrow(ab,ObjectRef(),toward(1,2)),arrow(cd,ObjectRef(),toward(3,e)),arrow(ef,ObjectRef(),toward(f,0))};
        }else if(mode==2||mode>=5){
            input[1]=arrow(cd,ef,mode>=5?toward(e,f):points[e]);
            input[2]=arrow(ef,ab,points[0]);
        }
        std::rotate(input.begin(),input.begin()+mode%3,input.end());
        std::vector<ObjectRef> refs;
        for(auto p:input){ObjectId id;require(arrows::addArrow(d,p,.6,&id).ok(),"create full arrow");refs.emplace_back(ObjectKind::Arrow,id);}

        std::vector<mechanism::ElectronMove> moves;auto err=mechanism::inferElectronMoves(d,input,&moves);
        std::cout<<"mode "<<mode<<" infer "<<err.ok()<<" "<<err.message<<"\n";
        if(err.ok()){std::vector<ObjectRef> made;err=mechanism::generateNextStep(d,moves,&made);std::cout<<"product "<<err.ok()<<" "<<err.message<<"\n";}
    }
}
