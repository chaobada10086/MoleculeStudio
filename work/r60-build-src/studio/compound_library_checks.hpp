#pragma once
#include "tin_radical_checks.hpp"
#include "compact_radical_checks.hpp"
#include "diazo_checks.hpp"
#include "photosensitizer_checks.hpp"
#include "terminal_drag_checks.hpp"
static void checkCompoundLibrary(App& app,const std::string& dir){
    auto& s=app.s;int typed=0,graphs=0,roundtrips=0;
    checkDiazo(app,dir);
    checkTinRadicalAddition(app,dir);
    checkCompactRadicals(app,dir);
    checkPhotosensitizers(app,dir);
    checkTerminalDrag(app,dir);
    auto require=[](bool ok,const std::string& why){if(!ok)throw std::runtime_error("compound library: "+why);};
    auto reset=[&](){s.cancel();s.doc=Document();s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());s.activateCommand("chooseSelectionTool_Marquee");s.refresh();};
    auto runs=[&](ObjectRef ref){return ref.kind==ObjectKind::Text?s.doc.runs(ref.id):studioatom::runs(s.doc,ref.id);};
    auto commit=[&](const char* input,bool atom){reset();ObjectId id;ObjectRef ref;
        if(atom){s.doc.addAtom(Vec2(70,70),6,&id);s.refresh();app.openText(kInvalidObjectId,id);ref=ObjectRef(ObjectKind::Atom,id);}
        else{s.doc.addText(Vec2(70,70),10,"",&id);s.refresh();app.openText(id);ref=ObjectRef(ObjectKind::Text,id);}
        app.textEditor.select(0,-1);auto text=W(input);SendMessageW(app.edit,EM_REPLACESEL,TRUE,LPARAM(text.c_str()));require(app.finishText(true),"commit through actual text editor");if(ref.kind==ObjectKind::Text&&!s.doc.texts().alive(ref.id)&&s.selected.size()==1)ref=s.selected.front();return ref;};
    for(bool atom:{false,true})for(const auto& sample:std::vector<std::pair<std::string,std::string>>{
        {"co2","CO2"},{"cs2","CS2"},{"bu3snh","Bu3SnH"},{"Bu3Sn-H","Bu3Sn-H"},{"bu3sn-h","Bu3Sn-H"},
        {"Bu3Sn·","Bu3Sn·"},{"bu3sn•","Bu3Sn·"},{"bu3sn.","Bu3Sn·"},{"bu3sn*","Bu3Sn·"},{"Me3Sn·","Me3Sn·"},{"Et3Si*","Et3Si·"},
        {"hatu","HATU"},{"nBuLi","n-BuLi"},
        {"c100h800","c100h800"},{"C100H800","C100H800"},{"xq123","xq123"}
    }){
        const auto ref=commit(sample.first.c_str(),atom);const auto text=ref.kind==ObjectKind::Atom?s.doc.atomLabel(ref.id):s.doc.plainText(ref.id);
        const auto* semantic=studio::compact_radicals::find(sample.second);
        require(text==(semantic?semantic->base:sample.second),"known spellings complete; unknown input stays exactly authored: "+sample.first);
        const auto parts=runs(ref);bool subscript=false,italic=false;
        for(const auto& part:parts){subscript|=(part.flags&kSubscript)!=0;italic|=(part.flags&kItalic)!=0;}
        if(sample.second=="Bu3Sn-H"||sample.second.find("·")!=std::string::npos){
            require(subscript,"tin/silicon variants have subscript counts");bool dot=false;
            for(const auto& part:parts)if(part.utf8.find("·")!=std::string::npos){dot=true;require((part.flags&kSuperscript)!=0,"radical dot is superscript");}
            require(!dot,"the radical is drawn once as geometry, never as a text dot");if(semantic)require(ref.kind==ObjectKind::Atom&&s.doc.atoms().alive(ref.id)->semantics.radical==uint8_t(Radical::Doublet),"typed radical is a real editable electron state");
            s.selected={ref};app.updateInfo();require(app.info.valid,"typed variant has formula/mass");
            if(!atom&&sample.first=="bu3sn*"){s.fit();app.invalidate();app.screenshot(dir+"/tributyltin-radical-typed.png");}
        }
        if(sample.first=="co2"||sample.first=="cs2"||sample.first=="bu3snh")require(subscript,"known chemical counts are explicit subscripts");
        if(sample.first=="nBuLi")require(italic,"known n- prefix becomes italic");
        if(sample.first=="c100h800"||sample.first=="C100H800"||sample.first=="xq123")require(!subscript&&!italic,"unknown text receives no automatic typography");
        if(sample.first=="cs2"||sample.first=="bu3snh"){
            s.selected={ref};app.updateInfo();require(app.info.valid&&app.info.averageMass>70,"caption and atom labels both report formula and mass");
            app.execute(Button("","","","@undoTextTidy"));
            require((ref.kind==ObjectKind::Atom?s.doc.atomLabel(ref.id):s.doc.plainText(ref.id))==sample.first,"undo-text-tidy restores original spelling on this object");
            for(const auto& part:runs(ref))require(!(part.flags&(kSubscript|kSuperscript)),"undo-text-tidy restores original baseline");
            app.execute(Button("","","","@textTidy"));require((ref.kind==ObjectKind::Atom?s.doc.atomLabel(ref.id):s.doc.plainText(ref.id))==sample.second,"manual tidy can reapply known formatting");
        }
        if(sample.first=="c100h800"){
            s.selected={ref};const auto original=s.doc.digest();app.execute(Button("","","","@textTidy"));
            require((ref.kind==ObjectKind::Atom?s.doc.atomLabel(ref.id):s.doc.plainText(ref.id))=="C100H800","manual tidy explicitly formats an unknown formula");
            bool formatted=false;for(const auto& part:runs(ref))formatted|=(part.flags&kSubscript)!=0;require(formatted,"manual formula counts become subscript");
            const auto tidy=s.doc.digest();require(s.undo().ok()&&s.doc.digest()==original&&s.undo(true).ok()&&s.doc.digest()==tidy,"manual tidy is one undo/redo step");
            app.execute(Button("","","","@undoTextTidy"));require((ref.kind==ObjectKind::Atom?s.doc.atomLabel(ref.id):s.doc.plainText(ref.id))==sample.first,"per-object restore returns unknown text exactly");
        }++typed;
    }
    for(const auto& entry:std::vector<std::pair<std::string,std::string>>{{"tempo","C9H18NO"},{"dmpo","C6H11NO"},{"pbn","C11H15NO"},{"pobn","C10H14N2O2"},{"depmpo","C9H18NO4P"},{"bmpo","C10H17NO3"},{"tempol","C9H18NO2"},{"tempone","C9H16NO2"},{"4-cooh-tempo","C10H18NO3"}}){
        auto ref=commit(entry.first.c_str(),false);auto label=ref.kind==ObjectKind::Text?s.doc.plainText(ref.id):s.doc.atomLabel(ref.id);
        require(compounddata::find(label)!=nullptr,"spin trap lowercase spelling recognized: "+entry.first);
        s.selected={ref};auto formula=studio::analyze(s.doc,s.selected);require(formula.valid&&formula.plain==entry.second,"independent spin-trap formula: "+entry.first+" "+formula.plain);
        app.expandPending=false;app.expandGroups();int radicals=0,charge=0;for(auto atom:s.selected)if(atom.kind==ObjectKind::Atom){auto a=s.doc.atoms().alive(atom.id);charge+=a->semantics.charge;if(a->semantics.radical){++radicals;require(a->element==8&&a->semantics.radical==uint8_t(Radical::Doublet),"nitroxide radical is on O");}}
        const bool nitroxide=entry.first=="tempo"||entry.first=="tempol"||entry.first=="tempone"||entry.first=="4-cooh-tempo";
        require(radicals==(nitroxide?1:0)&&charge==0,"nitroxide / nitrone electron states retained: "+entry.first);
    }
    for(const char* input:{"oh-","meo-"}){
        commit(input,false);require(s.selected.size()==1&&s.selected.front().kind==ObjectKind::Atom,"typed anion remains a chemical ion");
        const auto* oxygen=s.doc.atoms().alive(s.selected.front().id);require(oxygen&&oxygen->element==8&&oxygen->semantics.charge==-1,"known negative ion keeps its minus during spelling completion");
    }
    for(const char* input:{"Bu3SnH","bu3sn-h","Et3SiH","NH","HN","NH2","H2N","SH","CH3","HCOOR","UnknownH","C100H800"}){
        auto ref=commit(input,false);if(ref.kind==ObjectKind::Atom){const auto& b=arrows::mechanismVisualBoxes(s.doc).at(ref.id);require(b.hasAttachedProton,"typed hydride has an H drag target");}
        s.refresh();s.fit();auto before=s.doc.digest();Vec2 press;
        if(ref.kind==ObjectKind::Atom){auto b=arrows::mechanismVisualBoxes(s.doc).at(ref.id);press=(b.attachedProton.min+b.attachedProton.max)*.5;}
        else{const auto& t=*s.doc.texts().alive(ref.id);text::LayoutOptions o;o.defaultSize=t.size;o.defaultFont=s.doc.defaultFont();text::RichLayout layout;require(text::layoutRichText(s.doc.runs(ref.id),Vec2(),o,&s.glyphs,&layout).ok(),"caption H layout");auto offset=t.position-Vec2(0,layout.metrics.firstBaseline);bool first=std::string(input).front()=='H';const text::GlyphPlacement* target=nullptr;for(const auto& g:layout.glyphs)if(g.codepoint=='H'){target=&g;if(first)break;}require(target,"caption contains an actual H glyph");Box ink;bool any=false;for(auto contour:target->contours)for(auto p:contour){p=p+offset;if(any)ink.add(p);else{ink=Box(p,p);any=true;}}press=(ink.min+ink.max)*.5;}
        require(app.beginElectronMarkerDrag(s.view().toDevice(press)),std::string("grab terminal H: ")+input);
        auto end=s.view().toDevice(press+Vec2(0,-100));app.updateElectronMarkerDrag(end,false);require(app.attachedProtonPreview&&s.doc.digest()==before,"H extraction preview is reversible and nonmutating");app.updateElectronMarkerDrag(end,true);
        require(s.selected.size()==1&&s.selected[0].kind==ObjectKind::Atom,"extracted H selected");auto h=s.selected[0].id;require(s.doc.atoms().alive(h)->element==1&&s.doc.incidentBonds(h).size()==1,"one real H with one real single bond");auto edge=s.doc.bonds().alive(s.doc.incidentBonds(h)[0]);auto host=edge->beginAtom==h?edge->endAtom:edge->beginAtom;
        require(length(s.doc.atoms().alive(h)->position-s.doc.atoms().alive(host)->position)<=s.doc.defaultBondLength()*1.5+1e-6,"terminal H bond length is bounded");
        if(std::string(input)=="Bu3SnH"){
            require(s.doc.atomLabel(host)=="Bu3Sn"&&s.doc.atoms().alive(host)->element==50&&studioions::hiddenValence(s.doc,host)==3,"Sn-H materializes while keeping the Bu3Sn residue compact");
            const auto body=arrows::mechanismVisualBoxes(s.doc).at(host).body;require(length((body.min+body.max)*.5-s.doc.atoms().alive(host)->position)<.01,"Sn-H attaches to the Sn glyph rather than the Bu3 text");
            require(s.save(dir+"/sn-h-pull-debug.cdxml",true).ok(),"save Sn-H diagnostic");auto formula=studio::analyze(s.doc,s.selected);require(formula.valid&&formula.plain=="C12H28Sn","Sn-H extraction conserves formula and mass: "+formula.plain+" "+formula.note);
            std::vector<mechanism::ElectronMove> moves={{ObjectRef(ObjectKind::Bond,edge->id),ObjectRef(ObjectKind::Atom,host),1},{ObjectRef(ObjectKind::Bond,edge->id),ObjectRef(ObjectKind::Atom,h),1}};Document trial=s.doc;std::vector<ObjectRef> products;require(mechanism::generateNextStep(trial,moves,&products).ok(),"the pulled Sn-H bond supports homolysis");
            s.refresh();s.fit();app.updateInfo();app.screenshot(dir+"/tributyltin-hydrogen-pulled.png");
        }
        require(s.undo().ok()&&s.doc.digest()==before,"H extraction undoes in one step");
    }
    for(const char* label:{"","NH","HN"}){
        reset();ObjectId n,a,b,edge;s.doc.addAtom(Vec2(70,70),7,&n);s.doc.addAtom(Vec2(45,55),6,&a);s.doc.addAtom(Vec2(45,85),6,&b);s.doc.addBond(n,a,&edge);s.doc.addBond(n,b,&edge);auto sem=s.doc.atoms().alive(n)->semantics;sem.implicitHydrogens=1;s.doc.setAtomSemantics(n,sem);s.doc.setAtomLabel(n,label);s.refresh();s.fit();
        const auto visual=arrows::mechanismVisualBoxes(s.doc).at(n);require(visual.hasAttachedProton,"neutral two-bond NH exposes its H");auto press=(visual.attachedProton.min+visual.attachedProton.max)*.5;auto before=s.doc.digest();require(app.beginElectronMarkerDrag(s.view().toDevice(press)),"grab neutral imide NH hydrogen");app.updateElectronMarkerDrag(s.view().toDevice(Vec2(105,70)),true);require(s.doc.incidentBonds(n).size()==3&&s.doc.atoms().alive(n)->semantics.implicitHydrogens==0,"neutral NH extraction keeps both original bonds and counts H exactly once");require(s.undo().ok()&&s.doc.digest()==before,"neutral NH extraction undo");
    }
    for(bool atom:{false,true}){
        const auto ref=commit("bu3snh",atom);
        if(ref.kind==ObjectKind::Atom)app.openText(kInvalidObjectId,ref.id);else app.openText(ref.id);
        app.textEditor.select(0,-1);SendMessageW(app.edit,EM_REPLACESEL,TRUE,LPARAM(L"unlisted900"));require(app.finishText(true),"edit a previously tidied object");
        s.selected={ref};const auto before=s.doc.digest();app.execute(Button("","","","@undoTextTidy"));
        require(s.doc.digest()==before&&(ref.kind==ObjectKind::Atom?s.doc.atomLabel(ref.id):s.doc.plainText(ref.id))=="unlisted900","restoring typography cannot overwrite a later text edit with a stale backup");
    }
    for(bool atom:{false,true})for(const char* alias:{"HEPES","IPTG","CoQ10","dATP","TCEP-HCl","CPADB","Choline"}){
        auto ref=commit(alias,atom);const auto* entry=compounddata::find(alias);require(entry!=nullptr,std::string("new alias resolves uniquely: ")+alias);
        s.selected={ref};if(std::string(alias)=="Choline"){auto caption=studio::analyze(s.doc,s.selected);require(caption.valid&&caption.plain=="C5H14NO (1+)","charged compound caption retains its formula and intrinsic charge");}auto before=s.doc.digest();app.expandGroups();require(s.doc.digest()!=before&&s.doc.stats().aliveAtoms>1,std::string("new alias expands through real editor: ")+alias);auto info=studio::analyze(s.doc,s.selected);require(info.valid,std::string("new expanded graph has formula/mass: ")+alias);require(s.undo().ok()&&s.doc.digest()==before,"new template expansion is one undo");++typed;
    }
    // All complete library graphs must agree with their captions, including Sn,
    // counterions, coordinated metals, explicit hydrides, and isotope water.
    for(const auto& entry:compounddata::entries){
        Document d;auto e=persistence::readCdxml(std::string("<CDXML><page>")+entry.fragment+"</page></CDXML>",d);
        require(e.ok(),std::string("read ")+entry.name);std::vector<ObjectRef> all;for(const auto& a:d.atoms().records())if(a.isAlive())all.push_back(ObjectRef(ObjectKind::Atom,a.id));
        auto graph=studio::analyze(d,all);require(graph.valid,std::string("graph formula: ")+entry.name+" "+graph.note);
        if(compounddata::find(entry.name)){
            ObjectId text;Document caption;caption.addText(Vec2(),10,entry.name,&text);auto label=studio::analyze(caption,{ObjectRef(ObjectKind::Text,text)});
            require(label.valid&&std::abs(label.averageMass-graph.averageMass)<.001,std::string("caption/graph mass agreement: ")+entry.name);
        }++graphs;
    }
    for(const char* name:{"CO2","CS2","Bu3SnH","Bu3Sn-H","Bu3Sn·","Me3Sn·","Ph3Sn·","Et3Si·","Me3Si·","Ph3Si·","HATU","NaIO4","DMF","H2O","O2","N2","AIBN","Me3SnH","SO2","EtOAc","Na2SO4","CuSO4.5H2O","Ph3P","LAH","Ph3SiH"}){
        reset();ObjectId id;s.doc.addText(Vec2(90,90),10,name,&id);s.refresh();s.selected={ObjectRef(ObjectKind::Text,id)};
        const auto collapsed=studio::analyze(s.doc,s.selected);require(collapsed.valid,std::string("known caption ")+name);const auto before=s.doc.digest();
        app.expandGroups();require(!s.selected.empty()&&s.doc.stats().aliveAtoms>0&&s.doc.stats().aliveTexts==0,std::string("expand selected caption: ")+name+" "+app.status);
        auto expanded=studio::analyze(s.doc,s.selected);require(expanded.valid&&std::abs(expanded.averageMass-collapsed.averageMass)<.001,std::string("expanded formula/mass unchanged: ")+name);
        if(std::string(name).find("·")!=std::string::npos){int radicals=0;for(const auto& atom:s.doc.atoms().records())if(atom.isAlive()&&atom.semantics.radical){++radicals;require((atom.element==50||atom.element==14)&&atom.semantics.radical==uint8_t(Radical::Doublet)&&atom.semantics.implicitHydrogens==0,"real radical is on Sn/Si with no phantom H");}require(radicals==1,"expanded radical has exactly one unpaired electron");}
        const auto atoms=s.doc.stats().aliveAtoms,bonds=s.doc.stats().aliveBonds;const auto open=s.doc.digest();
        app.collapseGroups();require(s.doc.stats().aliveAtoms==1&&s.doc.stats().aliveBonds==0,std::string("whole graph collapses: ")+name+" "+app.status);
        auto folded=studio::analyze(s.doc,s.selected);require(folded.valid&&std::abs(folded.averageMass-collapsed.averageMass)<.001,std::string("folded formula/mass: ")+name);
        const auto fold=s.doc.digest();require(s.undo().ok()&&s.doc.digest()==open&&s.undo(true).ok()&&s.doc.digest()==fold,"collapse is one undo/redo");
        app.expandGroups();require(s.doc.stats().aliveAtoms==atoms&&s.doc.stats().aliveBonds==bonds,std::string("saved structure expands again: ")+name);
        require(studio::analyze(s.doc,s.selected).valid,"round-trip graph stays chemically countable");++roundtrips;
        if(std::string(name)=="CS2"){s.fit();app.updateInfo();app.invalidate();app.screenshot(dir+"/carbon-disulfide-expanded.png");}
    }
    reset();ObjectId a,b;s.doc.addText(Vec2(30,30),10,"CO2",&a);s.doc.addText(Vec2(30,70),10,"CS2",&b);s.refresh();
    auto view=s.view();view.center=Vec2(50,50);view.scale=4;view.dpiScale=1;s.setView(view);
    auto pixel=[&](Vec2 point){auto p=s.view().toDevice(point);return MAKELPARAM(int(std::lround(p.x)),int(std::lround(p.y)));};
    SendMessageW(app.canvas,WM_LBUTTONDOWN,MK_LBUTTON,pixel(Vec2(15,10)));SendMessageW(app.canvas,WM_MOUSEMOVE,MK_LBUTTON,pixel(Vec2(90,85)));SendMessageW(app.canvas,WM_LBUTTONUP,0,pixel(Vec2(90,85)));app.updateInfo();
    require(s.selected.size()==2,"the real marquee gesture selects both molecule captions");
    require(app.info.valid&&app.info.plain=="C2O2S2"&&app.info.components==2,"box-selected CO2 and CS2 display combined formula/mass");s.fit();app.invalidate();app.screenshot(dir+"/small-molecule-caption-mass.png");
    HMENU menu=CreatePopupMenu();appendSelectionContextMenu(menu,true,true);
    for(int id:{kContextCopyPngId,9,10,11,12})require(GetMenuState(menu,id,MF_BYCOMMAND)!=UINT(-1),"right-click commands are present without id collisions");DestroyMenu(menu);
    require(s.save(dir+"/small-molecule-caption.cdxml",true).ok(),"save known molecule captions");Session restored;require(restored.open(dir+"/small-molecule-caption.cdxml").ok(),"reopen captions");
    std::vector<ObjectRef> texts;for(const auto& t:restored.doc.texts().records())if(t.isAlive())texts.push_back(ObjectRef(ObjectKind::Text,t.id));require(studio::analyze(restored.doc,texts).plain=="C2O2S2","caption formula survives CDXML round-trip");
    std::ofstream(dir+"/compound-library-checks.json")<<"{\"pass\":true,\"typed_cases\":"<<typed<<",\"library_graphs\":"<<graphs<<",\"roundtrips\":"<<roundtrips<<"}";
}
