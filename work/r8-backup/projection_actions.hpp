#pragma once
#include "projection_templates.hpp"
void App::armProjection(int kind,int variant){
    if(textEditor.active&&!finishText(true))return;
    try{
        auto source=stereo_projection::make(s.doc,kind,variant);
        if(bioPlacing)cancelBioPlacement();s.cancel();patternPlacing=ringPlacing=radicalPlacing=false;reagentPlacing=false;
        bioSource=source;customPlacement=true;bioPlacing=true;customPlacementName=stereo_projection::name(kind);
        status=customPlacementName+"：点击画布插入 · 双击键端点填写取代基 · Esc 取消";SetFocus(canvas);updateCanvasCursor();invalidate();
    }catch(const std::exception& e){status=e.what();invalidate();}
}
void App::projectionMenu(int kind){
    HMENU menu=CreatePopupMenu();
    if(kind==0){for(int i=1;i<=4;++i){auto title=std::to_wstring(i)+L" 个交叉中心";studio_i18n::appendMenu(menu,MF_STRING,i,title.c_str());}studio_i18n::appendMenu(menu,MF_SEPARATOR,0,nullptr);studio_i18n::appendMenu(menu,MF_STRING,5,L"自定义中心数…");}
    else{studio_i18n::appendMenu(menu,MF_STRING,1,L"交叉式");studio_i18n::appendMenu(menu,MF_STRING,2,L"重叠式");}
    POINT p;GetCursorPos(&p);int choice=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,p.x,p.y,0,h,nullptr);DestroyMenu(menu);if(!choice)return;
    if(kind==0){double count=choice;if(choice==5&&!NumberDialog::show(h,L"费歇尔投影",L"交叉中心数（1–12）",2,&count,1,12))return;armProjection(kind,int(std::round(count)));}
    else armProjection(kind,choice-1);
}

