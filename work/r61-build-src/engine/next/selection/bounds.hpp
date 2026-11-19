#ifndef R62_NEXT_SELECTION_BOUNDS_HPP
#define R62_NEXT_SELECTION_BOUNDS_HPP
// =============================================================================
// re_round62_viewer/next/selection/bounds.hpp
//
// T05 - EXACT BOUNDS FOR EVERY OBJECT CLASS.
//
// WHY THIS IS ITS OWN FILE
// -----------------------------------------------------------------------------
// Everything T05 does - marquee, lasso, align, distribute, centre, group frame,
// transform pivot, scale factor - is computed from bounds. If bounds are wrong
// then every one of those is wrong in the same way, and the symptom ("the
// selection jumped") appears three modules away from the cause. So bounds are
// defined once, here, with a written rule per object kind, and the tests assert
// the rule rather than an incidental number.
//
// RELATION TO T03
// -----------------------------------------------------------------------------
// T03's render::computeBounds gives the CONSERVATIVE VISIBLE bounds of one
// PRIMITIVE (inflated by stroke width, arrowheads, glyph ink, markers, shadow,
// handles). T05 needs the GEOMETRIC bounds of one DOCUMENT OBJECT because it
// must invert them: scaling geometry by k must produce bounds scaled by exactly
// k. Inflating by a stroke width that does not scale would break that identity.
// The two are therefore different functions with different contracts, not a
// duplicate. Where a caller wants a HIT box it should use T03's; where it wants
// a geometric extent it uses this one.
//
// STROKE / DECORATION POLICY (explicit, because align must not lie)
// -----------------------------------------------------------------------------
// * Atom   : its position. An element label is drawn AT the atom, so the label
//            does not move the box. (A label that overflows the box is a
//            rendering concern; align operates on chemistry geometry.)
// * Bond   : the segment between the two atom positions. A double bond's second
//            line and a wedge's flare are display, not geometry.
// * Text   : an ABSTRACT BOX: the anchor point plus the INK EXTENT SUPPLIED BY
//            THE CALLER. This module does not link a font engine, so a caller
//            that has real metrics (T09 vector_text) passes them in via
//            TextMetricSink. With no sink the box is the anchor point, which is
//            honest ("we do not know") rather than a guessed 0.6*size.
//            Text bounds are the ONE place where the caller's metrics matter,
//            so the discrepancy is reportable: see BoundsReport.
// * Path   : all control points. By the convex-hull property of quadratics the
//            flattened curve lies inside the control polygon, so the control
//            box CONTAINS the visible curve. It is conservative in the same
//            direction T03 documents.
// * Arrow/Bracket/Orbital/Symbol/BioShape : the annotation's geometry[0..3]
//            points, restricted to those the preset actually uses. The caller
//            supplies "used point count" through AnnotationGeomRules so an
//            arrow with 2 live points is not boxed by 4 stale ones.
// * Table  : its declared frame box.
// * Group  : the union of its ALIVE members' bounds, recursively. A group whose
//            members are all gone is EMPTY (this is what makes "delete a group
//            and undo restores the full hierarchy" a bounds question too).
// =============================================================================

#include "next/core/document.hpp"

#include <map>
#include <string>
#include <vector>

