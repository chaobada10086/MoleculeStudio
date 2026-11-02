#ifndef STUDIO_TEXT_PREVIEW_HPP
#define STUDIO_TEXT_PREVIEW_HPP
bool App::renderTextBackdrop(){
    if(!gl||!canvas)return false;
    RECT client;GetClientRect(canvas,&client);int w=client.right,height=client.bottom;
    if(w<=0||height<=0)return false;
    auto& frame=textBackdrop;
    if(!frame.valid||frame.width!=w||frame.height!=height||frame.serial!=s.serial||frame.viewSerial!=s.viewSerial){
        frame.clear();textBackdropBuilding=true;render();textBackdropBuilding=false;
        HDC surface=GetDC(canvas);frame.memory=CreateCompatibleDC(surface);frame.bitmap=CreateCompatibleBitmap(surface,w,height);
        if(frame.memory&&frame.bitmap){frame.previous=SelectObject(frame.memory,frame.bitmap);
#ifdef MOLECULE_STUDIO_DX11
            if(dx11Renderer)frame.valid=dx11Renderer->present(frame.memory);else
#endif
            if(compatibleRenderer)frame.valid=compatibleRenderer->present(frame.memory);
            else frame.valid=BitBlt(frame.memory,0,0,w,height,surface,0,0,SRCCOPY)!=FALSE;
        }
        ReleaseDC(canvas,surface);
        if(!frame.valid){frame.clear();return false;}
        frame.width=w;frame.height=height;frame.serial=s.serial;frame.viewSerial=s.viewSerial;++textBackgroundBuilds;
    }
    HDC surface=GetDC(canvas);BitBlt(surface,0,0,w,height,frame.memory,0,0,SRCCOPY);ReleaseDC(canvas,surface);return true;
}
#endif