void App::sugarProjectionMenu(){
    HMENU menu=CreatePopupMenu();for(int mirror=0;mirror<2;++mirror){HMENU sub=CreatePopupMenu();for(size_t i=0;i<stereo_projection::sugars().size();++i){auto title=W(std::string(mirror?"L-":"D-")+stereo_projection::sugars()[i].title);studio_i18n::appendMenu(sub,MF_STRING,1+i+mirror*100,title.c_str());}studio_i18n::appendMenu(menu,MF_POPUP,UINT_PTR(sub),mirror?L"L-糖":L"D-糖");}
    POINT p;GetCursorPos(&p);int choice=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,p.x,p.y,0,h,nullptr);DestroyMenu(menu);if(!choice)return;
    if(textEditor.active&&!finishText(true))return;int index=(choice-1)%100;bool mirror=choice>100;
    try{auto source=stereo_projection::sugar(s.doc,index,mirror);if(bioPlacing)cancelBioPlacement();s.cancel();patternPlacing=ringPlacing=radicalPlacing=reagentPlacing=false;bioSource=source;customPlacement=true;bioPlacing=true;customPlacementName=std::string(mirror?"L-":"D-")+stereo_projection::sugars()[index].title+" · 费歇尔投影";status=customPlacementName+"：点击画布插入";SetFocus(canvas);updateCanvasCursor();invalidate();}catch(const std::exception& e){status=e.what();invalidate();}
}
// Projection terminals use the ordinary atom editor and molecular semantics.
bool App::editProjectionEndpoint(Vec2 point,bool probe,Vec2* capture){
    auto atom=s.nearestAtom(point,12);
    if(atom!=kInvalidObjectId&&projection_molecule::coreFor(s.doc,atom)!=kInvalidObjectId){if(capture)*capture=s.doc.atoms().alive(atom)->position;if(!probe)openText(kInvalidObjectId,atom);return true;}

    // Pick existing captions by their actual bounds, including long group names
    // outside the endpoint radius. A group pick otherwise hides its child text.
    double pad=s.view().deviceRadiusToDocument(2);
    for(const auto& g:s.doc.groups().records())if(g.isAlive()&&isProjectionGroupName(g.label))
        for(auto ref:g.members)if(ref.kind==ObjectKind::Text&&s.doc.texts().alive(ref.id)){
            Box box;for(const auto& primitive:s.scene.primitives())if(primitive.source==ref){box.add(primitive.bounds.min);box.add(primitive.bounds.max);}
            if(box.empty())box=selection::objectBounds(s.doc,ref);
            if(point.x>=box.min.x-pad&&point.x<=box.max.x+pad&&point.y>=box.min.y-pad&&point.y<=box.max.y+pad){if(capture)*capture=(box.min+box.max)*.5;if(!probe)openText(ref.id);return true;}
        }
    double best=s.view().deviceRadiusToDocument(12);ObjectId groupId=kInvalidObjectId;Vec2 endpoint,ray;std::vector<Vec2> allTips;
    for(const auto& g:s.doc.groups().records())if(g.isAlive()&&isProjectionGroupName(g.label)){
        std::vector<Vec2> groupTips;bool chosen=false;std::vector<std::pair<Vec2,Vec2>> lines;Vec2 circle;double radius=0;
        for(auto ref:g.members)if(ref.kind==ObjectKind::Path){auto p=s.doc.paths().alive(ref.id);if(!p)continue;if(!p->closed&&p->points.size()==2)lines.push_back({p->points[0],p->points[1]});else if(p->closed){Box bounds;for(auto v:p->points)bounds.add(v);double r=std::max(bounds.max.x-bounds.min.x,bounds.max.y-bounds.min.y)*.5;if(r>radius){radius=r;circle=(bounds.min+bounds.max)*.5;}}}
        for(size_t i=0;i<lines.size();++i)for(int end=0;end<2;++end){auto tip=end?lines[i].second:lines[i].first,from=end?lines[i].first:lines[i].second;bool terminal=true;
            if(radius>0&&length(tip-circle)<=radius*1.12)continue;
            for(size_t j=0;j<lines.size();++j)if(j!=i){auto a=lines[j].first,v=lines[j].second-a;double t=std::max(0.,std::min(1.,dot(tip-a,v)/std::max(1e-9,lengthSq(v))));if(length(tip-a-v*t)<s.doc.defaultBondLength()*.025){terminal=false;break;}}
            if(terminal)groupTips.push_back(tip);if(!terminal||length(tip-point)>=best)continue;best=length(tip-point);endpoint=tip;ray=(tip-from)*(1/std::max(1e-9,length(tip-from)));groupId=g.id;chosen=true;
        }
        if(chosen)allTips=groupTips;
    }
    if(groupId==kInvalidObjectId)return false;
    if(capture)*capture=endpoint;if(probe)return true;
    // Editing an occupied endpoint must reuse its label, not stack another one.
    auto g=s.doc.groups().alive(groupId);ObjectId existing=kInvalidObjectId;double gap=s.doc.defaultBondLength()*.8;
    for(auto ref:g->members)if(ref.kind==ObjectKind::Text&&s.doc.texts().alive(ref.id)){auto box=selection::objectBounds(s.doc,ref);auto middle=(box.min+box.max)*.5;double distance=length(middle-endpoint);bool nearest=true;for(auto tip:allTips)if(length(middle-tip)+.001<distance)nearest=false;if(nearest&&distance<gap){gap=distance;existing=ref.id;}}
    if(existing!=kInvalidObjectId){openText(existing);return true;}
    double em=s.doc.defaultTextSize();Vec2 at=endpoint+ray*(s.doc.defaultBondLength()*.2);at.y+=ray.y<-.65?0:ray.y>.65?em*.72:em*.32;
    // Separate the two labels of an eclipsed pair along their outward tangents.
    if(g->label=="Newman projection"){double closest=s.doc.defaultBondLength()*.5;Vec2 partner;bool paired=false;for(auto tip:allTips){double d=length(tip-endpoint);if(d>.001&&d<closest){closest=d;partner=tip;paired=true;}}if(paired)at=at+(endpoint-partner)*(em*.5/closest);}
    uint8_t align=uint8_t(ray.x>.3?TextAlign::Left:ray.x<-.3?TextAlign::Right:TextAlign::Center);
    auto created=std::make_shared<ObjectId>(kInvalidObjectId);
    auto e=s.submit(Command("投影取代基",kInvalidateGeometry|kInvalidateAppearance,[created,groupId,at,em,align](CommandContext& c){auto e=c.document.addText(at,em," ",created.get());if(!e.ok())return CommandResult::fail(e.code,e.message);TextFormat f;f.fontName=c.document.defaultFont();f.size=em;f.align=align;c.document.setTextFormat(*created,f);auto ref=ObjectRef(ObjectKind::Text,*created);c.document.extensions(ref).set("pst.attr.InterpretChemically","no");auto group=c.document.groups().mutableGet(groupId);group->members.push_back(ref);c.document.noteTouchedGroup(groupId);return CommandResult::success();}),"投影取代基");
    if(!e.ok()){error(e);return true;}openText(*created);textEditor.select(0,-1);return true;
}

