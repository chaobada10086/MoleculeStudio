#ifndef R62_NEXT_RENDER_GEOMETRY_HPP
#define R62_NEXT_RENDER_GEOMETRY_HPP
// =============================================================================
// re_round62_viewer/next/render/geometry.hpp
//
// T03 - CPU geometry kernel. NO OpenGL, NO Win32, NO font. Pure functions over
// core::Vec2 so every rule below is testable headlessly and byte-comparable
// across machines.
//
// Everything here works in DOCUMENT SPACE unless a name says otherwise.
// =============================================================================

#include "next/render/scene_api.hpp"

#include <vector>

namespace r62 {
namespace render {
namespace geom {

// ---------------------------------------------------------------------------
// Triangle accumulation
// ---------------------------------------------------------------------------
// The backend and the software rasterizer consume the SAME output, so a
// geometry bug cannot be hidden by one path drawing differently from the other.
struct Mesh {
    std::vector<float> triangles;   // x,y pairs, 3 vertices per triangle
    std::vector<float> lines;       // x,y pairs, 2 vertices per segment (limits)
    core::Box          bounds;
    bool               hasBounds;
    Mesh() : hasBounds(false) {}
    void clear() { triangles.clear(); lines.clear(); hasBounds = false; }
    void pushVertex(const core::Vec2& p) {
        triangles.push_back(static_cast<float>(p.x));
        triangles.push_back(static_cast<float>(p.y));
        if (!hasBounds) { bounds = core::Box(p, p); hasBounds = true; }
        else bounds.add(p);
    }
    void pushTriangle(const core::Vec2& a, const core::Vec2& b, const core::Vec2& c) {
        pushVertex(a); pushVertex(b); pushVertex(c);
    }
    void pushLine(const core::Vec2& a, const core::Vec2& b) {
        lines.push_back(static_cast<float>(a.x)); lines.push_back(static_cast<float>(a.y));
        lines.push_back(static_cast<float>(b.x)); lines.push_back(static_cast<float>(b.y));
    }
    size_t triangleCount() const { return triangles.size() / 6; }
    size_t lineCount() const { return lines.size() / 4; }
};

// ---------------------------------------------------------------------------
// Curve flattening
// ---------------------------------------------------------------------------
// A tessellation tolerance in DOCUMENT units. The caller derives it from the
// view (one device pixel worth of document distance) so the SAME flattened
// polyline can be re-projected at any zoom without visible faceting, and so a
// zoom-in produces MORE segments from the same control points rather than a
// scaled-up coarse polyline.
struct FlattenOptions {
    double tolerance;      // document units; <=0 selects a safe default
    int    maxSegments;    // hard cap per curve span (runaway guard)
    FlattenOptions() : tolerance(0.0), maxSegments(256) {}
};

// Flatten one primitive into a world polyline (points + closed flag).
// Segment/Polyline/Polygon/Curve/Rect/Ellipse are all handled; Text/Arrowhead/
// Marker return their own geometry via their dedicated builders. Returns false
// for kinds that are not path-like or for degenerate input.
bool flattenPath(const Primitive& prim, const FlattenOptions& options,
                 std::vector<core::Vec2>* out, bool* closed);

// Quadratic/cubic subdivision count for a tolerance. Exposed so a test can
// assert that halving the tolerance increases the count monotonically.
int quadSegments(const core::Vec2& p0, const core::Vec2& c, const core::Vec2& p1,
                 double tolerance, int maxSegments);
int cubicSegments(const core::Vec2& p0, const core::Vec2& c0, const core::Vec2& c1,
                  const core::Vec2& p1, double tolerance, int maxSegments);

// ---------------------------------------------------------------------------
// Dash / wave
// ---------------------------------------------------------------------------
// Split a polyline into dash sub-polylines. The pattern is scaled by width so a
// thicker line gets proportionally longer dashes - the behaviour a vector
// drawing app is expected to show, and the reason a naive fixed-pattern dash
// looks wrong at high zoom.
void applyDash(const std::vector<core::Vec2>& path, bool closed,
               const DashPattern& pattern, double width,
               std::vector<std::vector<core::Vec2> >* out);

// Replace a straight span with a sine wave perpendicular to it. Amplitude and
// wavelength are document units; wavelength<=0 derives 4*amplitude.
void applyWave(const std::vector<core::Vec2>& path, bool closed,
               double amplitude, double wavelength, double tolerance,
               std::vector<core::Vec2>* out);

// ---------------------------------------------------------------------------
// Stroke -> triangles
// ---------------------------------------------------------------------------
// Expand a polyline into a triangle strip mesh. This is why the renderer never
// needs glLineWidth (which is 1.0-only in core profile and driver-dependent in
// compatibility profile) and never needs glBegin per segment: a 5000-atom
// molecule becomes a handful of batched triangle arrays.
struct StrokeOptions {
    double      width;      // document units, > 0
    CapStyle    cap;
    JoinStyle   join;
    double      miterLimit;
    bool        closed;
    StrokeOptions() : width(0.6), cap(CapStyle::Butt), join(JoinStyle::Miter),
                      miterLimit(4.0), closed(false) {}
};

void strokePolyline(const std::vector<core::Vec2>& path, const StrokeOptions& options,
                    Mesh* mesh, size_t* segmentCount);

// ---------------------------------------------------------------------------
// Fill with holes
// ---------------------------------------------------------------------------
// Triangulate a set of closed contours honouring the winding rule. Contour 0 is
// the outer boundary; later contours are holes when they are inside it.
// Implemented with an ear-clipping sweep over a hole-bridged polygon so no
// external tessellator (GLU) is required at run time - this is what lets the
// headless rasterizer produce the same image as the GPU path.
core::Error triangulateContours(const std::vector<std::vector<core::Vec2> >& contours,
                                FillRule rule, Mesh* mesh);

// Even/odd point-in-polygon over several contours (used by the rasterizer and by
// hit tests that must agree with the fill).
bool pointInContours(const std::vector<std::vector<core::Vec2> >& contours,
                     FillRule rule, const core::Vec2& p);

// ---------------------------------------------------------------------------
// Arrow heads (geometry only - the preset choice is the business module's)
// ---------------------------------------------------------------------------
struct ArrowHeadGeometry {
    std::vector<core::Vec2> outline;   // closed polygon for a filled head
    std::vector<core::Vec2> strokes;   // segment pairs for an open/line head
    bool                    filled;
    ArrowHeadGeometry() : filled(false) {}
};

// Build a head at `tip`, pointing along (tip - tail) normalised, with the head
// length given by `length`. `width` is the half-width factor of the head.
ArrowHeadGeometry arrowHeadGeometry(const core::Vec2& tail, const core::Vec2& tip,
                                    ArrowShape shape, double length, double widthFactor);

// Marker geometry (handles, hotspot dots).
void markerGeometry(const core::Vec2& center, MarkerShape shape, double size,
                    Mesh* mesh, bool* hasOutline, std::vector<core::Vec2>* outline);

// ---------------------------------------------------------------------------
// Bounds
// ---------------------------------------------------------------------------
// The bounding box rule is part of the contract: it must be the CONSERVATIVE
// VISIBLE extent, not the geometric extent. A caller that hit-tests against it
// must never miss because a 3pt-wide stroke or a 2pt arrowhead stuck out.
//
//   geometry bounds
//     + half the stroke width on every side (round cap/join safe)
//     + arrowhead extent for Arrowhead primitives
//     + glyph ink box for Text (supplied by the caller's measured text)
//     + marker radius for Marker primitives
//     + an explicit padding term for shadows and drag handles
core::Box computeBounds(const Primitive& prim, double viewDefaultStrokeWidth);
// Padding added for a drop shadow at a given offset/blur, and for handles.
core::Box inflateForShadow(const core::Box& box, double dx, double dy, double blur);
core::Box inflateForHandles(const core::Box& box, double handleSize);

// ---------------------------------------------------------------------------
// Intersection helpers used by T04/T05 through the same rules
// ---------------------------------------------------------------------------
bool boxOverlaps(const core::Box& a, const core::Box& b);
bool segmentIntersectsBox(const core::Vec2& a, const core::Vec2& b, const core::Box& box);
double distancePointSegment(const core::Vec2& p, const core::Vec2& a, const core::Vec2& b);

// ---------------------------------------------------------------------------
// Text metrics (font-independent seam)
// ---------------------------------------------------------------------------
// The geometry kernel must not link GDI, because the headless tests run without
// a window station guarantee and the CPU half must stay portable. A text
// measurer is INJECTED: the platform module supplies a GDI-outline-backed one,
// tests supply an exact synthetic one.
struct GlyphMetrics {
    std::vector<float> triangles;  // glyph-local, baseline origin, +Y up
    double advance;
    double x0, y0, x1, y1;         // ink box in glyph units (em = 1.0)
    bool   missing;
    GlyphMetrics() : advance(0.0), x0(0), y0(0), x1(0), y1(0), missing(false) {}
};

class TextMeasurer {
public:
    virtual ~TextMeasurer() {}
    // Glyph for one code point at the default font. Returns false when the
    // measurer has no glyph (the caller substitutes a box).
    virtual bool glyph(uint32_t codepoint, const std::string& fontName,
                       GlyphMetrics* out) = 0;
    // Cached-glyph counters so the renderer's glyphSamples stat is real.
    virtual size_t sampleCount() const = 0;
    virtual size_t reuseCount() const = 0;
};

// Lay out a single line of UTF-8 text into world triangles.
// originY is the BASELINE. +Y in glyph space is up, document +Y is down, so the
// builder flips y exactly once, here, and nowhere else.
core::Error layoutText(const std::string& utf8, const core::Vec2& baseline,
                       double emSize, const std::string& fontName,
                       TextMeasurer* measurer, Mesh* mesh, core::Box* inkBox,
                       double* advance);

// Decode UTF-8 into code points with the same strictness as core::validateUtf8.
core::Error decodeUtf8(const std::string& utf8, std::vector<uint32_t>* out);

} // namespace geom
} // namespace render
} // namespace r62

#endif // R62_NEXT_RENDER_GEOMETRY_HPP
