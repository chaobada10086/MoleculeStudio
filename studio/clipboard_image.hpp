#pragma once
#include <gdiplus.h>
#include <objidl.h>
#include <memory>

namespace clipboard_image {
inline bool openClipboard(HWND owner){
    for(int attempt=0;attempt<4;++attempt){
        if(OpenClipboard(owner))return true;
        if(attempt<3)Sleep(4);
    }
    return false;
}
inline bool available(){return IsClipboardFormatAvailable(RegisterClipboardFormatW(L"PNG"))||IsClipboardFormatAvailable(CF_DIBV5)||IsClipboardFormatAvailable(CF_DIB)||IsClipboardFormatAvailable(CF_BITMAP);}
struct Stream {
    IStream* value=nullptr;
    ~Stream(){if(value)value->Release();}
    bool load(const std::vector<BYTE>& bytes){
        if(FAILED(CreateStreamOnHGlobal(nullptr,TRUE,&value)))return false;
        ULONG written=0;if(FAILED(value->Write(bytes.data(),ULONG(bytes.size()),&written))||written!=bytes.size())return false;
        LARGE_INTEGER zero={};return SUCCEEDED(value->Seek(zero,STREAM_SEEK_SET,nullptr));
    }
};
inline std::vector<BYTE> data(UINT format){
    HANDLE handle=GetClipboardData(format);if(!handle)return {};
    SIZE_T size=GlobalSize(handle);if(!size||size>128*1024*1024)return {};
    auto p=static_cast<const BYTE*>(GlobalLock(handle));if(!p)return {};
    std::vector<BYTE> result(p,p+size);GlobalUnlock(handle);return result;
}
// Read under one clipboard lock, then own the decoded pixels independently.
inline std::unique_ptr<Gdiplus::Bitmap> decode(HWND owner){
    if(!clipboard_image::openClipboard(owner))return nullptr;
    struct Close {~Close(){CloseClipboard();}} close;
    for(UINT format:{RegisterClipboardFormatW(L"PNG"),UINT(CF_DIBV5),UINT(CF_DIB)}){
        auto bytes=data(format);if(bytes.empty())continue;
        if(format==CF_DIB||format==CF_DIBV5){
            if(bytes.size()<sizeof(BITMAPINFOHEADER))continue;
            BITMAPINFOHEADER h={};memcpy(&h,bytes.data(),sizeof(h));
            if(h.biSize<sizeof(h)||h.biSize>bytes.size()||h.biWidth<=0||h.biWidth>16000||h.biHeight==0||h.biHeight< -16000||h.biHeight>16000||h.biPlanes!=1||uint64_t(h.biWidth)*std::abs(h.biHeight)>32*1024*1024)continue;
            size_t colors=h.biClrUsed?h.biClrUsed:(h.biBitCount<=8?size_t(1)<<h.biBitCount:0);
            if(colors>256)continue;
            size_t offset=h.biSize+colors*sizeof(RGBQUAD);
            if(h.biSize==40&&(h.biCompression==BI_BITFIELDS||h.biCompression==6))offset+=(h.biCompression==6?4:3)*sizeof(DWORD);
            if(offset>bytes.size())continue;
            // DIBV5 screenshots may carry alpha; ordinary BI_RGB DIB alpha is reserved.
            bool standardMasks=h.biCompression==BI_RGB;
            if(h.biCompression==BI_BITFIELDS&&bytes.size()>=52){DWORD masks[3];memcpy(masks,bytes.data()+40,12);standardMasks=masks[0]==0xff0000&&masks[1]==0xff00&&masks[2]==0xff;}
            if(h.biBitCount==32&&standardMasks){
                size_t row=size_t(h.biWidth)*4,height=size_t(std::abs(h.biHeight));
                if(row*height>bytes.size()-offset||row*height>128*1024*1024)continue;
                DWORD alphaMask=0;if(h.biSize>=56)memcpy(&alphaMask,bytes.data()+52,4);
                auto bitmap=std::unique_ptr<Gdiplus::Bitmap>(new Gdiplus::Bitmap(h.biWidth,int(height),PixelFormat32bppARGB));
                Gdiplus::Rect rect(0,0,h.biWidth,int(height));Gdiplus::BitmapData pixels={};
                if(bitmap->LockBits(&rect,Gdiplus::ImageLockModeWrite,PixelFormat32bppARGB,&pixels)!=Gdiplus::Ok)continue;
                for(size_t y=0;y<height;++y){auto dst=static_cast<BYTE*>(pixels.Scan0)+y*pixels.Stride;memcpy(dst,bytes.data()+offset+(h.biHeight>0?height-1-y:y)*row,row);if(alphaMask!=0xff000000)for(int x=0;x<h.biWidth;++x)dst[x*4+3]=255;}
                bitmap->UnlockBits(&pixels);return bitmap;
            }
            BITMAPFILEHEADER file={};file.bfType=0x4d42;file.bfSize=DWORD(sizeof(file)+bytes.size());file.bfOffBits=DWORD(sizeof(file)+offset);
            bytes.insert(bytes.begin(),sizeof(file),0);memcpy(bytes.data(),&file,sizeof(file));
        }else{
            // Bound PNG dimensions before asking GDI+ to allocate its pixel buffer.
            const BYTE signature[]={137,80,78,71,13,10,26,10};if(bytes.size()<24||memcmp(bytes.data(),signature,8)!=0)continue;
            auto dimension=[&](size_t n){return (uint32_t(bytes[n])<<24)|(uint32_t(bytes[n+1])<<16)|(uint32_t(bytes[n+2])<<8)|bytes[n+3];};
            auto w=dimension(16),h=dimension(20);if(!w||!h||w>16000||h>16000||uint64_t(w)*h>32*1024*1024)continue;
        }
        Stream stream;if(!stream.load(bytes))continue;
        std::unique_ptr<Gdiplus::Bitmap> image(Gdiplus::Bitmap::FromStream(stream.value));
        if(image&&image->GetLastStatus()==Gdiplus::Ok&&image->GetWidth()&&image->GetHeight()){
            auto copy=std::unique_ptr<Gdiplus::Bitmap>(image->Clone(0,0,INT(image->GetWidth()),INT(image->GetHeight()),PixelFormat32bppARGB));
            if(copy&&copy->GetLastStatus()==Gdiplus::Ok)return copy;
        }
    }
    HBITMAP handle=static_cast<HBITMAP>(GetClipboardData(CF_BITMAP));BITMAP info={};
    if(handle&&GetObjectW(handle,sizeof(info),&info)&&info.bmWidth>0&&info.bmHeight>0&&uint64_t(info.bmWidth)*info.bmHeight<=32*1024*1024){
        std::unique_ptr<Gdiplus::Bitmap> image(Gdiplus::Bitmap::FromHBITMAP(handle,nullptr));
        if(image&&image->GetLastStatus()==Gdiplus::Ok)return image;
    }
    return nullptr;
}
// Reuse the existing embedded-object path: pixels travel with native/CDXML
// documents, participate in selection transforms, and use the same undo stack.
inline bool read(HWND owner,double maxWidth,double maxHeight,r62::core::Vec2 center,std::string& xml){
    auto bitmap=decode(owner);if(!bitmap)return false;
    HDC dc=GetDC(nullptr);Gdiplus::RectF frame(0,0,float(bitmap->GetWidth()),float(bitmap->GetHeight()));
    Gdiplus::Metafile metafile(dc,frame,Gdiplus::MetafileFrameUnitPixel,Gdiplus::EmfTypeEmfPlusDual);ReleaseDC(nullptr,dc);
    if(metafile.GetLastStatus()!=Gdiplus::Ok)return false;
    {Gdiplus::Graphics graphics(&metafile);if(graphics.DrawImage(bitmap.get(),frame,0,0,frame.Width,frame.Height,Gdiplus::UnitPixel)!=Gdiplus::Ok)return false;}
    HENHMETAFILE handle=metafile.GetHENHMETAFILE();if(!handle)return false;
    UINT size=GetEnhMetaFileBits(handle,0,nullptr);std::vector<BYTE> bytes(size);bool ok=size&&GetEnhMetaFileBits(handle,size,bytes.data())==size;DeleteEnhMetaFile(handle);if(!ok)return false;
    static const char hex[]="0123456789ABCDEF";std::string encoded;encoded.reserve(size*2);for(auto b:bytes){encoded+=hex[b>>4];encoded+=hex[b&15];}
    double w=bitmap->GetWidth()*.75,h=bitmap->GetHeight()*.75;
    double factor=std::min(1.,std::min(maxWidth/w,maxHeight/h));w*=factor;h*=factor;
    std::ostringstream out;out.precision(12);out<<"<CDXML><page><embeddedobject id=\"1\" MoleculeStudioRaster=\"yes\" BoundingBox=\""<<center.x-w*.5<<' '<<center.y-h*.5<<' '<<center.x+w*.5<<' '<<center.y+h*.5<<"\" EnhancedMetafile=\""<<encoded<<"\"/></page></CDXML>";xml=out.str();return true;
}
}
