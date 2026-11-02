// =============================================================================
// re_round62_viewer/next/text/rich_text.cpp
//
// T09 - the layout engine.  Platform independent: it reaches fonts only through
// a GlyphProvider and geometry only through cached outlines.
//
// PIPELINE
//   1. validate + decode runs (strict UTF-8, control characters rejected)
//   2. resolve per-code-point style: face, effective size, script level
//      (explicit kSubscript/kSuperscript, or automatic inside Formula style)
//   3. tokenise and find break opportunities
//   4. break into lines (a hard break ends a line; a width limit wraps)
//   5. establish every line baseline (Automatic / Variable / Fixed spacing)
//   6. anchor the block (caption vs atom label) and align each line
//   7. emit glyph placements, decorations and metrics
//
// Every step runs once.  Nothing here samples a font twice for the same code
// point: the GlyphCache is the only path to outlines, and line metrics are
// cached per family.
// =============================================================================

#include "next/text/rich_text.hpp"

// The triangulator and the point-in-contour test come from T03 is geometry
// kernel.  The DEPENDENCY IS ONE-DIRECTIONAL and header-only in this direction:
// the layout engine owns no GPU state, and next/render does not know that
// next/text exists.  Triangulating at layout time (once per glyph) instead of at
// draw time is what keeps a frame free of glyph work.
#include "next/render/geometry.hpp"

#include <algorithm>
#include <cmath>
#include <map>

namespace r62 {
namespace text {

// ===========================================================================
// FontSpec::describe
// ===========================================================================
std::string FontSpec::describe() const {
    std::string s = family.empty() ? std::string("(default)") : family;
    if (bold) s += " Bold";
    if (italic) s += " Italic";
    return s;
}

// ===========================================================================
// GlyphCache
// ===========================================================================
GlyphCache::GlyphCache(GlyphProvider* provider, size_t maxEntries)
    : provider_(provider), maxEntries_(maxEntries ? maxEntries : 1),
      clock_(0), hits_(0), misses_(0), evictions_(0) {}

uint32_t GlyphCache::internFamily(const std::string& family) {
    for (size_t i = 0; i < families_.size(); ++i)
        if (families_[i] == family) return static_cast<uint32_t>(i + 1);
    families_.push_back(family);
    return static_cast<uint32_t>(families_.size());
}

void GlyphCache::clear() {
    entries_.clear();
    metrics_.clear();
    families_.clear();
    hits_ = misses_ = evictions_ = 0;
    clock_ = 0;
    if (provider_) provider_->clear();
}

void GlyphCache::setMaxEntries(size_t n) {
    maxEntries_ = n ? n : 1;
    evictIfNeeded();
}

GlyphCache::Stats GlyphCache::stats() const {
    Stats s;
    s.hits = hits_;
    s.misses = misses_;
    s.evictions = evictions_;
    s.entries = entries_.size();
    return s;
}

void GlyphCache::evictIfNeeded() {
    // LRU by last-use clock.  O(n) per eviction; evictions are rare because a
    // real drawing's working set is a few hundred glyphs, so the O(n) scan is
    // paid only in the pathological case this bound exists for.
    while (entries_.size() > maxEntries_ && !entries_.empty()) {
        std::map<Key, Entry>::iterator victim = entries_.begin();
        for (std::map<Key, Entry>::iterator it = entries_.begin(); it != entries_.end(); ++it)
            if (it->second.lastUse < victim->second.lastUse) victim = it;
        entries_.erase(victim);
        ++evictions_;
    }
}

const Glyph* GlyphCache::get(uint32_t codepoint, const FontSpec& font, bool* hardFailure) {
    if (hardFailure) *hardFailure = false;
    if (!provider_) {
        if (hardFailure) *hardFailure = true;
        return 0;
    }
    Key key;
    key.family = internFamily(font.family);
    key.codepoint = codepoint;
    key.bold = font.bold;
    key.italic = font.italic;

    std::map<Key, Entry>::iterator it = entries_.find(key);
    if (it != entries_.end()) {
        ++hits_;
        it->second.lastUse = ++clock_;
        return &it->second.glyph;
    }
    ++misses_;
    Entry entry;
    if (!provider_->glyph(codepoint, font, &entry.glyph)) {
        if (hardFailure) *hardFailure = true;
        return 0;
    }
    entry.lastUse = ++clock_;
    std::pair<std::map<Key, Entry>::iterator, bool> ins =
        entries_.insert(std::make_pair(key, entry));
    evictIfNeeded();
    return &ins.first->second.glyph;
}

void GlyphCache::glyphCopy(uint32_t codepoint, const FontSpec& font, Glyph* out,
                           bool* hardFailure) {
    if (!out) return;
    const Glyph* g = get(codepoint, font, hardFailure);
    if (!g) return;
    *out = *g;
}

bool GlyphCache::lineMetrics(const FontSpec& font, LineMetrics* out) {
    if (!out) return false;
    std::map<std::string, LineMetrics>::iterator it = metrics_.find(font.family);
    if (it != metrics_.end()) { *out = it->second; return it->second.valid; }
    LineMetrics m;
    const bool ok = provider_ ? provider_->lineMetrics(font, &m) : false;
    if (!ok) {
        // A provider without metrics still has to produce a usable line box.
        m.ascent = 0.8; m.descent = 0.2; m.lineGap = 0.0; m.em = 1.0; m.valid = false;
    }
    metrics_[font.family] = m;
    *out = m;
    return ok;
}

// ===========================================================================
// UTF conversion
// ===========================================================================
namespace {
bool decodeStrict(const std::string& utf8, std::vector<uint32_t>* out,
                  std::vector<uint32_t>* u16Index) {
    out->clear();
    if (u16Index) u16Index->clear();
    uint32_t u16 = 0;
    for (size_t i = 0; i < utf8.size();) {
        const unsigned char b = static_cast<unsigned char>(utf8[i++]);
        uint32_t cp = 0;
        unsigned extra = 0;
        uint32_t minimum = 0;
        if (b < 0x80u) { cp = b; }
        else if (b >= 0xC2u && b <= 0xDFu) { cp = b & 0x1Fu; extra = 1; minimum = 0x80u; }
        else if (b >= 0xE0u && b <= 0xEFu) { cp = b & 0x0Fu; extra = 2; minimum = 0x800u; }
        else if (b >= 0xF0u && b <= 0xF4u) { cp = b & 0x07u; extra = 3; minimum = 0x10000u; }
        else return false;
        if (i + extra > utf8.size()) return false;
        for (unsigned k = 0; k < extra; ++k) {
            const unsigned char c = static_cast<unsigned char>(utf8[i++]);
            if ((c & 0xC0u) != 0x80u) return false;
            cp = (cp << 6) | (c & 0x3Fu);
        }
        if (cp < minimum || cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu))
            return false;
        // Structural characters are ALLOWED, other control characters are not:
        //   LF  is the hard line break of a multi-line caption and of the
        //       StackedAbove atom label form (alt+enter in the guide).
        //   TAB is an inline wide space, not a layout instruction.
        // DEL and the remaining C0 controls have no meaning in a drawing and a
        // stray one is far more likely to be corruption than intent.
        if (cp < 32u && cp != 0x0Au && cp != 0x09u) return false;
        if (cp == 127u) return false;
        out->push_back(cp);
        if (u16Index) {
            u16Index->push_back(u16);
            u16 += (cp < 0x10000u) ? 1u : 2u;
        }
    }
    return true;
}
} // namespace

std::vector<uint16_t> utf8ToUtf16(const std::string& utf8) {
    std::vector<uint16_t> out;
    for (size_t i = 0; i < utf8.size();) {
        const unsigned char b = static_cast<unsigned char>(utf8[i++]);
        uint32_t cp = 0;
        unsigned extra = 0;
        if (b < 0x80u) { cp = b; }
        else if (b >= 0xC2u && b <= 0xDFu) { cp = b & 0x1Fu; extra = 1; }
        else if (b >= 0xE0u && b <= 0xEFu) { cp = b & 0x0Fu; extra = 2; }
        else if (b >= 0xF0u && b <= 0xF4u) { cp = b & 0x07u; extra = 3; }
        else { out.push_back(0xFFFDu); continue; }
        bool bad = false;
        for (unsigned k = 0; k < extra; ++k) {
            if (i >= utf8.size()) { bad = true; break; }
            const unsigned char c = static_cast<unsigned char>(utf8[i++]);
            if ((c & 0xC0u) != 0x80u) { bad = true; break; }
            cp = (cp << 6) | (c & 0x3Fu);
        }
        if (bad || cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu)) {
            out.push_back(0xFFFDu);
            continue;
        }
        if (cp < 0x10000u) {
            out.push_back(static_cast<uint16_t>(cp));
        } else {
            cp -= 0x10000u;
            out.push_back(static_cast<uint16_t>(0xD800u + (cp >> 10)));
            out.push_back(static_cast<uint16_t>(0xDC00u + (cp & 0x3FFu)));
        }
    }
    return out;
}

