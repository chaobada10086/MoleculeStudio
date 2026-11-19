#pragma once
namespace arc_properties {
using r62::core::Vec2;
struct Circle {bool valid=false;Vec2 center;double radius=0,sweep=0,start=0;};
inline Circle read(const r62::arrows::ArrowParams& p){
    Circle c;auto d=p.end-p.begin;double chord=r62::core::length(d),s=p.arcSagitta;if(chord<1e-8||std::abs(s)<1e-8||!p.route.empty())return c;
    auto normal=Vec2(-d.y,d.x)*(1/chord);double offset=(s*s-chord*chord*.25)/(2*s);c.center=(p.begin+p.end)*.5+normal*offset;c.radius=std::hypot(chord*.5,offset);c.sweep=-4*std::atan2(2*s,chord);c.start=std::atan2(p.begin.y-c.center.y,p.begin.x-c.center.x);c.valid=std::isfinite(c.radius);return c;
}
inline bool write(const Circle& c,r62::arrows::ArrowParams& p){
    if(!c.valid||c.radius<1e-8||std::abs(c.sweep)<1e-8||std::abs(c.sweep)>=6.283185307-1e-6)return false;
    auto point=[&](double a){return c.center+Vec2(std::cos(a),std::sin(a))*c.radius;};p.begin=point(c.start);p.end=point(c.start+c.sweep);
    p.arcSagitta=-r62::core::length(p.end-p.begin)*.5*std::tan(c.sweep*.25);p.route.clear();p.family=r62::arrows::ArrowFamily::Arc;p.sweep=r62::arrows::ArcSweep::Free;return true;
}
}
