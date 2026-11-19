#pragma once
#include "third_party/tinyxml2/tinyxml2.h"
#include <string>
#include <vector>
#include <sstream>
#include <functional>
#include <cmath>
#include <map>
#include <algorithm>

namespace studio_projection_io {
inline bool name(const char* n){const std::string s=n?n:"";return s=="Fischer projection"||s=="Sawhorse projection"||s=="Newman projection";}
// ChemDraw drops group names when writing CDX. Recover a projection only
// from its vector topology, never from a chemical fragment or an arbitrary group.
inline std::string identify(const tinyxml2::XMLElement* group){
    if(name(group->Attribute("Name")))return group->Attribute("Name");
    if(std::string(group->Name())!="group")return "";
    struct Point {double x,y;};struct Edge {Point a,b;};std::vector<Edge> lines;std::vector<std::pair<Point,double>> circles;int texts=0;
    auto near=[](Point a,Point b){return std::hypot(a.x-b.x,a.y-b.y)<.001;};
    auto point=[](const char* raw,Point& p){if(!raw)return false;std::istringstream in(raw);return bool(in>>p.x>>p.y)&&std::isfinite(p.x)&&std::isfinite(p.y);};
    for(auto e=group->FirstChildElement();e;e=e->NextSiblingElement()){
        std::string tag=e->Name();if(tag=="t"){++texts;continue;}
        if(tag=="graphic"&&e->Attribute("GraphicType")&&std::string(e->Attribute("GraphicType"))=="Oval"){
            Point c,u;if(!point(e->Attribute("Center3D"),c)||!point(e->Attribute("MajorAxisEnd3D"),u))return "";circles.push_back({c,std::hypot(c.x-u.x,c.y-u.y)});continue;
        }
        Edge edge;
        if(tag=="graphic"&&e->Attribute("GraphicType")&&std::string(e->Attribute("GraphicType"))=="Line"){
            std::istringstream in(e->Attribute("BoundingBox")?e->Attribute("BoundingBox"):"");if(!(in>>edge.a.x>>edge.a.y>>edge.b.x>>edge.b.y))return "";
        }else if(tag=="arrow"){
            auto head=e->Attribute("ArrowheadHead"),tail=e->Attribute("ArrowheadTail");if((head&&std::string(head)!="None")||(tail&&std::string(tail)!="None")||std::abs(e->DoubleAttribute("AngularSize"))>1e-8||!point(e->Attribute("Head3D"),edge.a)||!point(e->Attribute("Tail3D"),edge.b))return "";
        }else return "";
        if(near(edge.a,edge.b))return "";bool duplicate=false;for(auto old:lines)if((near(old.a,edge.a)&&near(old.b,edge.b))||(near(old.a,edge.b)&&near(old.b,edge.a)))duplicate=true;if(!duplicate)lines.push_back(edge);
    }
    if(texts<=10&&circles.size()==2&&(lines.size()==3||lines.size()==6)&&near(circles[0].first,circles[1].first)&&std::min(circles[0].second,circles[1].second)<std::max(circles[0].second,circles[1].second)*.25)return "Newman projection";
    if(!circles.empty())return "";
    if(texts<=10&&lines.size()==7){
        std::vector<Point> vertices;std::vector<int> degree;
        for(auto e:lines)for(auto p:{e.a,e.b}){size_t i=0;for(;i<vertices.size();++i)if(near(vertices[i],p))break;if(i==vertices.size()){vertices.push_back(p);degree.push_back(0);}++degree[i];}
        if(vertices.size()==8&&std::count(degree.begin(),degree.end(),4)==2&&std::count(degree.begin(),degree.end(),1)==6)return "Sawhorse projection";
    }
    if(lines.size()>=3&&lines.size()<=25&&lines.size()%2==1&&texts<=int(lines.size()+1)+12)for(size_t i=0;i<lines.size();++i){
        auto stem=lines[i];double dx=stem.b.x-stem.a.x,dy=stem.b.y-stem.a.y,len=std::hypot(dx,dy);bool match=true;std::vector<std::pair<Point,int>> junctions;
        for(size_t j=0;j<lines.size();++j)if(j!=i){auto e=lines[j];double ex=e.b.x-e.a.x,ey=e.b.y-e.a.y;if(std::abs(dx*ex+dy*ey)>len*std::hypot(ex,ey)*.001){match=false;break;}
            auto on=[&](Point p){double t=((p.x-stem.a.x)*dx+(p.y-stem.a.y)*dy)/(len*len);return t>.001&&t<.999&&std::abs((p.x-stem.a.x)*dy-(p.y-stem.a.y)*dx)<len*.001;};Point c;
            if(on(e.a))c=e.a;else if(on(e.b))c=e.b;else{match=false;break;}size_t k=0;for(;k<junctions.size();++k)if(near(junctions[k].first,c)||std::hypot(junctions[k].first.x-c.x,junctions[k].first.y-c.y)<len*.025)break;if(k==junctions.size())junctions.push_back({c,0});++junctions[k].second;
        }
        if(match){for(auto j:junctions)match&=j.second==2;if(match)return "Fischer projection";}
    }
    return "";
}
// ChemDraw saves each native vector line together with a modern headless arrow.
// Within a projection diagram retain one editable line, never two coincident
// objects. Other document arrows and groups are outside this conversion.
inline std::string normalize(const std::string& bytes){
    if(bytes.find("<group")==std::string::npos)return bytes;
    tinyxml2::XMLDocument xml;if(xml.Parse(bytes.c_str())!=tinyxml2::XML_SUCCESS)return bytes;bool changed=false;
    auto point=[](const char* raw,double* x,double* y){if(!raw)return false;std::istringstream in(raw);return bool(in>>*x>>*y);};
    std::function<void(tinyxml2::XMLElement*,bool)> walk=[&](tinyxml2::XMLElement* parent,bool diagram){
        auto kind=identify(parent);if(!kind.empty()){diagram=true;if(!name(parent->Attribute("Name"))){parent->SetAttribute("Name",kind.c_str());changed=true;}}
        for(auto e=parent->FirstChildElement();e;){auto next=e->NextSiblingElement();
            auto head=e->Attribute("ArrowheadHead"),tail=e->Attribute("ArrowheadTail");
            if(diagram&&std::string(e->Name())=="arrow"&&(!head||std::string(head)=="None")&&(!tail||std::string(tail)=="None")&&std::abs(e->DoubleAttribute("AngularSize"))<1e-8){
                double ax,ay,bx,by;if(point(e->Attribute("Head3D"),&ax,&ay)&&point(e->Attribute("Tail3D"),&bx,&by)){
                    bool duplicate=false;
                    for(auto line=parent->FirstChildElement("graphic");line;line=line->NextSiblingElement("graphic"))if(line->Attribute("GraphicType")&&std::string(line->Attribute("GraphicType"))=="Line"){
                        double x,y,z,v;std::istringstream in(line->Attribute("BoundingBox")?line->Attribute("BoundingBox"):"");if(!(in>>x>>y>>z>>v))continue;
                        auto near=[](double a,double b){return std::abs(a-b)<.001;};
                        if((near(x,ax)&&near(y,ay)&&near(z,bx)&&near(v,by))||(near(x,bx)&&near(y,by)&&near(z,ax)&&near(v,ay))){duplicate=true;line->DeleteAttribute("SupersededBy");break;}
                    }
                    if(!duplicate){auto line=xml.NewElement("graphic");line->SetAttribute("id",e->IntAttribute("id"));line->SetAttribute("GraphicType","Line");std::ostringstream box;box.precision(14);box<<ax<<' '<<ay<<' '<<bx<<' '<<by;line->SetAttribute("BoundingBox",box.str().c_str());for(const char* key:{"LineWidth","color","Z","LineType"})if(auto value=e->Attribute(key))line->SetAttribute(key,value);parent->InsertAfterChild(e,line);}
                    parent->DeleteChild(e);changed=true;e=next;continue;
                }
            }
            walk(e,diagram);e=next;
        }
    };walk(xml.RootElement(),false);if(!changed)return bytes;tinyxml2::XMLPrinter out;xml.Print(&out);return out.CStr();
}
}
