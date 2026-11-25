#pragma once
#include "next/core/document.hpp"
#include "next/core/serialization.hpp"
#include "engine/atom_runs.hpp"
#include "third_party/tinyxml2/tinyxml2.h"
namespace textbackup {
using namespace r62;using namespace core;
inline const char* key(){return "studio.text.tidy.original";}
inline bool sameRuns(const std::vector<TextRun>& a,const std::vector<TextRun>& b){if(a.size()!=b.size())return false;for(size_t i=0;i<a.size();++i)if(a[i].utf8!=b[i].utf8||a[i].fontName!=b[i].fontName||a[i].size!=b[i].size||a[i].flags!=b[i].flags)return false;return true;}
inline void save(Document& doc,ObjectRef ref,const std::string& plain,const std::vector<TextRun>& runs,const std::string& colors,bool replace=false){
    auto& bag=doc.extensions(ref);if(!replace&&bag.findValue(key()))return;
    tinyxml2::XMLDocument xml;auto root=xml.NewElement("before");xml.InsertEndChild(root);root->SetAttribute("text",plain.c_str());root->SetAttribute("colors",colors.c_str());
    root->SetAttribute("textHex",hexEncode(plain).c_str());
    for(const auto& run:runs){auto node=xml.NewElement("s");node->SetAttribute("font",run.fontName.c_str());node->SetAttribute("size",run.size);node->SetAttribute("flags",unsigned(run.flags));node->SetAttribute("textHex",hexEncode(run.utf8).c_str());node->SetText(run.utf8.c_str());root->InsertEndChild(node);}
    tinyxml2::XMLPrinter printer;xml.Print(&printer);bag.set(key(),printer.CStr());
}
inline Error restore(Document& doc,ObjectRef ref){
    std::string plain=ref.kind==ObjectKind::Atom?doc.atomLabel(ref.id):doc.plainText(ref.id);
    auto& bag=doc.extensions(ref);std::vector<TextRun> runs;std::string colors;
    const auto* raw=bag.findValue(key());
    if(raw){
        tinyxml2::XMLDocument xml;if(xml.Parse(raw->c_str())!=tinyxml2::XML_SUCCESS||!xml.RootElement())return Error(ErrorCode::InvalidArgument,"文字整理备份损坏");
        auto root=xml.RootElement();plain=root->Attribute("text")?root->Attribute("text"):plain;colors=root->Attribute("colors")?root->Attribute("colors"):"";
        if(auto encoded=root->Attribute("textHex")){auto decoded=hexDecode(encoded,&plain);if(!decoded.ok())return decoded;}
        for(auto node=root->FirstChildElement("s");node;node=node->NextSiblingElement("s")){TextRun run;run.utf8=node->GetText()?node->GetText():"";if(auto encoded=node->Attribute("textHex")){auto decoded=hexDecode(encoded,&run.utf8);if(!decoded.ok())return decoded;}run.fontName=node->Attribute("font")?node->Attribute("font"):"";run.size=node->DoubleAttribute("size");run.flags=uint16_t(node->UnsignedAttribute("flags"));runs.push_back(run);}
    }else{
        runs=ref.kind==ObjectKind::Atom?studioatom::runs(doc,ref.id):doc.runs(ref.id);
        for(auto& run:runs)run.flags&=~(kSubscript|kSuperscript|kFormula);
    }
    if(runs.empty()){TextRun run;run.utf8=plain;runs.push_back(run);}
    Error e;
    if(ref.kind==ObjectKind::Atom){e=doc.setAtomLabel(ref.id,plain);if(e.ok())studioatom::set(doc,ref.id,runs);bag.set("pst.attr.MoleculeStudioLiteralLabel","1");}
    else e=doc.setTextRuns(ref.id,runs);
    if(!colors.empty())bag.set("studio.text.colors",colors);
    else if(raw){bag.erase("studio.text.colors");bag.erase("studio.text.colors.edited");}
    bag.erase(key());return e;
}
}
