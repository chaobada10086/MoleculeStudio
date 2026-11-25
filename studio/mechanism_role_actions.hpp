#pragma once
#include "engine/mechanism_electrons.hpp"
void App::insertMechanismRole(bool base){
    s.cancel();ObjectId existing=kInvalidObjectId;
    std::vector<ObjectRef> captions;auto position=s.view().center;
    if(!s.selected.empty()){
        if(s.selected.size()==1&&s.selected[0].kind==ObjectKind::Atom)existing=s.selected[0].id;
        else{
            const TextRecord* label=nullptr;const TextRecord* charge=nullptr;bool valid=s.selected.size()<=2;
            for(auto ref:s.selected){auto t=ref.kind==ObjectKind::Text?s.doc.texts().alive(ref.id):nullptr;if(!t){valid=false;break;}
                auto text=s.doc.plainText(ref.id);
                if((base?text=="B":(text=="Nu"||text=="Nu:"))&&!label)label=t;
                else if((text=="-"||text=="−")&&!charge)charge=t;
                else{valid=false;break;}
            }
            if(!valid||!label){status="请选择单独的 B 或 Nu 文字，可同时选中旁边的负号；其他文字不能转换为通用试剂。";invalidate();return;}
            double size=label->size>0?label->size:s.doc.defaultTextSize();
            if(charge&&core::length(charge->position-label->position)>size*2){status="选中的负号离 B/Nu 太远，请只选择该试剂旁边的负号。";invalidate();return;}
            // Captions use a left-baseline anchor; atom labels use their centre.
            position=label->position+Vec2(size*(base?.3:.6),-size*.35);captions=s.selected;
        }
    }
    auto result=std::make_shared<ObjectId>(kInvalidObjectId);std::string role=base?"base":"nucleophile";
    auto error=s.submit(Command("插入通用试剂",kInvalidateStructure|kInvalidateGeometry|kInvalidateAppearance,[existing,result,position,role,captions](CommandContext& c){
        *result=existing;Error e;if(*result==kInvalidObjectId)e=c.document.addAtom(position,6,result.get());
        if(e.ok())e=r62::mechanism::setGenericRole(c.document,*result,role);
        if(e.ok()&&role=="nucleophile")
            c.document.extensions(ObjectRef(ObjectKind::Atom,*result)).set(integration::molecular::electronMarkerKey(false),"auto");
        if(e.ok())for(auto ref:captions){e=c.document.removeObject(ref);if(!e.ok())break;}
        return e.ok()?CommandResult::success():CommandResult::fail(e.code,e.message);
    }),"插入通用试剂");
    if(!error.ok()){status=error.message;invalidate();return;}
    s.selected={ObjectRef(ObjectKind::Atom,*result)};s.activateCommand("chooseSelectionTool_Marquee");updateInfo();
    status=base?"已插入通用碱 B⁻（不是硼）；可移动后连接去质子化机理箭头。":"已插入 Nu⁻，可从孤对电子起笔。";invalidate();
}

void App::insertMechanismProton(){
    s.cancel();
    auto position=s.view().center;
    ObjectRef caption;
    if(s.selected.size()==1&&s.selected[0].kind==ObjectKind::Text){
        const auto ref=s.selected[0];
        auto text=s.doc.texts().alive(ref.id);
        const auto label=s.doc.plainText(ref.id);
        if(text&&(label=="H+"||label=="H⁺")){
            const double em=text->size>0?text->size:s.doc.defaultTextSize();
            position=text->position+Vec2(em*.35,-em*.35);
            caption=ref;
        }
    }
    auto id=std::make_shared<ObjectId>(kInvalidObjectId);
    auto result=s.submit(Command("插入质子",kInvalidateStructure|kInvalidateAppearance,[position,caption,id](CommandContext& c){
        auto& doc=c.document;
        auto error=doc.addAtom(position,1,id.get());
        if(error.ok()){
            auto sem=doc.atoms().alive(*id)->semantics;
            sem.charge=1;sem.implicitHydrogens=0;sem.showLabel=true;
            error=doc.setAtomSemantics(*id,sem);
        }
        if(error.ok()&&caption.valid())error=doc.removeObject(caption);
        return error.ok()?CommandResult::success():CommandResult::fail(error.code,error.message);
    }),"插入质子");
    error(result);
    if(result.ok()){
        s.selected={ObjectRef(ObjectKind::Atom,*id)};
        s.activateCommand("chooseSelectionTool_Marquee");
        status="已插入 H⁺，可将双电子机理箭头指向 H 或正号。";
    }
    updateInfo();invalidate();
}

