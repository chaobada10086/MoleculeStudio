#include "engine/sprout_layout.hpp"
#include "session.hpp"
#include "next/chemistry/edit.hpp"
#include <set>
#include <deque>
namespace r62 { namespace integration {
interaction::RouterOutcome chemistryAction(Session& s,const interaction::ActionRequest& req){
    using A=interaction::HotkeyAction;using R=interaction::RouterOutcome;Error err;
    ObjectId id=req.hotspot.object.id;
    auto atom=req.hotspot.kind==interaction::HotspotKind::Atom?s.doc.atoms().alive(id):0;
    auto bond=req.hotspot.kind==interaction::HotspotKind::Bond?s.doc.bonds().alive(id):0;
    std::string tool;
    switch(req.action){
    case A::ActivateBenzeneTool:tool="chooseRingTool_Benzene";break;
    case A::ActivateCyclopentadieneTool:tool="chooseRingTool_Cyclopentadiene";break;
    // The straight (acyclic) chain is withdrawn: the one chain the shell offers
    // is the 120-degree zig-zag.
    case A::ActivateChainTool:tool="chooseAlkaneTool_Snaking_Chain";break;
    case A::ActivateBracketTool:tool="chooseBracketTool_SquareBrackets";break;
    case A::ActivateOrbitalTool:tool="chooseOrbitalTool_p_Shaded";break;
    case A::ToggleFixedLength:case A::ToggleFixedAngles:{
        if(req.action==A::ToggleFixedLength)s.fixedLen=!s.fixedLen;else s.fixedAngle=!s.fixedAngle;
        auto p=s.input.profile();p.fixedLength=s.fixedLen;p.fixedAngles=s.fixedAngle;s.input.setProfile(p);s.status="Constraints updated";return R::Handled;}
    case A::CancelGesture:s.cancel();s.activateCommand("chooseSelectionTool_Marquee");return R::Handled;
    case A::SetAtomElement:
        if(!atom||!req.binding)return R::Rejected;
        err=s.submit(chemistry::setElementCommand(id,req.binding->element),"hotspot element");break;
    case A::SetAtomLabel:
        if(!atom||!req.binding)return R::Rejected;
        err=s.submit(chemistry::setLabelTextCommand(id,req.binding->label,{},0),"hotspot label");break;
    case A::ClearAtomLabel:
        if(!atom)return R::Rejected;
        err=s.submit(chemistry::clearLabelCommand({id}),"clear atom label");break;
    case A::EditAtomLabel:
        if(!atom)return R::Rejected;
        s.editAtom=id;s.selected={req.hotspot.object};return R::Handled;
    case A::SetAtomChargeDelta:
        if(!atom||!req.binding)return R::Rejected;
        err=s.submit(chemistry::setChargeCommand({id},atom->semantics.charge+req.binding->parameter),"hotspot charge");break;
    case A::SelectHotspotAtom:case A::SelectHotspotBond:
        if(!req.hotspot.valid())return R::Rejected;
        s.selected={req.hotspot.object};return R::Handled;
    case A::SelectMolecule:{
        if(!atom&&!bond)return R::Rejected;
        std::set<ObjectId> ids;std::deque<ObjectId> todo;todo.push_back(atom?id:bond->beginAtom);
        // Explicit selection command may walk the document; hover queries do not.
        std::vector<std::vector<ObjectId> > adj(s.doc.atoms().size());
        for(const auto& b:s.doc.bonds().records())if(b.isAlive()){adj[b.beginAtom].push_back(b.endAtom);adj[b.endAtom].push_back(b.beginAtom);}
        while(!todo.empty()){auto a=todo.front();todo.pop_front();if(!ids.insert(a).second)continue;for(auto n:adj[a])todo.push_back(n);}
        s.selected.clear();for(auto a:ids)s.selected.push_back(ObjectRef(ObjectKind::Atom,a));
        for(const auto& b:s.doc.bonds().records())if(b.isAlive()&&ids.count(b.beginAtom)&&ids.count(b.endAtom))s.selected.push_back(ObjectRef(ObjectKind::Bond,b.id));
        return R::Handled;}
    case A::SetBondOrderSingle:case A::SetBondOrderDouble:case A::SetBondOrderTriple:
        if(!bond)return R::Rejected;
        err=s.submit(chemistry::setBondOrderWithRefreshCommand(id,req.action==A::SetBondOrderSingle?BondOrder::Single:req.action==A::SetBondOrderDouble?BondOrder::Double:BondOrder::Triple),"hotspot bond order");break;
    case A::SetBondDoubleLeft:case A::SetBondDoubleRight:case A::SetBondDoubleCenter:
        if(!bond)return R::Rejected;
        err=s.submit(bonds::setBondSideCommand({id},req.action==A::SetBondDoubleLeft?bonds::DoubleSide::Left:req.action==A::SetBondDoubleRight?bonds::DoubleSide::Right:bonds::DoubleSide::Centered),"double bond side");break;
    case A::SetBondDisplayDashed:case A::SetBondDisplayBold:case A::SetBondDisplayWavy:
    case A::SetBondDisplayWedge:case A::SetBondDisplayHash:case A::SetBondDisplayHashWedge:
        if(!bond)return R::Rejected;
        err=s.submit(bonds::setBondDisplayCommand({id},req.action==A::SetBondDisplayDashed?BondDisplay::Dashed:req.action==A::SetBondDisplayBold?BondDisplay::Bold:req.action==A::SetBondDisplayWavy?BondDisplay::Wavy:req.action==A::SetBondDisplayWedge?BondDisplay::Wedge:req.action==A::SetBondDisplayHash?BondDisplay::Hash:BondDisplay::WedgeHash),"hotspot bond display");break;
    case A::FuseBenzene:case A::FuseCyclopentadiene:case A::FuseRing:{
        if(!bond)return R::Rejected;
        rings_chains::RingRequest request;request.mode=rings_chains::RingAttachMode::FuseOnBond;request.anchorBond=id;request.pointer=req.pointer;
        if(req.action==A::FuseBenzene)request.preset=rings_chains::RingPresetId::Benzene;
        else if(req.action==A::FuseCyclopentadiene)request.preset=rings_chains::RingPresetId::Cyclopentadiene;
        else {int n=req.binding?req.binding->parameter:0;if(n<4||n>8)return R::Unsupported;request.preset=static_cast<rings_chains::RingPresetId>(n-3);}
        rings_chains::RingPlacement plan;err=rings_chains::resolveRingPlacement(s.doc,request,&plan);
        if(err.ok())err=s.submit(rings_chains::commands::createRingCommand(plan,0,0),"hotspot fuse ring");
        break;}
    case A::SproutBond:case A::SproutWedge:case A::SproutHashWedge:{
        if(!atom)return R::Rejected;
        auto dir=sprout_layout::resolve(s.doc,id,-30);if(!dir.ok){s.status=dir.rejectReason;return R::Rejected;}
        bonds::BondEndpoint start;start.joined=true;start.target=req.hotspot.object;start.point=atom->position;
        interaction::SnapResult end;end.endpoint=start.point+core::direction(dir.directionDeg)*s.doc.defaultBondLength();
        bonds::BondAxes axes;axes.display=req.action==A::SproutWedge?BondDisplay::Wedge:req.action==A::SproutHashWedge?BondDisplay::WedgeHash:BondDisplay::Normal;
        err=s.submit(bonds::addBondFromSnapCommand(start,end,axes,6,0),"sprout bond");break;}
    default:return R::NotHandled;
    }
    if(!tool.empty())return s.activateCommand(tool)?R::Handled:R::Unsupported;
    s.input.refreshHotspot(&s);
    if(!err.ok()){s.status=err.message;return R::Rejected;}return R::Handled;
}
} }