std::string utf16ToUtf8(const std::vector<uint16_t>& units) {
    std::string out;
    for (size_t i = 0; i < units.size(); ++i) {
        uint32_t cp = units[i];
        if (cp >= 0xD800u && cp <= 0xDBFFu && i + 1 < units.size() &&
            units[i + 1] >= 0xDC00u && units[i + 1] <= 0xDFFFu) {
            cp = 0x10000u + ((cp - 0xD800u) << 10) + (units[i + 1] - 0xDC00u);
            ++i;
        } else if (cp >= 0xD800u && cp <= 0xDFFFu) {
            cp = 0xFFFDu;
        }
        if (cp < 0x80u) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800u) {
            out += static_cast<char>(0xC0u | (cp >> 6));
            out += static_cast<char>(0x80u | (cp & 0x3Fu));
        } else if (cp < 0x10000u) {
            out += static_cast<char>(0xE0u | (cp >> 12));
            out += static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu));
            out += static_cast<char>(0x80u | (cp & 0x3Fu));
        } else {
            out += static_cast<char>(0xF0u | (cp >> 18));
            out += static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu));
            out += static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu));
            out += static_cast<char>(0x80u | (cp & 0x3Fu));
        }
    }
    return out;
}

uint32_t utf8Length(const std::string& utf8) {
    std::vector<uint32_t> cps;
    if (!decodeStrict(utf8, &cps, 0)) return 0;
    return static_cast<uint32_t>(cps.size());
}

uint32_t cpToU16(const std::string& utf8, uint32_t cpIndex) {
    std::vector<uint32_t> cps, idx;
    if (!decodeStrict(utf8, &cps, &idx)) return cpIndex;
    if (cpIndex >= idx.size()) {
        uint32_t total = 0;
        for (size_t i = 0; i < cps.size(); ++i) total += (cps[i] < 0x10000u) ? 1u : 2u;
        return total;
    }
    return idx[cpIndex];
}

uint32_t u16ToCp(const std::string& utf8, uint32_t u16Index) {
    std::vector<uint32_t> cps, idx;
    if (!decodeStrict(utf8, &cps, &idx)) return u16Index;
    for (size_t i = 0; i < idx.size(); ++i) {
        const uint32_t width = (cps[i] < 0x10000u) ? 1u : 2u;
        if (u16Index < idx[i] + width) return static_cast<uint32_t>(i);
    }
    return static_cast<uint32_t>(cps.size());
}

