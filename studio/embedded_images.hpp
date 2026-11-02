#ifndef STUDIO_EMBEDDED_IMAGES_HPP
#define STUDIO_EMBEDDED_IMAGES_HPP
#include "third_party/tinyxml2/tinyxml2.h"
#include "next/render/scene_api.hpp"
#include <map>
#include <memory>
#include <stdexcept>
namespace embedded {
// A bounded RFC1950/1951 decoder for CDXML's CompressedEnhancedMetafile.
// This keeps imports independent of installed ChemDraw and external programs.
struct Bits{const std::vector<unsigned char>& b;size_t bit;explicit Bits(const std::vector<unsigned char>& data):b(data),bit(16){} unsigned get(unsigned n){if(n>24||bit+n>b.size()*8)throw std::runtime_error("truncated deflate");unsigned v=0;for(unsigned i=0;i<n;++i,++bit)v|=((b[bit/8]>>(bit%8))&1)<<i;return v;}};
struct Huffman{
 std::map<unsigned,unsigned> codes;unsigned max=0;
 Huffman(const std::vector<unsigned>& lengths){unsigned count[16]={},next[16]={};for(auto n:lengths){if(n>15)throw std::runtime_error("invalid deflate tree");if(n)++count[n];max=std::max(max,n);}unsigned code=0;for(unsigned n=1;n<=15;++n){code=(code+count[n-1])<<1;next[n]=code;}
  for(unsigned i=0;i<lengths.size();++i){unsigned n=lengths[i];if(!n)continue;unsigned c=next[n]++,rev=0;for(unsigned j=0;j<n;++j)rev=(rev<<1)|((c>>j)&1);codes[(1u<<n)|rev]=i;}}
 unsigned read(Bits& b)const{unsigned code=0;for(unsigned n=1;n<=max;++n){code|=b.get(1)<<(n-1);auto it=codes.find((1u<<n)|code);if(it!=codes.end())return it->second;}throw std::runtime_error("invalid deflate code");}
};
inline std::vector<unsigned char> inflate(const std::vector<unsigned char>& data,size_t expected){
 if(data.size()<6||expected>32*1024*1024||!expected||(data[0]&15)!=8||((unsigned(data[0])*256+data[1])%31)||(data[1]&32))throw std::runtime_error("invalid zlib stream");
 Bits bits{data};std::vector<unsigned char> out;out.reserve(expected);bool last=false;
 const unsigned lb[]={3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
 const unsigned le[]={0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
 const unsigned db[]={1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
 const unsigned de[]={0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
 while(!last){last=bits.get(1);unsigned type=bits.get(2);if(type==0){bits.bit=(bits.bit+7)&~size_t(7);unsigned n=bits.get(16),inv=bits.get(16);if((n^inv)!=65535||out.size()+n>expected)throw std::runtime_error("invalid stored block");while(n--)out.push_back(bits.get(8));continue;}
  std::vector<unsigned> lit,dist;
  if(type==1){lit.resize(288);for(unsigned i=0;i<288;++i)lit[i]=i<144?8:i<256?9:i<280?7:8;dist.assign(32,5);}
  else if(type==2){unsigned nl=bits.get(5)+257,nd=bits.get(5)+1,nc=bits.get(4)+4;const unsigned order[]={16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};std::vector<unsigned> lengths(19),all;for(unsigned i=0;i<nc;++i)lengths[order[i]]=bits.get(3);Huffman tree(lengths);
   while(all.size()<nl+nd){unsigned c=tree.read(bits);if(c<16)all.push_back(c);else{unsigned n=c==16?bits.get(2)+3:c==17?bits.get(3)+3:bits.get(7)+11;if(c==16&&all.empty())throw std::runtime_error("invalid repeat");unsigned v=c==16?all.back():0;if(all.size()+n>nl+nd)throw std::runtime_error("long tree");all.insert(all.end(),n,v);}}lit.assign(all.begin(),all.begin()+nl);dist.assign(all.begin()+nl,all.end());}
  else throw std::runtime_error("reserved block");Huffman lt(lit),dt(dist);
  for(;;){unsigned c=lt.read(bits);if(c==256)break;if(c<256){if(out.size()==expected)throw std::runtime_error("long output");out.push_back(c);}else{if(c>285)throw std::runtime_error("bad length");unsigned n=lb[c-257]+bits.get(le[c-257]),d=dt.read(bits);if(d>=30)throw std::runtime_error("bad distance");unsigned back=db[d]+bits.get(de[d]);if(back>out.size()||out.size()+n>expected)throw std::runtime_error("bad backreference");while(n--)out.push_back(out[out.size()-back]);}}
 }
 if(out.size()!=expected)throw std::runtime_error("short output");return out;
}
inline std::vector<unsigned char> decode(const std::string& value,bool base64){
 std::vector<unsigned char> out;unsigned bits=0,count=0;const std::string alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
 for(unsigned char c:value){int v=-1;if(base64){auto n=alphabet.find(c);if(n!=std::string::npos)v=int(n);}else if(c>='0'&&c<='9')v=c-'0';else if(c>='A'&&c<='F')v=c-'A'+10;else if(c>='a'&&c<='f')v=c-'a'+10;
  if(v<0)continue;bits=(bits<<(base64?6:4))|unsigned(v);count+=base64?6:4;if(count>=8){count-=8;out.push_back((bits>>count)&255);}}
 return out;
}
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
