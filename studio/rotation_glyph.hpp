#pragma once
#include "next/core/document.hpp"
#include <vector>
#include <cmath>
namespace studio_rotation {
inline std::vector<r62::core::Vec2> arc(double radius){std::vector<r62::core::Vec2> points;for(int i=0;i<=40;++i){double angle=(45.+315.*i/40)*3.141592653589793/180;points.push_back(r62::core::Vec2(std::cos(angle)*radius,std::sin(angle)*radius));}return points;}
inline std::vector<r62::core::Vec2> arrow(){return {r62::core::Vec2(4.5,0),r62::core::Vec2(13.5,0),r62::core::Vec2(9,6)};}
}
