#ifndef STUDIO_MARQUEE_PREVIEW_HPP
#define STUDIO_MARQUEE_PREVIEW_HPP
// Freeze the document pixels during selection. No hit tests, chemical analysis,
// primitive hashing or GPU readback is needed for subsequent pointer frames.
bool App::renderMarquee(){
    if(!gl||!canvas)return false;
    RECT client;GetClientRect(canvas,&client);int w=client.right,h=client.bottom;
    if(w<=0||h<=0)return false;
    auto& frame=marqueeBackdrop;
    if(!frame.valid||frame.width!=w||frame.height!=h||frame.serial!=s.serial||frame.viewSerial!=s.viewSerial){
        frame.clear();auto preview=s.ghost;s.ghost.clear();marqueeBuilding=true;
        render();marqueeBuilding=false;s.ghost=std::move(preview);
        HDC surface=GetDC(canvas);frame.memory=CreateCompatibleDC(surface);frame.bitmap=CreateCompatibleBitmap(surface,w,h);
        if(frame.memory&&frame.bitmap){frame.previous=SelectObject(frame.memory,frame.bitmap);
#ifdef MOLECULE_STUDIO_DX11
            if(dx11Renderer)frame.valid=dx11Renderer->present(frame.memory);else
#endif
            if(compatibleRenderer)frame.valid=compatibleRenderer->present(frame.memory);
            else frame.valid=BitBlt(frame.memory,0,0,w,h,surface,0,0,SRCCOPY)!=FALSE;
        }
        ReleaseDC(canvas,surface);
        if(!frame.valid){frame.clear();return false;}
        frame.width=w;frame.height=h;frame.serial=s.serial;frame.viewSerial=s.viewSerial;++marqueeBackgroundBuilds;
    }
    HDC surface=GetDC(canvas);BitBlt(surface,0,0,w,h,frame.memory,0,0,SRCCOPY);
    const auto& points=s.selectionDragPoints();
    if(points.size()>1){
        HPEN pen=CreatePen(PS_DOT,1,RGB(70,140,205));auto oldPen=SelectObject(surface,pen);auto oldBrush=SelectObject(surface,GetStockObject(HOLLOW_BRUSH));int oldMode=SetBkMode(surface,TRANSPARENT);
        if(s.entries[s.activeIndex].command.find("Lasso")!=std::string::npos){
            std::vector<POINT> device;device.reserve(points.size()+1);for(auto p:points){auto d=s.view().toDevice(p);device.push_back(POINT{LONG(std::lround(d.x)),LONG(std::lround(d.y))});}device.push_back(device.front());Polyline(surface,device.data(),int(device.size()));
        }else{
            auto a=s.view().toDevice(points.front()),b=s.view().toDevice(points.back());
            Rectangle(surface,int(std::lround(std::min(a.x,b.x))),int(std::lround(std::min(a.y,b.y))),int(std::lround(std::max(a.x,b.x))),int(std::lround(std::max(a.y,b.y))));
        }
        SetBkMode(surface,oldMode);SelectObject(surface,oldBrush);SelectObject(surface,oldPen);DeleteObject(pen);
    }
    ReleaseDC(canvas,surface);++marqueeFastFrames;return true;
}
#endif
