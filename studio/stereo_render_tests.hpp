// Regression for the reported connected-zigzag hashed wedge. Exercise the
// actual scene adapter, endpoint orientation, label clipping and transactions.
{
    using namespace r62::integration;
    const BondDisplay displays[]={BondDisplay::Wedge,BondDisplay::WedgeHash,BondDisplay::Hash};
    for(auto display:displays)for(int angle=0;angle<360;angle+=15){
        Session stereo;ObjectId a,b,bond;Vec2 origin(30,50),u=core::direction(angle),n(-u.y,u.x);
        stereo.doc.addAtom(origin,6,&a);stereo.doc.addAtom(origin+u*14.4,6,&b);stereo.doc.addBond(a,b,&bond);stereo.doc.setBondDisplay(bond,display);stereo.refresh();
        RingGeometry geometry(stereo.doc);auto data=geometry.build(*stereo.doc.bonds().get(bond));
        require(data.shapes.size()==(display==BondDisplay::Wedge?1:6),"native stereo shape count at 14.4 pt");
        double previousEnd=-1,firstWidth=0,lastWidth=0;
        for(size_t i=0;i<data.shapes.size();++i){auto& shape=data.shapes[i];
            require(shape.kind==bonds::BondShapeKind::FilledPolygon&&shape.outline.size()==4,"stereo contour is a filled trapezoid, not a connected hash fan");
            double low=1e100,high=-1e100,minNormal=1e100,maxNormal=-1e100;
            for(auto p:shape.outline){double t=dot(p-origin,u),h=dot(p-origin,n);low=std::min(low,t);high=std::max(high,t);minNormal=std::min(minNormal,h);maxNormal=std::max(maxNormal,h);}
            require(low>=-1e-8&&high<=14.4+1e-8,"stereo geometry respects stored endpoints");
            if(i)require(low>previousEnd+.5,"hashed stripes have a real empty gap without diagonal connectors");
            if(i==0){require(std::abs(low)<1e-8,"first stereo contour starts at the narrow atom");firstWidth=maxNormal-minNormal;}
            previousEnd=high;lastWidth=maxNormal-minNormal;
        }
        require(std::abs(previousEnd-14.4)<1e-8,"last stripe reaches the wide atom");
        if(display==BondDisplay::WedgeHash)require(lastWidth>firstWidth*3,"virtual wedge widens toward its stored end");
        if(display==BondDisplay::Hash)require(std::abs(lastWidth-firstWidth)<1e-8,"plain hashes stay uniform width");
        size_t primitives=0;for(const auto& p:stereo.scene.primitives())if(p.source==ObjectRef(ObjectKind::Bond,bond)){
            require(p.kind==render::PrimKind::Polygon&&p.style.fill.enabled&&!p.style.stroke.enabled,"rendered wedge is filled once without an added outline or connecting polyline");++primitives;
        }
        require(primitives==data.shapes.size(),"every stripe reaches the real scene independently");
        auto before=stereo.doc.digest();require(stereo.move(stereo.all(),Vec2(17,9)).ok(),"move stereo structure");require(stereo.undo().ok()&&stereo.doc.digest()==before,"stereo drag undo restores document");
        if(angle==0){std::string path=testOut+"/stereo_"+std::to_string(int(display))+".cdxml";require(stereo.save(path,true).ok(),"save stereo structure");Session reload;require(reload.open(path).ok(),"reopen stereo structure");auto original=stereo.doc.bonds().get(bond),restored=reload.doc.bonds().get(bond);require(original->display==restored->display&&original->beginAtom==restored->beginAtom&&original->endAtom==restored->endAtom,"stereo display and direction survive CDXML");}
    }
    // A terminal O label must clip filled stereo geometry as well as line bonds.
    for(auto display:displays){Session labelled;ObjectId a,o,b;labelled.doc.addAtom(Vec2(),6,&a);labelled.doc.addAtom(Vec2(14.4,0),8,&o);labelled.doc.addBond(a,o,&b);labelled.doc.setBondDisplay(b,display);labelled.refresh();RingGeometry geometry(labelled.doc);auto labels=molecular::labels(labelled.doc,labelled.glyphs,geometry);auto data=geometry.build(*labelled.doc.bonds().get(b));molecular::clip(data,*labelled.doc.bonds().get(b),labels,1.6);require(data.clippedByLabel&&!data.shapes.empty(),"stereo contour clips at oxygen label");for(const auto& shape:data.shapes)for(auto p:shape.outline)require(p.x<11,"stereo filled contour stays clear of oxygen");}
    // Exercise toolbar drawing and verify that its preview has separate stripes.
    for(const auto& command:{"chooseBondTool_Wedged","chooseBondTool_Hashed_Wedged","chooseBondTool_Hollow_Wedged"}){
        Session drawn;drawn.activateCommand(command);drawn.gesture(PointerPhase::Down,Vec2());drawn.gesture(PointerPhase::Move,Vec2(14.4,0));
        if(std::string(command)=="chooseBondTool_Hashed_Wedged"){require(drawn.ghost.items().size()==6,"drag preview has six separate hashed stripes");auto overlay=drawn.overlay();size_t count=0;for(const auto& p:overlay.scene().primitives())if(p.kind==render::PrimKind::Polygon){require(p.style.fill.enabled&&!p.style.stroke.enabled,"hashed preview has no extra outline stroke");++count;}require(count==6,"actual preview scene has independent hash polygons");}
        drawn.gesture(PointerPhase::Up,Vec2(14.4,0));require(drawn.doc.stats().aliveBonds==1,"stereo toolbar commits one bond");require(drawn.undo().ok()&&drawn.doc.stats().aliveBonds==0,"stereo tool is one undo step");
    }
    // Visual artifacts: real toolbar icons and a molecule with both wedge types.
    Bitmap icons(560,280,PixelFormat32bppARGB);{Graphics graphics(&icons);graphics.Clear(white);graphics.SetSmoothingMode(SmoothingModeAntiAlias);icon(graphics,"chair",RectF(0,0,280,280),teal);icon(graphics,"chair2",RectF(280,0,280,280),teal);}
    UINT count=0,bytes=0;GetImageEncodersSize(&count,&bytes);std::vector<uint8_t> encoders(bytes);auto codecs=(ImageCodecInfo*)encoders.data();GetImageEncoders(count,bytes,codecs);for(UINT i=0;i<count;++i)if(wcscmp(codecs[i].MimeType,L"image/png")==0)icons.Save(W(testOut+"/chair_icons.png").c_str(),&codecs[i].Clsid,0);
    s.cancel();s.doc=Document();s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());
    ObjectId center,left,right,up,down,bond;s.doc.addAtom(Vec2(),6,&center);s.doc.addAtom(Vec2(-14.4,0),6,&left);s.doc.addAtom(Vec2(14.4,0),6,&right);s.doc.addAtom(Vec2(0,-14.4),6,&up);s.doc.addAtom(Vec2(0,14.4),6,&down);s.doc.addBond(center,left,&bond);s.doc.addBond(center,right,&bond);s.doc.addBond(center,up,&bond);s.doc.setBondDisplay(bond,BondDisplay::Wedge);s.doc.addBond(center,down,&bond);s.doc.setBondDisplay(bond,BondDisplay::WedgeHash);s.refresh();s.fit();screenshot(testOut+"/stereo_display.png",false,false);s.save(testOut+"/stereo_display.cdxml",true);
    std::ofstream(testOut+"/stereo_render_tests.json")<<"{\"rotations\":24,\"styles\":3,\"separate_hash_stripes\":true,\"uniform_hash_distinct\":true,\"filled_without_extra_stroke\":true,\"label_clearance\":true,\"preview\":true,\"drag_undo\":true,\"save_reload_direction\":true}";
    s.cancel();s.doc=Document();s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());s.refresh();
}