void App::attachMechanismHydrogen(ObjectId id){
    auto e=s.submit(Command("添加 B–H 化学键",kInvalidateStructure|kInvalidateGeometry|kInvalidateAppearance,[id](CommandContext& c){
        auto& d=c.document;auto a=d.atoms().alive(id);
        if(!a||r62::mechanism::genericRole(d,id)!="base"||a->semantics.charge!=-1||!d.incidentBonds(id).empty())return CommandResult::fail(ErrorCode::InvalidArgument,"请选择尚未成键的 B⁻。");
        auto pos=a->position;auto sem=a->semantics;sem.charge=0;ObjectId h,b;
        auto e=d.addAtom(pos+Vec2(d.defaultBondLength(),0),1,&h);
        if(e.ok()){auto hs=d.atoms().get(h)->semantics;hs.showLabel=true;hs.implicitHydrogens=0;e=d.setAtomSemantics(h,hs);}
        if(e.ok())e=d.addBond(id,h,&b);
        if(e.ok())e=d.setAtomSemantics(id,sem);
        return e.ok()?CommandResult::success():CommandResult::fail(e.code,e.message);
    }),"添加 B–H 化学键");
    error(e);if(e.ok())status="已生成可编辑的 B–H 单键，可参与机理推演。";updateInfo();invalidate();
}

