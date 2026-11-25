#pragma once
#include "mechanism_step_plan.hpp"

namespace studio_local_cleanup {
inline Report run(HWND owner, Document& doc, const Document& source, const std::vector<ObjectRef>& made) {
    return run(doc, source, made, [owner](const std::string& input, std::string& output) {
        return chemservice::run(owner, input, "text/xml", "text/xml", "clean", &output);
    });
}
}

App::StepPlan App::planMechanismStep(const Document& doc,const std::vector<ObjectRef>& selection,const std::vector<ObjectRef>& scene,int mode,int arrowStyle,bool interactive,bool keepLossStructures){
    return studio_mechanism_step::planStep(doc, selection, scene, mode, arrowStyle, keepLossStructures,
        [&](Document& product, const Document& source, const std::vector<ObjectRef>& made) {
            if(interactive){status="正在局部整理反应中心附近的支链…（Esc 取消）";invalidate(false);if(!testing)UpdateWindow(h);}
            const auto report = studio_local_cleanup::run(h, product, source, made);
            if(testing)std::ofstream(testOut+"/local-cleanup-progress.txt",std::ios::app)<<"movable="<<report.movable<<" attempted="<<report.attempted<<" changed="<<report.changed<<" "<<report.detail<<"\n";
            return report;
        });
}

void App::generateMechanismStep(int mode,int arrowStyle){
    s.cancel();
    try {
        auto plan=planMechanismStep(s.doc,s.selected,s.all(),mode,arrowStyle,true);
        auto result=s.submit(plan.command,"生成机理下一步");
        if(!result.ok())throw std::runtime_error(result.message);
        s.selected=*plan.created;
        s.activateCommand("chooseSelectionTool_Marquee");
        s.fit();updateInfo();
        status=std::string(*plan.wrapped?"已换到下一行左侧生成下一步结构（":"已在右侧生成下一步结构（")+plan.detail;
    } catch(const std::exception& e){
        status=e.what();
        if(!testing)MessageBoxW(h,W(status).c_str(),L"未生成下一步",MB_OK|MB_ICONINFORMATION);
    }
    invalidate();
}
