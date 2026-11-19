#pragma once
static void checkPolyatomicIons(App& app,const std::string& dir){
    auto& s=app.s;int cases=0;auto require=[](bool ok,const std::string& why){if(!ok)throw std::runtime_error("polyatomic ion: "+why);};
    auto reset=[&](){s.cancel();s.doc=Document();s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());app.expandPending=false;app.selectedElectronMarker={};};
    const std::vector<std::string> names={"N3","N3-","N₃⁻","CN-","SCN-","OCN-","NO2-","NO3-","CO32-","CO3-2","HCO3-","SO42-","SO32-","HSO4-","HSO3-","H2PO4-","HPO42-","PO43-","ClO-","ClO2-","ClO3-","ClO4-"};
    for(const auto& input:names)for(bool caption:{false,true}){
        const auto label=studioions::ascii(input);const auto* ion=compounddata::ion(label);require(ion,"library entry "+label);reset();ObjectId id;
        if(caption){require(s.doc.addText(Vec2(120,100),10,label,&id).ok(),"caption fixture");s.selected={{ObjectKind::Text,id}};}
        else{require(s.doc.addAtom(Vec2(120,100),6,&id).ok()&&nicknames::apply(s.doc,id,label).ok(),"editable compact ion "+label);s.selected={{ObjectKind::Atom,id}};require(s.doc.atoms().alive(id)->semantics.charge==compounddata::ionCharge(ion),"visible compact charge "+label);}
        const auto formula=studio::analyze(s.doc,s.selected);require(formula.valid&&formula.averageMass>20&&formula.plain.find("−")!=std::string::npos,"formula, mass and net charge "+label);
        auto plan=caption?functional_groups::prepareCompound(s.doc,ion->fragment,Vec2(120,100),kInvalidObjectId,id):functional_groups::prepareExpansion(s.doc,id);
        Document expanded=s.doc;std::vector<ObjectRef> refs;functional_groups::applyExpansion(expanded,plan,refs);int charge=0;for(const auto& a:expanded.atoms().records())if(a.isAlive()){charge+=a.semantics.charge;require(a.semantics.nodeType==uint8_t(NodeType::Element),"real expanded atoms");}
        require(charge==compounddata::ionCharge(ion),"expanded charge retained "+label);auto after=studio::analyze(expanded,refs);require(after.valid&&after.plain==formula.plain&&std::abs(after.averageMass-formula.averageMass)<1e-7,"expansion preserves formula and mass "+label);
        if(label=="N3-"||label=="N3"){
            require(expanded.stats().aliveAtoms==3&&expanded.stats().aliveBonds==2,"azide has three N atoms and two bonds");int negative=0,positive=0;for(const auto& a:expanded.atoms().records())if(a.isAlive()){require(a.element==7&&a.semantics.implicitHydrogens==0,"azide has no invented hydrogens");negative+=a.semantics.charge==-1;positive+=a.semantics.charge==1;}require(negative==2&&positive==1,"azide is N-=N+=N-");for(const auto& b:expanded.bonds().records())if(b.isAlive())require(b.order==BondOrder::Double,"azide double bonds");
            s.refresh();const auto before=s.doc.digest();app.expandGroups();require(s.doc.stats().aliveAtoms==3,"right-click expansion succeeds: "+app.status);for(const auto& a:s.doc.atoms().records())if(a.isAlive())require(!integration::molecular::circledElectronCharge(s.doc,a.id),"plain azide charges are drawn clear of both double bonds");auto made=s.doc.digest();require(s.undo().ok()&&s.doc.digest()==before&&s.undo(true).ok()&&s.doc.digest()==made,"expansion one undo redo");
            for(bool xml:{false,true}){auto path=dir+"/azide-"+std::to_string(cases)+(xml?".cdxml":".r62doc");require(s.save(path,xml).ok(),"save expanded azide");Session reopened;require(reopened.open(path).ok(),"reopen expanded azide");auto f=studio::analyze(reopened.doc,reopened.all());require(f.valid&&f.plain==formula.plain,"saved azide charge and mass");}
            if(caption&&label=="N3"){s.selected.clear();s.fit();app.invalidate();app.screenshot(dir+"/azide-expanded.png");}
        }
        ++cases;
    }
    for(const char* name:{"N3","CN","SCN"}){
        reset();ObjectId id;s.doc.addAtom(Vec2(120,100),6,&id);auto entry=studioions::find(name);require(entry&&studioions::apply(s.doc,id,*entry,-1).ok(),"legacy compact anion");
        auto formula=studio::analyze(s.doc,{{ObjectKind::Atom,id}});require(formula.valid&&formula.plain.find("−")!=std::string::npos,"legacy compact anion mass");auto plan=functional_groups::prepareExpansion(s.doc,id);std::vector<ObjectRef> refs;functional_groups::applyExpansion(s.doc,plan,refs);auto expanded=studio::analyze(s.doc,refs);require(expanded.valid&&expanded.plain==formula.plain,"legacy compact anion expansion");++cases;
    }
    // The same spelling attached to a parent is a neutral azido substituent.
    reset();ObjectId c,n,b;s.doc.addAtom(Vec2(100,100),6,&c);s.doc.addAtom(Vec2(130,100),6,&n);s.doc.addBond(c,n,&b);require(nicknames::apply(s.doc,n,"N3").ok(),"attached azido");auto plan=functional_groups::prepareExpansion(s.doc,n);std::vector<ObjectRef> refs;functional_groups::applyExpansion(s.doc,plan,refs);int charge=0;for(const auto& a:s.doc.atoms().records())if(a.isAlive())charge+=a.semantics.charge;require(charge==0&&s.doc.bonds().alive(b)&&s.doc.stats().aliveAtoms==4,"attached azido remains neutral with original parent bond");
    reset();s.doc.addAtom(Vec2(100,100),6,&n);require(nicknames::apply(s.doc,n,"N3-").ok(),"azide nucleophile");s.selected={{ObjectKind::Atom,n}};s.refresh();app.execute(Button("","","","@mechanismLonePair"));require(mechanism::lonePairCount(s.doc,n)==1,"azide donor lone pair");s.doc.addAtom(Vec2(150,100),6,&c);auto sem=s.doc.atoms().alive(c)->semantics;sem.charge=1;sem.implicitHydrogens=3;s.doc.setAtomSemantics(c,sem);std::vector<ObjectRef> product;require(mechanism::generateNextStep(s.doc,{{ObjectRef(ObjectKind::Atom,n),ObjectRef(ObjectKind::Atom,c)}},&product).ok(),"compact azide can form a bond");auto methylAzide=studio::analyze(s.doc,product);require(methylAzide.valid&&methylAzide.plain=="CH3N3","bonded azide has neutral molecular formula: "+methylAzide.plain+" / "+methylAzide.note);
    // Cover actual input, including multi-charge spelling and lower-case n3.
    for(const wchar_t* input:{L"n3",L"n3-",L"scn-",L"no3-",L"so42-"}){reset();app.openText(kInvalidObjectId);app.textEditor.select(0,-1);SendMessageW(app.edit,EM_REPLACESEL,TRUE,LPARAM(input));require(app.finishText(true),"type ion");require(s.selected.size()==1&&s.selected[0].kind==ObjectKind::Atom,"typed ion becomes chemical object");const auto* a=s.doc.atoms().alive(s.selected[0].id);require(a->semantics.charge<0&&studio::analyze(s.doc,s.selected).valid,"typed ion has charge and mass");++cases;}
    int catalogCases=0,catalogFailures=0;std::ofstream audit(dir+"/functional-group-expansion-audit.txt");
    for(const auto& entry:nickdata::nicknames){
        try{Document d;ObjectId host,parent;d.addAtom(Vec2(100,100),6,&host);d.addAtom(Vec2(70,100),6,&parent);d.addBond(parent,host,nullptr);require(nicknames::apply(d,host,entry.name).ok(),"catalog nickname");auto plan=functional_groups::prepareExpansion(d,host);std::vector<ObjectRef> made;functional_groups::applyExpansion(d,plan,made);require(d.atoms().alive(parent)&&d.incidentBonds(parent).size()==1&&made.size()>0,"catalog expands and retains attachment");++catalogCases;}
        catch(const std::exception& e){++catalogFailures;audit<<entry.name<<": "<<e.what()<<"\n";}
    }
    std::ofstream(dir+"/functional-group-expansion-audit.json")<<"{\"checked\":"<<nickdata::count<<",\"passed\":"<<catalogCases<<",\"failed\":"<<catalogFailures<<"}";
    std::ofstream(dir+"/polyatomic-ion-checks.json")<<"{\"cases\":"<<cases<<",\"azide_resonance\":true,\"nineteen_ions\":true,\"text_atom_input\":true,\"charge_mass\":true,\"attached_azido\":true,\"right_click_expand\":true,\"undo_redo\":true,\"native_cdxml\":true}";
}
