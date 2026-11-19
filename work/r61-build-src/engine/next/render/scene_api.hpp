#ifndef R62_NEXT_RENDER_SCENE_API_HPP
#define R62_NEXT_RENDER_SCENE_API_HPP
// =============================================================================
// re_round62_viewer/next/render/scene_api.hpp
//
// T03 - SHARED OpenGL VECTOR RENDER KERNEL.
//
// This is the FROZEN public API of the render module. Every module that draws
// (the editor shell, the tool previews, any future host overlay) goes through
// this header. It is an ADDITIVE layer on top of T01's frozen core contracts:
// it redefines NOTHING from r62::core. Vec2/Box/ViewTransform/LineStyle/
// PreviewScene/ObjectRef all come from next/core.
//
// -----------------------------------------------------------------------------
// WHAT THIS MODULE OWNS  (and what it deliberately does not)
// -----------------------------------------------------------------------------
//  OWNS : scene description -> batched GPU draw. Curves, fills with holes,
//         dashes, wavy lines, arrowheads, glyph meshes, clipping, selection /
//         hotspot / preview overlays, culling BVH, incremental caches, FBO/SSAA.
//  OWNS : world-space bounding boxes that INCLUDE stroke width, arrowheads,
//         glyph ink, shadows and drag handles, so T04/T05 hit tests can use
//         them without re-deriving visible extent.
//  NOT  : chemical semantics and geometry PARAMETERS (bond length, wedge angle,
//         ring size, preset shapes). Those belong to the business modules; a
//         business module publishes Primitive records and this module batches
//         them. arrowHeadGeometry() is a pure geometry utility, not a
//         chemistry decision.
//  NOT  : Win32 message routing (T04), persistence (T02), host assembly (T19).
//
// -----------------------------------------------------------------------------
// COORDINATE / UNIT CONTRACT  (inherited from next/core, never redefined)
// -----------------------------------------------------------------------------
//  * Scene geometry is DOCUMENT SPACE: x right, y DOWN, unit = CDXML point.
//  * device = (doc - center) * scale * dpiScale + viewportCenter  [core::ViewTransform]
//  * A Primitive never stores device pixels and never stores DPI.
//  * Angles are degrees, CCW in math orientation -> core::direction().
//
// -----------------------------------------------------------------------------
// WHY A PRIMITIVE LIST INSTEAD OF "LET EACH MODULE CALL GL"
// -----------------------------------------------------------------------------
//  The legacy editor accumulated per-object world meshes in one process-wide
//  state and rebuilt them on every view change. This API separates the two
//  invalidations that have different costs:
//
//    * VIEW change  (pan / zoom / DPI / resize) -> re-project cached world
//      triangles. No curve flattening, no glyph sampling, no BVH rebuild.
//    * OBJECT change (geometry / style / text)  -> rebuild ONLY the dirty
//      primitive's world mesh. The rest of the cache is reused by identity.
//
//  Both are observable through RenderStats (cacheRebuilds, viewRebuilds,
//  glyphSamples, bvhRebuilds) so a claim of "no per-frame full rebuild" is a
//  measurement, not an assertion.
// =============================================================================

