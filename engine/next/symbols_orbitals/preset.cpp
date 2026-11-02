#include "next/symbols_orbitals/preset.hpp"

#include <cmath>
#include <cstring>

namespace r62 {
namespace symbols {

namespace {
// [PROJECT] geometry constants: multiples of the document text size (symbols) or
// bond length (orbitals) so a preset scales with the document. The guide fixes
// the SEMANTICS (which end is anchored), never a millimetre value.
const double kSymbolSizeFactor     = 0.55;
const double kRingRadiusFactor     = 0.80;
const double kSignHalfFactor       = 0.34;
const double kDotRadiusFactor      = 0.13;
const double kDotGapFactor         = 0.42;
const double kBarHalfFactor        = 0.42;
const double kAttachmentRadiusFac  = 0.46;
const double kBeadRadiusFactor     = 0.62;

const double kOrbitalLengthFactor  = 1.35;
const double kOrbitalWidthFactor   = 0.88;

const int kRadicalNone    = 0;  // core::Radical::None
const int kRadicalDoublet = 2;  // core::Radical::Doublet
const int kRadicalTriplet = 3;  // core::Radical::Triplet
} // namespace

const char* familyName(SOFamily f) {
    switch (f) {
    case SOFamily::Symbol:  return "Symbol";
    case SOFamily::Push:    return "Push";
    case SOFamily::Orbital: return "Orbital";
    }
    return "?";
}

const char* symbolKindName(SymbolKind k) {
    switch (k) {
    case SymbolKind::None:       return "None";
    case SymbolKind::Ring:       return "Ring";
    case SymbolKind::Sign:       return "Sign";
    case SymbolKind::DotPair:    return "DotPair";
    case SymbolKind::Dot:        return "Dot";
    case SymbolKind::Bar:        return "Bar";
    case SymbolKind::HDot:       return "HDot";
    case SymbolKind::HDash:      return "HDash";
    case SymbolKind::Attachment: return "Attachment";
    case SymbolKind::Bead:       return "Bead";
    }
    return "?";
}

const char* symbolAnchorName(SymbolAnchor a) {
    switch (a) {
    case SymbolAnchor::Free:      return "Free";
    case SymbolAnchor::Centered:  return "Centered";
    case SymbolAnchor::Node:      return "Node";
    case SymbolAnchor::NarrowEnd: return "NarrowEnd";
    case SymbolAnchor::RingEnd:   return "RingEnd";
    }
    return "?";
}

const char* symbolMeaningName(SymbolMeaning m) {
    switch (m) {
    case SymbolMeaning::Decorative:      return "Decorative";
    case SymbolMeaning::Charge:          return "Charge";
    case SymbolMeaning::ChargeMinus:     return "ChargeMinus";
    case SymbolMeaning::ChargePlusMinus: return "ChargePlusMinus";
    case SymbolMeaning::RadicalMono:     return "RadicalMono";
    case SymbolMeaning::RadicalDouble:   return "RadicalDouble";
    case SymbolMeaning::LonePair:        return "LonePair";
    case SymbolMeaning::AttachmentPoint: return "AttachmentPoint";
    case SymbolMeaning::PolymerBead:     return "PolymerBead";
    }
    return "?";
}

const char* pushHeadName(PushHead h) {
    switch (h) {
    case PushHead::FullPair:    return "FullPair";
    case PushHead::HalfRadical: return "HalfRadical";
    }
    return "?";
}

const char* pushEndpointName(PushEndpoint e) {
    switch (e) {
    case PushEndpoint::None:    return "None";
    case PushEndpoint::Atom:    return "Atom";
    case PushEndpoint::Bond:    return "Bond";
    case PushEndpoint::Orbital: return "Orbital";
    }
    return "?";
}

const char* orbitalShapeName(OrbitalShape s) {
    switch (s) {
    case OrbitalShape::S:          return "S";
    case OrbitalShape::Sigma:      return "Sigma";
    case OrbitalShape::SingleLobe: return "SingleLobe";
    case OrbitalShape::P:          return "P";
    case OrbitalShape::HybridSp3:  return "HybridSp3";
    case OrbitalShape::Dxy:        return "Dxy";
    case OrbitalShape::Dz2:        return "Dz2";
    }
    return "?";
}

const char* orbitalFillName(OrbitalFill f) {
    switch (f) {
    case OrbitalFill::Open:   return "Open";
    case OrbitalFill::Shaded: return "Shaded";
    case OrbitalFill::Solid:  return "Solid";
    }
    return "?";
}

int orbitalLobeCount(OrbitalShape shape) {
    switch (shape) {
    case OrbitalShape::S:          return 1;
    case OrbitalShape::Sigma:      return 1;
    case OrbitalShape::SingleLobe: return 1;
    case OrbitalShape::P:          return 2;
    case OrbitalShape::HybridSp3:  return 2;
    case OrbitalShape::Dxy:        return 4;
    case OrbitalShape::Dz2:        return 3;
    }
    return 0;
}

bool orbitalIsDirectional(OrbitalShape shape) {
    return shape != OrbitalShape::S && shape != OrbitalShape::Sigma;
}

bool orbitalIsCentered(OrbitalShape shape) {
    return !orbitalIsDirectional(shape);
}

// ===========================================================================
// THE 16 CHEMICAL SYMBOL ROWS
// ===========================================================================
// Order == Chemical_Symbols_Toolbar.xml: 8x2 palette, reading column 1..8 of
// row 1, then column 1..8 of row 2. tools.csv preserved exactly this order.
// The palette graphics names (Symbol_CirclePlus, EPT_LonePair, ...) confirm the
// identity of each cell; the guide p242 letter captions "A)..P)" confirm the
// ordering convention.
static const SymbolPresetSpec kSymbols[] = {
  { 0, "chooseSymbolTool_CirclePlus", "r62.tool.symbol.circleplus",
    "Circle Plus", SOFamily::Symbol, SymbolKind::Ring, GlyphSign::Plus,
    SymbolMeaning::Charge, SymbolAnchor::RingEnd, 1, kRadicalNone,
    true, true, true, true,
    "[GUIDE p242] charge symbol; drag onto an atom. chargeDelta=+1." },
  { 1, "chooseSymbolTool_Plus", "r62.tool.symbol.plus",
    "Plus", SOFamily::Symbol, SymbolKind::Sign, GlyphSign::Plus,
    SymbolMeaning::Charge, SymbolAnchor::RingEnd, 1, kRadicalNone,
    true, true, false, true,
    "[GUIDE p242] bare plus sign; same chemistry as CirclePlus, no ring." },
  { 2, "chooseSymbolTool_RadicalCation", "r62.tool.symbol.radicalcation",
    "Radical Cation", SOFamily::Symbol, SymbolKind::Ring, GlyphSign::Plus,
    SymbolMeaning::ChargePlusMinus, SymbolAnchor::RingEnd, 1, kRadicalDoublet,
    true, true, true, true,
    "[GUIDE p243] charged radical: chargeDelta=+1 AND radical=doublet." },
  { 3, "chooseSymbolTool_DoubleRadical", "r62.tool.symbol.doubleradical",
    "Lone Pair / Double Radical", SOFamily::Symbol, SymbolKind::DotPair,
    GlyphSign::None, SymbolMeaning::LonePair, SymbolAnchor::Free, 0, kRadicalNone,
    true, true, false, true,
    "[GUIDE p242] two dots. Chemistry applies ONLY on explicit binding; the"
    " diradical reading next to an unlabeled carbon is opt-in, not automatic." },
  { 4, "choosePushElectronTool", "r62.tool.push.electron",
    "Electron Pushing", SOFamily::Symbol, SymbolKind::None, GlyphSign::None,
    SymbolMeaning::Decorative, SymbolAnchor::Free, 0, kRadicalNone,
    false, false, false, true,
    "[GUIDE p158] electron PAIR transfer; full-head arrow. Also in the push"
    " table; this row keeps the 16-command mapping complete." },
  { 5, "chooseSymbolTool_HDot", "r62.tool.symbol.hdot",
    "H-dot", SOFamily::Symbol, SymbolKind::HDot, GlyphSign::None,
    SymbolMeaning::Decorative, SymbolAnchor::Node, 0, kRadicalNone,
    false, false, false, true,
    "[GUIDE p242] H coming OUT of the plane; not movable from its atom;"
    " carries no AtomSemantics field, stored as an annotation (README limits)." },
  { 6, "chooseSymbolTool_Attachment_Diamond", "r62.tool.symbol.attach.diamond",
    "Attachment Point (Diamond)", SOFamily::Symbol, SymbolKind::Attachment,
    GlyphSign::None, SymbolMeaning::AttachmentPoint, SymbolAnchor::Node,
    0, kRadicalNone, false, true, false, true,
    "[GUIDE p243] diamond attachment; the ONLY attachment showing rank"
    " numbers, assigned sequentially as diamonds are added." },
  { 7, "chooseSymbolTool_PolymerBeadAttachmentPoint",
    "r62.tool.symbol.attach.bead",
    "Polymer Bead Attachment Point", SOFamily::Symbol, SymbolKind::Bead,
    GlyphSign::None, SymbolMeaning::PolymerBead, SymbolAnchor::Node,
    0, kRadicalNone, false, true, true, true,
    "[GUIDE p243] resin attachment bead; marquee-resizable." },
  { 8, "chooseSymbolTool_CircleMinus", "r62.tool.symbol.circleminus",
    "Circle Minus", SOFamily::Symbol, SymbolKind::Ring, GlyphSign::Minus,
    SymbolMeaning::ChargeMinus, SymbolAnchor::RingEnd, -1, kRadicalNone,
    true, true, true, true,
    "[GUIDE p242] negative charge ring; chargeDelta=-1." },
  { 9, "chooseSymbolTool_Minus", "r62.tool.symbol.minus",
    "Minus", SOFamily::Symbol, SymbolKind::Sign, GlyphSign::Minus,
    SymbolMeaning::ChargeMinus, SymbolAnchor::RingEnd, -1, kRadicalNone,
    true, true, false, true,
    "[GUIDE p242] bare minus sign." },
  { 10, "chooseSymbolTool_RadicalAnion", "r62.tool.symbol.radicalanion",
    "Radical Anion", SOFamily::Symbol, SymbolKind::Ring, GlyphSign::Minus,
    SymbolMeaning::ChargePlusMinus, SymbolAnchor::RingEnd, -1, kRadicalDoublet,
    true, true, true, true,
    "[GUIDE p243] charged radical: chargeDelta=-1 AND radical=doublet." },
  { 11, "chooseSymbolTool_Radical", "r62.tool.symbol.radical",
    "Radical", SOFamily::Symbol, SymbolKind::Dot, GlyphSign::None,
    SymbolMeaning::RadicalMono, SymbolAnchor::Free, 0, kRadicalDoublet,
    true, true, false, true,
    "[GUIDE p242] a single non-bonded electron; radical=doublet on bind." },
  { 12, "choosePushRadicalTool", "r62.tool.push.radical",
    "Radical Pushing", SOFamily::Symbol, SymbolKind::None, GlyphSign::None,
    SymbolMeaning::Decorative, SymbolAnchor::Free, 0, kRadicalNone,
    false, false, false, true,
    "[GUIDE p158] ONE electron transfer; half-head arrow. Also in the push table." },
  { 13, "chooseSymbolTool_HDash", "r62.tool.symbol.hdash",
    "H-dash", SOFamily::Symbol, SymbolKind::HDash, GlyphSign::None,
    SymbolMeaning::Decorative, SymbolAnchor::Node, 0, kRadicalNone,
    false, false, false, true,
    "[GUIDE p242] H going INTO the plane. Not movable from its atom." },
  { 14, "chooseSymbolTool_Attachment_Star", "r62.tool.symbol.attach.star",
    "Attachment Point (Star)", SOFamily::Symbol, SymbolKind::Attachment,
    GlyphSign::None, SymbolMeaning::AttachmentPoint, SymbolAnchor::Node,
    0, kRadicalNone, false, true, false, true,
    "[GUIDE p243] star attachment; no rank numbers (only the diamond has them)." },
  { 15, "chooseSymbolTool_Attachment_Wavy", "r62.tool.symbol.attach.wavy",
    "Attachment Point (Wavy)", SOFamily::Symbol, SymbolKind::Attachment,
    GlyphSign::None, SymbolMeaning::AttachmentPoint, SymbolAnchor::Node,
    0, kRadicalNone, false, true, false, true,
    "[GUIDE p243] wavy attachment; no rank numbers." },
};

const PushPresetSpec kPushes[] = {
  { 0, "choosePushElectronTool", "r62.tool.push.electron",
    "Electron Pushing (pair)", SOFamily::Push, PushHead::FullPair, true,
    "[GUIDE p158] FULL-head arrow == electron PAIR transfer." },
  { 1, "choosePushRadicalTool", "r62.tool.push.radical",
    "Radical Pushing (single electron)", SOFamily::Push, PushHead::HalfRadical, true,
    "[GUIDE p158] HALF-head arrow == ONE electron transfer." },
};
// ===========================================================================
// THE 21 ORBITAL ROWS
// ===========================================================================
// Order == Orbital_Toolbar.xml, which tools.csv preserved: the palette is 7
// columns x 3 rows and the file lists p_Shaded first (row 1 col 4) then walks
// column 1..7 of row 1, row 2, row 3. That traversal is why the sequence is not
// simply "s, sigma, p, d". NEVER reorder: presetId is persisted.
//
// The toolbar words map to the fill axis:
//   _Open    -> OrbitalFill::Open    (outline only)
//   _Shaded  -> OrbitalFill::Shaded  (outline + hatch)
//   _Solid   -> OrbitalFill::Solid   (outline + flat fill)
//   _Shaded2 -> Shaded + invertPhase (the palette second variant)
//   _Solid2  -> Solid  + invertPhase
//
// spLobe is the toolbar spelling of the SINGLE-LOBE tool; the guide calls it
// "single lobe orbital" and anchors it at the NARROW end [GUIDE p241].
static const OrbitalPresetSpec kOrbitals[] = {
  { 0, "chooseOrbitalTool_p_Shaded", "r62.tool.orbital.p.shaded",
    "p Orbital (Shaded)", SOFamily::Orbital, OrbitalShape::P,
    OrbitalFill::Shaded, false, SymbolAnchor::Node, 2, true,
    "[GUIDE p240] two lobes, node on the atom." },
  { 1, "chooseOrbitalTool_s_Open", "r62.tool.orbital.s.open",
    "s Orbital (Open)", SOFamily::Orbital, OrbitalShape::S,
    OrbitalFill::Open, false, SymbolAnchor::Centered, 1, true,
    "[GUIDE p241] centered on the atom; outline only." },
  { 2, "chooseOrbitalTool_Sigma_Open", "r62.tool.orbital.sigma.open",
    "sigma Orbital (Open)", SOFamily::Orbital, OrbitalShape::Sigma,
    OrbitalFill::Open, false, SymbolAnchor::Centered, 1, true,
    "[GUIDE p241] centered on the atom; circle with a node marker." },
  { 3, "chooseOrbitalTool_spLobe_Open", "r62.tool.orbital.lobe.open",
    "Single Lobe Orbital (Open)", SOFamily::Orbital, OrbitalShape::SingleLobe,
    OrbitalFill::Open, false, SymbolAnchor::NarrowEnd, 1, true,
    "[GUIDE p241] narrow end attached to the atom; outline only." },
  { 4, "chooseOrbitalTool_s_Shaded", "r62.tool.orbital.s.shaded",
    "s Orbital (Shaded)", SOFamily::Orbital, OrbitalShape::S,
    OrbitalFill::Shaded, false, SymbolAnchor::Centered, 1, true,
    "[GUIDE p241] centered; outline + hatch shading." },
  { 5, "chooseOrbitalTool_Sigma_Shaded", "r62.tool.orbital.sigma.shaded",
    "sigma Orbital (Shaded)", SOFamily::Orbital, OrbitalShape::Sigma,
    OrbitalFill::Shaded, false, SymbolAnchor::Centered, 1, true,
    "[GUIDE p241] centered; outline + hatch." },
  { 6, "chooseOrbitalTool_spLobe_Shaded", "r62.tool.orbital.lobe.shaded",
    "Single Lobe Orbital (Shaded)", SOFamily::Orbital, OrbitalShape::SingleLobe,
    OrbitalFill::Shaded, false, SymbolAnchor::NarrowEnd, 1, true,
    "[GUIDE p241] narrow end anchored; outline + hatch." },
  { 7, "chooseOrbitalTool_s_Solid", "r62.tool.orbital.s.solid",
    "s Orbital (Solid)", SOFamily::Orbital, OrbitalShape::S,
    OrbitalFill::Solid, false, SymbolAnchor::Centered, 1, true,
    "[GUIDE p241] centered; flat fill." },
  { 8, "chooseOrbitalTool_Sigma_Solid", "r62.tool.orbital.sigma.solid",
    "sigma Orbital (Solid)", SOFamily::Orbital, OrbitalShape::Sigma,
    OrbitalFill::Solid, false, SymbolAnchor::Centered, 1, true,
    "[GUIDE p241] centered; flat fill." },
  { 9, "chooseOrbitalTool_spLobe_Solid", "r62.tool.orbital.lobe.solid",
    "Single Lobe Orbital (Solid)", SOFamily::Orbital, OrbitalShape::SingleLobe,
    OrbitalFill::Solid, false, SymbolAnchor::NarrowEnd, 1, true,
    "[GUIDE p241] narrow end anchored; flat fill." },
  { 10, "chooseOrbitalTool_sp3_Shaded", "r62.tool.orbital.sp3.shaded",
    "Hybrid sp3 Orbital (Shaded)", SOFamily::Orbital, OrbitalShape::HybridSp3,
    OrbitalFill::Shaded, false, SymbolAnchor::Node, 2, true,
    "[GUIDE p240] large + small lobe; node on the atom." },
  { 11, "chooseOrbitalTool_sp3_Shaded2", "r62.tool.orbital.sp3.shaded2",
    "Hybrid sp3 Orbital (Shaded, inverted phase)", SOFamily::Orbital,
    OrbitalShape::HybridSp3, OrbitalFill::Shaded, true, SymbolAnchor::Node, 2, true,
    "Second palette variant: phase inverted (the large lobe becomes the open one)." },
  { 12, "chooseOrbitalTool_p_Solid", "r62.tool.orbital.p.solid",
    "p Orbital (Solid)", SOFamily::Orbital, OrbitalShape::P,
    OrbitalFill::Solid, false, SymbolAnchor::Node, 2, true,
    "[GUIDE p240] two lobes, node on the atom; flat fill." },
  { 13, "chooseOrbitalTool_sp3_Solid", "r62.tool.orbital.sp3.solid",
    "Hybrid sp3 Orbital (Solid)", SOFamily::Orbital, OrbitalShape::HybridSp3,
    OrbitalFill::Solid, false, SymbolAnchor::Node, 2, true,
    "[GUIDE p240] node on the atom; flat fill." },
  { 14, "chooseOrbitalTool_sp3_Solid2", "r62.tool.orbital.sp3.solid2",
    "Hybrid sp3 Orbital (Solid, inverted phase)", SOFamily::Orbital,
    OrbitalShape::HybridSp3, OrbitalFill::Solid, true, SymbolAnchor::Node, 2, true,
    "Second palette variant: phase inverted." },
  { 15, "chooseOrbitalTool_dxy_Shaded", "r62.tool.orbital.dxy.shaded",
    "dxy Orbital (Shaded)", SOFamily::Orbital, OrbitalShape::Dxy,
    OrbitalFill::Shaded, false, SymbolAnchor::Node, 4, true,
    "[GUIDE p240] four-lobe cloverleaf; node on the atom." },
  { 16, "chooseOrbitalTool_dz2_Shaded", "r62.tool.orbital.dz2.shaded",
    "dz2 Orbital (Shaded)", SOFamily::Orbital, OrbitalShape::Dz2,
    OrbitalFill::Shaded, false, SymbolAnchor::Node, 3, true,
    "[GUIDE p240] two large lobes plus a torus; node on the atom." },
  { 17, "chooseOrbitalTool_dz2_Shaded2", "r62.tool.orbital.dz2.shaded2",
    "dz2 Orbital (Shaded, inverted phase)", SOFamily::Orbital,
    OrbitalShape::Dz2, OrbitalFill::Shaded, true, SymbolAnchor::Node, 3, true,
    "Second palette variant: phase inverted." },
  { 18, "chooseOrbitalTool_dxy_Solid", "r62.tool.orbital.dxy.solid",
    "dxy Orbital (Solid)", SOFamily::Orbital, OrbitalShape::Dxy,
    OrbitalFill::Solid, false, SymbolAnchor::Node, 4, true,
    "[GUIDE p240] cloverleaf; flat fill." },
  { 19, "chooseOrbitalTool_dz2_Solid", "r62.tool.orbital.dz2.solid",
    "dz2 Orbital (Solid)", SOFamily::Orbital, OrbitalShape::Dz2,
    OrbitalFill::Solid, false, SymbolAnchor::Node, 3, true,
    "[GUIDE p240] flat fill." },
  { 20, "chooseOrbitalTool_dz2_Solid2", "r62.tool.orbital.dz2.solid2",
    "dz2 Orbital (Solid, inverted phase)", SOFamily::Orbital,
    OrbitalShape::Dz2, OrbitalFill::Solid, true, SymbolAnchor::Node, 3, true,
    "Second palette variant: phase inverted." },
};

// ===========================================================================
// Table access
// ===========================================================================
const SymbolPresetSpec* symbolPresets() { return kSymbols; }
size_t symbolPresetCount() { return sizeof(kSymbols) / sizeof(kSymbols[0]); }
const PushPresetSpec* pushPresets() { return kPushes; }
size_t pushPresetCount() { return sizeof(kPushes) / sizeof(kPushes[0]); }
const OrbitalPresetSpec* orbitalPresets() { return kOrbitals; }
size_t orbitalPresetCount() { return sizeof(kOrbitals) / sizeof(kOrbitals[0]); }

const SymbolPresetSpec* symbolPresetById(uint32_t id) {
    for (size_t i = 0; i < symbolPresetCount(); ++i)
        if (kSymbols[i].presetId == id) return &kSymbols[i];
    return 0;
}
const SymbolPresetSpec* symbolPresetByCommand(const std::string& cmd) {
    for (size_t i = 0; i < symbolPresetCount(); ++i)
        if (cmd == kSymbols[i].sourceCommand) return &kSymbols[i];
    return 0;
}
const SymbolPresetSpec* symbolPresetByToolId(const std::string& toolId) {
    for (size_t i = 0; i < symbolPresetCount(); ++i)
        if (toolId == kSymbols[i].toolId) return &kSymbols[i];
    return 0;
}
const PushPresetSpec* pushPresetById(uint32_t id) {
    for (size_t i = 0; i < pushPresetCount(); ++i)
        if (kPushes[i].presetId == id) return &kPushes[i];
    return 0;
}
const PushPresetSpec* pushPresetByCommand(const std::string& cmd) {
    for (size_t i = 0; i < pushPresetCount(); ++i)
        if (cmd == kPushes[i].sourceCommand) return &kPushes[i];
    return 0;
}
const OrbitalPresetSpec* orbitalPresetById(uint32_t id) {
    for (size_t i = 0; i < orbitalPresetCount(); ++i)
        if (kOrbitals[i].presetId == id) return &kOrbitals[i];
    return 0;
}
const OrbitalPresetSpec* orbitalPresetByCommand(const std::string& cmd) {
    for (size_t i = 0; i < orbitalPresetCount(); ++i)
        if (cmd == kOrbitals[i].sourceCommand) return &kOrbitals[i];
    return 0;
}
const OrbitalPresetSpec* orbitalPresetByToolId(const std::string& toolId) {
    for (size_t i = 0; i < orbitalPresetCount(); ++i)
        if (toolId == kOrbitals[i].toolId) return &kOrbitals[i];
    return 0;
}

bool isOwnedCommand(const std::string& command) {
    return symbolPresetByCommand(command) != 0 ||
           pushPresetByCommand(command) != 0 ||
           orbitalPresetByCommand(command) != 0;
}

SOFamily familyOfCommand(const std::string& command, bool* known) {
    if (pushPresetByCommand(command))    { if (known) *known = true; return SOFamily::Push; }
    if (symbolPresetByCommand(command))  { if (known) *known = true; return SOFamily::Symbol; }
    if (orbitalPresetByCommand(command)) { if (known) *known = true; return SOFamily::Orbital; }
    if (known) *known = false;
    return SOFamily::Symbol;
}
// ===========================================================================
// Resolution
// ===========================================================================
// The SAME functions preview and commit call, which is how "preview agrees with
// commit" is structural rather than disciplinary (the defect class
// SOURCES_AND_LIMITS.md records for the legacy editor).
SymbolParams resolveSymbolPreset(uint32_t presetId, const core::Vec2& anchorPoint,
                                  double documentTextSize) {
    SymbolParams p;
    const SymbolPresetSpec* spec = symbolPresetById(presetId);
    if (!spec) return p;
    const double em = (documentTextSize > 0.0 && std::isfinite(documentTextSize))
                          ? documentTextSize : 10.0;
    p.presetId      = spec->presetId;
    p.kind          = spec->kind;
    p.sign          = spec->sign;
    p.meaning       = spec->meaning;
    p.anchor        = spec->anchor;
    p.position      = anchorPoint;
    p.rotation      = 0.0;
    p.scale         = 1.0;
    p.size          = em * kSymbolSizeFactor;
    p.chargeDelta   = spec->chargeDelta;
    p.radicalValue  = spec->radicalValue;
    p.rotateFromEnd = spec->rotateFromEnd;
    p.resizable     = spec->resizable;
    p.drawCircle    = spec->drawCircle;
    return p;
}

OrbitalParams resolveOrbitalPreset(uint32_t presetId, const core::Vec2& anchorPoint,
                                   double documentBondLength, double documentTextSize) {
    OrbitalParams p;
    const OrbitalPresetSpec* spec = orbitalPresetById(presetId);
    if (!spec) return p;
    const double bl = (documentBondLength > 0.0 && std::isfinite(documentBondLength))
                          ? documentBondLength : 14.4;
    (void)documentTextSize;
    p.presetId    = spec->presetId;
    p.shape       = spec->shape;
    p.fill        = spec->fill;
    p.invertPhase = spec->invertPhase;
    p.anchor      = spec->anchor;
    p.position    = anchorPoint;
    p.rotation    = 0.0;
    p.length      = bl * kOrbitalLengthFactor;
    p.width       = bl * kOrbitalWidthFactor;
    p.scale       = 1.0;
    p.lobeCount   = orbitalLobeCount(spec->shape);

    // Phase/fill per lobe. The rule that is DATA rather than drawing code:
    //   * Open   -> nothing is filled.
    //   * Shaded -> lobes at EVEN index are filled (hatched), odd are open.
    //   * Solid  -> all lobes filled.
    //   * invertPhase is what the palette second variants (_Shaded2/_Solid2) are:
    //     it swaps the PHASE of every lobe, and for a partially-filled form
    //     (Shaded) it also swaps which lobes carry the hatch. It must NOT empty a
    //     SOLID form: "Solid, inverted phase" is still a solid orbital, it only
    //     paints the other phase colour. Negating "all filled" would silently turn
    //     _Solid2 into an unfilled outline, i.e. a duplicate of Open.
    // lobePositive marks the positive-phase lobe and is flipped by invertPhase, so
    // a renderer can colour the two phases differently without re-deriving them.
    for (int i = 0; i < 6; ++i) {
        bool filled = false;
        bool positive = true;
        if (spec->fill == OrbitalFill::Solid) filled = true;
        else if (spec->fill == OrbitalFill::Shaded) filled = (i % 2) == 0;
        if (spec->invertPhase) {
            positive = !positive;
            // Only a partially-filled form has a fill set to swap. A Solid form
            // stays fully filled so the inverted variant remains solid.
            if (spec->fill != OrbitalFill::Solid) filled = !filled;
        }
        p.lobeFilled[i]   = filled;
        p.lobePositive[i] = positive;
    }
    return p;
}

PushParams resolvePushPreset(uint32_t presetId, const core::Vec2& begin,
                             const core::Vec2& end) {
    PushParams p;
    const PushPresetSpec* spec = pushPresetById(presetId);
    if (!spec) return p;
    p.presetId = spec->presetId;
    p.head     = spec->head;
    p.begin    = begin;
    p.end      = end;
    p.sagitta  = 0.0;   // a straight push by default; a drag off the chord curves it
    p.scale    = 1.0;
    return p;
}

OrbitalParams applyOrbitalStyle(const OrbitalParams& current, uint32_t presetId,
                                double documentBondLength) {
    const OrbitalPresetSpec* spec = orbitalPresetById(presetId);
    if (!spec) return current;
    // Geometry (position, rotation, length, width, scale) is KEPT - a style
    // change must not move the object. Only the appearance axes are replaced.
    OrbitalParams p = current;
    p.presetId    = spec->presetId;
    p.shape       = spec->shape;
    p.fill        = spec->fill;
    p.invertPhase = spec->invertPhase;
    p.anchor      = spec->anchor;
    p.lobeCount   = orbitalLobeCount(spec->shape);
    for (int i = 0; i < 6; ++i) {
        bool filled = false;
        bool positive = true;
        if (spec->fill == OrbitalFill::Solid) filled = true;
        else if (spec->fill == OrbitalFill::Shaded) filled = (i % 2) == 0;
        if (spec->invertPhase) {
            positive = !positive;
            // Same rule as resolveOrbitalPreset: inverting a SOLID orbital swaps
            // the phase colour but keeps it solid; only a partially-filled form
            // swaps its fill set.
            if (spec->fill != OrbitalFill::Solid) filled = !filled;
        }
        p.lobeFilled[i]   = filled;
        p.lobePositive[i] = positive;
    }
    if (!std::isfinite(p.length) || p.length <= 0.0) {
        const double bl = (documentBondLength > 0.0) ? documentBondLength : 14.4;
        p.length = bl * kOrbitalLengthFactor;
        p.width  = bl * kOrbitalWidthFactor;
    }
    return p;
}

SymbolParams applySymbolStyle(const SymbolParams& current, uint32_t presetId) {
    const SymbolPresetSpec* spec = symbolPresetById(presetId);
    if (!spec) return current;
    SymbolParams p = current;
    p.presetId      = spec->presetId;
    p.kind          = spec->kind;
    p.sign          = spec->sign;
    p.meaning       = spec->meaning;
    p.anchor        = spec->anchor;
    p.chargeDelta   = spec->chargeDelta;
    p.radicalValue  = spec->radicalValue;
    p.rotateFromEnd = spec->rotateFromEnd;
    p.resizable     = spec->resizable;
    p.drawCircle    = spec->drawCircle;
    return p;
}

SymbolParams lonePairBarVariant(const SymbolParams& lonePair) {
    // [GUIDE p242] "if you ALT+click on an atom, it will place a lone pair BAR
    // symbol instead of a lone pair DOUBLE DOTS symbol".
    // Everything else - anchor, chemistry, size, rotation - is untouched, so the
    // two forms are one preset expressed twice rather than two presets.
    SymbolParams p = lonePair;
    p.kind = SymbolKind::Bar;
    return p;
}

// ===========================================================================
// Validation
// ===========================================================================
// A specific error, never a silent clamp, so a test can assert on the refusal.
core::Error validateSymbolParams(const SymbolParams& p) {
    if (p.presetId == kNoPreset)
        return core::Error(core::ErrorCode::InvalidArgument, "symbol has no resolved preset");
    if (!p.position.finite())
        return core::Error(core::ErrorCode::InvalidGeometry, "symbol position is not finite");
    if (!std::isfinite(p.rotation))
        return core::Error(core::ErrorCode::InvalidGeometry, "symbol rotation is not finite");
    if (!std::isfinite(p.scale) || p.scale <= 0.0)
        return core::Error(core::ErrorCode::InvalidGeometry, "symbol scale must be positive and finite");
    if (!std::isfinite(p.size) || p.size <= 0.0)
        return core::Error(core::ErrorCode::InvalidGeometry, "symbol size must be positive and finite");
    if (p.kind == SymbolKind::None)
        return core::Error(core::ErrorCode::InvalidArgument, "symbol kind None cannot be drawn");
    // A sign glyph with no sign, or a ring whose sign is None but whose meaning
    // is a charge, would draw an unlabelled charge. Refuse instead.
    if (p.kind == SymbolKind::Sign && p.sign == GlyphSign::None)
        return core::Error(core::ErrorCode::InvalidArgument, "Sign glyph requires a plus or minus");
    if (p.chargeDelta != 0 && p.sign == GlyphSign::None)
        return core::Error(core::ErrorCode::InvalidSemantics,
                                   "a charge symbol must draw a sign");
    if (p.chargeDelta < -15 || p.chargeDelta > 15)
        return core::Error(core::ErrorCode::OutOfRange, "chargeDelta outside -15..+15");
    if (p.radicalValue < 0 || p.radicalValue > 3)
        return core::Error(core::ErrorCode::OutOfRange, "radical value outside 0..3");
    if (p.chargeDelta != 0 && p.radicalValue != 0 &&
        p.meaning != SymbolMeaning::ChargePlusMinus)
        return core::Error(core::ErrorCode::InvalidSemantics,
                                   "charge and radical together must be a charge-radical symbol");
    return core::Error::success();
}

core::Error validatePushParams(const PushParams& p) {
    if (p.presetId == kNoPreset)
        return core::Error(core::ErrorCode::InvalidArgument, "push has no resolved preset");
    if (!p.begin.finite() || !p.end.finite())
        return core::Error(core::ErrorCode::InvalidGeometry, "push endpoint is not finite");
    if (!std::isfinite(p.sagitta))
        return core::Error(core::ErrorCode::InvalidGeometry, "push sagitta is not finite");
    if (!std::isfinite(p.scale) || p.scale <= 0.0)
        return core::Error(core::ErrorCode::InvalidGeometry, "push scale must be positive");
    const double dx = p.end.x - p.begin.x;
    const double dy = p.end.y - p.begin.y;
    if (std::sqrt(dx * dx + dy * dy) <= 0.0)
        return core::Error(core::ErrorCode::InvalidGeometry,
                                   "a push from a point to itself has no direction");
    // [GUIDE p158] transfers are drawn "between atoms, bonds, and orbitals".
    // Endpoints are optional at creation time (the user may start on empty space
    // and snap later) but an endpoint that IS set must be one of those three.
    if (p.sourceKind == PushEndpoint::None && p.sourceObject.valid())
        return core::Error(core::ErrorCode::InvalidSemantics,
                                   "source object set without a source kind");
    if (p.targetKind == PushEndpoint::None && p.targetObject.valid())
        return core::Error(core::ErrorCode::InvalidSemantics,
                                   "target object set without a target kind");
    return core::Error::success();
}

core::Error validateOrbitalParams(const OrbitalParams& p) {
    if (p.presetId == kNoPreset)
        return core::Error(core::ErrorCode::InvalidArgument, "orbital has no resolved preset");
    if (!p.position.finite())
        return core::Error(core::ErrorCode::InvalidGeometry, "orbital position is not finite");
    if (!std::isfinite(p.rotation))
        return core::Error(core::ErrorCode::InvalidGeometry, "orbital rotation is not finite");
    if (!std::isfinite(p.length) || p.length <= 0.0)
        return core::Error(core::ErrorCode::InvalidGeometry, "orbital length must be positive");
    if (!std::isfinite(p.width) || p.width <= 0.0)
        return core::Error(core::ErrorCode::InvalidGeometry, "orbital width must be positive");
    if (!std::isfinite(p.scale) || p.scale <= 0.0)
        return core::Error(core::ErrorCode::InvalidGeometry, "orbital scale must be positive");
    const int expected = orbitalLobeCount(p.shape);
    if (expected == 0)
        return core::Error(core::ErrorCode::InvalidArgument, "orbital shape has no lobes");
    if (p.lobeCount != expected)
        return core::Error(core::ErrorCode::InvalidSemantics,
                                   "lobeCount does not match the shape");
    // The anchor must agree with the shape: centered shapes cannot claim a node
    // anchor and directed shapes cannot claim Centered. This is the check that
    // stops a persisted record from describing an orbital the guide says cannot
    // exist ([GUIDE p240/p241] fix which end is attached).
    if (orbitalIsCentered(p.shape) && p.anchor != SymbolAnchor::Centered)
        return core::Error(core::ErrorCode::InvalidSemantics,
                                   "a centered orbital shape must use the Centered anchor");
    if (orbitalIsDirectional(p.shape) && p.anchor == SymbolAnchor::Centered)
        return core::Error(core::ErrorCode::InvalidSemantics,
                                   "a directed orbital shape must use a Node/NarrowEnd anchor");
    if (p.shape == OrbitalShape::SingleLobe && p.anchor != SymbolAnchor::NarrowEnd)
        return core::Error(core::ErrorCode::InvalidSemantics,
                                   "single lobe must anchor at its narrow end");
    return core::Error::success();
}

} // namespace symbols
} // namespace r62
