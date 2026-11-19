#pragma once
static void checkBaseByproduct(App& app,const std::string& dir,const char* font=nullptr,double size=10){
    auto require=[](bool ok,const char* why){if(!ok)throw std::runtime_error(std::string("BH layout: ")+why);};
    auto& s=app.s;s.cancel();s.doc=Document();s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());
    if(font){s.doc.setDefaultFont(font);s.doc.setDefaultTextSize(size);s.doc.documentExtensions().set("studio.style.LabelFontName",font);s.doc.documentExtensions().set("studio.style.LabelSize",std::to_string(size));}
    ObjectId base,h,o,c,oh;s.doc.addAtom(Vec2(20,20),6,&base);require(mechanism::setGenericRole(s.doc,base,"base").ok(),"generic base");
    s.doc.addAtom(Vec2(80,40),1,&h);s.doc.addAtom(Vec2(100,55),8,&o);s.doc.addAtom(Vec2(100,85),6,&c);s.doc.addBond(h,o,&oh);s.doc.addBond(o,c,nullptr);
    for(auto move:std::vector<mechanism::ElectronMove>{{ObjectRef(ObjectKind::Atom,base),ObjectRef(ObjectKind::Atom,h)},{ObjectRef(ObjectKind::Bond,oh),ObjectRef(ObjectKind::Atom,o)}}){
        Vec2 from,to;arrows::mechanismAnchorPoint(s.doc,move.source,&from);arrows::mechanismAnchorPoint(s.doc,move.target,&to);
        auto arrow=arrows::resolvePreset(49,from,to,.6);arrow.mechanism=true;arrow.electronSource=move.source;arrow.electronTarget=move.target;
        arrows::routeMechanism(s.doc,arrow);arrows::addArrow(s.doc,arrow,.6,nullptr);
    }
    s.refresh();auto before=s.doc.digest();s.selected=s.all();app.generateMechanismStep();
    ObjectId b2=kInvalidObjectId,h2=kInvalidObjectId,o2=kInvalidObjectId;arrows::ArrowParams reaction;bool found=false;
    for(auto ref:s.selected){if(ref.kind==ObjectKind::Atom){auto original=studio_mechanism_products::sourceAtom(s.doc,ref.id);if(original==base)b2=ref.id;if(original==h)h2=ref.id;if(original==o)o2=ref.id;}
        if(ref.kind==ObjectKind::Arrow)arrows::readArrow(s.doc,ref.id,&reaction,&found);}
    require(b2!=kInvalidObjectId&&h2!=kInvalidObjectId&&o2!=kInvalidObjectId&&found,"generated product roles");
    auto bPos=s.doc.atoms().alive(b2)->position,hPos=s.doc.atoms().alive(h2)->position;
    require(s.doc.bondBetween(b2,h2)!=kInvalidObjectId&&s.doc.bondBetween(o2,h2)==kInvalidObjectId,"H transfers to B");
    require(s.doc.atoms().alive(b2)->semantics.charge==0&&s.doc.atoms().alive(o2)->semantics.charge==-1,"charge preserved");
    require(std::abs(bPos.y-hPos.y)<1e-8,"B and H share a baseline");
    {auto labels=integration::molecular::labels(s.doc,s.glyphs,integration::RingGeometry(s.doc));text::LayoutOptions options;options.defaultFont=font?font:s.doc.defaultFont();options.defaultSize=s.doc.defaultTextSize();TextRun run;run.utf8="BH";run.fontName=options.defaultFont;run.size=options.defaultSize;text::RichLayout pair;require(text::layoutRichText({run},Vec2(),options,&s.glyphs,&pair).ok()&&pair.glyphs.size()==2,"normal BH typography reference");auto ink=[](const text::RichLayout& layout,size_t index){double left=1e100,right=-1e100;for(const auto& contour:layout.glyphs.at(index).contours)for(auto p:contour){left=std::min(left,p.x);right=std::max(right,p.x);}return layout.anchor.x+(left+right)*.5;};require(std::abs((ink(labels.at(h2).layout,0)-ink(labels.at(b2).layout,0))-(ink(pair,1)-ink(pair,0)))<.03,"B and H spacing matches a single text run");}
    const auto center=(bPos+hPos)*.5;
    require(std::max(bPos.y,hPos.y)<reaction.begin.y-s.doc.defaultTextSize()*.8&&center.x>reaction.begin.x&&center.x<reaction.end.x,"entire BH above arrow");
    require(s.doc.atoms().alive(o2)->position.x>reaction.end.x,"main product on right");
    ObjectId sign=kInvalidObjectId;for(auto ref:s.selected)if(ref.kind==ObjectKind::Text&&s.doc.plainText(ref.id)=="\xE2\x88\x92")sign=ref.id;
    require(sign!=kInvalidObjectId,"BH loss has a separate minus sign");
    auto signPos=s.doc.texts().alive(sign)->position;
    require(signPos.x<std::min(bPos.x,hPos.x)-s.doc.defaultTextSize()*.5&&signPos.y<reaction.begin.y,"loss sign precedes BH above arrow");
    require(std::abs(hPos.x-bPos.x)<s.doc.defaultTextSize()*1.1,"BH is compact formula typography");
    const ObjectRef hiddenBond(ObjectKind::Bond,s.doc.bondBetween(b2,h2));
    for(const auto& primitive:s.scene.primitives())require(primitive.source!=hiddenBond,"no visible bond stroke in BH condition");
    auto after=s.doc.digest();require(s.undo().ok()&&s.doc.digest()==before,"one undo restores source");require(s.undo(true).ok()&&s.doc.digest()==after,"redo restores layout");
    require(s.save(dir+"/bh-above-arrow.cdxml",true).ok(),"save example");Session reopened;require(reopened.open(dir+"/bh-above-arrow.cdxml").ok(),"reopen example");bool signSaved=false;for(auto ref:reopened.all())if(ref.kind==ObjectKind::Text&&reopened.doc.plainText(ref.id)=="\xE2\x88\x92")signSaved=true;require(signSaved,"loss minus survives CDXML");std::string exported;require(studioio::exportXml(s.doc,&exported).ok()&&exported.find("Visible=\"no\"")!=std::string::npos,"compact condition bond hidden in ChemDraw export");s.selected.clear();s.fit();app.invalidate();app.screenshot(dir+"/bh-above-arrow.png");
    std::ofstream(dir+"/bh-above-arrow-checks.json")<<"{\"minus_prefix\":true,\"above_arrow\":true,\"editable_bh_bond\":true,\"charge\":true,\"main_product_right\":true,\"undo_redo\":true}";
}