// ===========================================================================
// Layout internals
// ===========================================================================
namespace {

inline bool isSpaceCp(uint32_t cp) { return cp == 0x20u || cp == 0x09u; }
inline bool isBreakCp(uint32_t cp) { return cp == 0x0Au; }
inline bool isBlankCp(uint32_t cp) { return isSpaceCp(cp) || isBreakCp(cp); }
inline uint32_t u16WidthOf(uint32_t cp) { return cp < 0x10000u ? 1u : 2u; }
inline bool isAsciiDigit(uint32_t cp) { return cp >= 0x30u && cp <= 0x39u; }
inline bool isAsciiLetter(uint32_t cp) {
    return (cp >= 0x41u && cp <= 0x5Au) || (cp >= 0x61u && cp <= 0x7Au);
}

bool isCjk(uint32_t cp) {
    return (cp >= 0x1100u && cp <= 0x11FFu)
        || (cp >= 0x2E80u && cp <= 0x303Fu)
        || (cp >= 0x3040u && cp <= 0x30FFu)
        || (cp >= 0x3100u && cp <= 0x312Fu)
        || (cp >= 0x3130u && cp <= 0x318Fu)
        || (cp >= 0x3400u && cp <= 0x4DBFu)
        || (cp >= 0x4E00u && cp <= 0x9FFFu)
        || (cp >= 0xA000u && cp <= 0xA4CFu)
        || (cp >= 0xAC00u && cp <= 0xD7AFu)
        || (cp >= 0xF900u && cp <= 0xFAFFu)
        || (cp >= 0x20000u && cp <= 0x2FA1Fu);
}

bool isChargeCp(uint32_t cp) {
    return cp == 0x2Bu || cp == 0x2Du || cp == 0xB1u || cp == 0x2212u;
}

// Automatic (Formula-style) script classification.
//   superscript if a charge character follows the digit run
//   superscript if an element symbol follows and a letter or the string start
//     precedes (the isotope-prefix form)
//   subscript otherwise
int classifyDigits(const std::vector<uint32_t>& cps, size_t begin, size_t end) {
    if (end < cps.size() && isChargeCp(cps[end])) return 1;
    if (end < cps.size() && isAsciiLetter(cps[end])) {
        if (begin == 0) return 1;
        if (!isAsciiLetter(cps[begin - 1]) && !isAsciiDigit(cps[begin - 1])) return 1;
    }
    return -1;
}

void addFamily(std::vector<std::string>* list, const std::string& f) {
    if (f.empty()) return;
    for (size_t i = 0; i < list->size(); ++i) if ((*list)[i] == f) return;
    list->push_back(f);
}

struct Unit {
    uint32_t cp;
    uint32_t cpIndex;
    uint32_t u16Index;
    uint32_t u16Width;
    size_t   runIndex;
    FontSpec font;
    double   size;          // effective, document units
    int      script;        // -1 sub, 0 normal, +1 super
    bool     space;
    bool     missing;
    double   width;
    double   inkX0, inkY0, inkX1, inkY1;   // baseline-relative, y up
    bool     hasInk;
    uint16_t flags;
    double   maxAscent, maxDescent;        // base-size line extents, document units
    // The glyph outline in EM units, copied from the cache at decode time.  It is
    // held here so the emission pass never re-enters the cache: one font read per
    // (family, weight, style, code point) per layout, and none at draw time.
    std::vector<std::vector<std::vector<float> > > contours;
};

struct Line {
    std::vector<size_t> units;
    double width;
    double maxAscent, maxDescent;
    double inkLeft, inkRight;
    bool   hasInk;
    bool   hardBreak;
    uint32_t cpBegin, cpEnd, u16Begin, u16End;
};

} // namespace

