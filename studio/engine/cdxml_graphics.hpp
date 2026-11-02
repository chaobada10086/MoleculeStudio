#ifndef STUDIO_CDXML_GRAPHICS_HPP
#define STUDIO_CDXML_GRAPHICS_HPP
#include "third_party/tinyxml2/tinyxml2.h"
#include "next/render/scene_builder.hpp"
#include "next/core/document.hpp"
#include <sstream>
#include <cmath>
namespace r62 { namespace integration { namespace cdgraphics {
using namespace core;
inline uint32_t color(const Document& d,const tinyxml2::XMLElement* e){
 auto v=d.documentExtensions().findValue("studio.palette."+std::string(e->Attribute("color")?e->Attribute("color"):"0"));return v?uint32_t(std::strtoull(v->c_str(),0,10)):0x000000ff;
}
inline Vec2 point(const tinyxml2::XMLElement* e,const char* key,Vec2 fallback=Vec2()){auto v=e->Attribute(key);if(!v)return fallback;std::istringstream s(v);Vec2 p;return s>>p.x>>p.y?p:fallback;}
inline std::string attr(const tinyxml2::XMLElement* e,const char* k){auto p=e->Attribute(k);return p?p:"";}
inline bool append(const Document& d,ObjectRef ref,Vec2 current,std::vector<render::Primitive>* out){
 auto raw=d.extensions(ref).findValue("studio.cdxml.graphic");if(!raw)return false;
 tinyxml2::XMLDocument xml;if(xml.Parse(raw->c_str())!=tinyxml2::XML_SUCCESS)return false;auto e=xml.RootElement();if(!e)return false;
 if(attr(e,"Visible")=="no"||e->Attribute("SupersededBy"))return true;
 Vec2 a,b;std::istringstream box(attr(e,"BoundingBox"));box>>a.x>>a.y>>b.x>>b.y;
 bool curve=std::string(e->Name())=="curve",arrow=std::string(e->Name())=="arrow";Vec2 origin=a;if(curve)origin=point(e,"CurvePoints");if(arrow)origin=point(e,"Tail3D");
 if(ref.kind==ObjectKind::Path&&!curve&&!arrow&&attr(e,"GraphicType")!="Line"&&attr(e,"GraphicType")!="Arc")origin=Vec2(std::min(a.x,b.x),std::min(a.y,b.y));
 Vec2 offset=current-origin;
 if(std::string(e->Name())=="embeddedobject"){
  render::Primitive p;p.kind=render::PrimKind::Rect;p.source=ref;p.layer=render::LayerId(100);p.z=0;p.points={a,b,Vec2(b.x,a.y),Vec2(a.x,b.y)};p.style.fill.enabled=p.style.stroke.enabled=false;p.text="CDXML-EMF:"+*raw;
  double angle=e->DoubleAttribute("RotationAngle")/65536.*3.141592653589793/180.;Vec2 center=(a+b)*.5;
  for(auto& v:p.points){auto q=v-center;v=center+Vec2(q.x*cos(angle)-q.y*sin(angle),q.x*sin(angle)+q.y*cos(angle))+offset;}out->push_back(p);return true;
 }
 uint32_t ink=color(d,e);double width=e->DoubleAttribute("LineWidth",d.defaultLineWidth());
 if(auto c=d.extensions(ref).findValue("studio.color"))ink=uint32_t(std::strtoull(c->c_str(),nullptr,10));
 std::string line=attr(e,"LineType"),fill=attr(e,"FillType"),type=attr(e,"GraphicType");
 if(curve){int flags=e->IntAttribute("CurveType");if(flags&2)line+="Dashed";if(flags&4)line+="Bold";if(flags&128)fill="Solid";if(flags&256)fill="Shaded";}
 if(line.find("Bold")!=std::string::npos){auto v=d.documentExtensions().findValue("studio.style.BoldWidth");width=e->DoubleAttribute("BoldWidth",v?atof(v->c_str()):width*2);}
 int z=e->IntAttribute("Z",10);
 auto emit=[&](std::vector<Vec2> pts,bool closed,bool filled,uint32_t c,bool stroke){
  if(pts.size()<2)return;render::Primitive p;p.kind=closed?render::PrimKind::Polygon:render::PrimKind::Polyline;p.source=ref;p.layer=render::LayerId(100);p.z=0;
  for(auto v:pts)p.points.push_back(v+offset);p.style.stroke=render::Paint(c,stroke);p.style.fill=render::Paint(c,filled);p.style.strokeWidth=width;
  if(line.find("Dashed")!=std::string::npos)p.style.dash=render::DashPattern(4,4);
  out->push_back(p);
 };
 auto shaded=[&](const std::vector<Vec2>& pts,Vec2 center){
  // ChemDraw's shaded primitives use a displaced white highlight, not a flat fill.
  emit(pts,true,true,ink,true);Vec2 highlight=center;double minx=1e30,miny=1e30;for(auto p:pts){minx=std::min(minx,p.x);miny=std::min(miny,p.y);}highlight=highlight+(Vec2(minx,miny)-highlight)*.45;
  for(int j=1;j<=32;++j){double t=double(j)/33,scale=1-t;std::vector<Vec2> ring;for(auto p:pts)ring.push_back(highlight+(p-highlight)*scale);auto mix=[&](int shift){int c=(ink>>shift)&255;return uint32_t(c+(255-c)*(1-(1-t)*(1-t)));};uint32_t c=(mix(24)<<24)|(mix(16)<<16)|(mix(8)<<8)|255;emit(ring,true,true,c,false);}
 };
 auto ellipse=[&](Vec2 center,Vec2 u,Vec2 v){std::vector<Vec2> pts;for(int i=0;i<96;++i){double t=i*6.283185307179586/96;pts.push_back(center+u*cos(t)+v*sin(t));}return pts;};
 if(arrow){
  if(!e->Attribute("AngularSize")||e->Attribute("ArrowShaftSpacing"))return false;
  Vec2 tail=point(e,"Tail3D"),head=point(e,"Head3D"),center=point(e,"Center3D"),u=point(e,"MajorAxisEnd3D")-center,v=point(e,"MinorAxisEnd3D")-center;
  double determinant=u.x*v.y-u.y*v.x;if(std::abs(determinant)<1e-8)return false;
  auto angle=[&](Vec2 p){p=p-center;return atan2((u.x*p.y-u.y*p.x)/determinant,(p.x*v.y-p.y*v.x)/determinant);};
  double start=angle(tail),finish=angle(head),sweep=finish-start;while(sweep<0)sweep+=6.283185307179586;while(sweep>=6.283185307179586)sweep-=6.283185307179586;
  double wanted=std::abs(e->DoubleAttribute("AngularSize"))*3.141592653589793/180.;if(std::abs(6.283185307179586-sweep-wanted)<std::abs(sweep-wanted))sweep-=6.283185307179586;
  std::vector<Vec2> pts;int count=std::max(24,int(std::ceil(std::abs(sweep)*std::max(length(u),length(v))/.4)));count=std::min(count,2048);
  for(int i=0;i<=count;i++){double t=start+sweep*i/count;pts.push_back(center+u*cos(t)+v*sin(t));}pts.front()=tail;pts.back()=head;
  double hlen=e->DoubleAttribute("HeadSize",800)*width/100.,hwidth=e->DoubleAttribute("ArrowheadWidth",200)*width/100.,hcenter=e->DoubleAttribute("ArrowheadCenterSize",700)*width/100.;
  auto hasHead=[](const std::string& h){return !h.empty()&&h!="None";};std::string hh=attr(e,"ArrowheadHead"),ht=attr(e,"ArrowheadTail"),shape=attr(e,"ArrowheadType");
  auto trim=[&](double amount){while(pts.size()>2&&length(pts[1]-pts[0])<amount){amount-=length(pts[1]-pts[0]);pts.erase(pts.begin());}if(pts.size()>1){Vec2 d=pts[1]-pts[0];double l=length(d);if(l>0)pts[0]=pts[0]+d*(std::min(amount,l)/l);}};
  if(hasHead(ht))trim(hcenter);if(hasHead(hh)){std::reverse(pts.begin(),pts.end());trim(hcenter);std::reverse(pts.begin(),pts.end());}emit(pts,false,false,ink,true);
  auto tip=[&](Vec2 p,double t,bool begin,const std::string& h){if(!hasHead(h))return;Vec2 d=(u*(-sin(t))+v*cos(t))*(sweep<0?-1.:1.);if(begin)d=d*(-1);double l=length(d);if(l<1e-8)return;d=d*(1/l);Vec2 n(-d.y,d.x);bool left=h!="HalfRight",right=h!="HalfLeft";
   std::vector<Vec2> outline;if(left)outline.push_back(p-d*hlen+n*hwidth);outline.push_back(p);if(right)outline.push_back(p-d*hlen-n*hwidth);
   if(shape=="Angle")emit(outline,false,false,ink,true);else{outline.push_back(p-d*hcenter);emit(outline,true,shape!="Hollow",ink,shape=="Hollow");}
  };tip(tail,start,true,ht);tip(head,start+sweep,false,hh);return true;
 }
 if(curve){
  std::vector<Vec2> c;std::istringstream input(attr(e,"CurvePoints"));double x,y;while(input>>x>>y)c.push_back(Vec2(x,y));if(c.size()<3)return true;
  bool closed=(e->IntAttribute("CurveType")&1)||attr(e,"Closed")=="yes";std::vector<Vec2> pts;size_t n=c.size()/3;
  for(size_t k=0;k<(closed?n:n-1);++k){Vec2 p0=c[k*3+1],p1=c[k*3+2],p2=c[((k+1)%n)*3],p3=c[((k+1)%n)*3+1];for(int j=0;j<24;++j){double t=double(j)/24,u=1-t;pts.push_back(p0*(u*u*u)+p1*(3*u*u*t)+p2*(3*u*t*t)+p3*(t*t*t));}}
  if(!closed)pts.push_back(c[(n-1)*3+1]);
  bool filled=fill=="Solid"||fill=="Shaded"||(e->IntAttribute("CurveType")&128);if(fill=="Shaded"){Vec2 center;for(auto p:pts)center=center+p;shaded(pts,center*(1./pts.size()));}else emit(pts,closed,filled,ink,true);
  return true;
 }
 if(type=="Orbital"||type=="Oval"){
  Vec2 center=point(e,"Center3D",type=="Orbital"?b:(a+b)*.5),u=point(e,"MajorAxisEnd3D",a)-center,v=point(e,"MinorAxisEnd3D",center+Vec2(-u.y,u.x))-center;
  if(type=="Oval"&&!e->Attribute("Center3D")){u=Vec2((a.x-b.x)*.5,0);v=Vec2(0,(a.y-b.y)*.5);}
  std::string orbital=attr(e,"OrbitalType"),oval=attr(e,"OvalType");std::vector<Vec2> pts;
  if(orbital.find("lobe")!=std::string::npos){Vec2 axis=a-b;double l=length(axis);Vec2 side=l>0?Vec2(-axis.y,axis.x)*.35:Vec2();for(int i=0;i<96;++i){double t=i*6.283185307179586/96;double along=(1-cos(t))*.5;pts.push_back(b+axis*along+side*(sin(t)*sqrt(along)));}}
  else pts=ellipse(center,u,v);
  if(orbital.find("Shaded")!=std::string::npos||oval.find("Shaded")!=std::string::npos||fill=="Shaded")shaded(pts,center);
  else emit(pts,true,orbital.find("Filled")!=std::string::npos||oval.find("Filled")!=std::string::npos||fill=="Solid",ink,true);return true;
 }
 if(type=="Rectangle"){emit({a,Vec2(b.x,a.y),b,Vec2(a.x,b.y)},true,fill=="Solid"||attr(e,"RectangleType").find("Filled")!=std::string::npos,ink,true);return true;}
 if(type=="Bracket"){
  Vec2 axis=b-a;double len=length(axis);Vec2 n=len>0?Vec2(-axis.y,axis.x)*(std::min(5.,len*.2)/len):Vec2(4,0);std::string bracket=attr(e,"BracketType");
  if(bracket=="Round"){std::vector<Vec2> p;for(int i=0;i<=32;++i){double t=double(i)/32;p.push_back(a+axis*t+n*(1-4*t*(1-t)));}emit(p,false,false,ink,true);}
  else emit({a+n,a,b,b+n},false,false,ink,true);return true;
 }
 if(type=="Symbol"){
  // CDSymbol::DrawGraphic (0x35E4B2 family): BoundingBox contains the
  // symbol anchor and its size/second-radical handle, not an ink rectangle.
  std::string sym=attr(e,"SymbolType");double span=length(a-b),originalWidth=width;width*=.8;
  auto dot=[&](Vec2 p,double radius){emit(ellipse(p,Vec2(radius,0),Vec2(0,radius)),true,true,ink,false);};
  if(sym=="Electron"){dot(a,span/9.);return true;}
  if(sym=="LonePair"){dot(a,span*2/9.);dot(b,span*2/9.);return true;}
  if(sym=="DoubleDagger"||sym=="Dagger"){double r=span/5.;emit({a-Vec2(0,2*r+originalWidth*.5),a+Vec2(0,2*r+originalWidth*.5)},false,false,ink,true);emit({a+Vec2(-r-originalWidth*.5,-r),a+Vec2(r+originalWidth*.5,-r)},false,false,ink,true);if(sym=="DoubleDagger")emit({a+Vec2(-r-originalWidth*.5,r),a+Vec2(r+originalWidth*.5,r)},false,false,ink,true);return true;}
  Vec2 center=a;double r=span*2/9.+originalWidth*.5;
  if(sym=="RadicalAnion"||sym=="RadicalCation"){dot(a,span*2/9.);center=b;r=span*4/9.+originalWidth*.5;}
  emit({center-Vec2(r,0),center+Vec2(r,0)},false,false,ink,true);if(sym=="Plus"||sym=="RadicalCation")emit({center-Vec2(0,r),center+Vec2(0,r)},false,false,ink,true);return true;
 }
 if(type=="Line"){emit({a,b},false,false,ink,true);return true;}
 if(type=="Arc"){
  Vec2 center=point(e,"Center3D",b),u=point(e,"MajorAxisEnd3D",a)-center,v=point(e,"MinorAxisEnd3D",center+Vec2(-u.y,u.x))-center;double angle=e->DoubleAttribute("AngularSize",90)*3.141592653589793/180;std::vector<Vec2> pts;for(int i=0;i<=64;i++){double t=angle*i/64;pts.push_back(center+u*cos(t)+v*sin(t));}emit(pts,false,false,ink,true);return true;
 }
 return false;
}
}}}
#endif