static void checkMultilineByproducts(App& app,const std::string& dir){
    auto require=[](bool ok,const char* why){if(!ok)throw std::runtime_error(std::string("multiline losses: ")+why);};
    auto& s=app.s;s.cancel();s.doc=Document();s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());
    auto atom=[&](Vec2 p,int element,int hydrogens){ObjectId id;s.doc.addAtom(p,element,&id);auto sem=s.doc.atoms().alive(id)->semantics;sem.implicitHydrogens=hydrogens;s.doc.setAtomSemantics(id,sem);return id;};
    auto base=atom(Vec2(15,90),6,0);mechanism::setGenericRole(s.doc,base,"base");
    auto a=atom(Vec2(80,40),6,2),b=atom(Vec2(110,40),6,1),methyl=atom(Vec2(135,20),6,3),br=atom(Vec2(60,15),35,0),h=atom(Vec2(120,68),1,0);
    ObjectId ab,cb,ch;s.doc.addBond(a,b,&ab);s.doc.addBond(a,br,&cb);s.doc.addBond(b,h,&ch);s.doc.addBond(b,methyl,nullptr);
    for(auto move:std::vector<mechanism::ElectronMove>{{ObjectRef(ObjectKind::Atom,base),ObjectRef(ObjectKind::Atom,h)},{ObjectRef(ObjectKind::Bond,ch),ObjectRef(ObjectKind::Bond,ab)},{ObjectRef(ObjectKind::Bond,cb),ObjectRef(ObjectKind::Atom,br)}}){
        Vec2 from,to;arrows::mechanismAnchorPoint(s.doc,move.source,&from);arrows::mechanismAnchorPoint(s.doc,move.target,&to);auto p=arrows::resolvePreset(49,from,to,.6);p.mechanism=true;p.electronSource=move.source;p.electronTarget=move.target;arrows::routeMechanism(s.doc,p);arrows::addArrow(s.doc,p,.6,nullptr);
    }
    s.refresh();s.selected=s.all();auto before=s.doc.digest();app.generateMechanismStep();
    ObjectId newB=kInvalidObjectId,newBr=kInvalidObjectId;arrows::ArrowParams reaction;bool found=false;std::vector<double> signs;
    for(auto ref:s.selected){if(ref.kind==ObjectKind::Atom){auto source=studio_mechanism_products::sourceAtom(s.doc,ref.id);if(source==base)newB=ref.id;if(source==br)newBr=ref.id;}else if(ref.kind==ObjectKind::Text&&s.doc.plainText(ref.id)=="\xE2\x88\x92")signs.push_back(s.doc.texts().alive(ref.id)->position.y);else if(ref.kind==ObjectKind::Arrow)arrows::readArrow(s.doc,ref.id,&reaction,&found);}
    require(found&&newB!=kInvalidObjectId&&newBr!=kInvalidObjectId,"E2 creates BH and bromide");
    require(signs.size()==2&&std::abs(signs[0]-signs[1])>s.doc.defaultTextSize()*1.2,"each loss has its own vertically separated minus");
    auto bp=s.doc.atoms().alive(newB)->position,rp=s.doc.atoms().alive(newBr)->position;
    require(std::abs(bp.y-rp.y)>s.doc.defaultTextSize()&&std::max(bp.y,rp.y)<reaction.begin.y-s.doc.defaultTextSize()*.8,"BH and bromide occupy separate rows above arrow");
    require(s.doc.atoms().alive(newBr)->semantics.charge==-1&&s.doc.atoms().alive(newB)->semantics.charge==0,"row layout preserves charges");
    auto after=s.doc.digest();require(s.undo().ok()&&s.doc.digest()==before&&s.undo(true).ok()&&s.doc.digest()==after,"multiline layout undoes and redoes atomically");
    require(s.save(dir+"/multiline-losses.cdxml",true).ok(),"save multiline example");Session reopened;require(reopened.open(dir+"/multiline-losses.cdxml").ok(),"reopen multiline example");int count=0;for(auto ref:reopened.all())if(ref.kind==ObjectKind::Text&&reopened.doc.plainText(ref.id)=="\xE2\x88\x92")++count;require(count==2,"both loss rows survive CDXML");
    s.selected.clear();s.fit();app.invalidate();app.screenshot(dir+"/multiline-losses.png");
    std::ofstream(dir+"/multiline-loss-checks.json")<<"{\"separate_rows\":true,\"minus_per_row\":true,\"charges\":true,\"undo_redo\":true,\"cdxml\":true}";
}
