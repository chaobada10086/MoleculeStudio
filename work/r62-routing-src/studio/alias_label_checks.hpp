#pragma once
#include "hydrogen_label_checks.hpp"
#include "engine/molecular_render.hpp"
static void aliasLabelCheck(const std::string& dir){
    using namespace r62::integration;
    auto require=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    for(auto name:{"Ph","Me","Et","Boc"})for(auto font:{"Arial","Times New Roman"}){
        Session s;ObjectId middle,left,right,lb,rb;s.doc.setDefaultFont(font);
        require(s.doc.addAtom(Vec2(0,0),6,&middle).ok()&&s.doc.addAtom(Vec2(-12.4708,7.2),6,&left).ok()&&s.doc.addAtom(Vec2(12.4708,7.2),6,&right).ok(),"fixture atoms");
        require(s.doc.addBond(middle,left,&lb).ok()&&s.doc.addBond(middle,right,&rb).ok(),"fixture bonds");
        Vec2 lp=s.doc.atoms().get(left)->position,rp=s.doc.atoms().get(right)->position;
        require(s.submit(nicknames::labelCommand(left,name),"label left").ok()&&s.submit(nicknames::labelCommand(right,name),"label right").ok(),"apply nickname");
        require(core::length(s.doc.atoms().get(left)->position-lp)<1e-9&&core::length(s.doc.atoms().get(right)->position-rp)<1e-9,"label does not move atoms");
        RingGeometry rings(s.doc);auto labels=molecular::labels(s.doc,s.glyphs,rings);
        auto L=rings.build(*s.doc.bonds().get(lb)),R=rings.build(*s.doc.bonds().get(rb));
        molecular::clip(L,*s.doc.bonds().get(lb),labels,1.6);molecular::clip(R,*s.doc.bonds().get(rb),labels,1.6);
        require(!L.shapes.empty()&&!R.shapes.empty(),"both bond strokes visible");double ll=core::length(L.shapes[0].points[1]-L.shapes[0].points[0]),rr=core::length(R.shapes[0].points[1]-R.shapes[0].points[0]);
        std::ofstream(dir+"/lengths.txt",std::ios::app)<<name<<" "<<font<<" "<<ll<<" "<<rr<<"\n";
        if(std::string(name)=="Ph"&&std::abs(ll-rr)>=1.5){std::ostringstream why;why<<"left and right Ph visible lengths differ: "<<font<<" "<<ll<<" "<<rr;throw std::runtime_error(why.str());}
        require(labels.at(left).box.max.x<labels.at(right).box.min.x,"opposite labels extend outward");
        if(std::string(name)=="Ph"&&std::string(font)=="Times New Roman"){
            s.refresh();dragpreview::writeSvg(s.scene,dir+"/phenyl-bonds.svg");
            auto path=dir+"/phenyl-roundtrip.cdxml";require(s.save(path,true).ok(),"save CDXML");Session loaded;require(loaded.open(path).ok(),"reload CDXML");RingGeometry reloadRings(loaded.doc);auto reloadLabels=molecular::labels(loaded.doc,loaded.glyphs,reloadRings);
            require(reloadLabels.size()==2,"roundtrip labels");
            for(const auto& a:loaded.doc.atoms().records())if(a.isAlive()&&loaded.doc.atomLabel(a.id)=="Ph"&&a.position.x<0){auto info=reloadLabels.find(a.id);require(info!=reloadLabels.end()&&info->second.box.max.x<a.position.x+4.5,"reloaded left nickname remains outward");}
            auto& ext=s.doc.extensions(ObjectRef(ObjectKind::Atom,left));ext.set("studio.label.offset","2 3");ext.set("studio.label.text","Ph");RingGeometry nativeRings(s.doc);auto native=molecular::labels(s.doc,s.glyphs,nativeRings);auto baseline=native.at(left).layout.anchor+Vec2(0,native.at(left).layout.metrics.firstBaseline);require(core::length(baseline-(lp+Vec2(2,3)))<1e-7,"preserve imported explicit label position");
            std::ofstream(dir+"/alias-label-check.json")<<"{\"left_visible\":"<<ll<<",\"right_visible\":"<<rr<<",\"source_positions_unchanged\":true,\"native_offsets_preserved\":true,\"cdxml_roundtrip\":true}";
        }
    }
    hydrogenLabelCheck(dir);
}