core::Error layoutRichText(const std::vector<core::TextRun>& runs,
                           const core::Vec2& anchor,
                           const LayoutOptions& options,
                           GlyphCache* cache,
                           RichLayout* out) {
    if (!out) return core::Error(core::ErrorCode::InvalidArgument, "null layout output");
    if (cache == 0) return core::Error(core::ErrorCode::InvalidArgument, "null glyph cache");
    if (!anchor.finite())
        return core::Error(core::ErrorCode::InvalidGeometry, "non-finite text anchor");
    if (!std::isfinite(options.defaultSize) || options.defaultSize <= 0.0 ||
        options.defaultSize > 1e7)
        return core::Error(core::ErrorCode::InvalidArgument, "text size out of range");
    if (!std::isfinite(options.widthLimit) || options.widthLimit < 0.0)
        return core::Error(core::ErrorCode::InvalidArgument, "invalid width limit");
    if (!std::isfinite(options.lineHeightFactor) || options.lineHeightFactor <= 0.0)
        return core::Error(core::ErrorCode::InvalidArgument, "invalid line height factor");

    RichLayout result;
    result.anchor = anchor;

    // ---- 1./2. decode each run and resolve per-code-point style -------------
    std::vector<Unit> units;
    for (size_t r = 0; r < runs.size(); ++r) {
        const core::TextRun& run = runs[r];
        std::vector<uint32_t> cps, u16idx;
        if (!decodeStrict(run.utf8, &cps, &u16idx))
            return core::Error(core::ErrorCode::InvalidText,
                               "invalid UTF-8 or control character in text run");

        const FontSpec font(run.fontName.empty() ? options.defaultFont : run.fontName,
                            (run.flags & core::kBold) != 0,
                            (run.flags & core::kItalic) != 0);
        addFamily(&result.familiesUsed, font.family);
        double size = run.size > 0.0 ? run.size : options.defaultSize;
        if (!std::isfinite(size) || size <= 0.0) size = options.defaultSize;

        LineMetrics lm;
        cache->lineMetrics(font, &lm);
        const double ascentEm = (lm.ascent > 0.0) ? lm.ascent : 0.8;
        const double descentEm = (lm.descent > 0.0) ? lm.descent : 0.2;

        const uint16_t flags = run.flags ? run.flags : options.defaultFlags;
        const bool formula = (flags & core::kFormula) != 0;

        for (size_t c = 0; c < cps.size(); ++c) {
            const uint32_t cp = cps[c];
            int script = 0;
            if (flags & core::kSuperscript) script = 1;
            else if (flags & core::kSubscript) script = -1;
            if (formula && isAsciiDigit(cp)) {
                size_t e = c;
                while (e < cps.size() && isAsciiDigit(cps[e])) ++e;
                script = classifyDigits(cps, c, e);
            }

            Unit u;
            u.cp = cp;
            u.cpIndex = static_cast<uint32_t>(c);
            u.u16Index = u16idx[c];
            u.u16Width = (cp < 0x10000u) ? 1u : 2u;
            u.runIndex = r;
            u.font = font;
            u.script = script;
            u.space = isSpaceCp(cp);
            u.size = (script != 0) ? size * options.scriptScale : size;
            u.flags = flags;
            u.maxAscent = ascentEm * size;
            u.maxDescent = descentEm * size;

            bool hardFailure = false;
            Glyph g;
            cache->glyphCopy(cp, font, &g, &hardFailure);
            if (hardFailure)
                return core::Error(core::ErrorCode::Unsupported,
                                   "glyph provider could not supply a font");
            u.missing = g.missing;
            u.width = g.advance * u.size;
            u.hasInk = g.hasInk;
            u.inkX0 = g.x0 * u.size;
            u.inkY0 = g.y0 * u.size;
            u.inkX1 = g.x1 * u.size;
            u.inkY1 = g.y1 * u.size;
            if (g.blank) u.hasInk = false;
            u.contours = g.contours;
            units.push_back(u);
        }
    }

    if (units.empty()) {
        out->lines.clear();
        out->glyphs.clear();
        out->decorations.clear();
        out->metrics = LayoutMetrics();
        out->anchor = anchor;
        out->familiesUsed = result.familiesUsed;
        return core::Error();
    }

    // ---- 3. tokens ----------------------------------------------------------
    enum TokenKind { kTokenWord = 0, kTokenSpace, kTokenBreak };
    struct Token {
        size_t begin, end;      // unit index range [begin,end)
        double width;
        uint8_t kind;
        bool   breakBeforeCjk;
    };
    std::vector<Token> tokens;
    {
        size_t i = 0;
        while (i < units.size()) {
            Token t;
            t.begin = i;
            t.kind = kTokenWord;
            t.breakBeforeCjk = false;
            if (isBreakCp(units[i].cp)) {
                ++i;
                t.kind = kTokenBreak;
            } else if (units[i].space) {
                while (i < units.size() && units[i].space) ++i;
                t.kind = kTokenSpace;
            } else if (isCjk(units[i].cp)) {
                ++i;
            } else {
                while (i < units.size() && !isBlankCp(units[i].cp) &&
                       !isCjk(units[i].cp) && units[i].cp != 0x2Du)
                    ++i;
                if (i < units.size() && units[i].cp == 0x2Du) ++i;   // keep the hyphen
            }
            t.end = i;
            double w = 0.0;
            for (size_t k = t.begin; k < t.end; ++k) w += units[k].width;
            t.width = w;
            if (!tokens.empty() && tokens.back().kind != kTokenBreak) {
                const uint32_t firstCp = units[t.begin].cp;
                const uint32_t prevCp = units[t.begin - 1].cp;
                t.breakBeforeCjk = isCjk(firstCp) || isCjk(prevCp);
            }
            tokens.push_back(t);
        }
    }

    // ---- 4. line breaking ---------------------------------------------------
    std::vector<Line> lines;
    {
        Line cur;
        double width = 0.0;
        bool haveContent = false;
        size_t pendingSpaceBegin = 0, pendingSpaceEnd = 0;
        bool havePendingSpace = false;
        for (size_t ti = 0; ti < tokens.size(); ++ti) {
            const Token& t = tokens[ti];
            if (t.kind == kTokenBreak) {
                // A hard line break (LF).  It closes the current line even when
                // the line is empty, which is what makes a blank line visible in
                // a multi-line caption instead of being collapsed away.
                cur.hardBreak = true;
                lines.push_back(cur);
                cur = Line();
                width = 0.0;
                haveContent = false;
                havePendingSpace = false;
                continue;
            }
            if (t.kind == kTokenSpace) {
                if (haveContent) { pendingSpaceBegin = t.begin; pendingSpaceEnd = t.end; havePendingSpace = true; }
                continue;
            }
            const bool canBreak = haveContent && options.widthLimit > 0.0 &&
                                  (width + (havePendingSpace ? (units[t.begin - 1].width) : 0.0) + t.width) >
                                  options.widthLimit;
            if (canBreak) {
                cur.hardBreak = false;
                lines.push_back(cur);
                cur = Line();
                width = 0.0;
                haveContent = false;
                havePendingSpace = false;
            }
            if (havePendingSpace && haveContent) {
                for (size_t k = pendingSpaceBegin; k < pendingSpaceEnd; ++k) {
                    cur.units.push_back(k);
                    width += units[k].width;
                }
            }
            havePendingSpace = false;
            for (size_t k = t.begin; k < t.end; ++k) {
                cur.units.push_back(k);
                width += units[k].width;
            }
            haveContent = true;
        }
        cur.hardBreak = true;
        lines.push_back(cur);
    }

    // ---- 5. per-line run records, metrics and ink ---------------------------
    for (size_t li = 0; li < lines.size(); ++li) {
        Line& line = lines[li];
        line.width = 0.0;
        line.maxAscent = 0.0;
        line.maxDescent = 0.0;
        line.inkLeft = 0.0;
        line.inkRight = 0.0;
        line.hasInk = false;
        if (line.units.empty()) continue;

        line.cpBegin = units[line.units.front()].cpIndex;
        {
            const Unit& last = units[line.units.back()];
            line.cpEnd = last.cpIndex + 1;
            line.u16Begin = units[line.units.front()].u16Index;
            line.u16End = last.u16Index + last.u16Width;
        }

        double x = 0.0;
        size_t k = 0;
        while (k < line.units.size()) {
            const size_t begin = k;
            const size_t runIndex = units[line.units[k]].runIndex;
            while (k < line.units.size() && units[line.units[k]].runIndex == runIndex) ++k;
            for (size_t j = begin; j < k; ++j) {
                const Unit& u = units[line.units[j]];
                const int script = u.script;
                const double shift = (script > 0) ? (u.size * options.superBaselineShift)
                                   : (script < 0) ? (-u.size * options.subBaselineShift)
                                                  : 0.0;
                if (shift + u.maxAscent > line.maxAscent) line.maxAscent = shift + u.maxAscent;
                if (-shift + u.maxDescent > line.maxDescent) line.maxDescent = -shift + u.maxDescent;
                if (u.hasInk) {
                    const double ix0 = x + u.inkX0;
                    const double ix1 = x + u.inkX1;
                    if (!line.hasInk) { line.inkLeft = ix0; line.inkRight = ix1; line.hasInk = true; }
                    else {
                        if (ix0 < line.inkLeft) line.inkLeft = ix0;
                        if (ix1 > line.inkRight) line.inkRight = ix1;
                    }
                }
                x += u.width;
            }
        }
        line.width = x;
    }

    // ---- 6. baselines -------------------------------------------------------
    double uniformAscent = 0.0, uniformDescent = 0.0;
    for (size_t li = 0; li < lines.size(); ++li) {
        if (lines[li].maxAscent > uniformAscent) uniformAscent = lines[li].maxAscent;
        if (lines[li].maxDescent > uniformDescent) uniformDescent = lines[li].maxDescent;
    }
    if (!(uniformAscent > 0.0)) uniformAscent = options.defaultSize * 0.8;
    if (!(uniformDescent > 0.0)) uniformDescent = options.defaultSize * 0.2;

    std::vector<double> lineTop(lines.size(), 0.0), lineBottom(lines.size(), 0.0),
                        lineBaseline(lines.size(), 0.0);
    {
        double y = 0.0;
        for (size_t li = 0; li < lines.size(); ++li) {
            Line& line = lines[li];
            double asc = (options.spacing == LineSpacing::Variable) ? line.maxAscent : uniformAscent;
            double desc = (options.spacing == LineSpacing::Variable) ? line.maxDescent : uniformDescent;
            if (options.spacing == LineSpacing::Fixed && options.fixedLineHeight > 0.0) {
                const double total = options.fixedLineHeight;
                const double natural = asc + desc;
                if (natural > 0.0) { asc = total * (asc / natural); desc = total - asc; }
                else { asc = total; desc = 0.0; }
            } else {
                asc *= options.lineHeightFactor;
                desc *= options.lineHeightFactor;
            }
            if (!(asc > 0.0)) asc = options.defaultSize * 0.8;
            if (desc < 0.0) desc = 0.0;
            lineTop[li] = y;
            lineBaseline[li] = y + asc;
            lineBottom[li] = y + asc + desc;
            y = lineBottom[li];
        }
    }

    // ---- 7. anchor and alignment -------------------------------------------
    double blockWidth = 0.0;
    for (size_t li = 0; li < lines.size(); ++li)
        if (lines[li].width > blockWidth) blockWidth = lines[li].width;
    const double blockHeight = lines.empty() ? 0.0 : lineBottom.back();

    double blockInkLeft = 0.0, blockInkRight = blockWidth, blockInkTop = 0.0,
           blockInkBottom = blockHeight;
    {
        bool any = false;
        for (size_t li = 0; li < lines.size(); ++li) {
            if (!lines[li].hasInk) continue;
            if (!any) {
                blockInkLeft = lines[li].inkLeft;
                blockInkRight = lines[li].inkRight;
                blockInkTop = lineTop[li];
                blockInkBottom = lineBottom[li];
                any = true;
            } else {
                if (lines[li].inkLeft < blockInkLeft) blockInkLeft = lines[li].inkLeft;
                if (lines[li].inkRight > blockInkRight) blockInkRight = lines[li].inkRight;
                if (lineTop[li] < blockInkTop) blockInkTop = lineTop[li];
                if (lineBottom[li] > blockInkBottom) blockInkBottom = lineBottom[li];
            }
        }
        if (!any) { blockInkTop = 0.0; blockInkBottom = blockHeight; }
    }

    double offsetX = 0.0, offsetY = 0.0;
    switch (options.anchor) {
        case LayoutAnchor::CaptionTopLeft:
            break;
        case LayoutAnchor::CaptionCenter:
            offsetX = -0.5 * blockWidth;
            offsetY = -0.5 * blockHeight;
            break;
        case LayoutAnchor::AtomCenter:
        case LayoutAnchor::AtomLeft: {
            const double firstL = lines.empty() ? 0.0
                : (lines[0].hasInk ? lines[0].inkLeft : 0.0);
            const double firstR = lines.empty() ? 0.0
                : (lines[0].hasInk ? lines[0].inkRight : lines[0].width);
            offsetX = (options.anchor == LayoutAnchor::AtomCenter)
                ? (-0.5 * (firstL + firstR)) : (-firstL);
            offsetY = -0.5 * (lineTop[0] + lineBottom[0]);
            break;
        }
        case LayoutAnchor::AtomRight: {
            const double firstR = lines.empty() ? 0.0
                : (lines[0].hasInk ? lines[0].inkRight : lines[0].width);
            offsetX = -firstR;
            offsetY = -0.5 * (lineTop[0] + lineBottom[0]);
            break;
        }
        case LayoutAnchor::AtomAbove:
            offsetX = -0.5 * blockWidth;
            offsetY = -(blockHeight + 0.10 * options.defaultSize);
            break;
        case LayoutAnchor::AtomAutomatic:
            offsetX = -0.5 * (blockInkLeft + blockInkRight);
            offsetY = -0.5 * (blockInkTop + blockInkBottom);
            break;
    }

    std::vector<double> lineX(lines.size(), 0.0);
    for (size_t li = 0; li < lines.size(); ++li) {
        const double slack = blockWidth - lines[li].width;
        double x = offsetX;
        if (options.align == core::TextAlign::Center) x += 0.5 * slack;
        else if (options.align == core::TextAlign::Right) x += slack;
        lineX[li] = x;
    }

    // ---- 8. emit -----------------------------------------------------------
    double minX = 0.0, maxX = 0.0, minY = 0.0, maxY = 0.0;
    bool haveAnyInk = false;
    double firstBaseline = 0.0, lastBaseline = 0.0;
    size_t glyphCount = 0, missingCount = 0, triangulated = 0, windingFlips = 0;

    for (size_t li = 0; li < lines.size(); ++li) {
        const Line& line = lines[li];
        LineLayout outLine;
        outLine.cpBegin = line.cpBegin;
        outLine.cpEnd = line.cpEnd;
        outLine.u16Begin = line.u16Begin;
        outLine.u16End = line.u16End;
        outLine.x = lineX[li];
        outLine.width = line.width;
        outLine.baseline = lineBaseline[li] + offsetY;
        outLine.ascent = line.maxAscent;
        outLine.descent = line.maxDescent;
        outLine.top = lineTop[li] + offsetY;
        outLine.bottom = lineBottom[li] + offsetY;
        outLine.hardBreak = line.hardBreak;

        double pen = lineX[li];
        size_t k = 0;
        while (k < line.units.size()) {
            const size_t begin = k;
            const size_t runIndex = units[line.units[k]].runIndex;
            while (k < line.units.size() && units[line.units[k]].runIndex == runIndex) ++k;

            LineRun lr;
            lr.runIndex = static_cast<uint16_t>(runIndex);
            lr.cpBegin = units[line.units[begin]].cpIndex;
            lr.u16Begin = units[line.units[begin]].u16Index;
            lr.cpEnd = units[line.units[k - 1]].cpIndex + 1;
            lr.u16End = units[line.units[k - 1]].u16Index + units[line.units[k - 1]].u16Width;
            lr.flags = units[line.units[begin]].flags;
            lr.size = units[line.units[begin]].size;
            lr.x = pen;
            // A LineRun is the smallest unit the PUBLIC layout API exposes, but a
            // run may legitimately mix scripts inside itself (a formula run holds
            // "H4" and the 4 is a subscript).  The baseline shift is therefore
            // derived PER GLYPH below from that glyph is own script level and its
            // own size; the value stored on the LineRun is the one that applies to
            // the run is first glyph, for callers that only look at runs.
            {
                const int script0 = units[line.units[begin]].script;
                const double size0 = units[line.units[begin]].size;
                lr.baselineShift = (script0 > 0) ? (size0 * options.superBaselineShift)
                                 : (script0 < 0) ? (-size0 * options.subBaselineShift)
                                                 : 0.0;
            }

            double runInkL = 0.0, runInkR = 0.0;
            bool runInk = false;
            double prevAdvance = 0.0;
            uint32_t prevCp = 0;
            bool havePrev = false;
            for (size_t j = begin; j < k; ++j) {
                const Unit& u = units[line.units[j]];
                double kern = 0.0;
                if (options.applyKerning && havePrev && !u.space &&
                    !units[line.units[j - 1]].space && cache->provider()) {
                    // The provider states the kern as an EM FRACTION and
                    // LayoutOptions::maxKernFraction is the same unit, so the two are
                    // compared in em space and only then scaled to points.  Comparing a
                    // fraction against a point cap made the cap grow with the font size
                    // and let a 0.40 em kern through at size 10.
                    double kernEm = cache->provider()->kern(prevCp, u.cp, u.font);
                    const double capEm = options.maxKernFraction;
                    if (kernEm > capEm) kernEm = capEm;
                    if (kernEm < -capEm) kernEm = -capEm;
                    kern = kernEm * u.size;
                }
                const double gx = pen + kern;
                // Per-glyph script offset: a script digit is a fraction of ITS OWN
                // (already scaled) size, and the shift is applied to that glyph even
                // when the surrounding run sits on the baseline.
                const double glyphShift = (u.script > 0)
                    ? (u.size * options.superBaselineShift)
                    : (u.script < 0) ? (-u.size * options.subBaselineShift) : 0.0;
                const double gy = outLine.baseline - glyphShift;

                GlyphPlacement gp;
                gp.codepoint = u.cp;
                gp.font = u.font;
                gp.size = u.size;
                gp.x = gx;
                gp.y = gy;
                gp.advance = u.width;
                gp.runIndex = static_cast<uint16_t>(runIndex);
                gp.lineIndex = static_cast<uint16_t>(li);
                gp.cpIndex = u.cpIndex;
                gp.u16Index = u.u16Index;
                gp.missing = u.missing;
                if (u.hasInk) {
                    gp.inkX0 = gx + u.inkX0;
                    gp.inkX1 = gx + u.inkX1;
                    // inkY0/1 arrive as y-up; convert to layout space (y down).
                    gp.inkY0 = gy - u.inkY1;
                    gp.inkY1 = gy - u.inkY0;
                    if (gp.inkY1 < gp.inkY0) { const double t = gp.inkY0; gp.inkY0 = gp.inkY1; gp.inkY1 = t; }
                } else {
                    gp.inkX0 = gp.inkX1 = gx;
                    gp.inkY0 = gp.inkY1 = gy;
                }
                gp.inkBox = core::Box(core::Vec2(gp.inkX0, gp.inkY0),
                                      core::Vec2(gp.inkX1, gp.inkY1));
                // Map the outline into document space ONCE and triangulate it
                // here: the mirrored contours have the font's winding flipped,
                // and triangulating the mirrored form is what guarantees the
                // triangles agree with the contours under the NonZero rule.
                if (u.hasInk && !u.contours.empty()) {
                    for (size_t ci = 0; ci < u.contours.size(); ++ci) {
                        std::vector<core::Vec2> contour;
                        contour.reserve(u.contours[ci].size());
                        for (size_t pi = 0; pi < u.contours[ci].size(); ++pi) {
                            const double ox = u.contours[ci][pi][0] * u.size;
                            const double oy = u.contours[ci][pi][1] * u.size;
                            contour.push_back(core::Vec2(gx + ox, gy - oy));
                        }
                        if (contour.size() >= 3) gp.contours.push_back(contour);
                    }
                    if (!gp.contours.empty()) {
                        render::geom::Mesh glyphMesh;
                        const core::Error te = render::geom::triangulateContours(
                            gp.contours, r62::render::FillRule::NonZero, &glyphMesh);
                        if (te.ok()) {
                            gp.triangles = glyphMesh.triangles;
                            // One winding for every emitted triangle.  The merge
                            // into a single NonZero outline is only correct when
                            // triangles that share an interior edge traverse it in
                            // OPPOSITE directions, which cancellation requires; a
                            // mixed winding would leave a visible seam.  The flip
                            // is a swap of two vertices and changes no coverage.
                            for (size_t vi = 0; vi + 5 < gp.triangles.size(); vi += 6) {
                                const double ax = gp.triangles[vi], ay = gp.triangles[vi + 1];
                                const double bx = gp.triangles[vi + 2], by = gp.triangles[vi + 3];
                                const double cx = gp.triangles[vi + 4], cy = gp.triangles[vi + 5];
                                const double area2 = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
                                if (area2 < 0.0) {
                                    gp.triangles[vi + 2] = static_cast<float>(cx);
                                    gp.triangles[vi + 3] = static_cast<float>(cy);
                                    gp.triangles[vi + 4] = static_cast<float>(bx);
                                    gp.triangles[vi + 5] = static_cast<float>(by);
                                    ++windingFlips;
                                }
                            }
                            ++triangulated;
                        }
                    }
                }
                result.glyphs.push_back(gp);
                ++glyphCount;
                if (u.missing) ++missingCount;
                if (u.hasInk) {
                    if (!runInk) { runInkL = gp.inkX0; runInkR = gp.inkX1; runInk = true; }
                    else {
                        if (gp.inkX0 < runInkL) runInkL = gp.inkX0;
                        if (gp.inkX1 > runInkR) runInkR = gp.inkX1;
                    }
                    if (!haveAnyInk) {
                        minX = gp.inkX0; maxX = gp.inkX1; minY = gp.inkY0; maxY = gp.inkY1;
                        haveAnyInk = true;
                    } else {
                        if (gp.inkX0 < minX) minX = gp.inkX0;
                        if (gp.inkX1 > maxX) maxX = gp.inkX1;
                        if (gp.inkY0 < minY) minY = gp.inkY0;
                        if (gp.inkY1 > maxY) maxY = gp.inkY1;
                    }
                }
                pen += kern + u.width;
                prevCp = u.cp;
                prevAdvance = u.width;
                havePrev = true;
            }
            (void)prevAdvance;
            lr.advance = (k > begin) ? (pen - lr.x) : 0.0;
            lr.ascent = line.maxAscent;
            lr.descent = line.maxDescent;
            outLine.runs.push_back(lr);

            // Underline spans the run's INK, not its advance: an underline that
            // ran into trailing whitespace looks broken.
            if (runInk && (lr.flags & core::kUnderline)) {
                DecorationSpan d;
                d.kind = Decoration::Underline;
                d.x0 = runInkL;
                d.x1 = runInkR;
                d.y = outLine.baseline + 0.10 * lr.size;
                d.thickness = 0.06 * lr.size;
                result.decorations.push_back(d);
            }
        }
        if (li == 0) firstBaseline = outLine.baseline;
        lastBaseline = outLine.baseline;
        result.lines.push_back(outLine);
    }

    LayoutMetrics m;
    m.lineCount = result.lines.size();
    m.glyphCount = glyphCount;
    m.missingGlyphs = missingCount;
    m.triangulatedGlyphs = triangulated;
    m.windingFlips = windingFlips;
    m.decorationCount = result.decorations.size();
    m.firstBaseline = firstBaseline;
    m.lastBaseline = lastBaseline;
    m.width = blockWidth;
    m.height = blockHeight;
    m.ascent = firstBaseline;
    m.descent = blockHeight - lastBaseline;
    m.empty = (glyphCount == 0);
    core::Box layoutBox(core::Vec2(0, 0), core::Vec2(0, 0));
    if (haveAnyInk) {
        layoutBox = core::Box(core::Vec2(minX, minY), core::Vec2(maxX, maxY));
    } else {
        layoutBox = core::Box(core::Vec2(offsetX, offsetY),
                              core::Vec2(offsetX + blockWidth, offsetY + blockHeight));
    }
    m.layoutBox = layoutBox;
    m.inkBox = core::Box(core::Vec2(anchor.x + layoutBox.min.x, anchor.y + layoutBox.min.y),
                         core::Vec2(anchor.x + layoutBox.max.x, anchor.y + layoutBox.max.y));
    result.metrics = m;

    *out = result;
    return core::Error();
}

