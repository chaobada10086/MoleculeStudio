#include "next/symbols_orbitals/push.hpp"

#include <cmath>

namespace r62 {
namespace symbols {

namespace {

// [PROJECT] head sizes in document units. The guide fixes the SHAPE distinction,
// not a millimetre size, so these are our choices. They are deliberately unequal:
// a half (single-sided) head of the same nominal length as a full head reads as
// the same object at a glance, which is exactly the confusion the acceptance
// criterion forbids.
const double kFullHeadLength = 3.2;
const double kFullHeadWidth  = 2.4;
const double kHalfHeadLength = 3.6;
const double kHalfHeadWidth  = 2.2;

} // namespace

PushEndpoint resolvePushEndpoint(const core::Document& d, const core::ObjectRef& ref) {
    switch (ref.kind) {
    case core::ObjectKind::Atom:
        return d.atoms().alive(ref.id) ? PushEndpoint::Atom : PushEndpoint::None;
    case core::ObjectKind::Bond:
        return d.bonds().alive(ref.id) ? PushEndpoint::Bond : PushEndpoint::None;
    case core::ObjectKind::Orbital:
        return d.orbitals().alive(ref.id) ? PushEndpoint::Orbital : PushEndpoint::None;
    default:
        // [GUIDE p158] lists atoms, bonds and orbitals ONLY. A text, an arrow or a
        // symbol is refused rather than silently accepted.
        return PushEndpoint::None;
    }
}

bool pushEndpointAnchor(const core::Document& d, const core::ObjectRef& ref,
                        core::Vec2* out) {
    if (!out) return false;
    switch (ref.kind) {
    case core::ObjectKind::Atom: {
        const core::AtomRecord* a = d.atoms().alive(ref.id);
        if (!a) return false;
        *out = a->position;
        return true;
    }
    case core::ObjectKind::Bond: {
        const core::BondRecord* b = d.bonds().alive(ref.id);
        if (!b) return false;
        const core::AtomRecord* a0 = d.atoms().alive(b->beginAtom);
        const core::AtomRecord* a1 = d.atoms().alive(b->endAtom);
        if (!a0 || !a1) return false;
        // The midpoint of the bond is where a mechanism arrow points.
        *out = core::Vec2((a0->position.x + a1->position.x) * 0.5,
                          (a0->position.y + a1->position.y) * 0.5);
        return true;
    }
    case core::ObjectKind::Orbital: {
        const core::AnnotationRecord* o = d.orbitals().alive(ref.id);
        if (!o) return false;
        *out = o->geometry[0];
        return true;
    }
    default:
        return false;
    }
}

arrows::ArrowParams pushToArrowParams(const PushParams& p, double documentLineWidth) {
    // The head axis is the whole point of this function.
    const bool half = (p.head == PushHead::HalfRadical);

    // A push is a straight arrow with T10 family Straight. The preset id used here
    // is T10 preset 0 (a plain single-shaft arrow with a solid head at the end);
    // the head is then overwritten below, so the mapping does not depend on which
    // T10 row happens to be first.
    arrows::ArrowParams ap = arrows::resolvePreset(0, p.begin, p.end, documentLineWidth);

    // A push is normally straight, but a user may drag the chord to curve it (the
    // guide says its shape is editable "by using either the Arrow tool, or the
    // Electron Pushing Tool" [GUIDE p158]). T10 only actually honours the sagitta
    // when the FAMILY is Arc, so the family must change with the sagitta; leaving
    // family=Straight with a non-zero sagitta silently produced a straight line.
    const bool curved = (p.sagitta != 0.0);
    ap.family    = curved ? arrows::ArrowFamily::Arc : arrows::ArrowFamily::Straight;
    ap.shafts    = arrows::ShaftCount::One;
    ap.sweep     = curved ? arrows::ArcSweep::Free : arrows::ArcSweep::Straight;
    ap.direction = arrows::ArcDirection::None;
    ap.arcSagitta = p.sagitta;
    ap.scale      = (std::isfinite(p.scale) && p.scale > 0.0) ? p.scale : 1.0;

    if (half) {
        // [GUIDE p158] ONE electron -> HALF head. A one-sided barb is a genuinely
        // different silhouette, so the pair and the single-electron arrows can
        // never be mistaken for one another.
        ap.head.shape  = arrows::ArrowHeadShape::Barb;
        ap.head.sides  = arrows::HeadSides::Left;
        ap.head.length = kHalfHeadLength;
        ap.head.width  = kHalfHeadWidth;
    } else {
        // [GUIDE p158] electron PAIR -> FULL head.
        ap.head.shape  = arrows::ArrowHeadShape::Solid;
        ap.head.sides  = arrows::HeadSides::Both;
        ap.head.length = kFullHeadLength;
        ap.head.width  = kFullHeadWidth;
    }
    ap.head.atEnd   = true;
    ap.head.atBegin = false;
    return ap;
}

core::Error buildPushGeometry(const PushParams& p, double documentLineWidth,
                              const arrows::Tessellation& tess,
                              arrows::ArrowGeometry* out) {
    if (!out) return core::Error(core::ErrorCode::InvalidArgument, "null out");
    const core::Error v = validatePushParams(p);
    if (!v.ok()) return v;
    const arrows::ArrowParams ap = pushToArrowParams(p, documentLineWidth);
    return arrows::buildArrowGeometry(ap, documentLineWidth, tess, out);
}

double pushSagittaFromDrag(const core::Vec2& begin, const core::Vec2& end,
                           const core::Vec2& pointer) {
    // Delegated: T10 owns the inverse mapping, so a push drag and an arrow arc
    // drag produce the SAME sagitta for the same pointer motion.
    return arrows::sagittaFromHandleDrag(begin, end, pointer);
}

} // namespace symbols
} // namespace r62
