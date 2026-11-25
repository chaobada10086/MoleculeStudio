#pragma once
#include <sstream>
#include <memory>
#include <cmath>
#include "engine/projection_molecule.hpp"

// The three projections share native atoms and bonds. Their view metadata
// changes layout and Newman decoration without replacing molecular identity.
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

struct MoleculeBuilder {
    std::shared_ptr<Session> s;double L;
    MoleculeBuilder(const Document& style):s(std::make_shared<Session>()),L(style.defaultBondLength()){s->doc.setDefaultBondLength(L);s->doc.setDefaultFont(style.defaultFont());s->doc.setDefaultTextSize(style.defaultTextSize());s->doc.setDefaultLineWidth(style.defaultLineWidth());}
    ObjectId atom(Vec2 p,const std::string& label=""){ObjectId id;auto e=s->doc.addAtom(p,6,&id);if(!e.ok())throw std::runtime_error(e.message);if(!label.empty()){auto element=chemistry::elementInfoBySymbol(label.c_str(),int(label.size()));if(element&&element->number){e=s->doc.setElement(id,element->number);}else e=nicknames::apply(s->doc,id,label);if(!e.ok())throw std::runtime_error(e.message);}return id;}
    ObjectId core(Vec2 p,int kind,int plane=0){auto id=atom(p);projection_molecule::mark(s->doc,id,kind,plane);return id;}
    void bond(ObjectId a,ObjectId b,int order=1){ObjectId id;auto e=s->doc.addBond(a,b,&id);if(!e.ok())throw std::runtime_error(e.message);if(order!=1)s->doc.setBondOrder(id,BondOrder(order));}
    void tip(ObjectId a,Vec2 p,const std::string& label="",int order=1){bond(a,atom(p,label),order);}
    std::shared_ptr<Session> done(){projection_molecule::seed(s->doc);s->refresh();return s;}
};
inline std::shared_ptr<Session> sugar(const Document& style,int index,bool mirror,const char* secondCarbonGroup="OH",const char* terminalGroup="CH2OH"){
    const auto& item=sugars().at(index);MoleculeBuilder b(style);double L=b.L;int n=int(std::string(item.hydroxyl).size());double first=-(n-1)*L*.75,last=-first;
    auto top=b.atom(Vec2(0,first-L*(item.ketose?2.5:1.)),item.ketose?"CH2OH":"CHO");
    if(item.ketose){auto ketone=b.atom(Vec2(0,first-1.5*L));b.bond(top,ketone);b.tip(ketone,Vec2(L*.85,first-1.5*L),"O",2);top=ketone;}
    for(int i=0;i<n;++i){
        const double y=first+i*L*1.5;
        auto c=b.core(Vec2(0,y),0);b.bond(top,c);top=c;
        const bool right=(item.hydroxyl[i]=='R')!=mirror;
        const char* group=i==0 && !item.ketose ? secondCarbonGroup : "OH";
        b.tip(c,Vec2(-L,y),right?"H":group);b.tip(c,Vec2(L,y),right?group:"H");
    }
    b.tip(top,Vec2(0,last+L),terminalGroup);return b.done();
}
inline std::shared_ptr<Session> make(const Document& style,int kind,int variant){
    MoleculeBuilder b(style);double L=b.L;
    if(kind==Fischer){int n=std::max(1,std::min(12,variant));double first=-(n-1)*L*.75;auto previous=b.atom(Vec2(0,first-L));
        for(int i=0;i<n;++i){Vec2 p(0,first+i*1.5*L);auto c=b.core(p,0);b.bond(previous,c);previous=c;b.tip(c,p+Vec2(-L,0));b.tip(c,p+Vec2(L,0));}b.tip(previous,Vec2(0,-first+L));
    }else{
        Vec2 front=kind==Sawhorse?Vec2(-1.4*L,.65*L):Vec2(),back=kind==Sawhorse?Vec2(1.4*L,-.65*L):Vec2();auto a=b.core(front,kind,0),z=b.core(back,kind,1);b.bond(a,z);
        for(int plane=0;plane<2;++plane)for(int i=0;i<3;++i){double angle=kind==Sawhorse?i*120.+(plane&&variant==0?60.:0.):-90.+120.*i+(variant==0?(plane?60.:0.):(plane?8.:-8.));double t=angle*3.141592653589793/180.;Vec2 ray(std::cos(t),std::sin(t));if(kind==Sawhorse){ray.x+=.35*ray.y;ray=ray*(1/length(ray));}b.tip(plane?z:a,(plane?back:front)+ray*(L*(kind==Newman?1.12:plane?.82:.9)));}
    }
    return b.done();
}
}
