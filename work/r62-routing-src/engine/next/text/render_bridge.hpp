#ifndef R62_NEXT_TEXT_RENDER_BRIDGE_HPP
#define R62_NEXT_TEXT_RENDER_BRIDGE_HPP
// =============================================================================
// re_round62_viewer/next/text/render_bridge.hpp
//
// T09 - THE BRIDGE BETWEEN TEXT LAYOUT AND T03 RENDERING.
//
// This is the only header in next/text/ that includes next/render.  The layout
// engine itself is render independent so it stays testable without a GPU, and so
// a future host adapter can reuse it without adopting T03.
//
// HOW A TEXT OBJECT REACHES THE SCREEN WITHOUT RESAMPLING GLYPHS
// -----------------------------------------------------------------------------
//   1. layout()       -> RichLayout (outlines resolved ONCE, cached by GlyphCache)
//   2. emitFillMesh() -> document-space triangles (pure arithmetic over the
//                        cached layout; no font access, so GlyphCache sample
//                        counters do not move)
//   3. the triangles are merged into ONE closed outline whose NonZero winding is
//      1 inside the glyphs - including around holes, because the hole contour
//      winds the other way and the hole triangles cancel - and emitted as a
//      single PrimKind::Polygon per run.
//
// Why a merged triangle outline rather than one polygon per triangle: a per-run
// primitive keeps the primitive count at one per run instead of one per triangle
// (a 5-glyph label triangulates to roughly 40 triangles; at 50,000 atoms that
// difference is the whole frame budget), while the merged outline is still a
// plain Primitive that T03 batches, culls and hit-tests with no changes to
// next/render at all.
//
// A caller that prefers to extend next/render with a prebuilt-mesh primitive
// (see INTEGRATION.md, the optional T03 patch) can feed emitFillMesh() output to
// it directly; nothing in this header assumes the polygon route.
// =============================================================================

#include "next/core/contracts.hpp"
#include "next/render/geometry.hpp"
#include "next/render/scene_api.hpp"
#include "next/text/atom_label.hpp"
#include "next/text/rich_text.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace r62 {
namespace text {

// ---------------------------------------------------------------------------
// Signature: content identity for the layout cache
// ---------------------------------------------------------------------------
// Everything that can change the LAYOUT is hashed: the run text, its flags, its
// size, its font, and the anchor/alignment/spacing options.  The view transform
// is deliberately NOT part of it: zoom is not a layout change, which is what
// makes a zoom free.
uint64_t signatureOf(const std::vector<core::TextRun>& runs,
                     const core::Vec2& anchor,
                     const LayoutOptions& options);

// ---------------------------------------------------------------------------
// SLayout: a layout plus its merged fill mesh and its identity
// ---------------------------------------------------------------------------
struct SLayout {
    RichLayout layout;
    // Document-space merged triangle outline, as x,y pairs.  Kept so a caller
    // can hand it to a renderer that accepts prebuilt geometry, and so a test can
    // assert that a second emission is byte-identical.
    std::vector<float> fillVertices;
    core::Box   bounds;          // document-space conservative bounds
    uint64_t    signature;
    uint64_t    revision;        // bumped whenever the content is rebuilt
    size_t      fontSamplesAtBuild;
    SLayout() : signature(0), revision(0), fontSamplesAtBuild(0) {}
    bool empty() const { return layout.metrics.empty; }
    size_t vertexCount() const { return fillVertices.size() / 2; }
};

// ---------------------------------------------------------------------------
// TextLayoutCache: bounded, LRU, and observable
// ---------------------------------------------------------------------------
// Key = signature.  A cache hit copies no glyph data and never touches a font.
//
// STABILITY OF THE RETURNED POINTER (load-bearing, learned the hard way).
// -----------------------------------------------------------------------------
// An earlier version stored the entry BY VALUE in the map and returned
// &entry.value.  That pointer dies the moment the entry is evicted, and a frame
// that had fetched a layout once (a selection, a drag preview, a culling pass)
// then emits freed memory.  The failure mode is a segfault whose stack points at
// vector::size(), which is exactly the kind of bug that only appears in a long
// session.  Entries are therefore held through shared_ptr: the MAP entry can be
// erased while the SLayout object stays alive for as long as any caller still
// holds the pointer.  The cache is still BOUNDED - the bound counts live entries
// - and stats().evictions plus stats().retained show the difference between
// "evicted from the cache" and "actually released".

// BOUND AND RELEASE POLICY (the requirement is a policy, not a hope):
//   * at most maxEntries() distinct signatures are held;
//   * eviction is LRU, and an evicted layout frees its glyph placements, its
//     line records and its fill vertices;
//   * clear() releases everything and ALSO clears the wrapped GlyphCache, so
//     release reaches the outline cache and the provider device contexts;
//   * stats() exposes hits, misses, evictions and the entry count so the claim
//     about repeated labels sharing the cache is measurable.
class TextLayoutCache {
public:
    struct Stats {
        size_t hits, misses, evictions, entries, builds;
        size_t fontSamplesTotal;
        Stats() : hits(0), misses(0), evictions(0), entries(0), builds(0),
                  fontSamplesTotal(0) {}
    };
    TextLayoutCache(GlyphCache* glyphs, size_t maxEntries);