#include "next/core/contracts.hpp"
#include "next/core/tool_registry.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace r62 {
namespace render {

// The text measurer is DEFINED in geometry.hpp, which in turn includes this
// header for Primitive/Style. Including it here would be a cycle, so this header
// only forward-declares it; a caller that rasterizes text includes geometry.hpp.
namespace geom { class TextMeasurer; }

// ---------------------------------------------------------------------------
// Version / compatibility handshake
// ---------------------------------------------------------------------------
// Renderer API version is INDEPENDENT of core::kInterfaceVersion. A caller
// checks BOTH: core for the model vocabulary it compiles against, render for
// the drawing API. requestRenderCompatibility(major, minor) mirrors the core
// rule (major must match, minor may lag).
enum {
    kRenderMajor    = 1,
    kRenderMinor    = 0,
    kRenderRevision = 0,
    kRenderVersion  = kRenderMajor * 10000 + kRenderMinor * 100 + kRenderRevision
};

// The core interface version this header was compiled against. Recorded in the
// module manifest so a mismatch is detectable at integration time.
inline int compiledCoreInterfaceVersion() { return core::kInterfaceVersion; }

inline bool requestRenderCompatibility(int major, int minor) {
    return major == kRenderMajor && minor <= kRenderMinor;
}

// ---------------------------------------------------------------------------
// Style: the drawable appearance vocabulary
// ---------------------------------------------------------------------------
// This is a RENDER-SIDE style. It is intentionally a superset of core::LineStyle
// (which is the persisted document style) because the renderer also needs things
// the document does not store: overlay colours, alpha, selection tint, shadow.
// Conversion from core::LineStyle is a pure function (styleFromLineStyle).
enum class JoinStyle : uint8_t { Miter = 0, Bevel = 1, Round = 2 };
enum class CapStyle : uint8_t { Butt = 0, Square = 1, Round = 2 };
enum class FillRule : uint8_t { NonZero = 0, EvenOdd = 1 };

// Resolved paint. Colour is 0xRRGGBBAA everywhere in this module so that alpha
// is expressible without a second field; the GL layer uses standard src-alpha
// blending and does not premultiply.
//
// OPACITY RULE (load-bearing). core::LineStyle/TextFormat default to
// 0xFF000000u, which under 0xRRGGBBAA is alpha==0 - i.e. fully transparent. A
// renderer that took that literally would draw every default-styled object
// invisibly, which is the worst kind of failure: geometry is present, pixels are
// not, and nothing reports an error. This module therefore resolves a
// zero-alpha colour to OPAQUE at paint-application time (see resolvePaint()),
// and alpha is honoured only when it is non-zero. An author who wants
// translucency writes a non-zero alpha, so no real capability is lost.
struct Paint {
    uint32_t rgba;
    bool     enabled;
    Paint() : rgba(0xFF000000u), enabled(true) {}
    Paint(uint32_t c, bool e = true) : rgba(c), enabled(e) {}
};

inline uint8_t colorRed(uint32_t c)   { return static_cast<uint8_t>((c >> 24) & 0xFFu); }
inline uint8_t colorGreen(uint32_t c) { return static_cast<uint8_t>((c >> 16) & 0xFFu); }
inline uint8_t colorBlue(uint32_t c)  { return static_cast<uint8_t>((c >> 8) & 0xFFu); }
inline uint8_t colorAlpha(uint32_t c) { return static_cast<uint8_t>(c & 0xFFu); }
inline uint32_t makeColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 0xFFu) {
    return (static_cast<uint32_t>(r) << 24) | (static_cast<uint32_t>(g) << 16) |
           (static_cast<uint32_t>(b) << 8) | static_cast<uint32_t>(a);
}

// Turn a stored colour into a drawable one. A zero alpha becomes 0xFF so that a
// colour produced by a default-constructed core style is visible. Every backend
// and the rasterizer call this, so the rule is applied exactly once per colour and
// the two paths cannot disagree.
inline uint32_t resolvePaint(uint32_t rgba) {
    return (rgba & 0xFFu) == 0u ? (rgba | 0xFFu) : rgba;
}

// Dash pattern in DOCUMENT units. An all-zero pattern means "solid". The pattern
// is scaled by the stroke width at flatten time (proportional dashes), which is
// why the pattern is stored relative to the width.
struct DashPattern {
    double on;
    double off;
    DashPattern() : on(0.0), off(0.0) {}
    DashPattern(double on_, double off_) : on(on_), off(off_) {}
    bool solid() const { return on <= 0.0 || off <= 0.0; }
};

