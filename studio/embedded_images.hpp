#ifndef STUDIO_EMBEDDED_IMAGES_HPP
#define STUDIO_EMBEDDED_IMAGES_HPP
#include "third_party/tinyxml2/tinyxml2.h"
#include "next/render/scene_api.hpp"
#include "metafile_compression.hpp"
#include <map>
#include <memory>
#include <stdexcept>
namespace embedded {
inline std::map<std::string,std::unique_ptr<Gdiplus::Metafile>>& metafileCache(){
 static std::map<std::string,std::unique_ptr<Gdiplus::Metafile>> cache;return cache;
}
inline Gdiplus::Metafile* metafile(const std::string& xml){
 auto& cache=metafileCache();
 auto found=cache.find(xml);if(found!=cache.end())return found->second.get();std::unique_ptr<Gdiplus::Metafile> image;
 try{tinyxml2::XMLDocument dom;if(dom.Parse(xml.c_str())!=tinyxml2::XML_SUCCESS)return nullptr;auto e=dom.RootElement();if(!e)return nullptr;std::vector<unsigned char> bytes;
  if(auto v=e->Attribute("CompressedEnhancedMetafile"))bytes=inflate(decode(v,true),e->UnsignedAttribute("UncompressedEnhancedMetafileSize"));
  else if(auto v=e->Attribute("EnhancedMetafile"))bytes=decode(v,false);
  if(!bytes.empty()){HENHMETAFILE emf=SetEnhMetaFileBits(UINT(bytes.size()),bytes.data());if(emf)image.reset(new Gdiplus::Metafile(emf,TRUE));}
  else if(auto v=e->Attribute("WindowsMetafile")){bytes=decode(v,false);HMETAFILE mf=SetMetaFileBitsEx(UINT(bytes.size()),bytes.data());if(mf)image.reset(new Gdiplus::Metafile(mf,nullptr,TRUE));}
 }catch(const std::exception&){}
 auto* p=image.get();cache.emplace(xml,std::move(image));return p;
}
typedef std::pair<Gdiplus::Metafile*,std::pair<int,int>> PreviewKey;
inline std::map<PreviewKey,std::unique_ptr<Gdiplus::Bitmap>>& previewCache(){
 static std::map<PreviewKey,std::unique_ptr<Gdiplus::Bitmap>> previews;return previews;
}
inline size_t& previewBytes(){static size_t bytes=0;return bytes;}
inline void clearCaches(){
 // GDI+ objects must be released before GdiplusShutdown: function-local
 // statics are destroyed after WinMain returns and would otherwise touch a
 // shut-down GDI+ token on process exit.
 previewCache().clear();previewBytes()=0;metafileCache().clear();
}
inline Gdiplus::Image* screenImage(Gdiplus::Metafile* source,int width,int height){
 // Bounded, device-resolution previews for interactive painting only. OLE
 // and export still replay the original metafile rather than this bitmap.
 typedef std::pair<Gdiplus::Metafile*,std::pair<int,int>> Key;
 auto& previews=previewCache();size_t& bytes=previewBytes();
 if(width<1||height<1||width>4096||height>4096)return source;
 Key key(source,{width,height});auto found=previews.find(key);if(found!=previews.end())return found->second.get();
 size_t cost=size_t(width)*height*4;if(cost>64*1024*1024)return source;
 if(bytes+cost>64*1024*1024){previews.clear();bytes=0;}
 std::unique_ptr<Gdiplus::Bitmap> bitmap(new Gdiplus::Bitmap(width,height,PixelFormat32bppPARGB));
 if(bitmap->GetLastStatus()!=Gdiplus::Ok)return source;
 {Gdiplus::Graphics raster(bitmap.get());raster.Clear(Gdiplus::Color(0,0,0,0));raster.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);raster.DrawImage(source,Gdiplus::Rect(0,0,width,height));}
 auto result=bitmap.get();previews.emplace(key,std::move(bitmap));bytes+=cost;return result;
}
inline void draw(Gdiplus::Graphics& g,const r62::render::Scene& scene,const r62::render::View& view,int pass=0,bool screen=false){
 using namespace r62;for(const auto& p:scene.primitives())if(p.kind==render::PrimKind::Rect&&p.text.compare(0,10,"CDXML-EMF:")==0&&p.points.size()>=4){if((pass<0&&p.z>=0)||(pass>0&&p.z<0))continue;
  if(screen){core::Box visible;for(auto point:p.points)visible.add(view.transform.toDevice(point));if(visible.max.x< -2||visible.max.y< -2||visible.min.x>view.widthPx+2||visible.min.y>view.heightPx+2)continue;}
  auto* image=metafile(p.text.substr(10));if(!image)continue;
  Gdiplus::PointF points[3];int indices[]={0,2,3};for(int i=0;i<3;i++){auto v=p.points[indices[i]];v=(v-view.transform.center)*(view.transform.scale*view.transform.dpiScale)+view.transform.viewportCenter;points[i]=Gdiplus::PointF(float(v.x),float(v.y));}
  Gdiplus::Image* drawable=image;
  if(screen){int w=int(std::ceil(std::hypot(points[1].X-points[0].X,points[1].Y-points[0].Y)));int h=int(std::ceil(std::hypot(points[2].X-points[0].X,points[2].Y-points[0].Y)));drawable=screenImage(image,w,h);}
  g.DrawImage(drawable,points,3);
 }
}
}
#endif
