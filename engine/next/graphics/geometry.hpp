#ifndef R62_NEXT_GRAPHICS_GEOMETRY_HPP
#define R62_NEXT_GRAPHICS_GEOMETRY_HPP
// =============================================================================
// re_round62_viewer/next/graphics/geometry.hpp
//
// T11 - PURE GEOMETRY KERNEL for the 32 drawing shapes and the 8 brackets.
//
// -----------------------------------------------------------------------------
// WHY THIS FILE LINKS NO RENDERER AND NO DOCUMENT
// -----------------------------------------------------------------------------
// The kernel takes a parameter bundle and returns CONTOURS. It includes only
// next/core/contracts.hpp (for Vec2/Box) and next/graphics/preset.hpp. That is
// what makes every geometry claim in this task testable headlessly, and it is
// the same boundary next/arrows_curves/geometry.hpp draws.
//
// -----------------------------------------------------------------------------
// THE FRAME: ONE REPRESENTATION FOR EVERY ROTATED, NON-UNIFORMLY SCALED FORM
// -----------------------------------------------------------------------------
// A shape is stored as a CENTRE plus TWO AXIS VECTORS rather than as an
// axis-aligned box plus an angle, because that makes non-uniform scaling and
// flipped (negative) drags exact rather than approximate:
//
//     p(t) = centre + t.x * axisU + t.y * axisV          (t in the unit square)
//
// * axisU points from the left edge toward the right edge (half-extent vector).
// * axisV points from the top edge toward the bottom edge.
// * A BOX is t in [-1,1]^2 of that parallelogram; because axisU and axisV are
//   orthogonal for any shape this module creates, the "parallelogram" is a
//   rectangle, but a general affine transform (T05) can shear it and the
//   representation still describes it exactly.
// * A negative component of an axis is how a REVERSED DRAG is represented
//   (drag right-to-left / bottom-to-top). It must not be normalised away: the
//   acceptance criterion "reversed drag is correct" is exactly the statement
//   that the contour of a reversed drag equals the contour of the equivalent
//   forward drag, and that is asserted in t11_graphics_test.cpp.
// * Circle == Oval with |axisU| == |axisV|. Nothing else distinguishes them;
//   the preset id records which command the user picked so the editor can swap
//   Circle->Oval when a non-uniform drag would otherwise make a "circle" oval.
//
// -----------------------------------------------------------------------------
// HIT-TEST CONTRACT (normative - the renderer must agree with it)
// -----------------------------------------------------------------------------
// A click is a hit when:
//   * the appearance FILLS (Shaded/Solid/Shadowed)  -> the point is inside the
//     filled region (fillRule) OR within tol of the contour;
//   * the appearance does NOT fill (Hollow/Dashed)  -> the point is within tol
//     of the CONTOUR. The interior of a hollow shape is deliberately NOT a hit:
//     clicking through the middle of a hollow circle must reach whatever is
//     behind it. This is the difference the task asks to be made explicit.
//   * a Shadowed shape also hits inside its offset shadow shape.
//   * Line and Arc have no interior at all, in any appearance.
// tol is a DOCUMENT-space radius; the platform converts its pixel tolerance
// with core::ViewTransform::deviceRadiusToDocument (T01), as everywhere else.
// =============================================================================

#include "next/graphics/preset.hpp"

#include <vector>

