#ifndef R62_NEXT_TEXT_RICH_TEXT_HPP
#define R62_NEXT_TEXT_RICH_TEXT_HPP
// =============================================================================
// re_round62_viewer/next/text/rich_text.hpp
//
// T09 - RICH TEXT LAYOUT: runs, faces, sizes, super/subscript, formula style,
//       multi-line, alignment, justification, hit testing and caret geometry.
//
// WHAT THIS FILE IS AND IS NOT
// -----------------------------------------------------------------------------
// It IS the layout/geometry authority for every piece of text the editor draws:
// captions (free text) and atom labels go through the same engine, with
// different OPTIONS and different anchors.  Nothing downstream may re-implement
// line breaking or baseline placement, because two implementations of "where is
// the baseline" is how a caret drifts from the glyphs it edits.
//
// It is NOT a chemical semantics module.  r62::text::AtomLabelSpec is a
// LAYOUT INPUT: it says what to draw (element, isotope, charge, hydrogens,
// prefix/suffix tokens, free text).  Deciding that an atom IS N with charge +1
// is T06 job; this module only turns that decision into glyphs, baselines and
// ink bounds.  See atom_label.hpp for the parser and label_layout.cpp for the
// token placement.
//
// UNITS AND COORDINATES (inherited from next/core, never redefined)
// -----------------------------------------------------------------------------
//   * document space: x right, y DOWN, unit = CDXML point.
//   * an anchor is a document point; a baseline is a document point.
//   * em space (provider): y UP, baseline 0.  The ONE flip is in rich_text.cpp.
//   * no DPI and no device pixels appear in this header.  Zoom is a property of
//     the ViewTransform the caller applies at draw time; layout output is
//     resolution independent by construction.
//
// WHY LAYOUT OUTPUT IS CACHED, NOT RE-SAMPLED
// -----------------------------------------------------------------------------
// Glyph outlines are resolution independent, so a zoom or a pan changes only
// the ViewTransform: the caller re-runs transformed()/emitGeometry() against the
// SAME RichLayout, and GlyphCache::sampleCount() stays flat.  That is the
// measurable form of "dragging previews resample zero glyphs".
// =============================================================================

