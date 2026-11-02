#pragma once
// 构型标注 regression: RDKit CIP labels are inserted as ordinary text objects
// beside each stereocentre / E-Z double bond, in ONE undoable command.
//
// The check drives the real chemistry quick button, so it covers the button,
// the local RDKit service, the JSON-ish reply, the transaction and the
// save/reopen path - not just the backend.
void App::stereoLabelCheck(const std::string& dir){
    auto require=[](bool ok,const std::string& message){if(!ok)throw std::runtime_error(message);};
    SetWindowPos(h,0,0,0,1500,950,SWP_NOMOVE|SWP_NOZORDER);scale=1;layout();
    auto reset=[&](){s.cancel();s.doc=Document();s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());s.refresh();};
    auto build=[&](const std::string& smiles){
        std::string xml;if(!chemservice::run(h,smiles,"chemical/x-daylight-smiles","text/xml","convert",&xml))throw std::runtime_error("structure from SMILES failed: "+xml);
        reset();auto e=persistence::readCdxml(xml,s.doc);if(!e.ok())throw std::runtime_error(std::string("CDXML import: ")+e.message);
        s.refresh();s.selected=s.all();
    };
    auto textSize=[&](){return s.doc.defaultTextSize()>0?s.doc.defaultTextSize():10.0;};
    auto labels=[&](){std::vector<std::pair<std::string,Vec2>> out;double size=textSize();
        for(auto& t:s.doc.texts().records())if(t.isAlive()){std::string plain=fullText(s.doc,t.id);double w=size*.60*double(plain.size()),h=size*1.05;out.push_back({plain,Vec2(t.position.x+w*.5,t.position.y+h*.5)});}
        return out;};
    auto minAtomDistance=[&](Vec2 p){double best=1e9;for(auto& a:s.doc.atoms().records())if(a.isAlive())best=std::min(best,core::length(a.position-p));return best;};
    auto minBondDistance=[&](Vec2 p){double best=1e9;for(auto& b:s.doc.bonds().records())if(b.isAlive()){Vec2 a=s.doc.atoms().get(b.beginAtom)->position,c=s.doc.atoms().get(b.endAtom)->position;Vec2 d=c-a;double l2=d.x*d.x+d.y*d.y;double t=l2>1e-9?std::max(0.0,std::min(1.0,((p.x-a.x)*d.x+(p.y-a.y)*d.y)/l2)):0.0;best=std::min(best,core::length(p-(a+d*t)));}return best;};
    // Ring atoms = atoms with at least two heavy neighbours (terminal methyls
    // are excluded). Good enough to prove the label sits OUTSIDE the ring.
    auto ringGeometry=[&](Vec2* center,double* radius){Vec2 sum;int n=0;for(auto& a:s.doc.atoms().records())if(a.isAlive()&&s.doc.neighbours(a.id).size()>=2){sum=sum+a.position;++n;}
        require(n>=3,"ring atoms found");*center=sum*(1.0/double(n));*radius=0;for(auto& a:s.doc.atoms().records())if(a.isAlive()&&s.doc.neighbours(a.id).size()>=2)*radius=std::max(*radius,core::length(a.position-*center));};

    auto it=std::find_if(chemistryQuick.begin(),chemistryQuick.end(),[](const Button& b){return b.cmd=="@stereoLabels";});
    require(it!=chemistryQuick.end(),"构型标注 button exists in the chemistry quick group");
    Button stereo=*it;
    require(stereo.label=="构型标注","button label is 构型标注");
    auto position=size_t(it-chemistryQuick.begin());
    auto image=std::find_if(chemistryQuick.begin(),chemistryQuick.end(),[](const Button& b){return b.cmd=="@imageToStructure";});
    auto nmr=std::find_if(chemistryQuick.begin(),chemistryQuick.end(),[](const Button& b){return b.cmd=="@nmr";});
    require(image!=chemistryQuick.end()&&nmr!=chemistryQuick.end(),"neighbouring chemistry buttons exist");
    require(position==size_t(image-chemistryQuick.begin())+1&&position+1==size_t(nmr-chemistryQuick.begin()),"button sits after 图片转结构 and before 预测核磁");
    for(auto& b:chemistryQuick)require(!b.r.IntersectsWith(stereo.r)||b.cmd=="@stereoLabels","chemistry quick buttons do not overlap");

    // The seven quick buttons keep three per row and never collide with a group.
    require(chemistryQuick.size()==7,"chemistry quick group holds seven buttons");
    auto tidy=std::find_if(chemistryQuick.begin(),chemistryQuick.end(),[](const Button& b){return b.cmd=="@textTidy";});
    require(tidy!=chemistryQuick.end()&&tidy->label=="文字整理","文字整理 button sits in the chemistry quick group");
    for(int w:{800,1100,1500}){
        SetWindowPos(h,0,0,0,w,900,SWP_NOMOVE|SWP_NOZORDER);layout();
        for(size_t i=0;i<chemistryQuick.size();++i){
            const Button& b=chemistryQuick[i];
            require(b.r.GetRight()*scale<=width&&b.r.GetBottom()*scale<top,std::string("chemistry quick button inside the toolbar at ")+std::to_string(w)+"px id="+b.id+" right="+std::to_string(b.r.GetRight()*scale)+" bottom="+std::to_string(b.r.GetBottom()*scale)+" top="+std::to_string(top)+" width="+std::to_string(width)+" scale="+std::to_string(scale));
            for(const auto& group:groups)require(!b.r.IntersectsWith(group.r),std::string("chemistry quick button clears tool groups at ")+std::to_string(w)+"px");
            for(size_t j=i+1;j<chemistryQuick.size();++j)require(!b.r.IntersectsWith(chemistryQuick[j].r),"chemistry quick buttons never overlap");
            if(i>=3)require(std::abs(b.r.X-chemistryQuick[i%3].r.X)<.01&&std::abs((b.r.Y-chemistryQuick[i%3].r.Y)-30*float(i/3))<1,"chemistry quick layout stays three per row");
        }
    }
    SetWindowPos(h,0,0,0,1500,950,SWP_NOMOVE|SWP_NOZORDER);layout();

    struct Case{const char* smiles;const char* name;std::vector<std::string> expect;bool ring;};
    std::vector<Case> cases={
        {"N[C@@H](C)C(=O)O","L-丙氨酸 (S)",{"(S)"},false},
        {"CC[C@@H](C)O","(R)-2-丁醇 (R)",{"(R)"},false},
        {"C/C=C/C","(E)-2-丁烯 (E)",{"(E)"},false},
        {"C/C=C\\C","(Z)-2-丁烯 (Z)",{"(Z)"},false},
        {"C[C@@H](O)[C@H](C)O","内消旋-2,3-丁二醇 (R,S)",{"(R)","(S)"},false},
        {"C[C@H]1CCCC[C@@H]1O","2-甲基环己醇 (S,S，环上)",{"(S)","(S)"},true}
    };
    std::ofstream table(dir+"/stereo-labels.txt");
    table<<"molecule\texpected\tactual\tmin_atom_gap\tmin_bond_gap\tverdict\n";
    int checked=0;
    bool undoOk=true,replaceOk=true,outsideOk=true;
    for(auto& c:cases){
        build(c.smiles);
        require(s.doc.stats().aliveTexts==0,"fresh structure has no labels");
        size_t atomsBefore=s.doc.stats().aliveAtoms,bondsBefore=s.doc.stats().aliveBonds;
        execute(stereo);
        auto placed=labels();
        std::vector<std::string> actual;for(auto& l:placed)actual.push_back(l.first);
        std::vector<std::string> want=c.expect,have=actual;std::sort(want.begin(),want.end());std::sort(have.begin(),have.end());
        require(have==want,std::string("labels for ")+c.name+" are exactly the expected CIP labels (got "+std::to_string(actual.size())+")");
        require(s.doc.stats().aliveAtoms==atomsBefore&&s.doc.stats().aliveBonds==bondsBefore,"annotation never changes the structure");
        double atomGap=1e9,bondGap=1e9;
        for(auto& l:placed){atomGap=std::min(atomGap,minAtomDistance(l.second));bondGap=std::min(bondGap,minBondDistance(l.second));}
        for(size_t i=0;i<placed.size();++i)for(size_t j=i+1;j<placed.size();++j)require(core::length(placed[i].second-placed[j].second)>12,"labels do not overlap each other");
        require(atomGap>10.0,std::string("label clears every atom: ")+c.name);
        require(bondGap>7.0,std::string("label clears every bond: ")+c.name);
        if(c.ring){Vec2 center;double radius;ringGeometry(&center,&radius);for(auto& l:placed){double d=core::length(l.second-center);if(!(d>radius))outsideOk=false;}require(outsideOk,std::string("ring labels sit outside the ring: ")+c.name);}
        std::string expected;for(auto& e:c.expect)expected+=(expected.empty()?"":"+")+e;
        std::string got;for(auto& a:actual)got+=(got.empty()?"":"+")+a;
        table<<c.name<<"\t"<<expected<<"\t"<<got<<"\t"<<atomGap<<"\t"<<bondGap<<"\t"<<(have==want?"PASS":"FAIL")<<"\n";
        // One Ctrl+Z removes the whole annotation.
        if(!(s.undo().ok()&&s.doc.stats().aliveTexts==0))undoOk=false;
        execute(stereo);execute(stereo);
        if(s.doc.stats().aliveTexts!=placed.size())replaceOk=false;
        ++checked;
    }
    require(undoOk,"a single undo removes every label");
    require(replaceOk,"re-running replaces its own labels instead of stacking them");
    // Screenshot + save/reopen round trip on the ring case.
    build("C[C@H]1CCCC[C@@H]1O");
    execute(stereo);
    auto placed=labels();require(placed.size()==2,"ring case has two labels for the screenshot");
    s.selected.clear();s.fit();invalidate();UpdateWindow(h);UpdateWindow(canvas);
    screenshot(dir+"/stereo-labels.png",false,false);
    auto saveError=s.save(dir+"/stereo-labels.cdxml",true);require(saveError.ok(),std::string("save annotated cdxml: ")+saveError.message);
    Session reopened;auto openError=reopened.open(dir+"/stereo-labels.cdxml");require(openError.ok(),std::string("reopen annotated cdxml: ")+openError.message);
    size_t reopenedLabels=0;for(auto& t:reopened.doc.texts().records())if(t.isAlive())++reopenedLabels;
    require(reopenedLabels==placed.size(),"labels persist in the saved CDXML");
    std::string savedText;for(auto& t:reopened.doc.texts().records())if(t.isAlive())savedText+=fullText(reopened.doc,t.id);
    require(savedText.find('(S)')!=std::string::npos,"reopened labels keep the CIP text");
    std::ofstream(dir+"/stereo-label-checks.json")<<"{\"cases\":"<<checked<<",\"layout_three_per_row\":true,\"button_between_image_and_nmr\":true,\"single_undo\":true,\"replace_own_labels\":true,\"labels_outside_ring\":true,\"cdxml_roundtrip\":true,\"labels\":"<<placed.size()<<"}";
    reset();
    PostQuitMessage(0);
}
