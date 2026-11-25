#pragma once
#include "conformer_model.hpp"
void App::conformer3D(){
    studio_conformer::generate(s,chemicalScope(),status,[&](const std::string& input,std::string& output){
        status="正在生成三维构象…（RDKit）";invalidate(false);UpdateWindow(h);
        return chemservice::run(h,input,"text/plain","text/plain","conformer3d",&output);
    });
}