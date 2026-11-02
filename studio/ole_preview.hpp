#ifndef STUDIO_OLE_PREVIEW_HPP
#define STUDIO_OLE_PREVIEW_HPP
// Vector presentation for OLE objects: the editor scene is re-projected into an
// EMF so Word/PowerPoint can display the structure while our server is closed.
#include "interop.hpp"
#include "compatible_renderer.hpp"
#include "next/render/batcher.hpp"
#include <gdiplus.h>
#include <sstream>
#include <memory>
#include <queue>

namespace olepreview {
using namespace Gdiplus;
using namespace r62;
using namespace r62::core;
using r62::integration::Session;
// The container (Word/PowerPoint) converts EMF pixels to HIMETRIC with the
// window's logical DPI, while GDI+ records the metafile at the reference DC's
// physical DPI. Keep the intended DPI here so the object is not shrunk to 57%
// on a 175% scaled display.
inline UINT& previewDpi(){static UINT dpi=96;return dpi;}
// The preview Session has no loaded font cache; borrow the editor's measurer so
// every glyph is advanced instead of being stacked at one origin.
inline r62::render::geom::TextMeasurer*& previewMeasurer(){static r62::render::geom::TextMeasurer* m=nullptr;return m;}

inline void fillBatch(Graphics& g,const r62::render::Batch& batch,double strokeWidth=1.5,const r62::render::Scene* scene=nullptr,const r62::render::View* view=nullptr){
    using namespace r62::render;
    for(size_t i=0;i<batch.triangleRanges.size();){
        const uint32_t rgba=resolvePaint(batch.triangleRanges[i].rgba);
        std::vector<PointF> points;std::vector<BYTE> types;
        for(;i<batch.triangleRanges.size()&&resolvePaint(batch.triangleRanges[i].rgba)==rgba&&points.size()<1024;++i){
            const auto& run=batch.triangleRanges[i];
            if(scene&&view&&run.primitiveIndex<scene->size()&&directContourPrimitive(scene->primitives()[run.primitiveIndex])){
                // Preserve glyph outlines and their holes. Exporting every GPU
                // triangle made even short captions enormous compound polygons.
                const auto& primitive=scene->primitives()[run.primitiveIndex];
                auto inside=[](const std::vector<Vec2>& polygon,Vec2 p){bool hit=false;for(size_t j=0,k=polygon.size()-1;j<polygon.size();k=j++)if((polygon[j].y>p.y)!=(polygon[k].y>p.y)&&p.x<(polygon[k].x-polygon[j].x)*(p.y-polygon[j].y)/(polygon[k].y-polygon[j].y)+polygon[j].x)hit=!hit;return hit;};
                for(size_t j=0;j<primitive.subpaths.size();++j){
                    const auto& contour=primitive.subpaths[j].points;if(contour.size()<3)continue;
                    int depth=0;for(size_t k=0;k<primitive.subpaths.size();++k)if(k!=j&&primitive.subpaths[k].points.size()>=3&&inside(primitive.subpaths[k].points,contour[0]))++depth;
                    double area=0;for(size_t k=0;k<contour.size();++k){auto a=contour[k],b=contour[(k+1)%contour.size()];area+=a.x*b.y-b.x*a.y;}
                    const bool reverse=(area>0)!=(depth%2==0);
                    for(size_t k=0;k<contour.size();++k){auto d=view->transform.toDevice(contour[reverse?contour.size()-1-k:k]);points.emplace_back(float(d.x),float(d.y));types.push_back(k?PathPointTypeLine:PathPointTypeStart);}
                    types.back()|=PathPointTypeCloseSubpath;
                }
                continue;
            }
            if(run.first<0||run.count<=0||size_t(run.first)+size_t(run.count)>batch.triangleVertices.size()/2)continue;
            for(int k=0;k+2<run.count;k+=3){
                const float* v=&batch.triangleVertices[(size_t(run.first)+k)*2];
                const float area=(v[2]-v[0])*(v[5]-v[1])-(v[3]-v[1])*(v[4]-v[0]);if(!std::isfinite(area)||area==0)continue;
                const int second=area<0?4:2,third=area<0?2:4;
                points.emplace_back(v[0],v[1]);points.emplace_back(v[second],v[second+1]);points.emplace_back(v[third],v[third+1]);
                types.push_back(PathPointTypeStart);types.push_back(PathPointTypeLine);types.push_back(PathPointTypeLine|PathPointTypeCloseSubpath);
            }
        }
        if(points.empty())continue;
        // Construct once instead of repeatedly growing a GDI+ GraphicsPath.
        GraphicsPath path(points.data(),types.data(),INT(points.size()),FillModeWinding);
        SolidBrush brush(Color(colorAlpha(rgba),colorRed(rgba),colorGreen(rgba),colorBlue(rgba)));
        g.FillPath(&brush,&path);
    }
    // Aromatic inner lines and other stroked geometry are emitted as GL_LINES,
    // not triangles. Without this pass the preview loses every double-bond line.
    for(size_t i=0;i<batch.lineRanges.size();++i){
        DrawRange run=batch.lineRanges[i];
        if(run.first<0||run.count<2)continue;
        if(size_t(run.first)+size_t(run.count)>batch.lineVertices.size()/2)continue;
        uint32_t rgba=resolvePaint(run.rgba);
        Pen pen(Color(colorAlpha(rgba),colorRed(rgba),colorGreen(rgba),colorBlue(rgba)),float(std::max(1.0,strokeWidth)));
        pen.SetLineCap(LineCapFlat,LineCapFlat,DashCapFlat);
        for(int k=0;k+1<run.count;k+=2){
            size_t at=(size_t(run.first)+k)*2;
            g.DrawLine(&pen,batch.lineVertices[at],batch.lineVertices[at+1],batch.lineVertices[at+2],batch.lineVertices[at+3]);
        }
    }
}

inline HENHMETAFILE renderDocument(const Document& source,SIZE extent,size_t* triangleCount=nullptr,std::string* diagnostics=nullptr,const r62::render::Scene* providedScene=nullptr){
    Document doc=source;
    r62::integration::Session session;
    session.doc=doc;
    // The scene builder converts device-space bond gaps through the session view.
    // Give the preview session a neutral 1:1 view before it builds geometry.
    session.renderView.widthPx=1200;session.renderView.heightPx=800;
    session.renderView.transform.center=Vec2(0,0);
    session.renderView.transform.scale=1.0;
    session.renderView.transform.dpiScale=1.0;
    session.renderView.transform.viewportCenter=Vec2(600,400);
    session.refresh();
    if(session.scene.empty()){
        // One unsupported record makes the scene builder refuse the whole
        // document. For a presentation, drop paths and keep everything else.
        for(auto& path:session.doc.paths().mutableRecords())path.alive=0;
        session.refresh();
    }
    r62::render::Scene scene=providedScene?*providedScene:session.scene;
    Box bounds=scene.bounds();
    for(const auto& a:session.doc.atoms().records())if(a.isAlive())bounds.add(a.position);
    if(bounds.empty()||bounds.width()<=0||bounds.height()<=0)return nullptr;
    double aspect=(extent.cx>0&&extent.cy>0)?double(extent.cy)/double(extent.cx):bounds.height()/bounds.width();
    if(!std::isfinite(aspect)||aspect<=0)aspect=0.6;
    int width=1200;
    int height=int(width*aspect+0.5);
    if(height<160)height=160;
    if(height>2400)height=2400;
    double pad=std::max(4.0,std::min(width,height)*0.02);
    r62::render::View view;
    view.widthPx=width;view.heightPx=height;
    view.transform.center=(bounds.min+bounds.max)*.5;
    view.transform.dpiScale=1.0;
    view.transform.scale=std::min((width-2*pad)/std::max(1.0,bounds.width()),(height-2*pad)/std::max(1.0,bounds.height()));
    view.transform.viewportCenter=Vec2(width*.5,height*.5);
    view.defaultStrokeWidth=doc.defaultLineWidth();
    view.defaultTextSize=doc.defaultTextSize();
    // Build the scene with the same view that will project it. Building at a
    // neutral 1:1 view made the device-space double-bond offsets collapse and
    // the inner lines disappear from the preview.
    if(!providedScene){session.renderView=view;session.refresh();scene=session.scene;}
    // Match Session::gpuScene: expanded glyphs and bonds need distinct cache keys.
    for(auto& primitive:scene.mutablePrimitives())primitive.source=ObjectRef();
    r62::render::Batcher batcher;
    r62::render::BatchStats stats;
    r62::render::Batch batch;
    auto* measurer=providedScene?nullptr:(previewMeasurer()?previewMeasurer():session.measurer.get());
    batcher.sync(scene,view,measurer,&stats,true);
    batcher.buildBatch(scene,view,false,&batch,&stats,true);
    if(triangleCount)*triangleCount=batch.triangleCount();
    if(diagnostics){
        std::ostringstream d;
        d<<"triRanges="<<batch.triangleRanges.size()<<" lineRanges="<<batch.lineRanges.size()<<" triVerts="<<batch.triangleVertices.size()/2<<" lineVerts="<<batch.lineVertices.size()/2<<"\n";
        for(const auto& prim:scene.primitives()){d<<"prim kind="<<int(prim.kind)<<" strW="<<prim.style.strokeWidth<<" rgba=0x"<<std::hex<<prim.style.stroke.rgba<<std::dec<<" fill="<<prim.style.fill.enabled<<" bounds="<<prim.bounds.min.x<<","<<prim.bounds.min.y<<","<<prim.bounds.max.x<<","<<prim.bounds.max.y<<"\n";}
        for(const auto& range:batch.lineRanges){d<<"line range rgba=0x"<<std::hex<<range.rgba<<std::dec<<" first="<<range.first<<" count="<<range.count<<"\n";}
        for(const auto& range:batch.triangleRanges){d<<"tri range rgba=0x"<<std::hex<<range.rgba<<std::dec<<" first="<<range.first<<" count="<<range.count<<" p0="<<batch.triangleVertices[size_t(range.first)*2]<<","<<batch.triangleVertices[size_t(range.first)*2+1]<<"\n";}
        *diagnostics=d.str();
    }
    double strokePx=std::max(1.0,view.defaultStrokeWidth*view.transform.scale);
    // The object extent is in HIMETRIC. Make the EMF frame match it, otherwise
    // the container places a 12-inch object and the structure is scaled to a
    // speck (PowerPoint showed nothing at all).
    // Avoid virtualized device dimensions when a clipboard consumer invokes us
    // from a DPI-unaware thread. Restore the caller's context on every exit.
    struct DpiContext { DPI_AWARENESS_CONTEXT old;
        DpiContext():old(SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)){}
        ~DpiContext(){if(old)SetThreadDpiAwarenessContext(old);}
    } dpiContext;
    HDC reference=GetDC(nullptr);
    if(!reference)return nullptr;
    // OLE dimensions are physical HIMETRIC, independent of UI scaling.
    const double pixelsPerMmX=double(GetDeviceCaps(reference,HORZRES))/std::max(1,GetDeviceCaps(reference,HORZSIZE));
    const double pixelsPerMmY=double(GetDeviceCaps(reference,VERTRES))/std::max(1,GetDeviceCaps(reference,VERTSIZE));
    const double physicalW=extent.cx>0?extent.cx:width/pixelsPerMmX*100;
    const double physicalH=extent.cy>0?extent.cy:height/pixelsPerMmY*100;
    double frameW=physicalW*pixelsPerMmX/100,frameH=physicalH*pixelsPerMmY/100;
    // GDI+ rounds EMF path coordinates to reference-device pixels. Recording a
    // small molecule directly at screen DPI quantizes bonds and glyph outlines
    // before Office ever scales them. Use a virtual high-DPI reference device,
    // then describe its resolution in the header, retaining physical OLE size.
    const bool fixedPreview=source.documentExtensions().findValue("studio.ole.fixed-preview")!=nullptr;
    const LONG recordScale=std::max<LONG>(1,std::min<LONG>(32,LONG(std::ceil(4800.0/std::max(frameW,frameH)))));
    // Dual EMF retains smooth, floating-point EMF+ paths for WPS and modern
    // Office, plus GDI vector records for legacy consumers. GDI-only EMF loses
    // antialiasing in WPS at normal molecule sizes, leaving broken thin glyphs.
    // Use a short-lived file because this GDI+ build cannot return a handle from
    // a stream-backed metafile.
    static unsigned previewSerial=0;
    std::wstring name=L"MoleculeStudio_preview_"+std::to_wstring(GetCurrentProcessId())+L"_"+std::to_wstring(++previewSerial)+L".emf";
    const double recordX=fixedPreview?100.0/pixelsPerMmX:recordScale;
    const double recordY=fixedPreview?100.0/pixelsPerMmY:recordScale;
    auto record=[&](const std::wstring& file)->HENHMETAFILE{
        {
            Metafile metafile(file.c_str(),reference,RectF(0,0,float(physicalW*recordX),float(physicalH*recordY)),MetafileFrameUnitGdi,EmfTypeEmfPlusDual,L"Molecule Studio");
            if(metafile.GetLastStatus()!=Ok)return nullptr;
            Graphics g(&metafile);
            g.SetPageUnit(UnitPixel);
            g.SetSmoothingMode(SmoothingModeAntiAlias);
            g.SetPixelOffsetMode(PixelOffsetModeHalf);
            g.ScaleTransform(float(frameW*recordX/width),float(frameH*recordY/height));
            // Record only the drawing. A page-sized white rectangle becomes an
            // opaque background on coloured slides. Preserve the full canvas
            // bounds in the EMF header below instead of painting background ink.
            embedded::draw(g,scene,view,-1);
            fillBatch(g,batch,strokePx,&scene,&view);
            embedded::draw(g,scene,view,1);
            g.Flush(FlushIntentionSync);
        }
        HENHMETAFILE emf=GetEnhMetaFileW(file.c_str());
        DeleteFileW(file.c_str());
        if(emf){
            UINT bytes=GetEnhMetaFileBits(emf,0,nullptr);
            std::vector<BYTE> data(bytes);
            if(bytes>=sizeof(ENHMETAHEADER)&&GetEnhMetaFileBits(emf,bytes,data.data())==bytes){
                auto* header=reinterpret_cast<ENHMETAHEADER*>(data.data());
                header->rclBounds={0,0,LONG(std::lround(frameW*recordX))-1,LONG(std::lround(frameH*recordY))-1};
                header->rclFrame.right=LONG(std::lround(physicalW));
                header->rclFrame.bottom=LONG(std::lround(physicalH));
                header->szlDevice.cx*=recordScale;
                header->szlDevice.cy*=recordScale;
                if(fixedPreview){
                    header->szlDevice={header->szlMillimeters.cx*100,header->szlMillimeters.cy*100};
                    // EMF+ has an independent DPI header. Leaving screen DPI
                    // here makes PowerPoint treat virtual high-resolution pixels
                    // as physical screen pixels and infer a new natural size.
                    size_t at=header->nSize;
                    while(at+8<=data.size()){
                        DWORD type=0,size=0;memcpy(&type,data.data()+at,4);memcpy(&size,data.data()+at+4,4);
                        if(size<8||size>data.size()-at)break;
                        if(type==EMR_GDICOMMENT&&size>=16&&memcmp(data.data()+at+12,"EMF+",4)==0){
                            size_t pos=at+16;while(pos+12<=at+size){WORD record=0;DWORD length=0;memcpy(&record,data.data()+pos,2);memcpy(&length,data.data()+pos+4,4);
                                if(length<12||length>at+size-pos)break;
                                if(record==0x4001&&length>=28){DWORD dpi=2540;memcpy(data.data()+pos+20,&dpi,4);memcpy(data.data()+pos+24,&dpi,4);}
                                pos+=length;
                            }
                        }
                        at+=size;
                    }
                }
                HENHMETAFILE framed=SetEnhMetaFileBits(bytes,data.data());
                if(framed){DeleteEnhMetaFile(emf);emf=framed;}
            }
        }
        return emf;
    };
    HENHMETAFILE result=nullptr;
    wchar_t temp[MAX_PATH]={};
    if(GetTempPathW(MAX_PATH,temp))result=record(std::wstring(temp)+name);
    if(!result){
        wchar_t exe[MAX_PATH]={};GetModuleFileNameW(nullptr,exe,MAX_PATH);std::wstring directory=exe;
        size_t slash=directory.find_last_of(L"\\/");if(slash!=std::wstring::npos)directory.resize(slash+1);else directory.clear();
        result=record(directory+name);
    }
    if(!result)result=record(L".\\"+name);
    ReleaseDC(nullptr,reference);
    return result;
}

