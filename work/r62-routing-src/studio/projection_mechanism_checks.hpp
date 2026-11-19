#pragma once
static void checkProjectionMechanisms(App& app,const std::string& dir){
    auto require=[](bool ok,const std::string& why){if(!ok)throw std::runtime_error("projection mechanism: "+why);};
    auto checkContainers=[&](const std::string& path){
        tinyxml2::XMLDocument xml;require(xml.LoadFile(path.c_str())==tinyxml2::XML_SUCCESS,"read edited export");
        std::map<int,const tinyxml2::XMLElement*> atoms;std::vector<std::pair<const tinyxml2::XMLElement*,const tinyxml2::XMLElement*>> bonds;
        std::function<void(const tinyxml2::XMLElement*,const tinyxml2::XMLElement*)> visit=[&](const tinyxml2::XMLElement* parent,const tinyxml2::XMLElement* fragment){
            for(auto e=parent->FirstChildElement();e;e=e->NextSiblingElement()){
                std::string tag=e->Name();if(tag=="n"){require(fragment!=nullptr,"new atom must be in a molecular fragment");atoms[e->IntAttribute("id")]=fragment;}
                else if(tag=="b")bonds.push_back({e,fragment});
                else if(tag=="page"||tag=="group"||tag=="fragment")visit(e,tag=="fragment"?e:fragment);
            }
        };visit(xml.RootElement(),nullptr);
        for(auto edge:bonds)require(edge.second&&atoms.at(edge.first->IntAttribute("B"))==edge.second&&atoms.at(edge.first->IntAttribute("E"))==edge.second,"new bond and both endpoints share a molecular fragment");
    };
    int cases=0;const int kinds[]={0,1,2,1,2},variants[]={2,0,0,1,1};
    Document style;style.setDefaultFont("Arial");style.setDefaultBondLength(24);style.setDefaultTextSize(10);
    auto base=stereo_projection::make(style,1,0);ObjectId axis=kInvalidObjectId;for(const auto& b:base->doc.bonds().records())if(b.isAlive()&&projection_molecule::axis(base->doc,b)){axis=b.id;break;}
    auto ab=*base->doc.bonds().alive(axis);ObjectId a=ab.beginAtom,b=ab.endAtom,h=kInvalidObjectId,br=kInvalidObjectId,cl=kInvalidObjectId,et=kInvalidObjectId;
    std::vector<ObjectId> first,second;for(auto bid:base->doc.incidentBonds(a)){auto e=base->doc.bonds().alive(bid);auto n=e->beginAtom==a?e->endAtom:e->beginAtom;if(n!=b)first.push_back(n);}for(auto bid:base->doc.incidentBonds(b)){auto e=base->doc.bonds().alive(bid);auto n=e->beginAtom==b?e->endAtom:e->beginAtom;if(n!=a)second.push_back(n);}
    h=first[1];cl=first[2];br=second[0];et=second[1];base->doc.setElement(h,1);base->doc.setElement(cl,17);base->doc.setElement(br,35);require(nicknames::apply(base->doc,et,"Et").ok(),"ethyl fixture");
    for(int mirror=0;mirror<2;++mirror)for(int view=0;view<5;++view)for(int mode=0;mode<2;++mode){
        Document source=base->doc;if(mirror)for(auto id:source.incidentBonds(a)){auto bond=source.bonds().alive(id);auto v=projection_molecule::vector(source,*bond,a);std::ostringstream s;s<<v.x<<' '<<v.y<<' '<<-v.z;source.extensions(ObjectRef(ObjectKind::Bond,id)).set(projection_molecule::vectorKey(*bond,a),s.str());}
        require(projection_molecule::convert(source,axis,kinds[view],variants[view]).ok(),"fixture view conversion");
        ObjectId reagent;source.addAtom(source.atoms().alive(a)->position+Vec2(-70,60),mode==0?6:8,&reagent);if(mode==0)require(mechanism::setGenericRole(source,reagent,"base").ok(),"generic base");else{auto sem=source.atoms().alive(reagent)->semantics;sem.charge=-1;sem.implicitHydrogens=1;source.setAtomSemantics(reagent,sem);}
        auto A=[](ObjectId id){return ObjectRef(ObjectKind::Atom,id);};auto B=[](ObjectId id){return ObjectRef(ObjectKind::Bond,id);};
        source.extensions(B(axis)).set("studio.color.override",std::to_string(0x008C46FFu));
        source.extensions(A(br)).set("studio.color",std::to_string(0xDC1E1EFFu));
        source.extensions(A(cl)).set("studio.text.colors",std::to_string(0x0046DCFFu)+" "+std::to_string(0x0046DCFFu));
        auto ch=source.bondBetween(a,h),cx=source.bondBetween(b,br);std::vector<std::pair<ObjectRef,ObjectRef>> routes;
        if(mode==0)routes={{A(reagent),A(h)},{B(ch),B(axis)},{B(cx),A(br)}};else routes={{A(reagent),A(b)},{B(cx),A(br)}};
        std::vector<arrows::ArrowParams> arrows;
        for(auto link:routes){Vec2 from,to;arrows::mechanismAnchorPoint(source,link.first,&from);arrows::mechanismAnchorPoint(source,link.second,&to);auto p=arrows::resolvePreset(49,from,to,.6);p.mechanism=true;p.electronSource=link.first;p.electronTarget=link.second;p.sourceVisualAnchor=link.first.kind==ObjectKind::Atom?2:5;p.targetVisualAnchor=link.second.kind==ObjectKind::Atom?1:5;p.mechanismSide=1;p.arcSagitta=length(to-from)*.85;arrows::routeMechanism(source,p,true);
            if(mode==0&&link.first==A(reagent)){auto points=arrows::routedPoints(p);double lengthSum=0;for(size_t i=1;i<points.size();++i){lengthSum+=length(points[i]-points[i-1]);for(const auto& bond:source.bonds().records())if(bond.isAlive()){auto x=source.atoms().alive(bond.beginAtom)->position,y=source.atoms().alive(bond.endAtom)->position;auto cross=[](Vec2 a,Vec2 b,Vec2 c){auto u=b-a,v=c-a;return u.x*v.y-u.y*v.x;};require(!(cross(points[i-1],points[i],x)*cross(points[i-1],points[i],y)<-1e-8&&cross(x,y,points[i-1])*cross(x,y,points[i])<-1e-8),"base arrow must not cross a substrate bond");}}require(lengthSum<length(p.end-p.begin)*1.8,"base arrow does not loop around substrate");}
            if(mode==0&&link.first==A(reagent)){auto obstacles=arrows::mechanismObstacles(source);for(auto point:arrows::routedPoints(p))for(const auto& obstacle:obstacles)if(obstacle.glyphOwner.valid()&&obstacle.glyphOwner!=p.electronSource&&obstacle.glyphOwner!=p.electronTarget)require(arrows::mechanismObstacleDistance(point,obstacle)>=.55,"base arrow clears other substituent glyphs");}
            ObjectId arrow;require(arrows::addArrow(source,p,source.defaultLineWidth(),&arrow).ok(),"store routed mechanism");arrows.push_back(p);
        }
        Document prepared;std::vector<mechanism::ElectronMove> moves;std::vector<ObjectRef> made;std::vector<ObjectRef> selected;for(const auto& atom:source.atoms().records())if(atom.isAlive())selected.emplace_back(ObjectKind::Atom,atom.id);
        auto e=studio_mechanism::prepareProduct(source,selected,arrows,&prepared,&moves,&made);require(e.ok(),"prepare "+std::to_string(mode)+" view "+std::to_string(view)+": "+e.message);
        auto fresh=[&](ObjectId old){for(auto ref:made)if(ref.kind==ObjectKind::Atom&&studio_mechanism_products::sourceAtom(prepared,ref.id)==old)return ref.id;return kInvalidObjectId;};auto pa=fresh(a),pb=fresh(b);auto productAxis=prepared.bonds().alive(prepared.bondBetween(pa,pb));require(productAxis&&productAxis->order==(mode==0?BondOrder::Double:BondOrder::Single),"correct product axis order");
        auto axisInk=prepared.extensions(B(productAxis->id)).findValue("studio.color.override");require(axisInk&&*axisInk==std::to_string(0x008C46FFu),"projected E2 and SN2 retain reaction bond colour");
        auto brInk=prepared.extensions(A(fresh(br))).findValue("studio.color");require(brInk&&*brInk==std::to_string(0xDC1E1EFFu),"projected leaving group retains colour");
        auto clInk=prepared.extensions(A(fresh(cl))).findValue("studio.text.colors");require(clInk&&clInk->find(std::to_string(0x0046DCFFu))!=std::string::npos,"projected spectator retains glyph colours");
        if(mode==0){require(std::abs(length(prepared.atoms().alive(pa)->position-prepared.atoms().alive(pb)->position)-24)<1,"alkene bond regularized");for(auto center:{pa,pb}){auto partner=center==pa?pb:pa;auto direction=prepared.atoms().alive(partner)->position-prepared.atoms().alive(center)->position;for(auto bid:prepared.incidentBonds(center)){auto bond=prepared.bonds().alive(bid);auto n=bond->beginAtom==center?bond->endAtom:bond->beginAtom;if(n==partner)continue;auto arm=prepared.atoms().alive(n)->position-prepared.atoms().alive(center)->position;require(std::abs(dot(arm,direction)/length(arm)/length(direction)+.5)<.02,"product is trigonal at both alkene ends");}}}
        app.s.cancel();app.s.doc=prepared;app.s.history.clear();app.s.selected.clear();app.s.input.bindDocument(app.s.doc.id());app.s.refresh();app.s.fit();
        auto name="projection-mechanism-"+std::to_string(mirror)+"-"+std::to_string(view)+"-"+std::to_string(mode);require(app.s.save(dir+"/"+name+".cdxml",true).ok(),"mechanism export");if(!mirror&&mode==0)app.screenshot(dir+"/"+name+".png");++cases;
        auto exportComponent=[&](const Document& d,ObjectId root,const std::string& suffix){auto component=projection_molecule::component(d,root);std::set<ObjectId> ids(component.begin(),component.end());std::vector<ObjectRef> refs;for(auto id:component)refs.emplace_back(ObjectKind::Atom,id);for(const auto& bond:d.bonds().records())if(bond.isAlive()&&ids.count(bond.beginAtom)&&ids.count(bond.endAtom))refs.emplace_back(ObjectKind::Bond,bond.id);Document isolated;require(studioio::append(d,isolated,refs,Vec2(),nullptr).ok(),"isolated molecule export");std::string xml;require(studioio::exportXml(isolated,&xml).ok(),"isolated molecule XML");std::ofstream(dir+"/"+name+suffix+".cdxml")<<xml;};
        exportComponent(source,a,"-source");exportComponent(prepared,pa,"-product");
          Session imported;require(imported.open(dir+"/"+name+".cdxml").ok(),"reopen reaction before editing");
          require(studioio::append(prepared,imported.doc,made,Vec2(0,200),nullptr).ok(),"append generated products to imported CDXML");
          auto appended=dir+"/"+name+"-appended.cdxml";require(imported.save(appended,true).ok(),"save appended reaction products");checkContainers(appended);
    }
      {
          auto input=dir+"/imported-fragments.cdxml";
          std::ofstream(input)<<"<CDXML><page id='1'><fragment id='2'><n id='3' p='0 0'/></fragment><fragment id='4'><n id='5' p='48 0'/></fragment></page></CDXML>";
          Session edited;require(edited.open(input).ok(),"open separate imported fragments");std::vector<ObjectId> atoms;for(const auto& a:edited.doc.atoms().records())if(a.isAlive())atoms.push_back(a.id);
          ObjectId added;require(edited.doc.addAtom(Vec2(0,24),6,&added).ok()&&edited.doc.addBond(atoms[0],added,nullptr).ok()&&edited.doc.addBond(atoms[0],atoms[1],nullptr).ok(),"extend and join imported molecules");
          auto output=dir+"/imported-extended-merged.cdxml";require(edited.save(output,true).ok(),"save extended and joined molecules");checkContainers(output);
          Session reopened;require(reopened.open(output).ok()&&reopened.doc.stats().aliveAtoms==3&&reopened.doc.stats().aliveBonds==2,"reopen all new atoms and bonds");
      }
    // Extensions choose the on-disk format even when a caller omits the flag.
    require(app.s.save(dir+"/extension-only.cdxml").ok(),"CDXML extension save");std::string bytes;require(persistence::readFileBytes(dir+"/extension-only.cdxml",&bytes).ok()&&bytes.find("<CDXML")!=std::string::npos,"CDXML file contains XML, not native bytes");
      std::ofstream(dir+"/projection-mechanism-checks.json")<<"{\"cases\":"<<cases<<",\"views\":5,\"parities\":2,\"elimination_and_substitution\":true,\"compact_base_arrow\":true,\"trigonal_product\":true,\"cdxml_extension\":true,\"appended_products_in_fragments\":20,\"extend_and_merge_imported_fragments\":true}";
}
