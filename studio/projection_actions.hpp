#pragma once
#include "projection_templates.hpp"
#include "projection_session.hpp"
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
    const auto endpoint = studio_projection::endpointAt(s, point);
    if (!endpoint.found()) return false;
    if (capture) *capture = endpoint.capture;
    if (probe) return true;
    if (endpoint.atom != kInvalidObjectId) openText(kInvalidObjectId, endpoint.atom);
    else if (endpoint.text != kInvalidObjectId) openText(endpoint.text);
    else {
        auto created = std::make_shared<ObjectId>(kInvalidObjectId);
        const auto result = s.submit(studio_projection::addEndpointCaption(endpoint, " ", created), "投影取代基");
        if (!result.ok()) error(result);
        else { openText(*created); textEditor.select(0, -1); }
    }
    return true;
}

// Use the editing resolver for capture feedback: preview and click cannot disagree.
bool App::projectionCapture(Vec2 pixel,Vec2* capture){
    if(!pointerOverCanvas||textEditor.active||down||pan||cloneDragging||transformDragging||wrapDragging||tableDragPending||perspectiveTool||bioPlacing||ringPlacing||patternPlacing||radicalPlacing||virtualBondTool||phenalenePlacing||equivalencePlacement||partialChargePlacement)return false;
    const auto& tool=s.entries[s.activeIndex];
    if(tool.command.find("Eraser")!=std::string::npos)return false;
    return editProjectionEndpoint(s.view().toDocument(pixel),true,capture);
}
void App::appendProjectionCapture(render::OverlayScene& overlay){
    Vec2 point;
    if (projectionCapture(pointerPixel, &point)) studio_projection::highlightEndpoint(overlay, s.view(), point);
}

ObjectId App::projectionAxis(Vec2 point){
    return studio_projection::axisAt(s, point);
}
void App::convertProjection(ObjectId axis,int kind,int variant){
    if(textEditor.active&&!finishText(true))return;auto e=studio_projection::convert(s,axis,kind,variant);error(e);if(e.ok())status=std::string("已转换为")+stereo_projection::name(kind)+" · 分子连接关系保持不变";invalidate();
}