#include "next/core/contracts.hpp"
#include "next/text/glyph_source.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace r62 {
namespace text {

// Local point type: layout-space geometry.  Kept separate from core::Vec2 so
// that "layout units" and "document units" cannot be silently mixed; the public
// boundaries convert explicitly.
struct LPoint {
    double x, y;
    LPoint() : x(0), y(0) {}
    LPoint(double x_, double y_) : x(x_), y(y_) {}
};

// ---------------------------------------------------------------------------
// Decoration kinds.  There is deliberately no "strikethrough" flag in
// core::TextFlags: core owns what is PERSISTED, and this module owns what is
// DRAWN.  A decoration that the document cannot store is never invented here.
// ---------------------------------------------------------------------------
enum class Decoration : uint8_t { Underline = 0, Strike = 1 };

struct DecorationSpan {
    Decoration kind;
    double     x0, x1;      // layout space
    double     y;           // offset from the line baseline (positive = below)
    double     thickness;
};

// One positioned glyph.
struct GlyphPlacement {
    uint32_t    codepoint;
    FontSpec    font;
    double      size;        // EFFECTIVE size in document units (already scaled
                             // for super/subscript)
    double      x, y;        // layout-space pen origin on the owning baseline
    double      advance;
    uint16_t    runIndex;
    uint16_t    lineIndex;
    uint32_t    cpIndex;     // index of the code point inside run.text
    uint32_t    u16Index;    // index of the first UTF-16 unit inside run.utf16
    bool        missing;
    double      inkX0, inkY0, inkX1, inkY1;   // layout space, absolute
    // The glyph outline mapped into DOCUMENT space, as closed contours with the
    // font's winding (holes wind the other way).  This is what makes the merged
    // NonZero outline correct around "O"/"B"/"8" holes, and it is stored ONCE at
    // layout time so a frame emits triangles without touching the font.
    std::vector<std::vector<core::Vec2> > contours;
    // The triangulation of those contours (x,y pairs, three vertices per
    // triangle, consistent winding), also document space and also computed once.
    std::vector<float> triangles;
    core::Box    inkBox;      // == the four ink fields, as a box
    GlyphPlacement()
        : codepoint(0), size(0.0), x(0), y(0), advance(0.0), runIndex(0),
          lineIndex(0), cpIndex(0), u16Index(0), missing(false),
          inkX0(0), inkY0(0), inkX1(0), inkY1(0) {}
};

// One run as laid out on one line.  A source run that wraps produces one
// LineRun PER LINE, so a caller that wants "the run" must go back through
// runIndex.
struct LineRun {
    uint16_t runIndex;
    uint32_t cpBegin, cpEnd;      // code point range inside run.text
    uint32_t u16Begin, u16End;    // UTF-16 range inside run.utf16
    double   x;                   // left edge on this line
    double   advance;             // width contributed (excluding justification)
    double   size;                // effective size of the run on this line
    double   ascent, descent;     // effective line metrics for this run
    double   baselineShift;       // super/subscript shift (positive = raised)
    uint16_t flags;
    bool     hasSoftHyphenBreak;
    LineRun()
        : runIndex(0), cpBegin(0), cpEnd(0), u16Begin(0), u16End(0), x(0),
          advance(0.0), size(0.0), ascent(0), descent(0), baselineShift(0.0),
          flags(0), hasSoftHyphenBreak(false) {}
};

struct LineLayout {
    uint32_t cpBegin, cpEnd;      // whole-line code point range (source order)
    uint32_t u16Begin, u16End;
    double   x;                   // left edge of the laid-out line
    double   width;               // natural width (before justification)
    double   baseline;            // y of the line baseline (layout space)
    double   top, bottom;         // line box in layout space
    double   ascent, descent;
    bool     hardBreak;           // ended because of a hard break or end of text
    bool     justified;
    std::vector<LineRun> runs;
    LineLayout()
        : cpBegin(0), cpEnd(0), u16Begin(0), u16End(0), x(0), width(0),
          baseline(0), top(0), bottom(0), ascent(0), descent(0),
          hardBreak(false), justified(false) {}
};

// The measurement of the whole block.
struct LayoutMetrics {
    core::Box inkBox;        // union of line boxes (document space)
    core::Box layoutBox;     // same, in layout space
    double   width, height;  // of the line boxes
    double   firstBaseline;
    double   lastBaseline;
    double   ascent, descent;  // overshoot above/below the outer baselines
    size_t   lineCount, glyphCount, missingGlyphs, decorationCount;
    size_t   triangulatedGlyphs;   // glyphs whose outline became triangles
    size_t   windingFlips;         // triangles whose winding had to be normalised
    bool     empty;
    LayoutMetrics()
        : width(0), height(0), firstBaseline(0), lastBaseline(0),
          ascent(0), descent(0), lineCount(0), glyphCount(0), missingGlyphs(0),
          decorationCount(0), triangulatedGlyphs(0), windingFlips(0),
          empty(true) {}
};

struct RichLayout {
    std::vector<GlyphPlacement> glyphs;
    std::vector<LineLayout>     lines;
    std::vector<DecorationSpan> decorations;
    LayoutMetrics               metrics;
    core::Vec2                  anchor;   // as passed in
    // Diagnostic: the identity of every family the layout touched, in first-use
    // order.  Used by evidence output and by "unknown font" reporting.
    std::vector<std::string>    familiesUsed;
};

// ---------------------------------------------------------------------------
// Anchors
// ---------------------------------------------------------------------------
// A caption and an atom label are anchored differently, and this enum is the
// only place that difference is allowed to live:
//   * CaptionTopLeft  - free text: the anchor is the top-left of the line box.
//   * CaptionCenter   - the anchor is the centre of the whole block.
//   * AtomCenter      - the anchor is the atom position and the FIRST LINE ink
//                       box is placed so that its optical centre sits on it.
//                       This is the centred atom label (ChemDraw default).
//   * AtomLeft/AtomRight - flush-left / flush-right atom labels: the first
//                       token edge touches the atom (IUPAC flush-right note in
//                       the user guide: the bond attaches to the rightmost atom
//                       of the label).
//   * AtomAbove       - "StackedAbove": the label sits entirely above the atom.
//   * AtomAutomatic   - token-splitting automatic alignment (atom_label.hpp).
enum class LayoutAnchor : uint8_t {
    CaptionTopLeft = 0,
    CaptionCenter,
    AtomCenter,
    AtomLeft,
    AtomRight,
    AtomAbove,
    AtomAutomatic
};

// ---------------------------------------------------------------------------
// Line spacing (the three documented ChemDraw modes)
// ---------------------------------------------------------------------------
//   Automatic - consistently spaced lines based on the height of the tallest
//               character in the caption (uniform line height).
//   Variable  - lines spaced by the tallest character and the lowest descender
//               in each line (per-line height).
//   Fixed     - a spacing the caller specifies.
enum class LineSpacing : uint8_t {
    Automatic = 0,
    Variable = 1,
    Fixed = 2
};

// ---------------------------------------------------------------------------
// Layout options
// ---------------------------------------------------------------------------
struct LayoutOptions {
    double      defaultSize;    // document units; used by runs with size == 0
    std::string defaultFont;    // used by runs with an empty fontName
    uint16_t    defaultFlags;   // used when a run carries no flags of its own

    LayoutAnchor anchor;
    core::TextAlign align;
    LineSpacing  spacing;
    double       fixedLineHeight;   // document units, LineSpacing::Fixed only
    double       lineHeightFactor;  // multiplies the automatic line height
    double       widthLimit;        // > 0 enables wrapping at that width
    double       inlinePadding;     // extra per-run horizontal padding