// Full render style for one primitive.
struct Style {
    Paint       stroke;
    Paint       fill;
    double      strokeWidth;   // DOCUMENT units; 0 == use view default
    DashPattern dash;
    CapStyle    cap;
    JoinStyle   join;
    FillRule    fillRule;
    double      wavyAmplitude; // DOCUMENT units; >0 turns the stroke into a wave
    double      wavyWavelength;// DOCUMENT units; 0 == derive from amplitude
    double      miterLimit;
    Style()
        : strokeWidth(0.0), cap(CapStyle::Butt), join(JoinStyle::Miter),
          fillRule(FillRule::NonZero), wavyAmplitude(0.0), wavyWavelength(0.0),
          miterLimit(4.0) {}
    bool hasFill() const { return fill.enabled; }
};

// Bridge from the persisted document style. core::DashStyle is a small enum;
// the mapping is total and does not invent a pattern for Solid.
Style styleFromLineStyle(const core::LineStyle& ls, double viewDefaultWidth);
core::DashStyle dashStyleOf(const Style& style);

// ---------------------------------------------------------------------------
// Layer: draw order is an explicit, bounded, ordered list
// ---------------------------------------------------------------------------
// A layer is a named band with a z-order. Primitives carry a layer id; the
// backend sorts by (layer.z, primitive.z, stable insertion index). This is how
// stacking order is honoured without the caller having to submit objects in
// the right order.
struct LayerId {
    uint32_t value;
    LayerId() : value(0) {}
    explicit LayerId(uint32_t v) : value(v) {}
    bool operator==(const LayerId& o) const { return value == o.value; }
    bool operator!=(const LayerId& o) const { return value != o.value; }
    bool operator<(const LayerId& o) const { return value < o.value; }
};

// Reserved layer bands. User content lives in kContent..kContentTop; overlays
// are ALWAYS above it so an overlay can never be painted under a user object,
// and a user object can never be erased by an overlay clear.
namespace layers {
enum Reserved : uint32_t {
    kBackground   = 0,     // page / canvas fill
    kShadows      = 1,     // drop shadows of content objects
    kContent      = 100,   // user document objects (atoms, bonds, text, ...)
    kContentTop   = 900,   // user objects that must sit above the rest
    kHighlight    = 1000,  // hover hotspot
    kSelection    = 1100,  // selection marks and handles
    kPreview      = 1200,  // tool gesture preview
    kGuide        = 1300,  // marquee / guides / ruler overlays
    kTop          = 1400
};
} // namespace layers

struct LayerDesc {
    LayerId     id;
    int32_t     z;
    std::string name;
    LayerDesc() : z(0) {}
    LayerDesc(uint32_t id_, int32_t z_, const std::string& n)
        : id(id_), z(z_), name(n) {}
};

// ---------------------------------------------------------------------------
// Primitive kinds
// ---------------------------------------------------------------------------
// One bounded enum covers lines, curves, fills, text and overlays. A primitive
// is a RECORD OF GEOMETRY, never an algorithm choice: "Arrowhead" says "draw an
// arrowhead here", it does not say which arrow preset the business module
// picked.
enum class PrimKind : uint8_t {
    Segment = 0,   // points[0] -> points[1]
    Polyline,      // open polyline, points[]
    Polygon,       // closed, fillable (holes via subpaths)
    Curve,         // quadratic/cubic chain, points[] + pointKind[]
    Text,          // position + utf8 + font size
    Arrowhead,     // tip + tail direction, sized by style
    Marker,        // small filled shape (handle, dot, square)
    Rect,          // axis-aligned, fillable
    Ellipse        // center + radii, fillable
};

const char* primKindName(PrimKind kind);

// How a Curve's points are interpreted. Corner == a straight vertex.
enum class PointKind : uint8_t { Corner = 0, QuadControl = 1, CubicControl = 2 };

// Arrow head shapes. Geometry only.
enum class ArrowShape : uint8_t {
    None = 0,
    Triangle,        // filled solid triangle
    OpenTriangle,    // two strokes, no fill
    Barbed,          // concave filled head (stealth)
    Line,            // plain line end marker
    HalfBarb         // single-sided barb
};

