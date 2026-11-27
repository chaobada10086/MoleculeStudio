#include "next/arrows_curves/tools.hpp"
#include "mechanism_layout.hpp"
#include "mechanism_electrons.hpp"
#include "free_arc.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace r62 {
namespace arrows {

namespace {
// Resolve visible ink before the nearby halos, using device pixels for tolerance.
// Compare genuinely distinct objects for ambiguity, never the capture radius.
struct MechanismHit {
    core::ObjectRef ref;
    core::Vec2 point;
    double distance=0;
    uint8_t site=0;
    int priority=3; // specific electron, visible atom/bond, proximity, or empty
};
bool betterMechanismHit(const MechanismHit& a,const MechanismHit& b){
    return a.priority<b.priority||(a.priority==b.priority&&a.distance<b.distance);
}
core::ObjectRef pointerMechanismAnchor(const core::Document& doc,core::Vec2 point,const core::ViewTransform& view,
                                       double deviceRadius,uint8_t* visual=nullptr,core::Vec2* contact=nullptr){
    if(visual)*visual=0;
    if(contact)*contact=point;
    MechanismHit best,second;
    auto consider=[&](const MechanismHit& hit){
        if(!hit.ref.valid())return;
        if(betterMechanismHit(hit,best)){second=best;best=hit;}
        else if(betterMechanismHit(hit,second))second=hit;
    };
    const double atomRadius=view.deviceRadiusToDocument(deviceRadius+2.);
    const double bondRadius=view.deviceRadiusToDocument(deviceRadius+1.);
    const auto& boxes=mechanismVisualBoxes(doc);
    // The Newman circle denotes the rear carbon. A circumference hit targets
    // that atom; the dot at
    // the centre remains the front carbon, despite coincident coordinates.
    for(const auto& bond:doc.bonds().records())if(bond.isAlive()&&projection_molecule::axis(doc,bond)&&projection_molecule::kind(doc,bond.beginAtom)==2){auto center=doc.atoms().alive(bond.beginAtom)->position;double distance=core::length(point-center),radius=projection_molecule::radius(doc);
        if(std::abs(distance-radius)<=view.deviceRadiusToDocument(2.)&&distance>1e-8){if(visual)*visual=1;if(contact)*contact=center+(point-center)*(radius/distance);return core::ObjectRef(core::ObjectKind::Atom,projection_molecule::rear(doc,bond.beginAtom)?bond.beginAtom:bond.endAtom);}
    }
    for(const auto& a:doc.atoms().records())if(a.isAlive()){
        if(projection_molecule::rear(doc,a.id))continue;
        const auto ref=core::ObjectRef(core::ObjectKind::Atom,a.id);
        MechanismHit atomHit;
        auto addSite=[&](uint8_t site,core::Vec2 anchor,double distance,int priority,double radius){
            if(distance>radius)return;
            MechanismHit hit;hit.ref=ref;hit.point=anchor;hit.site=site;hit.distance=distance;hit.priority=priority;
            if(betterMechanismHit(hit,atomHit))atomHit=hit;
        };
        auto found=boxes.find(a.id);
        const double centerDistance=core::length(point-a.position);
        if(found!=boxes.end()&&!found->second.labelGlyphs.empty()){
            const auto& box=found->second;
            auto anchor=a.position;
            const auto* compact=studioions::find(doc.atomLabel(a.id));
            if(box.hasBody&&(mechanism::triphenylphosphineLabel(doc.atomLabel(a.id))||
                (compact&&compact->element==8)))
                anchor=(box.body.min+box.body.max)*.5;
            for(const auto& glyph:box.labelGlyphs){
                const auto distance=mechanismBoxDistance(glyph,point);
                addSite(1,anchor,distance,distance==0?1:2,atomRadius);
            }
            // A displayed HN is one atom label. The spaces between its
            // glyphs must remain clickable even when zoom makes them wide.
            const auto label=mechanismLabelEnvelope(box);
            const double distance=mechanismBoxDistance(label,point);
            const double halo=std::max(atomRadius,std::min(doc.defaultTextSize()*.22,label.height()*.18));
            addSite(1,anchor,distance,2,halo);
        }else{
            double vertexRadius=view.deviceRadiusToDocument(7.);
            for(auto id:doc.incidentBonds(a.id))if(auto bond=doc.bonds().alive(id)){
                const auto other=doc.atoms().alive(bond->beginAtom==a.id?bond->endAtom:bond->beginAtom);
                if(other)vertexRadius=std::min(vertexRadius,core::length(other->position-a.position)*.2);
            }
            addSite(1,a.position,centerDistance,centerDistance<=vertexRadius?0:2,atomRadius);
        }
        if(found!=boxes.end()&&found->second.hasCharge){
            const auto& charge=found->second.charge;
            const auto distance=mechanismBoxDistance(charge,point);
            addSite(2,(charge.min+charge.max)*.5,distance,distance==0?0:2,atomRadius);
        }
        if(found!=boxes.end())for(auto dot:found->second.radicalPoints){
            const auto distance=std::max(0.,core::length(point-dot)-mechanismRadicalRadius(doc));
            addSite(4,dot,distance,distance==0?0:2,atomRadius);
        }
        if(found!=boxes.end())for(const auto& pair:found->second.lonePairs){
            const auto distance=mechanismBoxDistance(pair,point);
            addSite(3,(pair.min+pair.max)*.5,distance,distance==0?0:2,atomRadius);
        }
        consider(atomHit);
    }
    for(const auto& bond:doc.bonds().records())if(bond.isAlive()){
        MechanismHit hit;
        double inkDistance;
        if(!mechanismBondContact(doc,bond.id,point,&hit.point,&inkDistance)||inkDistance>bondRadius)continue;
        hit.ref=core::ObjectRef(core::ObjectKind::Bond,bond.id);hit.site=5;
        hit.distance=inkDistance;hit.priority=inkDistance==0?1:2;
        consider(hit);
    }
    for(const auto& record:doc.arrows().records())if(record.isAlive()){
        MechanismHit hit;hit.ref=core::ObjectRef(core::ObjectKind::Arrow,record.id);
        if(!virtual_bonds::contact(doc,hit.ref,point,&hit.point,&hit.distance)||hit.distance>bondRadius)continue;
        hit.site=5;hit.priority=hit.distance<=view.deviceRadiusToDocument(1.5)?1:2;
        consider(hit);
    }
    if(!best.ref.valid()||(second.ref.valid()&&second.priority==best.priority&&
       second.distance-best.distance<=view.deviceRadiusToDocument(.25)))return core::ObjectRef();
    if(visual)*visual=best.site;
    if(contact)*contact=best.point;
    return best.ref;
}
bool mechanismInkStart(const core::Document& doc,core::ObjectRef ref,uint8_t site,core::Vec2 pointer,core::Vec2 contact){
    if(virtual_bonds::isGuide(doc,ref))return core::length(pointer-contact)<=doc.defaultLineWidth();
    if(site==4)return core::length(pointer-contact)<=mechanismRadicalRadius(doc);
    if(site==3){auto pair=mechanismClosestLonePair(doc,ref.id,pointer);return pair&&pair->contains(pointer);}
    if(site==5){double distance;return mechanismBondContact(doc,ref.id,pointer,nullptr,&distance)&&distance==0;}
    if(site==1||site==2){
        const auto& boxes=mechanismVisualBoxes(doc);
        auto found=boxes.find(ref.id);
        if(found==boxes.end())return false;
        if(site==2)return found->second.hasCharge&&found->second.charge.contains(pointer);
        for(const auto& glyph:found->second.labelGlyphs)if(glyph.contains(pointer))return true;
    }
    return false;
}
bool explicitMechanismStart(const core::Document& doc,core::ObjectRef ref,uint8_t site,core::Vec2 pointer,core::Vec2 contact,const core::ViewTransform& view){
    if(mechanismInkStart(doc,ref,site,pointer,contact))return true;
    const double margin=view.deviceRadiusToDocument(3.5);
    if(site==5)return core::length(pointer-contact)<=margin;
    if(site==4)return core::length(pointer-contact)<=mechanismRadicalRadius(doc)+margin;
    if(site==3){auto pair=mechanismClosestLonePair(doc,ref.id,pointer);return pair&&mechanismBoxDistance(*pair,pointer)<=margin;}
    if(ref.kind!=core::ObjectKind::Atom)return false;
    const auto& boxes=mechanismVisualBoxes(doc);
    auto found=boxes.find(ref.id);
    if(found==boxes.end())return false;
    if(site==2&&found->second.hasCharge)return mechanismBoxDistance(found->second.charge,pointer)<=margin;
    if(site==1&&!found->second.labelGlyphs.empty())
        return mechanismBoxDistance(mechanismLabelEnvelope(found->second),pointer)<=margin;
    return false;
}
std::string captureName(const core::Document& doc,core::ObjectRef ref,uint8_t site=0){
    if(!ref.valid())return "未捕捉";
    if(virtual_bonds::isGuide(doc,ref))return "待成键";
    if(ref.kind==core::ObjectKind::Bond)return "化学键";
    auto a=doc.atoms().alive(ref.id);if(site==2&&a)return a->semantics.charge>0?"正电荷":"负电荷";
    if(site==3)return "孤对电子";
    if(site==4)return "自由基电子";
    const auto label=doc.atomLabel(ref.id);
    if(!label.empty())return "原子 "+label;
    if(a&&mechanismHasLabel(doc,*a))return std::string("原子 ")+core::elementSymbol(a->element);
    return "原子";
}
void appendMechanismCapture(core::PreviewScene& preview,const core::Document& doc,const core::ViewTransform& view,
                            core::ObjectRef ref,uint8_t site,core::Vec2 center,bool source){
    const uint32_t color=virtual_bonds::isGuide(doc,ref)?virtual_bonds::color:ref.kind==core::ObjectKind::Bond?0xE67E00FFu:0x0078D4FFu;
    double radius=view.deviceRadiusToDocument(14.);
    if(site==1&&ref.kind==core::ObjectKind::Atom){
        const auto& boxes=mechanismVisualBoxes(doc);
        const auto found=boxes.find(ref.id);
        if(found!=boxes.end()&&found->second.hasBody)
            radius=std::max(radius,std::min(view.deviceRadiusToDocument(46.),found->second.body.height()*.35));
    }
    core::PreviewItem circle;circle.subject=ref;circle.primitive=core::PreviewPrimitive::Polyline;
    for(int i=0;i<=48;++i){
        double angle=i*6.283185307179586/48;
        circle.points.push_back(center+core::Vec2(std::cos(angle),std::sin(angle))*radius);
    }
    circle.style.width=view.deviceRadiusToDocument(7.);circle.style.colorRGBA=0xFFFFFFFFu;preview.add(circle);
    circle.style.width=view.deviceRadiusToDocument(3.5);circle.style.colorRGBA=color;preview.add(circle);
    core::PreviewItem label;label.subject=ref;label.primitive=core::PreviewPrimitive::Label;label.style.colorRGBA=color;
    label.points.push_back(center+core::Vec2(radius,-radius));
    label.text=(source?"电子来源：":"成键目标：")+captureName(doc,ref,site);
    preview.add(label);
}

// The click/drag threshold the whole project uses for "did the pointer move?".
// Mirrors interaction::SnapProfile::clickSlopDevice; expressed in DEVICE pixels and
// converted through the view, so it means the same thing at every zoom.
const double kClickSlopDevice = 4.0;

double dist(const core::Vec2& a, const core::Vec2& b) {
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

bool movedPastSlop(const core::Vec2& from, const core::Vec2& to,
                   const core::ViewTransform& view) {
    return dist(from, to) > view.deviceRadiusToDocument(kClickSlopDevice);
}

double clampd(double v, double lo, double hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// Snap a direction to the profile angle step [GUIDE p94-95 via
// SOURCES_AND_LIMITS.md: the guide states 15-degree increments].
core::Vec2 snapDirection(const core::Vec2& origin, const core::Vec2& pointer,
                         double stepDegrees, bool constrainAngle,
                         double constrainedLength, double* outLength) {
    const double dx = pointer.x - origin.x;
    const double dy = pointer.y - origin.y;
    const double raw = std::sqrt(dx * dx + dy * dy);
    if (raw < 1e-12) {
        if (outLength) *outLength = constrainedLength > 0.0 ? constrainedLength : 0.0;
        return core::Vec2(1.0, 0.0);
    }
    double angle = core::angleOf(core::Vec2(dx, dy));
    if (constrainAngle && stepDegrees > 0.0)
        angle = std::floor(angle / stepDegrees + 0.5) * stepDegrees;
    const double len = (constrainedLength > 0.0) ? constrainedLength : raw;
    if (outLength) *outLength = len;
    return core::direction(angle);
}

// Angle of an arc, in degrees, from its sagitta and chord. Used only for the
// on-screen readout the guide describes [GUIDE p109].
double arcAngleDegrees(double sagitta, double chord) {
    if (chord < 1e-9) return 0.0;
    const double ratio = std::fabs(sagitta) / chord;
    return 4.0 * std::atan(ratio * 2.0) * 180.0 / 3.14159265358979323846;
}

} // namespace   // end anonymous helpers

// forward declaration (defined below, after ToolHost::submit is reachable)
core::Error commitArrow(core::ToolHost& host, core::ObjectId target,
                        const ArrowParams& params);

// ===========================================================================
// Shared resolvers
// ===========================================================================
ArrowParams resolveArrowDrag(uint32_t presetId, const core::Vec2& press,
                             const core::Vec2& pointer, double documentLineWidth,
                             bool fixedLength, bool fixedAngles, double angleStepDegrees,
                             double defaultBondLength, uint32_t modifiers) {
    const bool release = altReleasesConstraints(modifiers);
    const bool constrainAngle = fixedAngles && !release;
    // Reaction arrow length follows the pointer, independently of fixed BOND length.
    const bool constrainLength = false;
    (void)fixedLength;

    double lenOut = 0.0;
    const core::Vec2 dir = snapDirection(press, pointer, angleStepDegrees,
                                         constrainAngle,
                                         constrainLength ? defaultBondLength : 0.0,
                                         &lenOut);
    const double len = (lenOut > 1e-9) ? lenOut : (defaultBondLength > 0.0 ? defaultBondLength : 24.0);
    return resolvePreset(presetId, press,
                         core::Vec2(press.x + dir.x * len, press.y + dir.y * len),
                         documentLineWidth);
}

core::Vec2 resolvePlacementOrigin(const core::Document& document,
                                  const core::Vec2& pointer,
                                  core::ObjectId nearestAtom) {
    if (nearestAtom != core::kInvalidObjectId) {
        const core::AtomRecord* atom = document.atoms().alive(nearestAtom);
        // The atom's EXACT stored coordinate, never the pointer. Using the pointer
        // leaves a new object 0.4pt from the object it was drawn from, which is the
        // defect SOURCES_AND_LIMITS.md records.
        if (atom) return atom->position;
    }
    return pointer;
}

// ===========================================================================
// registerArrowsCurves
// ===========================================================================
core::Error registerArrowsCurves(core::ToolRegistry* registry,
                                 uint32_t* outArrowPresets,
                                 uint32_t* outCurveStyles) {
    if (!registry) return core::Error(core::ErrorCode::InvalidArgument, "no registry");
    const size_t n = arrowPresetCount();
    for (size_t i = 0; i < n; ++i) {
        const PresetSpec& spec = arrowPresets()[i];
        core::ToolPreset p;
        p.id = spec.toolId;
        p.behavior = core::ToolBehavior::PlaceSingle;
        p.target = core::PresetTarget::Arrow;
        p.displayName = spec.displayName;
        p.category = "arrow";
        p.sourceCommand = spec.sourceCommand;
        p.optionKey = spec.toolId;
        p.invalidation = core::kInvalidateStructure | core::kInvalidateGeometry;
        // HONESTY: a preset is only reported implemented when its table row says so.
        p.implemented = spec.implemented;
        uint32_t assigned = core::kPresetNone;
        const core::Error e = registry->registerPreset(p, &assigned);
        if (!e.ok()) return e;
        if (outArrowPresets) outArrowPresets[i] = assigned;
    }
    const size_t cn = curveStyleCount();
    for (size_t i = 0; i < cn; ++i) {
        const CurveStyleSpec& spec = curveStyles()[i];
        core::ToolPreset p;
        p.id = spec.toolId;
        // A curve style is a STYLE OPTION, not a placement: it is applied to the
        // next curve drawn (or to a selection). Expressed as target=Selection rather
        // than pretending it creates an object, which is the "275 as style options"
        // distinction the task statement makes explicitly.
        p.behavior = core::ToolBehavior::DrawCurve;
        p.target = core::PresetTarget::Selection;
        p.displayName = spec.displayName;
        p.category = "free_curve";
        p.sourceCommand = spec.sourceCommand;
        p.optionKey = spec.toolId;
        p.invalidation = core::kInvalidateAppearance;
        p.implemented = spec.implemented;
        uint32_t assigned = core::kPresetNone;
        const core::Error e = registry->registerPreset(p, &assigned);
        if (!e.ok()) return e;
        if (outCurveStyles) outCurveStyles[i] = assigned;
    }
    return core::Error::success();
}

// ===========================================================================
// ArrowTool
// ===========================================================================
ArrowTool::ArrowTool(uint32_t presetId) : presetId_(presetId) {
    const PresetSpec* spec = presetById(presetId);
    id_ = spec ? std::string(spec->toolId) : std::string("r62.tool.arrow.unknown");
}

const core::ToolId& ArrowTool::id() const { return id_; }

core::Invalidation ArrowTool::activate(core::ToolHost& host) {
    state_ = ArrowGestureState();
    previewGeometry_ = ArrowGeometry();
    host.preview().clear();
    host.preview().mutableHidden().clear();
    return core::kInvalidateSelection;
}

size_t ArrowTool::pickHandle(const std::vector<Handle>& handles, const core::Vec2& pointer,
                             double deviceRadius, const core::ViewTransform& view) const {
    const double radius = view.deviceRadiusToDocument(deviceRadius);
    size_t best = handles.size();
    double bestDistance = view.deviceRadiusToDocument(std::max(deviceRadius,12.0*view.dpiScale));
    for (size_t i = 0; i < handles.size(); ++i) {
        const double d = dist(handles[i].point, pointer);
        // Strictly inside the radius, deliberately: a press exactly ON the capture
        // ring is a boundary case, and treating it as a grab makes "click the arrow
        // body to reverse it" [GUIDE p110] impossible at that exact distance. The
        // tie also picks the SMALLEST distance, so handle order never decides.
        const double capture = handles[i].kind==HandleKind::Middle ? view.deviceRadiusToDocument(std::max(deviceRadius,12.0*view.dpiScale)) : radius;
        if (d < capture && d < bestDistance) { bestDistance = d; best = i; }
    }
    return best;
}

void ArrowTool::hoverPreview(core::ToolHost& host,core::Vec2 point,uint32_t modifiers){
    if(state_.active){if(presetId_==58&&state_.target==core::kInvalidObjectId&&state_.arcStage>0){updateFreeArc(host,point,modifiers,state_.arcStage==2);publishPreview(host,host.view());}return;}
    if(presetId_==58){auto& preview=host.preview();preview.clear();preview.mutableHidden().clear();if(!altReleasesConstraints(modifiers))appendJunctionPreview(preview,arcJunction(host.document(),point,host.view().deviceRadiusToDocument(host.snapRadiusDevice())),host.view());return;}
    if(presetId_!=49&&presetId_!=57)return;
    auto& preview=host.preview();preview.clear();preview.mutableHidden().clear();
    if(altReleasesConstraints(modifiers))return;
    const auto& doc=host.document();const auto& view=host.view();uint8_t site=0;core::Vec2 contact=point;
    auto ref=pointerMechanismAnchor(doc,point,view,host.snapRadiusDevice(),&site,&contact);
    if(!ref.valid())return;
    // Match beginGesture: an exact selected handle is an edit; a deliberate
    // glyph press wins over a nearby handle's broad acquisition radius.
    for(auto selected:host.selection())if(selected.kind==core::ObjectKind::Arrow&&doc.arrows().alive(selected.id)){
        ArrowParams p;bool valid=false;if(readArrow(doc,selected.id,&p,&valid).ok()&&valid){
            auto handles=arrowHandles(p,doc.defaultLineWidth(),Tessellation());size_t hit=pickHandle(handles,point,host.snapRadiusDevice(),view);
            bool glyph=explicitMechanismStart(doc,ref,site,point,contact,view);
            if(hit<handles.size()&&((dist(handles[hit].point,point)<=view.deviceRadiusToDocument(2)&&!mechanismInkStart(doc,ref,site,point,contact))||!glyph))return;
            ArrowGeometry geometry;buildArrowGeometry(p,doc.defaultLineWidth(),Tessellation(),&geometry);
            if(!glyph&&hitArrow(geometry,point,view.deviceRadiusToDocument(5)))return;
        }break;
    }
    core::Vec2 center=contact;if(site<3)mechanismAnchorPoint(doc,ref,&center);
    if(ref.kind==core::ObjectKind::Atom)if(auto atom=doc.atoms().alive(ref.id)){
        if(site==1||site==2){auto box=site==2?mechanismChargeBox(doc,*atom):mechanismAtomBodyBox(doc,*atom);center=(box.min+box.max)*.5;}
    }
    appendMechanismCapture(preview,doc,view,ref,site,center,true);
}

core::CommandResult ArrowTool::beginGesture(core::ToolHost& host,
                                            const core::PointerState& pointer,
                                            core::HitContext& hits) {
    if(presetId_==58&&state_.active&&state_.target==core::kInvalidObjectId&&state_.arcStage>0){state_.arcStagePress=true;updateFreeArc(host,pointer.position,pointer.modifiers,state_.arcStage==2);publishPreview(host,host.view());return core::CommandResult::success(core::kInvalidateAppearance);}
    state_ = ArrowGestureState();
    previewGeometry_ = ArrowGeometry();
    state_.active = true;
    state_.pressPoint = pointer.position;
    state_.rawPressPoint = pointer.position;state_.arcEndpoint=pointer.position;

    const core::Document& doc = hits.document();
    const double lineWidth = doc.defaultLineWidth();
    const Tessellation tess;

    // 1. Is the press on an EXISTING selected arrow? If so this gesture EDITS it
    //    rather than placing a new one. [GUIDE p109] "When you mouse-over an arrow
    //    with the Lasso, Marquee, or an Arrow tool selected, the adjustment handles
    //    appear."
    const std::vector<core::ObjectRef>& selection = host.selection();
    core::ObjectId candidate = core::kInvalidObjectId;
    for (size_t i = 0; i < selection.size(); ++i) {
        if (selection[i].kind == core::ObjectKind::Arrow &&
            doc.arrows().alive(selection[i].id)) { candidate = selection[i].id; break; }
    }
    if (candidate != core::kInvalidObjectId) {
        ArrowParams params;
        bool ok = false;
        if (readArrow(doc, candidate, &params, &ok).ok()) {
            const std::vector<Handle> handles = arrowHandles(params, lineWidth, tess);
            const size_t hitIndex = pickHandle(handles, pointer.position,
                                               host.snapRadiusDevice(), host.view());
            bool explicitGlyphStart=false;
            if((presetId_==49||presetId_==57)&&!altReleasesConstraints(pointer.modifiers)){
                uint8_t visual=0;core::Vec2 contact;auto ref=pointerMechanismAnchor(doc,pointer.position,host.view(),host.snapRadiusDevice(),&visual,&contact);
                explicitGlyphStart=explicitMechanismStart(doc,ref,visual,pointer.position,contact,host.view());
                // An exact handle grab remains an edit. A broad handle hit
                // must not steal a deliberate press on a nearby charge glyph.
                if(hitIndex<handles.size()&&dist(handles[hitIndex].point,pointer.position)<=host.view().deviceRadiusToDocument(2)&&!mechanismInkStart(doc,ref,visual,pointer.position,contact))explicitGlyphStart=false;
            }
            if (hitIndex < handles.size()&&!explicitGlyphStart&&(presetId_!=58||handles[hitIndex].kind==HandleKind::Middle||dist(handles[hitIndex].point,pointer.position)<=host.view().deviceRadiusToDocument(2))) {
                state_.target = candidate;
                state_.grabbed = handles[hitIndex].kind;
                state_.handleIndex = hitIndex;
                state_.pending = params;
                if (handles[hitIndex].kind == HandleKind::Middle &&
                    shiftRequestsConnect(pointer.modifiers)) {
                    const ConnectTarget target =
                        resolveConnectTarget(doc, pointer.position,
                                             host.view().deviceRadiusToDocument(
                                                 host.snapRadiusDevice()),
                                             lineWidth, tess);
                    if (target.valid) host.setStatus("Connect: midpoint target found");
                    else host.setStatus("Connect: no target at this midpoint");
                }
                publishPreview(host, host.view());
                return core::CommandResult::success(core::kInvalidateAppearance);
            }
            // The press landed on the selected arrow but not on a handle: treat it
            // as a reversal CLICK (handled in endGesture) rather than a new arrow.
            ArrowGeometry geometry;buildArrowGeometry(params,lineWidth,tess,&geometry);
            if(presetId_!=58&&!explicitGlyphStart&&hitArrow(geometry,pointer.position,host.view().deviceRadiusToDocument(5))){
            state_.target = candidate;
            state_.grabbed = HandleKind::None;
            state_.pending = params;
            return core::CommandResult::success(core::kInvalidateAppearance);
            }
        }
    }

    // 2. Otherwise place a NEW arrow. The begin point honours the snap module: the
    //    caller has already resolved candidates, and the SAME resolved point is used
    //    by the preview and by the commit because both read state_.pending.
    const core::ObjectId nearAtom =
        hits.nearestAtom(pointer.position, host.snapRadiusDevice());
    core::Vec2 beginPoint = altReleasesConstraints(pointer.modifiers) ? pointer.position : mechanismSnap(doc, (presetId_==49||presetId_==57)?pointer.position:resolvePlacementOrigin(doc, pointer.position, nearAtom), host.view().deviceRadiusToDocument(host.snapRadiusDevice()));
    core::ObjectRef capturedSource;uint8_t capturedSourceSite=0;
    if((presetId_==49||presetId_==57)&&!altReleasesConstraints(pointer.modifiers)){
        capturedSource=pointerMechanismAnchor(doc,pointer.position,host.view(),host.snapRadiusDevice(),&capturedSourceSite,&beginPoint);
        if(virtual_bonds::isGuide(doc,capturedSource)){
            host.setStatus("待成键用于指定成键目标，请从已有键或原子开始画机理箭头。");
            return cancelGesture(host);
        }
        // A charge/lone-pair click carries atom semantics but keeps the visible
        // electron-origin position, rather than jumping back to the nucleus.
        if(capturedSource.kind!=core::ObjectKind::Atom&&capturedSourceSite!=5)mechanismAnchorPoint(doc,capturedSource,&beginPoint);
    }
    if(presetId_==58){beginPoint=pointer.position;state_.arcTrace.push_back(pointer.position);if(!altReleasesConstraints(pointer.modifiers)){auto snap=arcJunction(doc,pointer.position,host.view().deviceRadiusToDocument(host.snapRadiusDevice()));if(snap.valid()){beginPoint=snap.point;state_.joinBegin=snap.arrow;}}}
    state_.pressPoint = beginPoint;
    state_.pending = resolveArrowDrag(presetId_, beginPoint, beginPoint, lineWidth,
                                      host.fixedLength(), host.fixedAngles(),
                                      host.angleStepDegrees(), doc.defaultBondLength(),
                                      pointer.modifiers);
    state_.pending=withHeadTier(state_.pending,headTier_);state_.pending.strokeWidth=defaultStrokeWidth_;
    state_.pending.mechanism=(presetId_==49||presetId_==57);
    if(state_.pending.mechanism&&!altReleasesConstraints(pointer.modifiers))
        state_.pending.electronSource=capturedSource;
    state_.pending.sourceVisualAnchor=capturedSourceSite;
    if(state_.pending.mechanism)host.setStatus(state_.pending.electronSource.valid()?"已捕捉来源："+captureName(doc,state_.pending.electronSource,state_.pending.sourceVisualAnchor)+"；拖到目标后松开。":"来源未捕捉；请从原子、电荷或键开始拖动。");
    publishPreview(host, host.view());
    return core::CommandResult::success(core::kInvalidateAppearance);
}

void ArrowTool::updateFreeArc(core::ToolHost& host,core::Vec2 point,uint32_t modifiers,bool bend){
    const auto& doc=host.document();if(!bend)state_.arcEndpoint=point;
    auto p=withHeadTier(resolvePreset(58,state_.rawPressPoint,state_.arcEndpoint,doc.defaultLineWidth()),headTier_);p.sweep=ArcSweep::Free;p.strokeWidth=defaultStrokeWidth_;
    p.arcSagitta=bend?sagittaFromHandleDrag(p.begin,p.end,point):core::length(p.end-p.begin)*.2071067811865475;
    ArcJunction start,finish;if(!altReleasesConstraints(modifiers)){double radius=host.view().deviceRadiusToDocument(host.snapRadiusDevice());start=arcJunction(doc,state_.rawPressPoint,radius);finish=arcJunction(doc,state_.arcEndpoint,radius);}
    junctionCurve(p,start,finish);state_.joinBegin=start.arrow;state_.joinEnd=finish.arrow;
    if(arcBendLocked_){p.arcSagitta=core::length(p.end-p.begin)*arcBendRatio_;p.route.clear();}
    if(arcCurvatureLocked_){p.arcSagitta=sagittaAtCurvature(arcCurvature_,core::length(p.end-p.begin),arcCurvatureMajor_);p.route.clear();}
    state_.pending=p;
}

core::CommandResult ArrowTool::updateGesture(core::ToolHost& host,
                                             const core::PointerState& pointer,
                                             core::HitContext& hits) {
    if (!state_.active) return core::CommandResult::noChange();
    if(presetId_==58&&state_.target==core::kInvalidObjectId&&state_.arcStage>0){updateFreeArc(host,pointer.position,pointer.modifiers,state_.arcStage==2);publishPreview(host,host.view());return core::CommandResult::success(core::kInvalidateAppearance);}
    if (!state_.moved && !movedPastSlop(state_.rawPressPoint, pointer.position, host.view()))
        return core::CommandResult::noChange();
    state_.moved = true;

    const core::Document& doc = hits.document();
    const double lineWidth = doc.defaultLineWidth();
    const Tessellation tess;

    const bool directBend=state_.target!=core::kInvalidObjectId &&
        (state_.grabbed==HandleKind::Middle||state_.grabbed==HandleKind::ArcRadius);
    bool editingArc=false;
    if (state_.target != core::kInvalidObjectId) {
        // ---- editing an existing arrow ------------------------------------
        // Always recompute from the PRE-GESTURE value, so a drag is absolute and
        // not incremental. An incremental update would accumulate rounding error
        // across a long drag and the preview would drift away from the commit.
        ArrowParams p = state_.pending;
        ArrowParams base;
        bool ok = false;
        const bool haveBase = readArrow(doc, state_.target, &base, &ok).ok() && ok;
        if (haveBase) p = base;
        editingArc=haveBase&&base.family==ArrowFamily::Arc;
        if(p.virtualBond){
            if(state_.grabbed!=HandleKind::Begin&&state_.grabbed!=HandleKind::End)return core::CommandResult::noChange();
            uint8_t site=0;core::Vec2 contact;
            const auto atom=pointerMechanismAnchor(doc,pointer.position,host.view(),host.snapRadiusDevice(),&site,&contact);
            if(atom.kind!=core::ObjectKind::Atom)return core::CommandResult::noChange();
            if(state_.grabbed==HandleKind::Begin)p.electronSource=atom;else p.electronTarget=atom;
            if(p.electronSource==p.electronTarget||doc.bondBetween(p.electronSource.id,p.electronTarget.id)!=core::kInvalidObjectId)
                return core::CommandResult::noChange();
            p.begin=doc.atoms().alive(p.electronSource.id)->position;p.end=doc.atoms().alive(p.electronTarget.id)->position;
            state_.pending=p;publishPreview(host,host.view());
            return core::CommandResult::success(core::kInvalidateAppearance);
        }

        // Work from the value captured at press time when the bag could not be
        // read, so the gesture still behaves.
        if (state_.pending.begin == state_.pending.end && haveBase) p = base;

        switch (state_.grabbed) {
        case HandleKind::Begin:
            p.begin = altReleasesConstraints(pointer.modifiers)?pointer.position:mechanismSnap(doc,pointer.position,host.view().deviceRadiusToDocument(host.snapRadiusDevice()));
            if(p.mechanism){p.sourceVisualAnchor=0;p.electronSource=altReleasesConstraints(pointer.modifiers)?core::ObjectRef():pointerMechanismAnchor(doc,pointer.position,host.view(),host.snapRadiusDevice(),&p.sourceVisualAnchor,&p.begin);}
            break;
        case HandleKind::End:
            p.end = altReleasesConstraints(pointer.modifiers)?pointer.position:mechanismSnap(doc,pointer.position,host.view().deviceRadiusToDocument(host.snapRadiusDevice()));
            if(p.mechanism){p.targetVisualAnchor=0;p.electronTarget=altReleasesConstraints(pointer.modifiers)?core::ObjectRef():pointerMechanismAnchor(doc,pointer.position,host.view(),host.snapRadiusDevice(),&p.targetVisualAnchor,&p.end);if(p.targetVisualAnchor<2)mechanismAnchorPoint(doc,p.electronTarget,&p.end);}
            break;
        case HandleKind::Middle:
            // Drag the middle to create or change the arc [GUIDE p109]. The inverse
            // of the arc-midpoint mapping is ONE function, so preview and commit
            // read the same sagitta.
            p.route.clear();
            // Preserve the grab offset within the capture halo: no jump on acquisition.
            p.arcSagitta = sagittaFromHandleDrag(p.begin, p.end,
                p.mechanism?pointer.position:arcMidpoint(p.begin,p.end,p.arcSagitta)+pointer.position-state_.rawPressPoint);
            if (p.family == ArrowFamily::Straight && std::fabs(p.arcSagitta) > 1e-6) {
                p.family = ArrowFamily::Arc;
                // The user has created an arc of ANY length, which no palette sweep
                // names, so the sweep is recorded as Free.
                p.sweep = ArcSweep::Free;
            }
            if (p.family == ArrowFamily::Arc && std::fabs(p.arcSagitta) < 1e-6) {
                p.family = ArrowFamily::Straight;
                p.sweep = ArcSweep::Straight;
            }
            break;
        case HandleKind::ArcRadius:
            p.route.clear();
            // Preserve the grab offset within the capture halo: no jump on acquisition.
            p.arcSagitta = sagittaFromHandleDrag(p.begin, p.end,
                p.mechanism?pointer.position:arcMidpoint(p.begin,p.end,p.arcSagitta)+pointer.position-state_.rawPressPoint);
            break;
        case HandleKind::HeadSize: {
            const core::Vec2 d(p.end.x - p.begin.x, p.end.y - p.begin.y);
            const double chord = std::sqrt(d.x * d.x + d.y * d.y);
            if (chord > 1e-9) {
                const double angle=std::atan2(d.y,d.x)+(p.head.atEnd?-1:1)*2*std::atan(2*p.arcSagitta/chord);
                core::Vec2 u(std::cos(angle),std::sin(angle));
                if(p.route.size()>1){auto pts=routedPoints(p);u=p.head.atEnd?pts.back()-pts[pts.size()-2]:pts[1]-pts[0];double length=core::length(u);if(length>1e-9)u=u*(1/length);}
                const core::Vec2 n(-u.y, u.x);
                const core::Vec2 tip = p.head.atEnd ? p.end : p.begin;
                const core::Vec2 rel(pointer.position.x - tip.x, pointer.position.y - tip.y);
                const double w = std::fabs(rel.x * n.x + rel.y * n.y);
                const double ratio = (state_.pending.head.width > 1e-9)
                    ? state_.pending.head.length / state_.pending.head.width : 1.3;
                p.head.width  = clampd((w-1.5)/.9, 0.4, chord * 0.6);
                p.head.length = clampd(p.head.width * ratio, 0.4, chord * 0.9);
            }
            break;
        }
        case HandleKind::SplitA: {
            if(p.shafts==ShaftCount::Two){auto d=p.end-p.begin;double dd=d.x*d.x+d.y*d.y;if(dd>1e-9){auto rel=pointer.position-p.begin;double fraction=clampd(1-(rel.x*d.x+rel.y*d.y)/dd,.15/.85,1.);p.splitRatio=fraction/(1+fraction);}break;}
            // [GUIDE p112] "To change the length of one arrow, hold down the Alt key.
            // Two new adjustment handles appear. When adjusting one arrow of an
            // equilibrium arrow, the arrow cannot be extended beyond its original
            // length."
            const core::Vec2 d(p.end.x - p.begin.x, p.end.y - p.begin.y);
            const double chord = std::sqrt(d.x * d.x + d.y * d.y);
            if (chord > 1e-9) {
                const core::Vec2 u(d.x / chord, d.y / chord);
                const core::Vec2 rel(pointer.position.x - p.begin.x,
                                     pointer.position.y - p.begin.y);
                const double t = clampd((rel.x * u.x + rel.y * u.y) / chord, 0.15, 0.85);
                // The half may not grow past its ORIGINAL length.
                p.splitRatio = clampd(t, 0.15, std::max(0.15, p.originalSplitRatio));
            }
            break;
        }
        case HandleKind::SplitB: {
            if(p.shafts==ShaftCount::Two){auto d=p.end-p.begin;double dd=d.x*d.x+d.y*d.y;if(dd>1e-9){auto rel=pointer.position-p.begin;double fraction=clampd((rel.x*d.x+rel.y*d.y)/dd,.15/.85,1.);p.splitRatio=1/(1+fraction);}break;}
            const core::Vec2 d(p.end.x - p.begin.x, p.end.y - p.begin.y);
            const double chord = std::sqrt(d.x * d.x + d.y * d.y);
            if (chord > 1e-9) {
                const core::Vec2 u(d.x / chord, d.y / chord);
                const core::Vec2 rel(pointer.position.x - p.end.x,
                                     pointer.position.y - p.end.y);
                const double t = clampd(-(rel.x * u.x + rel.y * u.y) / chord, 0.15, 0.85);
                p.splitRatio = clampd(1.0 - t, 0.15, 0.85);
            }
            break;
        }
        case HandleKind::Control:
        case HandleKind::None:
        default:
            return core::CommandResult::noChange();
        }

        // Angle/length constraint while dragging an ENDPOINT, with Alt releasing it.
        // Applied to the free endpoint only, never to a handle drag.
        if (state_.grabbed == HandleKind::End) {
            const bool release = altReleasesConstraints(pointer.modifiers);
            if (host.fixedAngles() && !release && p.end == pointer.position && p.family != ArrowFamily::Arc) {
                double lenOut = 0.0;
                const core::Vec2 dir = snapDirection(p.begin, pointer.position,
                                                     host.angleStepDegrees(),
                                                     host.fixedAngles(),
                                                     0.0,
                                                     &lenOut);
                p.end = core::Vec2(p.begin.x + dir.x * lenOut, p.begin.y + dir.y * lenOut);
            }
        }

        if(p.mechanism&&(state_.grabbed==HandleKind::Begin||state_.grabbed==HandleKind::End)&&!altReleasesConstraints(pointer.modifiers)){
            // Re-capturing an existing endpoint uses the same glyph clearance as
            // a new gesture. The old arrow must not obstruct its own new route.
            p.route.clear();routeMechanism(doc,p,true,state_.target);clearMechanismLabels(doc,p);
        }
        if(p.mechanism&&state_.grabbed==HandleKind::Middle)orientAutomaticFishhook(p);
        if(!isMechanismArrow(p)&&(state_.grabbed==HandleKind::Begin||state_.grabbed==HandleKind::End)&&p.family==ArrowFamily::Arc){
            state_.joinBegin=state_.joinEnd=core::kInvalidObjectId;
            if(!altReleasesConstraints(pointer.modifiers)){auto snap=arcJunction(doc,pointer.position,host.view().deviceRadiusToDocument(host.snapRadiusDevice()),state_.target);if(snap.valid()){if(state_.grabbed==HandleKind::Begin){junctionCurve(p,snap,ArcJunction());state_.joinBegin=snap.arrow;}else{junctionCurve(p,ArcJunction(),snap);state_.joinEnd=snap.arrow;}}}
        }
        state_.pending = p;
    } else if(presetId_==58){
        updateFreeArc(host,pointer.position,pointer.modifiers,false);
    } else {
        auto capturedSource=state_.pending.electronSource;auto capturedSourceSite=state_.pending.sourceVisualAnchor;
        // ---- dragging out a NEW arrow -------------------------------------
        state_.pending = resolveArrowDrag(presetId_, state_.pressPoint, pointer.position,
                                          lineWidth, host.fixedLength(), host.fixedAngles(),
                                          host.angleStepDegrees(), doc.defaultBondLength(),
                                          pointer.modifiers);
        state_.pending=withHeadTier(state_.pending,headTier_);state_.pending.strokeWidth=defaultStrokeWidth_;
        state_.pending.mechanism=(presetId_==49||presetId_==57);
        if(state_.pending.mechanism){
            if(state_.mechanismTrace.empty()||dist(state_.mechanismTrace.back(),pointer.position)>host.view().deviceRadiusToDocument(1))
                state_.mechanismTrace.push_back(pointer.position);
            const auto chord=pointer.position-state_.rawPressPoint;
            const double span=core::length(chord);double bow=0;
            if(span>1e-8)for(auto sample:state_.mechanismTrace){auto q=sample-state_.rawPressPoint;
                double t=core::dot(q,chord)/(span*span);
                if(t<.08||t>.92)continue;
                double offset=(chord.x*q.y-chord.y*q.x)/span;
                if(std::fabs(offset)>std::fabs(bow))bow=offset;
            }
            if(std::fabs(bow)>host.view().deviceRadiusToDocument(3)){
                state_.pending.mechanismSide=bow>0?1:-1;
                state_.pending.arcSagitta=std::copysign(std::fabs(state_.pending.arcSagitta),bow);
                auto tangent=[&](core::Vec2 direction){double size=core::length(direction);if(size<1e-8)return core::Vec2();return core::Vec2(core::dot(direction,chord)/(size*span),(chord.x*direction.y-chord.y*direction.x)/(size*span));};
                const double minimum=std::max(host.view().deviceRadiusToDocument(6),span*.12);
                for(auto sample:state_.mechanismTrace)if(dist(sample,state_.rawPressPoint)>=minimum){state_.pending.mechanismDeparture=tangent(sample-state_.rawPressPoint);break;}
                for(auto it=state_.mechanismTrace.rbegin();it!=state_.mechanismTrace.rend();++it)if(dist(*it,pointer.position)>=minimum){state_.pending.mechanismArrival=tangent(pointer.position-*it);break;}
            }
        }
        if(!altReleasesConstraints(pointer.modifiers)) {
            core::Vec2 endpoint=mechanismSnap(doc,pointer.position,host.view().deviceRadiusToDocument(host.snapRadiusDevice()));
            if(endpoint!=pointer.position || state_.pending.family==ArrowFamily::Arc)state_.pending.end=endpoint;
            if(state_.pending.mechanism && state_.pending.family==ArrowFamily::Arc)
            {
                state_.pending.head.shape=ArrowHeadShape::Solid;
                state_.pending.head.length=3.2;state_.pending.head.width=2.24;
                if(state_.pending.mechanism){state_.pending.electronSource=capturedSource;state_.pending.sourceVisualAnchor=capturedSourceSite;state_.pending.electronTarget=pointerMechanismAnchor(doc,pointer.position,host.view(),host.snapRadiusDevice(),&state_.pending.targetVisualAnchor,&state_.pending.end);if(state_.pending.targetVisualAnchor<2)mechanismAnchorPoint(doc,state_.pending.electronTarget,&state_.pending.end);}
                routeMechanism(doc,state_.pending);
                clearMechanismLabels(doc,state_.pending);
            }
        }
    }

    if(arcBendLocked_&&!directBend&&!isMechanismArrow(state_.pending)&&(state_.pending.family==ArrowFamily::Arc||editingArc)){state_.pending.arcSagitta=core::length(state_.pending.end-state_.pending.begin)*arcBendRatio_;state_.pending.family=std::abs(state_.pending.arcSagitta)>1e-8?ArrowFamily::Arc:ArrowFamily::Straight;state_.pending.sweep=state_.pending.family==ArrowFamily::Arc?ArcSweep::Free:ArcSweep::Straight;state_.pending.route.clear();}
    if(arcCurvatureLocked_&&!directBend&&!isMechanismArrow(state_.pending)&&state_.pending.family==ArrowFamily::Arc){state_.pending.arcSagitta=sagittaAtCurvature(arcCurvature_,core::length(state_.pending.end-state_.pending.begin),arcCurvatureMajor_);state_.pending.route.clear();}
    publishPreview(host, host.view());
    (void)tess;
    if(state_.pending.mechanism)host.setStatus(std::string("来源：")+captureName(doc,state_.pending.electronSource,state_.pending.sourceVisualAnchor)+" → 目标："+captureName(doc,state_.pending.electronTarget,state_.pending.targetVisualAnchor));
    return core::CommandResult::success(core::kInvalidateAppearance);
}

core::CommandResult ArrowTool::endGesture(core::ToolHost& host,
                                          const core::PointerState& pointer,
                                          core::HitContext& hits) {
    if (!state_.active) return core::CommandResult::noChange();
    if(presetId_==58&&state_.target==core::kInvalidObjectId){
        bool finish=state_.arcStage==2&&state_.arcStagePress;
        if(!finish){
            if(state_.arcStage==0&&!movedPastSlop(state_.rawPressPoint,pointer.position,host.view())){state_.arcStage=1;state_.arcStagePress=false;host.setStatus("选择弧线终点");return core::CommandResult::success(core::kInvalidateAppearance);}
            updateFreeArc(host,pointer.position,pointer.modifiers,false);
            if(core::length(state_.pending.end-state_.pending.begin)<host.view().deviceRadiusToDocument(4))return core::CommandResult::noChange();
            state_.arcStage=2;state_.arcStagePress=false;state_.moved=true;
            if(!arcBendLocked_&&!arcCurvatureLocked_){host.setStatus("起点和终点已确定；移动鼠标选择弧度，单击完成，Esc 取消");publishPreview(host,host.view());return core::CommandResult::success(core::kInvalidateAppearance);}
        }else updateFreeArc(host,pointer.position,pointer.modifiers,true);
        auto e=commit(host);state_=ArrowGestureState();previewGeometry_=ArrowGeometry();host.preview().clear();host.preview().mutableHidden().clear();return e.ok()?core::CommandResult::success(core::kInvalidateStructure|core::kInvalidateGeometry):core::CommandResult::fail(e.code,e.message);
    }
    // Mouse-up carries the authoritative final pointer, including a drag whose
    // last move was coalesced by Windows.
    if(state_.moved||movedPastSlop(state_.rawPressPoint,pointer.position,host.view()))updateGesture(host,pointer,hits);
    if((state_.pending.mechanism||presetId_==58)&&!state_.moved&&state_.target==core::kInvalidObjectId)
        return cancelGesture(host);

    // A press with no movement is a CLICK. A click on an existing arrow with an
    // arrow tool selected REVERSES it [GUIDE p110]: "Click an unmodified arrow, or
    // one that has had only its length changed, to reverse its direction."
    if (!state_.moved && state_.target != core::kInvalidObjectId &&
        state_.grabbed == HandleKind::None) {
        ArrowParams p;
        bool ok = false;
        if (readArrow(hits.document(), state_.target, &p, &ok).ok() && canReverse(p)) {
            const ArrowParams flipped = reversedArrow(p);
            // ONE transaction for the whole reversal, even though it changes the
            // two endpoints, the head side and the split ratio at once.
            const core::Error e = commitArrow(host, state_.target, flipped);
            state_ = ArrowGestureState();
            previewGeometry_ = ArrowGeometry();
            host.preview().clear();
            host.preview().mutableHidden().clear();
            if (!e.ok()) return core::CommandResult::fail(e.code, e.message);
            return core::CommandResult::success(core::kInvalidateAppearance |
                                                core::kInvalidateGeometry);
        }
    }

    const core::Error e = commit(host);
    state_ = ArrowGestureState();
    previewGeometry_ = ArrowGeometry();
    host.preview().clear();
    host.preview().mutableHidden().clear();
    (void)pointer;
    if (!e.ok()) return core::CommandResult::fail(e.code, e.message);
    return core::CommandResult::success(core::kInvalidateStructure | core::kInvalidateGeometry);
}

core::CommandResult ArrowTool::cancelGesture(core::ToolHost& host) {
    // Nothing was ever written to the document, so cancelling is purely local.
    // That is the whole point of the preview design: there is no half-applied state
    // to undo, and therefore no way for a cancelled drag to leave a stray object.
    state_ = ArrowGestureState();
    previewGeometry_ = ArrowGeometry();
    host.preview().clear();
    host.preview().mutableHidden().clear();
    return core::CommandResult::success(core::kInvalidateAppearance);
}

core::Error ArrowTool::commit(core::ToolHost& host) {
    // ONE gesture == ONE transaction == ONE undo step (T01 R1). A drag that changed
    // the arc, the endpoints and the head is still one entry.
    if(state_.joinBegin!=core::kInvalidObjectId||state_.joinEnd!=core::kInvalidObjectId){
        const auto p=state_.pending;const auto target=state_.target;const double width=host.document().defaultLineWidth();const std::vector<core::ObjectId> joins={state_.joinBegin,state_.joinEnd};
        core::Command cmd("arrows.join",core::kInvalidateStructure|core::kInvalidateGeometry|core::kInvalidateAppearance,[p,target,width,joins](core::CommandContext& c){core::ObjectId id=target;auto e=id==core::kInvalidObjectId?addArrow(c.document,p,width,&id):writeArrow(c.document,id,p,width);if(e.ok())e=mergeArrowGroup(c.document,id,joins);return e.ok()?core::CommandResult::success():core::CommandResult::fail(e.code,e.message);});return host.submit(cmd,"arrows.join");
    }
    if (state_.target != core::kInvalidObjectId)
        return commitArrow(host, state_.target, state_.pending);
    return commitArrow(host, core::kInvalidObjectId, state_.pending);
}

void ArrowTool::publishPreview(core::ToolHost& host, const core::ViewTransform& view) {
    core::PreviewScene& preview = host.preview();
    preview.clear();
    preview.mutableHidden().clear();

    const core::Document& doc = host.document();
    if(!state_.pending.mechanism){ArcJunction start,finish;start.arrow=state_.joinBegin;start.point=state_.pending.begin;finish.arrow=state_.joinEnd;finish.point=state_.pending.end;appendJunctionPreview(preview,start,view);appendJunctionPreview(preview,finish,view);}
    if(state_.pending.mechanism){
        auto mark=[&](core::ObjectRef ref,bool source){
            core::Vec2 point;if(!ref.valid()||!mechanismAnchorPoint(doc,ref,&point))return;
            if(source)point=state_.target==core::kInvalidObjectId?state_.pressPoint:state_.pending.begin;else if(state_.pending.targetVisualAnchor>=2)point=state_.pending.end;
            const uint8_t visual=source?state_.pending.sourceVisualAnchor:state_.pending.targetVisualAnchor;
            if((visual==1||visual==2)&&ref.kind==core::ObjectKind::Atom){auto atom=doc.atoms().alive(ref.id);if(atom){auto box=visual==2?mechanismChargeBox(doc,*atom):mechanismAtomBodyBox(doc,*atom);point=(box.min+box.max)*.5;}}
            if(visual==3&&ref.kind==core::ObjectKind::Atom)
                if(auto pair=mechanismClosestLonePair(doc,ref.id,point))point=(pair->min+pair->max)*.5;
            if(visual==4&&ref.kind==core::ObjectKind::Atom){double best=1e100;auto query=point;for(auto dot:mechanismRadicalPoints(doc,ref.id)){double d=core::length(dot-query);if(d<best){best=d;point=dot;}}}
            if(visual==5&&ref.kind==core::ObjectKind::Bond){core::Vec2 contact;if(mechanismBondContact(doc,ref.id,point,&contact))point=contact;}
            appendMechanismCapture(preview,doc,view,ref,visual,point,source);
        };
        mark(state_.pending.electronSource,true);mark(state_.pending.electronTarget,false);
    }
    const Tessellation tess;
    ArrowGeometry g;
    if (!buildArrowGeometry(state_.pending, doc.defaultLineWidth(), tess, &g).ok()) {
        previewGeometry_ = ArrowGeometry();
        return;
    }
    previewGeometry_ = g;

    // The preview is built from the SAME ArrowGeometry the commit will write, so
    // "preview == commit" is structural rather than a discipline.
    if (state_.target != core::kInvalidObjectId)
        preview.mutableHidden().push_back(
            core::ObjectRef(core::ObjectKind::Arrow, state_.target));

    for (size_t s = 0; s < g.shafts.size(); ++s) {
        core::PreviewItem item;
        item.primitive = core::PreviewPrimitive::Polyline;
        item.style.width = g.strokeWidth;
        const auto& path=g.shafts[s];
        if(g.dashOn<=0||g.dashOff<=0){item.points=path;preview.add(item);continue;}
        // Keep the geometry's exact dash lengths and continuous phase through
        // curved segments. Heads, handles and markers remain solid.
        bool ink=true;double remaining=g.dashOn;
        if(!path.empty())item.points.push_back(path.front());
        for(size_t i=1;i<path.size();++i){auto from=path[i-1],delta=path[i]-from;double span=core::length(delta),used=0;
            if(span<1e-12)continue;
            while(used<span-1e-10){double step=std::min(remaining,span-used);used+=step;remaining-=step;auto point=from+delta*(used/span);if(ink)item.points.push_back(point);
                if(remaining<1e-10){if(ink&&item.points.size()>1)preview.add(item);item.points.clear();ink=!ink;remaining=ink?g.dashOn:g.dashOff;if(ink)item.points.push_back(point);}
            }
        }
        if(ink&&item.points.size()>1)preview.add(item);
    }
    for (size_t i = 0; i < g.heads.size(); ++i) {
        const HeadGeometry& h = g.heads[i];
        if (!h.outline.empty()) {
            core::PreviewItem item;
            item.primitive = core::PreviewPrimitive::Polygon;
            item.points = h.outline;
            item.style.width = g.strokeWidth;
            item.fillEnabled = h.filled ? 1 : 0;
            item.fillRGBA = h.filled ? 0x000000FFu : 0u;
            preview.add(item);
        }
        for (size_t k = 0; k + 1 < h.strokes.size(); k += 2) {
            core::PreviewItem item;
            item.primitive = core::PreviewPrimitive::Segment;
            item.points.push_back(h.strokes[k]);
            item.points.push_back(h.strokes[k + 1]);
            item.style.width = g.strokeWidth;
            preview.add(item);
        }
    }
    for (size_t i = 0; i < g.markers.size(); ++i) {
        core::PreviewItem item;
        item.primitive = core::PreviewPrimitive::Polyline;
        item.points = g.markers[i];
        item.style.width = g.strokeWidth * 1.4;
        preview.add(item);
    }

    // Handles, so the affordance the user grabbed stays visible during the drag.
    const std::vector<Handle> handles =
        arrowHandles(state_.pending, doc.defaultLineWidth(), tess);
    for (size_t i = 0; i < handles.size(); ++i) {
        core::PreviewItem item;
        item.primitive = core::PreviewPrimitive::Rect;
        const double s = view.deviceRadiusToDocument(3.0);
        item.points.push_back(core::Vec2(handles[i].point.x - s, handles[i].point.y - s));
        item.points.push_back(core::Vec2(handles[i].point.x + s, handles[i].point.y + s));
        preview.add(item);
    }

    // The arc angle readout [GUIDE p109]: "As you drag the selection point, the
    // angle of the arc appears, measured in degrees."
    if (!state_.pending.mechanism && std::fabs(state_.pending.arcSagitta) > 1e-6) {
        core::PreviewItem label;
        label.primitive = core::PreviewPrimitive::Label;
        label.points.push_back(state_.pending.begin);
        const double chord = core::length(state_.pending.end - state_.pending.begin);
        char buf[96];
        double sag=std::abs(state_.pending.arcSagitta);double radius=(chord*chord*.25+sag*sag)/(2*sag);
        std::snprintf(buf,sizeof(buf),"R %.1f pt / %.1f deg%s",radius,arcAngleDegrees(state_.pending.arcSagitta,chord),(state_.joinBegin!=core::kInvalidObjectId||state_.joinEnd!=core::kInvalidObjectId)?" / join":"");
        label.text = buf;
        preview.add(label);
    }
}

// ===========================================================================
// commitArrow: the single write path for an arrow
// ===========================================================================
// Every arrow edit goes through ToolHost::submit, which is the ONE transaction
// entry point (T01 R1). The lambda captures the resolved ArrowParams BY VALUE, so
// the command object is copyable and can be replayed by core::CommandLog::repeat.
// It must not capture a pointer into the document; the tool registry header says
// exactly that, and this is where it would be easy to get wrong.
core::Error commitArrow(core::ToolHost& host, core::ObjectId target,
                        const ArrowParams& params) {
    const double lineWidth = host.document().defaultLineWidth();
    ArrowParams payload = params;   // by value: the command must be copyable
    const core::ObjectId id = target;

    if(id==core::kInvalidObjectId&&payload.mechanism&&!payload.virtualBond&&payload.head.atEnd&&!payload.head.atBegin&&payload.head.sides==HeadSides::Both&&payload.shafts==ShaftCount::One&&payload.electronSource.valid()&&payload.electronTarget.valid()){
        for(const auto& record:host.document().arrows().records())if(record.isAlive()){
            ArrowParams first;bool valid=false;readArrow(host.document(),record.id,&first,&valid);
            if(!valid||!first.mechanism||first.virtualBond||first.shafts!=ShaftCount::One||first.head.sides!=HeadSides::Both||!first.head.atEnd||first.head.atBegin)continue;
            if(first.electronSource!=payload.electronTarget||first.electronTarget!=payload.electronSource)continue;
            auto carbenePair=[&](core::ObjectRef atomRef,core::ObjectRef bondRef){if(atomRef.kind!=core::ObjectKind::Atom||bondRef.kind!=core::ObjectKind::Bond)return false;auto atom=host.document().atoms().alive(atomRef.id);auto bond=host.document().bonds().alive(bondRef.id);return atom&&bond&&atom->element==6&&atom->semantics.radical==uint8_t(core::Radical::Singlet)&&bond->order==core::BondOrder::Double;};
            if(carbenePair(payload.electronSource,payload.electronTarget)||carbenePair(payload.electronTarget,payload.electronSource)){
                // These are two transfers around a cycloaddition, not one
                // reversible transfer. Keep both objects and opposite bows.
                payload.route.clear();payload.arcSagitta=first.arcSagitta;
                if(std::abs(payload.arcSagitta)<1.)payload.arcSagitta=host.document().defaultBondLength()*.4;
                continue;
            }
            first.head.atBegin=true;const auto originalId=record.id;
            core::Command merge("arrows.mergeOpposite",core::kInvalidateAppearance|core::kInvalidateGeometry,
                [first,originalId,lineWidth](core::CommandContext& ctx){auto e=writeArrow(ctx.document,originalId,first,lineWidth);return e.ok()?core::CommandResult::success():core::CommandResult::fail(e.code,e.message);});
            return host.submit(merge,"arrows.mergeOpposite");
        }
    }
    if (id == core::kInvalidObjectId) {
        core::ObjectId created = core::kInvalidObjectId;
        core::Command cmd("arrows.add",
                          core::kInvalidateStructure | core::kInvalidateGeometry,
                          [payload, lineWidth, &created](core::CommandContext& ctx) {
                              const core::Error e = addArrow(ctx.document, payload,
                                                             lineWidth, &created);
                              if (!e.ok()) return core::CommandResult::fail(e.code, e.message);
                              return core::CommandResult::success();
                          });
        return host.submit(cmd, "arrows.add");
    }

    core::Command cmd("arrows.edit",
                      core::kInvalidateAppearance | core::kInvalidateGeometry,
                      [payload, lineWidth, id](core::CommandContext& ctx) {
                          const core::Error e = writeArrow(ctx.document, id, payload, lineWidth);
                          if (!e.ok()) return core::CommandResult::fail(e.code, e.message);
                          return core::CommandResult::success();
                      });
    return host.submit(cmd, "arrows.edit");
}

// ===========================================================================
// DrawCurveTool
// ===========================================================================
// Interleave quadratic controls and midpoint joins. Both tangent vectors at
// each join follow the same chord, including the seam of a closed stroke.
static void smoothPenStroke(const std::vector<core::Vec2>& samples,bool closed,
                            std::vector<core::Vec2>& pts,std::vector<uint8_t>& kinds){
    pts.clear();kinds.clear();
    if(samples.size()<3){pts=samples;kinds.assign(pts.size(),0);return;}
    size_t n=samples.size();
    if(closed && core::length(samples.back()-samples.front())<1e-6)--n;
    auto put=[&](core::Vec2 p,uint8_t kind){pts.push_back(p);kinds.push_back(kind);};
    put(closed?(samples[n-1]+samples[0])*.5:samples.front(),0);
    for(size_t i=closed?0:1;i<(closed?n:n-1);++i){
        put(samples[i],1);
        put(!closed && i==n-2?samples.back():(samples[i]+samples[(i+1)%n])*.5,0);
    }
}

DrawCurveTool::DrawCurveTool()
    : previewClosed_(false), active_(false) {
    const CurveStyleSpec* plain = curveStyleByCommand("curveStyle_Plain");
    if (plain) style_ = *plain;
    else { style_.sourceCommand = "curveStyle_Plain"; style_.implemented = true; }
}

const core::ToolId& DrawCurveTool::id() const {
    // The one T10 tool id that comes from tools.csv rather than the table, because
    // it is a Pen tool rather than a preset.
    static const core::ToolId kId("r62.tool.pen.drawCurve");
    return kId;
}

core::Invalidation DrawCurveTool::activate(core::ToolHost& host) {
    raw_.clear();
    simplified_.clear();
    previewClosed_ = false;
    active_ = false;
    previewGeometry_ = CurveGeometry();
    host.preview().clear();
    host.preview().mutableHidden().clear();
    host.setStatus(std::string("Pen: draw curve (style ") +
                   (style_.displayName ? style_.displayName : "Plain") + ")");
    return core::kInvalidateSelection;
}

core::CommandResult DrawCurveTool::beginGesture(core::ToolHost& host,
                                                const core::PointerState& pointer,
                                                core::HitContext& hits) {
    raw_.clear();
    simplified_.clear();
    previewClosed_ = false;
    active_ = true;
    previewGeometry_ = CurveGeometry();
    raw_.push_back(pointer.position);
    publishPreview(host, host.view());
    (void)hits;
    return core::CommandResult::success(core::kInvalidateAppearance);
}

core::CommandResult DrawCurveTool::updateGesture(core::ToolHost& host,
                                                 const core::PointerState& pointer,
                                                 core::HitContext& hits) {
    if (!active_) return core::CommandResult::noChange();
    // NO DOCUMENT WORK HERE. A freehand stroke can deliver hundreds of move
    // messages; appending to a local vector is the whole cost during the drag.
    sample(pointer.position);
    publishPreview(host, host.view());
    (void)hits;
    return core::CommandResult::success(core::kInvalidateAppearance);
}

core::CommandResult DrawCurveTool::endGesture(core::ToolHost& host,
                                              const core::PointerState& pointer,
                                              core::HitContext& hits) {
    if (!active_) return core::CommandResult::noChange();
    raw_.push_back(pointer.position);

    // Decimation happens HERE, once, on release - not per mouse message. The result
    // is the control points a user would have placed by hand, so an editing gesture
    // later has something to grab.
    bool closed = false;
    const core::Error e = simplifyFreehand(raw_, options_, &simplified_, &closed);
    if (!e.ok()) {
        const core::CommandResult r = core::CommandResult::fail(e.code, e.message);
        active_ = false;
        host.preview().clear();
        return r;
    }
    previewClosed_ = closed;

    // ONE transaction for the whole stroke (T01 R1).
    const CurveStyleSpec style = style_;
    std::vector<core::Vec2> pts; std::vector<uint8_t> kinds;
    smoothPenStroke(simplified_,closed,pts,kinds);
    const bool isClosed = closed;
    const double lineWidth = host.document().defaultLineWidth();
    core::ObjectId created = core::kInvalidObjectId;
    core::Command cmd("curves.draw",
                      core::kInvalidateStructure | core::kInvalidateGeometry,
                      [pts, kinds, isClosed, style, lineWidth, &created](core::CommandContext& ctx) {
                          const core::Error err = addCurve(ctx.document, pts, kinds, isClosed,
                                                           style, lineWidth, &created);
                          if (!err.ok()) return core::CommandResult::fail(err.code, err.message);
                          return core::CommandResult::success();
                      });
    const core::Error submitError = host.submit(cmd, "curves.draw");
    active_ = false;
    host.preview().clear();
    host.preview().mutableHidden().clear();
    (void)hits;
    if (!submitError.ok())
        return core::CommandResult::fail(submitError.code, submitError.message);
    return core::CommandResult::success(core::kInvalidateStructure | core::kInvalidateGeometry);
}

core::CommandResult DrawCurveTool::cancelGesture(core::ToolHost& host) {
    raw_.clear();
    simplified_.clear();
    previewClosed_ = false;
    active_ = false;
    previewGeometry_ = CurveGeometry();
    host.preview().clear();
    host.preview().mutableHidden().clear();
    return core::CommandResult::success(core::kInvalidateAppearance);
}

core::Error DrawCurveTool::commit(core::ToolHost& host) {
    if (raw_.size() < 2)
        return core::Error(core::ErrorCode::InvalidGeometry, "the stroke has too few samples");
    bool closed = false;
    std::vector<core::Vec2> pts;
    core::Error e = simplifyFreehand(raw_, options_, &pts, &closed);
    if (!e.ok()) return e;
    const CurveStyleSpec style = style_;
    const double lineWidth = host.document().defaultLineWidth();
    core::ObjectId created = core::kInvalidObjectId;
    core::Command cmd("curves.draw", core::kInvalidateStructure | core::kInvalidateGeometry,
                      [pts, closed, style, lineWidth, &created](core::CommandContext& ctx) {
                          std::vector<core::Vec2> anchors; std::vector<uint8_t> kinds;
                          smoothPenStroke(pts, closed, anchors, kinds);
                          const core::Error err = addCurve(ctx.document, anchors, kinds, closed,
                                                           style, lineWidth, &created);
                          if (!err.ok()) return core::CommandResult::fail(err.code, err.message);
                          return core::CommandResult::success();
                      });
    return host.submit(cmd, "curves.draw");
}

void DrawCurveTool::publishPreview(core::ToolHost& host, const core::ViewTransform& view) {
    core::PreviewScene& preview = host.preview();
    preview.clear();
    preview.mutableHidden().clear();
    if (raw_.size() < 2) { previewGeometry_ = CurveGeometry(); return; }

    const core::Document& doc = host.document();
    const Tessellation tess;
    std::vector<core::Vec2> anchors; std::vector<uint8_t> kinds;
    smoothPenStroke(raw_,false,anchors,kinds);
    CurveGeometry g;
    if (!buildCurveGeometry(anchors, kinds, false, style_, doc.defaultLineWidth(),
                            tess, &g).ok()) {
        previewGeometry_ = CurveGeometry();
        return;
    }
    previewGeometry_ = g;
    (void)view;
    for (size_t c = 0; c < g.contours.size(); ++c) {
        core::PreviewItem item;
        item.primitive = core::PreviewPrimitive::Polyline;
        item.points = g.contours[c];
        item.style.width = g.strokeWidth;
        preview.add(item);
    }
}

// ===========================================================================
// EditCurveTool
// ===========================================================================
EditCurveTool::EditCurveTool()
    : selected_(core::kInvalidObjectId), closed_(false), active_(false),
      dragging_(false), dragIndex_(0), pressPoint_(0, 0) {
    const CurveStyleSpec* plain = curveStyleByCommand("curveStyle_Plain");
    if (plain) style_ = *plain;
    else { style_.sourceCommand = "curveStyle_Plain"; style_.implemented = true; }
}

const core::ToolId& EditCurveTool::id() const {
    static const core::ToolId kId("r62.tool.pen.editCurve");
    return kId;
}

core::Invalidation EditCurveTool::activate(core::ToolHost& host) {
    selected_ = core::kInvalidObjectId;
    points_.clear();
    pointKind_.clear();
    closed_ = false;
    active_ = false;
    dragging_ = false;
    previewGeometry_ = CurveGeometry();
    host.preview().clear();
    host.preview().mutableHidden().clear();
    host.setStatus("Pen: click a curve to select it, then drag its control points");
    return core::kInvalidateSelection;
}

size_t EditCurveTool::pickControlPoint(const core::Vec2& point, double documentRadius) const {
    size_t best = points_.size();
    double bestDistance = documentRadius;
    for (size_t i = 0; i < points_.size(); ++i) {
        const double d = dist(points_[i], point);
        if (d <= bestDistance) { bestDistance = d; best = i; }
    }
    return best;
}

core::Error EditCurveTool::selectCurve(core::ToolHost& host, core::ObjectId curveId) {
    std::vector<core::Vec2> pts;
    std::vector<uint8_t> kinds;
    bool closed = false;
    CurveStyleSpec style;
    bool haveStyle = false;
    core::Error e = readCurve(host.document(), curveId, &pts, &kinds, &closed, &style,
                              &haveStyle);
    if (!e.ok()) return e;
    selected_ = curveId;
    points_ = pts;
    pointKind_ = kinds;
    if (pointKind_.size() != points_.size()) pointKind_.assign(points_.size(), 0);
    closed_ = closed;
    style_ = style;
    host.setSelection(std::vector<core::ObjectRef>(1,
                       core::ObjectRef(core::ObjectKind::Path, curveId)));
    publishPreview(host, host.view());
    return core::Error::success();
}

core::Error EditCurveTool::selectAt(core::ToolHost& host, const core::Vec2& point,
                                    double deviceRadius) {
    const core::Document& doc = host.document();
    const double radius = host.view().deviceRadiusToDocument(deviceRadius);
    const Tessellation tess;
    const std::vector<core::PathRecord>& records = doc.paths().records();
    core::ObjectId best = core::kInvalidObjectId;
    double bestDistance = radius * 3.0;
    for (size_t i = 0; i < records.size(); ++i) {
        if (!records[i].isAlive() || records[i].points.size() < 2) continue;
        CurveGeometry g;
        CurveStyleSpec style;
        bool haveStyle = false;
        readCurve(doc, static_cast<core::ObjectId>(i), 0, 0, 0, &style, &haveStyle);
        if (!buildCurveGeometry(records[i].points, records[i].pointKind,
                                records[i].closed, style, doc.defaultLineWidth(),
                                tess, &g).ok()) continue;
        if (!hitCurve(g, point, radius)) continue;
        double d = 1e300;
        for (size_t c = 0; c < g.contours.size(); ++c)
            d = std::min(d, distanceToPolyline(g.contours[c], point));
        if (d < bestDistance) {
            bestDistance = d;
            best = static_cast<core::ObjectId>(i);
        }
    }
    if (best == core::kInvalidObjectId)
        return core::Error(core::ErrorCode::NotFound, "no curve under the pointer");
    return selectCurve(host, best);
}

core::CommandResult EditCurveTool::beginGesture(core::ToolHost& host,
                                                const core::PointerState& pointer,
                                                core::HitContext& hits) {
    active_ = false;
    dragging_ = false;
    pressPoint_ = pointer.position;

    if (selected_ == core::kInvalidObjectId) {
        // Mode 1: pick a curve. [GUIDE p117] the cursor changes when positioned
        // correctly, and a click selects.
        const core::Error e = selectAt(host, pointer.position, host.snapRadiusDevice());
        (void)hits;
        if (!e.ok()) return core::CommandResult::fail(e.code, e.message);
        // Selecting is a complete gesture. Its release must not append a segment.
        return core::CommandResult::success(core::kInvalidateSelection);
    }

    // Mode 2: the curve is selected. A press on a control point grabs it.
    const double radius = host.view().deviceRadiusToDocument(host.snapRadiusDevice());
    const size_t index = pickControlPoint(pointer.position, radius);
    if (index < points_.size()) {
        dragging_ = true;
        active_ = true;
        dragIndex_ = index;
        const double localRadius = host.view().deviceRadiusToDocument(
            host.view().deviceRadiusToDocument(1.0) > 0.0 ? 1.0 : 1.0);
        (void)localRadius;
        return core::CommandResult::success(core::kInvalidateSelection);
    }

    // A press on the curve body (but not a point) with a curve already selected is
    // the ADD A SEGMENT gesture [GUIDE p118]. It is handled on RELEASE so a drag
    // that turns out to be a selection change is not misread as an insertion.
    active_ = true;
    return core::CommandResult::success(core::kInvalidateSelection);
}

core::CommandResult EditCurveTool::updateGesture(core::ToolHost& host,
                                                 const core::PointerState& pointer,
                                                 core::HitContext& hits) {
    if (!active_) return core::CommandResult::noChange();
    if (!dragging_) return core::CommandResult::noChange();
    // Absolute, not incremental: always move the point to where the pointer IS.
    // An incremental delta would accumulate rounding error across a long drag and
    // the committed point would not equal the previewed one.
    core::Error e = moveControlPoint(&points_, pointKind_, dragIndex_, pointer.position);
    if (!e.ok()) return core::CommandResult::fail(e.code, e.message);
    publishPreview(host, host.view());
    (void)hits;
    return core::CommandResult::success(core::kInvalidateAppearance);
}

core::CommandResult EditCurveTool::endGesture(core::ToolHost& host,
                                              const core::PointerState& pointer,
                                              core::HitContext& hits) {
    if (!active_) return core::CommandResult::noChange();

    if (dragging_) {
        // ONE transaction for the whole drag, even if the preview moved the point
        // through a hundred intermediate positions.
        const core::Error e = commit(host);
        dragging_ = false;
        active_ = false;
        (void)pointer; (void)hits;
        if (!e.ok()) return core::CommandResult::fail(e.code, e.message);
        return core::CommandResult::success(core::kInvalidateGeometry);
    }

    // A press/release with no drag on the curve body: add a segment, or close the
    // curve when the click is near the first point. Both are documented gestures
    // on the same guide page.
    active_ = false;
    const double slop = host.view().deviceRadiusToDocument(kClickSlopDevice);
    if (dist(pressPoint_, pointer.position) > slop)
        return core::CommandResult::noChange();
    const core::Error e = clickAt(host, pointer.position);
    if (!e.ok()) return core::CommandResult::fail(e.code, e.message);
    return core::CommandResult::success(core::kInvalidateGeometry);
}

core::CommandResult EditCurveTool::cancelGesture(core::ToolHost& host) {
    // Restore the stored curve, discarding the in-flight point list. The document
    // was never written, so this is a local restore.
    if (selected_ != core::kInvalidObjectId) {
        std::vector<core::Vec2> pts;
        std::vector<uint8_t> kinds;
        bool closed = false;
        if (readCurve(host.document(), selected_, &pts, &kinds, &closed, 0, 0).ok()) {
            points_ = pts;
            pointKind_ = kinds;
            closed_ = closed;
        }
    }
    dragging_ = false;
    active_ = false;
    publishPreview(host, host.view());
    return core::CommandResult::success(core::kInvalidateAppearance);
}

bool EditCurveTool::handleKey(core::ToolHost& host, uint32_t virtualKey, uint32_t modifiers) {
    (void)modifiers;
    // VK_DELETE == 0x2E, VK_BACK == 0x08. Hard-coded rather than including
    // windows.h, because the platform-independent half of this module must build
    // without Win32 (the same rule T03 and T09 follow).
    if (virtualKey != 0x2Eu && virtualKey != 0x08u) return false;
    if (selected_ == core::kInvalidObjectId || points_.size() <= 2) return false;
    const size_t index = dragIndex_ < points_.size() ? dragIndex_ : points_.size() - 1;
    const core::Error e = deleteControlPoint(&points_, &pointKind_, index, &closed_);
    if (!e.ok()) { host.setStatus(e.message); return true; }
    const core::Error c = commit(host);
    if (!c.ok()) host.setStatus(c.message);
    publishPreview(host, host.view());
    return true;
}

core::Error EditCurveTool::clickAt(core::ToolHost& host, const core::Vec2& documentPoint) {
    if (selected_ == core::kInvalidObjectId)
        return selectAt(host, documentPoint, host.snapRadiusDevice());

    bool becameClosed = false;
    const bool wasClosed = closed_;
    const std::vector<core::Vec2> before = points_;
    core::Error e = addCurveSegment(&points_, &pointKind_, documentPoint, &closed_);
    if (!e.ok()) return e;
    becameClosed = (closed_ && !wasClosed);
    if (points_.size() == before.size() && !becameClosed)
        return core::Error::success();   // nothing changed; no transaction needed

    // ONE transaction per click. "Add a segment" and "close the curve" are each a
    // single user action, so each is a single undo step.
    e = commit(host);
    if (!e.ok()) return e;
    publishPreview(host, host.view());
    return core::Error::success();
}

core::Error EditCurveTool::commit(core::ToolHost& host) {
    if (selected_ == core::kInvalidObjectId)
        return core::Error(core::ErrorCode::NotFound, "no curve is selected");
    if (points_.size() < 2)
        return core::Error(core::ErrorCode::InvalidGeometry, "a curve needs at least 2 points");
    const core::ObjectId id = selected_;
    const std::vector<core::Vec2> pts = points_;
    const std::vector<uint8_t> kinds = pointKind_;
    const bool closed = closed_;
    core::Command cmd("curves.edit",
                      core::kInvalidateGeometry | core::kInvalidateAppearance,
                      [id, pts, kinds, closed](core::CommandContext& ctx) {
                          auto* path=ctx.document.paths().mutableGet(id);
                          if(!path || !path->isAlive())
                              return core::CommandResult::fail(core::ErrorCode::NotFound,"curve is no longer present");
                          path->points=pts;
                          path->pointKind=kinds;
                          path->closed=closed;
                          ctx.document.noteTouchedPath(id);
                          return core::CommandResult::success();
                      });
    return host.submit(cmd, "curves.edit");
}

void EditCurveTool::publishPreview(core::ToolHost& host, const core::ViewTransform& view) {
    core::PreviewScene& preview = host.preview();
    preview.clear();
    preview.mutableHidden().clear();
    if (selected_ == core::kInvalidObjectId || points_.size() < 2) {
        previewGeometry_ = CurveGeometry();
        return;
    }
    // Hide the stored curve while editing so the preview REPLACES it, rather than
    // drawing the edited shape on top of the old one.
    preview.mutableHidden().push_back(core::ObjectRef(core::ObjectKind::Path, selected_));

    const core::Document& doc = host.document();
    const Tessellation tess;
    CurveGeometry g;
    if (!buildCurveGeometry(points_, pointKind_, closed_, style_, doc.defaultLineWidth(),
                            tess, &g).ok()) {
        previewGeometry_ = CurveGeometry();
        return;
    }
    previewGeometry_ = g;
    for (size_t c = 0; c < g.contours.size(); ++c) {
        core::PreviewItem item;
        item.primitive = core::PreviewPrimitive::Polyline;
        item.points = g.contours[c];
        item.style.width = g.strokeWidth;
        preview.add(item);
    }
    // The control-point handles [GUIDE p117 "adjustment handles appear along the
    // curve"], including the direction-line endpoints for a quadratic control.
    const double handle = view.deviceRadiusToDocument(3.0);
    for (size_t i = 0; i < points_.size(); ++i) {
        core::PreviewItem item;
        item.primitive = core::PreviewPrimitive::Circle;
        item.points.push_back(points_[i]);
        item.style.width = handle * 2.0;
        preview.add(item);
    }
}

// ===========================================================================
// Candidates and connect targets
// ===========================================================================
core::Error appendArrowCandidates(const core::Document& document, core::ObjectId arrowId,
                                  const core::ViewTransform& view,
                                  double documentLineWidth, const Tessellation& tess,
                                  std::vector<interaction::SnapCandidate>* out) {
    if (!out) return core::Error(core::ErrorCode::InvalidArgument, "no candidate list");
    ArrowParams params;
    bool ok = false;
    core::Error e = readArrow(document, arrowId, &params, &ok);
    if (!e.ok()) return e;
    const core::ObjectRef ref(core::ObjectKind::Arrow, arrowId);
    const std::vector<Handle> handles = arrowHandles(params, documentLineWidth, tess);
    for (size_t i = 0; i < handles.size(); ++i) {
        const Handle& h = handles[i];
        interaction::CandidateKind kind = interaction::CandidateKind::ControlHandle;
        bool eligible = false;
        // [GUIDE p111] An arrow connects at either END or at the MIDPOINT. Those
        // three handles are therefore the ones a new arrow may JOIN; the head-size
        // and split handles are editable but not join targets.
        if (h.kind == HandleKind::Begin || h.kind == HandleKind::End ||
            h.kind == HandleKind::Middle) {
            kind = interaction::CandidateKind::Endpoint;
            eligible = true;
        }
        out->push_back(interaction::SnapCandidate(ref, h.point, kind, eligible, false));
    }
    (void)view;
    return core::Error::success();
}

core::Error appendCurveCandidates(const core::Document& document, core::ObjectId curveId,
                                  const core::ViewTransform& view,
                                  std::vector<interaction::SnapCandidate>* out) {
    if (!out) return core::Error(core::ErrorCode::InvalidArgument, "no candidate list");
    const core::PathRecord* record = document.paths().alive(curveId);
    if (!record) return core::Error(core::ErrorCode::NotFound, "curve id is not alive");
    const core::ObjectRef ref(core::ObjectKind::Path, curveId);
    for (size_t i = 0; i < record->points.size(); ++i) {
        // A control point is a tolerant candidate: it uses the smaller radius so a
        // dense freehand curve does not swallow the whole neighbourhood.
        out->push_back(interaction::SnapCandidate(
            ref, record->points[i], interaction::CandidateKind::ControlHandle,
            false, true));
    }
    (void)view;
    return core::Error::success();
}

ConnectTarget resolveConnectTarget(const core::Document& document,
                                   const core::Vec2& pointer, double documentRadius,
                                   double documentLineWidth, const Tessellation& tess) {
    ConnectTarget best;
    double bestDistance = documentRadius;
    const std::vector<core::AnnotationRecord>& records = document.arrows().records();
    for (size_t i = 0; i < records.size(); ++i) {
        if (!records[i].isAlive()) continue;
        const core::ObjectId id = static_cast<core::ObjectId>(i);
        ArrowParams params;
        bool ok = false;
        if (!readArrow(document, id, &params, &ok).ok()) continue;
        const std::vector<Handle> handles = arrowHandles(params, documentLineWidth, tess);
        for (size_t k = 0; k < handles.size(); ++k) {
            const Handle& h = handles[k];
            AnchorSlot slot = AnchorSlot::None;
            if (h.kind == HandleKind::Begin) slot = AnchorSlot::Begin;
            else if (h.kind == HandleKind::End) slot = AnchorSlot::End;
            else if (h.kind == HandleKind::Middle) slot = AnchorSlot::Midpoint;
            else continue;
            const double d = dist(h.point, pointer);
            // Deterministic tie break on (slot, id) so the answer does not depend on
            // the order the slab happens to be walked in.
            if (d < bestDistance ||
                (d == bestDistance && best.valid &&
                 (slot < best.slot || (slot == best.slot && id < best.object)))) {
                bestDistance = d;
                best.object = id;
                best.slot = slot;
                best.point = h.point;
                best.valid = true;
            }
        }
    }
    return best;
}

} // namespace arrows
} // namespace r62

