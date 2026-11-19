#pragma once
#include "mechanism_visual_anchors.hpp"
#include "third_party/tinyxml2/tinyxml2.h"
#include <map>
#include <set>
#include <vector>
#include <functional>
#include <sstream>
namespace studio_electron_graphics {
using namespace r62;using namespace core;
inline bool separateCharge(const Document& doc,const AtomRecord& atom){auto label=doc.atomLabel(atom.id);return arrows::drawChargeMarker(doc,atom)&&label.find('+')==std::string::npos&&label.find("−")==std::string::npos&&(label.empty()||label.back()!='-');}
inline std::string exportSymbols(const Document& doc,const std::map<ObjectId,int32_t>& atomIds,const std::string& source){
    bool needed=false;for(const auto& a:doc.atoms().records())if(a.isAlive())needed|=a.semantics.radical||separateCharge(doc,a);if(!needed)return source;
    tinyxml2::XMLDocument xml;if(xml.Parse(source.c_str())!=tinyxml2::XML_SUCCESS)return source;auto root=xml.RootElement(),page=root?root->FirstChildElement("page"):nullptr;if(!page)return source;
    std::map<int,tinyxml2::XMLElement*> nodes;std::set<std::pair<int,std::string>> represented;int next=1;
    std::function<void(tinyxml2::XMLElement*)> visit=[&](tinyxml2::XMLElement* n){if(n->Attribute("id")){int id=n->IntAttribute("id");nodes[id]=n;next=std::max(next,id+1);}if(std::string(n->Name())=="represent"&&n->Attribute("attribute"))represented.insert({n->IntAttribute("object"),n->Attribute("attribute")});for(auto child=n->FirstChildElement();child;child=child->NextSiblingElement())visit(child);};visit(root);
    auto symbol=[&](int owner,const char* property,const char* type,Vec2 a,Vec2 b,double lineWidth){auto g=xml.NewElement("graphic");g->SetAttribute("id",next++);g->SetAttribute("GraphicType","Symbol");g->SetAttribute("SymbolType",type);std::ostringstream box;box.precision(14);box<<a.x<<' '<<a.y<<' '<<b.x<<' '<<b.y;g->SetAttribute("BoundingBox",box.str().c_str());g->SetAttribute("LineWidth",lineWidth);auto node=nodes.find(owner);if(node!=nodes.end()&&node->second->Attribute("color"))g->SetAttribute("color",node->second->Attribute("color"));auto rep=xml.NewElement("represent");rep->SetAttribute("object",owner);rep->SetAttribute("attribute",property);g->InsertEndChild(rep);auto destination=page;if(node!=nodes.end()){auto parent=node->second->Parent();while(parent){auto element=parent->ToElement();if(element&&std::string(element->Name())=="page"){destination=element;break;}parent=parent->Parent();}}destination->InsertEndChild(g);};
    for(const auto& entry:atomIds){auto n=nodes.find(entry.second);if(n==nodes.end())continue;if(auto text=n->second->FirstChildElement("t"))for(auto run=text->FirstChildElement("s");run;run=run->NextSiblingElement("s")){std::string value=run->GetText()?run->GetText():"";if((run->IntAttribute("face")&64)&&(value.find('+')!=std::string::npos||value.find('-')!=std::string::npos||value.find("−")!=std::string::npos))represented.insert({entry.second,"Charge"});}}
    const auto& boxes=arrows::mechanismVisualBoxes(doc);
    for(const auto& atom:doc.atoms().records())if(atom.isAlive()){auto file=atomIds.find(atom.id);if(file==atomIds.end())continue;const int id=file->second;auto box=boxes.find(atom.id);
        if(atom.semantics.radical&&!represented.count({id,"Radical"})){const auto points=arrows::mechanismRadicalPoints(doc,atom.id);double radius=arrows::mechanismRadicalRadius(doc,atom.id);for(auto point:points)symbol(id,"Radical","Electron",point,point+Vec2(radius*9,0),doc.defaultLineWidth());}
        if(separateCharge(doc,atom)&&!represented.count({id,"Charge"})&&box!=boxes.end()&&box->second.hasCharge){auto point=(box->second.charge.min+box->second.charge.max)*.5;double em=integration::molecular::setting(doc,"LabelSize",10),radius=em*.19;bool circle=integration::molecular::circledElectronCharge(doc,atom.id);double lineWidth=doc.defaultLineWidth();
            // Native SVG measurements: a circled symbol uses 4/9 of the
            // handle distance for its radius, plus half the 0.8-width stroke.
            // Its size handle is not the radius of the inner plus/minus sign.
            double span=std::max(.1,radius-lineWidth*.5)*4.5;
            if(circle){radius=integration::molecular::electronChargeCircleRadius(em);lineWidth=std::max(.6,em*.08)/.8;span=std::max(.1,radius-lineWidth*.4)*2.25;}
            symbol(id,"Charge",atom.semantics.charge>0?(circle?"CirclePlus":"Plus"):(circle?"CircleMinus":"Minus"),point,point+Vec2(span,0),lineWidth);}
    }
    tinyxml2::XMLPrinter printer;xml.Print(&printer);return printer.CStr();
}
inline void importSymbols(Document& doc){
    std::map<int,ObjectId> atoms;for(const auto& a:doc.atoms().records())if(a.isAlive())atoms[a.sourceId]=a.id;
    std::map<ObjectId,std::vector<Vec2>> radicals;std::vector<ObjectId> consumed;
    for(const auto& item:doc.symbols().records())if(item.isAlive()){
        auto raw=doc.extensions(ObjectRef(ObjectKind::Symbol,item.id)).findValue("studio.cdxml.graphic");if(!raw)continue;tinyxml2::XMLDocument xml;if(xml.Parse(raw->c_str())!=tinyxml2::XML_SUCCESS)continue;auto graphic=xml.RootElement();if(!graphic)continue;auto type=graphic->Attribute("SymbolType");if(!type)continue;Vec2 a,b;std::istringstream points(graphic->Attribute("BoundingBox")?graphic->Attribute("BoundingBox"):"");if(!(points>>a.x>>a.y>>b.x>>b.y))continue;bool used=false;
        for(auto rep=graphic->FirstChildElement("represent");rep;rep=rep->NextSiblingElement("represent")){auto owner=atoms.find(rep->IntAttribute("object"));if(owner==atoms.end())continue;auto atom=doc.atoms().alive(owner->second);std::string property=rep->Attribute("attribute")?rep->Attribute("attribute"):"",kind=type;
            if(property=="Radical"&&atom->semantics.radical&&(kind=="Electron"||kind=="LonePair")){radicals[atom->id].push_back(a);if(kind=="LonePair")radicals[atom->id].push_back(b);used=true;}
            if(property=="Charge"&&std::abs(atom->semantics.charge)==1&&(kind=="Plus"||kind=="Minus"||kind=="CirclePlus"||kind=="CircleMinus")){double em=integration::molecular::setting(doc,"LabelSize",10);auto offset=(a-atom->position)*(1/em);std::ostringstream out;out.precision(14);out<<offset.x<<' '<<offset.y;auto& bag=doc.extensions(ObjectRef(ObjectKind::Atom,atom->id));bag.set("pst.attr.MoleculeStudioChargePosition",out.str());if(kind=="CirclePlus"||kind=="CircleMinus")bag.set("pst.attr.MoleculeStudioChargeCircle","1");else bag.erase("pst.attr.MoleculeStudioChargeCircle");used=true;}
        }if(used)consumed.push_back(item.id);
    }
    for(const auto& entry:radicals){auto atom=doc.atoms().alive(entry.first);Vec2 center;for(auto point:entry.second)center=center+point;center=center*(1./entry.second.size());double em=integration::molecular::setting(doc,"LabelSize",10);auto offset=(center-atom->position)*(1/em);std::ostringstream out;out.precision(14);out<<offset.x<<' '<<offset.y;doc.extensions(ObjectRef(ObjectKind::Atom,atom->id)).set("pst.attr.MoleculeStudioRadicalPosition",out.str());}
    for(auto id:consumed)doc.removeObject(ObjectRef(ObjectKind::Symbol,id));
}
}