// Marker shapes for handles and hotspot dots.
enum class MarkerShape : uint8_t {
    Circle = 0, Square, Diamond, Cross, Ring, HandleSquare, HandleCircle
};

// A subpath: an ordered point list plus its closed flag. A Polygon primitive
// with several subpaths expresses FILLS WITH HOLES: outer contour first, holes
// after, combined with the primitive's fillRule. This is the module's answer to
// holes - glyphs like "O" and "B", or a ring drawn as an annulus.
struct Subpath {
    std::vector<core::Vec2> points;
    std::vector<uint8_t>    kind;   // PointKind; empty == all Corner
    bool                    closed;
    Subpath() : closed(false) {}
};

// ---------------------------------------------------------------------------
// Primitive
// ---------------------------------------------------------------------------
// Every primitive carries:
//   * source       - the document ObjectRef it came from (invalid for overlays)
//   * bounds       - conservative VISIBLE bounds (see bounds.hpp rules)
//   * style / layer / z - appearance and stacking
// The bounds field is authoritative for culling and for T04/T05 hit tests.
// computeBounds() is the function that fills it.
struct Primitive {
    PrimKind     kind;
    LayerId      layer;
    int32_t      z;             // secondary order inside a layer
    core::ObjectRef source;     // document object this came from (may be invalid)

    std::vector<core::Vec2> points;    // Segment/Polyline/Rect/Ellipse/Arrowhead
    std::vector<uint8_t>    pointKind; // Curve only, PointKind per point
    std::vector<Subpath>    subpaths;  // Polygon (holes)

    // Text
    std::string text;           // UTF-8
    double      textSize;       // DOCUMENT units
    std::string fontName;       // empty == document default

    // Arrowhead / Marker
    ArrowShape  arrow;
    MarkerShape marker;
    double      size;           // head length / marker diameter, DOCUMENT units

    Style       style;
    core::Box   bounds;         // world bounds INCLUDING width/heads/glyph ink

    // Monotonic identity used by the incremental cache. When a business module
    // rebuilds a primitive it changes version (typically the document revision
    // of the touched object). The cache compares (source, version) plus a
    // content signature and rebuilds only on change.
    uint64_t    version;
    uint64_t    cacheKey;       // stable per-object/sub-primitive cache identity

    Primitive()
        : kind(PrimKind::Polyline), z(0), textSize(0.0),
          arrow(ArrowShape::None), marker(MarkerShape::Circle), size(0.0),
          version(0), cacheKey(0) {}
};

// ---------------------------------------------------------------------------
// Scene
// ---------------------------------------------------------------------------
// A Scene is a plain, copyable description. It does not own GL objects. The
// backend consumes it. Building a Scene is cheap and is expected to happen from
// a document diff, not by re-walking 50k objects every frame.
class Scene {
public:
    Scene() : generation_(0) {}

    void clear() { prims_.clear(); layers_.clear(); ++generation_; }
    uint64_t generation() const { return generation_; }

    LayerId addLayer(const std::string& name, int32_t z) {
        LayerId id(static_cast<uint32_t>(layers_.size()));
        layers_.push_back(LayerDesc(id.value, z, name));
        ++generation_;
        return id;
    }
    const std::vector<LayerDesc>& layers() const { return layers_; }

    Primitive& add(const Primitive& p) {
        prims_.push_back(p);
        ++generation_;
        return prims_.back();
    }
    void reserve(size_t n) { prims_.reserve(n); }
    std::vector<Primitive>& mutablePrimitives() { return prims_; }
    const std::vector<Primitive>& primitives() const { return prims_; }
    size_t size() const { return prims_.size(); }
    bool empty() const { return prims_.empty(); }
    // Bump generation after in-place mutation through mutablePrimitives().
    void touch() { ++generation_; }

    // World bounds of every primitive.
    core::Box bounds() const;

private:
    std::vector<Primitive>   prims_;
    std::vector<LayerDesc>   layers_;
    uint64_t                 generation_;
};

