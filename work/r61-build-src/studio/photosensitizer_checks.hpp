#pragma once
#include "expanded_photosensitizer_checks.hpp"
#include "spatial_template_checks.hpp"
static void checkPhotosensitizers(App& app,const std::string& dir){
    auto require=[](bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);};auto& s=app.s;
    auto reset=[&](){s.cancel();s.doc=Document();s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());s.refresh();};
    checkExpandedPhotosensitizers(app,dir);
    checkSpatialTemplates(app,dir);
    for(const char* typed:{"CO2.-","CO2*-","CO2•−","CO₂•⁻"}){
        reset();ObjectId id;s.doc.addText(Vec2(100,100),10,"",&id);s.refresh();app.openText(id);app.textEditor.select(0,-1);auto text=W(typed);SendMessageW(app.edit,EM_REPLACESEL,TRUE,LPARAM(text.c_str()));require(app.finishText(true),"type CO2 radical-anion notation");
        require(s.doc.texts().alive(id)&&compounddata::find(s.doc.plainText(id)),"CO2 radical-anion notation survives editor typography");s.selected={ObjectRef(ObjectKind::Text,id)};app.expandGroups();auto info=studio::analyze(s.doc,s.selected);require(info.valid&&info.plain=="CO2 (1−)","typed radical-anion notation expands correctly");
    }
    for(const char* alias:{"CO2RA","4CzIPN","4DPAIPN","3DPAFIPN","4CzTPN","2CzPN","5CzBN","Mes-Acr-MeBF4","Mes-Acr-MeClO4","RoseBengal-Na2","MethyleneBlue-Cl","N-Phenylphenothiazine","Thioxanthone"}){
        const auto* entry=compounddata::find(alias);require(entry!=nullptr,std::string("new photosensitizer alias is unambiguous: ")+alias);reset();ObjectId caption;s.doc.addText(Vec2(100,100),10,alias,&caption);s.refresh();s.selected={ObjectRef(ObjectKind::Text,caption)};
        const auto before=s.doc.digest();auto condensed=studio::analyze(s.doc,s.selected);require(condensed.valid,"photosensitizer caption formula");app.expandGroups();auto graph=studio::analyze(s.doc,s.selected);require(graph.valid&&graph.plain==condensed.plain&&std::abs(graph.averageMass-condensed.averageMass)<.001,"photosensitizer caption expands with unchanged formula and charge");
        if(std::string(alias)=="CO2RA"){
            require(s.doc.stats().aliveAtoms==3&&s.doc.stats().aliveBonds==2&&graph.plain=="CO2 (1−)","CO2 radical anion has exactly three atoms and net minus one");ObjectId carbon=kInvalidObjectId;int charge=0,radicals=0;
            for(const auto& a:s.doc.atoms().records())if(a.isAlive()){charge+=a.semantics.charge;radicals+=a.semantics.radical==uint8_t(Radical::Doublet);require(a.semantics.implicitHydrogens==0,"CO2 radical anion has no phantom hydrogen");if(a.element==6)carbon=a.id;}
            require(charge==-1&&radicals==1&&carbon!=kInvalidObjectId&&s.doc.atoms().alive(carbon)->semantics.radical==uint8_t(Radical::Doublet),"CO2 has a real carbon radical and a real negative charge");
            auto dots=arrows::mechanismRadicalPoints(s.doc,carbon);require(dots.size()==1,"CO2 radical has a visible capture dot");
            require(s.save(dir+"/carbon-dioxide-radical-anion.cdxml",true).ok(),"save CO2 radical anion");Session reopened;require(reopened.open(dir+"/carbon-dioxide-radical-anion.cdxml").ok(),"reopen CO2 radical anion");auto info=studio::analyze(reopened.doc,reopened.all());require(info.valid&&info.plain==graph.plain,"CO2 charge survives reopen");
            s.selected.clear();s.fit();app.invalidate();app.screenshot(dir+"/carbon-dioxide-radical-anion.png");
        }
        require(s.undo().ok()&&s.doc.digest()==before,"photosensitizer expansion undoes exactly");
    }
    int installed=0;
    for(size_t i=0;i<reagentEntries().size();++i){std::string alias=reagentEntries()[i].alias;if(alias=="4CzIPN"||alias=="4DPAIPN"||alias.find("Mes-Acr-MeBF4")!=std::string::npos){auto source=readReagentTemplate(int(i));reset();s.doc=source->doc;s.refresh();s.fit();app.invalidate();app.screenshot(dir+"/photosensitizer-"+alias.substr(0,alias.find(' '))+".png");++installed;}}
    require(installed==3,"large photocatalysts have installed readable depictions");std::ofstream(dir+"/photosensitizer-checks.json")<<"{\"typed_expansion_cases\":13,\"CO2_doublet_carbon\":true,\"CO2_charge_minus_one\":true,\"no_extra_hydrogen\":true,\"visible_radical\":true,\"compact_catalysts\":true,\"formula_charge\":true,\"undo\":true,\"save_reopen\":true}";
}
