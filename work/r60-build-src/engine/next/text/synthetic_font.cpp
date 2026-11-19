// =============================================================================
// re_round62_viewer/next/text/synthetic_font.cpp
//
// T09 - the deterministic GlyphProvider used by every headless test and by the
// offline evidence renderer.
//
// WHY A SYNTHETIC FONT EXISTS AT ALL
// -----------------------------------------------------------------------------
// The acceptance criteria include "O/B holes", "different run sizes", "missing
// glyph behaviour" and "repeated labels share the cache".  Each of those must be
// provable on a machine with no font installed, with exact numbers, and without
// the GDI provider's machine-dependent metrics making the assertion flaky.  A
// synthetic provider with arithmetic outlines gives exact assertions:
//
//   * Latin/digit/other: a filled rectangle from y=0 to y=0.7, x from 0 to 0.6
//     scaled to the requested advance, with a small notch so that two different
//     code points are not accidentally identical.
//   * CJK: advance 1.0 em, ink 0.05..0.95 x 0.0..0.8 - so a Chinese character
//     is measurably wider than a Latin one and line breaking can be checked.
//   * 'O', '0', 'B', '8', '%', U+25CB: TWO contours with OPPOSITE winding, i.e.
//     a real hole.  geom::triangulateContours with the NonZero rule must leave
//     the centre empty; the test asserts that by point sampling.
//   * space: no ink, advance 0.3 em (Latin) / 0.5 em (CJK).
//   * unmapped code points (configurable): the tofu box, missing=true, with
//     missingReason="synthetic: unmapped".
//
// The provider counts samples and reuses itself so that a test can prove the
// layout engine reached it exactly once per (family, weight, style, code point),
// independent of the GlyphCache.
// =============================================================================

#include "next/text/glyph_source.hpp"

#include <cmath>