core::Error layoutPlainText(const std::string& utf8,
                            const core::Vec2& anchor,
                            const LayoutOptions& options,
                            GlyphCache* cache,
                            RichLayout* out) {
    std::vector<core::TextRun> runs(1);
    runs[0].utf8 = utf8;
    runs[0].flags = 0;
    runs[0].size = 0.0;          // inherit options.defaultSize
    runs[0].fontName = options.defaultFont;
    return layoutRichText(runs, anchor, options, cache, out);
}

// ===========================================================================
// Bounds
// ===========================================================================
core::Box layoutBounds(const RichLayout& layout) {
    if (layout.metrics.empty) {
        return core::Box(core::Vec2(layout.anchor.x, layout.anchor.y),
                         core::Vec2(layout.anchor.x, layout.anchor.y));
    }
    return layout.metrics.inkBox;
}

core::Box deviceBounds(const RichLayout& layout, const core::ViewTransform& view) {
    const core::Box b = layoutBounds(layout);
    core::Box out(view.toDevice(b.min), view.toDevice(b.max));
    if (out.max.x < out.min.x) { const double t = out.min.x; out.min.x = out.max.x; out.max.x = t; }
    if (out.max.y < out.min.y) { const double t = out.min.y; out.min.y = out.max.y; out.max.y = t; }
    return out;
}

