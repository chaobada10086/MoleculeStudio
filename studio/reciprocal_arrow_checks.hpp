#pragma once
static void checkReciprocalArrows(App& app,const std::string& dir){
    auto require=[](bool ok,const char* why){if(!ok)throw std::runtime_error(std::string("reciprocal arrows: ")+why);};auto& s=app.s;
    s.cancel();s.doc=Document();s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());
    ObjectId c,o,left,n,co;s.doc.addAtom(Vec2(120,100),6,&c);s.doc.addAtom(Vec2(120,60),8,&o);s.doc.addAtom(Vec2(80,120),6,&left);s.doc.addAtom(Vec2(160,120),7,&n);
    s.doc.addBond(c,o,&co);s.doc.setBondOrder(co,BondOrder::Double);s.doc.addBond(c,left,nullptr);s.doc.addBond(c,n,nullptr);
    for(auto id:{o,n}){auto sem=s.doc.atoms().alive(id)->semantics;sem.implicitHydrogens=id==n?2:0;s.doc.setAtomSemantics(id,sem);}
    s.refresh();auto v=s.view();v.scale=3;s.setView(v);app.chooseArrowPreset(49);
    auto gesture=[&](Vec2 from,Vec2 via,Vec2 to){return s.gesture(PointerPhase::Down,from).ok()&&s.gesture(PointerPhase::Move,via).ok()&&s.gesture(PointerPhase::Move,to).ok()&&s.gesture(PointerPhase::Up,to).ok();};
    require(gesture(Vec2(116,80),Vec2(80,70),Vec2(120,60)),"draw first bond-to-oxygen curve");
    require(s.doc.arrows().size()==1,"first gesture creates one arrow");arrows::ArrowParams first;bool valid=false;arrows::readArrow(s.doc,0,&first,&valid);s.save(dir+"/reciprocal-first-debug.cdxml",true);std::ofstream(dir+"/reciprocal-endpoints.txt")<<int(first.electronSource.kind)<<" "<<first.electronSource.id<<" -> "<<int(first.electronTarget.kind)<<" "<<first.electronTarget.id<<" expected bond "<<co<<" atom "<<o;require(valid&&first.electronSource==ObjectRef(ObjectKind::Bond,co)&&first.electronTarget==ObjectRef(ObjectKind::Atom,o),"captured first semantic endpoints");
    auto before=s.doc.digest();s.selected.clear();
    require(s.gesture(PointerPhase::Down,Vec2(120,60)).ok()&&s.gesture(PointerPhase::Move,Vec2(165,70)).ok()&&s.gesture(PointerPhase::Move,Vec2(116,80)).ok(),"preview inverse gesture");
    require(s.doc.digest()==before,"inverse preview does not mutate first curve");
    require(s.gesture(PointerPhase::Up,Vec2(116,80)).ok(),"release inverse gesture");
    require(s.doc.arrows().size()==1,"opposite pair merges to one object on release");arrows::ArrowParams merged;arrows::readArrow(s.doc,0,&merged,&valid);require(valid&&merged.head.atBegin&&merged.head.atEnd,"both tips visible");
    auto expected=first;expected.head.atBegin=true;require(arrows::encodeArrowParams(expected)==arrows::encodeArrowParams(merged),"first curve and bindings stay exactly in place");
    arrows::ArrowGeometry geometry;arrows::buildArrowGeometry(merged,s.doc.defaultLineWidth(),arrows::Tessellation(),&geometry);require(geometry.heads.size()==2,"two rendered heads");
    auto after=s.doc.digest();require(s.undo().ok()&&s.doc.digest()==before&&s.undo(true).ok()&&s.doc.digest()==after,"merge is a single undoable step");
    require(s.save(dir+"/reciprocal-arrow.cdxml",true).ok(),"save merged curve");Session reopened;require(reopened.open(dir+"/reciprocal-arrow.cdxml").ok(),"reopen merged curve");arrows::ArrowParams back;arrows::readArrow(reopened.doc,0,&back,&valid);require(valid&&back.head.atBegin&&back.head.atEnd,"both tips survive CDXML");
    // A merged pair and its original two directed moves have identical chemistry.
    auto reverse=arrows::reversedArrow(first);std::vector<mechanism::ElectronMove> pairMoves,mergedMoves;
    auto x=mechanism::inferElectronMoves(s.doc,{first,reverse},&pairMoves),y=mechanism::inferElectronMoves(s.doc,{merged},&mergedMoves);
    require(x.ok()==y.ok()&&(!x.ok()||pairMoves.size()==mergedMoves.size()),"merged notation preserves inference outcome");
    s.selected.clear();s.fit();app.invalidate();app.screenshot(dir+"/reciprocal-arrow.png");
    ObjectId nu,otherNu;s.doc.addAtom(Vec2(210,90),7,&nu);s.doc.addAtom(Vec2(240,90),7,&otherNu);s.doc.addBond(nu,otherNu,nullptr);
    for(auto id:{nu,otherNu}){auto sem=s.doc.atoms().alive(id)->semantics;sem.implicitHydrogens=2;s.doc.setAtomSemantics(id,sem);}
    for(auto move:std::vector<mechanism::ElectronMove>{{ObjectRef(ObjectKind::Atom,nu),ObjectRef(ObjectKind::Atom,c)},{ObjectRef(ObjectKind::Bond,s.doc.bondBetween(c,n)),ObjectRef(ObjectKind::Atom,n)}}){Vec2 a,z;arrows::mechanismAnchorPoint(s.doc,move.source,&a);arrows::mechanismAnchorPoint(s.doc,move.target,&z);auto p=arrows::resolvePreset(49,a,z,.6);p.mechanism=true;p.electronSource=move.source;p.electronTarget=move.target;arrows::routeMechanism(s.doc,p);arrows::addArrow(s.doc,p,.6,nullptr);}
    std::vector<arrows::ArrowParams> directed;for(const auto& record:s.doc.arrows().records())if(record.isAlive()){arrows::ArrowParams p;bool ok=false;arrows::readArrow(s.doc,record.id,&p,&ok);if(!ok)continue;if(p.head.atBegin){p.head.atBegin=false;directed.push_back(p);directed.push_back(arrows::reversedArrow(p));}else directed.push_back(p);}
    Document fallback;std::vector<mechanism::ElectronMove> fallbackMoves;std::vector<ObjectRef> fallbackProducts;
    auto fallbackResult=studio_drawn_step::prepare(s.doc,s.all(),directed,fallback,fallbackMoves,fallbackProducts);
    require(fallbackResult.ok()&&fallbackMoves.size()==directed.size(),"direct-edit fallback uses one directed transfer per expanded input");
    studioio::Page narrow=studioio::Page::get(s.doc);
    narrow.width=337;narrow.x=0;narrow.across=1;narrow.store(s.doc);
    s.refresh();s.selected=s.all();auto sourceDigest=s.doc.digest();app.generateMechanismStep();require(s.doc.digest()!=sourceDigest,"merged carbonyl pair participates in addition-elimination generation");
    std::map<ObjectId,ObjectId> products;for(auto ref:s.selected)if(ref.kind==ObjectKind::Atom)products[studio_mechanism_products::sourceAtom(s.doc,ref.id)]=ref.id;
    require(products.count(c)&&products.count(o)&&products.count(nu)&&products.count(n),"all participants generated");
    require(s.doc.bonds().alive(s.doc.bondBetween(products[c],products[o]))->order==BondOrder::Double&&s.doc.bondBetween(products[c],products[nu])!=kInvalidObjectId&&s.doc.bondBetween(products[c],products[n])==kInvalidObjectId,"two-way pair preserves carbonyl while substitution forms and cleaves the requested bonds");
    require(s.doc.atoms().alive(products[nu])->semantics.charge==1&&s.doc.atoms().alive(products[n])->semantics.charge==-1,"substitution charges preserved");
    auto page=studioio::Page::get(s.doc);
    std::vector<ObjectRef> productAtoms;
    for(auto entry:products)productAtoms.emplace_back(ObjectKind::Atom,entry.second);
    const auto productBounds=studio_mechanism_page_flow::InkBounds(s.doc).bounds(productAtoms);
    require(page.across==1&&productBounds.min.x>=24-1e-6&&productBounds.min.x<42&&productBounds.min.y>120,
            "right edge wraps product structures to the lower left without adding columns");
    bool connectorEarlier=false;
    for(auto ref:s.selected)if(ref.kind==ObjectKind::Arrow){
        arrows::ArrowParams connector;bool ok=false;arrows::readArrow(s.doc,ref.id,&connector,&ok);
        connectorEarlier=ok&&!connector.mechanism&&connector.begin.x>240&&connector.begin.y<productBounds.min.y;
    }
    require(connectorEarlier,"generated reaction arrow remains on the previous row");
    require(s.save(dir+"/reciprocal-substitution.cdxml",true).ok(),"save substitution example");s.selected.clear();s.fit();app.invalidate();app.screenshot(dir+"/reciprocal-substitution.png");
    std::ofstream(dir+"/reciprocal-arrow-checks.json")<<"{\"release_merge\":true,\"first_route_preserved\":true,\"two_heads\":true,\"undo_redo\":true,\"cdxml\":true}";
}
