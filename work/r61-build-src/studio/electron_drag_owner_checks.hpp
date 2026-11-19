#pragma once
static void checkElectronDragOwner(App& app,const std::string& dir){
    auto& s=app.s;int cases=0;
    auto require=[](bool ok,const char* why){if(!ok)throw std::runtime_error(std::string("electron drag owner: ")+why);};
    struct Fixture{int z,charge,radical,site;const char* label;};
    for(const auto& f:std::vector<Fixture>{
        {6,1,0,2,""},{6,-1,0,2,""},{6,0,int(Radical::Doublet),4,""},
        {6,0,int(Radical::Singlet),4,""},{6,0,int(Radical::Triplet),4,""},
        {8,0,0,3,"HO"},{15,0,0,3,"Ph3P"},{15,0,0,3,"PPh3"},{15,0,0,3,"Ph₃P"}
    })for(double zoom:{1.,5.}){
        app.cancelElectronMarkerDrag();app.selectedElectronMarker={};s.cancel();s.doc=Document();s.history.clear();s.selected.clear();
        s.input.bindDocument(s.doc.id());s.activateCommand("chooseSelectionTool_Marquee");
        ObjectId host,neighbour,tail,bond;
        s.doc.addAtom(Vec2(80,80),f.z,&host);s.doc.addAtom(Vec2(85,84),7,&neighbour);
        if(f.z!=15){
            s.doc.addAtom(Vec2(104,100),6,&tail);s.doc.addBond(host,tail,&bond);
        }
        auto sem=s.doc.atoms().alive(host)->semantics;sem.charge=f.charge;sem.radical=f.radical;sem.implicitHydrogens=f.z==8?1:0;
        s.doc.setAtomSemantics(host,sem);if(*f.label)s.doc.setAtomLabel(host,f.label);
        auto& bag=s.doc.extensions(ObjectRef(ObjectKind::Atom,host));
        if(f.site==3){bag.set(mechanism::lonePairKey(),"1");mechanism::setLonePairOffset(s.doc,host,0,Vec2(-.8,-.8));}
        else bag.set(integration::molecular::electronMarkerKey(f.site==4),"-0.8 -0.8");
        s.refresh();auto view=s.view();view.center=Vec2(80,80);view.scale=zoom;view.dpiScale=1;s.setView(view);
        const auto& visual=arrows::mechanismVisualBoxes(s.doc).at(host);
        if(f.z==15){
            std::unique_ptr<text::GlyphProvider> provider(text::createGdiGlyphProvider("Arial","Microsoft YaHei"));text::GlyphCache glyphs(provider.get());
            integration::RingGeometry rings(s.doc);const auto labels=integration::molecular::labels(s.doc,glyphs,rings);
            const auto& layout=labels.at(host).layout;
            std::vector<const text::GlyphPlacement*> ordered;
            for(const auto& g:layout.glyphs)ordered.push_back(&g);
            std::sort(ordered.begin(),ordered.end(),[](const text::GlyphPlacement* a,const text::GlyphPlacement* b){return a->x<b->x;});
            const text::GlyphPlacement* phosphorus=nullptr;
            for(size_t i=0;i<ordered.size();++i)if(ordered[i]->codepoint=='P'&&
                (i+1==ordered.size()||ordered[i+1]->codepoint!='h'))phosphorus=ordered[i];
            require(phosphorus!=nullptr&&visual.hasBody,"Ph3P has a distinct phosphorus glyph");
            Box ink;bool found=false;for(const auto& contour:phosphorus->contours)for(auto p:contour){p=p+layout.anchor;if(found)ink.add(p);else{ink=Box(p,p);found=true;}}
            require(found&&length((visual.body.min+visual.body.max)*.5-(ink.min+ink.max)*.5)<1e-6,
                    "condensed phosphorus highlight targets the P outside Ph");
        }
        auto center=f.site==2?(visual.charge.min+visual.charge.max)*.5:f.site==3?(visual.lonePairs[0].min+visual.lonePairs[0].max)*.5:visual.radicalPoints.front();
        auto point=[&](Vec2 p){auto px=s.view().toDevice(p);return MAKELPARAM(int(std::lround(px.x)),int(std::lround(px.y)));};
        auto checkRing=[&](){
            render::OverlayScene overlay;app.appendElectronMarkerSelection(overlay);const auto& p=overlay.scene().primitives();
            require(p.size()==2,"drag displays a capture circle with a white halo");
            require(p[0].style.stroke.rgba==0xFFFFFFFFu&&p[1].style.stroke.rgba==0x0078D4FFu&&
                    !p[1].source.valid()&&p[0].version&&p[1].version&&p[0].version!=p[1].version,
                    "capture uses independent geometry versions instead of stale atom cache entries");
            const auto bounds=arrows::mechanismAtomBodyBox(s.doc,*s.doc.atoms().alive(host));
            const auto owner=(bounds.min+bounds.max)*.5;
            const auto circleCenter=(p[1].points[0]+p[1].points[24])*.5;
            require(length(circleCenter-owner)<1e-6&&length(circleCenter-s.doc.atoms().alive(neighbour)->position)>1,
                    "circle stays on the host glyph or skeletal atom, never the neighbour");
            require(length(p[1].points.front()-circleCenter)>=s.view().deviceRadiusToDocument(14.)-1e-6,
                    "capture stays legible at both zoom levels");
        };
        const auto before=s.doc.digest();const auto history=s.history.cursor();
        SendMessageW(app.canvas,WM_LBUTTONDOWN,MK_LBUTTON,point(center));
        require(app.electronDragAtom==host&&app.electronDragSite==f.site,"press captures the requested marker");checkRing();
        SendMessageW(app.canvas,WM_MOUSEMOVE,MK_LBUTTON,point(Vec2(85,84)));checkRing();
        require(s.doc.digest()==before&&s.history.cursor()==history,"owner feedback makes no document or undo changes");
        if(f.charge==1&&zoom==5.){app.invalidate();app.screenshot(dir+"/drag-charge-owner.png");}
        if(f.site==3&&f.z==15&&zoom==5.){
            app.invalidate();app.screenshot(dir+"/drag-phosphorus-owner.png");
            std::vector<uint8_t> pixels;const int width=s.renderView.widthPx,height=s.renderView.heightPx;
            require(app.gl->readPixels(0,0,width,height,&pixels).ok(),"read the actual drag display");
            double x=0,y=0;int blue=0;
            for(int row=0;row<height;++row)for(int col=0;col<width;++col){const auto i=(size_t(row)*width+col)*4;
                if(pixels[i]==0&&pixels[i+1]==120&&pixels[i+2]==212){x+=col;y+=height-1-row;++blue;}}
            const auto bounds=arrows::mechanismAtomBodyBox(s.doc,*s.doc.atoms().alive(host));
            const auto expected=s.view().toDevice((bounds.min+bounds.max)*.5);
            require(blue>20&&length(Vec2(x/std::max(1,blue),y/std::max(1,blue))-expected)<2,
                    "rendered blue pixels surround the real phosphorus rather than the previous carbon");
        }
        SendMessageW(app.canvas,WM_LBUTTONUP,0,point(Vec2(85,84)));
        require(app.electronDragAtom==kInvalidObjectId,"release ends the owner highlight");
        render::OverlayScene released;app.appendElectronMarkerSelection(released);
        for(const auto& p:released.scene().primitives())require(!p.source.valid(),"owner capture disappears after release");
        if(s.doc.digest()!=before)require(s.undo().ok()&&s.doc.digest()==before,"marker movement remains a single undo step");
        require(app.beginElectronMarkerDrag(s.view().toDevice(center)),"restart marker drag");checkRing();app.cancelElectronMarkerDrag();
        require(app.electronDragAtom==kInvalidObjectId,"cancel ends the owner highlight");++cases;
    }
    std::ofstream(dir+"/electron-drag-owner.json")<<"{\"pass\":true,\"cases\":"<<cases<<"}";
}