    double       scriptScale;       // super/subscript size factor (0.75)
    double       subBaselineShift;  // fraction of size, positive = lower (0.18)
    double       superBaselineShift;// fraction of size, positive = raise (0.36)
    double       isotopeScale;      // 0.5
    double       chargeScale;       // 0.5
    double       isotopeRaise;      // fraction of size (0.42)
    double       chargeRaise;       // fraction of size (0.36)
    double       countDrop;         // subscript number drop, fraction (0.18)

    bool triangulateGlyphs = true; // contour-only consumers avoid duplicate tessellation
    bool  applyKerning;             // bounded pair kerning
    double maxKernFraction;         // hard cap as a fraction of em

    LayoutOptions()
        : defaultSize(10.0), defaultFlags(0), anchor(LayoutAnchor::CaptionTopLeft),
          align(core::TextAlign::Left), spacing(LineSpacing::Automatic),
          fixedLineHeight(0.0), lineHeightFactor(1.0), widthLimit(0.0),
          inlinePadding(0.0), scriptScale(0.75), subBaselineShift(0.18),
          superBaselineShift(0.36), isotopeScale(0.5), chargeScale(0.5),
          isotopeRaise(0.42), chargeRaise(0.36), countDrop(0.18),
          applyKerning(true), maxKernFraction(0.05) {}
};

// ---------------------------------------------------------------------------
// The engine
// ---------------------------------------------------------------------------
// Lays out runs anchored at `anchor`.  Never throws for malformed input: an
// invalid run is rejected with core::Error and *out is left empty.  A provider
// that cannot map a code point yields a visible tofu glyph with missing=true,
// never a silent hole.
core::Error layoutRichText(const std::vector<core::TextRun>& runs,
                           const core::Vec2& anchor,
                           const LayoutOptions& options,
                           GlyphCache* cache,
                           RichLayout* out);

// Convenience: one run of plain text.
core::Error layoutPlainText(const std::string& utf8,
                            const core::Vec2& anchor,
                            const LayoutOptions& options,
                            GlyphCache* cache,
                            RichLayout* out);

// ---------------------------------------------------------------------------
// Geometry (defined in rich_geometry.cpp; it is the bridge to T03)
// ---------------------------------------------------------------------------
// The render bridge is declared in render_bridge.hpp so this header stays free
// of any dependency on next/render.  The helpers below are pure layout-space
// math and are what every bridge uses.
core::Box layoutBounds(const RichLayout& layout);
core::Box deviceBounds(const RichLayout& layout, const core::ViewTransform& view);

// ---------------------------------------------------------------------------
// Hit testing and caret geometry
// ---------------------------------------------------------------------------
// All result positions are LAYOUT space (add the anchor for document space).
struct TextHit {
    uint16_t lineIndex;
    uint16_t runIndex;
    uint32_t cpIndex;    // caret position, code point units
    uint32_t u16Index;   // caret position, UTF-16 units (the Windows caret unit)
    double   x, y;       // caret position in layout space
    double   distance;   // distance from the query point to the line box
    bool     insideLine; // query point was horizontally inside the line
    TextHit() : lineIndex(0), runIndex(0), cpIndex(0), u16Index(0), x(0), y(0),
                distance(0), insideLine(false) {}
};

// Query point in layout space.
TextHit hitTest(const RichLayout& layout, const LPoint& point);

// Caret at a (line, utf16 offset).  `affinityEnd` selects the end of the line
// when the offset is exactly a wrap boundary.
TextHit caretAt(const RichLayout& layout, uint16_t lineIndex, uint32_t u16Index,
                bool affinityEnd = false);
TextHit caretAtCp(const RichLayout& layout, uint32_t cpIndex, bool affinityEnd = false);

// Selection rectangle(s): one box per line the selection covers.
struct CaretRect { double x0, y0, x1, y1; };
std::vector<CaretRect> selectionRects(const RichLayout& layout, uint32_t cpFrom,
                                      uint32_t cpTo);
// The line box, for a click in the margin or for drawing the edit frame.
CaretRect lineBox(const RichLayout& layout, uint16_t lineIndex);

// ---------------------------------------------------------------------------
// UTF conversion helpers (Windows delivers UTF-16, documents store UTF-8)
// ---------------------------------------------------------------------------
// Strict on decode (same rules as core::validateUtf8), total on encode.
std::vector<uint16_t> utf8ToUtf16(const std::string& utf8);
std::string           utf16ToUtf8(const std::vector<uint16_t>& units);
uint32_t              utf8Length(const std::string& utf8);   // code points
uint32_t              cpToU16(const std::string& utf8, uint32_t cpIndex);
uint32_t              u16ToCp(const std::string& utf8, uint32_t u16Index);

} // namespace text
} // namespace r62

#endif // R62_NEXT_TEXT_RICH_TEXT_HPP