// ---------------------------------------------------------------------------
// Overlay: selection / hotspot / preview, kept SEPARATE from content
// ---------------------------------------------------------------------------
// The task requires that an overlay must not clear or corrupt user objects.
// Structurally: overlays live in their own Scene, and OverlayScene only accepts
// primitives whose layer is in the overlay band. The backend draws content
// first and overlays second; clearing the overlay touches only overlay meshes.
class OverlayScene {
public:
    OverlayScene() : generation_(0) {}
    void clear() { scene_.clear(); ++generation_; }
    uint64_t generation() const { return generation_; }
    // Rejects a primitive whose layer is below kHighlight. Returns false and
    // leaves the overlay unchanged; this is the enforcement of "no overlay can
    // silently replace content".
    bool add(const Primitive& p);
    Scene& scene() { return scene_; }
    const Scene& scene() const { return scene_; }
    static bool isOverlayLayer(LayerId id) {
        return id.value >= static_cast<uint32_t>(layers::kHighlight);
    }
private:
    Scene    scene_;
    uint64_t generation_;
};

// ---------------------------------------------------------------------------
// View: everything the projection depends on
// ---------------------------------------------------------------------------
// dpiScale and viewport are part of the VIEW, never of the scene. Changing any
// field here is a view change: cached world meshes stay valid.
struct View {
    core::ViewTransform transform;
    double              widthPx;    // device pixels
    double              heightPx;   // device pixels
    double              defaultStrokeWidth; // document units for width==0
    double              defaultTextSize;    // document units for size==0
    core::Vec2          clipMin, clipMax;   // device-space scissor; equal == no clip
    bool                clipEnabled;
    View()
        : widthPx(0), heightPx(0), defaultStrokeWidth(0.6), defaultTextSize(10.0),
          clipEnabled(false) {}
    bool viewportEquals(const View& o) const {
        return widthPx == o.widthPx && heightPx == o.heightPx &&
               transform.center == o.transform.center &&
               transform.scale == o.transform.scale &&
               transform.dpiScale == o.transform.dpiScale &&
               transform.viewportCenter == o.transform.viewportCenter &&
               clipEnabled == o.clipEnabled && clipMin == o.clipMin && clipMax == o.clipMax;
    }
};

// ---------------------------------------------------------------------------
// Stats: every performance claim must be observable
// ---------------------------------------------------------------------------
struct RenderStats {
    size_t primitivesTotal;     // primitives submitted this frame
    size_t primitivesCulled;    // rejected by the culling BVH / viewport test
    size_t primitivesDrawn;     // actually submitted to GL
    size_t meshRebuilds;        // world meshes regenerated (object changes)
    size_t viewRebuilds;        // vertices re-projected (view changes)
    size_t glyphSamples;        // glyphs rasterised from the font (cache misses)
    size_t glyphReuses;         // glyphs served from cache
    size_t bvhRebuilds;         // spatial index rebuilds
    size_t drawCalls;           // GL draw calls issued
    size_t triangles;           // triangles submitted
    size_t lines;               // line segments submitted
    size_t textureUploads;      // FBO/texture respecifications
    uint64_t buildMicros;       // scene -> mesh time
    uint64_t drawMicros;        // GL submission time
    RenderStats() { reset(); }
    void reset() {
        primitivesTotal = primitivesCulled = primitivesDrawn = 0;
        meshRebuilds = viewRebuilds = glyphSamples = glyphReuses = 0;
        bvhRebuilds = drawCalls = triangles = lines = textureUploads = 0;
        buildMicros = drawMicros = 0;
    }
};

// ---------------------------------------------------------------------------
// Culling BVH interface (implemented by spatial BVH, injectable for tests)
// ---------------------------------------------------------------------------
// The renderer must not force T04's hit-test structure and T04 must not force
// the renderer's. This interface is the seam: the backend queries it for the
// visible set; the same object can serve both by implementing both interfaces.
class CullIndex {
public:
    virtual ~CullIndex() {}
    virtual void rebuild(const std::vector<core::Box>& boxes) = 0;
    virtual void query(const core::Box& viewport, std::vector<size_t>* out) const = 0;
    virtual size_t size() const = 0;
    virtual size_t nodeCount() const = 0;
};

