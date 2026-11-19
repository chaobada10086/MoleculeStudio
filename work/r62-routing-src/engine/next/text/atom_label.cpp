// =============================================================================
// re_round62_viewer/next/text/atom_label.cpp
//
// T09 - atom label parsing and placement.  atom_label.hpp documents the
// behaviour implemented here and the T06 boundary.
// =============================================================================

#include "next/text/atom_label.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>

namespace r62 {
namespace text {

namespace {

void pushRun(std::vector<core::TextRun>* runs, const std::string& utf8, uint16_t flags) {
    if (utf8.empty()) return;
    core::TextRun r;
    r.utf8 = utf8;
    r.flags = flags;
    runs->push_back(r);
}

// Returns the byte length of a recognised tag at i, else 0.
size_t matchTag(const std::string& s, size_t i, bool* open, uint16_t* flag) {
    static const char* const names[] = { "b", "i", "u", "sub", "sup" };
    static const uint16_t flags[] = { core::kBold, core::kItalic, core::kUnderline,
                                      core::kSubscript, core::kSuperscript };
    if (i >= s.size() || s[i] != 0x3C) return 0;
    size_t j = i + 1;
    bool closing = false;
    if (j < s.size() && s[j] == 0x2F) { closing = true; ++j; }
    for (size_t k = 0; k < 5; ++k) {
        const size_t n = std::char_traits<char>::length(names[k]);
        if (j + n < s.size() && s.compare(j, n, names[k]) == 0 && s[j + n] == 0x3E) {
            *open = !closing;
            *flag = flags[k];
            return (closing ? 2u : 1u) + n + 1u;
        }
    }
    return 0;
}

size_t digitEnd(const std::string& s, size_t i) {
    while (i < s.size() && s[i] >= 0x30 && s[i] <= 0x39) ++i;
    return i;
}

std::string intToStr(int v) {
    std::ostringstream oss;
    oss << v;
    return oss.str();
}

bool runIsScript(const std::vector<core::TextRun>& runs, size_t i) {
    if (i >= runs.size()) return false;
    return (runs[i].flags & (core::kSubscript | core::kSuperscript)) != 0;
}

struct Attachment {
    size_t runIndex;
    size_t glyphBegin, glyphEnd;
    double inkX0, inkX1;
    bool   hasInk;
    Attachment() : runIndex(0), glyphBegin(0), glyphEnd(0), inkX0(0), inkX1(0),
                   hasInk(false) {}
};

// Group the laid-out glyphs back into ATTACHMENTS and index them.  An attachment
// is a run OR, inside a run, a maximal span of one SIZE.  The size split is what
// makes a formula run work: "H4" is ONE run whose 4 is rendered at 75%, and
// without the split the H and the count are a single attachment whose centre is
// neither the element nor the count.  The split is on the RESOLVED size rather
// than on a script flag because the layout is the only thing that knows how the
// formula-digit rule classified each glyph, and re-deriving that rule here would
// be a second implementation waiting to disagree.
void collectAttachments(const RichLayout& layout,
                        const std::vector<core::TextRun>& runs,
                        std::vector<Attachment>* out) {
    (void)runs;
    out->clear();
    size_t i = 0;
    while (i < layout.glyphs.size()) {
        Attachment at;
        at.runIndex = layout.glyphs[i].runIndex;
        at.glyphBegin = i;
        const double size0 = layout.glyphs[i].size;
        while (i < layout.glyphs.size() &&
               layout.glyphs[i].runIndex == at.runIndex &&
               std::fabs(layout.glyphs[i].size - size0) < 1e-9) ++i;
        at.glyphEnd = i;
        out->push_back(at);
    }
    for (size_t a = 0; a < out->size(); ++a) {
        Attachment& at = (*out)[a];
        bool any = false;
        for (size_t g = at.glyphBegin; g < at.glyphEnd && g < layout.glyphs.size(); ++g) {
            const GlyphPlacement& gp = layout.glyphs[g];
            if (gp.inkX0 == gp.inkX1) continue;
            if (!any) { at.inkX0 = gp.inkX0; at.inkX1 = gp.inkX1; any = true; }
            else {
                if (gp.inkX0 < at.inkX0) at.inkX0 = gp.inkX0;
                if (gp.inkX1 > at.inkX1) at.inkX1 = gp.inkX1;
            }
        }
        at.hasInk = any;
    }
}

// Which side of the atom the non-first tokens (H, count, charge) go on.  The
// direction flags are DERIVED from the angles here, so a caller that filled in
// only bondAnglesDeg still gets the documented avoidance: a host that computes
// bond geometry naturally HAS angles and not booleans, and demanding both would
// make this API easy to use subtly wrong.
LabelSide chooseSide(const LabelSpec& spec, const LabelAnchorContext* ctx) {
    if (spec.side != LabelSide::Auto) return spec.side;
    if (!ctx || ctx->bondCount <= 0) return LabelSide::Right;
    const_cast<LabelAnchorContext*>(ctx)->inferFromAngles();
    if (ctx->hasBondRight && !ctx->hasBondLeft) return LabelSide::Left;
    if (ctx->hasBondLeft && !ctx->hasBondRight) return LabelSide::Right;
    if (ctx->hasBondBelow && !ctx->hasBondAbove) return LabelSide::Above;
    if (ctx->hasBondAbove && !ctx->hasBondBelow) return LabelSide::Below;
    return LabelSide::Above;
}

} // namespace

std::string stripLabelMarkup(const std::string& utf8) {
    std::string out;
    for (size_t i = 0; i < utf8.size();) {
        bool open = false;
        uint16_t flag = 0;
        const size_t n = matchTag(utf8, i, &open, &flag);
        if (n) { i += n; continue; }
        out += utf8[i++];
    }
    return out;
}

void LabelAnchorContext::inferFromAngles() {
    hasBondRight = hasBondLeft = hasBondAbove = hasBondBelow = false;
    const int n = bondCount > 8 ? 8 : (bondCount < 0 ? 0 : bondCount);
    for (int i = 0; i < n; ++i) {
        const double rad = bondAnglesDeg[i] * 3.14159265358979323846 / 180.0;
        const double c = std::cos(rad);
        const double s = -std::sin(rad);
        if (c > 0.5) hasBondRight = true;
        if (c < -0.5) hasBondLeft = true;
        if (s > 0.5) hasBondBelow = true;
        if (s < -0.5) hasBondAbove = true;
    }
}

void buildLabelRuns(const LabelSpec& spec, std::vector<core::TextRun>* out) {
    if (!out) return;
    const uint16_t baseFlags = static_cast<uint16_t>(
        (spec.bold ? core::kBold : 0) | (spec.italic ? core::kItalic : 0) |
        (spec.underline ? core::kUnderline : 0));

    if (!spec.prefix.empty()) pushRun(out, spec.prefix, baseFlags);
    if (spec.atomicNumber > 0) pushRun(out, intToStr(spec.atomicNumber), baseFlags);
    if (spec.isotope > 0) {
        core::TextRun r;
        r.utf8 = intToStr(spec.isotope);
        r.flags = static_cast<uint16_t>(baseFlags | core::kSuperscript);
        out->push_back(r);
    }
    {
        core::TextRun r;
        r.utf8 = spec.element;
        r.flags = baseFlags;
        out->push_back(r);
    }
    if (spec.hydrogenCount > 0) {
        core::TextRun r;
        r.utf8 = "H" + intToStr(spec.hydrogenCount);
        r.flags = static_cast<uint16_t>(baseFlags | core::kFormula);
        out->push_back(r);
    }
    if (spec.radicalDot) {
        core::TextRun r;
        r.utf8 = "\xE2\x80\xA2";
        r.flags = static_cast<uint16_t>(baseFlags | core::kSuperscript);
        out->push_back(r);
    }
    if (spec.charge != 0) {
        core::TextRun r;
        const std::string magnitude = (spec.charge == 1 || spec.charge == -1)
            ? std::string() : intToStr(spec.charge > 0 ? spec.charge : -spec.charge);
        r.utf8 = magnitude + (spec.charge > 0 ? "+" : "\xE2\x88\x92");
        r.flags = static_cast<uint16_t>(baseFlags | core::kSuperscript);
        out->push_back(r);
    }
    if (!spec.suffix.empty()) pushRun(out, spec.suffix, baseFlags);
}

LabelParse parseLabel(const std::string& utf8) {
    LabelParse result;
    result.plain = stripLabelMarkup(utf8);
    result.spec.formulaStyle = false;

    // ---- explicit markup path ---------------------------------------------
    bool anyTag = false;
    {
        std::string text;
        uint16_t flags = 0;
        std::vector<core::TextRun> runs;
        for (size_t i = 0; i < utf8.size();) {
            bool open = false;
            uint16_t flag = 0;
            const size_t n = matchTag(utf8, i, &open, &flag);
            if (n) {
                anyTag = true;
                pushRun(&runs, text, flags);
                text.clear();
                if (open) flags = static_cast<uint16_t>(flags | flag);
                else flags = static_cast<uint16_t>(flags & ~flag);
                i += n;
                continue;
            }
            if (utf8[i] == 0x3C) {
                if (result.warning.empty())
                    result.warning = "unrecognised markup kept literally";
                ++result.warnings;
            }
            text += utf8[i++];
        }
        pushRun(&runs, text, flags);
        if (anyTag) {
            result.runs = runs;
            result.spec.element = result.plain;
            return result;
        }
    }

    // ---- implicit formula path --------------------------------------------
    const std::string& s = utf8;
    size_t i = 0;
    int isotope = 0;
    bool haveIsotope = false;
    if (i < s.size() && s[i] >= 0x30 && s[i] <= 0x39) {
        const size_t e = digitEnd(s, i);
        if (e < s.size() && s[e] >= 0x41 && s[e] <= 0x5A) {
            isotope = std::atoi(s.substr(i, e - i).c_str());
            haveIsotope = isotope > 0;
            i = e;
        }
    }
    std::string element;
    if (i < s.size() && s[i] >= 0x41 && s[i] <= 0x5A) {
        element += s[i++];
        while (i < s.size() && s[i] >= 0x61 && s[i] <= 0x7A) element += s[i++];
    }
    std::string rest;
    while (i < s.size()) rest += s[i++];

    int hydrogen = -1;
    int charge = 0;
    if (!element.empty() && !rest.empty() && rest[0] == 0x48) {
        const size_t e = digitEnd(rest, 1);
        if (e > 1) {
            hydrogen = std::atoi(rest.substr(1, e - 1).c_str());
            rest = rest.substr(e);
        }
    }
    if (!rest.empty()) {
        const char c = rest[0];
        if (c == 0x2B || c == 0x2D) {
            charge = (c == 0x2B) ? 1 : -1;
            rest = rest.substr(1);
        } else if (c >= 0x30 && c <= 0x39) {
            const size_t e = digitEnd(rest, 0);
            if (e < rest.size() && (rest[e] == 0x2B || rest[e] == 0x2D)) {
                const int magnitude = std::atoi(rest.substr(0, e).c_str());
                charge = (rest[e] == 0x2B) ? magnitude : -magnitude;
                rest = rest.substr(e + 1);
            }
        }
    }

    if (element.empty() && !haveIsotope) {
        // Nothing recognisable: keep the raw text as one run so a user label is
        // never silently lost.
        pushRun(&result.runs, utf8, 0);
        result.spec.element = utf8;
        return result;
    }
    if (element.empty()) element = result.plain;
    result.spec.element = element;
    result.spec.isotope = haveIsotope ? isotope : 0;
    result.spec.charge = charge;
    result.spec.hydrogenCount = hydrogen;
    result.spec.formulaStyle = true;
    if (!rest.empty()) result.spec.suffix = rest;
    buildLabelRuns(result.spec, &result.runs);
    return result;
}

core::Error layoutAtomLabel(const LabelSpec& spec,
                            const core::Vec2& atomPosition,
                            const LayoutOptions& options,
                            GlyphCache* cache,
                            const LabelAnchorContext* ctx,
                            RichLayout* out) {
    if (!out) return core::Error(core::ErrorCode::InvalidArgument, "null layout output");

    std::vector<core::TextRun> runs;
    buildLabelRuns(spec, &runs);
    if (runs.empty()) {
        out->lines.clear();
        out->glyphs.clear();
        out->decorations.clear();
        out->metrics = LayoutMetrics();
        out->anchor = atomPosition;
        return core::Error();
    }

    LayoutOptions opts = options;
    if (spec.size > 0.0) opts.defaultSize = spec.size;
    if (!spec.font.empty()) opts.defaultFont = spec.font;
    opts.widthLimit = 0.0;
    opts.anchor = LayoutAnchor::CaptionTopLeft;
    opts.align = core::TextAlign::Left;

    RichLayout layout;
    core::Error e = layoutRichText(runs, core::Vec2(0.0, 0.0), opts, cache, &layout);
    if (!e.ok()) return e;
    if (layout.glyphs.empty()) { layout.anchor = atomPosition; *out = layout; return core::Error(); }

    std::vector<Attachment> attachments;
    collectAttachments(layout, runs, &attachments);

    // The attach token is the first NON-script attachment: with an isotope the
    // script run precedes the element, and the element is what must sit on the
    // atom.
    size_t attach = 0;
    for (size_t a = 0; a < attachments.size(); ++a) {
        if (!runIsScript(runs, attachments[a].runIndex)) { attach = a; break; }
        attach = a;
    }

    LabelSide side = chooseSide(spec, ctx);
    const double baseSize = spec.size > 0.0 ? spec.size : options.defaultSize;
    const double gap = std::max(0.04 * baseSize, 0.02 * options.defaultSize);

    std::vector<double> dx(attachments.size(), 0.0);
    std::vector<double> dy(attachments.size(), 0.0);
    const double attachL = attachments[attach].hasInk ? attachments[attach].inkX0 : 0.0;
    const double attachR = attachments[attach].hasInk ? attachments[attach].inkX1 : 0.0;
    // The ELEMENT stays on the atom, always.  That is the invariant the whole
    // module exists to keep: a bond is drawn to the atom position, and a label whose
    // element drifted off the atom would leave the bond attached to nothing.  The
    // SIDE choice therefore governs where the OTHER tokens go (and, for a stacked
    // side, that they move to another row), never where the element goes.
    {
        // Everything before the attach token (a leading isotope) goes LEFT.
        double cursorLeft = attachL;
        for (size_t a = 0; a < attach; ++a) {
            const double e1 = attachments[a].hasInk ? attachments[a].inkX1 : cursorLeft;
            dx[a] = (cursorLeft - gap) - e1;
            const double e0 = attachments[a].hasInk ? attachments[a].inkX0 : cursorLeft;
            cursorLeft = e0 + dx[a];
        }
        // Everything after the attach token goes to the chosen side.  The cursor
        // for that side is the one that must move: a left-side label grows to the
        // LEFT of the element (a bond occupies the right), and using the right-hand
        // cursor here is what silently put an NH4+ to the right of its own bond.
        double cursorRight = attachR;
        for (size_t a = attach + 1; a < attachments.size(); ++a) {
            if (side == LabelSide::Left) {
                dx[a] = (cursorLeft - gap) -
                        (attachments[a].hasInk ? attachments[a].inkX1 : cursorLeft);
                cursorLeft = (attachments[a].hasInk ? attachments[a].inkX0 + dx[a]
                                                    : cursorLeft - gap);
            } else {
                dx[a] = (cursorRight + gap) -
                        (attachments[a].hasInk ? attachments[a].inkX0 : cursorRight);
                cursorRight = (attachments[a].hasInk ? attachments[a].inkX1 + dx[a]
                                                     : cursorRight + gap);
            }
        }
    }

    // A stacked side (Above/Below) puts the extra tokens on a second visual
    // line: they keep their own x placement but move to another baseline row.
    const bool stacked = (side == LabelSide::Above || side == LabelSide::Below);
    const double rowShift = stacked ? (1.05 * baseSize) : 0.0;
    const double rowSign = (side == LabelSide::Above) ? 1.0 : -1.0;
    // A stacked token must not be moved by the horizontal cursor as well.
    if (stacked) {
        for (size_t a = 0; a < attachments.size(); ++a) if (a != attach) dx[a] = 0.0;
        // The vertical shift belongs to the whole label, so it cancels out in the
        // later centring step; keep dy clear for the stacked case and let rowShift
        // do the work.
        dy[attach] = 0.0;
    }

    for (size_t a = 0; a < attachments.size(); ++a) {
        const bool extra = (a != attach);
        const bool stackedRun = stacked && extra;
        for (size_t g = attachments[a].glyphBegin;
             g < attachments[a].glyphEnd && g < layout.glyphs.size(); ++g) {
            GlyphPlacement& gp = layout.glyphs[g];
            if (stackedRun) {
                // Centre the stacked token on the element token, one row above
                // (or below) it.  docY grows downward, so "above" subtracts.
                const double tokenCentre = 0.5 * (attachL + attachR);
                const double tokenW = attachments[a].hasInk
                    ? (attachments[a].inkX1 - attachments[a].inkX0) : 0.0;
                const double target = tokenCentre - 0.5 * tokenW;
                const double shift = target - (attachments[a].hasInk ? attachments[a].inkX0 : 0.0);
                gp.x += shift;
                gp.inkX0 += shift;
                gp.inkX1 += shift;
                gp.y -= rowSign * rowShift;
                gp.inkY0 -= rowSign * rowShift;
                gp.inkY1 -= rowSign * rowShift;
            } else if (dx[a] != 0.0 || dy[a] != 0.0) {
                gp.x += dx[a];
                gp.inkX0 += dx[a];
                gp.inkX1 += dx[a];
                gp.y += dy[a];
                gp.inkY0 += dy[a];
                gp.inkY1 += dy[a];
            }
        }
    }

    // The attach token ink centre goes on the atom.
    double centreX = 0.0, centreY = 0.0;
    {
        bool any = false;
        double x0 = 0, x1 = 0, y0 = 0, y1 = 0;
        for (size_t g = attachments[attach].glyphBegin;
             g < attachments[attach].glyphEnd && g < layout.glyphs.size(); ++g) {
            const GlyphPlacement& gp = layout.glyphs[g];
            if (gp.inkX0 == gp.inkX1) continue;
            if (!any) { x0 = gp.inkX0; x1 = gp.inkX1; y0 = gp.inkY0; y1 = gp.inkY1; any = true; }
            else {
                if (gp.inkX0 < x0) x0 = gp.inkX0;
                if (gp.inkX1 > x1) x1 = gp.inkX1;
                if (gp.inkY0 < y0) y0 = gp.inkY0;
                if (gp.inkY1 > y1) y1 = gp.inkY1;
            }
        }
        if (any) { centreX = 0.5 * (x0 + x1); centreY = 0.5 * (y0 + y1); }
        else if (!layout.lines.empty()) {
            centreX = layout.lines[0].width * 0.5;
            centreY = 0.5 * (layout.lines[0].top + layout.lines[0].bottom);
        }
    }
    const double tx = atomPosition.x - centreX;
    const double ty = atomPosition.y - centreY;
    for (size_t g = 0; g < layout.glyphs.size(); ++g) {
        layout.glyphs[g].x += tx;
        layout.glyphs[g].y += ty;
        layout.glyphs[g].inkX0 += tx;
        layout.glyphs[g].inkX1 += tx;
        layout.glyphs[g].inkY0 += ty;
        layout.glyphs[g].inkY1 += ty;
    }
    for (size_t l = 0; l < layout.lines.size(); ++l) {
        layout.lines[l].x += tx;
        layout.lines[l].baseline += ty;
        layout.lines[l].top += ty;
        layout.lines[l].bottom += ty;
        for (size_t r = 0; r < layout.lines[l].runs.size(); ++r)
            layout.lines[l].runs[r].x += tx;
    }
    for (size_t d = 0; d < layout.decorations.size(); ++d) {
        layout.decorations[d].x0 += tx;
        layout.decorations[d].x1 += tx;
        layout.decorations[d].y += ty;
    }
    {
        bool any = false;
        double x0 = 0, x1 = 0, y0 = 0, y1 = 0;
        for (size_t g = 0; g < layout.glyphs.size(); ++g) {
            const GlyphPlacement& gp = layout.glyphs[g];
            if (gp.inkX0 == gp.inkX1 && gp.inkY0 == gp.inkY1) continue;
            if (!any) { x0 = gp.inkX0; x1 = gp.inkX1; y0 = gp.inkY0; y1 = gp.inkY1; any = true; }
            else {
                if (gp.inkX0 < x0) x0 = gp.inkX0;
                if (gp.inkX1 > x1) x1 = gp.inkX1;
                if (gp.inkY0 < y0) y0 = gp.inkY0;
                if (gp.inkY1 > y1) y1 = gp.inkY1;
            }
        }
        if (any) {
            layout.metrics.inkBox = core::Box(core::Vec2(x0, y0), core::Vec2(x1, y1));
            layout.metrics.layoutBox = layout.metrics.inkBox;
        }
    }
    layout.anchor = atomPosition;
    *out = layout;
    return core::Error();
}

core::Error layoutParsedLabel(const std::string& utf8,
                              const core::Vec2& atomPosition,
                              const LayoutOptions& options,
                              GlyphCache* cache,
                              const LabelAnchorContext* ctx,
                              RichLayout* out,
                              LabelParse* parseOut) {
    const LabelParse parsed = parseLabel(utf8);
    if (parseOut) *parseOut = parsed;
    return layoutAtomLabel(parsed.spec, atomPosition, options, cache, ctx, out);
}

bool labelAttachX(const std::vector<core::TextRun>& runs, const LayoutOptions& options,
                  GlyphCache* cache, double* outX) {
    if (!cache || !outX) return false;
    RichLayout layout;
    const core::Error e = layoutRichText(runs, core::Vec2(0.0, 0.0), options, cache, &layout);
    if (!e.ok() || layout.glyphs.empty()) return false;
    // The attachment is the FIRST non-script glyph group (the element): that is
    // where a bond meets the label, and where the label is anchored to the atom.
    // The FIRST non-script run is the element: that is the attachment this function
    // measures, and it is the same span layoutAtomLabel puts on the atom.  (An
    // earlier version broke out of this loop only on the LAST candidate, so "Cl"
    // measured the whole word: 6.15 instead of the element is 4.15.)
    size_t attach = 0;
    for (size_t g = 0; g < layout.glyphs.size(); ++g) {
        if (!runIsScript(runs, layout.glyphs[g].runIndex)) { attach = g; break; }
    }
    const uint16_t attachRun = layout.glyphs[attach].runIndex;
    bool any = false;
    double x0 = 0, x1 = 0;
    for (size_t g = 0; g < layout.glyphs.size(); ++g) {
        const GlyphPlacement& gp = layout.glyphs[g];
        if (gp.runIndex != attachRun || gp.inkX0 == gp.inkX1) continue;
        if (!any) { x0 = gp.inkX0; x1 = gp.inkX1; any = true; }
        else {
            if (gp.inkX0 < x0) x0 = gp.inkX0;
            if (gp.inkX1 > x1) x1 = gp.inkX1;
        }
    }
    if (!any) return false;
    *outX = 0.5 * (x0 + x1);
    return true;
}

} // namespace text
} // namespace r62
