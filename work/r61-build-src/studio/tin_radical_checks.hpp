#pragma once
static void checkTinRadicalAddition(App& app,const std::string& dir){
    auto require=[](bool ok,const char* why){if(!ok)throw std::runtime_error(std::string("tin radical: ")+why);};
    auto& s=app.s;s.cancel();s.doc=Document();s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());
    auto atom=[&](Vec2 p,int z){ObjectId id;require(s.doc.addAtom(p,z,&id).ok(),"fixture atom");return id;};
    auto bond=[&](ObjectId a,ObjectId b,BondOrder order=BondOrder::Single){ObjectId id;require(s.doc.addBond(a,b,&id).ok()&&s.doc.setBondOrder(id,order).ok(),"fixture bond");return id;};
    auto sn=atom(Vec2(25,65),50);for(auto v:{Vec2(-18,-12),Vec2(-17,14),Vec2(20,12)})bond(sn,atom(Vec2(25,65)+v,6));
    auto sem=s.doc.atoms().alive(sn)->semantics;sem.radical=uint8_t(Radical::Doublet);sem.implicitHydrogens=0;s.doc.setAtomSemantics(sn,sem);
    auto sulfur=atom(Vec2(135,90),16),carbon=atom(Vec2(135,112),6),pi=bond(sulfur,carbon,BondOrder::Double);
    bond(carbon,atom(Vec2(116,125),16));bond(carbon,atom(Vec2(154,125),8));s.refresh();
    std::vector<arrows::ArrowParams> inputs;
    for(int i=0;i<2;++i){auto from=i?Vec2(135,101):arrows::mechanismRadicalPoints(s.doc,sn).front();auto p=arrows::resolvePreset(57,from,Vec2(105,25+i),.6);p.mechanism=true;p.electronSource=ObjectRef(i?ObjectKind::Bond:ObjectKind::Atom,i?pi:sn);p.electronTarget=ObjectRef();p.arcSagitta=i?12:-15;ObjectId id;require(arrows::addArrow(s.doc,p,.6,&id).ok(),"two hooks meeting in empty space");inputs.push_back(p);}
    std::vector<mechanism::ElectronMove> moves;auto inferred=mechanism::inferElectronMoves(s.doc,inputs,&moves);if(!inferred.ok())throw std::runtime_error("tin radical inference: "+inferred.message);
    require(moves.size()==2,"infer both electrons");bool sulfurTarget=false;for(auto m:moves)if(m.source==ObjectRef(ObjectKind::Atom,sn))sulfurTarget=m.target==ObjectRef(ObjectKind::Atom,sulfur);require(sulfurTarget,"meeting point selects sulfur end of C=S");
    auto before=s.doc.digest();s.selected=s.all();app.generateMechanismStep();require(s.doc.digest()!=before,"actual generate-next-step command succeeds");
    std::map<ObjectId,ObjectId> map;for(auto ref:s.selected)if(ref.kind==ObjectKind::Atom){auto p=s.doc.extensions(ref).findValue("studio.mechanism.source.atom");if(p)map[ObjectId(std::stoul(*p))]=ref.id;}
    require(map.count(sn)&&map.count(sulfur)&&map.count(carbon),"product source identities survive");
    auto order=[&](ObjectId a,ObjectId b){auto id=s.doc.bondBetween(map.at(a),map.at(b));return id==kInvalidObjectId?0:int(s.doc.bonds().alive(id)->order);};
    require(order(sn,sulfur)==1&&order(sulfur,carbon)==1&&order(sn,carbon)==0,"new Sn-S bond and reduced C-S bond");
    require(s.doc.atoms().alive(map.at(sn))->semantics.radical==0&&s.doc.atoms().alive(map.at(carbon))->semantics.radical==uint8_t(Radical::Doublet),"unpaired electron moves from tin to carbon");
    s.refresh();s.fit();app.updateInfo();app.screenshot(dir+"/tin-thiocarbonyl-addition.png");require(s.save(dir+"/tin-thiocarbonyl-addition.cdxml",true).ok(),"save editable result");require(s.undo().ok()&&s.doc.digest()==before,"reaction generation is one undo step");
}