// Build the presentation from the editor's own rendered canvas pixels. This is
// the only path that preserves exactly what the user sees: the standalone
// preview batcher lost the aromatic inner lines and stacked every glyph.
inline HENHMETAFILE fromPixels(const std::vector<uint8_t>& rgba,int w,int h,int cx,int cy,int cw,int ch,SIZE extent){
    if(rgba.size()!=size_t(w)*size_t(h)*4||cw<=0||ch<=0||cx<0||cy<0||cx+cw>w||cy+ch>h)return nullptr;
    std::vector<uint8_t> bgra(size_t(cw)*size_t(ch)*4);
    for(int y=0;y<ch;++y)for(int x=0;x<cw;++x){
        size_t s=(size_t(cy+y)*size_t(w)+size_t(cx+x))*4,d=(size_t(y)*size_t(cw)+size_t(x))*4;
        bgra[d]=rgba[s+2];bgra[d+1]=rgba[s+1];bgra[d+2]=rgba[s];bgra[d+3]=rgba[s+3];
    }
    UINT dpi=previewDpi();if(dpi<96)dpi=96;
    double frameW=extent.cx>0?extent.cx*double(dpi)/2540.0:cw;
    double frameH=extent.cy>0?extent.cy*double(dpi)/2540.0:ch;
    if(!std::isfinite(frameW)||frameW<8)frameW=cw;
    if(!std::isfinite(frameH)||frameH<8)frameH=ch;
    HDC reference=GetDC(nullptr);if(!reference)return nullptr;
    RECT frame={0,0,(LONG)(frameW+0.5),(LONG)(frameH+0.5)};
    HDC dc=CreateEnhMetaFileW(reference,nullptr,&frame,L"Molecule Studio\0\0");
    if(!dc){ReleaseDC(nullptr,reference);return nullptr;}
    SetMapMode(dc,MM_ANISOTROPIC);
    SetWindowOrgEx(dc,0,0,nullptr);SetViewportOrgEx(dc,0,0,nullptr);
    SetWindowExtEx(dc,cw,ch,nullptr);SetViewportExtEx(dc,(int)(frameW+0.5),(int)(frameH+0.5),nullptr);
    BITMAPINFO bi={};bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bi.bmiHeader.biWidth=cw;bi.bmiHeader.biHeight=-ch;
    bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;bi.bmiHeader.biCompression=BI_RGB;
    SetStretchBltMode(dc,COLORONCOLOR);
    StretchDIBits(dc,0,0,cw,ch,0,0,cw,ch,bgra.data(),&bi,DIB_RGB_COLORS,SRCCOPY);
    HENHMETAFILE emf=CloseEnhMetaFile(dc);
    ReleaseDC(nullptr,reference);
    return emf;
}

