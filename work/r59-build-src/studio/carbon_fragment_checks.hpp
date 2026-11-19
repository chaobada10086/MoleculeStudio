#pragma once
static void checkCarbonFragments(App& app,const std::string& dir){
    auto require=[](bool ok,const char* why){if(!ok)throw std::runtime_error(std::string("carbon fragment: ")+why);};
    auto& s=app.s;
    auto glyphs=[&](Session& session,ObjectId id){
        const auto labels=integration::molecular::labels(session.doc,session.glyphs,integration::RingGeometry(session.doc));
        std::vector<uint32_t> result;
        auto found=labels.find(id);
        if(found!=labels.end())for(const auto& glyph:found->second.layout.glyphs)result.push_back(glyph.codepoint);
        return result;
    };
    // Default skeletal carbons need explicit labels once they become isolated.
    // Keep authored H counts, while deriving H for automatic-valence atoms.
    for(int state:{0,1,2,3,4})for(bool explicitH:{false,true}){
        Session fixture;ObjectId id;fixture.doc.addAtom(Vec2(40,40),6,&id);
        auto sem=fixture.doc.atoms().alive(id)->semantics;
        sem.charge=state==1?-1:state==2?1:0;
        sem.radical=state==3?uint8_t(Radical::Doublet):state==4?uint8_t(Radical::Singlet):0;
        const int h=state==0?4:state==4?2:3;
        sem.implicitHydrogens=explicitH?h:-1;fixture.doc.setAtomSemantics(id,sem);fixture.refresh();
        auto printed=glyphs(fixture,id);
        std::vector<uint32_t> expected={'C','H',uint32_t('0'+h)};
        if(sem.charge)expected.push_back(sem.charge<0?0x2212:'+');
        require(printed==expected,"isolated carbon displays its hydrogens and one charge");
        {
            const auto labels=integration::molecular::labels(fixture.doc,fixture.glyphs,integration::RingGeometry(fixture.doc));
            const auto& layout=labels.at(id).layout;
            require(layout.glyphs[2].y>layout.glyphs[1].y,"the hydrogen count is subscript");
            if(sem.charge)require(layout.glyphs.back().y<layout.glyphs[1].y,"formal charge is superscript");
        }
        require(!arrows::drawChargeMarker(fixture.doc,*fixture.doc.atoms().alive(id)),"inline charge is not also drawn as a separate marker");
        if(sem.radical){
            const auto& box=arrows::mechanismVisualBoxes(fixture.doc).at(id);
            require(box.radicalPoints.size()==size_t(state==4?2:1),"isolated radical retains visible electron dots");
            for(auto point:box.radicalPoints)require(length(point-fixture.doc.atoms().alive(id)->position)<25,"radical dots stay beside their carbon label");
        }
        if(sem.charge){
            fixture.doc.extensions(ObjectRef(ObjectKind::Atom,id)).set(integration::molecular::electronMarkerKey(false),"0.8 -0.8");
            fixture.refresh();printed=glyphs(fixture,id);expected.pop_back();
            require(printed==expected&&arrows::drawChargeMarker(fixture.doc,*fixture.doc.atoms().alive(id)),"positioned charge keeps the full carbon label and one movable marker");
        }
        ObjectId other,bond;fixture.doc.addAtom(Vec2(70,40),6,&other);fixture.doc.addBond(id,other,&bond);
        require(glyphs(fixture,id).empty(),"connected skeletal carbon stays abbreviated");
    }
    s.cancel();s.doc=Document();s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());
    s.doc.setDefaultBondLength(30);
    std::vector<ObjectId> atoms;
    for(auto item:std::vector<std::pair<Vec2,int>>{{Vec2(60,70),6},{Vec2(60,40),8},{Vec2(34,85),6},{Vec2(86,85),6}}){
        ObjectId id;s.doc.addAtom(item.first,item.second,&id);atoms.push_back(id);
    }
    ObjectId carbonyl,left,right;s.doc.addBond(atoms[0],atoms[1],&carbonyl);s.doc.setBondOrder(carbonyl,BondOrder::Double);
    s.doc.addBond(atoms[0],atoms[2],&left);s.doc.addBond(atoms[0],atoms[3],&right);
    auto arrow=arrows::resolvePreset(49,Vec2(73,77.5),Vec2(86,85),.6);
    arrow.mechanism=true;arrow.electronSource=ObjectRef(ObjectKind::Bond,right);arrow.electronTarget=ObjectRef(ObjectKind::Atom,atoms[3]);
    arrows::routeMechanism(s.doc,arrow);arrows::clearMechanismLabels(s.doc,arrow);
    require(arrows::addArrow(s.doc,arrow,.6,nullptr).ok(),"add the user's C-C cleavage arrow");
    s.refresh();s.selected=s.all();const auto before=s.doc.digest();app.generateMechanismStep();
    require(s.doc.stats().aliveAtoms==8,"next step preserves all acetone atoms");
    std::map<ObjectId,ObjectId> product;
    for(auto ref:s.selected)if(ref.kind==ObjectKind::Atom)product[studio_mechanism_products::sourceAtom(s.doc,ref.id)]=ref.id;
    require(product.size()==4,"all four atoms map into the product");
    auto methyl=product.at(atoms[3]),acyl=product.at(atoms[0]);
    require(s.doc.incidentBonds(methyl).empty()&&s.doc.atoms().alive(methyl)->semantics.charge==-1&&
            chemistry::refreshAtomDerived(s.doc,methyl,{}).newHydrogenCount==3,"detached fragment is CH3 minus");
    require(s.doc.atoms().alive(acyl)->semantics.charge==1&&functional_groups::bond(s.doc,acyl,product.at(atoms[1]))->order==BondOrder::Double,
            "the acylium retains its carbonyl and positive charge");
    const std::vector<uint32_t> expected={'C','H','3',0x2212};
    require(glyphs(s,methyl)==expected,"generated methyl anion is visibly CH3 minus");
    const auto after=s.doc.digest();
    require(s.undo().ok()&&s.doc.digest()==before&&s.undo(true).ok()&&s.doc.digest()==after,"cleavage undo and redo preserve all fragments");
    for(bool xml:{false,true}){
        auto path=dir+"/carbon-cleavage"+(xml?".cdxml":".r62doc");
        require(s.save(path,xml).ok(),"save the cleavage drawing");Session reopened;
        require(reopened.open(path).ok(),"reopen the cleavage drawing");
        int visible=0;for(const auto& a:reopened.doc.atoms().records())if(a.isAlive()&&a.element==6&&a.semantics.charge==-1&&reopened.doc.incidentBonds(a.id).empty()){
            require(glyphs(reopened,a.id)==expected,"reopened detached methyl retains its complete label");++visible;
        }
        require(visible==1,"saved drawing retains exactly one methyl anion");
    }
    s.selected.clear();s.fit();app.invalidate();app.screenshot(dir+"/carbon-cleavage.png");
}
