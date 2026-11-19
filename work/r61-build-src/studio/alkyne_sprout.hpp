#pragma once
// An sp carbon has exactly one linear continuation. When an atom carries a
// single incident triple bond, a bond sprouted from that atom must continue
// 180 degrees from the triple bond instead of using the 120-degree carbon
// chain direction (which produces the visible kink at an alkyne end).
inline bool spContinuationAngle(const r62::core::Document& doc,r62::core::ObjectId atom,double* angle){
    const auto* self=doc.atoms().alive(atom);
    if(!self||!angle)return false;
    int count=0;double direction=0;r62::core::BondOrder order=r62::core::BondOrder::Single;
    for(auto bondId:doc.incidentBonds(atom)){
        const auto* bond=doc.bonds().alive(bondId);
        if(!bond)continue;
        auto other=bond->beginAtom==atom?bond->endAtom:bond->beginAtom;
        if(other==atom)continue;
        const auto* neighbour=doc.atoms().alive(other);
        if(!neighbour)continue;
        if(r62::core::lengthSq(neighbour->position-self->position)<=0.0)continue;
        ++count;order=bond->order;
        direction=r62::core::angleOf(neighbour->position-self->position);
    }
    if(count!=1||order!=r62::core::BondOrder::Triple)return false;
    *angle=direction+180.0;
    return true;
}
