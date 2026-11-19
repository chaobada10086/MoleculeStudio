#pragma once
static void checkMethanolMechanism(App& app,const std::string& dir){
    auto require=[](bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);};
    auto& s=app.s;
    auto reset=[&](){s.cancel();s.doc=Document();s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());s.doc.setDefaultBondLength(30);s.refresh();};
    auto atom=[&](Vec2 p,int z){ObjectId id;s.doc.addAtom(p,z,&id);return id;};
    auto bond=[&](ObjectId a,ObjectId b){ObjectId id;s.doc.addBond(a,b,&id);return id;};
    // The same compact methanol and pulled-out H used by the drawing UI.
    reset();auto methanol=atom(Vec2(180,140),6);ObjectId h;
    require(nicknames::apply(s.doc,methanol,"MeOH").ok(),"type compact methanol");
    require(exposeAttachedProton(s.doc,methanol,Vec2(210,140),&h).ok(),"pull methanol H into a real bond");s.refresh();
    require(s.doc.atoms().alive(methanol)->element==8,"compact methoxy retains oxygen identity");
    integration::RingGeometry rings(s.doc);auto labels=integration::molecular::labels(s.doc,s.glyphs,rings);
    Box oxygen;bool ink=false;const auto& layout=labels.at(methanol).layout;
    for(const auto& g:layout.glyphs)if(g.codepoint=='O')for(const auto& contour:g.contours)for(auto p:contour){p=p+layout.anchor;if(ink)oxygen.add(p);else{oxygen=Box(p,p);ink=true;}}
    const auto body=arrows::mechanismAtomBodyBox(s.doc,*s.doc.atoms().alive(methanol));
    require(ink&&length(body.min-oxygen.min)<1e-6&&length(body.max-oxygen.max)<1e-6,"MeO capture body is the O ink only");
    auto p=arrows::resolvePreset(49,Vec2(195,140),(oxygen.min+oxygen.max)*.5,.6);p.mechanism=true;
    p.electronSource=ObjectRef(ObjectKind::Bond,s.doc.bondBetween(methanol,h));p.electronTarget=ObjectRef(ObjectKind::Atom,methanol);
    arrows::routeMechanism(s.doc,p,true);arrows::clearMechanismLabels(s.doc,p);
    ObjectId aid;require(arrows::addArrow(s.doc,p,.6,&aid).ok(),"return arrow targets methoxy oxygen");
    require(s.save(dir+"/methanol-oxygen-anchor.cdxml",true).ok(),"save methanol capture example");
    {
        Session legacy;legacy.doc=s.doc;auto sem=legacy.doc.atoms().alive(methanol)->semantics;sem.nodeType=uint8_t(NodeType::Query);
        legacy.doc.setElement(methanol,6);legacy.doc.setAtomSemantics(methanol,sem);
        auto& bag=legacy.doc.extensions(ObjectRef(ObjectKind::Atom,methanol));bag.erase("studio.nickname.fragment");bag.erase("studio.mechanism.ion.base");bag.erase("studio.mechanism.compact.valence");bag.set("pst.attr.MoleculeStudioLiteralLabel","1");
        require(legacy.save(dir+"/legacy-meo-h.cdxml",true).ok(),"save old textual methoxy-H");Session migrated;
        require(migrated.open(dir+"/legacy-meo-h.cdxml").ok()&&migrated.doc.atoms().alive(methanol)->element==8&&migrated.dirty,"old methoxy-H upgrades to oxygen on reopen");
    }
    // Reported hydroxy/methoxy bicyclic cleavage; compete with an alkoxide.
    reset();auto apex=atom(Vec2(600,470),6),o=atom(Vec2(600,440),8),om=atom(Vec2(630,470),8);
    auto l=atom(Vec2(585,496),6),r=atom(Vec2(615,496),6),ll=atom(Vec2(576,525),6),bottom=atom(Vec2(600,543),6),rr=atom(Vec2(624,525),6);
    auto co=bond(apex,o);bond(apex,om);bond(apex,l);auto breaking=bond(apex,r);bond(l,r);bond(l,ll);bond(ll,bottom);bond(bottom,rr);bond(rr,r);
    require(nicknames::apply(s.doc,om,"OMe").ok(),"bicyclic methoxy abbreviation");
    auto os=s.doc.atoms().alive(o)->semantics;os.implicitHydrogens=1;s.doc.setAtomSemantics(o,os);
    auto competitor=atom(Vec2(600,573),8);bond(bottom,competitor);
    auto bs=s.doc.atoms().alive(competitor)->semantics;bs.charge=-1;bs.implicitHydrogens=0;s.doc.setAtomSemantics(competitor,bs);
    for(auto move:std::vector<mechanism::ElectronMove>{{ObjectRef(ObjectKind::Atom,o),ObjectRef(ObjectKind::Bond,co)},{ObjectRef(ObjectKind::Bond,breaking),ObjectRef(ObjectKind::Atom,r)}}){
        Vec2 from,to;arrows::mechanismAnchorPoint(s.doc,move.source,&from);arrows::mechanismAnchorPoint(s.doc,move.target,&to);
        auto a=arrows::resolvePreset(49,from,to,.6);a.mechanism=true;a.electronSource=move.source;a.electronTarget=move.target;arrows::routeMechanism(s.doc,a,true);ObjectId id;arrows::addArrow(s.doc,a,.6,&id);
    }
    s.refresh();s.selected=s.all();auto before=s.doc.digest();app.generateMechanismStep(1);
    require(s.doc.digest()!=before,"ring cleavage plus proton transfer succeeds: "+app.status);
    ObjectId productO=kInvalidObjectId,productC=kInvalidObjectId,productCompetitor=kInvalidObjectId;
    for(auto ref:s.selected)if(ref.kind==ObjectKind::Atom){auto source=studio_mechanism_products::sourceAtom(s.doc,ref.id);if(source==o)productO=ref.id;if(source==r)productC=ref.id;if(source==competitor)productCompetitor=ref.id;}
    {std::ofstream diagnostic(dir+"/proton-priority-sites.txt");for(auto ref:s.selected)if(ref.kind==ObjectKind::Atom){const auto* a=s.doc.atoms().alive(ref.id);diagnostic<<ref.id<<" source="<<studio_mechanism_products::sourceAtom(s.doc,ref.id)<<" z="<<a->element<<" q="<<int(a->semantics.charge)<<" H="<<studio_mechanism_products::hydrogenCount(s.doc,ref.id)<<"\n";}}
    require(s.save(dir+"/ring-opening-proton-transfer.cdxml",true).ok(),"save combined proton transfer");
    require(productO!=kInvalidObjectId&&productC!=kInvalidObjectId&&productCompetitor!=kInvalidObjectId,"mapped product sites retained");
    require(s.doc.atoms().alive(productO)->semantics.charge==0&&s.doc.atoms().alive(productC)->semantics.charge==0&&s.doc.atoms().alive(productCompetitor)->semantics.charge==-1,"carbon anion protonated before competing oxygen anion");
    require(studio_mechanism_products::hydrogenCount(s.doc,productO)==0&&studio_mechanism_products::hydrogenCount(s.doc,productC)==2,"proton moves from oxonium onto opened carbon");
    require(s.undo().ok()&&s.doc.digest()==before,"combined proton transfer undoes atomically");
    // Real cleanup service with explicit H and OMe, translated far from origin.
    reset();auto c=atom(Vec2(600,500),6),c2=atom(Vec2(630,505),6),ox=atom(Vec2(655,480),8),hydrogen=atom(Vec2(590,475),1);
    bond(c,c2);bond(c2,ox);bond(c,hydrogen);require(nicknames::apply(s.doc,ox,"OMe").ok(),"cleanup methoxy fragment");
    s.refresh();s.selected=s.all();before=s.doc.digest();app.chemistryAction("clean");
    require(s.doc.digest()!=before,"cleanup succeeds with explicit H: "+app.status);
    for(const auto& b:s.doc.bonds().records())if(b.isAlive()){
        const double distance=length(s.doc.atoms().alive(b.beginAtom)->position-s.doc.atoms().alive(b.endAtom)->position);
        require(distance>8&&distance<65,"cleanup keeps H and heavy bonds local");
    }
    require(s.doc.atoms().alive(hydrogen)&&s.doc.atomLabel(ox)=="OMe","cleanup retains explicit H and compact methoxy");
    require(s.save(dir+"/cleaned-explicit-h-methoxy.cdxml",true).ok(),"save cleaned explicit hydrogen example");
    require(s.undo().ok()&&s.doc.digest()==before,"cleanup coordinate update is one undo");
    std::ofstream(dir+"/methanol-mechanism-checks.json")<<"{\"oxygen_glyph_anchor\":true,\"carbanion_proton_priority\":true,\"competing_alkoxide\":true,\"explicit_h_cleanup\":true,\"undo\":true}";
}
