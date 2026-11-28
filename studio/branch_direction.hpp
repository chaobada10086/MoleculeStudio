#pragma once
#include "next/core/commands.hpp"
#include <set>
#include <vector>
#include <cmath>
#include <sstream>
#include <functional>
namespace studio { namespace branch_direction {
using namespace r62::core;
struct Plan {
    std::set<ObjectId> atoms,texts;
    ObjectId inside=kInvalidObjectId,outside=kInvalidObjectId,alkene=kInvalidObjectId;
};
inline Error fail(const char* message){return Error(ErrorCode::InvalidArgument,message);}
inline bool isAlkeneAxis(const Document& doc,const BondRecord& bond){
    if(bond.order!=BondOrder::Double||bond.semantics!=BondSemantics::Normal||
        bond.stereo!=BondStereo::None||bond.display!=BondDisplay::Normal)return false;
    for(auto id:{bond.beginAtom,bond.endAtom}){
        const auto* atom=doc.atoms().alive(id);
        if(!atom||atom->element!=6||atom->semantics.nodeType!=uint8_t(NodeType::Element))return false;
        for(auto incident:doc.incidentBonds(id))
            if(incident!=bond.id&&doc.bonds().alive(incident)->order!=BondOrder::Single)return false;
    }
    return true;
}
inline Error resolve(const Document& doc,const std::vector<ObjectRef>& refs,Plan* out){
    Plan p;std::set<ObjectId> bonds,groups;
    std::function<void(ObjectRef)> add=[&](ObjectRef r){if(r.kind==ObjectKind::Atom&&doc.atoms().alive(r.id))p.atoms.insert(r.id);else if(r.kind==ObjectKind::Bond&&doc.bonds().alive(r.id))bonds.insert(r.id);else if(r.kind==ObjectKind::Text&&doc.texts().alive(r.id))p.texts.insert(r.id);else if(r.kind==ObjectKind::Group&&groups.insert(r.id).second){auto g=doc.groups().alive(r.id);if(g)for(auto member:g->members)add(member);}};
    for(auto ref:refs)add(ref);
    if(p.atoms.empty()&&bonds.size()==1){auto bond=doc.bonds().alive(*bonds.begin());
        auto side=[&](ObjectId start){std::set<ObjectId> seen{start};std::vector<ObjectId> queue{start};for(size_t i=0;i<queue.size();++i)for(auto bid:doc.incidentBonds(queue[i]))if(bid!=bond->id){auto b=doc.bonds().alive(bid);auto next=b->beginAtom==queue[i]?b->endAtom:b->beginAtom;if(seen.insert(next).second)queue.push_back(next);}return seen;};
        auto a=side(bond->beginAtom),b=side(bond->endAtom);if(a.count(bond->endAtom))return fail("环内的键没有独立支链，请框选完整支链。");
        if(a.size()==b.size())return fail("连接键两侧大小相同，请框选需要换向的一侧。");p.atoms=a.size()<b.size()?a:b;
    }else if(p.atoms.empty())for(auto id:bonds){auto b=doc.bonds().alive(id);p.atoms.insert(b->beginAtom);p.atoms.insert(b->endAtom);}
    if(p.atoms.empty())return fail("请框选需要换向的完整支链，或选中其连接键。");
    auto boundary=[&](){std::vector<std::pair<ObjectId,ObjectId>> result;for(auto id:p.atoms)for(auto bid:doc.incidentBonds(id)){auto b=doc.bonds().alive(bid);auto other=b->beginAtom==id?b->endAtom:b->beginAtom;if(!p.atoms.count(other))result.push_back({id,other});}return result;};
    auto edges=boundary();
    // A marquee may omit the shared attachment carbon between two selected arms.
    if(edges.size()>1){auto root=edges.front().second;bool common=true;for(auto edge:edges)common&=edge.second==root;if(common){p.atoms.insert(root);edges=boundary();}}
    if(edges.size()!=1)return fail("请框选完整支链，只保留一处与主体相连；整分子可使用水平或垂直翻转。");
    p.inside=edges[0].first;p.outside=edges[0].second;
    auto attachment=doc.bonds().alive(doc.bondBetween(p.inside,p.outside));
    if(!attachment)return fail("找不到支链的连接键，请重新选择。");
    if(attachment->order!=BondOrder::Single){
        if(!isAlkeneAxis(doc,*attachment))return fail("请以连接单键或普通碳碳双键为轴，框选需要换向的一侧。");
        p.alkene=attachment->id;
    }
    std::set<ObjectId> connected{p.inside};std::vector<ObjectId> queue{p.inside};for(size_t i=0;i<queue.size();++i)for(auto bid:doc.incidentBonds(queue[i])){auto b=doc.bonds().alive(bid);auto n=b->beginAtom==queue[i]?b->endAtom:b->beginAtom;if(p.atoms.count(n)&&connected.insert(n).second)queue.push_back(n);}
    if(connected!=p.atoms)return fail("一次请选择一条相连的支链。");
    Vec2 origin=doc.atoms().alive(p.outside)->position,u=doc.atoms().alive(p.inside)->position-origin;double len=length(u);if(len<1e-8)return fail("连接键过短，请先整理连接位置。");u=u*(1./len);
    double spread=0;for(auto id:p.atoms)spread=std::max(spread,std::abs((doc.atoms().alive(id)->position.x-origin.x)*u.y-(doc.atoms().alive(id)->position.y-origin.y)*u.x));
    if(spread<1e-7)return fail("选中部分位于连接轴上，请扩大选区，包含需要换向的侧基。");
    if(out)*out=p;return Error();
}
inline Command command(const Plan& plan){return Command("换向",kInvalidateGeometry|kInvalidateAppearance|kInvalidateSemantics,[plan](CommandContext& ctx){auto& doc=ctx.document;
    auto a=doc.atoms().alive(plan.outside),b=doc.atoms().alive(plan.inside);if(!a||!b)return CommandResult::fail(ErrorCode::NotFound,"支链连接位置已改变，请重新选择。");
    const Vec2 origin=a->position;Vec2 u=b->position-origin;double len=length(u);if(len<1e-8)return CommandResult::fail(ErrorCode::InvalidGeometry,"连接键过短。");u=u*(1./len);
    auto reflect=[&](Vec2 v){return u*(2*dot(u,v))-v;};
    for(auto id:plan.atoms){auto atom=doc.atoms().alive(id);if(!atom)return CommandResult::fail(ErrorCode::NotFound,"支链原子已不存在。");if(id!=plan.inside){auto q=origin+reflect(atom->position-origin);if(length(q-atom->position)>1e-9){auto e=doc.setAtomPosition(id,q);if(!e.ok())return CommandResult::fail(e.code,e.message);}}
        auto& bag=doc.extensions(ObjectRef(ObjectKind::Atom,id));bag.erase("studio.atom.z");bag.erase("pst.attr.xyz");
        for(auto key:{"studio.label.offset","pst.attr.MoleculeStudioChargePosition","pst.attr.MoleculeStudioRadicalPosition"})if(auto value=bag.findValue(key)){std::istringstream in(*value);Vec2 v;if(in>>v.x>>v.y){v=reflect(v);std::ostringstream out;out.precision(15);out<<v.x<<' '<<v.y;bag.set(key,out.str());}}
    }
    for(const auto& bond:doc.bonds().records())if(bond.isAlive()&&plan.atoms.count(bond.beginAtom)){
        auto display=bond.display;auto stereo=bond.stereo;auto& bag=doc.extensions(ObjectRef(ObjectKind::Bond,bond.id));
        if(display==BondDisplay::Wedge)display=BondDisplay::WedgeHash;
        else if(display==BondDisplay::HollowWedge){display=BondDisplay::WedgeHash;bag.set("studio.branchFlip.hollow","1");}
        else if(display==BondDisplay::WedgeHash){if(bag.findValue("studio.branchFlip.hollow")){display=BondDisplay::HollowWedge;bag.erase("studio.branchFlip.hollow");}else display=BondDisplay::Wedge;}
        if(stereo==BondStereo::Up)stereo=BondStereo::Down;else if(stereo==BondStereo::Down)stereo=BondStereo::Up;
        auto e=doc.setBondDisplay(bond.id,display);if(e.ok())e=doc.setBondStereo(bond.id,stereo);if(!e.ok())return CommandResult::fail(e.code,e.message);
    }
    for(auto id:plan.texts)if(auto t=doc.texts().alive(id)){auto e=doc.setTextPosition(id,origin+reflect(t->position-origin));if(!e.ok())return CommandResult::fail(e.code,e.message);}
    if(plan.alkene!=kInvalidObjectId)
        doc.extensions(ObjectRef(ObjectKind::Bond,plan.alkene)).erase("studio.bond.unspecifiedEZ");
    return CommandResult::success();
});}
} }
