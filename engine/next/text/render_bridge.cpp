// =============================================================================
// re_round62_viewer/next/text/render_bridge.cpp
//
// T09 - layout cache and T03 emission.  See render_bridge.hpp for the design.
// =============================================================================

#include "next/text/render_bridge.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

namespace r62 {
namespace text {

// One alias, one place: Mesh and the triangulator live in r62::render::geom, and
// spelling the full path at every call site makes the emission code unreadable.
namespace geom = ::r62::render::geom;

namespace {

// FNV-1a over bytes; every input goes in as its little-endian byte image so the
// hash cannot depend on a struct padding byte.
void hashBytes(uint64_t* h, const void* data, size_t size) {
    const unsigned char* p = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < size; ++i) {
        *h ^= static_cast<uint64_t>(p[i]);
        *h *= 1099511628211ull;
    }
}

void hashU64(uint64_t* h, uint64_t v) { hashBytes(h, &v, sizeof(v)); }
void hashDouble(uint64_t* h, double v) {
    uint64_t bits = 0;
    std::memcpy(&bits, &v, sizeof(bits));
    hashU64(h, bits);
}
void hashString(uint64_t* h, const std::string& s) {
    hashU64(h, static_cast<uint64_t>(s.size()));
    if (!s.empty()) hashBytes(h, s.data(), s.size());
}

} // namespace

uint64_t signatureOf(const std::vector<core::TextRun>& runs,
                     const core::Vec2& anchor,
                     const LayoutOptions& options) {
    uint64_t h = 1469598103934665603ull;
    hashU64(&h, static_cast<uint64_t>(runs.size()));
    for (size_t i = 0; i < runs.size(); ++i) {
        hashString(&h, runs[i].utf8);
        hashU64(&h, runs[i].flags);
        hashString(&h, runs[i].fontName);
        hashDouble(&h, runs[i].size);
    }
    hashDouble(&h, anchor.x);
    hashDouble(&h, anchor.y);
    hashDouble(&h, options.defaultSize);
    hashString(&h, options.defaultFont);
    hashU64(&h, options.defaultFlags);
    hashU64(&h, static_cast<uint64_t>(options.anchor));
    hashU64(&h, static_cast<uint64_t>(options.align));
    hashU64(&h, static_cast<uint64_t>(options.spacing));
    hashDouble(&h, options.fixedLineHeight);
    hashDouble(&h, options.lineHeightFactor);
    hashDouble(&h, options.widthLimit);
    hashDouble(&h, options.inlinePadding);
    hashDouble(&h, options.scriptScale);
    hashDouble(&h, options.subBaselineShift);
    hashDouble(&h, options.superBaselineShift);
    hashDouble(&h, options.isotopeScale);
    hashDouble(&h, options.chargeScale);
    hashDouble(&h, options.isotopeRaise);
    hashDouble(&h, options.chargeRaise);
    hashDouble(&h, options.countDrop);
    hashU64(&h, options.applyKerning ? 1u : 0u);
    hashDouble(&h, options.maxKernFraction);
    return h;
}

// ===========================================================================
// Emission
// ===========================================================================
void emitFillMesh(const RichLayout& layout, const core::Vec2* translation,
                  geom::Mesh* mesh) {
    if (!mesh) return;
    const double tx = translation ? translation->x : 0.0;
    const double ty = translation ? translation->y : 0.0;
    for (size_t g = 0; g < layout.glyphs.size(); ++g) {
        const GlyphPlacement& gp = layout.glyphs[g];
        const std::vector<float>& t = gp.triangles;
        for (size_t i = 0; i + 1 < t.size(); i += 2) {
            mesh->pushVertex(core::Vec2(t[i] + tx, t[i + 1] + ty));
        }
    }
}

void emitFillOutline(const RichLayout& layout, const core::Vec2* translation,
                     std::vector<float>* vertices) {
    if (!vertices) return;
    const double tx = translation ? translation->x : 0.0;
    const double ty = translation ? translation->y : 0.0;
    for (size_t g = 0; g < layout.glyphs.size(); ++g) {
        const std::vector<float>& t = layout.glyphs[g].triangles;
        const size_t before = vertices->size();
        vertices->resize(before + t.size());
        float* dst = &(*vertices)[before];
        for (size_t i = 0; i + 1 < t.size(); i += 2) {
            dst[i] = static_cast<float>(t[i] + tx);
            dst[i + 1] = static_cast<float>(t[i + 1] + ty);
        }
    }
}

void emitDecorationMesh(const RichLayout& layout, const core::Vec2* translation,
                        geom::Mesh* mesh) {
    if (!mesh) return;
    const double tx = translation ? translation->x : 0.0;
    const double ty = translation ? translation->y : 0.0;
    for (size_t d = 0; d < layout.decorations.size(); ++d) {
        const DecorationSpan& s = layout.decorations[d];
        const double x0 = s.x0 + tx;
        const double x1 = s.x1 + tx;
        const double y0 = s.y + ty;
        const double y1 = y0 + s.thickness;
        const core::Vec2 a(x0, y0), b(x1, y0), c(x1, y1), e(x0, y1);
        mesh->pushTriangle(a, b, c);
        mesh->pushTriangle(a, c, e);
    }
}

// ===========================================================================
// TextLayoutCache
// ===========================================================================
TextLayoutCache::TextLayoutCache(GlyphCache* glyphs, size_t maxEntries)
    : glyphs_(glyphs), maxEntries_(maxEntries ? maxEntries : 1), clock_(0) {}

// LRU over the live entries.  The map IS the bound, so eviction really releases the
// layout and the outlines behind it; a caller holding a pointer from an earlier
// call has to re-request or copy, which the header states plainly.
void TextLayoutCache::evictIfNeeded() {
    while (entries_.size() > maxEntries_ && !entries_.empty()) {
        std::map<uint64_t, Entry>::iterator victim = entries_.begin();
        for (std::map<uint64_t, Entry>::iterator it = entries_.begin();
             it != entries_.end(); ++it)
            if (it->second.lastUse < victim->second.lastUse) victim = it;
        entries_.erase(victim);
        ++counters_.evictions;
    }
}

void TextLayoutCache::clear() {
    entries_.clear();
    clock_ = 0;
    counters_ = Stats();
    if (glyphs_) glyphs_->clear();
}

TextLayoutCache::Stats TextLayoutCache::stats() const {
    Stats s = counters_;
    s.entries = entries_.size();
    if (glyphs_) s.fontSamplesTotal = glyphs_->sampleCount();
    return s;
}

void TextLayoutCache::setMaxEntries(size_t n) {
    maxEntries_ = n ? n : 1;
    evictIfNeeded();
}

core::Error TextLayoutCache::layout(const std::vector<core::TextRun>& runs,
                                    const core::Vec2& anchor,
                                    const LayoutOptions& options,
                                    const SLayout** out) {
    if (!out) return core::Error(core::ErrorCode::InvalidArgument, "null layout out");
    if (!glyphs_) return core::Error(core::ErrorCode::InvalidArgument, "null glyph cache");
    const uint64_t signature = signatureOf(runs, anchor, options);
    {
        std::map<uint64_t, Entry>::iterator it = entries_.find(signature);
        // The stored key is checked as well as the map key: a 64-bit hash collision
        // must rebuild rather than serve another caption is geometry.
        if (it != entries_.end() && it->second.key == signature) {
            ++counters_.hits;
            it->second.lastUse = ++clock_;
            *out = &it->second.value;
            return core::Error();
        }
    }
    ++counters_.misses;
    Entry entry;
    entry.key = signature;
    entry.value.fontSamplesAtBuild = glyphs_->sampleCount();
    core::Error e = layoutRichText(runs, anchor, options, glyphs_, &entry.value.layout);
    if (!e.ok()) return e;
    emitFillOutline(entry.value.layout, 0, &entry.value.fillVertices);
    entry.value.bounds = layoutBounds(entry.value.layout);
    entry.value.signature = signature;
    entry.value.revision = ++counters_.builds;
    entry.lastUse = ++clock_;
    {
        std::map<uint64_t, Entry>::iterator existing = entries_.find(signature);
        if (existing != entries_.end()) entries_.erase(existing);
    }
    entries_.insert(std::make_pair(signature, entry));
    evictIfNeeded();
    // Re-find by key: eviction may have removed what was just inserted when the
    // bound is smaller than one entry, and the iterator from insert() would then be
    // a use-after-free.
    std::map<uint64_t, Entry>::iterator stored = entries_.find(signature);
    if (stored == entries_.end()) {
        static SLayout fallback;
        fallback = SLayout();
        *out = &fallback;
        return core::Error(core::ErrorCode::LimitExceeded, "layout cache bound too small");
    }
    *out = &stored->second.value;
    return core::Error();
}

core::Error TextLayoutCache::layoutLabel(const LabelSpec& spec,
                                         const core::Vec2& atomPosition,
                                         const LayoutOptions& options,
                                         const LabelAnchorContext* ctx,
                                         const SLayout** out) {
    if (!out) return core::Error(core::ErrorCode::InvalidArgument, "null layout out");
    if (!glyphs_) return core::Error(core::ErrorCode::InvalidArgument, "null glyph cache");
    // The label path builds its runs from the spec and shares the caption key space,
    // so an identical label at an identical anchor IS the same entry: 5000 repeated
    // oxygen labels therefore cost one layout and one mesh.  Bond angles are part of
    // the key because they decide which side the surroundings go on.
    std::vector<core::TextRun> runs;
    buildLabelRuns(spec, &runs);
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < runs.size(); ++i) {
        hashString(&h, runs[i].utf8);
        hashU64(&h, runs[i].flags);
        hashString(&h, runs[i].fontName);
        hashDouble(&h, runs[i].size);
    }
    hashU64(&h, 0x9E3779B97F4A7C15ull);   // domain separator: label vs caption
    hashU64(&h, ctx ? static_cast<uint64_t>(ctx->bondCount) : 0ull);
    for (int i = 0; ctx && i < 8 && i < ctx->bondCount; ++i)
        hashDouble(&h, ctx->bondAnglesDeg[i]);
    hashU64(&h, static_cast<uint64_t>(static_cast<int>(spec.side)));
    {
        std::map<uint64_t, Entry>::iterator it = entries_.find(h);
        if (it != entries_.end() && it->second.key == h) {
            ++counters_.hits;
            it->second.lastUse = ++clock_;
            *out = &it->second.value;
            return core::Error();
        }
    }
    ++counters_.misses;
    Entry entry;
    entry.key = h;
    entry.value.fontSamplesAtBuild = glyphs_->sampleCount();
    core::Error e = layoutAtomLabel(spec, atomPosition, options, glyphs_, ctx,
                                    &entry.value.layout);
    if (!e.ok()) return e;
    emitFillOutline(entry.value.layout, 0, &entry.value.fillVertices);
    entry.value.bounds = layoutBounds(entry.value.layout);
    entry.value.signature = h;
    entry.value.revision = ++counters_.builds;
    entry.lastUse = ++clock_;
    {
        std::map<uint64_t, Entry>::iterator existing = entries_.find(h);
        if (existing != entries_.end()) entries_.erase(existing);
    }
    entries_.insert(std::make_pair(h, entry));
    evictIfNeeded();
    std::map<uint64_t, Entry>::iterator stored = entries_.find(h);
    if (stored == entries_.end()) {
        static SLayout fallback;
        fallback = SLayout();
        *out = &fallback;
        return core::Error(core::ErrorCode::LimitExceeded, "layout cache bound too small");
    }
    *out = &stored->second.value;
    return core::Error();
}