// ===========================================================================
// Hit testing and caret geometry
// ===========================================================================
TextHit caretAt(const RichLayout& layout, uint16_t lineIndex, uint32_t u16Index,
                bool affinityEnd) {
    TextHit hit;
    if (layout.lines.empty()) return hit;
    size_t li = lineIndex;
    if (li >= layout.lines.size()) li = layout.lines.size() - 1;
    const LineLayout& line = layout.lines[li];
    hit.lineIndex = static_cast<uint16_t>(li);
    hit.y = line.baseline;

    uint32_t target = u16Index;
    if (target < line.u16Begin) target = line.u16Begin;
    if (target > line.u16End) target = line.u16End;
    hit.x = line.x + line.width;
    bool found = false;

    for (size_t gi = 0; gi < layout.glyphs.size() && !found; ++gi) {
        const GlyphPlacement& gp = layout.glyphs[gi];
        if (gp.lineIndex != li) continue;
        if (target <= gp.u16Index) {
            hit.runIndex = gp.runIndex;
            hit.cpIndex = gp.cpIndex;
            hit.u16Index = gp.u16Index;
            hit.x = gp.x;
            found = true;
            break;
        }
        if (target < gp.u16Index + u16WidthOf(gp.codepoint)) {
            hit.runIndex = gp.runIndex;
            hit.cpIndex = gp.cpIndex;
            hit.u16Index = gp.u16Index;
            hit.x = affinityEnd ? (gp.x + gp.advance) : gp.x;
            found = true;
            break;
        }
    }
    if (!found && !line.runs.empty()) {
        const LineRun& last = line.runs.back();
        hit.runIndex = last.runIndex;
        hit.cpIndex = last.cpEnd;
        hit.u16Index = line.u16End;
        hit.x = last.x + last.advance;
    }
    hit.insideLine = true;
    return hit;
}

