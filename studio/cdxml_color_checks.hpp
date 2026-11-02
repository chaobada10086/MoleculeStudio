#pragma once
// Regression for the reported "black page in ChemDraw" bug.
//
// ChemDraw paints the page with colortable entry 0 and resolves an object's
// color/FillColor through the same table with a two-slot offset (0 = default ink,
// 1 = paper, 2+n = entry n). A saved file with no table, or with a table whose
// first entry is black, therefore opens as a black page with black ink - both
// shapes were reproduced with ChemDraw 26 on files this tool wrote. These checks
// save the four shapes the writer has to get right and assert the invariant on
// the bytes that actually reached disk, then reopen each file through Session.
void App::cdxmlColorCheck(const std::string& dir){
    auto require=[](bool ok,const std::string& what){if(!ok)throw std::runtime_error(what.c_str());};
    auto inspect=[&](const std::string& path,const std::string& label)->std::string{
        std::string bytes;require(persistence::readFileBytes(path,&bytes).ok(),label+": the saved file is readable");
        tinyxml2::XMLDocument xml;require(xml.Parse(bytes.c_str())==tinyxml2::XML_SUCCESS,label+": the saved file parses as XML");
        auto root=xml.RootElement();require(root!=nullptr,label+": the saved file has a root element");
        int tables=0,entries=0,first[3]={-1,-1,-1},second[3]={-1,-1,-1};
        for(auto t=root->FirstChildElement("colortable");t;t=t->NextSiblingElement("colortable")){
            ++tables;if(tables>1)continue;
            int index=0;
            for(auto c=t->FirstChildElement("color");c;c=c->NextSiblingElement("color"),++index,++entries){
                const char* keys[3]={"r","g","b"};int rgb[3];
                for(int k=0;k<3;++k){double v=c->DoubleAttribute(keys[k],-1.0);require(v>=0.0&&v<=1.0,label+": every colour channel is a 0..1 float");rgb[k]=int(std::floor(v*255.0+.5));}
                if(index==0)for(int k=0;k<3;++k)first[k]=rgb[k];
                if(index==1)for(int k=0;k<3;++k)second[k]=rgb[k];
            }
        }
        require(tables==1,label+": exactly one <colortable>, got "+std::to_string(tables));
        require(entries>=8,label+": at least eight colours, got "+std::to_string(entries));
        require(first[0]==255&&first[1]==255&&first[2]==255,label+": entry 0 is the white page");
        require(second[0]==0&&second[1]==0&&second[2]==0,label+": entry 1 is black ink");
        size_t outOfRange=0;
        std::function<void(tinyxml2::XMLElement*)> scan=[&](tinyxml2::XMLElement* e){
            for(const tinyxml2::XMLAttribute* a=e->FirstAttribute();a;a=a->Next()){
                const char* name=a->Name();if(!name)continue;
                if(std::strcmp(name,"color")&&std::strcmp(name,"Color")&&std::strcmp(name,"FillColor")&&std::strcmp(name,"bgcolor"))continue;
                char* end=0;long slot=std::strtol(a->Value()?a->Value():"",&end,10);if(!end||*end||slot<0)continue;
                if(slot>=2&&size_t(slot-2)>=size_t(entries))++outOfRange;
            }
            for(auto c=e->FirstChildElement();c;c=c->NextSiblingElement())scan(c);
        };scan(root);
        require(outOfRange==0,label+": every color/FillColor slot addresses an existing entry");
        return bytes;
    };
    auto tableHasColor=[&](const std::string& bytes,int wantR,int wantG,int wantB){
        tinyxml2::XMLDocument xml;if(xml.Parse(bytes.c_str())!=tinyxml2::XML_SUCCESS)return false;
        auto table=xml.RootElement()?xml.RootElement()->FirstChildElement("colortable"):nullptr;if(!table)return false;
        for(auto c=table->FirstChildElement("color");c;c=c->NextSiblingElement("color")){
            int rgb[3];const char* keys[3]={"r","g","b"};
            for(int k=0;k<3;++k){double v=c->DoubleAttribute(keys[k],-1.0);rgb[k]=int(std::floor(v*255.0+.5));}
            if(rgb[0]==wantR&&rgb[1]==wantG&&rgb[2]==wantB)return true;
        }
        return false;
    };
    const uint32_t blue=0x0046DCFFu;

    // (a) A newly drawn document: nothing forced a table before this regression.
    s.dirty=false;execute(Button("","新建","","@new"));
    std::vector<ObjectId> ring;
    for(int i=0;i<6;++i){ObjectId id;double angle=6.2831853*i/6;require(s.doc.addAtom(Vec2(45*cos(angle),45*sin(angle)),6,&id).ok(),"fresh atom");ring.push_back(id);}
    for(int i=0;i<6;++i){ObjectId bond;require(s.doc.addBond(ring[i],ring[(i+1)%6],&bond).ok(),"fresh bond");if(i%2==0)s.doc.setBondOrder(bond,BondOrder::Double);}
    s.refresh();
    require(s.save(dir+"/cdxml-color-new.cdxml",true).ok(),"save a freshly drawn document");
    inspect(dir+"/cdxml-color-new.cdxml","fresh document");
    {Session fresh;require(fresh.open(dir+"/cdxml-color-new.cdxml").ok(),"reopen the freshly drawn document");
     require(fresh.doc.stats().aliveAtoms==6&&fresh.doc.stats().aliveBonds==6,"fresh document keeps six atoms and six bonds");}

    // (b) A document written by ChemDraw 26, re-saved without edits. Its own table
    // must be kept verbatim (not duplicated) and color="4" must stay red.
    const char* chemdraw=R"CDXML(<?xml version="1.0" encoding="UTF-8" ?>
<CDXML CreationProgram="ChemDraw 26.0.0.6141" Name="chemdraw-probe.cdxml" BoundingBox="0 0 300 160" LabelFont="3" LabelSize="10" CaptionFont="3" CaptionSize="10" color="0" bgcolor="1">
<fonttable><font id="3" charset="iso-8859-1" name="Arial"/></fonttable>
<colortable>
<color r="1" g="1" b="1"/>
<color r="0" g="0" b="0"/>
<color r="1" g="0" b="0"/>
<color r="1" g="1" b="0"/>
<color r="0" g="1" b="0"/>
<color r="0" g="1" b="1"/>
<color r="0" g="0" b="1"/>
<color r="1" g="0" b="1"/>
</colortable>
<page id="20" BoundingBox="0 0 300 160" WidthPages="1" HeightPages="1">
<fragment id="21">
<n id="22" p="60 60" Element="8" NumHydrogens="0"><t p="60 60" BoundingBox="54 54 66 66"><s font="3" size="10" color="0" face="96">O</s></t></n>
<n id="23" p="120 60"/>
<b id="24" B="22" E="23"/>
</fragment>
<t id="25" p="40 110" BoundingBox="40 100 60 120"><s font="3" size="10" color="4">R</s></t>
</page>
</CDXML>)CDXML";
    std::string chemdrawPath=dir+"/cdxml-color-chemdraw.cdxml";
    {std::ofstream out(chemdrawPath.c_str(),std::ios::binary);out.write(chemdraw,std::streamsize(std::string(chemdraw).size()));}
    Session chem;require(chem.open(chemdrawPath).ok(),"open a ChemDraw document");
    uint32_t red=0;for(const auto& text:chem.doc.texts().records())if(text.isAlive()&&fullText(chem.doc,text.id)=="R")red=text.format.colorRGBA;
    require(red==0xFF0000FFu,"ChemDraw color=\"4\" still resolves to red (table entry 2)");
    require(chem.save(dir+"/cdxml-color-chemdraw-resaved.cdxml",true).ok(),"re-save an unedited ChemDraw document");
    std::string chemdrawSaved=inspect(dir+"/cdxml-color-chemdraw-resaved.cdxml","ChemDraw re-save");
    require(chemdrawSaved.find("<color r=\"1\" g=\"0\" b=\"0\"/>")!=std::string::npos,"ChemDraw's own table is preserved, not replaced");
    {Session chemBack;require(chemBack.open(dir+"/cdxml-color-chemdraw-resaved.cdxml").ok(),"reopen the re-saved ChemDraw document");
     require(chemBack.doc.stats().aliveAtoms==chem.doc.stats().aliveAtoms&&chemBack.doc.stats().aliveBonds==chem.doc.stats().aliveBonds,"ChemDraw document keeps its atom and bond counts");
     uint32_t redBack=0;for(const auto& text:chemBack.doc.texts().records())if(text.isAlive()&&fullText(chemBack.doc,text.id)=="R")redBack=text.format.colorRGBA;
     require(redBack==0xFF0000FFu,"the re-saved ChemDraw colour is still red");}

    // (c) Explicit colour overrides on a shape and on an atom: the appended colour
    // has to land in the table and survive the round trip.
    s.dirty=false;execute(Button("","新建","","@new"));
    ObjectId left,right,bond;require(s.doc.addAtom(Vec2(0,0),6,&left).ok()&&s.doc.addAtom(Vec2(40,0),6,&right).ok(),"colour fixture atoms");
    require(s.doc.addBond(left,right,&bond).ok(),"colour fixture bond");
    core::PathRecord curve;curve.points={Vec2(0,60),Vec2(70,90)};ObjectId pathId;require(s.doc.addPath(curve,&pathId).ok(),"colour fixture path");
    s.refresh();s.selected={ObjectRef(ObjectKind::Path,pathId),ObjectRef(ObjectKind::Atom,left)};
    require(applyGraphicsStyle(1.5,blue,true,true).ok(),"recolour a path and an atom");
    require(s.save(dir+"/cdxml-color-override.cdxml",true).ok(),"save a document with explicit colours");
    std::string overridden=inspect(dir+"/cdxml-color-override.cdxml","colour override");
    require(tableHasColor(overridden,0,70,220),"the explicit blue is stored in the colour table");
    {Session overrideBack;require(overrideBack.open(dir+"/cdxml-color-override.cdxml").ok(),"reopen the recoloured document");
     bool pathBlue=false;for(const auto& p:overrideBack.doc.paths().records())if(p.isAlive()&&p.style.colorRGBA==blue)pathBlue=true;
     require(pathBlue,"the recoloured path keeps its colour through save and reload");
     // The reader records a source colour as studio.color; the file must carry the
     // slot that resolves back to the same blue.
     bool atomBlue=false;for(const auto& a:overrideBack.doc.atoms().records())if(a.isAlive()){auto v=static_cast<const Document&>(overrideBack.doc).extensions(ObjectRef(ObjectKind::Atom,a.id)).findValue("studio.color");if(v&&*v==std::to_string(blue))atomBlue=true;}
     require(atomBlue,"the recoloured atom keeps its colour through save and reload");}

    // (d) A document whose table is a single black entry (紫杉醇.cdxml as reported,
    // or the embedded equivalent on a machine without it): the page must become
    // white and the black ink must stay black, not turn into entry-0 white.
    std::wstring desktop;
    {wchar_t profile[MAX_PATH]={};DWORD n=GetEnvironmentVariableW(L"USERPROFILE",profile,MAX_PATH);if(n>0&&n<MAX_PATH)desktop=std::wstring(profile)+L"\\Desktop\\";}
    std::string brokenPath=U(desktop)+(u8"\u7d2b\u6749\u9187.cdxml");
    bool fromDesktop=!desktop.empty()&&GetFileAttributesW(W(brokenPath).c_str())!=INVALID_FILE_ATTRIBUTES;
    if(!fromDesktop){
        brokenPath=dir+"/cdxml-color-broken-table.cdxml";
        const char* broken=R"CDXML(<?xml version="1.0" encoding="UTF-8"?>
<CDXML BondLength="16.399999999999999" LineWidth="1.1899999999999999" CaptionSize="10" CaptionFont="3" LabelFont="3" CreationProgram="Molecule Studio" BoundingBox="0 0 240 160" LabelSize="10">
    <colortable>
        <color r="0" g="0" b="0"/>
    </colortable>
    <fonttable>
        <font id="3" charset="iso-8859-1" name="Times New Roman"/>
    </fonttable>
    <page id="20" BoundingBox="0 0 240 160" WidthPages="1" HeightPages="1">
        <fragment id="21">
            <n id="3" p="60 60" color="2"/>
            <n id="4" p="100 60" Element="8" NumHydrogens="0" color="2"><t color="2"><s font="3" size="10" face="96" color="2">O</s></t></n>
            <b id="5" B="3" E="4" color="2"/>
        </fragment>
    </page>
</CDXML>)CDXML";
        std::ofstream out(brokenPath.c_str(),std::ios::binary);out.write(broken,std::streamsize(std::string(broken).size()));
    }
    require(s.open(brokenPath).ok(),"open a document whose colour table is a single black entry");
    size_t brokenAtoms=s.doc.stats().aliveAtoms,brokenBonds=s.doc.stats().aliveBonds;
    require(brokenAtoms>0,"the single-entry-table document has structure");
    require(s.save(dir+"/cdxml-color-repaired.cdxml",true).ok(),"re-save the single-entry-table document");
    inspect(dir+"/cdxml-color-repaired.cdxml","repaired document");
    {Session repaired;require(repaired.open(dir+"/cdxml-color-repaired.cdxml").ok(),"reopen the repaired document");
     require(repaired.doc.stats().aliveAtoms==brokenAtoms&&repaired.doc.stats().aliveBonds==brokenBonds,"the repaired document keeps every atom and bond");
     // Entry 0 was black ink in the source; after the rebuild every label must still
     // be dark. White is the failure that made the reported file look empty.
     size_t labels=0;bool inkDark=true;for(const auto& text:repaired.doc.texts().records())if(text.isAlive()){++labels;const uint32_t c=text.format.colorRGBA;if(((c>>24)&255)>200&&((c>>16)&255)>200&&((c>>8)&255)>200)inkDark=false;}
     require(labels>0&&inkDark,"the ink that was entry-0 black is still dark after the table is rebuilt");}

    s.selected=s.all();s.refresh();s.fit();switchTab(1);layout();screenshot(dir+"/cdxml-colors.png");
    std::ofstream(dir+"/cdxml-color-checks.json")<<"{\"one_colortable\":true,\"eight_entries\":true,\"white_page\":true,\"black_ink\":true,\"slots_in_range\":true,\"fresh_document\":true,\"chemdraw_resave\":true,\"colour_override\":true,\"single_entry_repaired\":true,\"repair_source\":\""<<(fromDesktop?"desktop":"fixture")<<"\"}";
    s.dirty=false;
}
