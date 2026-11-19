from pathlib import Path
root=Path.cwd()
def edit(name,old,new):
 p=root/name;s=p.read_text(encoding='utf-8');assert old in s,name;s=s.replace(old,new,1);p.write_text(s,encoding='utf-8')
# Share the exact same continuous, tapered tail in the editor and renderer.
p=root/'studio/tlc_preferences.hpp';s=p.read_text(encoding='utf-8');pos=s.index('struct Preferences')
s=s[:pos]+'''struct TailSlice {std::vector<r62::core::Vec2> points;double opacity;};
inline std::vector<TailSlice> tailSlices(const r62::experimental::Spot& spot){
    std::vector<TailSlice> result;
    double length=std::min(spot.tailLengthV,std::max(0.,.98-spot.v));
    if(length<=1e-5||spot.tailOpacity<=1e-5)return result;
    const int count=64;
    for(int i=0;i<count;++i){double a=double(i)/count,b=double(i+1)/count;
        double wa=spot.radiusU*.92*std::sqrt(1-a),wb=spot.radiusU*.92*std::sqrt(1-b);
        TailSlice slice;slice.opacity=spot.opacity*spot.tailOpacity*(1-.85*(a+b)*.5);
        slice.points={{spot.u-wa,spot.v+a*length},{spot.u+wa,spot.v+a*length},{spot.u+wb,spot.v+b*length},{spot.u-wb,spot.v+b*length}};
        result.push_back(slice);
    }return result;
}
''' + s[pos:];p.write_text(s,encoding='utf-8')
# Both render bridges must include the tail, including the build's active override.
for name in ['engine/next/experimental/render_bridge.cpp','studio/engine/experimental_render_bridge.cpp']:
 p=root/name;s=p.read_text(encoding='utf-8');
 if '#include "tlc_preferences.hpp"' not in s:s='#include "tlc_preferences.hpp"\n'+s
 start=s.index('        const Spot& spot = figure.spots[i];')+len('        const Spot& spot = figure.spots[i];');end=s.index('        const std::vector<core::Vec2> poly =',start)
 s=s[:start]+'''
        for(const auto& slice:studio_tlc::tailSlices(spot)){
            std::vector<core::Vec2> tail;for(auto uv:slice.points)tail.push_back(frame.pointAt(uv.x,uv.y));
            render::Style fade=style;fade.stroke.enabled=false;
            fade.fill=render::Paint(withOpacity(spot.fillRGBA,slice.opacity),true);
            out->push_back(makeFilled(tail,fade,source,version,layer,baseZ+5));
        }
'''+s[end:];p.write_text(s,encoding='utf-8')
p=root/'studio/tlc_dialog.hpp';s=p.read_text(encoding='utf-8');a=s.index('if(spot.tailLengthV>0)for(');b=s.index('g.FillEllipse(&ink',a)
s=s[:a]+'''for(const auto& slice:studio_tlc::tailSlices(spot)){PointF vertices[4];for(int i=0;i<4;++i)vertices[i]=point(board,slice.points[i].x,slice.points[i].y);SolidBrush fade(Color(BYTE(std::round(slice.opacity*255)),40,48,48));auto mode=g.GetSmoothingMode();g.SetSmoothingMode(SmoothingModeNone);g.FillPolygon(&fade,vertices,4);g.SetSmoothingMode(mode);}'''+s[b:]
s=s.replace('spot->laneId=lane->id;laneId=lane->id;','spot->laneId=lane->id;spot->u=lane->u;laneId=lane->id;').replace('copy.u=std::max(-.98,std::min(.98,source.u+lane.u-sourceLane->u));','copy.u=lane.u;')
s=s.replace('            if(id==201&&notification', '''            if((id==232||id==233)&&notification==EN_CHANGE){if(d->commit(false))InvalidateRect(d->preview,0,FALSE);return TRUE;}
            if(id==201&&notification''');p.write_text(s,encoding='utf-8')
# Snap before drawing the ghost and reuse the snapped coordinate on commit.
edit('studio/engine/session.cpp','        if(phase==PointerPhase::Move){ghost.clear();PreviewItem preview;preview.primitive=PreviewPrimitive::Circle;preview.points={frame.pointAt(u,v)', '''        auto targetLane=std::min_element(plate.lanes.begin(),plate.lanes.end(),[&](const experimental::Lane& a,const experimental::Lane& b){return std::abs(a.u-u)<std::abs(b.u-u);});
        if(targetLane==plate.lanes.end())return Error();u=targetLane->u;
        if(phase==PointerPhase::Move){ghost.clear();PreviewItem preview;preview.primitive=PreviewPrimitive::Circle;preview.points={frame.pointAt(u,v)''')
edit('studio/engine/session.cpp','                uint32_t spotId=0;auto added=experimental::figureAddSpot(&figure,lane,u,v,&spotId);','''                auto target=experimental::findLane(figure,lane);if(!target)return Error();u=target->u;
                v=std::max(figure.frontV,std::min(figure.originV,v));
                uint32_t spotId=0;auto added=experimental::figureAddSpot(&figure,lane,u,v,&spotId);''')
# Actual width, not an arbitrary threshold, determines when Home needs fewer columns.
edit('studio/studio.cpp','homeAutoCompact=ribbonTab==0&&w<1450;','homeAutoCompact=ribbonTab==0&&(y>groupsTop||x>w-(compactQuick?12.f:318.f));')
p=root/'studio/studio.cpp';s=p.read_text(encoding='utf-8');a=s.index('        if(ribbonCompact||graphicsAutoCompact||homeAutoCompact||otherAutoCompact){x=12;');b=s.index('        if(!ribbonVisible)',a)
old=s[a:b];old=old.replace('homeAutoCompact?4:visibleGroups().size()','visibleGroups().size()')
# Other tabs keep their existing compact presentation.
old=old.replace('if(ribbonCompact||graphicsAutoCompact||homeAutoCompact||otherAutoCompact)','if(ribbonTab!=0&&(ribbonCompact||graphicsAutoCompact||otherAutoCompact))')
home='''        if(ribbonTab==0&&(ribbonCompact||homeAutoCompact)){
            x=12;y=groupsTop;gh=ribbonCompact?48.f:98.f;
            float available=w-(compactQuick?24.f:330.f);
            auto& list=visibleGroups();size_t n=list.size();
            // Every group stays reachable; spend remaining width one column at a time.
            float menuWidth=24.f;std::vector<size_t> cols(n,1);
            float used=float(n)*(26.f+menuWidth+6.f);
            bool grew=true;while(grew&&!ribbonCompact){grew=false;for(size_t i=0;i<n;++i)if(cols[i]<(list[i].items.size()+2)/3&&used+26.f<=available){++cols[i];used+=26.f;grew=true;}}
            for(size_t index=0;index<n;++index){auto& group=list[index];
                size_t rows=ribbonCompact?1:3,count=std::min(group.items.size(),cols[index]*rows);
                float gw=float(cols[index])*26+menuWidth;
                group.r=RectF(x,y,gw,gh);for(auto& item:group.items)item.r=RectF(-10000,-10000,1,1);
                for(size_t i=0;i<count;++i)group.items[i].r=RectF(x+4+float(i/rows)*26,y+4+float(i%rows)*26,24,24);
                compactMenus.push_back({RectF(x+gw-23,y+4,22,24),index});x+=gw+6;
            }
        }
'''
s=s[:a]+old+home+s[b:];p.write_text(s,encoding='utf-8')