// ---------------------------------------------------------------------------
// Backend: the only place OpenGL is called
// ---------------------------------------------------------------------------
// A backend instance owns GL resources (buffers, FBOs, glyph atlas). It must be
// created and destroyed on the thread that owns the GL context. destroy() is
// idempotent and is what guarantees "no leak after resize / DPI / focus loss /
// close": the platform calls it (or lets the destructor run) while the context
// is still current.
class Backend {
public:
    virtual ~Backend() {}

    virtual const char* name() const = 0;
    // Returns false when no usable GL context is current. Never throws for a
    // missing context; the caller decides whether to fall back.
    virtual bool initialize(std::string* error) = 0;
    virtual bool ready() const = 0;

    // Object-change path: bring world meshes up to date with the scene. Only
    // dirty primitives are rebuilt. Cheap when nothing changed.
    virtual void sync(const Scene& scene, const View& view) = 0;
    // View-change path: re-project for a new View. No geometry regeneration.
    virtual void setView(const View& view) = 0;

    // Draw the given scenes. Order: content, then overlay.
    virtual core::Error draw(const Scene& content, const Scene* overlay,
                             const View& view, RenderStats* stats) = 0;

    // Offscreen readback (used by the acceptance tests). Reads the CURRENT
    // framebuffer region in device pixels, RGBA8, bottom-up.
    virtual core::Error readPixels(int x, int y, int w, int h,
                                   std::vector<uint8_t>* rgba) const = 0;

    virtual void destroy() = 0;
};

// Backend factory. Declared here so a test can link the CPU half of the module
// without linking OpenGL.
Backend* createGLBackend();
// Null backend: same batching/culling/stat accounting, no GL calls. Used by
// headless tests to verify the culling and cache logic without a context.
Backend* createNullBackend();

// ---------------------------------------------------------------------------
// Software rasterizer (headless geometry evidence)
// ---------------------------------------------------------------------------
// The acceptance criteria demand offscreen images and geometric bounds proof.
// A GPU is not always present on a build machine, and a Null backend produces no
// image. This rasterizer consumes the SAME Primitive records (not the GL mesh)
// and is therefore real evidence that the geometry is correct, independent of
// any driver. It is deliberately simple (scanline, SSAA) and is NOT the
// production path.
struct RasterOptions {
    int    width;
    int    height;
    int    supersample;      // 1 or 2 (2x SSAA matches the GL path)
    uint32_t background;     // RGBA
    RasterOptions() : width(640), height(480), supersample(2), background(0xFFFFFFFFu) {}
};

// Render content + optional overlay into an RGBA8 buffer, bottom-up rows.
//
// `measurer` is optional and is the SAME seam the GL backend uses: pass the real
// outline-backed measurer to draw real glyph geometry, or null to draw text only
// from the ink box already recorded in the primitive. Either way the rasterizer
// itself links no font library.
core::Error rasterize(const Scene& content, const Scene* overlay,
                      const View& view, const RasterOptions& options,
                      std::vector<uint8_t>* rgba);

// Overload with an injected text measurer. Declared as a distinct name so the
// geometry kernel header stays the single declaration site for TextMeasurer.
core::Error rasterizeWithText(const Scene& content, const Scene* overlay,
                              const View& view, const RasterOptions& options,
                              geom::TextMeasurer* measurer,
                              std::vector<uint8_t>* rgba);

// ---------------------------------------------------------------------------
// PNG writer (evidence output; no third-party dependency)
// ---------------------------------------------------------------------------
core::Error writePng(const std::string& path, int width, int height,
                     const std::vector<uint8_t>& rgbaBottomUp);

} // namespace render
} // namespace r62

#endif // R62_NEXT_RENDER_SCENE_API_HPP
