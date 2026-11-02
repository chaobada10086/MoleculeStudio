#ifndef STUDIO_TLC_PREFERENCES_HPP
#define STUDIO_TLC_PREFERENCES_HPP
#include "next/experimental/model.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <cstdio>
namespace studio_tlc {
struct Preferences {
    int lanes=4,spots=1;
    double diameter=4;
    bool guides=false,origin=true,front=true,showRf=false,customTemplate=false;
    r62::experimental::PlateFigure plateTemplate;
};
inline Preferences& preferences(){static Preferences value;return value;}
inline void resizeSpots(r62::experimental::Spot& spot,const r62::experimental::PlateFrame& frame,double diameter){
    spot.radiusU=std::max(.001,std::min(.20,diameter*.5/std::max(1.,frame.halfWidth())));
    spot.radiusV=std::max(.001,std::min(.20,diameter*.5/std::max(1.,frame.halfHeight())));
}
// Keep surviving element identities, measurements, captions and positions. Only
// a requested count change adds/removes elements; changing display is lossless.
inline r62::experimental::PlateFigure configure(r62::experimental::PlateFigure figure,
        const r62::experimental::PlateFrame& frame,const Preferences& options,bool counts=true){
    using namespace r62::experimental;
    figure.showLaneBorders=options.guides;figure.originDefined=options.origin;figure.frontDefined=options.front;
    if(!counts){for(auto& spot:figure.spots)resizeSpots(spot,frame,options.diameter);return figure;}
    const size_t laneCount=size_t(std::max(1,std::min(12,options.lanes)));
    const size_t spotCount=size_t(std::max(0,std::min(10,options.spots)));
    const auto oldLanes=figure.lanes;const auto oldSpots=figure.spots;
    const bool redistribute=oldLanes.size()!=laneCount;
    figure.lanes.resize(laneCount);
    for(size_t i=0;i<laneCount;++i){auto& lane=figure.lanes[i];
        if(i>=oldLanes.size()){lane.id=allocateElementId(&figure);lane.label=std::to_string(i+1);}
        if(redistribute){lane.u=laneCount==1?0:-.72+1.44*i/(laneCount-1);lane.width=std::min(.5,1.44/laneCount*.55);}
    }
    std::map<uint32_t,std::vector<Spot>> byLane;
    for(auto spot:oldSpots){uint32_t laneId=spot.laneId;
        if(!laneId&&!oldLanes.empty()){auto nearest=std::min_element(oldLanes.begin(),oldLanes.end(),[&](const Lane& a,const Lane& b){return std::abs(a.u-spot.u)<std::abs(b.u-spot.u);});laneId=nearest->id;}
        spot.laneId=laneId;byLane[laneId].push_back(spot);
    }
    figure.spots.clear();
    for(const auto& lane:figure.lanes){auto& old=byLane[lane.id];auto previous=std::find_if(oldLanes.begin(),oldLanes.end(),[&](const Lane& item){return item.id==lane.id;});
        for(size_t i=0;i<spotCount;++i){Spot spot;
            if(i<old.size()){spot=old[i];if(redistribute&&previous!=oldLanes.end())spot.u=std::max(-.95,std::min(.95,spot.u+lane.u-previous->u));}
            else {spot.id=allocateElementId(&figure);spot.laneId=lane.id;spot.u=lane.u;
                // These are illustrative positions, never measured Rf values.
                spot.v=figure.originV+(figure.frontV-figure.originV)*double(i+1)/double(spotCount+1);
                for(int attempt=0;attempt<20;++attempt){bool collision=false;for(const auto& placed:figure.spots)if(placed.laneId==lane.id&&std::abs(placed.v-spot.v)<.08)collision=true;if(!collision)break;spot.v+=.09;if(spot.v>.52)spot.v=-.65;}
            }
            resizeSpots(spot,frame,options.diameter);figure.spots.push_back(spot);
        }
    }
    // A band owned by a removed lane cannot remain as a dangling reference.
    figure.bands.erase(std::remove_if(figure.bands.begin(),figure.bands.end(),[&](const Band& band){return !findLane(figure,band.laneId);}),figure.bands.end());
    return figure;
}
inline r62::experimental::PlateFigure create(const r62::experimental::PlateFrame& frame){
    if(preferences().customTemplate){auto figure=preferences().plateTemplate;for(auto& spot:figure.spots)resizeSpots(spot,frame,preferences().diameter);return figure;}
    auto figure=r62::experimental::makeDefaultFigure(r62::experimental::FigureKind::TlcPlate,preferences().lanes);
    figure.plateFillRGBA=0xFFFFFFFFu;
    figure=configure(figure,frame,preferences());
    for(auto& lane:figure.lanes)lane.label.clear();
    return figure;
}
inline double rf(const r62::experimental::PlateFigure& figure,const r62::experimental::Spot& spot){
    double span=figure.originV-figure.frontV;return span>1e-8?(figure.originV-spot.v)/span:0;
}
inline void setRf(r62::experimental::PlateFigure& figure,r62::experimental::Spot& spot,double value){
    value=std::max(0.,std::min(1.,value));spot.v=figure.originV-value*(figure.originV-figure.frontV);
    if(spot.rfMeasured.present){spot.rfMeasured.value=value;spot.rfMeasured.origin=r62::experimental::MeasureOrigin::UserEntered;spot.rfMeasured.source="用户修改 Rf";}
}
inline std::vector<uint32_t> spotsIn(const r62::experimental::PlateFigure& figure,uint32_t lane){
    std::vector<uint32_t> result;for(const auto& spot:figure.spots)if(spot.laneId==lane)result.push_back(spot.id);return result;
}
inline void assignUnowned(r62::experimental::PlateFigure& figure){
    for(auto& spot:figure.spots)if(!spot.laneId&&!figure.lanes.empty())spot.laneId=std::min_element(figure.lanes.begin(),figure.lanes.end(),[&](const r62::experimental::Lane& a,const r62::experimental::Lane& b){return std::abs(a.u-spot.u)<std::abs(b.u-spot.u);})->id;
}
inline uint32_t addSpot(r62::experimental::PlateFigure& figure,const r62::experimental::PlateFrame& frame,uint32_t laneId,double diameter,double value=.5){
    auto lane=r62::experimental::findLane(figure,laneId);if(!lane)return 0;
    r62::experimental::Spot spot;spot.id=r62::experimental::allocateElementId(&figure);spot.laneId=laneId;spot.u=lane->u;setRf(figure,spot,value);resizeSpots(spot,frame,diameter);figure.spots.push_back(spot);return spot.id;
}
inline void setSpotCount(r62::experimental::PlateFigure& figure,const r62::experimental::PlateFrame& frame,uint32_t laneId,int count,double diameter){
    auto owned=spotsIn(figure,laneId);count=std::max(0,std::min(30,count));
    for(size_t i=size_t(count);i<owned.size();++i)figure.spots.erase(std::remove_if(figure.spots.begin(),figure.spots.end(),[&](const r62::experimental::Spot& spot){return spot.id==owned[i];}),figure.spots.end());
    while(int(owned.size())<count){
        // Fill the largest free Rf interval without moving existing spots.
        std::vector<double> positions={0.,1.};for(auto id:owned){auto spot=r62::experimental::findSpot(figure,id);if(spot)positions.push_back(std::max(0.,std::min(1.,rf(figure,*spot))));}std::sort(positions.begin(),positions.end());
        double value=.5,gap=-1;for(size_t i=1;i<positions.size();++i)if(positions[i]-positions[i-1]>gap){gap=positions[i]-positions[i-1];value=(positions[i]+positions[i-1])*.5;}
        owned.push_back(addSpot(figure,frame,laneId,diameter,value));
    }
}
inline void distributeLanes(r62::experimental::PlateFigure& figure){
    for(size_t i=0,n=figure.lanes.size();i<n;++i){auto& lane=figure.lanes[i];double u=n==1?0:-.72+1.44*i/(n-1),delta=u-lane.u;for(auto& spot:figure.spots)if(spot.laneId==lane.id)spot.u=std::max(-.98,std::min(.98,spot.u+delta));lane.u=u;lane.width=std::min(.5,1.44/n*.55);}
}
inline uint32_t addLane(r62::experimental::PlateFigure& figure){
    if(figure.lanes.size()>=12)return 0;r62::experimental::Lane lane;lane.id=r62::experimental::allocateElementId(&figure);figure.lanes.push_back(lane);distributeLanes(figure);return lane.id;
}
inline void removeLane(r62::experimental::PlateFigure& figure,uint32_t laneId){
    if(figure.lanes.size()<=1)return;
    figure.spots.erase(std::remove_if(figure.spots.begin(),figure.spots.end(),[&](const r62::experimental::Spot& spot){return spot.laneId==laneId;}),figure.spots.end());
    figure.bands.erase(std::remove_if(figure.bands.begin(),figure.bands.end(),[&](const r62::experimental::Band& band){return band.laneId==laneId;}),figure.bands.end());
    figure.lanes.erase(std::remove_if(figure.lanes.begin(),figure.lanes.end(),[&](const r62::experimental::Lane& lane){return lane.id==laneId;}),figure.lanes.end());distributeLanes(figure);
}
struct RfLabel {uint32_t spot;std::string text;r62::core::Vec2 position;double size;};
inline std::vector<RfLabel> rfLabels(const r62::experimental::PlateFigure& figure,const r62::experimental::PlateFrame& frame,double defaultSize){
    using r62::core::Vec2;using r62::core::Box;
    std::vector<RfLabel> result;std::vector<Box> occupied;auto board=frame.aabb();
    auto area=[](const Box& a,const Box& b){return std::max(0.,std::min(a.max.x,b.max.x)-std::max(a.min.x,b.min.x))*std::max(0.,std::min(a.max.y,b.max.y)-std::max(a.min.y,b.min.y));};
    double pitch=figure.lanes.size()>1?frame.halfWidth()*1.44/(figure.lanes.size()-1):frame.halfWidth();
    double size=std::max(2.,std::min(defaultSize*.65,pitch/4.2)),height=size*1.35,gap=std::max(1.,size*.2);
    for(const auto& spot:figure.spots){if(figure.originV-figure.frontV<=1e-8)continue;char text[48];std::snprintf(text,sizeof(text),"Rf %.2f",rf(figure,spot));double width=std::string(text).size()*size*.56;
        Vec2 p=frame.pointAt(spot.u,spot.v);double rx=spot.radiusU*frame.halfWidth(),ry=spot.radiusV*frame.halfHeight();
        std::vector<Vec2> candidates={Vec2(p.x-width*.5,p.y+ry+gap),Vec2(p.x+rx+gap,p.y-height*.5),Vec2(p.x-rx-gap-width,p.y-height*.5),Vec2(p.x-width*.5,p.y-ry-gap-height)};
        Vec2 best=candidates[0];double score=1e100;Box chosen;
        for(size_t i=0;i<candidates.size();++i){auto anchor=candidates[i];Box box(anchor-Vec2(.5,.5),anchor+Vec2(width+.5,height+.5));double cost=i*.001;
            for(const auto& other:figure.spots){auto center=frame.pointAt(other.u,other.v);double x=other.radiusU*frame.halfWidth()+gap*.5,y=other.radiusV*frame.halfHeight()+gap*.5;cost+=1000*area(box,Box(center-Vec2(x,y),center+Vec2(x,y)));}
            for(const auto& placed:occupied)cost+=10*area(box,placed);cost+=std::max(0.,(box.max.x-box.min.x)*(box.max.y-box.min.y)-area(box,board));
            if(cost<score){score=cost;best=anchor;chosen=box;}
        }
        RfLabel label;label.spot=spot.id;label.text=text;label.position=best;label.size=size;result.push_back(label);occupied.push_back(chosen);
    }
    return result;
}
}
#endif