inline std::wstring wideFromUtf8(const std::string& s){
    int n=MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),0,0);
    std::wstring w(n,L'\0');if(n)MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),&w[0],n);return w;
}
// Dependency-free presentation: draw the model directly with GDI+. It always
// draws double bonds and the complete text, independent of window state.
inline Box contentBounds(const Document& doc){
    Box b;bool any=false;
    for(const auto& a:doc.atoms().records())if(a.isAlive()){b.add(a.position);any=true;}
    for(const auto& t:doc.texts().records())if(t.isAlive()){
        double size=t.size>0?t.size:doc.defaultTextSize();
        std::string text=doc.plainText(t.id);
        if(text.empty())continue;
        double width=std::max(8.0,size*0.6*double(std::max<size_t>(1,text.size())));
        b.add(t.position);b.add(t.position+Vec2(width,size));b.add(t.position+Vec2(0,-size));any=true;
    }
    for(const auto& p:doc.paths().records())for(const auto& q:p.points){b.add(q);any=true;}
    (void)any;return b;
}
inline HENHMETAFILE renderSimple(const Document& doc,SIZE extent){
    Box b=contentBounds(doc);
    if(b.empty()||b.width()<=0||b.height()<=0)return nullptr;
    double margin=std::max(4.0,std::max(b.width(),b.height())*0.08);
    b.min=b.min-Vec2(margin,margin);b.max=b.max+Vec2(margin,margin);
    UINT dpi=previewDpi();if(dpi<96)dpi=96;
    double frameW=extent.cx>0?extent.cx*double(dpi)/2540.0:b.width();
    double frameH=extent.cy>0?extent.cy*double(dpi)/2540.0:b.height();
    if(!std::isfinite(frameW)||frameW<8)frameW=b.width();
    if(!std::isfinite(frameH)||frameH<8)frameH=b.height();
    HDC reference=GetDC(nullptr);if(!reference)return nullptr;
    static unsigned serial=0;
    std::wstring name=L"MoleculeStudio_simple_"+std::to_wstring(GetCurrentProcessId())+L"_"+std::to_wstring(++serial)+L".emf";
    auto record=[&](const std::wstring& file)->HENHMETAFILE{
        {Metafile metafile(file.c_str(),reference,RectF(0,0,float(frameW),float(frameH)),MetafileFrameUnitPixel,EmfTypeEmfOnly,L"Molecule Studio");
         if(metafile.GetLastStatus()!=Ok)return nullptr;
         Graphics g(&metafile);g.SetSmoothingMode(SmoothingModeAntiAlias);g.SetPixelOffsetMode(PixelOffsetModeHalf);
         SolidBrush page(Color(255,255,255));g.FillRectangle(&page,0.f,0.f,float(frameW),float(frameH));
         double scale=std::min((frameW-8.0)/b.width(),(frameH-8.0)/b.height());
         if(!std::isfinite(scale)||scale<=0)return nullptr;
         double ox=(frameW-b.width()*scale)*0.5-b.min.x*scale;
         double oy=(frameH-b.height()*scale)*0.5-b.min.y*scale;
         Matrix m(float(scale),0,0,float(scale),float(ox),float(oy));g.SetTransform(&m);
         double lw=std::max(0.35,doc.defaultLineWidth());
         Pen pen(Color(255,20,20,20),float(lw));pen.SetLineCap(LineCapFlat,LineCapFlat,DashCapFlat);
         Vec2 atomCenter(0,0);int atomCount=0;
         for(const auto& a:doc.atoms().records())if(a.isAlive()){atomCenter.x+=a.position.x;atomCenter.y+=a.position.y;++atomCount;}
         if(atomCount){atomCenter.x/=atomCount;atomCenter.y/=atomCount;}
         std::vector<const AtomRecord*> atoms;
         for(const auto& a:doc.atoms().records())if(a.isAlive())atoms.push_back(&a);
         auto indexOf=[&](ObjectId id){for(size_t i=0;i<atoms.size();++i)if(atoms[i]->id==id)return int(i);return -1;};
         std::vector<std::vector<int>> adj(atoms.size());
         for(const auto& bond:doc.bonds().records())if(bond.isAlive()){
             int i=indexOf(bond.beginAtom),j=indexOf(bond.endAtom);
             if(i>=0&&j>=0){adj[i].push_back(j);adj[j].push_back(i);}
         }
         auto pathRingCenterFor=[&](const Vec2& mid,Vec2& center)->bool{
             bool found=false;double bestArea=1e300;
             for(const auto& p:doc.paths().records())if(p.isAlive()&&p.points.size()>=3){
                 bool closed=p.closed;
                 if(!closed){Vec2 d=p.points.front()-p.points.back();if(std::sqrt(d.x*d.x+d.y*d.y)<1e-3)closed=true;}
                 if(!closed)continue;
                 Box pb;for(const auto& q:p.points)pb.add(q);
                 if(mid.x<pb.min.x-0.5||mid.x>pb.max.x+0.5||mid.y<pb.min.y-0.5||mid.y>pb.max.y+0.5)continue;
                 Vec2 pc(0,0);for(const auto& q:p.points){pc.x+=q.x;pc.y+=q.y;}pc.x/=p.points.size();pc.y/=p.points.size();
                 double area=pb.width()*pb.height();
                 if(area<bestArea){bestArea=area;center=pc;found=true;}
             }
             return found;
         };
         auto ringCenterFor=[&](int ia,int ib,Vec2& center)->bool{
             std::vector<int> parent(atoms.size(),-1);
             std::queue<int> q;q.push(ia);parent[ia]=ia;
             while(!q.empty()){
                 int u=q.front();q.pop();
                 for(int v:adj[u]){
                     if((u==ia&&v==ib)||(u==ib&&v==ia))continue;
                     if(parent[v]!=-1)continue;
                     parent[v]=u;
                     if(v==ib)break;
                     q.push(v);
                 }
                 if(parent[ib]!=-1)break;
             }
             if(parent[ib]==-1)return false;
             Vec2 sum(0,0);int count=0;int v=ib;
             while(v!=ia){sum.x+=atoms[v]->position.x;sum.y+=atoms[v]->position.y;++count;v=parent[v];}
             sum.x+=atoms[ia]->position.x;sum.y+=atoms[ia]->position.y;++count;
             if(count<3)return false;
             center.x=sum.x/count;center.y=sum.y/count;return true;
         };
         for(const auto& bond:doc.bonds().records())if(bond.isAlive()){
             const AtomRecord* a=doc.atoms().alive(bond.beginAtom);const AtomRecord* z=doc.atoms().alive(bond.endAtom);
             if(!a||!z)continue;
             Vec2 av=a->position,zv=z->position;Vec2 d=zv-av;double len=std::sqrt(d.x*d.x+d.y*d.y);
             if(len<1e-6)continue;
             Vec2 n(-d.y/len,d.x/len);
             int order=0;
             if(bond.order==BondOrder::Double||bond.order==BondOrder::Aromatic||bond.order==BondOrder::SingleOrAromatic||bond.order==BondOrder::DoubleOrAromatic)order=2;
             else if(bond.order==BondOrder::Triple)order=3;
             int ia=indexOf(bond.beginAtom),ib=indexOf(bond.endAtom);
             Vec2 rc;bool ring=false;
             if(order==2){ring=pathRingCenterFor((av+zv)*0.5,rc);if(!ring&&ia>=0&&ib>=0)ring=ringCenterFor(ia,ib,rc);}
             if(order<=1)g.DrawLine(&pen,float(av.x),float(av.y),float(zv.x),float(zv.y));
             else if(order==2){
                 double off=lw*2.2;
                 if(ring){
                     Vec2 mid=(av+zv)*0.5;Vec2 to=rc-mid;double side=(to.x*n.x+to.y*n.y)>=0?1.0:-1.0;
                     g.DrawLine(&pen,float(av.x),float(av.y),float(zv.x),float(zv.y));
                     g.DrawLine(&pen,float(av.x+n.x*off*side),float(av.y+n.y*off*side),float(zv.x+n.x*off*side),float(zv.y+n.y*off*side));
                 }else{
                     Vec2 mid=(av+zv)*0.5;Vec2 to=atomCenter-mid;double side=(to.x*n.x+to.y*n.y)>=0?1.0:-1.0;
                     g.DrawLine(&pen,float(av.x),float(av.y),float(zv.x),float(zv.y));
                     g.DrawLine(&pen,float(av.x+n.x*off*side),float(av.y+n.y*off*side),float(zv.x+n.x*off*side),float(zv.y+n.y*off*side));
                 }
             }
             else {double off=lw*2.0;g.DrawLine(&pen,float(av.x),float(av.y),float(zv.x),float(zv.y));g.DrawLine(&pen,float(av.x-n.x*off),float(av.y-n.y*off),float(zv.x-n.x*off),float(zv.y-n.y*off));g.DrawLine(&pen,float(av.x+n.x*off),float(av.y+n.y*off),float(zv.x+n.x*off),float(zv.y+n.y*off));}
         }
         SolidBrush ink(Color(255,20,20,20));
         for(const auto& a:doc.atoms().records())if(a.isAlive()){
             std::string label=doc.atomLabel(a.id);
             if(label.empty()&&a.element!=6){const char* sym="";switch(a.element){case 1:sym="H";break;case 7:sym="N";break;case 8:sym="O";break;case 9:sym="F";break;case 15:sym="P";break;case 16:sym="S";break;case 17:sym="Cl";break;case 35:sym="Br";break;case 53:sym="I";break;default:break;}label=sym;}
             if(label.empty())continue;
             double size=doc.defaultTextSize();std::wstring w=wideFromUtf8(label);
             Font font(L"Microsoft YaHei UI",float(size),FontStyleRegular,UnitPixel);
             g.DrawString(w.c_str(),int(w.size()),&font,PointF(float(a.position.x-size*0.3),float(a.position.y-size*0.5)),&ink);
         }
         for(const auto& t:doc.texts().records())if(t.isAlive()){
             std::string text=doc.plainText(t.id);if(text.empty())continue;
             double size=t.size>0?t.size:doc.defaultTextSize();std::wstring w=wideFromUtf8(text);
             std::wstring family=wideFromUtf8(t.format.fontName.empty()?doc.defaultFont():t.format.fontName);
             Font font(family.c_str(),float(size),FontStyleRegular,UnitPixel);
             g.DrawString(w.c_str(),int(w.size()),&font,PointF(float(t.position.x),float(t.position.y)),&ink);
         }
         for(const auto& path:doc.paths().records())if(path.points.size()>1){
             std::vector<PointF> pts;pts.reserve(path.points.size());for(const auto& q:path.points)pts.push_back(PointF(float(q.x),float(q.y)));
             g.DrawLines(&pen,pts.data(),int(pts.size()));
         }
         g.Flush(FlushIntentionSync);}
        HENHMETAFILE emf=GetEnhMetaFileW(file.c_str());DeleteFileW(file.c_str());return emf;
    };
    HENHMETAFILE result=nullptr;wchar_t temp[MAX_PATH]={};
    if(GetTempPathW(MAX_PATH,temp))result=record(std::wstring(temp)+name);
    if(!result){wchar_t exe[MAX_PATH]={};GetModuleFileNameW(nullptr,exe,MAX_PATH);std::wstring dir=exe;size_t slash=dir.find_last_of(L"\\/");if(slash!=std::wstring::npos)dir.resize(slash+1);else dir.clear();result=record(dir+name);}
    if(!result)result=record(L".\\"+name);
    ReleaseDC(nullptr,reference);return result;
}

