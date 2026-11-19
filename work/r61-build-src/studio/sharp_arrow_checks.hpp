#pragma once
static void checkSharpArrows(App& app,const std::string& dir){
    auto require=[](bool ok,const char* why){if(!ok)throw std::runtime_error(std::string("sharp arrows: ")+why);};
    int tips=0,pairs=0;Session gallery;gallery.doc.setDefaultFont("Arial");gallery.doc.setDefaultTextSize(9);
    for(double angle:{0.,45.,90.,180.,270.})for(double width:{.4,.6,1.2,2.4})for(int tier:{0,2,4}){
        auto u=core::direction(angle);auto normal=Vec2(-u.y,u.x);
        for(uint32_t preset:{0u,2u,5u,49u,57u,58u}){
            auto p=arrows::withHeadTier(arrows::resolvePreset(preset,Vec2(),u*100,width),tier);
            if(preset==49||preset==57){p.mechanism=true;p.head.length=3.2;p.head.width=2.24;p.strokeWidth=width;}
            for(double bend:{-.35,0.,.35}){p.arcSagitta=bend*100;p.family=bend?arrows::ArrowFamily::Arc:arrows::ArrowFamily::Straight;p.sweep=bend?arrows::ArcSweep::Free:arrows::ArcSweep::Straight;
                arrows::ArrowGeometry g;require(arrows::buildArrowGeometry(p,width,arrows::Tessellation(),&g).ok(),"matrix builds");
                for(const auto& head:g.heads){require(head.filled&&head.outline.size()==(preset==57?3u:4u)&&head.strokes.empty(),"single filled fishhook or swept full head replaces blunt strokes");
                    auto axis=core::direction(head.angleDeg);for(auto q:head.outline)require(dot(q-head.tip,axis)<1e-7,"no head vertex protrudes past tip");
                    for(const auto& shaft:g.shafts)for(auto q:shaft)require(length(q-head.tip)>g.strokeWidth*.2,"shaft cap cannot cover sharp apex");++tips;}
            }
        }
        for(double ratio:{.28,.5,.72}){auto p=arrows::withHeadTier(arrows::resolvePreset(38,Vec2(),u*100,width),tier);p.splitRatio=ratio;
            arrows::ArrowGeometry g;require(arrows::buildArrowGeometry(p,width,arrows::Tessellation(),&g).ok()&&g.heads.size()==2,"two harpoons");
            const auto& top=g.shafts[0];const auto& bottom=g.shafts[1];
            double topTail=dot(top.front(),u),bottomTail=dot(bottom.back(),u);
            if(ratio<.5){require(std::abs(bottomTail-100)<1e-7,"reverse trend aligned right");require(std::abs(topTail-100*(1-ratio/(1-ratio)))<1e-7,"upper short arrow has correct length");}
            if(ratio>.5){require(std::abs(topTail)<1e-7,"forward trend aligned left");require(std::abs(bottomTail-100*(1-ratio)/ratio)<1e-7,"lower short arrow has correct length");}
            for(const auto& head:g.heads)require(head.filled&&head.outline.size()==4&&head.strokes.empty(),"pointed harpoon");++pairs;
        }
    }
    int row=0;for(uint32_t preset:{0u,2u,38u,39u,39u,49u,57u,57u}){
        double y=35+row*80;auto p=arrows::withHeadTier(arrows::resolvePreset(preset,Vec2(80,y),Vec2(250,y),.6),2);
        if(row==3)p.splitRatio=.28;if(row==4)p.splitRatio=.72;
        if(preset==49||preset==57){p.mechanism=true;p.head.length=5;p.head.width=3.5;p.arcSagitta=row==7?-30:30;}
        ObjectId id;require(arrows::addArrow(gallery.doc,p,.6,&id).ok(),"gallery arrow");gallery.doc.addText(Vec2(12,y-7),8,std::to_string(row+1),&id);++row;
    }
    gallery.refresh();require(gallery.save(dir+"/sharp-arrow-gallery.cdxml",true).ok(),"export gallery");Session reopened;require(reopened.open(dir+"/sharp-arrow-gallery.cdxml").ok(),"reopen gallery");
    int count=0;for(const auto& arrow:reopened.doc.arrows().records())if(arrow.isAlive())++count;require(count==8,"no duplicate companion heads");
    for(const auto& path:reopened.doc.paths().records())require(!path.isAlive(),"vector companions removed after semantic reload");
    auto& s=app.s;s.cancel();s.doc=gallery.doc;s.history.clear();s.selected.clear();s.refresh();s.fit();app.invalidate();app.screenshot(dir+"/sharp-arrow-gallery.png",false,false);
    std::ofstream(dir+"/sharp-arrow-checks.json")<<"{\"sharp_tip_cases\":"<<tips<<",\"equilibrium_cases\":"<<pairs<<",\"pointed_fishhooks\":true,\"notched_ordinary_heads\":true,\"both_bias_directions\":true,\"end_aligned\":true,\"cdxml_round_trip\":true}";
}
