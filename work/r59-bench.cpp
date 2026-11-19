#include <windows.h>
#include <gdiplus.h>
#include "next/integration/session.hpp"
#include <chrono>
#include <iostream>
#include <algorithm>
using namespace r62;using namespace core;
int main(){
    Gdiplus::GdiplusStartupInput startup;ULONG_PTR token;
    Gdiplus::GdiplusStartup(&token,&startup,nullptr);
    {
        integration::Session s;
        if(!s.open("work/r59-live-michael.r62doc").ok())return 2;
        s.activateCommand("chooseSelectionTool_Marquee");
        s.selected.clear();
        for(int id=15;id<=22;++id)s.selected.emplace_back(ObjectKind::Atom,id);
        for(auto& b:s.doc.bonds().records())if(b.isAlive()&&b.beginAtom>=15&&b.beginAtom<=22&&b.endAtom>=15&&b.endAtom<=22)s.selected.emplace_back(ObjectKind::Bond,b.id);
        auto v=s.view();v.scale=3;s.setView(v);s.dragFixedLength=true;
        const auto p=s.doc.atoms().alive(18)->position;
        const auto before=s.doc.digest();const auto rebuilds=s.scene.size();
        if(!s.gesture(PointerPhase::Down,p,0).ok())return 3;
        const auto serial=s.serial; std::vector<double> samples;
        for(int i=0;i<60;++i){
            const auto start=std::chrono::steady_clock::now();
            if(!s.gesture(PointerPhase::Move,p+Vec2(4+.25*i,3+.2*i),0).ok())return 4;
            if(s.displayScene().size()<rebuilds/2)return 5;
            const auto stop=std::chrono::steady_clock::now();
            samples.push_back(std::chrono::duration<double,std::milli>(stop-start).count());
        }
        std::sort(samples.begin(),samples.end());
        std::cout<<"events=60 median_ms="<<samples[30]<<" p95_ms="<<samples[56]<<" serial_delta="<<s.serial-serial<<"\n";
        s.cancel(); if(s.doc.digest()!=before)return 6;
    }
    Gdiplus::GdiplusShutdown(token);
}
