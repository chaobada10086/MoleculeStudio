#pragma once
static void checkGeneratedSpacing(App& app,const std::string& dir){
    auto require=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};
    Document d;d.setDefaultBondLength(30);ObjectId n,p,other,edge,nn;
    d.addAtom(Vec2(90,65),7,&n);d.addAtom(Vec2(81,76),15,&p);d.setAtomLabel(p,"Ph3P");d.addAtom(Vec2(120,65),7,&other);
    d.addBond(n,p,&edge);d.addBond(n,other,&nn);auto sem=d.atoms().alive(p)->semantics;sem.charge=1;sem.implicitHydrogens=0;d.setAtomSemantics(p,sem);sem=d.atoms().alive(other)->semantics;sem.charge=-1;sem.implicitHydrogens=0;d.setAtomSemantics(other,sem);
    ObjectId ring[6];for(int i=0;i<6;++i)d.addAtom(Vec2(90+30*std::cos(i*3.141592653589793/3),10+30*std::sin(i*3.141592653589793/3)),6,&ring[i]);
    for(int i=0;i<6;++i){ObjectId b;d.addBond(ring[i],ring[(i+1)%6],&b);}ObjectId link;d.addBond(n,ring[0],&link);
    auto original=d;std::vector<ObjectRef> refs;for(const auto& a:d.atoms().records())if(a.isAlive())refs.emplace_back(ObjectKind::Atom,a.id);
    auto visible=[&](const Document& doc,ObjectId id){std::unique_ptr<text::GlyphProvider> provider(text::createGdiGlyphProvider("Arial","Microsoft YaHei"));text::GlyphCache glyphs(provider.get());integration::RingGeometry rings(doc);auto labels=integration::molecular::clippingLabels(doc,integration::molecular::labels(doc,glyphs,rings));return studio_generated_spacing::visibleLength(doc,*doc.bonds().alive(id),rings,labels);};
    require(visible(d,edge)<30*.35,"fixture has a clipped short N--P bond");
    require(studio_generated_spacing::improve(d,refs).ok(),"space generated labels");
    require(visible(d,edge)>=30*.35-.01,"N--P bond remains visibly long enough after label and charge clipping");
    for(auto id:ring)require(length(d.atoms().alive(id)->position-original.atoms().alive(id)->position)<1e-8,"remote ring stays fixed");
    require(d.stats().aliveAtoms==original.stats().aliveAtoms&&d.stats().aliveBonds==original.stats().aliveBonds&&d.atoms().alive(p)->semantics.charge==1,"spacing preserves connectivity and charge");
    auto once=d.digest();require(studio_generated_spacing::improve(d,refs).ok()&&d.digest()==once,"spacing is idempotent");
    auto save=[&](const Document& snapshot,const char* name){app.s.cancel();app.s.doc=snapshot;app.s.history.clear();app.s.selected.clear();app.s.refresh();app.s.fit();app.invalidate();app.screenshot(dir+"/"+name+".png");require(app.s.save(dir+"/"+name+".cdxml",true).ok(),"save spacing example");};
    save(original,"generated-spacing-before");save(d,"generated-spacing-after");
    Document healthy;ObjectId a,b,bond;healthy.addAtom(Vec2(),6,&a);healthy.addAtom(Vec2(30,0),6,&b);healthy.addBond(a,b,&bond);healthy.setDefaultBondLength(30);auto before=healthy.digest();
    require(studio_generated_spacing::improve(healthy,{{ObjectKind::Atom,a},{ObjectKind::Atom,b}}).ok()&&healthy.digest()==before,"normal skeletal bonds are unchanged");
    for(auto display:{BondDisplay::WedgeHash,BondDisplay::Wedge}){
        Document stereo;stereo.setDefaultBondLength(30);ObjectId c,h,ch;stereo.addAtom(Vec2(),6,&c);stereo.addAtom(Vec2(0,-30),1,&h);stereo.addBond(c,h,&ch);stereo.setBondDisplay(ch,display);
        auto before=stereo.digest();require(studio_generated_spacing::improve(stereo,{{ObjectKind::Atom,c},{ObjectKind::Atom,h}}).ok()&&stereo.digest()==before,"normal explicit C--H wedge and hashed wedge never stretch");
        require(visible(stereo,ch)>15,"hashed wedge measures the entire axial envelope");
    }
    auto fixed=original;require(studio_generated_spacing::improve(fixed,{{ObjectKind::Atom,n}}).ok()&&fixed.digest()==original.digest(),"unselected atoms never move");
    // A ring-opening branch can land exactly on the neighboring carbonyl O.
    Document overlap;overlap.setDefaultBondLength(30);ObjectId a0,a1,oxygen,nitrogen,tail,edge0;
    overlap.addAtom(Vec2(0,0),6,&a0);overlap.addAtom(Vec2(30,0),6,&a1);overlap.addAtom(Vec2(30,30),8,&oxygen);overlap.addAtom(Vec2(30,30),7,&nitrogen);overlap.addAtom(Vec2(55,45),6,&tail);
    overlap.addBond(a0,a1,nullptr);overlap.addBond(a1,oxygen,&edge0);overlap.setBondOrder(edge0,BondOrder::Double);overlap.addBond(a0,nitrogen,nullptr);overlap.addBond(nitrogen,tail,nullptr);
    std::vector<ObjectRef> made;for(auto id:{a0,a1,oxygen,nitrogen,tail})made.emplace_back(ObjectKind::Atom,id);
    auto graph=overlap.stats();require(studio_generated_spacing::improve(overlap,made).ok(),"separate generated overlapping carbonyl and nitrogen branch");
    require(length(overlap.atoms().alive(oxygen)->position-overlap.atoms().alive(nitrogen)->position)>15,"O and N labels no longer overlap");
    require(overlap.stats().aliveAtoms==graph.aliveAtoms&&overlap.stats().aliveBonds==graph.aliveBonds,"overlap cleanup preserves graph");
    auto fixedOverlap=overlap.digest();require(studio_generated_spacing::improve(overlap,made).ok()&&overlap.digest()==fixedOverlap,"overlap cleanup is stable on repeat");
    save(overlap,"generated-overlap-after");
    std::ofstream(dir+"/generated-spacing-checks.json")<<"{\"visible_NP_bond\":true,\"remote_ring_fixed\":true,\"idempotent\":true,\"connectivity_charge_preserved\":true}";
}
