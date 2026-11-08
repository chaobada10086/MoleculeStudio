#ifndef STUDIO_COMPATIBLE_RENDERER_HPP
#define STUDIO_COMPATIBLE_RENDERER_HPP
#include "next/render/batcher.hpp"
#include "embedded_images.hpp"

// The VMware vm3dmp crash bucket is a kernel driver failure. Do not create an
// OpenGL context on that adapter: even a successful GL call can trigger it.
// Keep the existing mesh cache, culling, projection and colour batching. Only
// rasterization changes, using a reusable DIB and one BitBlt to present it.
namespace compatible {
inline bool vmwareAdapter(){
    DISPLAY_DEVICEW device={};device.cb=sizeof(device);
    for(DWORD i=0;EnumDisplayDevicesW(nullptr,i,&device,0);++i){
        if(device.StateFlags&DISPLAY_DEVICE_ATTACHED_TO_DESKTOP){
            std::wstring name=device.DeviceString,id=device.DeviceID;
            for(auto& c:name)c=towlower(c);for(auto& c:id)c=towlower(c);
            if(name.find(L"vmware")!=std::wstring::npos||id.find(L"ven_15ad")!=std::wstring::npos)return true;
        }
        device.cb=sizeof(device);
    }
    return false;
}
class Renderer : public r62::render::Backend {
    HDC memory_=nullptr;HBITMAP dib_=nullptr;HGDIOBJ old_=nullptr;void* pixels_=nullptr;
    std::unique_ptr<Gdiplus::Bitmap> image_;
    int width_=0,height_=0;bool ready_=false;
    // Offscreen PNG snapshots need the drawing alone on a transparent surface.
    // The interactive canvas keeps the default white paper.
    bool transparentBackground_=false;
    r62::render::Batcher content_,overlay_;
    r62::render::Scene foreground_;
    std::unique_ptr<Gdiplus::Bitmap> vectorImage_;
    uint64_t vectorKey_=0,geometryKey_=0;bool vectorValid_=false,geometryValid_=false;
    const r62::render::Scene* contentScene_=nullptr;
    const r62::render::Scene* overlayScene_=nullptr;
    void releaseImage(){vectorImage_.reset();vectorValid_=false;GdiFlush();image_.reset();if(memory_&&old_)SelectObject(memory_,old_);if(dib_)DeleteObject(dib_);if(memory_)DeleteDC(memory_);memory_=nullptr;dib_=nullptr;old_=nullptr;pixels_=nullptr;width_=height_=0;}
    bool resize(int w,int h){
        if(w==width_&&h==height_&&image_)return true;
        if(w<=0||h<=0||uint64_t(w)*uint64_t(h)>64ull*1024*1024)return false;
        releaseImage();BITMAPINFO info={};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=w;info.bmiHeader.biHeight=-h;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
        memory_=CreateCompatibleDC(nullptr);if(!memory_)return false;
        dib_=CreateDIBSection(memory_,&info,DIB_RGB_COLORS,&pixels_,nullptr,0);
        if(!dib_){releaseImage();return false;}old_=SelectObject(memory_,dib_);
        image_.reset(new Gdiplus::Bitmap(w,h,w*4,PixelFormat32bppPARGB,static_cast<BYTE*>(pixels_)));
        if(image_->GetLastStatus()!=Gdiplus::Ok){releaseImage();return false;}
        width_=w;height_=h;return true;
    }
    struct ContourOrder{uint64_t version=0;bool valid=false;std::vector<bool> reversed;};
    std::vector<ContourOrder> contourOrder_;
    void submit(Gdiplus::Graphics& g,const r62::render::Batch& batch,r62::render::RenderStats* stats,const r62::render::Scene* scene=nullptr,const r62::render::View* view=nullptr){
        using namespace r62::render;using r62::core::Vec2;
        for(size_t i=0;i<batch.triangleRanges.size();){
            uint32_t rgba=resolvePaint(batch.triangleRanges[i].rgba);
            std::vector<Gdiplus::PointF> points;std::vector<BYTE> types;
            for(;i<batch.triangleRanges.size()&&resolvePaint(batch.triangleRanges[i].rgba)==rgba;++i){
                const auto& run=batch.triangleRanges[i];bool direct=false;
                if(scene&&view&&run.primitiveIndex<scene->size()){
                    const auto& p=scene->primitives()[run.primitiveIndex];
                    if(directContourPrimitive(p)){
                        auto inside=[](const std::vector<Vec2>& polygon,Vec2 p){bool hit=false;for(size_t j=0,k=polygon.size()-1;j<polygon.size();k=j++)if((polygon[j].y>p.y)!=(polygon[k].y>p.y)&&p.x<(polygon[k].x-polygon[j].x)*(p.y-polygon[j].y)/(polygon[k].y-polygon[j].y)+polygon[j].x)hit=!hit;return hit;};
                        std::vector<const std::vector<Vec2>*> contours;
                        if(p.subpaths.empty())contours.push_back(&p.points);else for(const auto& sub:p.subpaths)contours.push_back(&sub.points);
                        if(contourOrder_.size()<=run.primitiveIndex)contourOrder_.resize(run.primitiveIndex+1);
                        auto& order=contourOrder_[run.primitiveIndex];bool reuse=p.version&&order.valid&&order.version==p.version&&order.reversed.size()==contours.size();
                        if(!reuse){order.reversed.assign(contours.size(),false);for(size_t j=0;j<contours.size();++j){const auto& path=*contours[j];if(path.size()<3)continue;int depth=0;for(size_t k=0;k<contours.size();++k)if(k!=j&&contours[k]->size()>=3&&inside(*contours[k],path[0]))++depth;double area=0;for(size_t k=0;k<path.size();++k){auto a=path[k],b=path[(k+1)%path.size()];area+=a.x*b.y-b.x*a.y;}order.reversed[j]=(area>0)!=(depth%2==0);}order.version=p.version;order.valid=true;}
                        for(size_t j=0;j<contours.size();++j){const auto& path=*contours[j];if(path.size()<3)continue;
                            bool reverse=order.reversed[j];for(size_t k=0;k<path.size();++k){auto d=view->transform.toDevice(path[reverse?path.size()-1-k:k]);points.emplace_back(float(d.x),float(d.y));types.push_back(k?Gdiplus::PathPointTypeLine:Gdiplus::PathPointTypeStart);}types.back()|=Gdiplus::PathPointTypeCloseSubpath;
                        }direct=true;
                    }
                }
                if(direct||run.first<0||run.count<=0||size_t(run.first)+size_t(run.count)>batch.triangleVertices.size()/2)continue;
                for(int k=0;k+2<run.count;k+=3){const float* v=&batch.triangleVertices[(size_t(run.first)+k)*2];
                    float area=(v[2]-v[0])*(v[5]-v[1])-(v[3]-v[1])*(v[4]-v[0]);if(!std::isfinite(area)||area==0)continue;int second=area<0?4:2,third=area<0?2:4;points.emplace_back(v[0],v[1]);points.emplace_back(v[second],v[second+1]);points.emplace_back(v[third],v[third+1]);types.push_back(Gdiplus::PathPointTypeStart);types.push_back(Gdiplus::PathPointTypeLine);types.push_back(Gdiplus::PathPointTypeLine|Gdiplus::PathPointTypeCloseSubpath);
                }
            }
            if(points.empty())continue;Gdiplus::GraphicsPath path(points.data(),types.data(),INT(points.size()),Gdiplus::FillModeWinding);
            Gdiplus::SolidBrush brush(Gdiplus::Color(colorAlpha(rgba),colorRed(rgba),colorGreen(rgba),colorBlue(rgba)));g.FillPath(&brush,&path);if(stats)++stats->drawCalls;
        }
    }

public:
    ~Renderer(){destroy();}
    const char* name()const override{return "GDI+-cached-compatible";}
    bool initialize(std::string*)override{ready_=true;return true;}
    bool ready()const override{return ready_;}
    void sync(const r62::render::Scene& scene,const r62::render::View& view)override{content_.sync(scene,view,nullptr,nullptr);}
    void setView(const r62::render::View&)override{}
    r62::core::Error draw(const r62::render::Scene& scene,const r62::render::Scene* overlay,const r62::render::View& view,r62::render::RenderStats* stats)override{
        using namespace r62::render;using namespace r62::core;
        if(stats)*stats=RenderStats();
        if(!ready_||!std::isfinite(view.widthPx)||!std::isfinite(view.heightPx)||view.widthPx<=0||view.heightPx<=0||view.widthPx>32768||view.heightPx>32768)return Error(ErrorCode::InvalidArgument,"invalid viewport");
        if(!resize(int(view.widthPx+.5),int(view.heightPx+.5)))return Error(ErrorCode::IoError,"cannot allocate compatible render surface");
        if(contentScene_!=&scene){content_.clear();contourOrder_.clear();contentScene_=&scene;geometryValid_=false;vectorValid_=false;}
        if(overlayScene_!=overlay){overlay_.clear();overlayScene_=overlay;}
        uint64_t key=1469598103934665603ull;
        auto hash=[&](const void* ptr,size_t length){const unsigned char* bytes=static_cast<const unsigned char*>(ptr);for(size_t i=0;i<length;++i){key^=bytes[i];key*=1099511628211ull;}};
        for(double x:{view.transform.center.x,view.transform.center.y,view.transform.viewportCenter.x,view.transform.viewportCenter.y,view.transform.scale,view.transform.dpiScale,view.widthPx,view.heightPx,view.defaultStrokeWidth})hash(&x,sizeof(x));
        hash(&view.clipEnabled,sizeof(view.clipEnabled));hash(&view.clipMin,sizeof(view.clipMin));hash(&view.clipMax,sizeof(view.clipMax));
        uint64_t viewKey=key;key=1469598103934665603ull;
        for(const auto& p:scene.primitives())if(!(p.layer.value==0&&p.kind==PrimKind::Rect)&&p.text.compare(0,10,"CDXML-EMF:")!=0){
            hash(&p.version,sizeof(p.version));hash(&p.kind,sizeof(p.kind));
            if(!p.version){for(auto x:p.points)hash(&x,sizeof(x));for(auto& sub:p.subpaths)for(auto x:sub.points)hash(&x,sizeof(x));hash(&p.style.fill.rgba,sizeof(p.style.fill.rgba));hash(&p.style.stroke.rgba,sizeof(p.style.stroke.rgba));hash(&p.style.strokeWidth,sizeof(p.style.strokeWidth));}
        }
        uint64_t geometryKey=key;hash(&viewKey,sizeof(viewKey));
        BatchStats a,b;Batch content,over;
        if(!vectorValid_||vectorKey_!=key){
            if(!geometryValid_||geometryKey_!=geometryKey){foreground_.clear();for(const auto& p:scene.primitives())if(!(p.layer.value==0&&p.kind==PrimKind::Rect)&&p.text.compare(0,10,"CDXML-EMF:")!=0)foreground_.add(p);content_.sync(foreground_,view,nullptr,&a,true);geometryKey_=geometryKey;geometryValid_=true;}
            content_.buildBatch(foreground_,view,false,&content,&a,true);
            if(!vectorImage_)vectorImage_.reset(new Gdiplus::Bitmap(width_,height_,PixelFormat32bppPARGB));
            Gdiplus::Graphics vectors(vectorImage_.get());vectors.Clear(Gdiplus::Color(0,0,0,0));vectors.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);vectors.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
            submit(vectors,content,stats,&foreground_,&view);vectors.Flush(Gdiplus::FlushIntentionSync);vectorKey_=key;vectorValid_=true;
        }
        if(overlay){overlay_.sync(*overlay,view,nullptr,&b);overlay_.buildBatch(*overlay,view,false,&over,&b);}
        GdiFlush();Gdiplus::Graphics g(image_.get());g.Clear(transparentBackground_?Gdiplus::Color(0,0,0,0):Gdiplus::Color::White);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        if(view.clipEnabled)g.SetClip(Gdiplus::RectF(float(view.clipMin.x),float(view.clipMin.y),float(std::max(0.,view.clipMax.x-view.clipMin.x)),float(std::max(0.,view.clipMax.y-view.clipMin.y))));
        for(const auto& p:scene.primitives())if(p.layer.value==0&&p.kind==PrimKind::Rect&&p.points.size()>=2){
            auto project=[&](Vec2 x){return (x-view.transform.center)*(view.transform.scale*view.transform.dpiScale)+view.transform.viewportCenter;};
            auto a=project(p.points[0]),b=project(p.points[1]);uint32_t c=resolvePaint(p.style.fill.rgba);
            Gdiplus::SolidBrush fill(Gdiplus::Color(colorAlpha(c),colorRed(c),colorGreen(c),colorBlue(c)));
            g.FillRectangle(&fill,float(a.x),float(a.y),float(b.x-a.x),float(b.y-a.y));
        }
        embedded::draw(g,scene,view,-1,true);
        g.DrawImage(vectorImage_.get(),0,0,width_,height_);
        embedded::draw(g,scene,view,1,true);submit(g,over,stats);g.Flush(Gdiplus::FlushIntentionSync);
        if(stats){stats->primitivesTotal=scene.size()+(overlay?overlay->size():0);stats->primitivesCulled=a.culled+b.culled;stats->primitivesDrawn=a.drawn+b.drawn;stats->meshRebuilds=a.meshRebuilds+b.meshRebuilds;stats->viewRebuilds=a.viewRebuilds+b.viewRebuilds;stats->bvhRebuilds=a.bvhRebuilds+b.bvhRebuilds;stats->triangles=content.triangleCount()+over.triangleCount();}
        return Error();
    }
    void setTransparentBackground(bool transparent){transparentBackground_=transparent;}
    bool present(HDC target){return memory_&&BitBlt(target,0,0,width_,height_,memory_,0,0,SRCCOPY);}
    r62::core::Error readPixels(int x,int y,int w,int h,std::vector<uint8_t>* rgba)const override{
        using namespace r62::core;
        if(!rgba||!pixels_||x<0||y<0||w<=0||h<=0||int64_t(x)+w>width_||int64_t(y)+h>height_)return Error(ErrorCode::InvalidArgument,"invalid readback rectangle");
        GdiFlush();rgba->resize(size_t(w)*h*4);
        for(int row=0;row<h;++row){const BYTE* src=static_cast<const BYTE*>(pixels_)+(size_t(height_-1-y-row)*width_+x)*4;BYTE* dst=rgba->data()+size_t(row)*w*4;for(int col=0;col<w;++col){dst[0]=src[2];dst[1]=src[1];dst[2]=src[0];dst[3]=src[3];src+=4;dst+=4;}}
        return Error();
    }
    void destroy()override{releaseImage();content_.clear();overlay_.clear();contourOrder_.clear();contentScene_=overlayScene_=nullptr;ready_=false;geometryValid_=false;}
};
}
#endif