App::ElectronMarkerHit App::electronMarkerAt(Vec2 point,double tolerancePixels,bool includeEditingSites,bool deleting) const{
    const double tolerance=s.view().deviceRadiusToDocument(tolerancePixels);
    double bestGap=1e100,bestDistance=1e100;
    ElectronMarkerHit hit;
    if(!includeEditingSites&&!deleting&&std::none_of(s.doc.atoms().records().begin(),s.doc.atoms().records().end(),[](const AtomRecord& atom){
        return atom.isAlive()&&(atom.semantics.radical||atom.semantics.charge);
    }))return hit;
    const auto& boxes=arrows::mechanismVisualBoxes(s.doc);
    auto consider=[&](ObjectId id,int site,Vec2 center,double gap,int pair=-1){
        if(gap>tolerance)return;
        const double distance=length(point-center);
        if(gap>bestGap||(gap==bestGap&&distance>=bestDistance))return;
        bestGap=gap;bestDistance=distance;
        hit.atom=id;hit.site=site;hit.pair=pair;hit.center=center;
    };
    for(const auto& entry:boxes){
        const auto& visual=entry.second;
        const auto* atom=s.doc.atoms().alive(entry.first);
        // A generated charge may still be a superscript in the atom label.
        if(visual.hasCharge&&atom&&atom->semantics.charge!=0){
            const auto& box=visual.charge;
            consider(entry.first,2,(box.min+box.max)*.5,arrows::mechanismBoxDistance(box,point));
        }
        if(includeEditingSites&&!deleting&&visual.hasAttachedProton)
            consider(entry.first,attachedProtonDragSite,(visual.attachedProton.min+visual.attachedProton.max)*.5,
                     arrows::mechanismBoxDistance(visual.attachedProton,point));
        if(includeEditingSites&&!deleting&&visual.hasTerminal)
            consider(entry.first,attachedProtonDragSite,(visual.terminal.min+visual.terminal.max)*.5,arrows::mechanismBoxDistance(visual.terminal,point),100+visual.terminalElement);
        if(includeEditingSites&&!deleting&&visual.hasAttachedProtonFirst)
            consider(entry.first,attachedProtonDragSite,(visual.attachedProtonFirst.min+visual.attachedProtonFirst.max)*.5,
                     arrows::mechanismBoxDistance(visual.attachedProtonFirst,point),-2);
        if(includeEditingSites||deleting)for(size_t index=0;index<visual.lonePairs.size();++index){
            const auto& box=visual.lonePairs[index];
            consider(entry.first,3,(box.min+box.max)*.5,arrows::mechanismBoxDistance(box,point),int(index));
        }
        if(!visual.radicalPoints.empty()){
            auto center=(visual.radicalPoints.front()+visual.radicalPoints.back())*.5;
            for(auto dot:visual.radicalPoints)
                consider(entry.first,4,center,std::max(0.,length(point-dot)-arrows::mechanismRadicalRadius(s.doc,entry.first)));
        }
    }
    return hit;
}
bool App::beginElectronMarkerDrag(Vec2 pixel,uint32_t modifiers){
    const auto& tool=s.entries[s.activeIndex];
    if(textEditor.active||down||(modifiers&(kModControl|kModAlt))||
       tool.owner=="T09"||tool.command.find("Eraser")!=std::string::npos)return false;
    const bool selection=tool.owner=="T05";
    // Shift moves a marker while an arrow tool is active.
    if(dynamic_cast<const arrows::ArrowTool*>(tool.tool.get())&&!(modifiers&kModShift))return false;
    const auto point=s.view().toDocument(pixel);
    auto hit=electronMarkerAt(point,selection?3:2,selection);
    electronDragText=kInvalidObjectId;
    if(hit.atom==kInvalidObjectId&&selection){
        double best=s.view().deviceRadiusToDocument(3);
        for(const auto& caption:s.doc.texts().records())if(caption.isAlive()){
            auto label=studioions::ascii(s.doc.plainText(caption.id));auto terminal=studio_terminal_drag::find(label);if(!terminal&&!studio::hydride_labels::terminal(label))continue;
            text::LayoutOptions options;options.triangulateGlyphs=false;options.defaultSize=caption.size;options.defaultFont=caption.format.fontName.empty()?s.doc.defaultFont():caption.format.fontName;options.defaultFlags=caption.format.flags;
            text::RichLayout layout;if(!text::layoutRichText(s.doc.runs(caption.id),Vec2(),options,&s.glyphs,&layout).ok())continue;
            Vec2 offset=caption.position-Vec2(0,layout.metrics.firstBaseline);if(caption.format.align==1)offset.x-=layout.metrics.width*.5;else if(caption.format.align==2)offset.x-=layout.metrics.width;
            size_t last=label.size();while(last&&label[last-1]>='0'&&label[last-1]<='9')--last;
            const bool trailing=last>1&&label[last-1]=='H';
            for(size_t index=0;index<layout.glyphs.size();++index){const auto& glyph=layout.glyphs[index];bool terminalGlyph=terminal&&index==terminal->offset;bool hydrogenGlyph=glyph.codepoint=='H'&&(index==0||(trailing&&index==last-1));if(!terminalGlyph&&!hydrogenGlyph)continue;
                Box box;bool ink=false;for(const auto& contour:glyph.contours)for(auto p:contour){p=p+offset;if(ink)box.add(p);else{box=Box(p,p);ink=true;}}
                if(terminalGlyph)for(size_t j=index+1;j<index+terminal->token.size()&&j<layout.glyphs.size();++j)for(const auto& contour:layout.glyphs[j].contours)for(auto p:contour)box.add(p+offset);
                if(!ink)continue;double gap=arrows::mechanismBoxDistance(box,point);if(gap>best)continue;best=gap;
                hit.atom=caption.id;hit.site=attachedProtonDragSite;hit.pair=terminalGlyph?100+terminal->element:index==0?-2:-1;hit.center=(box.min+box.max)*.5;electronDragText=caption.id;
                Box body;bool any=false;for(size_t j=0;j<layout.glyphs.size();++j){const auto& other=layout.glyphs[j];if(terminalGlyph?(j>=terminal->offset&&j<terminal->offset+terminal->token.size()):other.codepoint=='H')continue;if(other.codepoint>='0'&&other.codepoint<='9')continue;for(const auto& contour:other.contours)for(auto p:contour){p=p+offset;if(any)body.add(p);else{body=Box(p,p);any=true;}}}
                electronDragHostPosition=any?(body.min+body.max)*.5:caption.position;
            }
        }
    }
    if(hit.atom==kInvalidObjectId)return false;
    if(electronDragText==kInvalidObjectId)electronDragHostPosition=s.doc.atoms().alive(hit.atom)->position;
    SetFocus(canvas);
    SetCapture(canvas);
    electronDragAtom=hit.atom;electronDragSite=hit.site;electronDragPair=hit.pair;electronDragCenter=hit.center;
    electronDragPress=point;
    selectedElectronMarker={};
    if(hit.site==2||hit.site==3||hit.site==4){
        selectedElectronMarker=hit;selectedElectronDocument=s.doc.id();selectedElectronRevision=s.doc.revision();
        s.selected.clear();
        status="已选中电子标记 · Delete 删除并按剩余成键补氢";
    }
    s.ghost.clear();
    SetCursor(LoadCursorW(nullptr,MAKEINTRESOURCEW(32646)));
    invalidate();
    return true;
}
bool App::electronMarkerSelected() const{
    const auto& hit=selectedElectronMarker;
    if(hit.atom==kInvalidObjectId||selectedElectronDocument!=s.doc.id()||
       selectedElectronRevision!=s.doc.revision()||!s.selected.empty())return false;
    const auto* atom=s.doc.atoms().alive(hit.atom);
    return atom&&((hit.site==2&&atom->semantics.charge)||(hit.site==4&&atom->semantics.radical)||
                 (hit.site==3&&hit.pair>=0&&hit.pair<mechanism::lonePairCount(s.doc,hit.atom)));
}
bool App::deleteSelectedElectronMarker(){
    if(!electronMarkerSelected()){selectedElectronMarker={};return false;}
    const auto hit=selectedElectronMarker;
    cancelElectronMarkerDrag();deleteElectronMarker(hit);return true;
}
void App::appendElectronMarkerSelection(render::OverlayScene& overlay) const{
    auto append=[&](render::Primitive primitive){
        // These overlays are appended after Session::overlay() has assigned its
        // cache versions. Give them geometry-based versions too, or DX11 can
        // reuse a previous atom's circle at the same overlay ordinal.
        uint64_t hash=1469598103934665603ull^s.doc.id();
        auto add=[&](const void* data,size_t count){const auto* bytes=static_cast<const unsigned char*>(data);for(size_t i=0;i<count;++i){hash^=bytes[i];hash*=1099511628211ull;}};
        add(&primitive.kind,sizeof(primitive.kind));add(&primitive.layer,sizeof(primitive.layer));add(&primitive.z,sizeof(primitive.z));
        for(auto point:primitive.points){add(&point.x,sizeof(point.x));add(&point.y,sizeof(point.y));}
        add(&primitive.style.strokeWidth,sizeof(primitive.style.strokeWidth));add(&primitive.style.stroke.rgba,sizeof(primitive.style.stroke.rgba));
        primitive.version=hash;primitive.source=ObjectRef();overlay.add(primitive);
    };
    if(electronDragAtom!=kInvalidObjectId&&electronDragText==kInvalidObjectId){
        if(electronDragSite!=2&&electronDragSite!=3&&electronDragSite!=4)return;
        const auto* atom=s.doc.atoms().alive(electronDragAtom);if(!atom)return;
        // Follow the captured owner throughout the drag, even when the pointer
        // passes over a neighbouring atom. Use the attachment-element glyph so
        // condensed OH/HO and Ph3P highlight O/P rather than the whole label.
        const auto body=arrows::mechanismAtomBodyBox(s.doc,*atom);
        const auto center=(body.min+body.max)*.5;
        const double radius=std::max(s.view().deviceRadiusToDocument(14.),
            std::min(s.view().deviceRadiusToDocument(46.),body.height()*.35));
        render::Primitive ring;ring.kind=render::PrimKind::Polyline;
        ring.layer=render::LayerId(render::layers::kSelection);ring.z=4;
        for(int i=0;i<=48;++i){
            const double angle=i*6.283185307179586/48;
            ring.points.push_back(center+Vec2(std::cos(angle),std::sin(angle))*radius);
        }
        const double pad=radius+s.view().deviceRadiusToDocument(3.5);
        ring.bounds=Box(center-Vec2(pad,pad),center+Vec2(pad,pad));ring.style.fill.enabled=false;
        ring.style.stroke=render::Paint(0xFFFFFFFFu,true);ring.style.strokeWidth=s.view().deviceRadiusToDocument(7.);append(ring);
        ring.z=5;ring.style.stroke=render::Paint(0x0078D4FFu,true);ring.style.strokeWidth=s.view().deviceRadiusToDocument(3.5);append(ring);
        return;
    }
    if(!electronMarkerSelected())return;
    const auto& hit=selectedElectronMarker;
    const auto& boxes=arrows::mechanismVisualBoxes(s.doc);
    const auto entry=boxes.find(hit.atom);if(entry==boxes.end())return;
    const auto& visual=entry->second;Box box;
    if(hit.site==2)box=visual.charge;
    else if(hit.site==3&&size_t(hit.pair)<visual.lonePairs.size())box=visual.lonePairs[hit.pair];
    else if(hit.site==4&&!visual.radicalPoints.empty()){
        box=Box(visual.radicalPoints.front(),visual.radicalPoints.front());
        for(auto dot:visual.radicalPoints)box.add(dot);
        const double radius=arrows::mechanismRadicalRadius(s.doc,hit.atom);box.min=box.min-Vec2(radius,radius);box.max=box.max+Vec2(radius,radius);
    }else return;
    const double pad=s.view().deviceRadiusToDocument(4);
    box.min=box.min-Vec2(pad,pad);box.max=box.max+Vec2(pad,pad);
    render::Primitive edge;edge.kind=render::PrimKind::Polyline;edge.layer=render::LayerId(render::layers::kSelection);edge.z=3;
    edge.points={box.min,Vec2(box.max.x,box.min.y),box.max,Vec2(box.min.x,box.max.y),box.min};
    edge.style.stroke=render::Paint(0x0078D4FFu,true);edge.style.fill.enabled=false;
    edge.style.strokeWidth=s.view().deviceRadiusToDocument(1.5);edge.bounds=box;
    append(edge);
}
void App::deleteElectronMarker(ElectronMarkerHit hit){
    const bool radical=hit.site==4,lonePair=hit.site==3;
    auto result=s.submit(studio::electron_marker::remove({hit.atom,hit.site,hit.pair,hit.center}),"删除电子标记");
    error(result);
    if(result.ok()){selectedElectronMarker={};status=lonePair?"已删除孤电子对，氢数已按剩余成键更新":radical?"已删除自由基或卡宾，氢数已按剩余成键更新":"已删除电荷，氢数已按剩余成键更新";}
    updateInfo();invalidate();
}
using studio::attached_atoms::exposeAttachedProton;
using studio::attached_atoms::materializeHydrogenCaption;

