#pragma once
static void checkHalogenSubstitution(App& app,const std::string& dir){
    auto require=[](bool ok,const std::string& why){if(!ok)throw std::runtime_error("halogen substitution: "+why);};auto& s=app.s;
    {
        Document aryl;ObjectId ring[6],iodine,ci;
        for(int i=0;i<6;++i){
            const double angle=i*3.141592653589793/3.;
            aryl.addAtom(Vec2(14.4*std::cos(angle),14.4*std::sin(angle)),6,&ring[i]);
        }
        for(int i=0;i<6;++i){
            ObjectId bond;aryl.addBond(ring[i],ring[(i+1)%6],&bond);
            if(i%2==0){
                aryl.setBondOrder(bond,BondOrder::Double);
                aryl.setBondSemantics(bond,BondSemantics::Aromatic);
            }
        }
        aryl.addAtom(Vec2(28.8,0),53,&iodine);aryl.addBond(ring[0],iodine,&ci);
        require(chemistry::refreshAtomDerived(aryl,ring[0],{}).newHydrogenCount==0,"ipso carbon has no implicit H");
        require(chemistry::refreshAtomDerived(aryl,ring[1],{}).newHydrogenCount==1,"unsubstituted ring carbon retains one H");
        auto arrow=arrows::resolvePreset(49,Vec2(21.6,0),Vec2(28.8,0),.6);
        arrow.mechanism=true;arrow.electronSource=ObjectRef(ObjectKind::Bond,ci);arrow.electronTarget=ObjectRef(ObjectKind::Atom,iodine);
        Document product;std::vector<ObjectRef> made;std::vector<mechanism::ElectronMove> moves;
        require(studio_drawn_step::prepare(aryl,{}, {arrow},product,moves,made).ok(),"aryl iodide heterolysis with native aromatic flags");
        std::map<ObjectId,ObjectId> copies;
        for(auto ref:made)if(ref.kind==ObjectKind::Atom)copies[studio_mechanism_products::sourceAtom(product,ref.id)]=ref.id;
        auto c=copies.at(ring[0]),i=copies.at(iodine);
        require(product.atoms().alive(c)->semantics.charge==1&&product.atoms().alive(c)->semantics.implicitHydrogens==0&&
                product.atoms().alive(i)->semantics.charge==-1&&product.incidentBonds(i).empty(),"phenyl cation and detached iodide");
    }
    for(int halogen:{9,17,35,53})for(const auto& label:std::vector<std::string>{"CH3","H3C"}){
        s.cancel();s.doc=Document();s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());ObjectId methyl,x,sulfur,residue,cx;
        s.doc.addAtom(Vec2(60,50),6,&methyl);s.doc.addAtom(Vec2(80,50),halogen,&x);s.doc.addAtom(Vec2(125,90),16,&sulfur);s.doc.addAtom(Vec2(150,90),6,&residue);s.doc.addBond(methyl,x,&cx);s.doc.addBond(sulfur,residue,nullptr);ObjectId ethyl;s.doc.addAtom(Vec2(170,100),6,&ethyl);s.doc.addBond(residue,ethyl,nullptr);
        auto sem=s.doc.atoms().alive(methyl)->semantics;sem.nodeType=uint8_t(NodeType::Nickname);sem.implicitHydrogens=0;sem.showLabel=true;s.doc.setAtomLabel(methyl,label);s.doc.setAtomSemantics(methyl,sem);sem=s.doc.atoms().alive(sulfur)->semantics;sem.charge=-1;sem.implicitHydrogens=0;s.doc.setAtomSemantics(sulfur,sem);
        std::vector<arrows::ArrowParams> inputs;std::vector<ObjectId> ids;
        for(auto move:std::vector<mechanism::ElectronMove>{{ObjectRef(ObjectKind::Atom,sulfur),ObjectRef(ObjectKind::Atom,methyl)},{ObjectRef(ObjectKind::Bond,cx),ObjectRef(ObjectKind::Atom,x)}}){Vec2 a,z;arrows::mechanismAnchorPoint(s.doc,move.source,&a);arrows::mechanismAnchorPoint(s.doc,move.target,&z);auto p=arrows::resolvePreset(49,a,z,.6);p.mechanism=true;p.electronSource=move.source;p.electronTarget=move.target;p.sourceVisualAnchor=move.source.kind==ObjectKind::Bond?5:1;p.targetVisualAnchor=1;arrows::routeMechanism(s.doc,p);arrows::clearMechanismLabels(s.doc,p);ObjectId id;arrows::addArrow(s.doc,p,.6,&id);inputs.push_back(p);ids.push_back(id);}
        auto before=s.doc.digest();Document prepared;std::vector<mechanism::ElectronMove> moves;std::vector<ObjectRef> made;
        auto e=studio_mechanism::prepareProduct(s.doc,{},inputs,&prepared,&moves,&made);require(e.ok(),"explicit carbon hydride uses full valence solver: "+e.message);std::map<ObjectId,ObjectId> copied;for(auto ref:made)if(ref.kind==ObjectKind::Atom)copied[studio_mechanism_products::sourceAtom(prepared,ref.id)]=ref.id;
        require(prepared.bondBetween(copied[methyl],copied[x])==kInvalidObjectId&&prepared.bondBetween(copied[methyl],copied[sulfur])!=kInvalidObjectId,"forms C--S and cleaves C--X");require(prepared.atoms().alive(copied[x])->semantics.charge==-1&&prepared.atoms().alive(copied[methyl])->semantics.implicitHydrogens==3&&!prepared.atoms().alive(copied[methyl])->semantics.charge,"neutral methyl product and separate halide");require(s.doc.digest()==before,"solver preserves source");
        e=studio_mechanism_products::finish(prepared,s.doc,made,0);require(e.ok(),"finish leaving-ion layout");
        arrows::ArrowParams reaction;bool reactionFound=false;for(auto ref:made)if(ref.kind==ObjectKind::Arrow){bool valid=false;arrows::readArrow(prepared,ref.id,&reaction,&valid);reactionFound=valid;}
        require(reactionFound,"generated reaction arrow");auto ion=prepared.atoms().alive(copied[x]);auto middle=(reaction.begin+reaction.end)*.5;
        require(ion&&ion->semantics.charge==-1&&ion->position.y<middle.y-prepared.defaultTextSize()*.8&&ion->position.x>std::min(reaction.begin.x,reaction.end.x)&&ion->position.x<std::max(reaction.begin.x,reaction.end.x),"detached halide placed above reaction arrow");
        int lossSigns=0;for(auto ref:made)if(ref.kind==ObjectKind::Text&&prepared.plainText(ref.id)=="\xE2\x88\x92")++lossSigns;require(lossSigns==1,"detached halide has one baseline loss minus in addition to its ionic charge");
        require(prepared.atoms().alive(copied[methyl])->position.x>reaction.end.x,"main product stays to the right");require(s.doc.digest()==before,"leaving-ion layout preserves source");
        auto onlyAttack=std::vector<arrows::ArrowParams>{inputs[0]};auto onlyId=std::vector<ObjectId>{ids[0]};require(studio_mechanism::includeDrawnHalogenDepartures(s.doc,onlyAttack,onlyId)==1&&onlyId.back()==ids[1],"includes only the unique already drawn departure");require(studio_mechanism::includeDrawnHalogenDepartures(s.doc,onlyAttack,onlyId)==0,"never duplicates a selected departure");
        s.doc.removeObject(ObjectRef(ObjectKind::Arrow,ids[1]));onlyAttack.resize(1);onlyId.resize(1);require(studio_mechanism::includeDrawnHalogenDepartures(s.doc,onlyAttack,onlyId)==0,"never invents an absent departure");e=studio_drawn_step::prepare(s.doc,{},onlyAttack,prepared,moves,made);require(!e.ok(),"incomplete methyl attack cannot retain C--X as an overvalent carbon");
    }
    auto fixture=dir+"/user-carbon-iodine-input.r62doc";
    if(GetFileAttributesW(W(fixture).c_str())!=INVALID_FILE_ATTRIBUTES){
        for(bool omitted:{false,true}){s.cancel();s.history.clear();require(s.open(fixture).ok(),"open actual user C--I reaction");auto count=s.doc.stats().aliveAtoms;auto before=s.doc.digest();s.selected={ObjectRef(ObjectKind::Arrow,7)};if(!omitted)s.selected.emplace_back(ObjectKind::Arrow,8);app.generateMechanismStep();require(s.doc.stats().aliveAtoms==count+21,"actual user reaction creates all twenty-one atoms with or without short-arrow marquee omission");std::map<ObjectId,ObjectId> copied;for(auto ref:s.selected)if(ref.kind==ObjectKind::Atom)copied[studio_mechanism_products::sourceAtom(s.doc,ref.id)]=ref.id;
            require(copied.count(62)&&copied.count(64)&&copied.count(65),"actual reacting atoms retain their source IDs");require(s.doc.bondBetween(copied[64],copied[65])==kInvalidObjectId&&s.doc.bondBetween(copied[62],copied[64])!=kInvalidObjectId&&s.doc.atoms().alive(copied[65])->semantics.charge==-1&&!s.doc.atoms().alive(copied[64])->semantics.charge,"actual generated methylation cleaves C--I and produces iodide");
            for(auto edge:std::vector<std::pair<ObjectId,ObjectId>>{{45,46},{46,47},{47,48},{48,49},{49,50},{51,52},{52,53},{53,54}}){auto original=s.doc.atoms().alive(edge.second)->position-s.doc.atoms().alive(edge.first)->position;auto generated=s.doc.atoms().alive(copied.at(edge.second))->position-s.doc.atoms().alive(copied.at(edge.first))->position;require(length(original-generated)<1e-7,"actual nucleophile fused rings preserve exact source orientation and lengths");}
            if(omitted){auto excerpt=s.selected;std::set<ObjectId> source;for(ObjectId id=45;id<=65;++id){excerpt.emplace_back(ObjectKind::Atom,id);source.insert(id);}for(const auto& b:s.doc.bonds().records())if(b.isAlive()&&source.count(b.beginAtom)&&source.count(b.endAtom))excerpt.emplace_back(ObjectKind::Bond,b.id);for(ObjectId id:{7,8})excerpt.emplace_back(ObjectKind::Arrow,id);Session clean;require(studioio::append(s.doc,clean.doc,excerpt,Vec2(),nullptr).ok()&&clean.save(dir+"/carbon-iodine-corrected.cdxml",true).ok(),"save corrected actual C--I reaction");auto result=s.doc;s.doc=clean.doc;s.selected.clear();s.refresh();s.fit();app.invalidate();app.screenshot(dir+"/carbon-iodine-corrected.png");s.doc=result;s.refresh();}
            require(s.undo().ok()&&s.doc.digest()==before,"actual methylation undoes atomically");
        }
    }
    std::ofstream(dir+"/halogen-substitution-checks.json")<<"{\"halogens\":4,\"hydride_directions\":2,\"drawn_companion_only\":true,\"overvalent_incomplete_rejected\":true}";
}
