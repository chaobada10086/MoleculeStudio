#include "next/arrows_curves/tools.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace r62 {
namespace arrows {

namespace {

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
    double bestDistance = radius;
    for (size_t i = 0; i < handles.size(); ++i) {
        const double d = dist(handles[i].point, pointer);
        // Strictly inside the radius, deliberately: a press exactly ON the capture
        // ring is a boundary case, and treating it as a grab makes "click the arrow
        // body to reverse it" [GUIDE p110] impossible at that exact distance. The
        // tie also picks the SMALLEST distance, so handle order never decides.
        if (d < bestDistance) { bestDistance = d; best = i; }
    }
    return best;
}

core::CommandResult ArrowTool::beginGesture(core::ToolHost& host,
                                            const core::PointerState& pointer,
                                            core::HitContext& hits) {
    state_ = ArrowGestureState();
    previewGeometry_ = ArrowGeometry();
    state_.active = true;
    state_.pressPoint = pointer.position;

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
            if (hitIndex < handles.size()) {
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
            if(hitArrow(geometry,pointer.position,host.view().deviceRadiusToDocument(5))){
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
    const core::Vec2 beginPoint = resolvePlacementOrigin(doc, pointer.position, nearAtom);
    state_.pending = resolveArrowDrag(presetId_, beginPoint, beginPoint, lineWidth,
                                      host.fixedLength(), host.fixedAngles(),
                                      host.angleStepDegrees(), doc.defaultBondLength(),
                                      pointer.modifiers);
    state_.pending.strokeWidth=defaultStrokeWidth_;
    publishPreview(host, host.view());
    return core::CommandResult::success(core::kInvalidateAppearance);
}

core::CommandResult ArrowTool::updateGesture(core::ToolHost& host,
                                             const core::PointerState& pointer,
                                             core::HitContext& hits) {
    if (!state_.active) return core::CommandResult::noChange();
    if (!state_.moved && !movedPastSlop(state_.pressPoint, pointer.position, host.view()))
        return core::CommandResult::noChange();
    state_.moved = true;

    const core::Document& doc = hits.document();
    const double lineWidth = doc.defaultLineWidth();
    const Tessellation tess;

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

        // Work from the value captured at press time when the bag could not be
        // read, so the gesture still behaves.
        if (state_.pending.begin == state_.pending.end && haveBase) p = base;

        switch (state_.grabbed) {
        case HandleKind::Begin:
            p.begin = pointer.position;
            break;
        case HandleKind::End:
            p.end = pointer.position;
            break;
        case HandleKind::Middle:
            // Drag the middle to create or change the arc [GUIDE p109]. The inverse
            // of the arc-midpoint mapping is ONE function, so preview and commit
            // read the same sagitta.
            p.arcSagitta = sagittaFromHandleDrag(p.begin, p.end, pointer.position);
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
            p.arcSagitta = sagittaFromHandleDrag(p.begin, p.end, pointer.position);
            break;
        case HandleKind::HeadSize: {
            const core::Vec2 d(p.end.x - p.begin.x, p.end.y - p.begin.y);
            const double chord = std::sqrt(d.x * d.x + d.y * d.y);
            if (chord > 1e-9) {
                const core::Vec2 u(d.x / chord, d.y / chord);
                const core::Vec2 n(-u.y, u.x);
                const core::Vec2 tip = p.head.atEnd ? p.end : p.begin;
                const core::Vec2 rel(pointer.position.x - tip.x, pointer.position.y - tip.y);
                const double w = std::fabs(rel.x * n.x + rel.y * n.y);
                const double ratio = (state_.pending.head.width > 1e-9)
                    ? state_.pending.head.length / state_.pending.head.width : 1.3;
                p.head.width  = clampd(w * 1.1, 0.4, chord * 0.6);
                p.head.length = clampd(p.head.width * ratio, 0.4, chord * 0.9);
            }
            break;
        }
        case HandleKind::SplitA: {
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
            if (host.fixedAngles() && !release) {
                double lenOut = 0.0;
                const core::Vec2 dir = snapDirection(p.begin, pointer.position,
                                                     host.angleStepDegrees(),
                                                     host.fixedAngles(),
                                                     0.0,
                                                     &lenOut);
                p.end = core::Vec2(p.begin.x + dir.x * lenOut, p.begin.y + dir.y * lenOut);
            }
        }

        state_.pending = p;
    } else {
        // ---- dragging out a NEW arrow -------------------------------------
        state_.pending = resolveArrowDrag(presetId_, state_.pressPoint, pointer.position,
                                          lineWidth, host.fixedLength(), host.fixedAngles(),
                                          host.angleStepDegrees(), doc.defaultBondLength(),
                                          pointer.modifiers);
        state_.pending.strokeWidth=defaultStrokeWidth_;
    }

    publishPreview(host, host.view());
    (void)tess;
    return core::CommandResult::success(core::kInvalidateAppearance);
}

core::CommandResult ArrowTool::endGesture(core::ToolHost& host,
                                          const core::PointerState& pointer,
                                          core::HitContext& hits) {
    if (!state_.active) return core::CommandResult::noChange();
    // Mouse-up carries the authoritative final pointer, including a drag whose
    // last move was coalesced by Windows.
    if(movedPastSlop(state_.pressPoint,pointer.position,host.view()))updateGesture(host,pointer,hits);

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
    if (state_.target != core::kInvalidObjectId)
        return commitArrow(host, state_.target, state_.pending);
    return commitArrow(host, core::kInvalidObjectId, state_.pending);
}

void ArrowTool::publishPreview(core::ToolHost& host, const core::ViewTransform& view) {
    core::PreviewScene& preview = host.preview();
    preview.clear();
    preview.mutableHidden().clear();

    const core::Document& doc = host.document();
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
        item.points = g.shafts[s];
        item.style.width = g.strokeWidth;
        preview.add(item);
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
    if (std::fabs(state_.pending.arcSagitta) > 1e-6) {
        core::PreviewItem label;
        label.primitive = core::PreviewPrimitive::Label;
        label.points.push_back(state_.pending.begin);
        const double chord = core::length(state_.pending.end - state_.pending.begin);
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.1f deg",
                      arcAngleDegrees(state_.pending.arcSagitta, chord));
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
    const ArrowParams payload = params;   // by value: the command must be copyable
    const core::ObjectId id = target;

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
    if (raw_.size() < options_.maxPoints) raw_.push_back(pointer.position);
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
    const std::vector<core::Vec2> pts = simplified_;
    const bool isClosed = closed;
    const double lineWidth = host.document().defaultLineWidth();
    core::ObjectId created = core::kInvalidObjectId;
    core::Command cmd("curves.draw",
                      core::kInvalidateStructure | core::kInvalidateGeometry,
                      [pts, isClosed, style, lineWidth, &created](core::CommandContext& ctx) {
                          const std::vector<uint8_t> kinds(pts.size(), 0);
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
                          const std::vector<uint8_t> kinds(pts.size(), 0);
                          const core::Error err = addCurve(ctx.document, pts, kinds, closed,
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
    const std::vector<uint8_t> kinds(raw_.size(), 0);
    CurveGeometry g;
    if (!buildCurveGeometry(raw_, kinds, false, style_, doc.defaultLineWidth(),
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
        active_ = true;
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
    const CurveStyleSpec style = style_;
    const double lineWidth = host.document().defaultLineWidth();
    core::Command cmd("curves.edit",
                      core::kInvalidateGeometry | core::kInvalidateAppearance,
                      [id, pts, kinds, closed, style, lineWidth](core::CommandContext& ctx) {
                          const core::Error err = writeCurve(ctx.document, id, pts, kinds,
                                                             closed, style, lineWidth);
                          if (!err.ok()) return core::CommandResult::fail(err.code, err.message);
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