namespace r62 {
namespace text {

namespace {

const float kLatinAdvance = 0.6f;
const float kCjkAdvance = 1.0f;
const float kSpaceAdvance = 0.3f;
const float kCjkSpaceAdvance = 0.5f;

void addRect(std::vector<std::vector<std::vector<float> > >* contours,
             float x0, float y0, float x1, float y1, bool ccw) {
    std::vector<std::vector<float> > c;
    const float xs[4] = { x0, x1, x1, x0 };
    const float ys[4] = { y0, y0, y1, y1 };
    for (int i = 0; i < 4; ++i) {
        const int k = ccw ? i : (3 - i);
        std::vector<float> p;
        p.push_back(xs[k]);
        p.push_back(ys[k]);
        c.push_back(p);
    }
    contours->push_back(c);
}

bool isHoleCode(uint32_t cp) {
    return cp == 0x4Fu || cp == 0x30u || cp == 0x42u || cp == 0x38u ||
           cp == 0x25u || cp == 0x25CBu;
}

} // namespace

SyntheticGlyphProvider::SyntheticGlyphProvider() : samples_(0), reuses_(0) {}

void SyntheticGlyphProvider::setMissing(uint32_t codepoint, bool missing) {
    if (missing) {
        for (size_t i = 0; i < missing_.size(); ++i) if (missing_[i] == codepoint) return;
        missing_.push_back(codepoint);
    } else {
        for (size_t i = 0; i < missing_.size(); ++i)
            if (missing_[i] == codepoint) { missing_.erase(missing_.begin() + i); return; }
    }
}

void SyntheticGlyphProvider::setFamilyMissing(const std::string& family, bool missing) {
    if (missing) {
        for (size_t i = 0; i < missingFamilies_.size(); ++i)
            if (missingFamilies_[i] == family) return;
        missingFamilies_.push_back(family);
    } else {
        for (size_t i = 0; i < missingFamilies_.size(); ++i)
            if (missingFamilies_[i] == family) {
                missingFamilies_.erase(missingFamilies_.begin() + i);
                return;
            }
    }
}

bool SyntheticGlyphProvider::isWide(uint32_t cp) const {
    return (cp >= 0x1100u && cp <= 0x11FFu) || (cp >= 0x2E80u && cp <= 0x9FFFu) ||
           (cp >= 0xAC00u && cp <= 0xD7AFu) || (cp >= 0xF900u && cp <= 0xFAFFu) ||
           (cp >= 0x20000u && cp <= 0x2FA1Fu);
}

bool SyntheticGlyphProvider::hasHole(uint32_t cp) const { return isHoleCode(cp); }

bool SyntheticGlyphProvider::glyph(uint32_t codepoint, const FontSpec& font, Glyph* out) {
    if (!out) return false;
    Glyph& g = *out;
    g = Glyph();
    g.resolved = font;

    for (size_t i = 0; i < missingFamilies_.size(); ++i) {
        if (missingFamilies_[i] == font.family) {
            g.missing = true;
            g.missingReason = "synthetic: family marked missing";
            break;
        }
    }
    if (!g.missing) {
        for (size_t i = 0; i < missing_.size(); ++i) {
            if (missing_[i] == codepoint) {
                g.missing = true;
                g.missingReason = "synthetic: code point marked unmapped";
                break;
            }
        }
    }

    ++samples_;
    if (g.missing) {
        // Tofu: an outer box and a wound inner box, so a missing glyph is not
        // confused with a real character by the hole test.
        addRect(&g.contours, 0.05f, 0.0f, 0.75f, 0.75f, true);
        addRect(&g.contours, 0.13f, 0.08f, 0.67f, 0.67f, false);
        g.advance = 0.8;
        g.x0 = 0.05; g.y0 = 0.0; g.x1 = 0.75; g.y1 = 0.75;
        g.hasInk = true;
        return true;
    }

    if (codepoint == 0x20u || codepoint == 0x09u) {
        g.blank = true;
        g.advance = isWide(codepoint) ? kCjkSpaceAdvance : kSpaceAdvance;
        return true;
    }

    const bool wide = isWide(codepoint);
    const float advance = wide ? kCjkAdvance : kLatinAdvance;
    g.advance = advance;

    if (hasHole(codepoint)) {
        addRect(&g.contours, 0.06f, -0.02f, 0.56f, 0.70f, true);
        addRect(&g.contours, 0.20f, 0.16f, 0.42f, 0.52f, false);
        g.x0 = 0.06; g.y0 = -0.02; g.x1 = 0.56; g.y1 = 0.70;
    } else if (wide) {
        addRect(&g.contours, 0.05f, 0.0f, 0.95f, 0.80f, true);
        g.x0 = 0.05; g.y0 = 0.0; g.x1 = 0.95; g.y1 = 0.80;
    } else {
        // A per-code-point variation so that a test can detect a cache that
        // returned the wrong glyph: the ink width depends on the low bits.
        const float inset = 0.01f * static_cast<float>(codepoint & 0x7u);
        addRect(&g.contours, 0.03f + inset, 0.0f, 0.57f, 0.70f, true);
        g.x0 = 0.03 + inset; g.y0 = 0.0; g.x1 = 0.57; g.y1 = 0.70;
    }
    // Bold and italic change the outline, not just the metrics, so a cache that
    // ignored the weight or the slant would be caught by a geometry assertion.
    if (font.bold) {
        for (size_t c = 0; c < g.contours.size(); ++c)
            for (size_t p = 0; p < g.contours[c].size(); ++p)
                g.contours[c][p][1] = g.contours[c][p][1] * 1.10f;
        g.y1 *= 1.10;
        g.advance += (wide ? 0.08 : 0.04);
    }
    if (font.italic) {
        for (size_t c = 0; c < g.contours.size(); ++c)
            for (size_t p = 0; p < g.contours[c].size(); ++p)
                g.contours[c][p][0] = g.contours[c][p][0] + 0.18f * g.contours[c][p][1];
        g.x1 += 0.18 * g.y1;
        g.advance += (wide ? 0.04 : 0.02);
    }
    g.hasInk = true;
    return true;
}

bool SyntheticGlyphProvider::lineMetrics(const FontSpec& font, LineMetrics* out) {
    if (!out) return false;
    out->ascent = font.bold ? 0.85 : 0.80;
    out->descent = 0.20;
    out->lineGap = 0.05;
    out->em = 1.0;
    out->valid = true;
    return true;
}

} // namespace text
} // namespace r62
