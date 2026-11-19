// =============================================================================
// re_round62_viewer/next/bonds/adapters.cpp
//
// T07 - render / preset / tool adapters. See adapters.hpp.
//
// This is the ONLY file in next/bonds/ that includes next/render. geometry.cpp
// stays render-free so a headless test can assert exact polygons without T03.
// =============================================================================

#include "next/bonds/adapters.hpp"

#include "next/bonds/edit.hpp"
#include "next/render/geometry.hpp"

#include <cmath>

namespace r62 {
namespace bonds {

namespace {

// FNV-1a over the fields that change what is drawn. The T03 incremental cache
// compares this value: equal -> reuse the cached mesh, different -> rebuild.
inline void mix64(uint64_t* h, uint64_t v) {
    *h ^= v;
    *h *= 1099511628211ull;
}
// core::CommandResult has no Error constructor; one adapter keeps call sites
// uniform (the same helper exists privately in edit.cpp).
core::CommandResult fromErrorPublic(const core::Error& e) {
    core::CommandResult r;
    r.error = e;
    r.invalidation = core::kInvalidateNone;
    return r;
}

inline void mixDouble(uint64_t* h, double d) {
    // Quantise to 1/1000 document unit so a bit-level float difference that is
    // visually irrelevant does not thrash the cache.
    mix64(h, static_cast<uint64_t>(static_cast<int64_t>(d * 1000.0)));
}

} // namespace

// ---------------------------------------------------------------------------
// Render adapter
// ---------------------------------------------------------------------------
bool toPrimitives(const BondRenderData& data, const core::ObjectRef& source,
                  const RenderAdapterOptions& options,
                  std::vector<render::Primitive>* out) {
    if (!out) return false;
    out->clear();
    if (data.degenerate || data.shapes.empty()) return false;

    for (size_t i = 0; i < data.shapes.size(); ++i) {
        const BondShape& s = data.shapes[i];
        render::Primitive p;
        p.layer = options.layer;
        // The axis stroke sits below the decoration so an overlapping wedge
        // outline still reads. Every shape keeps the SAME source ref.
        p.z = options.z + static_cast<int32_t>(i);
        p.source = source;
        p.style.stroke = render::Paint(options.color, true);
        p.style.strokeWidth = s.strokeWidth > 0.0 ? s.strokeWidth : options.style.width;
        if (!(p.style.strokeWidth > 0.0)) p.style.strokeWidth = options.viewDefaultWidth;
        p.style.cap = render::CapStyle::Butt;
        p.style.join = render::JoinStyle::Miter;
        if (s.dashOn > 0.0 && s.dashOff > 0.0)
            p.style.dash = render::DashPattern(s.dashOn, s.dashOff);
        if (s.wavyAmplitude > 0.0) {
            p.style.wavyAmplitude = s.wavyAmplitude;
            p.style.wavyWavelength = s.wavyWavelength;
        }

        switch (s.kind) {
            case BondShapeKind::FilledPolygon: {
                p.kind = render::PrimKind::Polygon;
                render::Subpath outer;
                outer.points = s.outline;
                outer.closed = true;
                p.subpaths.push_back(outer);
                if (!s.fillHoles.empty()) {
                    render::Subpath hole;
                    hole.points = s.fillHoles;
                    hole.closed = true;
                    p.subpaths.push_back(hole);
                }
                p.style.fill = render::Paint(options.color, s.fill);
                p.style.stroke.enabled = false;
                p.style.fillRule = render::FillRule::EvenOdd;
                break;
            }
            case BondShapeKind::Polyline: {
                // Several independent strokes in one primitive. T03 strokes each
                // consecutive PAIR, which is what a hashed fan needs.
                p.kind = render::PrimKind::Polyline;
                p.points = s.points;
                break;
            }
            case BondShapeKind::Stroke:
            default: {
                p.kind = render::PrimKind::Polyline;
                p.points = s.points;
                break;
            }
        }
        if (p.points.size() < 2 && p.subpaths.empty()) continue;
        p.bounds = render::geom::computeBounds(p, options.viewDefaultWidth);
        p.version = 0;   // filled by signatureOf() at the caller's level
        out->push_back(p);
    }
    return !out->empty();
}

uint64_t signatureOf(const BondRenderData& data, const core::BondRecord& record,
                     const RenderAdapterOptions& options) {
    uint64_t h = 14695981039346656037ull;
    mix64(&h, static_cast<uint64_t>(record.id));
    mix64(&h, static_cast<uint64_t>(record.beginAtom));
    mix64(&h, static_cast<uint64_t>(record.endAtom));
    mix64(&h, static_cast<uint64_t>(record.order));
    mix64(&h, static_cast<uint64_t>(record.display));
    mix64(&h, static_cast<uint64_t>(record.semantics));
    mix64(&h, static_cast<uint64_t>(record.stereo));
    mix64(&h, static_cast<uint64_t>(record.alive));
    mixDouble(&h, data.axisBegin.x);
    mixDouble(&h, data.axisBegin.y);
    mixDouble(&h, data.axisEnd.x);
    mixDouble(&h, data.axisEnd.y);
    mix64(&h, static_cast<uint64_t>(options.color));
    mixDouble(&h, options.viewDefaultWidth);
    return h;
}

// ---------------------------------------------------------------------------
// Preset adapter
// ---------------------------------------------------------------------------
core::ToolPreset toToolPreset(const BondPreset& preset) {
    core::ToolPreset tp;
    tp.id = preset.id;
    tp.behavior = core::ToolBehavior::DragBond;
    tp.target = core::PresetTarget::Bond;
    tp.displayName = preset.displayName;
    tp.category = "化学键";                 // exactly tools.csv's category cell
    tp.sourceCommand = preset.command;      // exact tools.csv command spelling
    tp.optionKey = preset.id;
    tp.invalidation = core::kInvalidateAppearance | core::kInvalidateSemantics;
    tp.implemented = preset.implemented;
    return tp;
}

// ---------------------------------------------------------------------------
// Tool adapter
// ---------------------------------------------------------------------------
BondStyleTool::BondStyleTool(const BondPreset& preset)
    : id_(preset.id), preset_(preset), origin_(0, 0), originJoined_(false),
      originAtom_(core::kInvalidObjectId), active_(false) {}

void BondStyleTool::setPreset(const BondPreset& preset) {
    preset_ = preset;
    id_ = preset.id;
}

// PointerDown: resolve the ORIGIN once and keep it. Nothing is written yet.
core::CommandResult BondStyleTool::beginGesture(core::ToolHost& host,
                                                const core::PointerState& pointer,
                                                core::HitContext& hits) {
    origin_ = pointer.position;
    originJoined_ = false;
    originAtom_ = core::kInvalidObjectId;

    // Join to an existing atom when one is under the pointer. The snap radius is
    // supplied by the platform profile (T04) and converted from device pixels by
    // the ViewTransform - never assumed to be document units.
    const double radiusDoc =
        hits.view().deviceRadiusToDocument(host.snapRadiusDevice());
    const core::ObjectId near = hits.nearestAtom(pointer.position, host.snapRadiusDevice());
    if (near != core::kInvalidObjectId) {
        (void)radiusDoc;
        const core::AtomRecord* a = hits.document().atoms().alive(near);
        if (a) {
            // Joining means the resolved origin IS the atom stored coordinate,
            // so preview and commit cannot drift by a fraction of a point.
            origin_ = a->position;
            originJoined_ = true;
            originAtom_ = near;
        }
    }
    active_ = true;
    host.setStatus(preset_.displayName);
    return core::CommandResult::noChange();
}

// PointerMove: publish a preview only. The document is NOT touched during a
// gesture; that is what makes "preview equals commit" structural.
core::CommandResult BondStyleTool::updateGesture(core::ToolHost& host,
                                                 const core::PointerState& pointer,
                                                 core::HitContext& hits) {
    if (!active_) return core::CommandResult::noChange();
    core::PreviewScene& preview = host.preview();
    preview.clear();

    // The pointer is the source of truth for the free end. A target atom under
    // the pointer substitutes its exact coordinate.
    core::Vec2 end = pointer.position;
    const core::ObjectId near = hits.nearestAtom(pointer.position, host.snapRadiusDevice());
    if (near != core::kInvalidObjectId && near != originAtom_) {
        const core::AtomRecord* b = hits.document().atoms().alive(near);
        if (b) end = b->position;
    }

    const BondGeometryOptions& o = options_;
    BondAxis ax = makeAxis(origin_, end, 0.0);
    if (ax.degenerate) return core::CommandResult::noChange();

    const BondRenderData data = buildBondGeometry(ax, preset_.axes, o,
                                                  EndpointContext(), EndpointContext());
    for (size_t i = 0; i < data.shapes.size(); ++i) {
        const BondShape& s = data.shapes[i];
        core::PreviewItem item;
        item.style.width = s.strokeWidth;
        if (s.kind == BondShapeKind::FilledPolygon) {
            item.primitive = core::PreviewPrimitive::Polygon;
            item.points = s.outline;
            item.fillEnabled = 1;
            item.fillRGBA = 0x8A8A8AFFu;
        } else if (s.points.size() == 2) {
            item.primitive = core::PreviewPrimitive::Segment;
            item.points = s.points;
        } else {
            item.primitive = core::PreviewPrimitive::Polyline;
            item.points = s.points;
        }
        preview.add(item);
    }
    return core::CommandResult::noChange();
}

// PointerUp: commit the SAME resolved endpoints the preview used.
core::CommandResult BondStyleTool::endGesture(core::ToolHost& host,
                                              const core::PointerState& pointer,
                                              core::HitContext& hits) {
    if (!active_) return core::CommandResult::noChange();
    active_ = false;
    host.preview().clear();

    core::Vec2 end = pointer.position;
    core::ObjectRef endRef;
    bool endJoined = false;
    const core::ObjectId near = hits.nearestAtom(pointer.position, host.snapRadiusDevice());
    if (near != core::kInvalidObjectId && near != originAtom_) {
        const core::AtomRecord* b = hits.document().atoms().alive(near);
        if (b) {
            end = b->position;
            endRef = core::ObjectRef(core::ObjectKind::Atom, near);
            endJoined = true;
        }
    }
    // A zero-length drag creates nothing: SOURCES_AND_LIMITS.md records that a
    // zero-length drag must not manufacture a default direction.
    BondAxis probe = makeAxis(origin_, end, options_.minDecoratedLength);
    if (probe.degenerate) {
        host.setStatus("bond: zero-length drag ignored");
        return core::CommandResult::noChange();
    }

    BondEndpoint originEp;
    originEp.point = origin_;
    originEp.joined = originJoined_;
    if (originJoined_) originEp.target = core::ObjectRef(core::ObjectKind::Atom, originAtom_);

    interaction::SnapResult snap;
    snap.endpoint = end;
    snap.target = endRef;
    snap.joined = endJoined;
    snap.valid = true;

    core::ObjectId bond = core::kInvalidObjectId;
    const core::Error e = host.submit(
        addBondFromSnapCommand(originEp, snap, preset_.axes, 6, &bond), "bond.tool");
    if (!e.ok()) return fromErrorPublic(e);
    return core::CommandResult::success();
}

core::CommandResult BondStyleTool::cancelGesture(core::ToolHost& host) {
    active_ = false;
    host.preview().clear();
    return core::CommandResult::noChange();
}

// Character hotkeys for the side placement are OWNED BY T04's keymap, not here.
// Returning false is the honest answer: this tool consumes no characters, and
// pretending otherwise would shadow the documented element hotkeys.
bool BondStyleTool::handleChar(core::ToolHost& host, uint32_t unicodeChar,
                               uint32_t modifiers) {
    (void)host; (void)unicodeChar; (void)modifiers;
    return false;
}

} // namespace bonds
} // namespace r62
