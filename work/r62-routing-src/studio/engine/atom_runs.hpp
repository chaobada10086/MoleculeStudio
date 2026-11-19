#ifndef STUDIO_ATOM_RUNS_HPP
#define STUDIO_ATOM_RUNS_HPP
#include "next/core/document.hpp"
#include "third_party/tinyxml2/tinyxml2.h"
namespace studioatom {
// Slab IDs are only unique within ObjectKind. Document::runs belongs to Text;
// using an Atom slab ID there overwrites a completely unrelated caption.
inline void set(r62::core::Document& doc,r62::core::ObjectId id,const std::vector<r62::core::TextRun>& runs){
 tinyxml2::XMLDocument xml;auto root=xml.NewElement("runs");xml.InsertEndChild(root);
 for(auto& r:runs){auto s=xml.NewElement("s");s->SetAttribute("font",r.fontName.c_str());s->SetAttribute("size",r.size);s->SetAttribute("flags",unsigned(r.flags));s->SetText(r.utf8.c_str());root->InsertEndChild(s);}
 tinyxml2::XMLPrinter out;xml.Print(&out);doc.extensions(r62::core::ObjectRef(r62::core::ObjectKind::Atom,id)).set("studio.atom.runs",out.CStr());
}
inline std::vector<r62::core::TextRun> runs(const r62::core::Document& doc,r62::core::ObjectId id){
 std::vector<r62::core::TextRun> result;auto raw=doc.extensions(r62::core::ObjectRef(r62::core::ObjectKind::Atom,id)).findValue("studio.atom.runs");if(!raw)return result;
 tinyxml2::XMLDocument xml;if(xml.Parse(raw->c_str())!=tinyxml2::XML_SUCCESS||!xml.RootElement())return result;
 for(auto s=xml.RootElement()->FirstChildElement("s");s;s=s->NextSiblingElement("s")){r62::core::TextRun r;r.utf8=s->GetText()?s->GetText():"";r.fontName=s->Attribute("font")?s->Attribute("font"):"";r.size=s->DoubleAttribute("size");r.flags=uint16_t(s->UnsignedAttribute("flags"));result.push_back(r);}return result;
}
}
#endif
