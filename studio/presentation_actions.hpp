#pragma once
#include "presentation_catalog.hpp"
void App::shapeMenu(){
    HMENU menu=CreatePopupMenu();std::map<std::string,HMENU> categories;
    const auto& shapes=presentationCatalog();
    for(size_t i=0;i<shapes.size();++i){const auto& item=shapes[i];if(!categories.count(item.category)){auto sub=CreatePopupMenu();categories[item.category]=sub;studio_i18n::appendMenu(menu,MF_POPUP,UINT_PTR(sub),W(item.category).c_str());}studio_i18n::appendMenu(categories[item.category],MF_OWNERDRAW,100+i,(LPCWSTR)UINT_PTR(10000+i));}
    POINT pt;GetCursorPos(&pt);int chosen=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY,pt.x,pt.y,0,h,nullptr);DestroyMenu(menu);
    if(chosen>=100&&chosen<int(100+shapes.size()))insertAutoShape(chosen-100);
}
bool App::drawShapeItem(DRAWITEMSTRUCT* item){
    if(!item||item->CtlType!=ODT_MENU||item->itemData<10000||item->itemData>=10000+presentationCatalog().size())return false;
    const auto& shape=presentationCatalog()[item->itemData-10000];Graphics g(item->hDC);g.SetSmoothingMode(SmoothingModeAntiAlias);g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);g.TranslateTransform(float(item->rcItem.left),float(item->rcItem.top));g.ScaleTransform(scale,scale);
    float w=(item->rcItem.right-item->rcItem.left)/scale,height=(item->rcItem.bottom-item->rcItem.top)/scale;SolidBrush bg(item->itemState&ODS_SELECTED?Color(255,225,239,232):white);g.FillRectangle(&bg,0.f,0.f,w,height);
    std::vector<PointF> points;for(auto p:shape.points)points.push_back(PointF(12+float(p.x)*35,6+float(p.y)*(height-12)));SolidBrush fill(Color(255,220,239,233));Pen stroke(teal,1.2f);g.FillPolygon(&fill,points.data(),int(points.size()));g.DrawPolygon(&stroke,points.data(),int(points.size()));txt(g,shape.title,RectF(62,0,w-68,height),11);return true;
}
void App::insertAutoShape(size_t index){
    if(index>=presentationCatalog().size())return;
    s.cancel();
    s.activateCommand("chooseSelectionTool_Marquee");
    s.ghost.clear();
    patternShape=int(index);patternPlacing=true;patternDragging=false;patternPreviewPending=false;
    status="插入图案 · 在画布上拖出大小（单击使用默认大小），Esc 取消";
    if(canvas)SetCursor(LoadCursorW(0,MAKEINTRESOURCEW(32515)));
    invalidate();
}
void App::updatePatternPreview(){
    patternPreviewPending=false;
    if(!patternPlacing||patternShape<0||patternShape>=int(presentationCatalog().size()))return;
    s.ghost.clear();
    double width=std::abs(patternEnd.x-patternStart.x),height=std::abs(patternEnd.y-patternStart.y);
    if(width<=0.0||height<=0.0)return;
    Vec2 origin(std::min(patternStart.x,patternEnd.x),std::min(patternStart.y,patternEnd.y));
    const auto& shape=presentationCatalog()[size_t(patternShape)];
    PreviewItem preview;preview.primitive=PreviewPrimitive::Polyline;
    for(auto point:shape.points)preview.points.push_back(origin+Vec2(point.x*width,point.y*height));
    if(!preview.points.empty())preview.points.push_back(preview.points.front());
    preview.style.width=s.graphicsWidth>0?s.graphicsWidth:s.doc.defaultLineWidth();
    preview.style.colorRGBA=s.graphicsColor;
    s.ghost.add(preview);
}
void App::finishPatternPlacement(Vec2 end){
    if(!patternPlacing)return;
    patternEnd=end;int index=patternShape;patternPlacing=false;patternDragging=false;patternPreviewPending=false;patternShape=-1;
    if(GetCapture()==canvas)ReleaseCapture();
    s.ghost.clear();
    if(index<0||index>=int(presentationCatalog().size())){invalidate();return;}
    double width=std::abs(patternEnd.x-patternStart.x),height=std::abs(patternEnd.y-patternStart.y);
    double minimum=s.view().deviceRadiusToDocument(6);
    Vec2 origin;
    if(width<minimum||height<minimum){width=96;height=64;origin=patternStart-Vec2(width/2,height/2);}
    else origin=Vec2(std::min(patternStart.x,patternEnd.x),std::min(patternStart.y,patternEnd.y));
    const auto& shape=presentationCatalog()[size_t(index)];
    PathRecord path;path.closed=true;path.fillEnabled=shapeFillEnabled;path.fillRGBA=shapeFill;path.style.colorRGBA=s.graphicsColor;path.style.width=s.graphicsWidth>0?s.graphicsWidth:s.doc.defaultLineWidth();
    for(auto point:shape.points)path.points.push_back(origin+Vec2(point.x*width,point.y*height));
    auto created=std::make_shared<ObjectId>(kInvalidObjectId);
    auto result=s.submit(Command("插入图案",kInvalidateGeometry|kInvalidateAppearance,[path,created](CommandContext& c){auto e=c.document.addPath(path,created.get());return e.ok()?CommandResult::success():CommandResult::fail(e.code,e.message);}),"插入图案");
    if(result.ok()){s.activateCommand("chooseSelectionTool_Marquee");s.selected={ObjectRef(ObjectKind::Path,*created)};s.refresh();status="图案已插入 · 可拖动角点调整大小，或用选择工具移动、缩放和旋转";}else error(result);
    updateCanvasCursor();invalidate();
}
void App::cancelPatternPlacement(){
    if(!patternPlacing)return;
    patternPlacing=patternDragging=patternPreviewPending=false;patternShape=-1;s.ghost.clear();
    if(GetCapture()==canvas)ReleaseCapture();
    status="已取消插入图案";updateCanvasCursor();invalidate();
}
void App::shapeFillMenu(){
    HMENU menu=CreatePopupMenu();studio_i18n::appendMenu(menu,MF_STRING,1,L"无填充");studio_i18n::appendMenu(menu,MF_STRING,2,L"浅青色");studio_i18n::appendMenu(menu,MF_STRING,3,L"浅蓝色");studio_i18n::appendMenu(menu,MF_STRING,4,L"浅橙色");studio_i18n::appendMenu(menu,MF_STRING,5,L"自定义颜色…");
    POINT pt;GetCursorPos(&pt);int choice=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY,pt.x,pt.y,0,h,nullptr);DestroyMenu(menu);if(!choice)return;
    uint32_t color=choice==3?0xDFEBFAFFu:choice==4?0xFFF0DCFFu:0xE6F2EFFFu;
    if(choice==5){static COLORREF custom[16]={};CHOOSECOLORW cc={};cc.lStructSize=sizeof(cc);cc.hwndOwner=h;cc.lpCustColors=custom;cc.rgbResult=RGB(BYTE(shapeFill>>24),BYTE(shapeFill>>16),BYTE(shapeFill>>8));cc.Flags=CC_FULLOPEN|CC_RGBINIT;if(!studio_i18n::chooseColor(&cc))return;color=(uint32_t(GetRValue(cc.rgbResult))<<24)|(uint32_t(GetGValue(cc.rgbResult))<<16)|(uint32_t(GetBValue(cc.rgbResult))<<8)|255;}
    shapeFill=color;shapeFillEnabled=choice!=1;auto refs=studioio::expanded(s.doc,s.selected);bool enabled=shapeFillEnabled;
    if(!refs.empty())error(s.submit(Command("图案填充",kInvalidateAppearance,[refs,color,enabled](CommandContext& c){for(auto ref:refs)if(ref.kind==ObjectKind::Path){auto path=c.document.paths().mutableGet(ref.id);if(path){path->fillEnabled=enabled;path->fillRGBA=color;c.document.noteTouchedPath(ref.id);}}return CommandResult::success();}),"图案填充"));
    invalidate();
}