namespace r62 {
namespace selection {

// ---------------------------------------------------------------------------
// Interface version
// ---------------------------------------------------------------------------
enum {
    kSelectionMajor    = 1,
    kSelectionMinor    = 0,
    kSelectionRevision = 0,
    kSelectionVersion  = kSelectionMajor * 10000 + kSelectionMinor * 100 + kSelectionRevision
};

inline bool requestSelectionCompatibility(int major, int minor) {
    return major == kSelectionMajor && minor <= kSelectionMinor;
}

// The core interface this build compiled against. A host checks both.
int compiledCoreInterfaceVersion();
enum { kCoreInterfaceMajor = 1, kCoreInterfaceMinor = 0 };

// ---------------------------------------------------------------------------
// TextMetricSink: caller-supplied text ink extent
// ---------------------------------------------------------------------------
// T09 (vector text) implements this. Core's own 2x guess is NOT used: a wrong
// guess inside an align operation is a visible defect, so the module reports
// "no metrics" instead.
class TextMetricSink {
public:
    virtual ~TextMetricSink() {}
    // Ink box of `utf8` laid out from the baseline anchor `anchor` at em size
    // `size` in font `fontName`. Returns false when the sink has no opinion
    // (missing glyph, no font). A false return falls back to the anchor point.
    virtual bool textInkBox(const std::string& utf8, const core::Vec2& anchor,
                            double size, const std::string& fontName,
                            core::Box* out) = 0;
};

// Deterministic sink for tests and for headless use: every code point is an
// em-square of the requested size. It is NOT a font; it is a reproducible
// stand-in whose numbers the tests can predict.
class MonospaceMetricSink : public TextMetricSink {
public:
    virtual bool textInkBox(const std::string& utf8, const core::Vec2& anchor,
                            double size, const std::string& fontName,
                            core::Box* out);
};

// TextMetricSink measures a STRING, which is not enough for a real document:
// a caption carries rich runs (bold, sub/superscript, per-run fonts), and its
// ink extent depends on the document that owns it. Align therefore asks a
// provider that receives the Text OBJECT. The host wires in the same layout
// engine the renderer uses, so the box align moves by is the box the user sees
// selected - a caption is never treated as its anchor point. Without a
// provider the module keeps the documented anchor-point fallback.
class TextBoundsProvider {
public:
    virtual ~TextBoundsProvider() {}
    virtual bool textInkBox(const core::Document& document, core::ObjectId id,
                            core::Box* out) const = 0;
};

// ---------------------------------------------------------------------------
// AnnotationGeomRules
// ---------------------------------------------------------------------------
// How many of an annotation's geometry[] points a preset actually uses, and
// whether the preset is drawn in a rotated local frame. Kept as an injectable
// rule table rather than a hard-coded switch so T03's preset registry stays the
// single source of preset semantics.
struct AnnotationGeomRule {
    uint8_t usedPoints;    // 1..4 points of geometry[] that carry the extent
    bool    addScale;      // inflate the box by scale (a shipped glyph's extent)
    AnnotationGeomRule() : usedPoints(2), addScale(false) {}
};

class AnnotationGeomRules {
public:
    AnnotationGeomRules();
    // Default: two-point annotations use 2 points, the four-point control-box
    // ones use 4. `presetId` is T01's stable preset index; unknown ids keep the
    // default rule and are counted in BoundsReport.unruledPresets.
    AnnotationGeomRule ruleFor(core::ObjectKind kind, uint32_t presetId) const;
    void setRule(core::ObjectKind kind, uint32_t presetId, const AnnotationGeomRule& rule);
    size_t ruleCount() const { return rules_.size(); }
private:
    std::map<std::pair<uint16_t, uint32_t>, AnnotationGeomRule> rules_;
};

// ---------------------------------------------------------------------------
// BoundsReport: what the computation could NOT see
// ---------------------------------------------------------------------------
// A bounds computation that silently fell back to a point is the failure this
// struct makes countable.
struct BoundsReport {
    uint64_t objectsMeasured;
    uint64_t textWithoutMetrics;   // Text objects boxed at their anchor
    uint64_t emptyObjects;         // alive object with no geometry at all
    uint64_t unruledPresets;
    uint64_t deadRefsSkipped;
    uint64_t groupsExpanded;
    BoundsReport();
    void clear();
    std::string summary() const;
};

// ---------------------------------------------------------------------------
// BoundsOptions
// ---------------------------------------------------------------------------
struct BoundsOptions {
    // Unused when a sink is null.
    TextMetricSink* textMetrics;
    // Document default text size, used when a TextRecord carries size == 0.
    double defaultTextSize;
    // Document default font, used when neither the record nor the run names one.
    std::string defaultFont;
    // Expand a Group into its members' union. When false a group contributes a
    // zero-area box at the origin, which is only useful for debugging.
    bool expandGroups;
    // Guard against a group that (through corruption) contains itself.
    uint32_t maxGroupDepth;
    const AnnotationGeomRules* annotationRules;
    BoundsOptions();
};

// ---------------------------------------------------------------------------
// Per-object bounds
// ---------------------------------------------------------------------------
// A single alive object. Returns an EMPTY box (max < min) when the object does
// not exist, is dead, or has no geometry; the caller distinguishes those through
// the report. Never returns a box containing non-finite coordinates: a NaN atom
// is reported as empty rather than poisoning every union it takes part in.
core::Box objectBounds(const core::Document& document, const core::ObjectRef& ref,
                       const BoundsOptions& options, BoundsReport* report);

// Convenience overload with default options.
core::Box objectBounds(const core::Document& document, const core::ObjectRef& ref);

// Union over a set of refs. Duplicates are harmless. A ref that is dead or has
// no geometry contributes nothing and is counted in the report.
core::Box selectionBounds(const core::Document& document,
                          const std::vector<core::ObjectRef>& refs,
                          const BoundsOptions& options, BoundsReport* report);

core::Box selectionBounds(const core::Document& document,
                          const std::vector<core::ObjectRef>& refs);

// Bounds of every alive object in the document. This is the "all objects"
// extent used by Select All and by page-centring, and it deliberately walks
// every slab rather than a spatial index so a stale index cannot shrink it.
core::Box documentBounds(const core::Document& document,
                         const BoundsOptions& options, BoundsReport* report);

core::Box documentBounds(const core::Document& document);

// ---------------------------------------------------------------------------
// Small geometry helpers this module needs and core does not provide
// ---------------------------------------------------------------------------
inline core::Vec2 boxCenter(const core::Box& box) {
    return core::Vec2((box.min.x + box.max.x) * 0.5, (box.min.y + box.max.y) * 0.5);
}
inline bool boxValid(const core::Box& box) { return !box.empty(); }
inline bool boxFinite(const core::Box& box) {
    return box.min.finite() && box.max.finite();
}
// Union honouring an initially-empty accumulator. core::Box::add() on a default
// box would otherwise clip to (0,0).
void unionBox(core::Box* accumulator, const core::Box& box);
// Intersection. Returns false when the boxes do not overlap.
bool intersectBox(const core::Box& a, const core::Box& b, core::Box* out);
// Expand uniformly by `margin` document units.
core::Box inflateBox(const core::Box& box, double margin);
// Apply an axis-aligned scale about `pivot` (used by scale/stretch/flip tests).
core::Box scaleBox(const core::Box& box, const core::Vec2& pivot, double sx, double sy);

} // namespace selection
} // namespace r62

#endif // R62_NEXT_SELECTION_BOUNDS_HPP