TextHit caretAtCp(const RichLayout& layout, uint32_t cpIndex, bool affinityEnd) {
    TextHit hit;
    if (layout.lines.empty()) return hit;
    size_t li = 0;
    for (size_t i = 0; i < layout.lines.size(); ++i) {
        li = i;
        if (cpIndex < layout.lines[i].cpEnd) break;
    }
    const LineLayout& line = layout.lines[li];
    uint32_t u16 = line.u16End;
    for (size_t gi = 0; gi < layout.glyphs.size(); ++gi) {
        const GlyphPlacement& gp = layout.glyphs[gi];
        if (gp.lineIndex != li) continue;
        if (gp.cpIndex >= cpIndex) { u16 = gp.u16Index; break; }
        u16 = gp.u16Index + u16WidthOf(gp.codepoint);
    }
    return caretAt(layout, static_cast<uint16_t>(li), u16, affinityEnd);
}

TextHit hitTest(const RichLayout& layout, const LPoint& point) {
    TextHit best;
    if (layout.lines.empty()) return best;
    size_t bestLine = 0;
    double bestDist = 1e300;
    for (size_t li = 0; li < layout.lines.size(); ++li) {
        const double top = layout.lines[li].top;
        const double bottom = layout.lines[li].bottom;
        double d = 0.0;
        if (point.y < top) d = top - point.y;
        else if (point.y > bottom) d = point.y - bottom;
        if (d < bestDist) { bestDist = d; bestLine = li; }
    }
    const LineLayout& line = layout.lines[bestLine];
    best.lineIndex = static_cast<uint16_t>(bestLine);
    best.distance = bestDist;
    best.y = line.baseline;

    bool found = false;
    double bestDelta = 1e300;
    for (size_t gi = 0; gi < layout.glyphs.size(); ++gi) {
        const GlyphPlacement& gp = layout.glyphs[gi];
        if (gp.lineIndex != bestLine) continue;
        const double x0 = gp.x;
        const double x1 = gp.x + gp.advance;
        if (point.x >= x0 && point.x <= x1) {
            const bool after = point.x >= 0.5 * (x0 + x1);
            best.runIndex = gp.runIndex;
            best.cpIndex = gp.cpIndex + (after ? 1u : 0u);
            best.u16Index = gp.u16Index + (after ? u16WidthOf(gp.codepoint) : 0u);
            best.x = after ? x1 : x0;
            best.insideLine = true;
            found = true;
            break;
        }
        const double d = (point.x < x0) ? (x0 - point.x) : (point.x - x1);
        if (d < bestDelta) {
            bestDelta = d;
            best.runIndex = gp.runIndex;
            if (point.x < x0) {
                best.cpIndex = gp.cpIndex;
                best.u16Index = gp.u16Index;
                best.x = x0;
            } else {
                best.cpIndex = gp.cpIndex + 1u;
                best.u16Index = gp.u16Index + u16WidthOf(gp.codepoint);
                best.x = x1;
            }
        }
    }
    if (!found) best.insideLine = false;
    return best;
}

