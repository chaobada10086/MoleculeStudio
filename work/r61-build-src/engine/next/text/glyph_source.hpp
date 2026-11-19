#ifndef R62_NEXT_TEXT_GLYPH_SOURCE_HPP
#define R62_NEXT_TEXT_GLYPH_SOURCE_HPP
// =============================================================================
// re_round62_viewer/next/text/glyph_source.hpp
//
// T09 - GLYPH OUTLINES, THE INJECTION SEAM.
//
// The rich-text layout engine in this directory must be testable without a
// window station, without a GPU and without assuming a particular font is
// installed.  It therefore NEVER calls GDI/DirectWrite itself: it asks a
// GlyphProvider for one code point at a time.  Two providers ship here:
//
//   * gdi_glyph_source.cpp  -> GdiGlyphProvider   (real outlines, real machine)
//   * synthetic provider    -> SyntheticGlyphProvider (deterministic, headless)
//
// WHY CONTOURS *AND* TRIANGLES ARE BOTH RETURNED
// -----------------------------------------------------------------------------
// A TrueType glyph such as "O", "B", "8", "%" or "0" is a SET OF CLOSED
// CONTOURS whose winding encodes the holes.  Two consumers need two different
// things:
//
//   * the renderer needs filled triangles -> geom::triangulateContours()
//     (T03, ear clipping with hole bridging; no GLU, so the headless
//     rasterizer and the GPU path agree);
//   * the evidence/export path needs the OUTLINES, because an SVG or a
//     "does this glyph really have a hole" assertion is about contours, not
//     about a triangle soup that happens to look right.
//
// The provider therefore returns BOTH, always in em units (em == 1.0) with the
// baseline at y == 0 and +Y up, which is the same convention as
// render::geom::GlyphMetrics.  The layout engine flips Y exactly once when it
// emits document-space geometry, per the next/core contract.
//
// COORDINATE / UNIT CONTRACT (inherited from next/core, never redefined here)
//   * em space: y up, baseline at 0, advance to the right.
//   * document space: y DOWN.  The single flip lives in rich_text.cpp.
//   * lengths handed to a provider are always em units, never points and never
//     pixels, so a provider cannot accidentally apply DPI.
// =============================================================================

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace r62 {
namespace text {

// ---------------------------------------------------------------------------
// Version handshake
// ---------------------------------------------------------------------------
// Independent of core::kInterfaceVersion and render::kRenderVersion.  A caller
// checks all three.  Additive changes bump minor; a signature or semantic change
// bumps major.
enum {
    kTextMajor    = 1,
    kTextMinor    = 0,
    kTextRevision = 0,
    kTextVersion  = kTextMajor * 10000 + kTextMinor * 100 + kTextRevision
};

inline bool requestTextCompatibility(int major, int minor) {
    return major == kTextMajor && minor <= kTextMinor;
}

// ---------------------------------------------------------------------------
// FontSpec: what can change a glyph shape
// ---------------------------------------------------------------------------
// EVERY field here is part of a glyph cache key.  A cache that omitted the
// weight would serve the regular "N" for a bold "N", which is exactly the class
// of bug that is invisible in a unit test and obvious on screen.  Keeping the
// key inside the spec type makes "the key includes what it must" reviewable in
// one place.
//
// size is NOT part of the key in the default cache: outlines are resolution
// independent (em units), so one glyph serves every size and the layout scales
// it.  The optional outline cache in rich_text.cpp keeps size in its key only
// for providers whose quadtree depends on hinted pixel sizes.
struct FontSpec {
    std::string family;   // empty == the provider's default UI family
    bool        bold;
    bool        italic;
    FontSpec() : bold(false), italic(false) {}
    FontSpec(const std::string& f, bool b, bool i) : family(f), bold(b), italic(i) {}
    bool operator==(const FontSpec& o) const {
        return family == o.family && bold == o.bold && italic == o.italic;
    }
    bool operator!=(const FontSpec& o) const { return !(*this == o); }
    bool operator<(const FontSpec& o) const {
        if (family != o.family) return family < o.family;
        if (bold != o.bold) return bold < o.bold;
        return italic < o.italic;
    }
    // A diagnostic string.  Used in messages and in SVG metadata; never parsed.
    std::string describe() const;
};

// ---------------------------------------------------------------------------
// Glyph: one code point's outline in em units
// ---------------------------------------------------------------------------
struct Glyph {
    // Closed contours, y up, baseline at 0, em == 1.0.  [0] is not guaranteed to
    // be the outer contour; the winding rule (not the order) defines the holes,
    // which is what makes "O" a ring after triangulation.
    std::vector<std::vector<std::vector<float> > > contours;  // [contour][point][2]
    // Filled triangles in the same space.  Empty for a whitespace glyph.
    std::vector<float> triangles;                            // x,y pairs
    double advance;      // pen movement, em units
    double x0, y0, x1, y1;   // ink box, em units; all zero when there is no ink
    bool   hasInk;
    bool   missing;      // provider has no such glyph -> a substituted box outline
    bool   blank;        // real glyph with no ink (space)
    FontSpec resolved;   // the font actually used (fallback, or after death)
    std::string missingReason;  // empty when the glyph was found

    Glyph() : advance(0.0), x0(0), y0(0), x1(0), y1(0),
              hasInk(false), missing(false), blank(false) {}
};

// ---------------------------------------------------------------------------
// Line metrics: what a baseline calculation needs
// ---------------------------------------------------------------------------
struct LineMetrics {
    double ascent;    // em units, above the baseline (positive)
    double descent;   // em units, below the baseline (positive)
    double lineGap;   // em units
    double em;        // normally 1.0; providers with a different design size say so
    bool   valid;
    LineMetrics() : ascent(0.8), descent(0.2), lineGap(0.0), em(1.0), valid(false) {}
    // Default line height, used when a caller asks for line spacing but the
    // provider has no metrics: matches the synthetic provider so headless and
    // real layouts are comparable.
    double lineHeight() const { return ascent + descent + lineGap; }
};

// ---------------------------------------------------------------------------
// GlyphProvider: the seam
// ---------------------------------------------------------------------------
class GlyphProvider {
public:
    virtual ~GlyphProvider() {}
    // Returns false ONLY for a hard failure (no font at all).  A code point the
    // provider cannot map returns true with missing=true and a substitute box,
    // because "show tofu" is a rendering result while "false" means "I have no
    // font", and the caller must be able to tell those apart.
    virtual bool glyph(uint32_t codepoint, const FontSpec& font, Glyph* out) = 0;
    virtual bool lineMetrics(const FontSpec& font, LineMetrics* out) = 0;
    // Optional pair kerning in em units, ADDED to the advance.  Providers that
    // have no kern table inherit this and report 0.0, which is the honest answer
    // ("no adjustment"), not a substitute guess.  The layout engine caps the
    // absolute value (LayoutOptions::maxKernFraction) so a broken font table
    // cannot overlap glyphs.
    virtual double kern(uint32_t /*left*/, uint32_t /*right*/, const FontSpec& /*font*/) {
        return 0.0;
    }
    // Advanced typographic shaping (ligatures, Arabic joining, Indic reordering,
    // mark positioning).  A provider that cannot shape reports false and the
    // layout engine falls back to one-glyph-per-code-point, which is correct for
    // Latin/CJK and approximate elsewhere.  See README.md "complex scripts".
    virtual bool shape(const std::string& utf8, const FontSpec& font,
                       std::vector<uint32_t>* glyphIds) {
        (void)utf8; (void)font; (void)glyphIds; return false;
    }
    // Counters so a caller's "zero resampling" claim is a measurement.
    virtual size_t sampleCount() const = 0;
    virtual size_t reuseCount() const = 0;
    virtual void   resetCounters() {}
    // Human-readable identity for evidence output, e.g. "GDI/Segoe UI".
    virtual const char* providerName() const = 0;
    // Clear any internal resource cache.  Must be safe to call at any time and
    // is what makes the memory bound in rich_text.cpp a real release, not a
    // cosmetic clear of the outer map.
    virtual void clear() {}
};

// ---------------------------------------------------------------------------
// GlyphCache: key (family, weight, style, code point) -> Glyph
// ---------------------------------------------------------------------------
// BOUNDED.  A caption made of 10,000 distinct code points must not be able to
// grow the cache without limit, and the bound must be observable, so eviction is
// LRU-with-a-counter rather than "grow forever and hope".  The cap is in
// ENTRIES, not bytes, because a byte bound would need per-provider accounting
// that the seam does not expose; the README states this limitation.
class GlyphCache {
public:
    struct Stats {
        size_t hits, misses, evictions, entries;
        Stats() : hits(0), misses(0), evictions(0), entries(0) {}
    };
    explicit GlyphCache(GlyphProvider* provider = 0, size_t maxEntries = 4096);

    // Never returns a dangling reference: the returned pointer is owned by the
    // cache and stays valid until the next call for a DIFFERENT key that evicts
    // it, so a caller that holds several glyphs across calls must copy or use
    // glyphCopy().  layoutText() copies into its output, so it is safe.
    const Glyph* get(uint32_t codepoint, const FontSpec& font, bool* hardFailure);
    void glyphCopy(uint32_t codepoint, const FontSpec& font, Glyph* out, bool* hardFailure);
    bool lineMetrics(const FontSpec& font, LineMetrics* out);

    GlyphProvider* provider() const { return provider_; }
    void setProvider(GlyphProvider* p) { provider_ = p; clear(); }
    void clear();
    void setMaxEntries(size_t n);
    size_t maxEntries() const { return maxEntries_; }
    Stats stats() const;
    size_t sampleCount() const { return provider_ ? provider_->sampleCount() : 0; }
    size_t reuseCount() const { return provider_ ? provider_->reuseCount() : 0; }

private:
    struct Key {
        uint32_t family;      // interned family id
        uint32_t codepoint;
        bool     bold;
        bool     italic;
        bool operator<(const Key& o) const {
            if (family != o.family) return family < o.family;
            if (bold != o.bold) return bold < o.bold;
            if (italic != o.italic) return italic < o.italic;
            return codepoint < o.codepoint;
        }
    };
    struct Entry {
        Glyph  glyph;
        uint64_t lastUse;
    };
    uint32_t internFamily(const std::string& family);
    void     evictIfNeeded();

    GlyphProvider* provider_;
    size_t maxEntries_;
    uint64_t clock_;
    size_t hits_, misses_, evictions_;
    std::vector<std::string> families_;
    std::map<uint32_t, size_t> familyIds_;
    std::map<Key, Entry> entries_;
    std::map<std::string, LineMetrics> metrics_;
};

// ---------------------------------------------------------------------------
// SyntheticGlyphProvider: deterministic outlines for headless tests
// ---------------------------------------------------------------------------
// Every glyph is box-like so that geometry assertions are exact and readable:
//   * a capital letter / digit / CJK ideograph is a filled rectangle in the
//     upper part of the em box, so a run of them has a known ink box;
//   * 'O', '0', 'B', '8', '%', and U+25CB (white circle) get a real HOLE
//     (two contours, opposite winding), which is what proves the hole path
//     end to end;
//   * space is blank with an advance;
//   * anything the provider is told to treat as unmapped returns tofu.
// advance is 0.6 em for Latin, 1.0 em for CJK, so line widths are predictable.
class SyntheticGlyphProvider : public GlyphProvider {
public:
    SyntheticGlyphProvider();
    // Mark a code point as absent.  Used to prove the "unknown font / missing
    // glyph" behaviour without uninstalling a font.
    void setMissing(uint32_t codepoint, bool missing);
    // Force every glyph of a family to be missing (simulates "font not
    // installed": the fallback chain decides what happens next).
    void setFamilyMissing(const std::string& family, bool missing);

    bool glyph(uint32_t codepoint, const FontSpec& font, Glyph* out);
    bool lineMetrics(const FontSpec& font, LineMetrics* out);
    size_t sampleCount() const { return samples_; }
    size_t reuseCount() const { return reuses_; }
    void resetCounters() { samples_ = 0; reuses_ = 0; }
    const char* providerName() const { return "synthetic"; }

private:
    bool isWide(uint32_t cp) const;
    bool hasHole(uint32_t cp) const;
    std::vector<uint32_t> missing_;
    std::vector<std::string> missingFamilies_;
    size_t samples_, reuses_;
};

// ---------------------------------------------------------------------------
// GdiGlyphProvider: real outlines from the installed fonts
// ---------------------------------------------------------------------------
// Declared here, defined in gdi_glyph_source.cpp.  On a non-Windows build the
// factory returns 0 instead of failing to link, so the layout engine and its
// tests stay portable.
//
// Features:
//   * GetGlyphOutlineW(GGO_NATIVE|GGO_UNHINTED) outlines, quadratic splines
//     flattened at a fixed em-space tolerance (NOT a per-frame sample: the
//     result is cached by GlyphCache and the outline itself never changes with
//     zoom, which is the "no glyph resampling while dragging" requirement);
//   * a fallback chain: requested family -> CJK family -> default UI family ->
//     tofu.  The reason is recorded in Glyph::missingReason so an unknown font
//     has an EXPLAINABLE result instead of a blank;
//   * contour winding is preserved from the font, so holes work with the
//     NonZero fill rule T03 uses.
class GdiGlyphProvider;
GlyphProvider* createGdiGlyphProvider(const std::string& defaultFamily = std::string(),
                                      const std::string& cjkFamily = std::string());
// True when this build can actually create one.
bool gdiGlyphProviderAvailable();
// Fallback-chain diagnostics: which family actually served each requested
// family.  A layout that asked for an uninstalled font can therefore explain
// itself ("Verdana -> Segoe UI"), and the evidence renderer prints it.
void gdiFallbackReport(std::vector<std::string>* out);

} // namespace text
} // namespace r62

#endif // R62_NEXT_TEXT_GLYPH_SOURCE_HPP
