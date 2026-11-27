#pragma once
inline void expandGroupCheck(App& a,const std::string& dir){
 auto check=[](bool b,const char* why){if(!b)throw std::runtime_error(why);};
 if(!studioions::find("MeS")||studioions::find("Mes")||studioions::find("MES")||chemcompletion::ambiguousGroups().at("mes").size()!=3)throw std::runtime_error("Mes spelling collision");
 for(const std::string label:{"MeS","Mes","legacy"}){
    Document d;ObjectId ring[6],left,right,b;
    for(int i=0;i<6;i++)d.addAtom(Vec2(std::cos(i*3.14159/3)*30,std::sin(i*3.14159/3)*30),6,&ring[i]);
    for(int i=0;i<6;i++){d.addBond(ring[i],ring[(i+1)%6],&b);if(i%2==0)d.setBondOrder(b,BondOrder::Double);}
    d.addAtom(Vec2(60,0),6,&left);d.addBond(ring[0],left,&b);d.addAtom(Vec2(-30,52),6,&right);d.addBond(ring[2],right,&b);
    functional_groups::checked(nicknames::apply(d,right,"Dipp"));
    if(label=="legacy"){
        functional_groups::checked(nicknames::apply(d,left,"MES"));
        functional_groups::checked(studioions::apply(d,left,*studioions::find("SMe"),0));
    }else functional_groups::checked(nicknames::apply(d,left,label));
    auto expected=label=="Mes"?"C27H32":"C19H24S";
    std::vector<ObjectRef> refs{ObjectRef(ObjectKind::Atom,ring[0])};
    check(studio::analyze(d,refs).plain==expected,"compact aryl formula");
    for(auto id:{left,right}){auto expansion=functional_groups::prepareExpansion(d,id);std::vector<ObjectRef> inserted;functional_groups::applyExpansion(d,expansion,inserted);}
    check(studio::analyze(d,refs).plain==expected&&studio::validation::overValentAtoms(d).empty(),"expanded aryl formula and valence");
 }
 {Document d;ObjectId atom,parent,edge;d.addAtom(Vec2(),6,&atom);functional_groups::checked(nicknames::apply(d,atom,"MES"));
  auto free=functional_groups::prepareExpansion(d,atom);check(free.graph->stats().aliveAtoms==12,"standalone MES heavy atoms");
  d.addAtom(Vec2(30,0),6,&parent);d.addBond(atom,parent,&edge);bool rejected=false;
  try{functional_groups::prepareExpansion(d,atom);}catch(const std::runtime_error&){rejected=true;}check(rejected,"attached MES has no defined port");}
 {
    a.s.doc=Document();a.s.history.clear();a.expandPending=false;
    ObjectId host,bond,buffer; a.s.doc.addAtom(Vec2(),6,&host);
    int i=0;for(const auto label:{"SMe","Mes","MES"}){
        ObjectId tip;a.s.doc.addAtom(direction(120.*i++)*30,6,&tip);a.s.doc.addBond(host,tip,&bond);
        functional_groups::checked(nicknames::apply(a.s.doc,tip,label));buffer=tip;
    }
    a.s.refresh();a.s.selected=a.s.all();a.execute(Button("","","","@expandGroups"));
    check(a.s.doc.stats().aliveAtoms==13&&a.s.doc.atomLabel(buffer)=="MES","mixed selection expands both groups while preserving attached buffer");
 }
 std::ofstream audit(dir+"/nickname-expansion.tsv");int failures=0;
 for(const auto& entry:nickdata::nicknames){
    try{
        Document d;ObjectId parent,group,bond;d.addAtom(Vec2(),6,&parent);d.addAtom(Vec2(30,0),6,&group);d.addBond(parent,group,&bond);
        functional_groups::checked(nicknames::apply(d,group,entry.name));
        auto formula=[&](){std::vector<ObjectRef> all;for(const auto& atom:d.atoms().records())if(atom.isAlive())all.emplace_back(ObjectKind::Atom,atom.id);return studio::analyze(d,all);};
        auto before=formula();
        auto expansion=functional_groups::prepareExpansion(d,group);
        std::vector<ObjectRef> inserted;functional_groups::applyExpansion(d,expansion,inserted);
        auto after=formula();
        bool ok=before.valid&&after.valid&&before.plain==after.plain&&studio::validation::overValentAtoms(d).empty();
        audit<<entry.name<<"\t"<<before.plain<<"\t"<<after.plain<<"\t"<<(ok?"OK":"FAIL")<<"\n";failures+=!ok;
    }catch(const std::exception& e){audit<<entry.name<<"\t"<<e.what()<<"\tFAIL\n";++failures;}
 }
 for(const auto& entry:bridgedata::entries){
    Document d;ObjectId l,g,r,b1,b2;d.addAtom(Vec2(0,0),6,&l);d.addAtom(Vec2(30,0),6,&g);d.addAtom(Vec2(60,0),6,&r);d.addBond(l,g,&b1);d.addBond(g,r,&b2);
    functional_groups::checked(nicknames::apply(d,g,entry.name));
    auto all=std::vector<ObjectRef>{ObjectRef(ObjectKind::Atom,l)};
    auto before=studio::analyze(d,all);auto plan=functional_groups::prepareExpansion(d,g);
    std::vector<ObjectRef> inserted;functional_groups::applyExpansion(d,plan,inserted);auto after=studio::analyze(d,all);
    bool ok=before.valid&&after.valid&&before.plain==after.plain&&d.bonds().alive(b2)->beginAtom!=g&&studio::validation::overValentAtoms(d).empty();
    audit<<entry.name<<"\t"<<before.plain<<"\t"<<after.plain<<"\t"<<(ok?"OK":"FAIL")<<"\n";failures+=!ok;
 }
 for(const auto& entry:compounddata::entries)if(entry.standalone){
    Document d;ObjectId id;d.addAtom(Vec2(),6,&id);
    auto result=nicknames::apply(d,id,entry.name);std::vector<ObjectRef> all{ObjectRef(ObjectKind::Atom,id)};
    auto before=studio::analyze(d,all);auto plan=functional_groups::prepareExpansion(d,id);std::vector<ObjectRef> inserted;
    functional_groups::applyExpansion(d,plan,inserted);auto after=studio::analyze(d,inserted);
    bool ok=result.ok()&&before.valid&&after.valid&&before.plain==after.plain;
    audit<<entry.name<<"\t"<<before.plain<<"\t"<<after.plain<<"\t"<<(ok?"OK":"FAIL")<<"\n";failures+=!ok;
 }
 audit.close();check(failures==0,"catalogue expansion audit failed; see nickname-expansion.tsv");
 // Exercise actual attachment/expansion, including disconnected counterions
 // and the charge-separated MIDA chelate; these cannot be checked by spelling alone.
 for(const char* label:{"Dipp","Tipp","Dep","Mes*","Tbt","PMP","PMBM","PBB","PCB","EE","TPS","SiPh3","Bhoc","NVOC","NPPOC","Dde","ivDde","Bcat","Bneop","Bdan","BMIDA","Baam","BF3K"}){
  Document d;ObjectId parent,group,bond;d.addAtom(Vec2(),7,&parent);d.addAtom(Vec2(30,0),6,&group);d.addBond(parent,group,&bond);
  check(nicknames::apply(d,group,label).ok(),"new nickname application");
  auto expansion=functional_groups::prepareExpansion(d,group);
  check(expansion.graph&&expansion.graph->atoms().alive(expansion.anchor),"new nickname attachment point");
  if(label[0]=='B'&&std::string(label)!="Bhoc")check(expansion.graph->atoms().alive(expansion.anchor)->element==5,"boron group attaches through boron");
  std::vector<ObjectRef> inserted;functional_groups::applyExpansion(d,expansion,inserted);
  check(!inserted.empty(),"new nickname expansion");
  check(d.incidentBonds(parent).size()==1,"parent connection preserved");
  check(studio::validation::overValentAtoms(d).empty(),"expanded new group valence");
  int charge=0,positive=0,negative=0;for(const auto& atom:d.atoms().records())if(atom.isAlive()){charge+=atom.semantics.charge;positive+=atom.semantics.charge>0;negative+=atom.semantics.charge<0;}
  check(charge==0,"expanded group total charge");
  if(std::string(label)=="BMIDA"||std::string(label)=="BF3K")check(positive==1&&negative==1,"boron charge pair retained");
 }
 for(const char* label:{"COOEt","CO2Et","COOMe","COOCH3","COOtBu"}){if(!nickdata::find(label))continue;a.s.doc=Document();a.s.history.clear();a.s.selected.clear();a.expandPending=false;ObjectId parent,group,bond;a.s.doc.addAtom(Vec2(0,0),6,&parent);a.s.doc.addAtom(Vec2(25,-20),6,&group);a.s.doc.addBond(parent,group,&bond);check(nicknames::apply(a.s.doc,group,label).ok(),"ester fixture");a.s.refresh();a.s.selected={{ObjectKind::Atom,group}};auto original=a.s.doc.digest();auto formula=studio::analyze(a.s.doc,a.s.all());a.expandGroups();int compact=0;for(const auto& atom:a.s.doc.atoms().records())if(atom.isAlive())compact+=!functional_groups::definition(a.s.doc,atom.id).empty();check(compact==1&&!a.expandPending&&a.s.doc.stats().aliveAtoms==4,"first ester expansion retains exactly one OR abbreviation");auto first=a.s.doc.digest();auto partial=studio::analyze(a.s.doc,a.s.all());check(formula.valid&&partial.valid&&formula.plain==partial.plain,"first ester formula");a.s.save(dir+"/ester-first-"+label+".cdxml",true);Session saved;check(saved.open(dir+"/ester-first-"+label+".cdxml").ok(),"first stage reopen");a.expandGroups();compact=0;for(const auto& atom:a.s.doc.atoms().records())if(atom.isAlive())compact+=!functional_groups::definition(a.s.doc,atom.id).empty();check(compact==0&&a.s.doc.stats().aliveAtoms>4,"second ester expansion exposes alkyl skeleton");auto full=studio::analyze(a.s.doc,a.s.all());check(full.valid&&full.plain==formula.plain,"second ester formula");a.s.save(dir+"/ester-second-"+label+".cdxml",true);check(a.s.undo().ok()&&a.s.doc.digest()==first&&a.s.undo().ok()&&a.s.doc.digest()==original,"ester steps undo independently");}
 {auto parts=chemcompletion::segments("N3-",true);std::string scripts;for(auto part:parts)for(char c:part.text)scripts+=part.script<0?'-':part.script>0?'+':'0';check(scripts=="0-+","azide count is subscript and charge superscript");auto iron=chemcompletion::segments("Fe3+",true);check(iron.back().script==1,"metal charge rule preserved");Document d;ObjectId n,c,o,r,b;d.addAtom(Vec2(0,0),6,&r);d.addAtom(Vec2(20,-20),7,&n);d.addAtom(Vec2(40,-20),6,&c);d.addAtom(Vec2(60,-10),8,&o);d.addBond(r,n,&b);d.addBond(n,c,&b);d.setBondOrder(b,BondOrder::Double);d.addBond(c,o,&b);d.setBondOrder(b,BondOrder::Double);check(studio_local_cleanup::straightenIsocyanates(d,{c,o}),"isocyanate straightening");auto axis=d.atoms().alive(n)->position-d.atoms().alive(r)->position;auto tail=d.atoms().alive(o)->position-d.atoms().alive(n)->position;check(std::abs(axis.x*tail.y-axis.y*tail.x)<1e-8&&dot(axis,tail)>0,"R N C O aligned");d.setAtomPosition(c,Vec2(40,-20));d.setAtomPosition(o,Vec2(60,-10));Document source;std::vector<ObjectRef> products;for(auto id:{r,n,c,o})products.emplace_back(ObjectKind::Atom,id);studio_local_cleanup::run(a.h,d,source,products);tail=d.atoms().alive(o)->position-d.atoms().alive(n)->position;check(std::abs(axis.x*tail.y-axis.y*tail.x)<1e-8,"generated isocyanate default layout");Session sample;sample.doc=d;sample.refresh();sample.save(dir+"/isocyanate-linear.cdxml",true);}
 {Document d;ObjectId r,g,b;d.addAtom(Vec2(),6,&r);d.addAtom(Vec2(20,0),6,&g);d.addBond(r,g,&b);nicknames::apply(d,g,"COOH");check(!functional_groups::prepareExpansion(d,g).intermediate,"carboxylic acid keeps ordinary expansion");}
 a.s.doc=Document();a.s.history.clear();a.s.selected.clear();a.expandPending=false;a.s.refresh();
 ObjectId n,ph,b;a.s.doc.addAtom(Vec2(),7,&n);a.s.doc.addAtom(Vec2(14.4,0),6,&ph);a.s.doc.addBond(n,ph,&b);nicknames::apply(a.s.doc,ph,"Ph");a.s.refresh();a.s.selected=a.s.all();auto before=a.s.doc.digest();
 a.execute(Button("","","","@expandGroups"));check(a.expandPending,"first click expands and arms cleanup");check(a.s.doc.stats().aliveAtoms==7,"Ph expands six ring atoms");auto expanded=a.s.doc.digest();check(a.s.undo().ok()&&a.s.doc.digest()==before,"expansion atomic undo");check(a.s.undo(true).ok()&&a.s.doc.digest()==expanded,"expansion redo");
 a.execute(Button("","","","@expandGroups"));check(!a.expandPending&&a.s.doc.stats().aliveAtoms==7,"second click cleans");a.s.save(dir+"/ph-expanded.cdxml",true);
 a.expandPending=true;a.expandDigest=a.s.doc.digest();a.expandDocument=a.s.doc.id();a.expandSelection=a.s.selected;a.execute(Button("","","","@fit"));check(!a.expandPending,"other action cancels pending");
 auto digest=a.s.doc.digest();a.s.selected.clear();a.expandPending=true;a.execute(Button("","","","@expandGroups"));check(!a.expandPending&&a.s.doc.digest()==digest,"empty selection preserves document");
 App bad;ObjectId x;bad.s.doc.addAtom(Vec2(),6,&x);auto sem=bad.s.doc.atoms().get(x)->semantics;sem.nodeType=uint8_t(NodeType::Nickname);bad.s.doc.setAtomSemantics(x,sem);bad.s.doc.setAtomLabel(x,"UnknownGroup");bad.s.refresh();bad.s.selected=bad.s.all();auto old=bad.s.doc.digest();bad.expandGroups();check(bad.s.doc.digest()==old&&!bad.expandPending,"unknown abbreviation atomic failure");
 std::ofstream(dir+"/expand-group-checks.json")<<"{\"passed\":true,\"button\":true,\"real_atoms\":true,\"undo_redo\":true,\"second_cleanup\":true,\"action_cancels\":true,\"empty_selection\":true,\"unknown_atomic\":true}";
}