// Render the editor's own scene through the same compatible renderer used for the
// canvas, then crop to the ink. This preserves exact bond/text placement (the
// model-only renderer approximated double-bond sides and could include a blank
// text box that inflated the object with whitespace).
inline HENHMETAFILE renderSceneImage(const r62::render::Scene& scene,const r62::render::View& view,SIZE extent){
    if(view.widthPx<8||view.heightPx<8)return nullptr;
    compatible::Renderer renderer;std::string why;
    if(!renderer.initialize(&why))return nullptr;
    renderer.draw(scene,nullptr,view,nullptr);
    std::vector<uint8_t> rgba;
    if(!renderer.readPixels(0,0,view.widthPx,view.heightPx,&rgba).ok()||rgba.size()!=size_t(view.widthPx)*size_t(view.heightPx)*4)return nullptr;
    int minx=view.widthPx,miny=view.heightPx,maxx=-1,maxy=-1;
    for(int y=0;y<view.heightPx;++y)for(int x=0;x<view.widthPx;++x){
        const uint8_t* p=&rgba[(size_t(y)*view.widthPx+size_t(x))*4];
        if(p[3]>16&&(p[0]<235||p[1]<235||p[2]<235)){if(x<minx)minx=x;if(x>maxx)maxx=x;if(y<miny)miny=y;if(y>maxy)maxy=y;}
    }
    if(maxx<0||maxy<0)return nullptr;
    int pad=std::max(2,int(std::min(view.widthPx,view.heightPx)*0.01));
    minx=std::max(0,minx-pad);miny=std::max(0,miny-pad);maxx=std::min(int(view.widthPx)-1,maxx+pad);maxy=std::min(int(view.heightPx)-1,maxy+pad);
    int cw=maxx-minx+1,ch=maxy-miny+1;
    std::vector<uint8_t> bgra(size_t(cw)*size_t(ch)*4);
    for(int y=0;y<ch;++y)for(int x=0;x<cw;++x){
        size_t src=(size_t(miny+y)*view.widthPx+size_t(minx+x))*4,dst=(size_t(y)*cw+size_t(x))*4;
        bgra[dst]=rgba[src+2];bgra[dst+1]=rgba[src+1];bgra[dst+2]=rgba[src];bgra[dst+3]=rgba[src+3];
    }
    UINT dpi=previewDpi();if(dpi<96)dpi=96;
    double frameW=extent.cx>0?extent.cx*double(dpi)/2540.0:cw;
    double frameH=extent.cy>0?extent.cy*double(dpi)/2540.0:ch;
    if(!std::isfinite(frameW)||frameW<8)frameW=cw;
    if(!std::isfinite(frameH)||frameH<8)frameH=ch;
    HDC reference=GetDC(nullptr);if(!reference)return nullptr;
    static unsigned serial=0;
    std::wstring name=L"MoleculeStudio_scene_"+std::to_wstring(GetCurrentProcessId())+L"_"+std::to_wstring(++serial)+L".emf";
    auto record=[&](const std::wstring& file)->HENHMETAFILE{
        {Metafile metafile(file.c_str(),reference,RectF(0,0,float(frameW),float(frameH)),MetafileFrameUnitPixel,EmfTypeEmfOnly,L"Molecule Studio");
         if(metafile.GetLastStatus()!=Ok)return nullptr;
         Graphics g(&metafile);g.SetSmoothingMode(SmoothingModeAntiAlias);g.SetPixelOffsetMode(PixelOffsetModeHalf);
         SolidBrush page(Color(255,255,255));g.FillRectangle(&page,0.f,0.f,float(frameW),float(frameH));
         Bitmap bitmap(cw,ch,cw*4,PixelFormat32bppARGB,bgra.data());
         if(bitmap.GetLastStatus()!=Ok)return nullptr;
         double scale=std::min((frameW-8.0)/cw,(frameH-8.0)/ch);
         if(!std::isfinite(scale)||scale<=0)scale=1.0;
         double ox=(frameW-cw*scale)*0.5,oy=(frameH-ch*scale)*0.5;
         g.DrawImage(&bitmap,float(ox),float(oy),float(cw*scale),float(ch*scale));
         g.Flush(FlushIntentionSync);}
        HENHMETAFILE emf=GetEnhMetaFileW(file.c_str());DeleteFileW(file.c_str());return emf;
    };
    HENHMETAFILE result=nullptr;wchar_t temp[MAX_PATH]={};
    if(GetTempPathW(MAX_PATH,temp))result=record(std::wstring(temp)+name);
    if(!result){wchar_t exe[MAX_PATH]={};GetModuleFileNameW(nullptr,exe,MAX_PATH);std::wstring dir=exe;size_t slash=dir.find_last_of(L"\\/");if(slash!=std::wstring::npos)dir.resize(slash+1);else dir.clear();result=record(dir+name);}
    if(!result)result=record(L".\\"+name);
    ReleaseDC(nullptr,reference);return result;
}

inline HENHMETAFILE fromNative(const std::string& bytes,SIZE extent){
    if(bytes.empty())return nullptr;
    Document doc;
    std::istringstream in(bytes);
    if(!r62::integration::loadSafeNative(doc,in).ok())return nullptr;
    return renderDocument(doc,extent);
}
}
#endif

