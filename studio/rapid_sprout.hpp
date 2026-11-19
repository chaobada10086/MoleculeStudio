#pragma once
#include "engine/sprout_layout.hpp"
#include "next/core/commands.hpp"
#include <memory>

namespace studio { namespace rapid_sprout {
using namespace r62;using namespace core;
struct State {
    bool valid=false;DocumentId document=0;ObjectId anchor=kInvalidObjectId;
    uint64_t tick=0,serial=0,revision=0;size_t history=0;std::vector<ObjectId> tips;
    void clear(){*this=State();}
    bool continues(const Document& d,ObjectId host,uint64_t now,uint64_t currentSerial,size_t cursor)const{
        return valid&&document==d.id()&&anchor==host&&now>=tick&&now-tick<=1000&&
            serial==currentSerial&&revision==d.revision()&&history==cursor&&tips.size()<4;
    }
    void record(const Document& d,ObjectId host,uint64_t now,uint64_t currentSerial,size_t cursor,std::vector<ObjectId> added){
        valid=true;document=d.id();anchor=host;tick=now;serial=currentSerial;revision=d.revision();history=cursor;tips=std::move(added);
    }
};
struct Placement {bool arranged=false;std::vector<Vec2> positions;};
inline bool plainSingle(const BondRecord& b){return b.order==BondOrder::Single&&b.semantics==BondSemantics::Normal&&b.display==BondDisplay::Normal&&b.stereo==BondStereo::None;}
// Reflow only leaves created by this burst. Everything predating it is a frame.
inline Placement arrange(const Document& d,ObjectId host,const std::vector<ObjectId>& recent){
    Placement result;const auto* center=d.atoms().alive(host);
    // Chair substituents belong to axial/equatorial slots, even within a burst.
    // Never reflow a previously drawn slot into the generic +/-45 degree fan.
    if(chair_layout::frame(d,host).valid)return result;
    if(!center||recent.empty()||recent.size()>3||center->semantics.aromatic||center->semantics.radical||center->semantics.nodeType!=uint8_t(NodeType::Element))return result;
    std::set<ObjectId> movable(recent.begin(),recent.end());if(movable.size()!=recent.size())return result;
    std::vector<double> fixed;Vec2 inward;
    for(auto bid:d.incidentBonds(host)){
        const auto& b=*d.bonds().alive(bid);if(!plainSingle(b))return result;
        auto other=b.beginAtom==host?b.endAtom:b.beginAtom;
        if(!movable.count(other)){auto v=d.atoms().alive(other)->position-center->position;double span=length(v);if(span<1e-8)return result;inward=inward+v*(1./span);fixed.push_back(angleOf(v));}
    }
    for(auto id:recent){const auto* a=d.atoms().alive(id);if(!a||a->element!=6||a->semantics.charge||a->semantics.isotope||a->semantics.radical||!d.atomLabel(id).empty()||d.incidentBonds(id).size()!=1||d.bondBetween(host,id)==kInvalidObjectId)return result;}
    const size_t count=recent.size()+1,total=count+fixed.size();if(total>4)return result;
    std::vector<double> offsets;double axis=0;
    if(fixed.empty()){
        if(count==3)offsets={90,210,330};else if(count==4)offsets={90,210,330,270};else return result;
    }else if(fixed.size()==1){
        axis=fixed.front()+180;if(count==2)offsets={-60,60};else if(count==3)offsets={-60,60,0};else return result;
    }else if(fixed.size()==2&&count==2){
        axis=length(inward)>1e-8?angleOf(inward*-1):angleOf(d.atoms().alive(recent.front())->position-center->position);offsets={-45,45};
    }else return result;
    Document frame=d;for(auto id:recent)if(!frame.removeAtom(id).ok())return result;
    sprout_layout::Environment obstacles(frame,host);double best=1e300;std::vector<Vec2> chosen;
    const double unit=d.defaultBondLength();
    for(double turn:{0.,-15.,15.,-30.,30.}){
        double score=std::abs(turn)*10.;bool safe=true;std::vector<Vec2> points;
        for(double offset:offsets){double crowd=0,angle=axis+offset+turn;if(!obstacles.score(angle,fixed,&crowd)){safe=false;break;}score+=crowd;points.push_back(center->position+direction(angle)*unit);}
        if(safe&&score<best){best=score;chosen=points;}
    }
    if(chosen.empty())return result;result.arranged=true;result.positions=chosen;return result;
}
struct Addition {ObjectId atom=kInvalidObjectId;bool arranged=false;};
inline Command command(ObjectId host,const std::vector<ObjectId>& recent,std::shared_ptr<Addition> added){
    return Command("添加单键",kInvalidateStructure|kInvalidateGeometry,[host,recent,added](CommandContext& c){
        auto& d=c.document;const auto* a=d.atoms().alive(host);if(!a)return CommandResult::fail(ErrorCode::NotFound,"原子不存在");
        auto placement=arrange(d,host,recent);Vec2 point;
        if(placement.arranged)point=placement.positions.back();
        else{auto chair=chair_layout::target(d,host,a->position,true);if(chair.recognized){if(!chair.available)return CommandResult::fail(ErrorCode::InvalidGeometry,"椅式环的平伏键和直立键位置已占用");point=chair.end;}else{auto dir=sprout_layout::resolve(d,host,-30);if(!dir.ok)return CommandResult::fail(ErrorCode::InvalidArgument,dir.rejectReason);point=a->position+direction(dir.directionDeg)*d.defaultBondLength();}}
        ObjectId id,bond;auto e=d.addAtom(point,6,&id);if(e.ok())e=d.addBond(host,id,&bond);
        if(e.ok()&&placement.arranged)for(size_t i=0;i<recent.size()&&e.ok();++i)e=d.setAtomPosition(recent[i],placement.positions[i]);
        if(!e.ok())return CommandResult::fail(e.code,e.message);added->atom=id;added->arranged=placement.arranged;return CommandResult::success();
    });
}
} }
