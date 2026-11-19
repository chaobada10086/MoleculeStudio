from pathlib import Path
import subprocess
for name in ['engine/next/experimental/render_bridge.cpp','studio/engine/experimental_render_bridge.cpp']:
 p=Path(name);s=p.read_text(encoding='utf-8');head=subprocess.check_output(['git','show','HEAD:'+name]).decode('utf-8')
 a=s.index('        const Spot& spot = figure.spots[i];');b=s.index('        const std::vector<core::Vec2> poly = bandPolygon',a)
 ha=head.index('        const Spot& spot = figure.spots[i];');hb=head.index('        const std::vector<core::Vec2> poly = bandPolygon',ha)
 s=s[:a]+head[ha:hb]+s[b:]
 a=s.rindex('        const Spot& spot = figure.spots[i];')+len('        const Spot& spot = figure.spots[i];');b=s.index('        const std::vector<core::Vec2> poly =',a)
 s=s[:a]+'''
        for(const auto& slice:studio_tlc::tailSlices(spot)){
            std::vector<core::Vec2> tail;for(auto uv:slice.points)tail.push_back(frame.pointAt(uv.x,uv.y));
            render::Style fade=style;fade.stroke.enabled=false;
            fade.fill=render::Paint(withOpacity(spot.fillRGBA,slice.opacity),true);
            out->push_back(makeFilled(tail,fade,source,version,layer,baseZ+5));
        }
'''+s[b:];p.write_text(s,encoding='utf-8')
p=Path('studio/studio.cpp');s=p.read_text(encoding='utf-8');s=s.replace('RectF(gr.r.X+3,gr.r.Y+29,gr.r.Width-6,16)','RectF(gr.r.X+3,gr.r.GetBottom()-19,gr.r.Width-6,16)');p.write_text(s,encoding='utf-8')
# Verify the width-dependent real layout and cover the missing wider-window case.
p=Path('studio/current_fixes_checks.hpp');s=p.read_text(encoding='utf-8');needle='    checkCompact(1);if(!graphicsAutoCompact)';i=s.index(needle)
s=s[:i]+'''    int previousVisible=0;bool savedCompact=ribbonCompact;ribbonCompact=false;
    for(int logicalWidth:{900,1100,1300,1500}){
        SetWindowPos(h,nullptr,0,0,int(logicalWidth*scale),int(800*scale),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);ribbonTab=0;layout();int shown=0;
        for(const auto& group:groups){if(group.r.X<0||group.r.Y!=80.f)throw std::runtime_error("home group hidden or wrapped");for(const auto& button:group.items)if(button.r.X>=0){++shown;if(button.r.GetBottom()>group.r.GetBottom()-19)throw std::runtime_error("home icon overlaps group caption");}for(const auto& button:chemistryQuick)if(button.r.X>=0&&button.r.IntersectsWith(group.r))throw std::runtime_error("home tool overlaps quick action");}
        if(shown<=18||shown<previousVisible)throw std::runtime_error("home does not progressively expand");previousVisible=shown;
        screenshot(dir+"/ribbon-home-"+std::to_string(logicalWidth)+".png");
    }
    ribbonCompact=savedCompact;SetWindowPos(h,nullptr,0,0,int(900*scale),int(800*scale),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
'''+s[i:];p.write_text(s,encoding='utf-8')
# Drop the pointer between lanes, not directly on a column as the old test did.
p=Path('studio/tlc_checks.inc');s=p.read_text(encoding='utf-8');s=s.replace('auto to=bounds.pointAt(board.lanes[2].u,originalSpot.v-.12);','auto to=bounds.pointAt(board.lanes[2].u+.05,originalSpot.v-.12);');s=s.replace('moved->laneId==board.lanes[2].id&&','moved->laneId==board.lanes[2].id&&std::abs(moved->u-board.lanes[2].u)<1e-8&&')
s=s.replace('auto point=frame.pointAt(candidate.lanes[1].u,.47);','auto point=frame.pointAt(candidate.lanes[1].u+.03,.95);');s=s.replace('clicked.spots.back().laneId==candidate.lanes[1].id&&','clicked.spots.back().laneId==candidate.lanes[1].id&&std::abs(clicked.spots.back().u-candidate.lanes[1].u)<1e-8&&std::abs(studio_tlc::rf(clicked,clicked.spots.back()))<1e-8&&')
p.write_text(s,encoding='utf-8')