void App::updateAttachedProtonDrag(Vec2 pixel,bool finish){
    const auto host=electronDragAtom;
    const auto caption=electronDragText;
    const bool front=electronDragPair==-2;
    const int terminalElement=electronDragPair>=100?electronDragPair-100:0;
    const auto hostPosition=electronDragHostPosition;
    const auto* atom=s.doc.atoms().alive(host);
    if(caption==kInvalidObjectId?!atom:!s.doc.texts().alive(caption)){cancelElectronMarkerDrag();return;}
    const auto delta=s.view().toDocument(pixel)-electronDragPress;
    const bool moved=length(delta)>=s.view().deviceRadiusToDocument(3);
    if(!moved){
        if(finish)cancelElectronMarkerDrag();
        else if(attachedProtonPreview){attachedProtonPreview.reset();invalidate();}
        return;
    }
    auto direction=electronDragCenter+delta-hostPosition;
    if(length(direction)<1e-8)direction=electronDragCenter-hostPosition;
    const double distance=length(direction);
    if(distance<1e-8){cancelElectronMarkerDrag();return;}
    const double unit=s.doc.defaultBondLength();
    const auto position=hostPosition+direction*(std::max(unit,std::min(unit*1.5,distance))/distance);
    if(!finish){
        if(attachedProtonPreview&&position==attachedProtonPreviewPosition)return;
        if(!attachedProtonPreview)attachedProtonPreview.reset(new Session());
        attachedProtonPreview->doc=s.doc;
        ObjectId hydrogen,previewHost=host;auto prepared=Error();
        if(caption!=kInvalidObjectId)prepared=materializeHydrogenCaption(attachedProtonPreview->doc,caption,hostPosition,&previewHost);
        if(prepared.ok()&&exposeAttachedProton(attachedProtonPreview->doc,previewHost,position,&hydrogen,front,terminalElement).ok()){
            attachedProtonPreviewPosition=position;
            attachedProtonPreview->refresh();
        }else attachedProtonPreview.reset();
        invalidate();return;
    }
    cancelElectronMarkerDrag();
    auto id=std::make_shared<ObjectId>(kInvalidObjectId);
    const std::string extractionTitle=terminalElement?"拉出末端原子":"拉出附着氢";
    auto result=s.submit(Command(extractionTitle,kInvalidateStructure|kInvalidateGeometry|kInvalidateAppearance,
        [host,caption,hostPosition,position,id,front,terminalElement](CommandContext& c){
            ObjectId actualHost=host;auto e=Error();if(caption!=kInvalidObjectId)e=materializeHydrogenCaption(c.document,caption,hostPosition,&actualHost);
            if(e.ok())e=exposeAttachedProton(c.document,actualHost,position,id.get(),front,terminalElement);
            return e.ok()?CommandResult::success():CommandResult::fail(e.code,e.message);
        }),extractionTitle);
    error(result);
    if(result.ok()){
        s.selected={ObjectRef(ObjectKind::Atom,*id)};
        status=terminalElement?"已拉出末端原子，保留原分子组成；可捕捉原子和新键绘制机理箭头。":"已拉出显式 H，可捕捉氢原子和新单键绘制机理箭头。";
    }
    updateInfo();invalidate();
}
void App::updateElectronMarkerDrag(Vec2 pixel,bool finish){
    if(electronDragSite==attachedProtonDragSite){updateAttachedProtonDrag(pixel,finish);return;}
    const auto id=electronDragAtom;
    const auto* atom=s.doc.atoms().alive(id);
    if(!atom){cancelElectronMarkerDrag();return;}
    const auto delta=s.view().toDocument(pixel)-electronDragPress;
    const double em=std::max(.1,integration::molecular::setting(s.doc,"LabelSize",10));
    const int site=electronDragSite,pair=electronDragPair;
    const bool radical=site==4,lonePair=site==3;
    auto host=atom->position;
    if(lonePair)
        host=arrows::mechanismVisualBoxes(s.doc).at(id).lonePairHost;
    auto desired=electronDragCenter+delta-host;
    if(length(desired)<1e-8)desired=(electronDragCenter-host)*.01;
    const auto offset=integration::molecular::boundedElectronOffset(desired,em);
    const auto center=host+offset;
    const int charge=atom->semantics.charge;
    const bool circled=integration::molecular::circledElectronCharge(s.doc,id);
    if(finish){
        cancelElectronMarkerDrag();
        if(length(delta)<s.view().deviceRadiusToDocument(3)){
            if(s.entries[s.activeIndex].owner=="T05"){
                s.selected.clear();
                pointerPixel=pixel;pointerOverCanvas=true;
                status=lonePair?"按 Delete 删除这个孤电子对并补氢":radical?"按 Delete 删除这个自由基或卡宾并补氢":"按 Delete 删除这个电荷并补氢";
                updateInfo();invalidate();
            }
            return;
        }
        error(s.submit(studio::electron_marker::move(s.doc,{id,site,pair,electronDragCenter},host,offset,em),"移动电子标记"));
        if(selectedElectronMarker.atom==id)selectedElectronRevision=s.doc.revision();
    }else{
        s.ghost.clear();
        PreviewItem preview;
        preview.style.colorRGBA=0x2563EBFF;
        preview.style.width=std::max(.6,em*.08);
        auto drawCircle=[&](Vec2 at,double radius,bool filled){
            preview.primitive=filled?PreviewPrimitive::Polygon:PreviewPrimitive::Polyline;
            preview.fillEnabled=filled;preview.fillRGBA=preview.style.colorRGBA;preview.points.clear();
            for(int i=0;i<=32;++i){double angle=6.283185307179586*i/32.;preview.points.push_back(at+Vec2(std::cos(angle),std::sin(angle))*radius);}
            s.ghost.add(preview);
        };
        if(radical)drawCircle(center,arrows::mechanismRadicalRadius(s.doc,id),true);
        else if(lonePair){
            for(auto dot:mechanism::lonePairDotsAt(center,host,em))
                drawCircle(dot,mechanism::lonePairDotRadius(em),true);
        }else{
            if(circled)drawCircle(center,integration::molecular::electronChargeCircleRadius(em),false);
            preview.primitive=PreviewPrimitive::Segment;
            auto arm=Vec2(em*.19,0);
            preview.points={center-arm,center+arm};s.ghost.add(preview);
            if(charge>0){arm=Vec2(0,em*.19);preview.points={center-arm,center+arm};s.ghost.add(preview);}
        }
    }
    invalidate();
}
void App::cancelElectronMarkerDrag(){
    if(electronDragAtom==kInvalidObjectId)return;
    electronDragAtom=kInvalidObjectId;electronDragText=kInvalidObjectId;attachedProtonPreview.reset();s.ghost.clear();
    if(GetCapture()==canvas)ReleaseCapture();
    invalidate();
}

