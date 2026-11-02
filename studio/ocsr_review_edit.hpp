#pragma once
// Batch changes share the editor's transaction log: one click, one undo.
namespace ocsr_edit {
inline r62::core::Command flipConfiguration(){
    using namespace r62::core;
    return Command("整体构型翻转",kInvalidateStructure|kInvalidateAppearance|kInvalidateSemantics,
        [](CommandContext& c){
            struct Change{ObjectId id;BondDisplay display;BondStereo stereo;};std::vector<Change> changes;
            for(const auto& b:c.document.bonds().records())if(b.isAlive()&&b.order==BondOrder::Single&&b.semantics==BondSemantics::Normal){
                if(b.display==BondDisplay::Wedge)changes.push_back({b.id,BondDisplay::WedgeHash,BondStereo::Down});
                else if(b.display==BondDisplay::WedgeHash)changes.push_back({b.id,BondDisplay::Wedge,BondStereo::Up});
                else if(b.display==BondDisplay::Bold)changes.push_back({b.id,BondDisplay::Hash,BondStereo::None});
                else if(b.display==BondDisplay::Hash)changes.push_back({b.id,BondDisplay::Bold,BondStereo::None});
            }
            if(changes.empty())return CommandResult::noChange();
            for(const auto& change:changes){auto e=c.document.setBondDisplay(change.id,change.display);
                if(e.ok())e=c.document.setBondStereo(change.id,change.stereo);
                if(!e.ok())return CommandResult::fail(e.code,e.message);}
            return CommandResult::success(kInvalidateAppearance|kInvalidateSemantics);
        });
}
// Clipboard/page export moves coordinates into a printable page. The review
// document instead keeps the canvas coordinates and writes current defaults
// over the original CDXML prologue, which may still contain model defaults.
inline r62::core::Error exportReview(const r62::core::Document& d,std::string* result){
    using namespace r62;
    std::ostringstream bytes;auto e=persistence::writeCdxml(d,bytes);if(!e.ok())return e;
    tinyxml2::XMLDocument xml;
    if(xml.Parse(bytes.str().c_str())!=tinyxml2::XML_SUCCESS||!xml.RootElement())
        return core::Error(core::ErrorCode::ParseError,"invalid review CDXML");
    auto root=xml.RootElement();root->SetAttribute("MoleculeStudioOcsrReviewed","1");
    root->SetAttribute("BondLength",d.defaultBondLength());root->SetAttribute("LineWidth",d.defaultLineWidth());
    root->SetAttribute("CaptionSize",d.defaultTextSize());root->SetAttribute("CreationProgram","Molecule Studio");
    double label=10;if(auto value=d.documentExtensions().findValue("studio.style.LabelSize"))label=std::atof(value->c_str());
    root->SetAttribute("LabelSize",label>0?label:10);
    for(const char* key:{"BondSpacing","MarginWidth","LabelFace","BoldWidth","HashSpacing"}){
        if(auto value=d.documentExtensions().findValue(std::string("studio.style.")+key))root->SetAttribute(key,value->c_str());
        else root->DeleteAttribute(key);
    }
    auto table=root->FirstChildElement("fonttable");if(!table){table=xml.NewElement("fonttable");root->InsertFirstChild(table);}
    int font=0,next=3;
    for(auto item=table->FirstChildElement("font");item;item=item->NextSiblingElement("font")){
        next=std::max(next,item->IntAttribute("id",0)+1);
        if(item->Attribute("name")&&d.defaultFont()==item->Attribute("name"))font=item->IntAttribute("id");
    }
    if(!font){font=next;auto item=xml.NewElement("font");item->SetAttribute("id",font);item->SetAttribute("name",d.defaultFont().c_str());item->SetAttribute("charset","iso-8859-1");table->InsertEndChild(item);}
    root->SetAttribute("LabelFont",font);root->SetAttribute("CaptionFont",font);
    tinyxml2::XMLPrinter printer;xml.Print(&printer);*result=printer.CStr();return core::Error();
}
inline r62::core::Command swapBondTypes(bool hashed){
    using namespace r62::core;
    return Command(hashed?"虚楔与粗虚线互换":"实楔与粗实线互换",kInvalidateStructure|kInvalidateAppearance|kInvalidateSemantics,
        [hashed](CommandContext& c){
            struct Change {ObjectId id;BondDisplay display;BondStereo stereo;};
            std::vector<Change> changes;
            for(const auto& b:c.document.bonds().records())if(b.isAlive()&&b.order==BondOrder::Single&&b.semantics==BondSemantics::Normal){
                if(!hashed&&b.display==BondDisplay::Wedge)changes.push_back({b.id,BondDisplay::Bold,BondStereo::None});
                else if(!hashed&&b.display==BondDisplay::Bold)changes.push_back({b.id,BondDisplay::Wedge,BondStereo::Up});
                else if(hashed&&b.display==BondDisplay::WedgeHash)changes.push_back({b.id,BondDisplay::Hash,BondStereo::None});
                else if(hashed&&b.display==BondDisplay::Hash)changes.push_back({b.id,BondDisplay::WedgeHash,BondStereo::Down});
            }
            if(changes.empty())return CommandResult::noChange();
            for(const auto& change:changes){auto e=c.document.setBondDisplay(change.id,change.display);
                if(e.ok())e=c.document.setBondStereo(change.id,change.stereo);
                if(!e.ok())return CommandResult::fail(e.code,e.message);}
            return CommandResult::success(kInvalidateAppearance|kInvalidateSemantics);
        });
}
}
