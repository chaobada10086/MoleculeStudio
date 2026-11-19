#pragma once
static void checkCurrentAlkoxides(App& app,const std::string& dir){
    auto require=[](bool ok,const std::string& why){if(!ok)throw std::runtime_error("alkoxide library: "+why);};auto& s=app.s;int whole=0,attached=0;
    for(const char* input:{"Al(OiPr)3","Al(Oi-Pr)3","异丙醇铝","Al(OMe)3","Al(OEt)3","Al(OtBu)3","Ti(OiPr)4","Ti(OEt)4","Ti(OtBu)4","Zr(OiPr)4","Zr(OtBu)4","B(OiPr)3"}){
        auto name=chemcompletion::knownSpelling(input);auto entry=compounddata::find(input);require(entry&&!name.empty(),std::string("recognized full alkoxide: ")+input);
        s.cancel();s.doc=Document();s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());ObjectId atom;s.doc.addAtom(Vec2(),6,&atom);s.refresh();require(s.submit(nicknames::labelCommand(atom,name),"原子快捷键").ok(),"type full alkoxide");auto before=s.doc.digest();auto plan=functional_groups::prepareExpansion(s.doc,atom);
        require(s.submit(Command("展开醇盐",kInvalidateStructure|kInvalidateGeometry,[plan](CommandContext& c){std::vector<ObjectRef> made;functional_groups::applyExpansion(c.document,plan,made);return CommandResult::success();}),"官能团展开").ok(),"expand full alkoxide");
        auto formula=studio::analyze(s.doc,s.all());require(formula.valid&&formula.plain==entry->formula,std::string("full formula: ")+input+" "+formula.plain+" vs "+entry->formula);
        require(s.doc.atoms().alive(atom)->element!=6&&s.doc.incidentBonds(atom).size()>=3,std::string("metal anchor and actual O-metal bonds: ")+input);
        if(std::string(input)=="Al(Oi-Pr)3"){s.selected.clear();s.fit();s.save(dir+"/aluminium-isopropoxide.cdxml",true);app.screenshot(dir+"/aluminium-isopropoxide.png",false,false);}
        auto after=s.doc.digest();require(s.undo().ok()&&s.doc.digest()==before&&s.undo(true).ok()&&s.doc.digest()==after,"whole alkoxide undo/redo");++whole;
    }
    for(const char* input:{"Al(OiPr)2","Al(Oi-Pr)2","Al(OEt)2","Al(OMe)2","Al(OtBu)2","Ti(OiPr)3","Ti(OEt)3","Ti(OtBu)3","Zr(OiPr)3","Zr(OtBu)3","B(OiPr)2"}){
        auto name=chemcompletion::knownSpelling(input);require(!name.empty()&&nickdata::find(name),std::string("recognized attachment group: ")+input);
        s.cancel();s.doc=Document();s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());ObjectId carbon,oxygen,metal,bond;s.doc.addAtom(Vec2(-28,0),6,&carbon);s.doc.addAtom(Vec2(-14,0),8,&oxygen);s.doc.addAtom(Vec2(),6,&metal);s.doc.addBond(carbon,oxygen,nullptr);s.doc.addBond(oxygen,metal,&bond);s.refresh();require(s.submit(nicknames::labelCommand(metal,name),"原子快捷键").ok(),"type attached alkoxide");auto before=s.doc.digest();auto plan=functional_groups::prepareExpansion(s.doc,metal);
        require(s.submit(Command("展开醇盐配体",kInvalidateStructure|kInvalidateGeometry,[plan](CommandContext& c){std::vector<ObjectRef> made;functional_groups::applyExpansion(c.document,plan,made);return CommandResult::success();}),"官能团展开").ok(),"expand attached alkoxide");
        int expected=name.find("Al(")==0?13:name.find("Ti(")==0?22:name.find("Zr(")==0?40:5;require(s.doc.atoms().alive(metal)->element==expected&&s.doc.bondBetween(oxygen,metal)==bond,"existing O-metal bond retained");
        require(s.doc.incidentBonds(metal).size()==size_t(expected==13||expected==5?3:4),"all attached ligands present");auto after=s.doc.digest();require(s.undo().ok()&&s.doc.digest()==before&&s.undo(true).ok()&&s.doc.digest()==after,"attachment undo/redo");
        if(std::string(input)=="Al(Oi-Pr)2"){s.selected.clear();s.fit();s.save(dir+"/attached-aluminium-alkoxide.cdxml",true);app.screenshot(dir+"/attached-aluminium-alkoxide.png",false,false);}++attached;
    }
    const std::string spelling="Al(Oi-Pr)2";auto mask=chemitalic::maskUtf8(spelling);for(size_t i=0;i<spelling.size();++i)require(bool(mask[i])==(spelling[i]=='i'),"only i italic; hyphen and Pr upright");
    // Actual editor input must normalize the spelling and keep the style runs.
    s.cancel();s.doc=Document();s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());ObjectId atom;s.doc.addAtom(Vec2(),6,&atom);s.refresh();app.openText(kInvalidObjectId,atom);SetWindowTextW(app.edit,L"Al(OiPr)2");require(app.finishText(true),"finish actual atom editor");require(s.doc.atomLabel(atom)==spelling,"editor inserts descriptor hyphen");bool italic=false;for(auto run:studioatom::runs(s.doc,atom))if(run.utf8.find('i')!=std::string::npos)italic|=(run.flags&kItalic)!=0;require(italic,"editor styles i italic");
    std::ofstream(dir+"/alkoxide-library-checks.json")<<"{\"whole_alias_cases\":"<<whole<<",\"attachment_cases\":"<<attached<<",\"metal_oxygen_bonds\":true,\"formula\":true,\"undo_redo\":true,\"editor_hyphen\":true,\"italic_i_upright_Pr\":true}";
}