// ===========================================================================
// Primitives
// ===========================================================================
void appendPrimitives(const RichLayout& layout,
                      double viewDefaultTextSize,
                      uint32_t colorRGBA,
                      uint32_t layer,
                      int32_t baseZ,
                      std::vector<render::Primitive>* out) {
    if (!out) return;
    for (size_t li = 0; li < layout.lines.size(); ++li) {
        const LineLayout& line = layout.lines[li];
        for (size_t ri = 0; ri < line.runs.size(); ++ri) {
            const LineRun& lr = line.runs[ri];
            render::Primitive p;
            p.kind = render::PrimKind::Polygon;
            p.layer = render::LayerId(layer);
            p.z = baseZ + static_cast<int32_t>(li);
            p.source = core::ObjectRef(core::ObjectKind::Text, 0);
            p.style.fill = render::Paint(colorRGBA, true);
            p.style.stroke = render::Paint(colorRGBA, false);
            p.style.fillRule = r62::render::FillRule::NonZero;
            render::Subpath sp;
            sp.closed = true;
            // Collect this run glyph triangles into one subpath.  Merged this
            // way the NonZero winding is 1 inside the glyphs and 0 inside a
            // hole, which is exactly the fill-with-holes contract.
            double minX = 0, minY = 0, maxX = 0, maxY = 0;
            bool haveBounds = false;
            for (size_t g = 0; g < layout.glyphs.size(); ++g) {
                const GlyphPlacement& gp = layout.glyphs[g];
                if (gp.lineIndex != li || gp.runIndex != lr.runIndex) continue;
                for (size_t i = 0; i + 1 < gp.triangles.size(); i += 2) {
                    const core::Vec2 v(gp.triangles[i], gp.triangles[i + 1]);
                    sp.points.push_back(v);
                    if (!haveBounds) { minX = maxX = v.x; minY = maxY = v.y; haveBounds = true; }
                    else {
                        if (v.x < minX) minX = v.x;
                        if (v.x > maxX) maxX = v.x;
                        if (v.y < minY) minY = v.y;
                        if (v.y > maxY) maxY = v.y;
                    }
                }
            }
            if (sp.points.empty()) continue;
            p.subpaths.push_back(sp);
            p.bounds = core::Box(core::Vec2(minX, minY), core::Vec2(maxX, maxY));
            const double size = lr.size > 0.0 ? lr.size : viewDefaultTextSize;
            p.textSize = size;
            // A content signature, not a document id: the batcher only needs it
            // to change when the content changes.
            uint64_t sig = 1469598103934665603ull;
            hashU64(&sig, static_cast<uint64_t>(li));
            hashU64(&sig, static_cast<uint64_t>(lr.runIndex));
            hashU64(&sig, lr.flags);
            hashDouble(&sig, lr.x);
            hashDouble(&sig, size);
            hashU64(&sig, static_cast<uint64_t>(sp.points.size()));
            hashU64(&sig, static_cast<uint64_t>(colorRGBA));
            p.version = sig;
            out->push_back(p);
        }

        // Underline (and any future decoration) is REAL GEOMETRY, emitted as its
        // own primitive.  It is not part of the fill outline because a decoration
        // must not merge into the glyph winding: a bar inside a counter would be
        // cancelled by the NonZero rule and the underline would disappear exactly
        // on the letters that have holes.
        for (size_t d = 0; d < layout.decorations.size(); ++d) {
            const DecorationSpan& ds = layout.decorations[d];
            if (ds.kind != Decoration::Underline && ds.kind != Decoration::Strike)
                continue;
            const double x0 = ds.x0, x1 = ds.x1;
            const double y0 = ds.y;
            const double y1 = ds.y + ds.thickness;
            if (x1 <= x0 || y1 <= y0) continue;
            render::Primitive dp;
            dp.kind = render::PrimKind::Polygon;
            dp.layer = render::LayerId(layer);
            // Above the glyphs of the same line: a decoration drawn under a filled
            // glyph is invisible.
            dp.z = baseZ + static_cast<int32_t>(li) + 1;
            dp.source = core::ObjectRef(core::ObjectKind::Text, 0);
            dp.style.fill = render::Paint(colorRGBA, true);
            dp.style.stroke = render::Paint(colorRGBA, false);
            dp.style.fillRule = r62::render::FillRule::NonZero;
            render::Subpath ds2;
            ds2.closed = true;
            ds2.points.push_back(core::Vec2(x0, y0));
            ds2.points.push_back(core::Vec2(x1, y0));
            ds2.points.push_back(core::Vec2(x1, y1));
            ds2.points.push_back(core::Vec2(x0, y1));
            dp.subpaths.push_back(ds2);
            dp.bounds = core::Box(core::Vec2(x0, y0), core::Vec2(x1, y1));
            const double dsize = layout.lines[li].runs.empty()
                ? viewDefaultTextSize : layout.lines[li].runs[0].size;
            dp.textSize = dsize > 0.0 ? dsize : viewDefaultTextSize;
            uint64_t dsig = 1469598103934665603ull;
            hashU64(&dsig, static_cast<uint64_t>(li));
            hashU64(&dsig, static_cast<uint64_t>(d));
            hashU64(&dsig, static_cast<uint64_t>(ds.kind));
            hashDouble(&dsig, x0);
            hashDouble(&dsig, y0);
            hashDouble(&dsig, ds.thickness);
            dsig = dsig * 1099511628211ull + static_cast<uint64_t>(colorRGBA);
            dp.version = dsig;
            out->push_back(dp);
        }
    }
}

