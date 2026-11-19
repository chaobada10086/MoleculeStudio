#pragma once
static void checkCompactDonorLonePairs(App& app,const std::string& dir){
    auto& s=app.s;int cases=0;
    auto require=[](bool ok,const std::string& why){if(!ok)throw std::runtime_error("compact donor: "+why);};
    const std::vector<std::string> labels={"MeOH","EtOH","nPrOH","iPrOH","nBuOH","iBuOH","sBuOH","tBuOH","BnOH","PhOH","PhCH2OH","CF3CH2OH","Et2O","Et₂O","MeNH2","EtNH2","nPrNH2","iPrNH2","nBuNH2","tBuNH2","PhNH2","BnNH2","Me2NH","HNMe2","Et2NH","HNEt2","Bn2NH","Me3N","NMe3","Et3N","NEt3","NEt₃","Bu3N","NBu3","MeSH","EtSH","PhSH","Me2S","Et2S"};
    for(const auto& label:labels)for(int form=0;form<3;++form){
        app.cancelElectronMarkerDrag();app.selectedElectronMarker={};s.cancel();s.doc=Document();s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());app.expandPending=false;
        ObjectId id;const auto* group=mechanism::compactLonePairGroup(label);require(group!=nullptr,"known donor "+label);
        if(form==2){require(s.doc.addText(Vec2(120,120),10,label,&id).ok(),"create legacy caption");s.selected={{ObjectKind::Text,id}};}
        else{require(s.doc.addAtom(Vec2(120,120),6,&id).ok(),"create atom");if(form==0)require(nicknames::apply(s.doc,id,label).ok(),"normal abbreviation "+label);else{auto sem=s.doc.atoms().alive(id)->semantics;sem.nodeType=uint8_t(NodeType::Query);s.doc.setAtomSemantics(id,sem);s.doc.setAtomLabel(id,label);}s.selected={{ObjectKind::Atom,id}};}
        s.refresh();const auto before=s.doc.digest();auto originalFormula=studio::analyze(s.doc,s.selected);
        app.execute(Button("","","","@mechanismLonePair"));require(s.selected.size()==1&&s.selected[0].kind==ObjectKind::Atom,"caption becomes editable atom "+label);id=s.selected[0].id;
        const auto* atom=s.doc.atoms().alive(id);require(atom->element==group->element&&atom->semantics.implicitHydrogens==group->hydrogens&&studioions::hiddenValence(s.doc,id)==group->hidden,"actual donor valence "+label);
        require(s.doc.atomLabel(id)==label&&mechanism::lonePairCount(s.doc,id)==1,"spelling and lone pair "+label);
        const auto formula=studio::analyze(s.doc,s.selected);require(formula.valid,"molecular formula available "+label);if(originalFormula.valid)require(formula.plain==originalFormula.plain&&std::abs(formula.averageMass-originalFormula.averageMass)<1e-8,"composition unchanged "+label);
        integration::RingGeometry rings(s.doc);auto drawn=integration::molecular::labels(s.doc,s.glyphs,rings);const auto& layout=drawn.at(id).layout;
        const auto center=mechanism::compactLonePairCenter(label,layout,atom->position);const auto visual=arrows::mechanismVisualBoxes(s.doc).at(id);
        require(visual.hasBody&&visual.lonePairs.size()==1&&length(visual.lonePairHost-center)<1e-6&&length((visual.body.min+visual.body.max)*.5-center)<1e-6,"annotation and capture target donor glyph "+label);
        const auto dots=mechanism::lonePairDots(s.doc,*atom,0,10,&layout);
        for(const auto& glyph:visual.labelGlyphs)for(auto dot:dots)require(arrows::mechanismBoxDistance(glyph,dot)>.8,"dots avoid every letter, not just H "+label);
        const auto marked=s.doc.digest();require(s.undo().ok()&&s.doc.digest()==before&&s.undo(true).ok()&&s.doc.digest()==marked,"one undo redo "+label);
        for(int pair=1;pair<(group->element==7?1:2);++pair)app.execute(Button("","","","@mechanismLonePair"));const auto full=s.doc.digest();app.execute(Button("","","","@mechanismLonePair"));require(s.doc.digest()==full,"capacity is enforced "+label);
        if(form==0){for(bool xml:{false,true}){auto path=dir+"/donor-"+std::to_string(cases)+(xml?".cdxml":".r62doc");require(s.save(path,xml).ok(),"save "+label);Session reopened;require(reopened.open(path).ok(),"reopen "+label);bool found=false;for(const auto& a:reopened.doc.atoms().records())if(a.isAlive()&&reopened.doc.atomLabel(a.id)==label){found=true;require(mechanism::lonePairCount(reopened.doc,a.id)==(group->element==7?1:2),"saved donor pairs "+label);auto f=studio::analyze(reopened.doc,{{ObjectKind::Atom,a.id}});require(f.valid&&f.plain==formula.plain,"saved donor composition "+label);}require(found,"compact label retained "+label);}}
        if(label=="EtOH"&&form==0){
            auto v=s.view();v.center=center;v.scale=6;v.dpiScale=1;s.setView(v);auto pair=arrows::mechanismVisualBoxes(s.doc).at(id).lonePairs[0];auto start=(pair.min+pair.max)*.5;
            require(app.beginElectronMarkerDrag(v.toDevice(start)),"drag EtOH pair");app.updateElectronMarkerDrag(v.toDevice(start+Vec2(2,-1)),true);auto moved=arrows::mechanismVisualBoxes(s.doc).at(id).lonePairs[0];auto finish=(moved.min+moved.max)*.5;require(length(finish-(start+Vec2(2,-1)))<1e-5,"drag starts without jump from O");
            app.chooseArrowPreset(49);s.selected.clear();auto* tool=dynamic_cast<arrows::ArrowTool*>(s.entries[s.activeIndex].tool.get());require(tool&&s.gesture(PointerPhase::Down,finish).ok()&&tool->previewParams().electronSource==ObjectRef(ObjectKind::Atom,id)&&tool->previewParams().sourceVisualAnchor==3,"arrow starts on EtOH oxygen pair");s.cancel();s.selected.clear();app.selectedElectronMarker={};s.fit();app.invalidate();app.screenshot(dir+"/ethanol-lone-pairs.png");
            s.selected={{ObjectKind::Atom,id}};app.expandGroups();require(s.doc.stats().aliveAtoms==3&&s.doc.atoms().alive(id)->element==8&&mechanism::lonePairCount(s.doc,id)==2&&studioions::hiddenValence(s.doc,id)==0,"expanded ethanol keeps pairs on real O without double-counted hidden valence");
            auto expandedFormula=studio::analyze(s.doc,s.all());require(expandedFormula.valid&&expandedFormula.plain==formula.plain,"expanded marked ethanol composition unchanged");
        }
        ++cases;
    }
    for(const char* label:{"EtOH solution","NMe4+","EtOH/MeOH","UnknownOH"})require(!mechanism::compactLonePairGroup(label),"do not guess unknown/charged/multiple donor labels");
    std::ofstream(dir+"/compact-donor-lone-pair-checks.json")<<"{\"cases\":"<<cases<<",\"atom_caption_legacy\":true,\"oxygen_nitrogen_sulfur\":true,\"glyph_anchor\":true,\"capacity\":true,\"formula_mass\":true,\"drag\":true,\"arrow_capture\":true,\"undo_redo\":true,\"native_cdxml\":true}";
}
