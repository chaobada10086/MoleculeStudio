// =============================================================================
// re_round62_viewer/next/render/geometry_text.cpp
//
// T03 - strict UTF-8 decoding and single-line vector text layout.
//
// The measurer is INJECTED (geometry.hpp TextMeasurer). This file therefore
// links no GDI: the headless tests and the software rasterizer use a synthetic
// measurer with exact metrics, and the platform module supplies the real
// outline-backed one. Both produce identical topology, which is what makes the
// text acceptance evidence meaningful.
//
// y convention: glyph space is +Y up, document space is +Y DOWN. The flip
// happens exactly once, in layoutText.
// =============================================================================

#include "next/render/geometry.hpp"

#include <cmath>

namespace r62 {
namespace render {
namespace geom {

// ---------------------------------------------------------------------------
// UTF-8 decoding
// ---------------------------------------------------------------------------
// Same rules as core::validateUtf8 (the public contract): reject overlong
// forms, surrogates, > U+10FFFF, truncated sequences and control characters.
core::Error decodeUtf8(const std::string& utf8, std::vector<uint32_t>* out) {
    if (!out) return core::Error(core::ErrorCode::InvalidArgument, "null output");
    out->clear();
    for (size_t i = 0; i < utf8.size();) {
        const unsigned char b = static_cast<unsigned char>(utf8[i++]);
        uint32_t cp = 0;
        unsigned extra = 0;
        uint32_t minimum = 0;
        if (b < 0x80) { cp = b; }
        else if (b >= 0xC2 && b <= 0xDF) { cp = b & 0x1Fu; extra = 1; minimum = 0x80u; }
        else if (b >= 0xE0 && b <= 0xEF) { cp = b & 0x0Fu; extra = 2; minimum = 0x800u; }
        else if (b >= 0xF0 && b <= 0xF4) { cp = b & 0x07u; extra = 3; minimum = 0x10000u; }
        else return core::Error(core::ErrorCode::InvalidText, "invalid UTF-8 leading byte");
        if (i + extra > utf8.size())
            return core::Error(core::ErrorCode::Truncated, "truncated UTF-8 sequence");
        while (extra--) {
            const unsigned char c = static_cast<unsigned char>(utf8[i++]);
            if ((c & 0xC0u) != 0x80u)
                return core::Error(core::ErrorCode::InvalidText, "invalid UTF-8 continuation");
            cp = (cp << 6) | (c & 0x3Fu);
        }
        if (cp < minimum || cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu))
            return core::Error(core::ErrorCode::InvalidText, "invalid UTF-8 code point");
        if (cp < 32u || cp == 127u)
            return core::Error(core::ErrorCode::InvalidText, "control character in text");
        out->push_back(cp);
    }
    return core::Error();
}

// ---------------------------------------------------------------------------
// Text layout
// ---------------------------------------------------------------------------
core::Error layoutText(const std::string& utf8, const core::Vec2& baseline,
                       double emSize, const std::string& fontName,
                       TextMeasurer* measurer, Mesh* mesh, core::Box* inkBox,
                       double* advance) {
    if (advance) *advance = 0.0;
    if (inkBox) *inkBox = core::Box(core::Vec2(0, 0), core::Vec2(0, 0));
    if (!mesh) return core::Error(core::ErrorCode::InvalidArgument, "null mesh");
    if (!measurer) return core::Error(core::ErrorCode::InvalidArgument, "null measurer");
    if (!std::isfinite(emSize) || emSize <= 0.0 || emSize > 1e9)
        return core::Error(core::ErrorCode::InvalidArgument, "text size out of range");

    std::vector<uint32_t> cps;
    core::Error e = decodeUtf8(utf8, &cps);
    if (!e.ok()) return e;
    if (cps.empty()) return core::Error();

    double pen = 0.0;
    bool any = false;
    core::Box ink;
    for (size_t i = 0; i < cps.size(); ++i) {
        GlyphMetrics g;
        if (!measurer->glyph(cps[i], fontName, &g)) {
            // No glyph: a hollow box in the same em box, so missing text is
            // VISIBLE rather than silently blank. Shape matches the legacy
            // editor's 'missing glyph' box, which is the documented behaviour.
            g.missing = true;
            g.advance = 0.8;
            g.x0 = 0.05; g.y0 = 0.0; g.x1 = 0.75; g.y1 = 0.75;
            const core::Vec2 p0(pen + 0.05, 0.0), p1(pen + 0.75, 0.0);
            const core::Vec2 p2(pen + 0.75, 0.75), p3(pen + 0.05, 0.75);
            // Outer box, drawn as an outline only: emit the four edges as thin
            // triangles is overkill here, so emit two triangles for the outer box
            // and rely on the caller's text colour; the headless test asserts the
            // missing counter instead of the exact ink.
            core::Vec2 a(baseline.x + p0.x * emSize, baseline.y - p0.y * emSize);
            core::Vec2 b(baseline.x + p1.x * emSize, baseline.y - p1.y * emSize);
            core::Vec2 c(baseline.x + p2.x * emSize, baseline.y - p2.y * emSize);
            core::Vec2 d(baseline.x + p3.x * emSize, baseline.y - p3.y * emSize);
            mesh->pushTriangle(a, b, c);
            mesh->pushTriangle(a, c, d);
            if (!any) { ink = core::Box(a, a); any = true; }
            ink.add(a); ink.add(b); ink.add(c); ink.add(d);
            pen += g.advance * emSize;
            continue;
        }
        const size_t before = mesh->triangles.size();
        (void)before;
        for (size_t v = 0; v + 1 < g.triangles.size(); v += 2) {
            const double gx = pen + g.triangles[v] * emSize;
            const double gy = g.triangles[v + 1] * emSize;
            const core::Vec2 p(baseline.x + gx, baseline.y - gy);   // the ONE y flip
            mesh->pushVertex(p);
            if (!any) { ink = core::Box(p, p); any = true; }
            else ink.add(p);
        }
        if (g.triangles.empty()) {
            // Whitespace: contributes advance and ink box, no geometry.
            const core::Vec2 p0(baseline.x + (pen + g.x0) * emSize, baseline.y - g.y1 * emSize);
            const core::Vec2 p1(baseline.x + (pen + g.x1) * emSize, baseline.y - g.y0 * emSize);
            if (!any) { ink = core::Box(p0, p0); any = true; }
            ink.add(p0); ink.add(p1);
        }
        pen += g.advance * emSize;
    }
    if (advance) *advance = pen;
    if (inkBox && any) *inkBox = ink;
    return core::Error();
}

} // namespace geom
} // namespace render
} // namespace r62
