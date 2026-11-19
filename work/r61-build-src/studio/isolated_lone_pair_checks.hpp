#pragma once
static void checkIsolatedLonePairs(App& app,const std::string& dir){
    auto require=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};auto& s=app.s;int cases=0;
    for(const char* label:{"H2N","NH2","H₂N","NH₂","HO","OH","HS","SH","MeO","OMe"}){
        s.cancel();s.doc=Document();s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());ObjectId host,other,bond;s.doc.addAtom(Vec2(100,100),6,&host);s.doc.addAtom(Vec2(130,100),6,&other);s.doc.addBond(host,other,&bond);auto sem=s.doc.atoms().alive(host)->semantics;sem.nodeType=uint8_t(NodeType::Nickname);s.doc.setAtomSemantics(host,sem);s.doc.setAtomLabel(host,label);s.selected={ObjectRef(ObjectKind::Atom,host)};s.refresh();auto before=s.doc.digest();app.execute(Button("","","","@mechanismLonePair"));require(mechanism::lonePairCount(s.doc,host)==1,"reversed hydride receives a lone pair");integration::RingGeometry rings(s.doc);auto labels=integration::molecular::labels(s.doc,s.glyphs,rings);auto layout=labels.at(host).layout;auto center=mechanism::lonePairGlyphCenter(s.doc,*s.doc.atoms().alive(host),&layout);auto dots=mechanism::lonePairDots(s.doc,*s.doc.atoms().alive(host),0,10,&layout);require(length((dots[0]+dots[1])*.5-center)<11,"lone pair is anchored beside actual heteroatom glyph");for(const auto& g:layout.glyphs)if(g.codepoint=='H')for(const auto& contour:g.contours)for(auto p:contour)require(length(p+layout.anchor-dots[0])>1.1&&length(p+layout.anchor-dots[1])>1.1,"lone pair avoids hydrogen glyph ink");require(s.undo().ok()&&s.doc.digest()==before,"reversed hydride lone pair undo");++cases;
    }
    for(const char* label:{"H2O","H₂O","OH2","NH3","NH₃","H2S","PH3"})for(int form:{0,1,2,3}){
        s.cancel();s.doc=Document();s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());
        auto group=mechanism::lonePairGroup(label);require(group!=nullptr,"resolve simple hydride lone-pair host");ObjectId id;
        if(form==0)s.doc.addText(Vec2(90,90),10,label,&id);
        else {s.doc.addAtom(Vec2(90,90),form==1?group->element:6,&id);auto sem=s.doc.atoms().alive(id)->semantics;sem.nodeType=uint8_t(form==1?NodeType::Element:NodeType::Nickname);sem.implicitHydrogens=form==1?group->hydrogens:-1;s.doc.setAtomSemantics(id,sem);s.doc.setAtomLabel(id,label);}
        s.refresh();if(form==3){ObjectId owner;GroupRecord record;record.members={ObjectRef(ObjectKind::Atom,id)};s.doc.addGroup(record,&owner);s.selected={ObjectRef(ObjectKind::Group,owner)};}
        else s.selected={ObjectRef(form==0?ObjectKind::Text:ObjectKind::Atom,id)};
        const auto before=s.doc.digest();app.execute(Button("","","","@mechanismLonePair"));
        require(s.selected.size()==1&&s.selected.front().kind==ObjectKind::Atom,"isolated hydride becomes a selectable chemical atom");auto host=s.selected.front().id;
        require(s.doc.atoms().alive(host)->element==group->element&&s.doc.atoms().alive(host)->semantics.implicitHydrogens==group->hydrogens&&mechanism::lonePairCount(s.doc,host)==1,"text and legacy hydrides receive one visible lone pair without losing hydrogen");
        require(arrows::mechanismVisualBoxes(s.doc).at(host).lonePairs.size()==1,"isolated lone pair is visibly rendered and capturable");auto marked=s.doc.digest();require(s.undo().ok()&&s.doc.digest()==before&&s.undo(true).ok()&&s.doc.digest()==marked,"isolated hydride annotation undoes and redoes atomically");
        if(group->element==8){app.execute(Button("","","","@mechanismLonePair"));require(mechanism::lonePairCount(s.doc,host)==2,"water accepts its second oxygen lone pair");auto complete=s.doc.digest();app.execute(Button("","","","@mechanismLonePair"));require(s.doc.digest()==complete&&app.status.find("全部显示")!=std::string::npos,"full lone-pair capacity gives clear feedback without an empty command");}
        if(form==0&&std::string(label)=="H2O"){require(s.save(dir+"/isolated-water-lone-pairs.cdxml",true).ok(),"save water lone pairs");Session reopened;require(reopened.open(dir+"/isolated-water-lone-pairs.cdxml").ok(),"reopen water lone pairs");bool found=false;for(const auto& a:reopened.doc.atoms().records())if(a.isAlive()&&a.element==8)found|=mechanism::lonePairCount(reopened.doc,a.id)==2;require(found,"water lone pairs survive save and reopen");s.selected.clear();s.fit();app.invalidate();app.screenshot(dir+"/isolated-water-lone-pairs.png");}
        ++cases;
    }
    for(bool explicitWater:{false,true})for(int form:{0,1,2}){
        s.cancel();s.doc=Document();s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());ObjectId o,a,b,oa,ab;
        s.doc.addAtom(Vec2(90,90),8,&o);s.doc.addAtom(Vec2(60,90),explicitWater?1:6,&a);s.doc.addAtom(Vec2(60,120),explicitWater?1:6,&b);
        s.doc.addBond(o,a,&oa);s.doc.addBond(explicitWater?o:a,b,&ab);s.refresh();
        if(form==0)s.selected=s.all();else if(form==1)s.selected={ObjectRef(ObjectKind::Bond,oa),ObjectRef(ObjectKind::Bond,ab)};else{ObjectId owner;GroupRecord record;record.members=s.all();s.doc.addGroup(record,&owner);s.selected={ObjectRef(ObjectKind::Group,owner)};}
        const auto before=s.doc.digest();app.execute(Button("","","","@mechanismLonePair"));
        require(mechanism::lonePairCount(s.doc,o)==1&&mechanism::lonePairCount(s.doc,a)==0&&mechanism::lonePairCount(s.doc,b)==0,"whole water/alcohol and bond/group selections annotate only the eligible oxygen");require(s.undo().ok()&&s.doc.digest()==before,"whole-molecule lone-pair annotation is one exact undo");++cases;
    }
    std::ofstream(dir+"/isolated-lone-pair-checks.json")<<"{\"cases\":"<<cases<<",\"water_text\":true,\"unicode\":true,\"legacy_nodes\":true,\"groups_and_bonds\":true,\"whole_alcohol\":true,\"explicit_water\":true,\"hydrogens_preserved\":true,\"visible_pairs\":true,\"undo_redo\":true,\"save_reopen\":true}";
}
