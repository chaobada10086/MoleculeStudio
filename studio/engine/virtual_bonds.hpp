#pragma once
#include "next/arrows_curves/model.hpp"
#include <algorithm>

namespace r62 { namespace virtual_bonds {
using namespace core;
constexpr uint32_t color=0xF28AB8FFu;

inline bool endpoints(const Document& doc,ObjectRef ref,ObjectId& begin,ObjectId& end){
    if(ref.kind!=ObjectKind::Arrow)return false;
    arrows::ArrowParams p;bool valid=false;
    if(!arrows::readArrow(doc,ref.id,&p,&valid).ok()||!valid||!p.virtualBond||
       p.electronSource.kind!=ObjectKind::Atom||p.electronTarget.kind!=ObjectKind::Atom)return false;
    begin=p.electronSource.id;end=p.electronTarget.id;
    return begin!=end&&doc.atoms().alive(begin)&&doc.atoms().alive(end);
}
inline bool isGuide(const Document& doc,ObjectRef ref){
    ObjectId a,b;return endpoints(doc,ref,a,b);
}
inline bool contact(const Document& doc,ObjectRef ref,Vec2 query,Vec2* nearest,double* distance=nullptr){
    ObjectId a,b;if(!endpoints(doc,ref,a,b))return false;
    const auto start=doc.atoms().alive(a)->position,axis=doc.atoms().alive(b)->position-start;
    const auto norm=dot(axis,axis);if(norm<1e-12)return false;
    const auto point=start+axis*std::max(0.,std::min(1.,dot(query-start,axis)/norm));
    if(nearest)*nearest=point;if(distance)*distance=length(query-point);
    return true;
}
inline Error add(Document& doc,ObjectId a,ObjectId b,ObjectId* id){
    if(a==b||!doc.atoms().alive(a)||!doc.atoms().alive(b))
        return Error(ErrorCode::InvalidArgument,"待成键需要连接两个不同的原子。");
    if(doc.bondBetween(a,b)!=kInvalidObjectId)
        return Error(ErrorCode::DuplicateBond,"这两个原子之间已有化学键。");
    for(const auto& record:doc.arrows().records())if(record.isAlive()){
        ObjectId x,y;
        if(endpoints(doc,ObjectRef(ObjectKind::Arrow,record.id),x,y)&&((x==a&&y==b)||(x==b&&y==a)))
            return Error(ErrorCode::DuplicateBond,"这两个原子之间已有待成键。");
    }
    auto p=arrows::resolvePreset(0,doc.atoms().alive(a)->position,doc.atoms().alive(b)->position,doc.defaultLineWidth());
    p.virtualBond=true;p.head.atBegin=p.head.atEnd=false;p.line=arrows::ArrowLine::Dashed;
    p.electronSource=ObjectRef(ObjectKind::Atom,a);p.electronTarget=ObjectRef(ObjectKind::Atom,b);
    auto e=arrows::addArrow(doc,p,doc.defaultLineWidth(),id);
    if(e.ok()){
        auto style=doc.arrows().alive(*id)->style;style.colorRGBA=color;
        e=doc.setLineStyle(ObjectRef(ObjectKind::Arrow,*id),style);
        auto& bag=doc.extensions(ObjectRef(ObjectKind::Arrow,*id));
        bag.set("studio.color",std::to_string(color));
        bag.set("studio.color.override",std::to_string(color));
    }
    return e;
}
} }
