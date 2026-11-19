#pragma once
static void checkProjectionSpacing(App& app,const std::string& dir){
    auto require=[](bool ok,const std::string& why){if(!ok)throw std::runtime_error("projection spacing: "+why);};
    const int kinds[]={0,1,2,1,2},variants[]={2,0,0,1,1},elements[]={17,1,35,9,8,7};
    int checked=0;std::string spacingFailure;std::ofstream measurements(dir+"/projection-spacing.csv");measurements<<"font,size,view,length,tip,gap\n";Session gallery;gallery.doc.setDefaultFont("Arial");gallery.doc.setDefaultBondLength(24);gallery.doc.setDefaultTextSize(10);
    studioio::Page page;page.width=570;page.height=350;page.x=page.y=0;page.store(gallery.doc);
    for(const char* font:{"Arial","Times New Roman"})for(double size:{8.,10.,16.})for(int view=0;view<5;++view){
        std::vector<double> previous;
        for(double bondLength:{24.,48.}){
            Document style;style.setDefaultFont(font);style.setDefaultTextSize(size);style.setDefaultBondLength(bondLength);
            auto source=stereo_projection::make(style,kinds[view],variants[view]);int index=0;std::vector<ObjectId> tips;
            for(const auto& atom:source->doc.atoms().records())if(atom.isAlive()&&source->doc.incidentBonds(atom.id).size()==1){tips.push_back(atom.id);source->doc.setElement(atom.id,elements[index++%6]);}
            source->refresh();auto before=source->doc.digest();
            integration::RingGeometry rings(source->doc);auto labels=integration::molecular::labels(source->doc,source->glyphs,rings);
            for(size_t i=0;i<tips.size();++i){auto id=tips[i];auto atom=source->doc.atoms().alive(id);auto edge=source->doc.bonds().alive(source->doc.incidentBonds(id).front());
                auto data=rings.build(*edge);integration::molecular::clip(data,*edge,labels,1.6);Vec2 end;bool found=false;
                for(const auto& shape:data.shapes)if(shape.points.size()==2){end=shape.points[edge->beginAtom==id?0:1];found=true;break;}
                require(found&&length(end-atom->position)<.02,"label preserves full projection spoke");
                const auto& label=labels.at(id);double distance=1e100;
                // Independently measure contour-to-visible-end distance.
                for(const auto& glyph:label.layout.glyphs)for(const auto& contour:glyph.contours)for(size_t j=0;j<contour.size();++j){auto a=contour[j]+label.layout.anchor,b=contour[(j+1)%contour.size()]+label.layout.anchor,v=b-a;double t=std::max(0.,std::min(1.,dot(end-a,v)/std::max(1e-12,lengthSq(v))));distance=std::min(distance,length(end-a-v*t));}
                require(distance>.5&&distance<1.6+size*.4,"visible ink clearance: "+std::to_string(distance)+" view "+std::to_string(view));
                measurements<<font<<','<<size<<','<<view<<','<<bondLength<<','<<i<<','<<distance<<'\n';
                // O/N hydrides may change their H orientation to avoid nearby
                // labels; compare fixed element symbols outside eclipsed pairs.
                if(bondLength==24)previous.push_back(distance);else if(i<4&&view!=4&&std::abs(distance-previous[i])>=.02&&spacingFailure.empty())spacingFailure="gap changed: "+std::string(font)+" size "+std::to_string(size)+" view "+std::to_string(view)+" tip "+std::to_string(i)+": "+std::to_string(previous[i])+" -> "+std::to_string(distance);
                ++checked;
            }
            if(view==4)for(size_t i=0;i<tips.size();++i)for(size_t j=i+1;j<tips.size();++j){auto delta=source->doc.atoms().alive(tips[j])->position-source->doc.atoms().alive(tips[i])->position;double distance=length(delta);if(distance>bondLength*.45)continue;auto direction=delta*(1/distance);double last=-1e100,first=1e100;
                for(const auto& glyph:labels.at(tips[i]).layout.glyphs)for(const auto& contour:glyph.contours)for(auto p:contour)last=std::max(last,dot(p+labels.at(tips[i]).layout.anchor,direction));
                for(const auto& glyph:labels.at(tips[j]).layout.glyphs)for(const auto& contour:glyph.contours)for(auto p:contour)first=std::min(first,dot(p+labels.at(tips[j]).layout.anchor,direction));
                require(first-last>1.5,"eclipsed Newman labels remain visibly separate");
            }
            require(before==source->doc.digest(),"layout preserves molecular data");
            if(size==10&&bondLength==24&&std::string(font)=="Arial"){
                auto bounds=source->scene.bounds();require(studioio::append(source->doc,gallery.doc,source->all(),Vec2(95+(view%3)*190,85+(view/3)*170)-(bounds.min+bounds.max)*.5,nullptr).ok(),"spacing gallery");
                std::string xml;require(studioio::exportXml(source->doc,&xml).ok(),"spacing export");std::ofstream(dir+"/spacing-view-"+std::to_string(view)+".cdxml")<<xml;
            }
        }
    }
    app.s.cancel();app.s.doc=gallery.doc;app.s.history.clear();app.s.selected.clear();app.s.input.bindDocument(app.s.doc.id());app.s.refresh();app.s.fit();app.invalidate();app.screenshot(dir+"/projection-spacing.png");
    require(spacingFailure.empty(),spacingFailure);
    std::ofstream(dir+"/projection-spacing-checks.json")<<"{\"endpoint_cases\":"<<checked<<",\"fonts\":2,\"font_sizes\":3,\"views\":5,\"bond_lengths\":2,\"stable_gap\":true,\"unchanged_atom_positions\":true}";
}
