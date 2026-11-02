#pragma once

// Original utility glyphs on a 28-unit grid. Conventional alignment lines,
// object outlines and direction arrows remain readable at ribbon icon size.
bool utilityGlyph(Graphics& g,const std::string& id,Color color){
    Pen stroke(color,1.8f);stroke.SetStartCap(LineCapFlat);stroke.SetEndCap(LineCapFlat);stroke.SetLineJoin(LineJoinMiter);
    SolidBrush fill(color);Pen guide(color,1.2f);
    auto line=[&](float x,float y,float u,float v){g.DrawLine(&stroke,x,y,u,v);};
    if(id.compare(0,9,"textalign")==0){
        int n=atoi(id.substr(9).c_str());
        for(int i=0;i<4;++i){float width=n==3||i%2==0?20.f:13.f,x=n==1?14-width*.5f:n==2?24-width:4.f;line(x,5+i*6,x+width,5+i*6);}return true;
    }
    if(id.compare(0,5,"align")==0){
        int n=atoi(id.substr(5).c_str());if(n>=3){g.TranslateTransform(28,0);g.RotateTransform(90);n-=3;}
        float axis=n==0?3:n==1?14:25;g.DrawLine(&guide,axis,2.f,axis,26.f);
        for(int i=0;i<3;++i){float width=i==1?12.f:18.f,x=n==0?7:n==1?14-width*.5f:21-width;line(x,6+i*8,x+width,6+i*8);}return true;
    }
    if(id=="distH"||id=="distV"){
        if(id=="distV"){g.TranslateTransform(28,0);g.RotateTransform(90);}
        for(int i=0;i<3;++i)g.DrawRectangle(&stroke,float(3+i*9),i==1?8.f:5.f,4.f,i==1?12.f:18.f);return true;
    }
    if(id=="group"||id=="ungroup"){
        if(id=="group"){
            g.DrawRectangle(&guide,2,2,24,24);g.DrawRectangle(&stroke,6,6,9,9);g.DrawRectangle(&stroke,13,13,9,9);
        }else{
            g.DrawRectangle(&stroke,3,3,9,9);g.DrawRectangle(&stroke,16,16,9,9);
            line(16,10,23,3);line(18,3,23,3);line(23,3,23,8);
        }return true;
    }
    if(id=="merge"){
        PointF shape[]={{3,4},{17,4},{17,11},{25,11},{25,24},{11,24},{11,17},{3,17}};
        g.DrawPolygon(&stroke,shape,8);return true;
    }
    if(id=="rotate"){
        g.DrawArc(&stroke,4,4,20,20,15,295);line(20,3,24,8);line(24,8,17,8);return true;
    }
    if(id=="flipH"||id=="flipV"){
        if(id=="flipV"){g.TranslateTransform(28,0);g.RotateTransform(90);}
        for(int y=3;y<26;y+=5)g.DrawLine(&guide,14.f,float(y),14.f,float(y+2));
        PointF left[]={{3,7},{10,14},{3,21}},right[]={{25,7},{18,14},{25,21}};
        g.DrawPolygon(&stroke,left,3);g.DrawPolygon(&stroke,right,3);return true;
    }
    if(id=="front"||id=="back"||id=="layerUp"||id=="layerDown"){
        if(id=="front"){g.DrawRectangle(&guide,3,3,14,14);g.FillRectangle(&fill,11,11,14,14);}
        else if(id=="back"){g.FillRectangle(&fill,3,3,14,14);g.DrawRectangle(&stroke,11,11,14,14);}
        else{g.DrawRectangle(&guide,2,3,12,12);g.DrawRectangle(&stroke,7,11,12,12);bool up=id=="layerUp";line(24,up?23:3,24,up?3:23);line(24,up?3:23,21,up?7:19);line(24,up?3:23,27,up?7:19);}return true;
    }
    if(id=="sizeUp"||id=="sizeDown"){
        line(4,22,10,6);line(10,6,16,22);line(6.5f,16,13.5f,16);
        line(19,11,26,11);if(id=="sizeUp")line(22.5f,7.5f,22.5f,14.5f);return true;
    }
    if(id=="windowMin"){line(8,18,20,18);return true;}
    if(id=="windowMax"){g.DrawRectangle(&stroke,7,7,14,14);return true;}
    if(id=="windowRestore"){g.DrawRectangle(&stroke,6,10,12,12);line(10,6,22,6);line(22,6,22,18);return true;}
    if(id=="windowClose"){line(8,8,20,20);line(20,8,8,20);return true;}
    return false;
}

void arrowPreview(Graphics& g,uint32_t presetId,RectF rect,Color color,double width=0,double baseWidth=.6){
    auto params=arrows::resolvePreset(presetId,Vec2(0,0),Vec2(72,0),baseWidth);params.strokeWidth=width;
    arrows::ArrowGeometry geometry;arrows::Tessellation tess;tess.tolerance=.15;
    if(!arrows::buildArrowGeometry(params,baseWidth,tess,&geometry).ok()||!geometry.valid)return;
    auto extent=geometry.bounds.max-geometry.bounds.min,center=(geometry.bounds.min+geometry.bounds.max)*.5;
    float factor=float(std::min((rect.Width-3)/std::max(1.,extent.x),(rect.Height-3)/std::max(1.,extent.y)));
    auto point=[&](Vec2 v){return PointF(rect.X+rect.Width*.5f+float(v.x-center.x)*factor,rect.Y+rect.Height*.5f+float(v.y-center.y)*factor);};
    bool large=rect.Width>100;Pen stroke(color,std::max(large?1.25f:.85f,float(geometry.strokeWidth)*factor));stroke.SetLineJoin(LineJoinRound);SolidBrush fill(color);
    auto contour=[&](const std::vector<Vec2>& line){std::vector<PointF> points;for(auto p:line)points.push_back(point(p));if(points.size()>1)g.DrawLines(&stroke,points.data(),int(points.size()));};
    if(geometry.dashOn>0){REAL dashes[]={float(geometry.dashOn/geometry.strokeWidth),float(geometry.dashOff/geometry.strokeWidth)};stroke.SetDashPattern(dashes,2);}
    for(const auto& shaft:geometry.shafts)contour(shaft);stroke.SetDashStyle(DashStyleSolid);
    // Keep arrowhead semantics readable in small toolbar/menu swatches.
    for(const auto& head:geometry.heads){float boost=std::max(1.f,(large?6.f:3.f)/std::max(.1f,float(params.head.length)*factor));auto hp=[&](Vec2 p){return point(head.tip+(p-head.tip)*boost);};
        std::vector<PointF> outline;for(auto p:head.outline)outline.push_back(hp(p));if(outline.size()>2){if(head.filled)g.FillPolygon(&fill,outline.data(),int(outline.size()));else g.DrawPolygon(&stroke,outline.data(),int(outline.size()));}
        for(size_t i=1;i<head.strokes.size();i+=2)g.DrawLine(&stroke,hp(head.strokes[i-1]),hp(head.strokes[i]));}
    for(const auto& marker:geometry.markers)contour(marker);
}
