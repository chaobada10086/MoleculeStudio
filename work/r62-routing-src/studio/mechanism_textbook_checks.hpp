#pragma once

static void checkA048(App& app,const std::string& dir,std::ostream& log){
    auto& s=app.s;
    s.cancel();s.doc=Document();s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());
    auto& doc=s.doc;
    doc.setDefaultBondLength(22);
    auto require=[](bool ok,const std::string& what){if(!ok)throw std::runtime_error("A048 "+what);};
    const Vec2 points[]={{44,53},{63,42},{82,53},{82,75},{63,86},{44,75},
                         {101,42},{120,53},{120,75},{101,86},{25,42},{82,31},{25,86},{8,107}};
    std::vector<ObjectId> ids;
    for(size_t i=0;i<sizeof(points)/sizeof(points[0]);++i){
        ObjectId id;require(doc.addAtom(points[i],i==12?8:i==13?1:6,&id).ok(),"source atom");
        ids.push_back(id);
    }
    auto atom=[&](int i){return ObjectRef(ObjectKind::Atom,ids.at(i));};
    auto bond=[&](int i,int j){return ObjectRef(ObjectKind::Bond,doc.bondBetween(ids.at(i),ids.at(j)));};
    const int links[][3]={{0,1,2},{1,2,1},{2,3,1},{3,4,2},{4,5,1},{5,0,1},
                          {2,6,1},{6,7,1},{7,8,1},{8,9,1},{9,3,1},{0,10,1},{2,11,1},{5,12,2}};
    for(const auto& link:links){
        ObjectId id;require(doc.addBond(ids[link[0]],ids[link[1]],&id).ok(),"source bond");
        require(doc.setBondOrder(id,BondOrder(link[2])).ok(),"source bond order");
    }
    auto proton=doc.atoms().alive(ids[13])->semantics;proton.charge=1;
    require(doc.setAtomSemantics(ids[13],proton).ok(),"insert proton");
    doc.extensions(atom(12)).set(mechanism::lonePairKey(),"1");
    s.refresh();
    struct ExpectedBond {int first,last,order;};
    struct ExpectedCharge {int atom,charge;};
    auto run=[&](const char* name,const std::vector<mechanism::ElectronMove>& moves,
                 std::initializer_list<ExpectedBond> bonds,std::initializer_list<ExpectedCharge> charges){
        s.selected.clear();
        auto view=s.view();view.scale=2;s.setView(view);
        std::vector<ObjectRef> selection;
        ObjectId misbound=kInvalidObjectId;
        ObjectRef bridgehead;
        for(const auto& move:moves){
            Vec2 start,end;
            require(arrows::mechanismAnchorPoint(doc,move.source,&start)&&
                    arrows::mechanismAnchorPoint(doc,move.target,&end),std::string(name)+" arrow anchor");
            if(move.source==atom(12)){
                const auto& box=arrows::mechanismVisualBoxes(doc).at(ids[12]).lonePairs.front();
                start=(box.min+box.max)*.5;
            }
            require(s.activateCommand("chooseArrowTool_90_CW"),"activate pair arrow");
            require(s.gesture(PointerPhase::Down,start).ok()&&s.gesture(PointerPhase::Move,end).ok()&&
                    s.gesture(PointerPhase::Up,end).ok(),std::string(name)+" draw arrow");
            const auto id=doc.arrows().records().back().id;
            arrows::ArrowParams params;bool read=false;
            arrows::readArrow(doc,id,&params,&read);
            require(read&&params.electronSource==move.source&&params.electronTarget==move.target,
                    std::string(name)+" arrow bound to intended atom or bond");
            const bool stepB=std::string(name)=="A048-B"&&move.source==bond(2,6);
            const bool stepC=std::string(name)=="A048-C"&&move.source==bond(3,9);
            if(stepB||stepC){
                // These saved tips were on the bridgehead but snapped to
                // neighboring bonds. In C that assignment can open the ring
                // while still passing valence checks.
                params.electronTarget=stepB?bond(3,9):bond(3,4);
                params.targetVisualAnchor=5;
                require(arrows::writeArrow(doc,id,params,doc.defaultLineWidth()).ok(),
                        std::string(name)+" reproduce saved neighboring-bond snap");
                misbound=id;bridgehead=move.target;
            }
            selection.emplace_back(ObjectKind::Arrow,id);
        }
        if(std::string(name)=="A048-C"){
            std::vector<arrows::ArrowParams> centered;
            for(auto ref:selection){
                arrows::ArrowParams params;bool read=false;
                require(arrows::readArrow(doc,ref.id,&params,&read).ok()&&read,"C load arrow for midpoint check");
                if(ref.id==misbound)
                    require(arrows::mechanismAnchorPoint(doc,params.electronTarget,&params.end),"C locate the bond midpoint");
                centered.push_back(params);
            }
            require(!studio_mechanism::correctRingMigrationTarget(doc,centered),"C keeps an intentional bond-midpoint target");
        }
        s.selected=selection;
        const auto before=doc.digest();
        app.generateMechanismStep();
        std::vector<ObjectId> products;
        for(auto ref:s.selected)if(ref.kind==ObjectKind::Atom)products.push_back(ref.id);
        std::sort(products.begin(),products.end());
        const bool loss=std::string(name)=="A048-D";
        require(products.size()==ids.size()-(loss?1:0),std::string(name)+" generation: "+app.status);
        if(loss)products.push_back(kInvalidObjectId);
        if(misbound!=kInvalidObjectId){
            arrows::ArrowParams repaired;bool read=false;
            require(arrows::readArrow(doc,misbound,&repaired,&read).ok()&&read&&
                    repaired.electronTarget==bridgehead&&repaired.targetVisualAnchor==1,
                    std::string(name)+" snap corrected to the bridgehead");
        }
        ids=std::move(products);
        for(auto item:bonds){
            const auto* record=doc.bonds().alive(bond(item.first,item.last).id);
            require((record?int(record->order):0)==item.order,std::string(name)+" product connectivity");
        }
        for(auto item:charges)
            require(doc.atoms().alive(ids[item.atom])->semantics.charge==item.charge,std::string(name)+" product charge");
        const auto after=doc.digest();
        require(s.undo().ok()&&doc.digest()==before&&s.undo(true).ok()&&doc.digest()==after,
                std::string(name)+" undo and redo");
        log<<name<<" product and "<<moves.size()<<" native arrows verified"<<std::endl;
    };
    run("A048-A",{{atom(12),atom(13)}},{{12,13,1},{5,12,2}},{{12,1},{13,0}});
    run("A048-B",{{bond(2,6),atom(3)},{bond(3,4),bond(4,5)},{bond(5,12),atom(12)}},
        {{2,6,0},{3,6,1},{3,4,1},{4,5,2},{5,12,1},{12,13,1}},
        {{2,1},{3,0},{6,0},{12,0}});
    run("A048-C",{{bond(0,1),bond(1,2)},{bond(4,5),bond(5,0)},{bond(3,9),atom(4)}},
        {{0,1,1},{1,2,2},{4,5,1},{5,0,2},{3,9,0},{4,9,1},{3,4,1}},
        {{2,0},{3,1},{4,0},{9,0}});
    for(const auto& triplet:std::vector<std::array<int,3>>{{{0,5,10}},{{5,0,12}}}){
        const auto center=doc.atoms().alive(ids[triplet[0]])->position;
        const auto pi=doc.atoms().alive(ids[triplet[1]])->position-center;
        const auto sigma=doc.atoms().alive(ids[triplet[2]])->position-center;
        require(std::abs(dot(pi,sigma)/(length(pi)*length(sigma))+.5)<1e-7,
                "C double-bond carbon substituent is at 120 degrees");
    }
    ObjectId hydrogen;
    const auto carbon=doc.atoms().alive(ids[4])->position;
    auto direction=sprout_layout::resolve(doc,ids[4]);
    auto carbonSemantics=doc.atoms().alive(ids[4])->semantics;
    require(direction.ok&&carbonSemantics.implicitHydrogens==1,"D bridgehead has one removable hydrogen");
    carbonSemantics.implicitHydrogens=0;
    ObjectId chBond;
    require(doc.setAtomSemantics(ids[4],carbonSemantics).ok()&&
            doc.addAtom(carbon+core::direction(direction.directionDeg)*22,1,&hydrogen).ok()&&
            doc.addBond(ids[4],hydrogen,&chBond).ok(),"D draw explicit bridgehead C–H");
    ids.push_back(hydrogen);s.refresh();
    run("A048-D",{{bond(4,14),bond(3,4)}},
        {{0,1,1},{1,2,2},{2,3,1},{3,4,2},{4,5,1},{5,0,2},{4,14,0},{5,12,1}},
        {{3,0},{4,0},{12,0}});
    require(s.save(dir+"/A048.cdxml",true).ok(),"save complete sequence");
}