namespace r62 {
namespace graphics {

// ---------------------------------------------------------------------------
// Frames
// ---------------------------------------------------------------------------
struct ShapeFrame {
    core::Vec2 centre;
    core::Vec2 axisU;      // half-extent along the shape's own +x
    core::Vec2 axisV;      // half-extent along the shape's own +y
    ShapeFrame() : centre(0, 0), axisU(14.4, 0), axisV(0, 14.4) {}
    ShapeFrame(const core::Vec2& c, const core::Vec2& u, const core::Vec2& v)
        : centre(c), axisU(u), axisV(v) {}
    // Point (u,v) in the unit square (-1..1) mapped into document space.
    core::Vec2 pointAt(double u, double v) const {
        return core::Vec2(centre.x + u * axisU.x + v * axisV.x,
                          centre.y + u * axisU.y + v * axisV.y);
    }
    // The four corners, clockwise starting top-left in the shape's own frame.
    void corners(core::Vec2 out[4]) const {
        out[0] = pointAt(-1.0, -1.0);
        out[1] = pointAt( 1.0, -1.0);
        out[2] = pointAt( 1.0,  1.0);
        out[3] = pointAt(-1.0,  1.0);
    }
    // Axis-aligned bounding box of the four corners (ignores any interior
    // bulge, which for these primitives is always inside the corners except for
    // a partial arc - see shapeBounds).
    core::Box aabb() const {
        core::Vec2 c[4];
        corners(c);
        core::Box b(c[0], c[0]);
        for (int i = 1; i < 4; ++i) b.add(c[i]);
        return b;
    }
    double halfWidth() const { return core::length(axisU); }
    double halfHeight() const { return core::length(axisV); }
    bool degenerate() const { return halfWidth() < 1e-9 || halfHeight() < 1e-9; }
};

// ---------------------------------------------------------------------------
// Shape parameters: what the document actually stores for one shape object
// ---------------------------------------------------------------------------
// This struct is the WHOLE content of a shape beyond its identity. It is what
// the extension bag persists, what the renderer consumes and what the hit test
// consumes, so render and hit test cannot disagree.
struct ShapeParams {
    uint32_t         presetId;
    ShapeForm        form;
    ShapeAppearance  appearance;
    LineVariant      line;          // meaningful for Line and Arc
    int              arcSweepDeg;   // 0 unless form == Arc
    ShapeFrame       frame;
    double           cornerFraction;// RoundRectangle: radius / min(halfExtent)
    double           arcStartDeg;   // Arc: start angle, core::direction() convention
    double           arcSpanDeg;    // Arc: signed sweep in degrees
    double           scale;         // user scale multiplier, 1.0 == preset default
    core::LineStyle  style;
    uint32_t         fillRGBA;
    ShapeParams()
        : presetId(kNoPreset), form(ShapeForm::Rectangle),
          appearance(ShapeAppearance::Hollow), line(LineVariant::Solid),
          arcSweepDeg(0), cornerFraction(0.22), arcStartDeg(0.0),
          arcSpanDeg(90.0), scale(1.0), fillRGBA(0x80808000u) {}
    bool hasInterior() const { return appearanceFills(appearance) &&
                                      form != ShapeForm::Line && form != ShapeForm::Arc; }
};

// ---------------------------------------------------------------------------
// Bracket parameters
// ---------------------------------------------------------------------------
struct BracketLabel {
    BracketLabelAnchor anchor;   // frame-parametric; survives any move/resize
    std::string        text;     // UTF-8; EMPTY means "no label attached"
    double             size;     // document units; 0 == document default
    BracketLabel() : size(0.0) {}
};

struct BracketParams {
    uint32_t          presetId;
    BracketFamily     family;
    BracketSide       side;
    BracketDirection  direction;
    ShapeFrame        frame;
    double            scale;
    core::LineStyle   style;
    // Labels are attached to the BRACKET object and positioned in frame
    // coordinates, so a drag or a resize moves them with the bracket by
    // construction. Chemistry business meaning (repeat counts, molecular
    // weight, containership) belongs to T13 and is NOT modelled here.
    std::vector<BracketLabel> labels;
    BracketParams()
        : presetId(kNoPreset), family(BracketFamily::Square),
          side(BracketSide::Left), direction(BracketDirection::Vertical),
          scale(1.0) {}
    bool paired() const { return side == BracketSide::Pair; }
};

// ---------------------------------------------------------------------------
// Stroke description passed to the hit test and to the bounds call
// ---------------------------------------------------------------------------
// The kernel does NOT read the document default line width; the caller resolves
// width 0 to the document default first (exactly as T03 requires).
struct StrokeSpec {
    double   width;        // document units, > 0
    bool     dashed;       // dash-aware hit testing is NOT implemented (v1)
    bool     wavy;
    double   wavyAmplitude;// document units
    StrokeSpec() : width(0.6), dashed(false), wavy(false), wavyAmplitude(0.0) {}
};

StrokeSpec strokeSpecFor(const ShapeParams& p, double documentLineWidth);
StrokeSpec strokeSpecFor(const BracketParams& p, double documentLineWidth);

// ---------------------------------------------------------------------------
// Geometry output
// ---------------------------------------------------------------------------
struct Edge {
    core::Vec2 a, b;
    Edge() {}
    Edge(const core::Vec2& a_, const core::Vec2& b_) : a(a_), b(b_) {}
};

// One stroke run. `closed` says whether the last point joins the first. A
// bracket PAIR is two runs (left and right), which is why this is a list and
// not one point array.
struct Contour {
    std::vector<core::Vec2> points;
    std::vector<uint8_t>    kind;     // render::PointKind values: 0 corner, 1 quad
    bool                    closed;
    Contour() : closed(false) {}
};

struct ShapeGeometry {
    ShapeParams      params;
    StrokeSpec       stroke;
    std::vector<Contour> fills;      // closed contours with fill enabled
    std::vector<Contour> strokes;    // open or closed outline runs
    std::vector<Contour> shadows;    // shadow copies (already offset)
    core::Box        fillBounds;     // geometry only, no stroke inflation
    core::Box        hitBounds;      // includes stroke, shadow, wave amplitude
    bool             hasFill;
    bool             hasShadow;
    ShapeGeometry() : hasFill(false), hasShadow(false) {}
};

struct BracketGeometry {
    BracketParams        params;
    StrokeSpec           stroke;
    std::vector<Contour> runs;       // one run per drawn bracket side
    std::vector<core::Vec2> labelPositions;  // absolute, parallel to labels[]
    core::Box            hitBounds;
    BracketGeometry() {}
};

// ---------------------------------------------------------------------------
// Parameter encoding (extension-bag payloads)
// ---------------------------------------------------------------------------
// A compact "k=v;k=v" text record, the same choice next/arrows_curves made and
// for the same reasons: it survives a text diff, it has no struct layout in the
// document, an unknown key is ignored on read, and a version suffix lets the
// encoding change without a format bump.
extern const char* const kShapeParamsKey;    // "gfx.shape.params.1"
extern const char* const kBracketParamsKey;  // "gfx.bracket.params.1"
extern const char* const kTableParamsKey;    // "gfx.table.params.1"
extern const char* const kEmbedKeyPrefix;    // "gfx.embed."

std::string encodeShapeParams(const ShapeParams& p);
// On a malformed record *ok is false and *out is left at its default; a corrupt
// document can therefore never produce a half-configured shape.
ShapeParams decodeShapeParams(const std::string& text, bool* ok);

std::string encodeBracketParams(const BracketParams& p);
BracketParams decodeBracketParams(const std::string& text, bool* ok);

// Stable content signature. Changes when, and only when, something that affects
// the drawn result changes. This is the value written to Primitive::version.
uint64_t shapeSignature(const ShapeParams& p, double documentLineWidth);
uint64_t bracketSignature(const BracketParams& p, double documentLineWidth);

// ---------------------------------------------------------------------------
// Geometry construction
// ---------------------------------------------------------------------------
// The ONLY place a preset becomes geometry. `spec` may be null for a legacy
// object whose preset is unknown; in that case the form/appearance already in
// `p` are used and no error is raised.
core::Error buildShapeGeometry(const ShapeParams& p, const StrokeSpec& stroke,
                               ShapeGeometry* out);
core::Error buildBracketGeometry(const BracketParams& p, const StrokeSpec& stroke,
                                 double labelTextSize, BracketGeometry* out);

// The closed contour of a shape in document space. Empty for Line and Arc.
std::vector<core::Vec2> shapeOutline(const ShapeParams& p);

// Absolute position of one attached label. Recomputed from the CURRENT frame,
// which is what keeps it in place across a drag/resize.
core::Vec2 bracketLabelPosition(const BracketParams& p, const BracketLabelAnchor& a);

// ---------------------------------------------------------------------------
// Hit testing (see the contract at the top of this file)
// ---------------------------------------------------------------------------
bool shapeHitTest(const ShapeParams& p, const core::Vec2& point, double tolerance);
bool bracketHitTest(const BracketParams& p, const core::Vec2& point, double tolerance,
                    double labelTextSize);

// Distance from a point to a geometry, exposed for tests and for the caller that
// wants its own band. Never negative.
core::Box shapeBounds(const ShapeParams& p, const StrokeSpec& stroke);
core::Box bracketBounds(const BracketParams& p, const StrokeSpec& stroke,
                        double labelTextSize);

// ---------------------------------------------------------------------------
// Small helpers shared with the table module and the tests
// ---------------------------------------------------------------------------
bool pointInPolygon(const std::vector<core::Vec2>& poly, const core::Vec2& p);
double distancePointSegment(const core::Vec2& p, const core::Vec2& a, const core::Vec2& b);
bool pointInBox(const core::Box& b, const core::Vec2& p);
core::Box inflateBox(const core::Box& b, double amount);
// The shadow offset used by ShapeAppearance::Shadowed. [PROJ] A fixed fraction
// of the shape size with a floor, so the shadow is visible on a small shape and
// does not become a billboard on a huge one.
core::Vec2 shadowOffsetFor(const ShapeFrame& frame);
double shadowBlurFor(const ShapeFrame& frame);
// Flatten a quadratic control-point chain (kind: 0 corner, 1 quad control) into
// a dense polyline. Used for curly brackets and for hit testing.
std::vector<core::Vec2> flattenQuadChain(const Contour& c, int segmentsPerCurve);

} // namespace graphics
} // namespace r62

#endif // R62_NEXT_GRAPHICS_GEOMETRY_HPP