// ===========================================================================
// T03 measurer adapter (legacy path)
// ===========================================================================
namespace {

class MeasurerAdapter : public geom::TextMeasurer {
public:
    MeasurerAdapter(GlyphCache* cache, const FontSpec& font)
        : cache_(cache), font_(font) {}
    bool glyph(uint32_t codepoint, const std::string&, geom::GlyphMetrics* out) {
        if (!out) return false;
        *out = geom::GlyphMetrics();
        if (!cache_) return false;
        bool hardFailure = false;
        Glyph g;
        cache_->glyphCopy(codepoint, font_, &g, &hardFailure);
        if (hardFailure) return false;
        out->advance = g.advance;
        out->x0 = g.x0; out->y0 = g.y0; out->x1 = g.x1; out->y1 = g.y1;
        out->missing = g.missing;
        // Triangles for T03: triangulate the em-space contours.  This is
        // the legacy adapter, so it triangulates per call and relies on the
        // GlyphCache to keep the cheap path cheap; the rich path never uses it.
        if (!g.contours.empty()) {
            std::vector<std::vector<core::Vec2> > contours;
            for (size_t c = 0; c < g.contours.size(); ++c) {
                std::vector<core::Vec2> contour;
                for (size_t p = 0; p < g.contours[c].size(); ++p)
                    contour.push_back(core::Vec2(g.contours[c][p][0], g.contours[c][p][1]));
                if (contour.size() >= 3) contours.push_back(contour);
            }
            geom::Mesh mesh;
            if (geom::triangulateContours(contours, r62::render::FillRule::NonZero, &mesh).ok())
                out->triangles = mesh.triangles;
        }
        return true;
    }
    size_t sampleCount() const { return cache_ ? cache_->sampleCount() : 0; }
    size_t reuseCount() const { return cache_ ? cache_->reuseCount() : 0; }
private:
    GlyphCache* cache_;
    FontSpec     font_;
};

} // namespace

render::geom::TextMeasurer* createMeasurerAdapter(GlyphCache* cache,
                                                  const FontSpec& fontOverride) {
    return new MeasurerAdapter(cache, fontOverride);
}

} // namespace text
} // namespace r62
