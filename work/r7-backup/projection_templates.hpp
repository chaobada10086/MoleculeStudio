#pragma once
#include <sstream>
#include <memory>
#include <cmath>

// These are editable diagram templates. Standard CDXML groups, lines, ovals
// and captions preserve the drawing in ChemDraw without private render types.
namespace stereo_projection {
using namespace r62;using namespace core;using namespace integration;
enum Kind { Fischer=0,Sawhorse=1,Newman=2 };
inline const char* name(int kind){return kind==Fischer?"费歇尔投影":kind==Sawhorse?"锯架式":"纽曼投影";}
struct Builder {
    tinyxml2::XMLDocument xml; tinyxml2::XMLElement* root; tinyxml2::XMLElement* group;int next=3;double unit,em,width;
    Builder(const Document& style):unit(style.defaultBondLength()),em(style.defaultTextSize()),width(style.defaultLineWidth()){
        root=xml.NewElement("CDXML");xml.InsertEndChild(root);root->SetAttribute("BondLength",unit);root->SetAttribute("LineWidth",width);root->SetAttribute("LabelSize",em);root->SetAttribute("CaptionSize",em);root->SetAttribute("LabelFont",3);root->SetAttribute("CaptionFont",3);
        auto fonts=xml.NewElement("fonttable"),font=xml.NewElement("font");font->SetAttribute("id",3);font->SetAttribute("name",style.defaultFont().c_str());fonts->InsertEndChild(font);root->InsertEndChild(fonts);
        auto page=xml.NewElement("page");page->SetAttribute("id",1);root->InsertEndChild(page);group=xml.NewElement("group");group->SetAttribute("id",2);page->InsertEndChild(group);
    }
    std::string point(Vec2 p){std::ostringstream s;s.precision(14);s<<p.x<<' '<<p.y;return s.str();}
    void graphic(const char* type,Vec2 a,Vec2 b,bool filled=false){
        auto e=xml.NewElement("graphic");e->SetAttribute("id",next++);e->SetAttribute("GraphicType",type);e->SetAttribute("BoundingBox",(point(a)+" "+point(b)).c_str());e->SetAttribute("LineWidth",width);
        if(std::string(type)=="Oval"){e->SetAttribute("OvalType",filled?"Circle Filled":"Circle");if(filled)e->SetAttribute("FillType","Solid");}
        group->InsertEndChild(e);
    }
    void line(Vec2 a,Vec2 b){graphic("Line",a,b);}
    void circle(Vec2 center,double radius,bool filled){
        graphic("Oval",center-Vec2(radius,radius),center+Vec2(radius,radius),filled);
        // ChemDraw needs explicit ellipse axes; its legacy BoundingBox-only
        // fallback interprets one corner as the centre, enlarging the circle.
        auto e=group->LastChildElement();e->SetAttribute("Center3D",(point(center)+" 0").c_str());e->SetAttribute("MajorAxisEnd3D",(point(center+Vec2(radius,0))+" 0").c_str());e->SetAttribute("MinorAxisEnd3D",(point(center+Vec2(0,radius))+" 0").c_str());
    }
    void caption(Vec2 position,const std::string& label,const char* alignment="Center"){
        auto t=xml.NewElement("t");t->SetAttribute("id",next++);t->SetAttribute("p",point(position).c_str());t->SetAttribute("Justification",alignment);t->SetAttribute("InterpretChemically","no");
        // Explicit runs also retain subscripts in native ChemDraw. Inline text
        // editing remains available after insertion, including empty templates.
        if(label.empty())return;
        auto run=[&](const std::string& value,int face){if(value.empty())return;auto s=xml.NewElement("s");s->SetAttribute("font",3);s->SetAttribute("size",em);if(face)s->SetAttribute("face",face);s->SetText(value.c_str());t->InsertEndChild(s);};
        for(size_t i=0;i<label.size();){bool digit=label[i]>='0'&&label[i]<='9';size_t j=i+1;while(j<label.size()&&((label[j]>='0'&&label[j]<='9')==digit))++j;run(label.substr(i,j-i),digit?32:0);i=j;}group->InsertEndChild(t);
    }
    void terminal(Vec2 origin,Vec2 ray,const std::string& label,double length=1.,double side=0){
        Vec2 end=origin+ray*(unit*length),normal(-ray.y,ray.x);line(origin,end);
        Vec2 p=end+ray*(unit*.2)+normal*(side*em);
        p.y+=ray.y<-.65?0:ray.y>.65?em*.72:em*.32;
        caption(p,label,side!=0?"Center":ray.x>.3?"Left":ray.x<-.3?"Right":"Center");
    }
};
struct Sugar {const char* title;const char* hydroxyl;bool ketose;};
inline const std::vector<Sugar>& sugars(){static const std::vector<Sugar> data={
    {"甘油醛","R",false},{"赤藓糖","RR",false},{"苏阿糖","LR",false},
    {"核糖","RRR",false},{"阿拉伯糖","LRR",false},{"木糖","RLR",false},{"来苏糖","LLR",false},
    {"阿洛糖","RRRR",false},{"阿卓糖","LRRR",false},{"葡萄糖","RLRR",false},{"甘露糖","LLRR",false},
    {"古洛糖","RRLR",false},{"艾杜糖","LRLR",false},{"半乳糖","RLLR",false},{"塔罗糖","LLLR",false},
    {"核酮糖","RR",true},{"木酮糖","LR",true},{"果糖","LRR",true}};return data;}
inline std::shared_ptr<Session> finish(Builder& b){
    tinyxml2::XMLPrinter printer;b.xml.Print(&printer);auto source=std::make_shared<Session>();auto e=persistence::readCdxml(printer.CStr(),source->doc);if(!e.ok())throw std::runtime_error(e.message);source->refresh();return source;
}
inline std::shared_ptr<Session> sugar(const Document& style,int index,bool mirror){
    const auto& item=sugars().at(index);Builder b(style);b.group->SetAttribute("Name","Fischer projection");
    const double L=b.unit;int n=int(std::string(item.hydroxyl).size());double first=-(n-1)*L*.75,last=-first;
    // The ketone is an explicit C=O cross above the stereogenic centres.
    double top=first-L*(item.ketose?2.5:1.);b.line(Vec2(0,top),Vec2(0,last+L));
    b.caption(Vec2(0,top-b.em*.3),item.ketose?"CH2OH":"CHO");b.caption(Vec2(0,last+L+b.em),"CH2OH");
    if(item.ketose){double y=first-1.5*L,half=L*.06;b.line(Vec2(0,y-half),Vec2(L*.85,y-half));b.line(Vec2(0,y+half),Vec2(L*.85,y+half));b.caption(Vec2(L*1.03,y+b.em*.32),"O","Left");}
    for(int i=0;i<n;++i){bool right=(item.hydroxyl[i]=='R')!=mirror;Vec2 c(0,first+i*L*1.5);b.terminal(c,Vec2(-1,0),right?"H":"HO");b.terminal(c,Vec2(1,0),right?"OH":"H");}
    return finish(b);
}
inline std::shared_ptr<Session> make(const Document& style,int kind,int variant){
    Builder b(style);const double L=b.unit;
    b.group->SetAttribute("Name",kind==Fischer?"Fischer projection":kind==Sawhorse?"Sawhorse projection":"Newman projection");
    if(kind==Fischer){
        int centers=std::max(1,std::min(12,variant));double first=-(centers-1)*L*.75,last=-first;
        b.line(Vec2(0,first-L),Vec2(0,last+L));
        b.caption(Vec2(0,first-L-b.em*.3),"");b.caption(Vec2(0,last+L+b.em),"");
        for(int i=0;i<centers;++i){Vec2 c(0,first+i*L*1.5);b.terminal(c,Vec2(-1,0),"");b.terminal(c,Vec2(1,0),"");}
    }else if(kind==Sawhorse){
        Vec2 front(-1.4*L,.65*L),back(1.4*L,-.65*L);b.line(front,back);
        const double turn=variant==0?60.:0.;
        for(int plane=0;plane<2;++plane)for(int i=0;i<3;++i){double a=(i*120.+(plane?turn:0.))*3.141592653589793/180.;Vec2 ray(std::cos(a)+.35*std::sin(a),std::sin(a));ray=ray*(1./length(ray));b.terminal(plane?back:front,ray,"",plane?.82:.9);}
    }else{
        const double radius=L*.44;b.circle(Vec2(),radius,false);
        // A small conventional separation makes both eclipsed bonds visible.
        // Front spokes start at the dot; rear spokes start at the circumference.
        for(int plane=1;plane>=0;--plane)for(int i=0;i<3;++i){
            double a=(-90.+120.*i+(variant==0?(plane?60.:0.):(plane?8.:-8.)))*3.141592653589793/180.;Vec2 ray(std::cos(a),std::sin(a));
            b.line(ray*(plane?radius:0.),ray*(L*1.12));
        }
        b.circle(Vec2(),std::max(b.width,b.em*.055),true);
    }
    return finish(b);
}
}
