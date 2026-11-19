#ifndef R62_NEXT_TEXT_ATOM_LABEL_HPP
#define R62_NEXT_TEXT_ATOM_LABEL_HPP
// =============================================================================
// re_round62_viewer/next/text/atom_label.hpp
//
// T09 - ATOM LABEL LAYOUT (as opposed to free caption text).
//
// WHY THIS IS NOT "CENTER THE STRING"
// -----------------------------------------------------------------------------
// An atom label is not a caption.  The atom position is the anchor of the
// ELEMENT SYMBOL, and the parts around it (isotope prefix, charge, hydrogen
// count, atomic number, radical dot) hang off that symbol so that the label does
// not cover the bonds that meet at the atom.  Centring the string removes the
// charge, the isotope and the hydrogens from the visible field, which is why
// ChemDraw treats a label as tokens rather than as one centred string.
//
// DOCUMENTED BEHAVIOUR THIS IMPLEMENTS (ChemDraw user guide, PDF pages 48-50)
//   * Formula style formats numbers as subscripts.
//   * Superscript reduces the text size by about 25 percent and raises its
//     baseline; subscript is the mirror image.
//   * A super-scripted number before the element symbol is an isotope number.
//   * Charges may be superscripted and a charge following an element belongs to
//     that element.
//   * ChemDraw breaks a label into tokens; a token is an uppercase letter
//     followed by numbers or lowercase letters; the first token is attached to
//     the atom, and the rest is placed where there is room (for example, tokens
//     go left to right when there are bonds to the left of the atom).
//   * Atom label justification: Centered, FlushLeft, FlushRight, StackedAbove or
//     Automatic.  core::TextAlign plus LayoutAnchor cover all five.
//
// T06 BOUNDARY
// -----------------------------------------------------------------------------
// T06 supplies the SEMANTICS (element, charge, isotope, hydrogens, the label
// text).  This module supplies GLYPHS AND PLACEMENT.  parseLabel() is the
// convenience converter used when a label arrives as text (CDXML text content or
// a keystroke hotkey); when T06 provides fields directly, fill LabelSpec
// yourself and call layoutAtomLabel().  No chemical validation happens here.
//
// core/contracts.hpp has no radical flag in TextFlags, so this module does NOT
// invent one: LabelSpec::radicalDot is a LAYOUT INPUT for future radical
// support, and layoutAtomLabel draws it as a raised dot when asked.
// =============================================================================

#include "next/core/contracts.hpp"
#include "next/text/rich_text.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace r62 {
namespace text {

enum class LabelSide : int8_t {
    Auto = 0,
    Left = -1,
    Right = 1,
    Above = 2,
    Below = 3
};

struct LabelSpec {
    std::string element;
    int         isotope;
    int         charge;
    int         hydrogenCount;
    int         atomicNumber;
    std::string prefix;
    std::string suffix;
    bool        formulaStyle;
    bool        radicalDot;
    bool        bold, italic, underline;
    double      size;
    std::string font;
    core::TextAlign align;
    LayoutAnchor    anchor;
    LabelSide       side;

    LabelSpec()
        : isotope(0), charge(0), hydrogenCount(-1), atomicNumber(0),
          formulaStyle(true), radicalDot(false), bold(false), italic(false),
          underline(false), size(0.0), align(core::TextAlign::Center),
          anchor(LayoutAnchor::AtomCenter), side(LabelSide::Auto) {}
};

struct LabelAnchorContext {
    int    bondCount;
    double bondAnglesDeg[8];
    bool   hasBondRight;
    bool   hasBondLeft;
    bool   hasBondAbove;
    bool   hasBondBelow;
    LabelAnchorContext()
        : bondCount(0), hasBondRight(false), hasBondLeft(false),
          hasBondAbove(false), hasBondBelow(false) {}
    void inferFromAngles();
};

struct LabelParse {
    std::vector<core::TextRun> runs;
    LabelSpec     spec;
    std::string   plain;
    size_t        warnings;
    std::string   warning;
    LabelParse() : warnings(0) {}
};

LabelParse parseLabel(const std::string& utf8);
std::string stripLabelMarkup(const std::string& utf8);
void buildLabelRuns(const LabelSpec& spec, std::vector<core::TextRun>* out);

core::Error layoutAtomLabel(const LabelSpec& spec,
                            const core::Vec2& atomPosition,
                            const LayoutOptions& options,
                            GlyphCache* cache,
                            const LabelAnchorContext* ctx,
                            RichLayout* out);

core::Error layoutParsedLabel(const std::string& utf8,
                              const core::Vec2& atomPosition,
                              const LayoutOptions& options,
                              GlyphCache* cache,
                              const LabelAnchorContext* ctx,
                              RichLayout* out,
                              LabelParse* parseOut);

bool labelAttachX(const std::vector<core::TextRun>& runs, const LayoutOptions& options,
                  GlyphCache* cache, double* outX);

} // namespace text
} // namespace r62

#endif // R62_NEXT_TEXT_ATOM_LABEL_HPP
