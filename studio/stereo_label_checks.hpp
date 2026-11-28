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
    if(std::getenv("MOLECULE_STUDIO_STEREO_DRAG_CHECK")){
        for(auto smiles:{"N[C@@H](C)C(=O)O","CC[C@@H](C)O","C/C=C/C","C/C=C\\C"}){
            build(smiles);execute(stereo);
            auto texts=s.doc.texts().records();auto text=texts.back();
            const ObjectRef caption(ObjectKind::Text,text.id);
            auto raw=s.doc.extensions(caption).findValue("studio.stereo.owner");require(raw!=nullptr,"new label stores an owner");
            const auto owner=namelink::decodeRefs(*raw).at(0);
            const bool ez=fullText(s.doc,text.id)=="(E)"||fullText(s.doc,text.id)=="(Z)";
            require(owner.kind==(ez?ObjectKind::Bond:ObjectKind::Atom),"correct chemical owner type");
            std::vector<ObjectId> hosts;
            if(ez){auto bond=s.doc.bonds().alive(owner.id);require(bond&&bond->order==BondOrder::Double,"owner is the double bond");hosts={bond->beginAtom,bond->endAtom};}
            else{require(s.doc.neighbours(owner.id).size()==3,"owner is the stereocentre");hosts={owner.id};}
            s.selected.clear();s.activateCommand("chooseSelectionTool_Marquee");s.fit();
            auto view=s.view();view.scale*=.72;s.setView(view);
            auto bounds=selection::objectBounds(s.doc,caption);
            Vec2 grab=(bounds.min+bounds.max)*.5;
            // Pick actual glyph ink for fonts with large caption ascent/descent.
            for(const auto& primitive:s.scene.primitives())if(primitive.source==caption&&!primitive.bounds.empty()){
                auto center=(primitive.bounds.min+primitive.bounds.max)*.5;
                if(s.pick(center)==caption){grab=center;break;}
            }
            require(s.pick(grab)==caption,"test hits caption");
            auto pos=[&](Vec2 p){auto v=s.view().toDevice(p);return MAKELPARAM(int(std::lround(v.x)),int(std::lround(v.y)));};
            auto overlay=[&](){render::OverlayScene o;appendElectronMarkerSelection(o);return o.scene().primitives();};
            auto moved=grab+Vec2(23,12);
            SendMessageW(canvas,WM_LBUTTONDOWN,MK_LBUTTON,pos(grab));
            SendMessageW(canvas,WM_MOUSEMOVE,MK_LBUTTON,pos(moved));UpdateWindow(canvas);
            auto marks=overlay();require(marks.size()==2,"one owner highlight with a halo");
            const auto& mark=marks[1];
            require(mark.style.stroke.rgba==0x0078D4FFu&&marks[0].style.stroke.rgba==0xFFFFFFFFu,"blue highlight with a white halo");
            if(ez){
                require(mark.points.size()==2&&marks[0].points.size()==2&&length(mark.points[0]-marks[0].points[0])+length(mark.points[1]-marks[0].points[1])<1e-5,"one bar follows the double bond");
                auto a=s.doc.atoms().alive(hosts[0])->position,b=s.doc.atoms().alive(hosts[1])->position;
                const auto direction=b-a,segment=mark.points[1]-mark.points[0];
                require(length(mark.points[0]+mark.points[1]-a-b)<1e-5&&
                        length(segment-direction*(length(segment)/length(direction)))<1e-5&&
                        length(segment)>=.69*length(direction)&&length(segment)<=length(direction),
                        "bar is centered, aligned and confined to the owner bond");
            }else{
                const auto body=arrows::mechanismAtomBodyBox(s.doc,*s.doc.atoms().alive(hosts[0]));
                require(mark.points.size()==49&&length((mark.points[0]+mark.points[24]-body.min-body.max)*.5)<1e-5,"blue ring follows correct atom");
            }
            auto token=fullText(s.doc,text.id).substr(1,1);
            screenshot(dir+"/drag-"+token+".png",false,false);
            SendMessageW(canvas,WM_LBUTTONUP,0,pos(moved));require(overlay().empty(),"release clears owner feedback");
            require(length(s.doc.texts().alive(text.id)->position-text.position)>3,"caption moves");
            s.selected.clear();
            SendMessageW(canvas,WM_LBUTTONDOWN,MK_LBUTTON,pos(moved));
            SendMessageW(canvas,WM_MOUSEMOVE,MK_LBUTTON,pos(grab));UpdateWindow(canvas);
            require(overlay().size()==2,"second drag retains owner");
            SendMessageW(canvas,WM_KEYDOWN,VK_ESCAPE,0);
            require(overlay().empty(),"escape clears owner feedback");
            require(s.save(dir+"/drag-"+token+".cdxml",true).ok(),"save moved label");
            Session back;require(back.open(dir+"/drag-"+token+".cdxml").ok(),"reopen moved label");
            auto backText=back.doc.texts().records().front();auto value=back.doc.extensions(ObjectRef(ObjectKind::Text,backText.id)).findValue("studio.stereo.owner");
            require(value&&namelink::decodeRefs(*value).front().kind==owner.kind,"reopened label resolves to owner");
            std::vector<ObjectRef> copies;require(studioio::append(s.doc,back.doc,s.all(),Vec2(100,0),&copies).ok(),"copy structure and label");
            for(auto copy:copies)if(copy.kind==ObjectKind::Text){
                auto link=back.doc.extensions(copy).findValue("studio.stereo.owner");require(link!=nullptr,"copied owner retained");
                auto ref=namelink::decodeRefs(*link).front();require(std::find(copies.begin(),copies.end(),ref)!=copies.end(),"copied label follows copied structure");
            }
            s.selected.clear();execute(stereo);require(s.doc.stats().aliveTexts==1,"rerunning after drag replaces label");
        }
        std::ofstream(dir+"/drag-check.json")<<"{\"passed\":4}";
        reset();PostQuitMessage(0);return;
    }
    auto position=size_t(it-chemistryQuick.begin());
    auto image=std::find_if(chemistryQuick.begin(),chemistryQuick.end(),[](const Button& b){return b.cmd=="@textTidy";});
    auto nmr=std::find_if(chemistryQuick.begin(),chemistryQuick.end(),[](const Button& b){return b.cmd=="@nmr";});
    require(image!=chemistryQuick.end()&&nmr!=chemistryQuick.end(),"neighbouring chemistry buttons exist");
    require(position==size_t(image-chemistryQuick.begin())+1&&position+1==size_t(nmr-chemistryQuick.begin()),"button sits after 文字整理 and before 预测核磁");
    for(auto& b:chemistryQuick)require(!b.r.IntersectsWith(stereo.r)||b.cmd=="@stereoLabels","chemistry quick buttons do not overlap");

    // The seven quick buttons keep three per row and never collide with a group.
    require(chemistryQuick.size()==8,"chemistry quick group holds eight buttons");
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
    require(savedText.find("(S)")!=std::string::npos,"reopened labels keep the CIP text");
    std::ofstream(dir+"/stereo-label-checks.json")<<"{\"cases\":"<<checked<<",\"layout_three_per_row\":true,\"button_between_image_and_nmr\":true,\"single_undo\":true,\"replace_own_labels\":true,\"labels_outside_ring\":true,\"cdxml_roundtrip\":true,\"labels\":"<<placed.size()<<"}";
    reset();
    PostQuitMessage(0);
}
