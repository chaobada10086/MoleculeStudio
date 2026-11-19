#pragma once
static void checkCompactReagents(App& app,const std::string& dir){
    auto require=[](bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);};auto& s=app.s;int count=0;
    for(size_t i=0;i<reagentEntries().size();++i){auto source=readReagentTemplate(int(i));bool compact=false;for(const auto& a:source->doc.atoms().records())if(a.isAlive()&&source->doc.extensions(ObjectRef(ObjectKind::Atom,a.id)).findValue("studio.nickname.fragment"))compact=true;if(!compact)continue;
        s.cancel();s.doc=source->doc;s.history.clear();s.selected=s.all();s.input.bindDocument(s.doc.id());s.refresh();
        for(const auto& atom:s.doc.atoms().records())if(atom.isAlive()&&s.doc.atomLabel(atom.id)=="PPh2"){
            auto original=s.doc.digest();s.selected={ObjectRef(ObjectKind::Atom,atom.id)};app.applyMechanismElectronState(0);
            require(mechanism::lonePairCount(s.doc,atom.id)==1,"compact PPh2 preserves its phosphorus lone-pair host");require(s.undo().ok()&&s.doc.digest()==original,"PPh2 lone-pair annotation undoes exactly");s.selected=s.all();break;
        }
        const auto before=s.doc.digest();auto info=studio::analyze(s.doc,s.selected);require(info.valid,"compact formula: "+std::string(reagentEntries()[i].alias));auto atoms=s.doc.stats().aliveAtoms;
        app.expandGroups();require(s.doc.stats().aliveAtoms>atoms,"compact ligands expand into real rings");auto expanded=studio::analyze(s.doc,s.selected);
        if(!expanded.valid||expanded.plain!=info.plain||std::abs(expanded.averageMass-info.averageMass)>=.001)s.save(dir+"/compact-expansion-failure.cdxml",true);
        require(expanded.valid&&expanded.plain==info.plain&&std::abs(expanded.averageMass-info.averageMass)<.001,"compact/expanded formula: "+std::string(reagentEntries()[i].alias)+" "+info.plain+" -> "+expanded.plain+" "+expanded.note);require(s.undo().ok()&&s.doc.digest()==before,"compact reagent expansion is one exact undo");
        if(std::string(reagentEntries()[i].alias)=="Pd(PPh3)4"){
            require(atoms==1&&s.doc.stats().aliveBonds==0&&s.doc.atomLabel(0)=="Pd(PPh3)4","tetrakis palladium uses a single parenthesized formula");
            require(s.save(dir+"/compact-palladium.cdxml",true).ok(),"save compact palladium");Session reopened;require(reopened.open(dir+"/compact-palladium.cdxml").ok(),"reopen compact palladium");auto restored=studio::analyze(reopened.doc,reopened.all());require(restored.valid&&restored.plain==info.plain,"compact palladium retains hidden full chemistry on reopen");
            s.selected.clear();s.fit();app.invalidate();app.screenshot(dir+"/compact-palladium.png");
        }
        if(std::string(reagentEntries()[i].alias)=="BINAP"||std::string(reagentEntries()[i].alias)=="Xantphos"){s.selected.clear();s.fit();app.invalidate();app.screenshot(dir+"/compact-"+reagentEntries()[i].alias+".png");}
        ++count;
    }
    require(count>=26,"remaining reviewed compact drawings are installed; spatial photosensitizers and phosphines use full skeletons");std::ofstream(dir+"/compact-reagent-checks.json")<<"{\"templates\":"<<count<<",\"expandable_chemistry\":true,\"formula_mass\":true,\"undo\":true,\"save_reopen\":true}";
}
