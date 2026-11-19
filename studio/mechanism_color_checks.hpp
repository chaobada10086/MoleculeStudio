#pragma once
static void checkMechanismColors(App& app,const std::string& dir){
    auto require=[](bool ok,const std::string& why){if(!ok)throw std::runtime_error("mechanism colour: "+why);};
    const uint32_t blue=0x0046DCFFu,green=0x008C46FFu,red=0xDC1E1EFFu,purple=0x8B39B5FFu;
    auto A=[](ObjectId id){return ObjectRef(ObjectKind::Atom,id);};auto B=[](ObjectId id){return ObjectRef(ObjectKind::Bond,id);};
    auto& s=app.s;s.cancel();s.history.clear();s.doc=Document();s.selected.clear();s.input.bindDocument(s.doc.id());
    s.doc.setDefaultBondLength(24);s.doc.setDefaultFont("Arial");s.doc.setDefaultTextSize(10);
    std::vector<ObjectId> ring,edges;const Vec2 corners[]={{-120,-60},{-99,-48},{-99,-24},{-120,-12},{-141,-24},{-141,-48}};
    for(auto point:corners){ObjectId id;s.doc.addAtom(point,6,&id);ring.push_back(id);}
    for(int i=0;i<6;++i){ObjectId id;s.doc.addBond(ring[i],ring[(i+1)%6],&id);if(i%2==0)s.doc.setBondOrder(id,BondOrder::Double);edges.push_back(id);
        s.doc.extensions(B(id)).set(i%2?"studio.color.override":"studio.color",std::to_string(blue));}
    ObjectId beta,alpha,br,h,base,axis,ch,cbr;
    s.doc.addAtom(Vec2(-74,-9),6,&beta);s.doc.addAtom(Vec2(-50,-24),6,&alpha);s.doc.addBond(ring[2],beta,nullptr);s.doc.addBond(beta,alpha,&axis);
    s.doc.addAtom(Vec2(-50,-48),35,&br);s.doc.addBond(alpha,br,&cbr);s.doc.addAtom(Vec2(-74,15),1,&h);s.doc.addBond(beta,h,&ch);
    s.doc.addAtom(Vec2(-119,54),6,&base);require(mechanism::setGenericRole(s.doc,base,"base").ok(),"create base");
    s.doc.extensions(B(axis)).set("studio.color",std::to_string(blue));s.doc.extensions(B(axis)).set("studio.color.override",std::to_string(green));
    s.doc.extensions(A(br)).set("studio.color",std::to_string(red));s.doc.extensions(A(base)).set("studio.color.override",std::to_string(purple));
    s.doc.extensions(A(h)).set("studio.text.colors",std::to_string(red));s.doc.extensions(A(h)).set("studio.text.colors.edited","1");
    std::vector<arrows::ArrowParams> inputs;
    for(auto link:std::vector<std::pair<ObjectRef,ObjectRef>>{{A(base),A(h)},{B(ch),B(axis)},{B(cbr),A(br)}}){
        Vec2 from,to;arrows::mechanismAnchorPoint(s.doc,link.first,&from);arrows::mechanismAnchorPoint(s.doc,link.second,&to);
        auto p=arrows::resolvePreset(49,from,to,.6);p.mechanism=true;p.electronSource=link.first;p.electronTarget=link.second;arrows::routeMechanism(s.doc,p);
        require(arrows::addArrow(s.doc,p,.6,nullptr).ok(),"store colour fixture arrows");inputs.push_back(p);
    }
    s.refresh();const auto source=s.doc;const auto before=s.doc.digest();s.selected=s.all();s.graphicsColor=0xFF8800FFu;app.generateMechanismStep();
    require(s.doc.stats().aliveAtoms==source.stats().aliveAtoms*2,"E2 generation succeeds: "+app.status);
    auto product=s.selected;std::map<ObjectId,ObjectId> copies;for(auto ref:product)if(ref.kind==ObjectKind::Atom)copies[studio_mechanism_products::sourceAtom(s.doc,ref.id)]=ref.id;
    s.selected.clear();s.fit();app.screenshot(dir+"/mechanism-colour-E2.png");s.save(dir+"/mechanism-colour-E2.cdxml",true);
    auto painted=[&](ObjectRef ref,uint32_t expected){bool visible=false;for(const auto& p:s.scene.primitives())if(p.source==ref){if(p.style.stroke.enabled){require(p.style.stroke.rgba==expected,"stroke retains source colour, object "+std::to_string(ref.id)+" actual "+std::to_string(p.style.stroke.rgba));visible=true;}if(p.style.fill.enabled&&(p.kind==r62::render::PrimKind::Polygon||p.kind==r62::render::PrimKind::Rect||p.kind==r62::render::PrimKind::Ellipse)){require(p.style.fill.rgba==expected,"glyph retains source colour, object "+std::to_string(ref.id)+" actual "+std::to_string(p.style.fill.rgba));visible=true;}}require(visible,"coloured product is visibly painted");};
    for(int i=0;i<6;++i){auto id=s.doc.bondBetween(copies.at(ring[i]),copies.at(ring[(i+1)%6]));require(id!=kInvalidObjectId,"phenyl bond retained");painted(B(id),blue);}
    auto alkene=s.doc.bondBetween(copies.at(beta),copies.at(alpha));require(s.doc.bonds().alive(alkene)->order==BondOrder::Double,"green reaction bond becomes double");painted(B(alkene),green);
    painted(A(copies.at(br)),red);painted(A(copies.at(h)),red);painted(A(copies.at(base)),purple);
    require(s.doc.atoms().alive(copies.at(br))->semantics.charge==-1,"coloured leaving group keeps chemistry");
    for(auto ref:product)if(ref.kind==ObjectKind::Bond){auto b=s.doc.bonds().alive(ref.id);if(b->beginAtom==copies.at(base)||b->endAtom==copies.at(base))require(!s.doc.extensions(ref).findValue("studio.color.override"),"new bond does not inherit unrelated colour");}
    for(const auto& atom:source.atoms().records())if(atom.isAlive())require(s.doc.atoms().alive(atom.id)->position==atom.position,"original reactant coordinates unchanged");
    const auto after=s.doc.digest();require(s.undo().ok()&&s.doc.digest()==before,"colours undo with the whole step");require(s.undo(true).ok()&&s.doc.digest()==after,"redo restores colours exactly");
    s.selected.clear();s.fit();app.screenshot(dir+"/mechanism-colour-E2.png");
    for(const char* extension:{"r62doc","cdxml"}){auto file=dir+"/mechanism-colour-E2."+extension;require(s.save(file,std::string(extension)=="cdxml").ok(),"save coloured step");Session opened;require(opened.open(file).ok(),"reopen coloured step");
        int blueStrokes=0,greenStrokes=0,redGlyphs=0;for(const auto& p:opened.scene.primitives()){if(p.style.stroke.enabled&&p.style.stroke.rgba==blue)++blueStrokes;if(p.style.stroke.enabled&&p.style.stroke.rgba==green)++greenStrokes;if(p.style.fill.enabled&&p.style.fill.rgba==red)++redGlyphs;}
        require(blueStrokes>=12&&greenStrokes>=3&&redGlyphs>=4,"saved reactants, alkene and leaving labels retain rendered colours");}
    Document drawn;std::vector<mechanism::ElectronMove> moves;std::vector<ObjectRef> made;
    require(studio_drawn_step::prepare(source,{},inputs,drawn,moves,made).ok(),"drawn fallback generates coloured E2");
    std::map<ObjectId,ObjectId> fallback;for(auto ref:made)if(ref.kind==ObjectKind::Atom)fallback[studio_mechanism_products::sourceAtom(drawn,ref.id)]=ref.id;
    auto fallbackAxis=drawn.bondBetween(fallback.at(beta),fallback.at(alpha));auto color=drawn.extensions(B(fallbackAxis)).findValue("studio.color.override");require(color&&*color==std::to_string(green),"drawn fallback keeps altered bond colour");
    Document clone;made.clear();require(studio_mechanism_products::cloneStage(source,{A(beta)},clone,made).ok(),"clone stage");
    int coloured=0;for(auto ref:made)if(ref.kind==ObjectKind::Bond&&(clone.extensions(ref).findValue("studio.color")||clone.extensions(ref).findValue("studio.color.override")))++coloured;require(coloured==7,"proton-operation clone retains all coloured bonds");
    std::ofstream(dir+"/mechanism-colour-checks.json")<<"{\"blue_phenyl\":true,\"green_single_to_double\":true,\"red_leaving_group\":true,\"per_character_hydrogen\":true,\"generic_base\":true,\"new_bond_default_colour\":true,\"source_unchanged\":true,\"undo_redo\":true,\"native_cdxml_roundtrip\":true,\"drawn_fallback\":true,\"proton_operation_clone\":true}";
}
