#pragma once
static void checkExpandedPhotosensitizers(App& app,const std::string& dir){
    auto require=[](bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);};auto& s=app.s;
    const std::set<std::string> categories={"有机光催化剂","吖啶鎓与吡喃鎓光催化剂","吩噻嗪与吩噁嗪光催化剂","芳烃光敏剂","芳香酮光敏剂","染料光敏剂","自由基阴离子中间体"};
    std::vector<std::shared_ptr<Session>> expanded;TemplateLibraryModel model;model.title="完整展开检查";
    for(const auto& entry:reagentEntries())if(categories.count(entry.category)){
        std::string alias=entry.alias;alias=alias.substr(0,alias.find(' '));s.cancel();s.doc=Document();s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());ObjectId caption;s.doc.addText(Vec2(100,100),10,alias,&caption);s.refresh();s.selected={ObjectRef(ObjectKind::Text,caption)};
        app.expandGroups();require(s.doc.stats().aliveAtoms>=3,"full photosensitizer expands: "+alias);const double unit=s.doc.defaultBondLength();
        std::vector<AtomRecord> atoms;std::vector<BondRecord> bonds;for(const auto& a:s.doc.atoms().records())if(a.isAlive())atoms.push_back(a);for(const auto& b:s.doc.bonds().records())if(b.isAlive())bonds.push_back(b);
        for(size_t i=0;i<atoms.size();++i)for(size_t j=0;j<i;++j)if(s.doc.bondBetween(atoms[i].id,atoms[j].id)==kInvalidObjectId)require(length(atoms[i].position-atoms[j].position)>unit*.35,"no superimposed atoms in "+alias);
        for(size_t i=0;i<bonds.size();++i){auto a=s.doc.atoms().alive(bonds[i].beginAtom)->position,b=s.doc.atoms().alive(bonds[i].endAtom)->position;
            for(const auto& atom:atoms)if(atom.id!=bonds[i].beginAtom&&atom.id!=bonds[i].endAtom)require(mechanism::step_detail::pointSegment(atom.position,a,b)>unit*.15,"no atom lies over an unrelated bond: "+alias);
            for(size_t j=0;j<i;++j){const auto& x=bonds[i];const auto& y=bonds[j];if(x.beginAtom==y.beginAtom||x.beginAtom==y.endAtom||x.endAtom==y.beginAtom||x.endAtom==y.endAtom)continue;
                require(!mechanism::step_detail::segmentsCross(a,b,s.doc.atoms().alive(y.beginAtom)->position,s.doc.atoms().alive(y.endAtom)->position),"no crossed skeletal bonds in "+alias);}
        }
        if(alias=="4DPAIPN"){
            require(atoms.size()==62&&bonds.size()==70,"4DPAIPN retains 62 atoms and nine independent rings");auto info=studio::analyze(s.doc,s.all());require(info.valid&&info.plain=="C56H40N6","4DPAIPN molecular formula remains correct");
            auto neighbors=[&](ObjectId id){std::vector<ObjectId> result;for(const auto& b:bonds){if(b.beginAtom==id)result.push_back(b.endAtom);else if(b.endAtom==id)result.push_back(b.beginAtom);}return result;};
            int compactDonors=0;
            for(const auto& n:atoms)if(n.element==7&&neighbors(n.id).size()==3)for(auto id:neighbors(n.id)){
                std::vector<ObjectId> ringNeighbors;for(auto adjacent:neighbors(id))if(adjacent!=n.id)ringNeighbors.push_back(adjacent);
                if(ringNeighbors.size()!=2||neighbors(ringNeighbors[0]).size()!=3||neighbors(ringNeighbors[1]).size()!=3)continue;
                auto position=s.doc.atoms().alive(id)->position;double edge=0.;for(auto adjacent:ringNeighbors)edge+=length(position-s.doc.atoms().alive(adjacent)->position)*.5;
                require(length(n.position-position)<=edge*1.401,"4DPAIPN core--N bonds stay near normal length instead of stretching the entire drawing");++compactDonors;
            }
            require(compactDonors==4,"all four 4DPAIPN donor arms use a compact layout");
        }
        require(s.save(dir+"/expanded-photo-"+alias+".cdxml",true).ok(),"save fully expanded photosensitizer");
        if(alias=="4DPAIPN"||alias=="4CzIPN"||alias=="4CzTPN"||alias=="2CzPN"||alias=="5CzBN"){s.selected.clear();s.fit();app.invalidate();app.screenshot(dir+"/expanded-photo-"+alias+".png");}
        auto source=std::make_shared<Session>();source->doc=s.doc;source->refresh();int index=int(expanded.size());expanded.push_back(source);model.parents.emplace_back(entry.title,alias,alias,entry.category,index,1);model.variants.emplace_back(index,"完整展开",alias);
    }
    require(expanded.size()==38,"all 38 new photochemical templates have full-depiction checks");model.load=[expanded](int index){return expanded.at(size_t(index));};
    for(size_t first=0;first<expanded.size();first+=12){Bitmap sheet(1600,1200,PixelFormat32bppARGB);Graphics g(&sheet);g.SetPageUnit(UnitPixel);g.Clear(white);g.ScaleTransform(2,2);g.SetSmoothingMode(SmoothingModeAntiAlias);
        for(size_t slot=0;slot<12&&first+slot<expanded.size();++slot){int i=int(first+slot);float x=float(slot%4)*200,y=float(slot/4)*200;model.paint(g,i,RectF(x+6,y+6,188,158));require(!model.failures.count(i),"native full-depiction thumbnail");txt(g,model.parents[i].english,RectF(x+4,y+166,192,24),11,ink,true,true);}
        UINT count=0,bytes=0;GetImageEncodersSize(&count,&bytes);std::vector<uint8_t> data(bytes);auto codecs=(ImageCodecInfo*)data.data();GetImageEncoders(count,bytes,codecs);bool saved=false;for(UINT i=0;i<count;++i)if(wcscmp(codecs[i].MimeType,L"image/png")==0){saved=sheet.Save(W(dir+"/expanded-photo-sheet-"+std::to_string(first/12+1)+".png").c_str(),&codecs[i].Clsid,0)==Ok;break;}require(saved,"save full photosensitizer review sheet");
    }
    std::ofstream(dir+"/expanded-photosensitizer-checks.json")<<"{\"expanded_templates\":38,\"no_coincident_atoms\":true,\"no_crossing_bonds\":true,\"no_atoms_on_unrelated_bonds\":true,\"4DPAIPN_nine_rings\":true,\"native_review_sheets\":4}";
}