// Representative steps redrawn from Fukuyama's exercise solutions. Use the
// native pointer gesture, then the same command that the mechanism ribbon runs.
static void checkTextbookMechanisms(App& app,const std::string& dir){
    std::ofstream log(dir+"/mechanism-textbook-progress.txt");
    Session sheet;
    sheet.doc.setDefaultBondLength(22);
    double nextRow=26,width=0;
    int failures=0;
    for(int example=0;example<5;++example){
        const char* names[]={"A002-B","A040-B","A047-B","A064-A","B018-C"};
        const std::string name=names[example];
        auto& s=app.s;
        s.cancel();s.doc=Document();s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());
        auto& doc=s.doc;
        doc.setDefaultBondLength(22);
        auto require=[](Error e){if(!e.ok())throw std::runtime_error(e.message);};
        auto a=[&](double x,double y,int z=6,int charge=0,bool radical=false){
            ObjectId id;require(doc.addAtom(Vec2(x,y),z,&id));
            auto sem=doc.atoms().alive(id)->semantics;
            sem.charge=charge;
            if(radical)sem.radical=uint8_t(Radical::Doublet);
            require(doc.setAtomSemantics(id,sem));
            return ObjectRef(ObjectKind::Atom,id);
        };
        auto b=[&](ObjectRef first,ObjectRef last,int order=1){
            ObjectId id;require(doc.addBond(first.id,last.id,&id));
            require(doc.setBondOrder(id,BondOrder(order)));
            return ObjectRef(ObjectKind::Bond,id);
        };
        auto pair=[&](ObjectRef atom){doc.extensions(atom).set(mechanism::lonePairKey(),"1");};
        auto cycle=[&](const std::vector<ObjectRef>& atoms){
            for(size_t i=0;i<atoms.size();++i)b(atoms[i],atoms[(i+1)%atoms.size()]);
        };
        auto ester=[&](ObjectRef root,Vec2 v,bool ethyl){
            const auto p=doc.atoms().alive(root.id)->position;
            auto c=a(p.x+v.x,p.y+v.y);
            b(root,c);
            auto o=a(p.x+v.x-v.y*.83,p.y+v.y+v.x*.83,8);
            auto alkoxy=a(p.x+v.x+v.y*.83,p.y+v.y-v.x*.83,8);
            b(c,o,2);b(c,alkoxy);
            auto alkyl=a(p.x+2*v.x+v.y*.83,p.y+2*v.y-v.x*.83);
            b(alkoxy,alkyl);
            if(ethyl)b(alkyl,a(p.x+2.5*v.x+1.7*v.y,p.y+2.5*v.y-1.7*v.x));
        };
        struct CheckBond {ObjectRef a,b;int order;};
        std::vector<mechanism::ElectronMove> moves;
        std::vector<CheckBond> expectedBonds;
        std::vector<std::pair<ObjectRef,int>> expectedCharges;
        ObjectRef expectedRadical,hydroxyl,skeletalCharge,nitroNitrogen;
        if(example==0){
            auto ipso=a(65,76),c=a(88,63),o=a(88,40,8,1),oh=a(108,76,8);
            auto h=a(105,26,1),alkoxy=a(99,123,8),eh=a(121,134,1);
            auto ch2=a(81,135),ch3=a(61,123);
            auto p1=a(46,65),p2=a(27,76),p3=a(27,98),p4=a(46,109),p5=a(65,98);
            b(ipso,p1,2);b(p1,p2);b(p2,p3,2);b(p3,p4);b(p4,p5,2);b(p5,ipso);
            b(ipso,c);auto co=b(c,o,2);b(o,h);b(c,oh);b(alkoxy,eh);b(alkoxy,ch2);b(ch2,ch3);
            pair(alkoxy);
            hydroxyl=oh;
            moves={{alkoxy,c},{co,o}};
            expectedBonds={{c,alkoxy,1},{c,o,1},{alkoxy,eh,1}};
            expectedCharges={{o,0},{alkoxy,1}};
        }else if(example==1){
            auto enolate=a(81,115),acyl=a(59,128),ox=a(40,116,8,-1);
            auto alkoxy=a(59,150,8),methyl=a(40,161);
            b(acyl,alkoxy);b(alkoxy,methyl);auto acylO=b(acyl,ox);auto pi=b(acyl,enolate,2);
            ester(enolate,Vec2(-2,-25),false);
            auto ipso=a(152,85),ortho=a(152,63),meta=a(171,52),para=a(190,63),distal=a(190,85),last=a(171,96);
            auto ringPi=b(ipso,ortho,2);b(ortho,meta);b(meta,para,2);b(para,distal);b(distal,last,2);b(last,ipso);
            b(ipso,a(133,96,9));auto nitro=a(132,52,7,1);
            b(ortho,nitro);b(nitro,a(132,30,8),2);b(nitro,a(113,63,8,-1));
            nitroNitrogen=nitro;
            pair(ox);
            moves={{ox,acylO},{pi,ipso},{ringPi,ortho}};
            expectedBonds={{enolate,ipso,1},{acyl,ox,2},{acyl,enolate,1},{ipso,ortho,1}};
            expectedCharges={{ox,0},{ortho,-1}};
        }else if(example==2){
            auto donor=a(65,69),acceptor=a(87,70,6,1),oxygen=a(67,92,8);
            auto migrating=a(51,51);
            cycle({donor,migrating,a(30,59),a(30,80),a(50,88)});
            cycle({acceptor,a(101,53),a(122,60),a(123,82),a(102,89)});
            auto migration=functional_groups::bond(doc,donor.id,migrating.id)->id;
            b(donor,acceptor);auto co=b(donor,oxygen);pair(oxygen);
            hydroxyl=oxygen;skeletalCharge=acceptor;
            moves={{ObjectRef(ObjectKind::Bond,migration),acceptor},{oxygen,co}};
            expectedBonds={{donor,migrating,0},{acceptor,migrating,1},{donor,oxygen,2}};
            expectedCharges={{acceptor,0},{oxygen,1}};
        }else if(example==3){
            auto c0=a(69,42),c1=a(50,53),c2=a(50,75),c3=a(69,86),c4=a(88,75),o=a(88,53,8);
            auto d1=b(c0,c1,2),middle=b(c1,c2);b(c2,c3,2);b(c3,c4);b(c4,o);b(o,c0);
            b(c4,a(109,85,8),2);
            auto y0=a(151,54),y1=a(151,76);auto triple=b(y0,y1,3);
            ester(y0,Vec2(8,-24),false);ester(y1,Vec2(8,24),false);
            moves={{d1,middle},{ObjectRef(ObjectKind::Bond,functional_groups::bond(doc,c2.id,c3.id)->id),y1},{triple,c0}};
            expectedBonds={{c0,y0,1},{c3,y1,1},{c1,c2,2},{y0,y1,2},{c0,c1,1},{c2,c3,1}};
        }else{
            auto c0=a(56,34),c1=a(78,34),c2=a(94,50,6,0,true),c3=a(94,72);
            auto c4=a(78,88),c5=a(56,88),c6=a(40,72),c7=a(40,50);
            b(c0,c1);b(c1,c2);b(c2,c3);b(c3,c4);b(c4,c5);b(c5,c6);auto pi=b(c6,c7,2);b(c7,c0);
            auto r=a(115,80);b(c3,r);b(r,a(115,102,17));b(r,a(133,94,17));b(r,a(135,74,17));
            moves={{c2,c6,1},{pi,c7,1}};
            expectedBonds={{c2,c6,1},{c6,c7,1}};
            expectedRadical=c7;
        }
        s.refresh();
        auto verifyNitroLabel=[&](ObjectId id){
            const auto& glyphs=arrows::mechanismVisualBoxes(doc).at(id).labelGlyphs;
            if(glyphs.size()!=1||chemistry::refreshAtomDerived(doc,id,{}).newHydrogenCount!=0)
                throw std::runtime_error("A040-B nitro nitrogen acquired spurious hydrogen");
        };
        if(nitroNitrogen.valid())verifyNitroLabel(nitroNitrogen.id);
        if(hydroxyl.valid()){
            const auto& glyphs=arrows::mechanismVisualBoxes(doc).at(hydroxyl.id).labelGlyphs;
            if(glyphs.size()!=2)throw std::runtime_error(name+" implicit OH hydrogen is missing from the drawing");
        }
        if(skeletalCharge.valid()){
            const auto& charge=arrows::mechanismVisualBoxes(doc).at(skeletalCharge.id).charge;
            const auto center=(charge.min+charge.max)*.5;
            for(auto id:doc.incidentBonds(skeletalCharge.id)){
                const auto* bond=doc.bonds().alive(id);
                if(mechanism::step_detail::pointSegment(center,doc.atoms().alive(bond->beginAtom)->position,
                   doc.atoms().alive(bond->endAtom)->position)<doc.defaultTextSize()*.35)
                    throw std::runtime_error(name+" automatic charge overlaps a skeletal bond");
            }
        }
        const auto numReactantAtoms=doc.atoms().size();
        int mismatched=0;
        for(const auto& move:moves){
            Vec2 start,end;
            if(!arrows::mechanismAnchorPoint(doc,move.source,&start)||!arrows::mechanismAnchorPoint(doc,move.target,&end))
                throw std::runtime_error("missing textbook arrow anchor");
            if(move.source.kind==ObjectKind::Atom){
                const auto& boxes=arrows::mechanismVisualBoxes(doc);
                const auto it=boxes.find(move.source.id);
                if(it!=boxes.end()){
                    if(move.electronCount==1&&!it->second.radicalPoints.empty()){
                        start=it->second.radicalPoints.front();
                    }else if(!it->second.lonePairs.empty()){
                        const auto& box=it->second.lonePairs.front();start=(box.min+box.max)*.5;
                    }
                }
            }
            s.activateCommand(move.electronCount==1?"chooseArrowTool_MechanismFishhook":"chooseArrowTool_90_CW");
            const auto count=doc.arrows().size();
            s.gesture(PointerPhase::Down,start);
            s.gesture(PointerPhase::Move,end);
            s.gesture(PointerPhase::Up,end);
            arrows::ArrowParams arrow;bool valid=false;
            const auto& records=doc.arrows().records();
            if(records.size()>count)arrows::readArrow(doc,records.back().id,&arrow,&valid);
            if(!valid||arrow.electronSource!=move.source||arrow.electronTarget!=move.target){
                ++mismatched;
                log<<name<<" gesture mismatch "<<(valid?int(arrow.electronSource.kind):-1)<<":"
                   <<arrow.electronSource.id<<" -> "<<(valid?int(arrow.electronTarget.kind):-1)<<":"
                   <<arrow.electronTarget.id<<"; expected "<<int(move.source.kind)<<":"<<move.source.id
                   <<" -> "<<int(move.target.kind)<<":"<<move.target.id<<std::endl;
            }
            if(example==4&&valid&&move.source.kind==ObjectKind::Bond){
                const auto* pi=doc.bonds().alive(move.source.id);
                const auto first=doc.atoms().alive(pi->beginAtom)->position;
                const auto last=doc.atoms().alive(pi->endAtom)->position;
                double bow=0;
                for(auto point:arrows::routedPoints(arrow))
                    bow=std::max(bow,mechanism::step_detail::pointSegment(point,first,last));
                if(bow<doc.defaultTextSize()*.48)throw std::runtime_error("B018-C short fishhook hugs the pi bond");
            }
        }
        if(mismatched){++failures;continue;}
        ObjectId caption;require(doc.addText(Vec2(22,18),12,name,&caption));
        require(s.save(dir+"/"+name+"-input.cdxml",true));
        const auto before=doc.digest();
        s.selected=s.all();app.generateMechanismStep();
        std::vector<ObjectId> made;
        for(auto ref:s.selected)if(ref.kind==ObjectKind::Atom&&ref.id>=numReactantAtoms)made.push_back(ref.id);
        if(made.size()!=numReactantAtoms||doc.atoms().size()!=2*numReactantAtoms){
            log<<name<<" generation failed: "<<app.status<<std::endl;++failures;continue;
        }
        std::sort(made.begin(),made.end());
        if(nitroNitrogen.valid())verifyNitroLabel(made.at(nitroNitrogen.id));
        for(const auto& edge:expectedBonds){
            const auto* record=functional_groups::bond(doc,made.at(edge.a.id),made.at(edge.b.id));
            if((record?int(record->order):0)!=edge.order)throw std::runtime_error(name+" product bond mismatch");
        }
        for(auto charge:expectedCharges)
            if(doc.atoms().alive(made.at(charge.first.id))->semantics.charge!=charge.second)
                throw std::runtime_error(name+" product charge mismatch");
        for(size_t i=0;i<made.size();++i)
            if(bool(doc.atoms().alive(made[i])->semantics.radical)!=(expectedRadical.valid()&&i==expectedRadical.id))
                throw std::runtime_error(name+" product radical mismatch");
        if(example==0){
            const auto oxygen=made.at(hydroxyl.id);
            const auto& glyphs=arrows::mechanismVisualBoxes(doc).at(oxygen).labelGlyphs;
            if(glyphs.size()!=2)throw std::runtime_error("A002-B hydroxyl label is missing in the product");
            const auto& h=glyphs.back();
            for(auto ref:s.selected)if(ref.kind==ObjectKind::Bond){
                const auto* bond=doc.bonds().alive(ref.id);
                if(bond->beginAtom==oxygen||bond->endAtom==oxygen)continue;
                const auto first=doc.atoms().alive(bond->beginAtom)->position;
                const auto last=doc.atoms().alive(bond->endAtom)->position;
                const Vec2 corners[]={h.min,Vec2(h.max.x,h.min.y),h.max,Vec2(h.min.x,h.max.y)};
                for(size_t i=0;i<4;++i)
                    if(mechanism::step_detail::segmentsCross(first,last,corners[i],corners[(i+1)%4]))
                        throw std::runtime_error("A002-B product bond crosses the hydroxyl H glyph");
            }
        }
        const auto after=doc.digest();
        require(s.undo());
        if(doc.digest()!=before)throw std::runtime_error(name+" undo did not restore the source");
        require(s.undo(true));
        if(doc.digest()!=after)throw std::runtime_error(name+" redo did not restore the product");
        log<<name<<" product correct; "<<moves.size()<<" native arrows; undo and redo verified"<<std::endl;
        require(s.save(dir+"/"+name+".cdxml",true));
        const auto objects=s.all();
        const auto bounds=selection::selectionBounds(doc,objects);
        require(studioio::append(doc,sheet.doc,objects,Vec2(24-bounds.min.x,nextRow-bounds.min.y),nullptr));
        nextRow+=bounds.max.y-bounds.min.y+30;
        width=std::max(width,bounds.max.x-bounds.min.x+48);
        s.selected.clear();app.switchTab(6);app.layout();s.fit();app.invalidate();
        app.screenshot(dir+"/"+name+".png");
    }
    if(failures)throw std::runtime_error("textbook mechanism generation failed for "+std::to_string(failures)+" cases");
    checkA048(app,dir,log);
    studioio::Page page;page.x=page.y=0;page.width=std::ceil(width);page.height=std::ceil(nextRow);
    page.store(sheet.doc);
    const auto error=sheet.save(dir+"/textbook-mechanisms.cdxml",true);
    if(!error.ok())throw std::runtime_error(error.message);
}
