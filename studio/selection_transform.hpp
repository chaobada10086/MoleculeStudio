#ifndef STUDIO_SELECTION_TRANSFORM_HPP
#define STUDIO_SELECTION_TRANSFORM_HPP
// The selection frame of a single caption is its text box: a fixed wrap width
// set on the object wins over the ink extents, so a short caption still shows
// the whole box and its two width handles stay where the user put them.
bool App::textWrapBox(Box* out){
    if(s.selected.size()!=1||s.selected[0].kind!=ObjectKind::Text)return false;
    auto ref=s.selected[0];const auto* text=s.doc.texts().alive(ref.id);if(!text)return false;
    if(auto angle=static_cast<const Document&>(s.doc).extensions(ref).findValue("studio.text.RotationAngle"))if(std::abs(std::atof(angle->c_str()))>1e-7)return false;
    Box ink=studioio::selectionBounds(s);if(ink.empty())return false;
    double width=0;auto stored=static_cast<const Document&>(s.doc).extensions(ref).findValue("studio.text.width");
    if(stored)width=std::atof(stored->c_str());
    int align=text->format.align>2?0:int(text->format.align);
    double anchor=text->position.x;
    if(width<=0)width=ink.max.x-ink.min.x;
    double left=anchor-(align==1?width*.5:align==2?width:0);
    *out=Box(Vec2(left,ink.min.y),Vec2(left+width,ink.max.y));
    return true;
}
std::vector<Vec2> App::selectionHandlePoints(){
    if(s.selected.empty())return {};
    // Arrow endpoint handles and table cells retain their own editing tools.
    if(s.selected.size()==1&&(s.selected[0].kind==ObjectKind::Arrow||s.selected[0].kind==ObjectKind::Table))return {};
    auto b=studioio::selectionBounds(s);if(b.empty())return {};
    Box textBox;if(textWrapBox(&textBox)){b.min.x=textBox.min.x;b.max.x=textBox.max.x;}
    double unit=s.view().deviceRadiusToDocument(s.view().dpiScale*.75),pad=unit*3;
    b.min=b.min-Vec2(pad,pad);b.max=b.max+Vec2(pad,pad);
    double x=(b.min.x+b.max.x)*.5,y=(b.min.y+b.max.y)*.5;
    return {b.min,Vec2(x,b.min.y),Vec2(b.max.x,b.min.y),Vec2(b.min.x,y),Vec2(b.max.x,y),Vec2(b.min.x,b.max.y),Vec2(x,b.max.y),b.max,Vec2(x,b.min.y-unit*12)};
}
// Dragging the left/right middle handle of a caption sets its wrap width, which
// is what decides how many characters fit on one line. The opposite edge stays
// put, the anchor follows the alignment, and the document command keeps the
// whole gesture as one undo step.
bool App::beginTextWrap(Vec2,int handle){
    Box box;if(!textWrapBox(&box))return false;
    auto ref=s.selected[0];const auto* text=s.doc.texts().alive(ref.id);if(!text)return false;
    auto& bag=s.doc.extensions(ref);auto stored=bag.findValue("studio.text.width");
    wrapHadWidth=stored!=nullptr;wrapOldWidth=stored?*stored:std::string();
    wrapDragging=true;wrapHandle=handle;wrapText=ref.id;wrapAlign=text->format.align>2?0:int(text->format.align);
    wrapLeft=box.min.x;wrapRight=box.max.x;wrapStartWidth=box.max.x-box.min.x;
    wrapOldPositionX=text->position.x;wrapPreviewAt=0;
    s.cancel();status="拖动文字框左右边缘调整宽度（决定每行字数）";SetFocus(canvas);SetCapture(canvas);updateCanvasCursor();invalidate();return true;
}
void App::updateTextWrap(Vec2 pixel){
    if(!wrapDragging)return;
    auto* text=s.doc.texts().mutableGet(wrapText);if(!text||!text->isAlive()){cancelTextWrap();return;}
    double minimum=std::max(2.0,(text->size>0?text->size:s.doc.defaultTextSize())*0.5);
    double at=s.view().toDocument(pixel).x,left=wrapLeft,right=wrapRight;
    if(wrapHandle==4)right=std::max(left+minimum,at);else left=std::min(right-minimum,at);
    double width=right-left;
    double anchor=wrapAlign==1?left+width*.5:wrapAlign==2?right:left;
    text->position.x=anchor;
    s.doc.extensions(ObjectRef(ObjectKind::Text,wrapText)).set("studio.text.width",std::to_string(width));
    uint64_t now=GetTickCount64();
    if(now-wrapPreviewAt>=24){wrapPreviewAt=now;s.refresh();}
    invalidate();
}
void App::finishTextWrap(Vec2 pixel){
    if(!wrapDragging)return;
    updateTextWrap(pixel);
    auto* text=s.doc.texts().mutableGet(wrapText);
    double newAnchor=text?text->position.x:wrapOldPositionX,width=wrapStartWidth;
    {Box box;if(textWrapBox(&box))width=box.max.x-box.min.x;}
    wrapDragging=false;
    if(GetCapture()==canvas)ReleaseCapture();
    auto ref=ObjectRef(ObjectKind::Text,wrapText);
    if(text)text->position.x=wrapOldPositionX;
    auto& bag=s.doc.extensions(ref);
    if(wrapHadWidth)bag.set("studio.text.width",wrapOldWidth);else bag.erase("studio.text.width");
    bool moved=std::abs(width-wrapStartWidth)>1e-6||std::abs(newAnchor-wrapOldPositionX)>1e-6;
    if(moved&&text){
        ObjectId id=wrapText;
        error(s.submit(Command("文字框宽度",kInvalidateAppearance|kInvalidateGeometry,[id,newAnchor,width](CommandContext& c){
            auto* target=c.document.texts().mutableGet(id);
            if(!target||!target->isAlive())return CommandResult::fail(ErrorCode::InvalidArgument,"文字对象已不存在");
            if(std::abs(target->position.x-newAnchor)>1e-9){Error e=c.document.setTextPosition(id,Vec2(newAnchor,target->position.y));if(!e.ok())return CommandResult::fail(e.code,e.message);}
            c.document.extensions(ObjectRef(ObjectKind::Text,id)).set("studio.text.width",std::to_string(width));
            return CommandResult::success();
        }),"文字框宽度"));
        if(s.selected.size()==1)status="文字框宽度已调整 · Ctrl+Z 可撤销";
    }else s.refresh();
    updateCanvasCursor();invalidate();
}
void App::cancelTextWrap(){
    if(!wrapDragging)return;
    wrapDragging=false;
    if(auto* text=s.doc.texts().mutableGet(wrapText))text->position.x=wrapOldPositionX;
    auto& bag=s.doc.extensions(ObjectRef(ObjectKind::Text,wrapText));
    if(wrapHadWidth)bag.set("studio.text.width",wrapOldWidth);else bag.erase("studio.text.width");
    if(GetCapture()==canvas)ReleaseCapture();
    s.refresh();updateCanvasCursor();invalidate();
}
int App::selectionHandleAt(Vec2 pixel){
    auto points=selectionHandlePoints();double radius=6*scale;int result=-1;double best=radius;
    for(size_t i=0;i<points.size();++i){double d=core::length(s.view().toDevice(points[i])-pixel);if(d<=best){best=d;result=int(i);}}
    return result;
}
bool App::beginSelectionTransform(Vec2 pixel,uint32_t modifiers){
    const auto& tool=s.entries[s.activeIndex];if(tool.owner!="T05"||tool.command.find("Eraser")!=std::string::npos||textEditor.active||(modifiers&kModControl))return false;
    int hit=selectionHandleAt(pixel);if(hit<0)return false;
    if((hit==3||hit==4)&&beginTextWrap(pixel,hit))return true;
    if(s.dragFixedLength){
        auto refs=studioio::expanded(s.doc,s.selected);std::set<ObjectId> atoms;
        for(auto r:refs)if(r.kind==ObjectKind::Atom&&s.doc.atoms().alive(r.id))atoms.insert(r.id);
        if(!atoms.empty()){
            // A resize handle is often just a few pixels from a bond endpoint.
            // Give that endpoint to the constrained atom-drag path instead of
            // scaling the selection and silently changing all its bond lengths.
            ObjectId nearAtom=s.nearestAtom(s.view().toDocument(pixel),8);
            if(nearAtom!=kInvalidObjectId&&atoms.count(nearAtom)){
                s.selected={s.topLevelRef(ObjectRef(ObjectKind::Atom,nearAtom))};
                return false;
            }
            if(hit!=8)return false;
            // Rotating a whole component is length preserving; rotating only
            // part would stretch its boundary bonds, so do not start it.
            for(const auto& b:s.doc.bonds().records())if(b.isAlive()&&
                bool(atoms.count(b.beginAtom))!=bool(atoms.count(b.endAtom))){
                status="键长锁定中：请拖动原子调整键角";invalidate();return true;
            }
        }
    }
    auto points=selectionHandlePoints();transformHandle=hit;transformBox=Box(points[0],points[7]);
    const int opposite[]={7,6,5,4,3,2,1,0};
    transformPivot=hit==8?(transformBox.min+transformBox.max)*.5:points[opposite[hit]];
    transformPress=s.view().toDocument(pixel);transformMatrix=selection::Affine2();transformRefs=s.selected;
    transformOwned=studioio::expanded(s.doc,transformRefs);transformPreviewDocument=0;s.cancel();
#ifdef MOLECULE_STUDIO_DX11
    ++rotationGesture;rotationTargets.clear();rotationGpuEligible=hit==8&&transformOwned.size()>256&&!r62::advanced::readCamera(s.doc).enabled;
    if(rotationGpuEligible)for(const auto& primitive:s.scene.primitives())if(transformOwned.count(primitive.source)){
        if(primitive.text.compare(0,10,"CDXML-EMF:")==0){rotationGpuEligible=false;break;}
        int mode=1;Vec2 anchor;
        if(primitive.source.kind==ObjectKind::Atom){mode=2;anchor=s.doc.atoms().get(primitive.source.id)->position;}
        if(primitive.source.kind==ObjectKind::Text){mode=3;anchor=s.doc.texts().get(primitive.source.id)->position;}
        rotationTargets.emplace_back(primitive.cacheKey,mode,anchor);
    }
#endif
    SetFocus(canvas);transformDragging=true;SetCapture(canvas);updateCanvasCursor();return true;
}
void App::updateSelectionTransform(Vec2 pixel,uint32_t modifiers){
    if(!transformDragging)return;
    auto from=transformPress-transformPivot,to=s.view().toDocument(pixel)-transformPivot;
    if(transformHandle==8){
        double degrees=(std::atan2(to.y,to.x)-std::atan2(from.y,from.x))*180/3.141592653589793;
        if(modifiers&kModShift)degrees=std::round(degrees/15)*15;
        transformMatrix=selection::Affine2::rotation(transformPivot,-degrees);
    }else{
        double factor=1,den=from.x*from.x+from.y*from.y;
        if(transformHandle==1||transformHandle==6){if(std::abs(from.y)>1e-9)factor=to.y/from.y;}
        else if(transformHandle==3||transformHandle==4){if(std::abs(from.x)>1e-9)factor=to.x/from.x;}
        else if(den>1e-9)factor=(to.x*from.x+to.y*from.y)/den;
        factor=std::max(.01,std::min(100.,factor));
        // Chemistry defaults (font and stroke points) stay fixed, as with the
        // numeric scale command. Corners resize the geometry proportionally.
        transformMatrix=selection::Affine2::scaling(transformPivot,factor,factor);
    }
    ++transformRevision;invalidate();
}
void App::finishSelectionTransform(Vec2 pixel,uint32_t modifiers){
    updateSelectionTransform(pixel,modifiers);auto matrix=transformMatrix;auto refs=transformRefs;
    bool moved=core::length(s.view().toDocument(pixel)-transformPress)>s.view().deviceRadiusToDocument(1);
    transformDragging=false;if(GetCapture()==canvas)ReleaseCapture();
    if(moved)error(s.submit(Command("选区缩放/旋转",kInvalidateGeometry,[refs,matrix](CommandContext& c){selection::TransformOptions opts;selection::transformObjects(c.document,refs,matrix,opts);return CommandResult::success();}),"选区缩放/旋转"));
    transformOwned.clear();transformRefs.clear();transformPreview.clear();transformPreviewSlots.clear();transformPreviewDocument=0;updateCanvasCursor();invalidate();
}
void App::cancelSelectionTransform(){
    if(!transformDragging)return;transformDragging=false;transformOwned.clear();transformRefs.clear();transformPreview.clear();transformPreviewSlots.clear();transformPreviewDocument=0;
    if(GetCapture()==canvas)ReleaseCapture();invalidate();
}
const render::Scene& App::selectionTransformScene(){
    const auto& originals=s.scene.primitives();
    if(transformPreviewDocument!=s.doc.id()||transformPreviewSerial!=s.serial||transformPreview.size()!=s.gpuScene.size()){
        transformPreview=s.gpuScene;transformPreviewSlots.clear();
        for(size_t i=0;i<originals.size();++i)if(transformOwned.count(originals[i].source))transformPreviewSlots.push_back(i);
        transformPreviewDocument=s.doc.id();transformPreviewSerial=s.serial;
    }
    const size_t offset=s.gpuScene.size()-originals.size();
    for(size_t i:transformPreviewSlots){
        const auto& original=originals[i];auto& p=transformPreview.mutablePrimitives()[offset+i];p=s.gpuScene.primitives()[offset+i];
        auto matrix=transformMatrix;
        if(original.source.kind==ObjectKind::Atom){auto a=s.doc.atoms().alive(original.source.id);if(a)matrix=selection::Affine2::translation(transformMatrix.apply(a->position)-a->position);}
        if(original.source.kind==ObjectKind::Text){auto t=s.doc.texts().alive(original.source.id);if(t){
            const Vec2 anchor=transformMatrix.apply(t->position);double scale=std::hypot(transformMatrix.m00,transformMatrix.m10);
            if(scale>1e-12&&transformMatrix.determinant()>0){matrix.m00/=scale;matrix.m01/=scale;matrix.m10/=scale;matrix.m11/=scale;matrix.m02=anchor.x-matrix.m00*t->position.x-matrix.m01*t->position.y;matrix.m12=anchor.y-matrix.m10*t->position.x-matrix.m11*t->position.y;}
            else matrix=selection::Affine2::translation(anchor-t->position);
        }}
        for(auto& pt:p.points)pt=matrix.apply(pt);
        for(auto& sub:p.subpaths)for(auto& pt:sub.points)pt=matrix.apply(pt);
        p.bounds=render::geom::computeBounds(p,.6);
        p.version=(p.version^(transformRevision*0x9E3779B97F4A7C15ull)^0xD761CE8D1A23B04Full)|1ull;
    }
    transformPreview.touch();return transformPreview;
}

#endif
