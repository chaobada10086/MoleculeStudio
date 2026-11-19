#include "next/integration/session.hpp"
#include "next/arrows_curves/model.hpp"
#include <iostream>
using namespace r62;using namespace core;
int main(){integration::Session s;auto e=s.open("work/test-r43-crossing/balanced-crossing.cdxml");if(!e.ok())return 1;Vec2 shift;arrows::ArrowParams curved;bool found=false,first=false;for(auto a:s.doc.arrows().records())if(a.isAlive()){arrows::ArrowParams p;bool valid=false;arrows::readArrow(s.doc,a.id,&p,&valid);if(!valid)return 2;if(p.route.empty()){shift=p.begin-Vec2(40,130);first=true;if(length((p.end-p.begin)-Vec2(190,0))>1e-4)return 3;}else if(p.route.size()==129){curved=p;found=true;}}if(!first||!found)return 4;if(length(curved.begin-Vec2(130,30)-shift)>1e-4||length(curved.end-Vec2(130,230)-shift)>1e-4)return 5;std::cout<<"CDXML page translation preserves both fixed endpoints and editable S curve\n";return 0;}
