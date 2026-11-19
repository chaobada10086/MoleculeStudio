#include "next/arrows_curves/geometry.hpp"
#include <iostream>
#include <fstream>
using namespace r62;using namespace r62::arrows;
int main(){
for(double a:{0.,45.,90.,180.,270.})for(double w:{.4,.6,1.2,2.4})for(double bend:{-35.,35.})for(HeadSides side:{HeadSides::Left,HeadSides::Right}){
 auto p=resolvePreset(57,core::Vec2(),core::direction(a)*100,w);p.mechanism=true;p.head.length=3.2;p.head.width=2.24;p.strokeWidth=w;p.arcSagitta=bend;p.head.sides=side;ArrowGeometry g;buildArrowGeometry(p,w,Tessellation(),&g);auto q=g.shafts[0].back();const auto& poly=g.heads[0].outline;bool inside=false;double distance=1e100;
 for(size_t i=0,j=poly.size()-1;i<poly.size();j=i++){auto x=poly[i],y=poly[j];if((x.y>q.y)!=(y.y>q.y)&&q.x<(y.x-x.x)*(q.y-x.y)/(y.y-x.y)+x.x)inside=!inside;auto d=y-x;double dd=d.x*d.x+d.y*d.y;double t=dd>1e-12?std::max(0.,std::min(1.,((q.x-x.x)*d.x+(q.y-x.y)*d.y)/dd)):0;distance=std::min(distance,core::length(q-(x+d*t)));}
 if(!inside&&distance>w*.45){std::cerr<<"fishhook gap a="<<a<<" width="<<w<<" bend="<<bend<<" gap="<<distance;return 6;}
}
std::cout<<"fishhook junction matrix passed\n";
for(size_t i=0;i<arrowPresetCount();++i)for(int tier=0;tier<5;++tier)for(double a:{0.,90.,135.,180.})for(double width:{.6,1.2}){
 auto p=withHeadTier(resolvePreset(arrowPresets()[i].presetId,core::Vec2(20,30),core::Vec2(20,30)+core::direction(a)*100,width),tier);if(p.presetId==49||p.presetId==57){p.mechanism=true;p.head.length=3.2;p.head.width=2.24;}
 ArrowGeometry g;auto e=buildArrowGeometry(p,width,Tessellation(),&g);if(!e.ok()){std::cerr<<e.message;return 4;}
 if(p.family==ArrowFamily::Arc&&p.shafts==ShaftCount::Double)for(size_t k=0;k<2;++k){const auto& head=g.heads[k];auto join=head.filled&&!g.hollow&&head.outline.size()==4?head.outline[2]:(head.outline[1]+head.outline[head.outline.size()==4?3:2])*.5;const auto& line=g.shafts[k];double gap=std::min(core::length(join-line.front()),core::length(join-line.back()));if(gap>1e-6){std::cerr<<"join preset="<<p.presetId<<" tier="<<tier<<" angle="<<a<<" width="<<width<<" head="<<k<<" gap="<<gap;return 5;}}
}
std::cout<<"all double arc joins passed\n";std::ofstream f("work/arrow-probe.json");f<<"[";int row=0;
for(unsigned preset:{0u,2u,38u,39u,39u,49u,57u,57u}){auto p=withHeadTier(resolvePreset(preset,core::Vec2(80,35+row*48),core::Vec2(250,35+row*48),.6),2);if(row==3)p.splitRatio=.28;if(row==4)p.splitRatio=.72;if(preset==49||preset==57){p.mechanism=true;p.head.length=5;p.head.width=3.5;p.arcSagitta=row==7?-50:50;}ArrowGeometry g;auto e=buildArrowGeometry(p,.6,Tessellation(),&g);if(!e.ok()){std::cerr<<e.message;return 2;}if(row)f<<",";f<<"{\"shafts\":[";bool first=true;auto pts=[&](const std::vector<core::Vec2>& v){f<<"[";for(size_t i=0;i<v.size();++i){if(i)f<<",";f<<"["<<v[i].x<<","<<v[i].y<<"]";}f<<"]";};for(auto& s:g.shafts){if(!first)f<<",";first=false;pts(s);}f<<"],\"heads\":[";first=true;for(auto& h:g.heads){if(!first)f<<",";first=false;pts(h.outline);}f<<"]}";row++;}f<<"]";}