// Use the editing resolver for capture feedback: preview and click cannot disagree.
bool App::projectionCapture(Vec2 pixel,Vec2* capture){
    if(!pointerOverCanvas||textEditor.active||down||pan||cloneDragging||transformDragging||wrapDragging||tableDragPending||perspectiveTool||bioPlacing||ringPlacing||patternPlacing||radicalPlacing||virtualBondTool||phenalenePlacing||equivalencePlacement||partialChargePlacement)return false;
    const auto& tool=s.entries[s.activeIndex];
    if(tool.command.find("Eraser")!=std::string::npos)return false;
    return editProjectionEndpoint(s.view().toDocument(pixel),true,capture);
}
void App::appendProjectionCapture(render::OverlayScene& overlay){
    Vec2 point;if(!projectionCapture(pointerPixel,&point))return;
    render::Primitive ring;ring.kind=render::PrimKind::Polyline;ring.style.stroke=render::Paint(0x2563EBFF,true);ring.style.fill.enabled=false;ring.style.strokeWidth=s.view().deviceRadiusToDocument(2.5);ring.layer=render::LayerId(900);ring.z=100;
    double radius=s.view().deviceRadiusToDocument(9);
    for(int i=0;i<=40;++i)ring.points.push_back(point+Vec2(std::cos(i*6.283185307/40),std::sin(i*6.283185307/40))*radius);
    ring.bounds=render::geom::computeBounds(ring,.6);auto halo=ring;halo.style.stroke.rgba=0xFFFFFFFF;halo.style.strokeWidth=s.view().deviceRadiusToDocument(5);halo.bounds=render::geom::computeBounds(halo,.6);overlay.scene().add(halo);overlay.scene().add(ring);
}

ObjectId App::projectionAxis(Vec2 point){
    auto eligible=[&](const BondRecord& b){return b.order==BondOrder::Single&&s.doc.atoms().alive(b.beginAtom)->element==6&&s.doc.atoms().alive(b.endAtom)->element==6&&(projection_molecule::marked(s.doc,b.beginAtom)||projection_molecule::marked(s.doc,b.endAtom));};
    auto pick=s.pick(point);if(pick.kind==ObjectKind::Bond){auto b=s.doc.bonds().alive(pick.id);if(b&&eligible(*b))return b->id;}
    std::set<ObjectId> scope;auto refs=studioio::expanded(s.doc,s.selected);for(auto ref:refs)if(ref.kind==ObjectKind::Atom)scope.insert(ref.id);else if(ref.kind==ObjectKind::Bond){auto b=s.doc.bonds().alive(ref.id);if(b){scope.insert(b->beginAtom);scope.insert(b->endAtom);}}
    if(pick.kind==ObjectKind::Atom){for(auto id:projection_molecule::component(s.doc,pick.id))scope.insert(id);}
    ObjectId fallback=kInvalidObjectId;double distance=1e100;
    for(const auto& b:s.doc.bonds().records())if(b.isAlive()&&eligible(b)&&(scope.count(b.beginAtom)||scope.count(b.endAtom))){auto p=(s.doc.atoms().alive(b.beginAtom)->position+s.doc.atoms().alive(b.endAtom)->position)*.5;double score=length(p-point)+(projection_molecule::axis(s.doc,b)?0:10000);if(score<distance){distance=score;fallback=b.id;}}
    return fallback;
}
void App::convertProjection(ObjectId axis,int kind,int variant){
    if(textEditor.active&&!finishText(true))return;auto e=s.submit(Command("转换立体投影",kInvalidateStructure|kInvalidateGeometry|kInvalidateAppearance|kInvalidateSemantics,[axis,kind,variant](CommandContext& c){auto e=projection_molecule::convert(c.document,axis,kind,variant);return e.ok()?CommandResult::success():CommandResult::fail(e.code,e.message);}),"转换立体投影");error(e);if(e.ok())status=std::string("已转换为")+stereo_projection::name(kind)+" · 分子连接关系保持不变";invalidate();
}
