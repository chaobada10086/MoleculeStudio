#pragma once
#include "third_party/tinyxml2/tinyxml2.h"
#include <functional>
#include <map>
#include <vector>
#include <algorithm>
#include <sstream>
#include <cmath>
namespace studio_projection_export {
inline std::string strip(const std::string& bytes){
    if(bytes.find("MoleculeStudioProjection")==std::string::npos)return bytes;
    tinyxml2::XMLDocument xml;if(xml.Parse(bytes.c_str())!=tinyxml2::XML_SUCCESS)return bytes;
    // ChemDraw retains standard object tags while discarding private attributes.
    // Consume those tags before normal parsing (native files may reuse tag IDs).
    std::function<void(tinyxml2::XMLElement*)> tags=[&](tinyxml2::XMLElement* parent){for(auto e=parent->FirstChildElement();e;){auto next=e->NextSiblingElement();std::string label=e->Attribute("Name")?e->Attribute("Name"):"";auto equal=label.find('=');if(std::string(e->Name())=="objecttag"&&label.compare(0,24,"MoleculeStudioProjection")==0&&equal!=std::string::npos){parent->SetAttribute(label.substr(0,equal).c_str(),label.substr(equal+1).c_str());parent->DeleteChild(e);}else tags(e);e=next;}};tags(xml.RootElement());
    std::function<void(tinyxml2::XMLElement*)> walk=[&](tinyxml2::XMLElement* parent){for(auto e=parent->FirstChildElement();e;){auto next=e->NextSiblingElement();if(e->Attribute("MoleculeStudioProjectionInk")||(e->Attribute("Name")&&std::string(e->Attribute("Name"))=="MoleculeStudioProjectionInk"))parent->DeleteChild(e);else{if(e->Attribute("MoleculeStudioProjectionWarning")){e->DeleteAttribute("IgnoreWarnings");e->DeleteAttribute("MoleculeStudioProjectionWarning");}if(e->Attribute("MoleculeStudioProjectionHidden")){e->DeleteAttribute("Visible");e->DeleteAttribute("MoleculeStudioProjectionHidden");}walk(e);}e=next;}};walk(xml.RootElement());tinyxml2::XMLPrinter out;xml.Print(&out);return out.CStr();
}
inline std::string write(const std::string& input){
    if(input.find("MoleculeStudioProjection=")==std::string::npos)return input;
    tinyxml2::XMLDocument xml;auto bytes=strip(input);if(xml.Parse(bytes.c_str())!=tinyxml2::XML_SUCCESS)return input;auto root=xml.RootElement();int next=1;
    std::map<int,tinyxml2::XMLElement*> nodes;std::vector<tinyxml2::XMLElement*> bonds;
    std::function<void(tinyxml2::XMLElement*)> scan=[&](tinyxml2::XMLElement* e){next=std::max(next,e->IntAttribute("id")+1);if(std::string(e->Name())=="n")nodes[e->IntAttribute("id")]=e;if(std::string(e->Name())=="b")bonds.push_back(e);for(auto c=e->FirstChildElement();c;c=c->NextSiblingElement())scan(c);};scan(root);
    auto number=[](double x){std::ostringstream s;s.precision(14);s<<x;return s.str();};auto point=[&](double x,double y){return number(x)+" "+number(y);};
    double radius=root->DoubleAttribute("BondLength",30)*.44,width=root->DoubleAttribute("LineWidth",.6);
    auto pageOf=[&](tinyxml2::XMLElement* node){for(auto p=node->Parent();p;p=p->Parent())if(p->ToElement()&&std::string(p->Value())=="page")return p->ToElement();return root->FirstChildElement("page");};
    auto graphic=[&](tinyxml2::XMLElement* page,const char* type){auto g=xml.NewElement("graphic");g->SetAttribute("id",next++);g->SetAttribute("Name","MoleculeStudioProjectionInk");g->SetAttribute("MoleculeStudioProjectionInk","1");g->SetAttribute("GraphicType",type);g->SetAttribute("LineWidth",width);page->InsertEndChild(g);return g;};
    auto ignoreOverlap=[](tinyxml2::XMLElement* e){if(!e->Attribute("IgnoreWarnings")){e->SetAttribute("IgnoreWarnings","yes");e->SetAttribute("MoleculeStudioProjectionWarning","1");}};
    for(auto pair:nodes){auto n=pair.second;if(n->IntAttribute("MoleculeStudioProjection",-1)/10!=2)continue;if(n->IntAttribute("Element",6)==6)ignoreOverlap(n);bool rear=n->IntAttribute("MoleculeStudioProjection")==21;double x=0,y=0;std::istringstream pos(n->Attribute("p")?n->Attribute("p"):"");if(!(pos>>x>>y))continue;auto page=pageOf(n);if(!page)continue;double r=rear?radius:std::max(width,root->DoubleAttribute("LabelSize",10)*.055);auto g=graphic(page,"Oval");g->SetAttribute("OvalType",rear?"Circle":"Circle Filled");if(!rear)g->SetAttribute("FillType","Solid");g->SetAttribute("BoundingBox",(point(x-r,y-r)+" "+point(x+r,y+r)).c_str());g->SetAttribute("Center3D",(point(x,y)+" 0").c_str());g->SetAttribute("MajorAxisEnd3D",(point(x+r,y)+" 0").c_str());g->SetAttribute("MinorAxisEnd3D",(point(x,y+r)+" 0").c_str());
        if(!rear)continue;for(auto b:bonds){int a=b->IntAttribute("B"),z=b->IntAttribute("E");if(a!=pair.first&&z!=pair.first)continue;auto found=nodes.find(a==pair.first?z:a);if(found==nodes.end())continue;auto other=found->second;b->SetAttribute("Visible","no");b->SetAttribute("MoleculeStudioProjectionHidden","1");if(other->IntAttribute("MoleculeStudioProjection",-1)/10==2){ignoreOverlap(b);continue;}double ox=0,oy=0;std::istringstream p(other->Attribute("p")?other->Attribute("p"):"");if(!(p>>ox>>oy))continue;double length=std::hypot(ox-x,oy-y);if(length<1e-8)continue;auto line=graphic(page,"Line");line->SetAttribute("BoundingBox",(point(x+(ox-x)*radius/length,y+(oy-y)*radius/length)+" "+point(ox,oy)).c_str());}
    }
    std::function<void(tinyxml2::XMLElement*)> metadata=[&](tinyxml2::XMLElement* e){for(auto a=e->FirstAttribute();a;a=a->Next())if(std::string(a->Name()).compare(0,24,"MoleculeStudioProjection")==0){auto tag=xml.NewElement("objecttag");tag->SetAttribute("id",next++);tag->SetAttribute("Name",(std::string(a->Name())+"="+a->Value()).c_str());e->InsertEndChild(tag);}for(auto c=e->FirstChildElement();c;c=c->NextSiblingElement())metadata(c);};metadata(root);
    tinyxml2::XMLPrinter out;xml.Print(&out);return out.CStr();
}
}