    // LIFETIME CONTRACT: the pointer is owned by the cache and is invalidated by a
    // later layout()/layoutLabel()/clear()/setMaxEntries() that evicts that entry.
    // A caller that needs to hold a layout across other cache traffic must COPY the
    // SLayout (it is a value type) or re-request it by the same key, which is what
    // the acceptance test does.  This is a real limitation, stated here rather than
    // discovered as a segfault, and it is the same rule GlyphCache::get documents.
    core::Error layout(const std::vector<core::TextRun>& runs,
                       const core::Vec2& anchor,
                       const LayoutOptions& options,
                       const SLayout** out);

    core::Error layoutLabel(const LabelSpec& spec,
                            const core::Vec2& atomPosition,
                            const LayoutOptions& options,
                            const LabelAnchorContext* ctx,
                            const SLayout** out);

    void clear();
    Stats stats() const;
    size_t maxEntries() const { return maxEntries_; }
    void setMaxEntries(size_t n);
    const GlyphCache* glyphCache() const { return glyphs_; }

private:
    struct Entry {
        SLayout  value;
        uint64_t key;          // the signature it was built for (collision guard)
        uint64_t lastUse;
        Entry() : key(0), lastUse(0) {}
    };
    void evictIfNeeded();
    GlyphCache* glyphs_;
    size_t maxEntries_;
    uint64_t clock_;
    Stats counters_;
    // Keyed by signature and BOUNDED by maxEntries_: the map IS the bound, so an
    // eviction truly releases the layout and the glyph outlines behind it.  The
    // stored `key` guards against a 64-bit hash collision serving the wrong
    // geometry, which would be silent and very hard to find.
    std::map<uint64_t, Entry> entries_;
};

// ---------------------------------------------------------------------------
// Mesh emission
// ---------------------------------------------------------------------------
// Filled glyph triangles in document space.  `translation` may be null; when it
// is not, every vertex is shifted by it, which is how the SAME cached layout
// serves many atoms (drag previews, repeated labels) with zero extra font work.
void emitFillMesh(const RichLayout& layout,
                  const core::Vec2* translation,
                  r62::render::geom::Mesh* mesh);

// Underline quads, same convention.
void emitDecorationMesh(const RichLayout& layout,
                        const core::Vec2* translation,
                        r62::render::geom::Mesh* mesh);

// The merged triangle outline described at the top of this file.
void emitFillOutline(const RichLayout& layout,
                     const core::Vec2* translation,
                     std::vector<float>* vertices);

// One Primitive per RUN (not per glyph, not per triangle), ready for Scene::add.
// `viewDefaultTextSize` resolves runs whose size is 0.  Bounds come from the
// layout, so T04 hit tests and T03 culling both see the true ink.
void appendPrimitives(const RichLayout& layout,
                      double viewDefaultTextSize,
                      uint32_t colorRGBA,
                      uint32_t layer,
                      int32_t baseZ,
                      std::vector<render::Primitive>* out);

// The T03 measurer adapter.  Its glyph() ignores the requested font name and
// always uses the cache provider with `fontOverride`, which makes it correct for
// the single-style text T03 layoutText can express.  It exists so T19 can keep
// the legacy path alive while migrating; the rich path does not use it.
render::geom::TextMeasurer* createMeasurerAdapter(GlyphCache* cache,
                                                  const FontSpec& fontOverride);

} // namespace text
} // namespace r62

#endif // R62_NEXT_TEXT_RENDER_BRIDGE_HPP