CaretRect lineBox(const RichLayout& layout, uint16_t lineIndex) {
    CaretRect r;
    r.x0 = r.y0 = r.x1 = r.y1 = 0.0;
    if (layout.lines.empty()) return r;
    size_t li = lineIndex;
    if (li >= layout.lines.size()) li = layout.lines.size() - 1;
    r.x0 = layout.lines[li].x;
    r.x1 = layout.lines[li].x + layout.lines[li].width;
    r.y0 = layout.lines[li].top;
    r.y1 = layout.lines[li].bottom;
    return r;
}

std::vector<CaretRect> selectionRects(const RichLayout& layout, uint32_t cpFrom,
                                      uint32_t cpTo) {
    std::vector<CaretRect> out;
    if (layout.lines.empty()) return out;
    if (cpFrom > cpTo) { const uint32_t t = cpFrom; cpFrom = cpTo; cpTo = t; }
    for (size_t li = 0; li < layout.lines.size(); ++li) {
        const LineLayout& line = layout.lines[li];
        const uint32_t a = cpFrom > line.cpBegin ? cpFrom : line.cpBegin;
        const uint32_t b = cpTo < line.cpEnd ? cpTo : line.cpEnd;
        if (a >= b) {
            // An empty line still gets a thin caret-width box so a selection
            // that covers it is visible.
            if (cpFrom <= line.cpBegin && cpTo >= line.cpEnd && line.cpBegin == line.cpEnd) {
                CaretRect r;
                r.x0 = line.x; r.x1 = line.x + 0.5; r.y0 = line.top; r.y1 = line.bottom;
                out.push_back(r);
            }
            continue;
        }
        const TextHit h0 = caretAtCp(layout, a, false);
        const TextHit h1 = caretAtCp(layout, b, true);
        CaretRect r;
        r.x0 = h0.x < h1.x ? h0.x : h1.x;
        r.x1 = h0.x < h1.x ? h1.x : h0.x;
        r.y0 = line.top;
        r.y1 = line.bottom;
        out.push_back(r);
    }
    return out;
}

} // namespace text
} // namespace r62
