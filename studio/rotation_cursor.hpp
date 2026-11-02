#pragma once
#include "rotation_glyph.hpp"
static HCURSOR rotationPointer(UINT dpi){
    struct Cursors {std::map<int,HCURSOR> values;~Cursors(){for(auto& item:values)if(item.second)DestroyCursor(item.second);}};
    static Cursors cache;int size=std::max(24,MulDiv(24,int(dpi?dpi:96),96));auto old=cache.values.find(size);if(old!=cache.values.end())return old->second;
    Bitmap bitmap(size,size,PixelFormat32bppARGB);{Graphics g(&bitmap);g.Clear(Color(0,0,0,0));g.ScaleTransform(size/24.f,size/24.f);g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.TranslateTransform(10.f,11.f);Pen ink(Color(104,104,104),1.8f);ink.SetLineJoin(LineJoinRound);
        for(double radius:{9.,5.}){auto points=studio_rotation::arc(radius);std::vector<PointF> line;for(auto p:points)line.push_back(PointF(float(p.x),float(p.y)));g.DrawLines(&ink,line.data(),int(line.size()));}
        auto points=studio_rotation::arrow();std::vector<PointF> arrow;for(auto p:points)arrow.push_back(PointF(float(p.x),float(p.y)));SolidBrush fill(Color(104,104,104));g.FillPolygon(&fill,arrow.data(),int(arrow.size()));

    }
    HICON icon=nullptr;bitmap.GetHICON(&icon);ICONINFO info={};HCURSOR cursor=nullptr;if(icon&&GetIconInfo(icon,&info)){info.fIcon=FALSE;info.xHotspot=MulDiv(10,size,24);info.yHotspot=MulDiv(11,size,24);cursor=HCURSOR(CreateIconIndirect(&info));DeleteObject(info.hbmColor);DeleteObject(info.hbmMask);}if(icon)DestroyIcon(icon);cache.values[size]=cursor;return cursor?cursor:LoadCursorW(0,MAKEINTRESOURCEW(32649));
}