void App::applyMechanismElectronState(int state,bool circled){
    if((state==-1||state==1||state==0)&&s.selected.size()==1){
        const auto selected=s.selected.front();
        const auto label=selected.kind==ObjectKind::Text?s.doc.plainText(selected.id):selected.kind==ObjectKind::Atom?s.doc.atomLabel(selected.id):std::string();
        if(const auto* ion=state==0?mechanism::lonePairGroup(label):studioions::find(label)){
            if(selected.kind==ObjectKind::Text&&!mechanism::compactLonePairGroup(label)){
                const auto* caption=s.doc.texts().alive(selected.id);const auto at=caption->position+Vec2(caption->size*.35,-caption->size*.35);
                auto id=std::make_shared<ObjectId>(kInvalidObjectId);
                auto e=s.submit(Command("文字转换为离子",kInvalidateStructure|kInvalidateSemantics|kInvalidateGeometry|kInvalidateAppearance,[selected,ion,state,at,id,circled](CommandContext& c){
                    auto e=c.document.addAtom(at,ion->element,id.get());if(e.ok())e=studioions::apply(c.document,*id,*ion,state);
                    if(e.ok()&&state==0)c.document.extensions(ObjectRef(ObjectKind::Atom,*id)).set(mechanism::lonePairKey(),"1");
                    if(e.ok()&&circled)c.document.extensions(ObjectRef(ObjectKind::Atom,*id)).set("pst.attr.MoleculeStudioChargeCircle","1");
                    if(e.ok())e=c.document.removeObject(selected);
                    return e.ok()?CommandResult::success():CommandResult::fail(e.code,e.message);
                }),"文字转换为离子");error(e);if(e.ok())s.selected={ObjectRef(ObjectKind::Atom,*id)};updateInfo();invalidate();return;
            }
        }
    }
    if(state==0&&s.selected.size()==1&&s.selected.front().kind==ObjectKind::Text){
        const auto caption=s.selected.front();
        const auto* record=s.doc.texts().alive(caption.id);
        auto runs=s.doc.runs(caption.id);
        // Formula typography splits the caption into runs at the subscript.
        std::string label;
        for(const auto& run:runs)label+=run.utf8;
        const auto* compact=mechanism::compactLonePairGroup(label);
        if(record&&(compact||mechanism::triphenylphosphineLabel(label))){
            text::LayoutOptions options;
            options.defaultFont=record->format.fontName.empty()?s.doc.defaultFont():record->format.fontName;
            options.defaultSize=record->size>0?record->size:s.doc.defaultTextSize();
            options.defaultFlags=record->format.flags;
            for(auto& run:runs){
                if(run.fontName.empty())run.fontName=options.defaultFont;
                if(run.size<=0)run.size=options.defaultSize;
            }
            text::RichLayout layout;
            auto e=text::layoutRichText(runs,Vec2(),options,&s.glyphs,&layout);
            if(!e.ok()){error(e);return;}
            auto baseline=record->position;
            if(record->format.align==1)baseline.x-=layout.metrics.width*.5;
            if(record->format.align==2)baseline.x-=layout.metrics.width;
            layout.anchor=baseline-Vec2(0,layout.metrics.firstBaseline);
            const auto center=compact?mechanism::compactLonePairCenter(label,layout,baseline):mechanism::triphenylphosphineCenter(label,&layout,baseline);
            std::ostringstream offset;offset.precision(16);offset<<(baseline.x-center.x)<<" "<<(baseline.y-center.y);
            const auto color=record->format.colorRGBA;
            const auto labelOffset=offset.str();
            const auto id=std::make_shared<ObjectId>(kInvalidObjectId);
            auto result=s.submit(Command("为缩写添加孤对电子",kInvalidateStructure|kInvalidateGeometry|kInvalidateAppearance,
                [caption,label,center,labelOffset,runs,color,id,compact](CommandContext& c){
                    auto& doc=c.document;
                    auto e=doc.addAtom(center,compact?compact->element:15,id.get());
                    if(e.ok()&&compact&&compounddata::find(label))e=nicknames::apply(doc,*id,label);
                    if(e.ok()&&compact)e=doc.setElement(*id,compact->element);
                    if(e.ok()){
                        auto sem=doc.atoms().alive(*id)->semantics;
                        sem.nodeType=uint8_t(compact?NodeType::Element:NodeType::Query);sem.showLabel=true;sem.implicitHydrogens=compact?compact->hydrogens:0;
                        e=doc.setAtomSemantics(*id,sem);
                    }
                    if(e.ok())e=doc.setAtomLabel(*id,label);
                    if(e.ok()){
                        studioatom::set(doc,*id,runs);
                        auto& bag=doc.extensions(ObjectRef(ObjectKind::Atom,*id));
                        bag.set("studio.label.offset",labelOffset);bag.set("studio.label.text",label);
                        if(color&255)bag.set("studio.color",std::to_string(color));
                        bag.set(mechanism::lonePairKey(),"1");
                        if(compact){bag.set("studio.mechanism.compact.valence",std::to_string(compact->hidden));bag.set("studio.mechanism.ion.base",compact->label);}
                        e=doc.removeObject(caption);
                    }
                    return e.ok()?CommandResult::success():CommandResult::fail(e.code,e.message);
                }),"为缩写添加孤对电子");
            error(result);
            if(result.ok()){
                s.selected={ObjectRef(ObjectKind::Atom,*id)};
                s.activateCommand("chooseSelectionTool_Marquee");
                status="已在缩写的供电子原子旁添加一对孤对电子。";
            }
            updateInfo();invalidate();return;
        }
    }
    std::vector<ObjectId> ids;for(auto ref:studioio::expanded(s.doc,s.selected)){
        if(ref.kind==ObjectKind::Atom&&s.doc.atoms().alive(ref.id))ids.push_back(ref.id);
        else if(state==0&&ref.kind==ObjectKind::Bond)if(auto b=s.doc.bonds().alive(ref.id)){ids.push_back(b->beginAtom);ids.push_back(b->endAtom);}
    }
    if(ids.empty()){status="请先选择要标注电子状态的原子。";invalidate();return;}
    std::sort(ids.begin(),ids.end());ids.erase(std::unique(ids.begin(),ids.end()),ids.end());
    if(state==0){
        auto hosts=mechanism::lonePairTargets(s.doc,ids);
        hosts.erase(std::remove_if(hosts.begin(),hosts.end(),[&](ObjectId id){return mechanism::lonePairCapacity(s.doc,id)<1;}),hosts.end());
        if(hosts.empty()){status="选区中没有可添加孤电子对的原子；请先检查结构、电荷和价态。";invalidate();return;}
        hosts.erase(std::remove_if(hosts.begin(),hosts.end(),[&](ObjectId id){return mechanism::lonePairCount(s.doc,id)>=mechanism::lonePairCapacity(s.doc,id);}),hosts.end());
        if(hosts.empty()){status="所选原子的可用孤电子对已全部显示。";invalidate();return;}
        auto e=s.submit(mechanism::setLonePairCommand(hosts),"插入孤对电子");
        error(e);if(e.ok()){
            s.selected.clear();for(auto id:hosts)s.selected.emplace_back(ObjectKind::Atom,id);
            s.activateCommand("chooseSelectionTool_Marquee");
            status="已添加孤对电子，可在原子附近拖动两点。";
        }
        updateInfo();invalidate();return;
    }
    auto cmd=atomElectronStateCommand(ids,state,circled);
    auto e=s.submit(cmd,cmd.name());
    error(e);
    if(e.ok()){
        s.activateCommand("chooseSelectionTool_Marquee");
        status="已更新电子状态 · 可在原子周围拖动标记";
    }
    updateInfo();invalidate();
}
