#ifndef R62_NEXT_SYMBOLS_ORBITALS_PRESET_HPP
#define R62_NEXT_SYMBOLS_ORBITALS_PRESET_HPP
// =============================================================================
// re_round62_viewer/next/symbols_orbitals/preset.hpp
//
// T12 - CHEMICAL SYMBOL / ELECTRON-PUSH / ORBITAL PRESET TABLES.
//
// -----------------------------------------------------------------------------
// WHAT THIS MODULE OWNS
// -----------------------------------------------------------------------------
// Two toolbars from tools.csv, 16 + 21 = 37 command strings:
//
//   * ChemicalSymbolsToolBarID -> 16 commands (charge, radical, lone pair,
//     electron/radical pushing, H-dot/H-dash, four attachment points).
//   * OrbitalsToolBarID        -> 21 commands (s / sigma / single-lobe / p /
//     hybrid sp3 / d(xy) / d(z2), each in Open / Shaded / Solid variants).
//
// The central design decision is the same one T10 made for arrows: these are NOT
// 37 algorithms. They are a bounded set of PRIMITIVES (a ring, a plus, a minus,
// two dots, a bar, a dot, a diamond, a star, a wavy stub, a filled/hatched/open
// ellipse or lobe) composed from a small number of parameter axes. A new preset
// is a table row, not a function.
//
// -----------------------------------------------------------------------------
// PROVENANCE OF EVERY CLAIM
// -----------------------------------------------------------------------------
// The 37 command strings are copied VERBATIM from
// host_integration/drawing_inventory/tools.csv, which was vendored from the
// installed ChemDraw GUI XML. Cross-checked against the live files:
//
//   .../GUI/Common/XML/Toolbars/Chemical_Symbols_Toolbar.xml
//   .../GUI/Common/XML/Toolbars/Orbital_Toolbar.xml
//
// SEMANTICS come from the installed ChemDraw User Guide (443 pages, extracted in
// host_integration/drawing_inventory/guide_text.txt). Marked [GUIDE pNNN]:
//
//   p240 "You draw orbitals so that the node appears first."
//   p241 "Display the Info window to view the orbital's length and angle
//         relative to the X-axis while you draw it. To constrain the length
//         and angle of an orbital, navigate to Object > Fixed Lengths or
//         Fixed Angles."
//   p241 "Orbitals are not normally a part of the structure they are drawn
//         near and are not selected when you double-click a bond, atom, or
//         atom label with a selection tool."
//   p241 s-orbital  : "Click an atom in the drawing where the orbital will be
//         centered."
//   p241 sigma      : click an atom where the orbital will be centered.
//   p241 single lobe: "Click an atom where the NARROW end of the orbital will
//         be attached."
//   p240 p-orbital  : "Click an atom where the NODE of the orbital will be
//         attached."
//   p240 hybrid sp3 : click an atom where the node will be attached.
//   p240 d-orbitals : dxy and dz2, click an atom where the node is attached.
//   p242 "You can move any attached chemical symbol (other than H-dot and
//         H-dash) any distance from its related atom."
//   p242 "You can place unattached symbols anywhere within your document and
//         resize them."
//   p242 lone pair: "Click and drag the atom to where you want the lone pair.
//         The lone pair is offset from the atom at a fixed position."
//   p242 "With the lone pair tool selected, if you ALT+click on an atom, it
//         will place a lone pair BAR symbol instead of a lone pair DOUBLE DOTS
//         symbol."
//   p242 H-dot / H-dash: out of / into the plane along Z; "To insert H-dots
//         and H-dashes, click an atom."
//   p242 "The lone pair symbol is interpreted as a DIRADICAL when it is placed
//         next to an unlabeled carbon atom."
//   p242 charge: "Click and drag the atom to which you want the charge to
//         correspond. The number of hydrogen atoms increases or decreases as
//         appropriate for the addition of the charge."
//   p243 "The bead tools are intended to indicate attachment to a resin."
//   p243 "You can resize a polymer bead by selecting it with a marquee tool,
//         and dragging the resize drag handles."
//   p243 "You can rotate the radical anion, cation, and lone pair symbols
//         around the same end from which they were originally drawn."
//   p158 ELECTRON PUSHING: "Click and drag the mouse from the source object to
//         the target object and release the mouse. A FULL-HEAD arrow appears
//         signifying the electron PAIR transfer."
//   p158 RADICAL PUSHING:  "... A HALF-HEAD arrow signifying only ONE electron
//         transfer appears."
//   p158 "You can delete the electron transfer arrow by either selecting or
//         hovering over the arrow and pressing <DELETE>."
//   p158 "When you hover over the electron or radical pushing arrows, the
//         associated objects are highlighted in addition to the arrows."
//
// The FULL-HEAD vs HALF-HEAD distinction is the load-bearing one: it is the
// documented, user-visible difference between an electron-PAIR push and a
// single-electron push, and the acceptance criterion asks for it explicitly.
//
// NUMERIC VALUES (dot spacing, bar length, ring radius factor, lobe aspect) are
// [PROJECT] choices: the guide states the SEMANTICS and the anchor point of each
// orbital, never millimetre geometry. They are multiples of the document font
// size / bond length so a preset scales with the document.
// =============================================================================

#include "next/core/contracts.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace r62 {
namespace symbols {

enum {
    kSymbolsMajor    = 1,
    kSymbolsMinor    = 0,
    kSymbolsRevision = 0,
    kSymbolsVersion  = kSymbolsMajor * 10000 + kSymbolsMinor * 100 + kSymbolsRevision
};

inline int compiledCoreInterfaceVersion() { return core::kInterfaceVersion; }

inline bool requestSymbolsCompatibility(int major, int minor) {
    return major == kSymbolsMajor && minor <= kSymbolsMinor;
}

// Sentinel for "no preset resolved". Deliberately NOT core::kInvalidObjectId
// (that is the OBJECT-id sentinel and would be encodable as preset=4294967295)
// and deliberately NOT core::kPresetNone (that lives in tool_registry.hpp).
static const uint32_t kNoPreset = 0xFFFFFFFEu;

enum class SOFamily : uint8_t {
    Symbol = 0,    // a chemical symbol glyph (charge / radical / H / attachment)
    Push   = 1,    // an electron or single-electron transfer trajectory
    Orbital = 2    // an orbital outline / lobe diagram
};

const char* familyName(SOFamily f);

// ---------------------------------------------------------------------------
// SYMBOL AXES
// ---------------------------------------------------------------------------
enum class SymbolKind : uint8_t {
    None = 0,
    Ring,        // a circle outline; carries a sign inside (charge ring)
    Sign,        // a bare plus or minus, no enclosing circle
    DotPair,     // two side-by-side dots (lone pair / doublet radical)
    Dot,         // a single dot (mono-radical)
    Bar,         // a short bar (lone-pair BAR form, Alt+click per [GUIDE p242])
    HDot,        // an H with a leading dot  (H coming OUT of the plane)
    HDash,       // an H with a leading dash (H going INTO the plane)
    Attachment,  // diamond / star / wavy attachment point stub
    Bead         // a polymer bead (resin attachment) circle
};

const char* symbolKindName(SymbolKind k);

enum class GlyphSign : int8_t {
    None  = 0,
    Plus  = 1,
    Minus = -1
};

enum class SymbolAnchor : uint8_t {
    Free = 0,       // unattached; may be placed anywhere and resized [p242]
    Centered,       // s-orbital / sigma: centered ON the atom [p241]
    Node,           // the node of the shape sits on the atom [p240]
    NarrowEnd,      // the narrow end of a single lobe [p241]
    RingEnd         // the charge-end of a rotatable charge-radical [p243]
};

const char* symbolAnchorName(SymbolAnchor a);

enum class SymbolMeaning : uint8_t {
    Decorative = 0,   // no chemical field is ever touched
    Charge,           // +1
    ChargeMinus,      // -1
    ChargePlusMinus,  // ring with a sign parameterised in `sign`
    RadicalMono,      // one unpaired electron (dot / radical cation / anion)
    RadicalDouble,    // two unpaired electrons (double radical)
    LonePair,         // two dots; interpreted as diradical only when opted in
    AttachmentPoint,  // node type Attachment / ExternalConnection
    PolymerBead       // node type Polymer
};

const char* symbolMeaningName(SymbolMeaning m);

enum class PushHead : uint8_t {
    FullPair = 0,   // a solid full arrowhead  -> electron PAIR
    HalfRadical = 1 // a single-sided barb      -> ONE electron
};

const char* pushHeadName(PushHead h);

enum class PushEndpoint : uint8_t {
    None = 0,
    Atom,
    Bond,
    Orbital
};

const char* pushEndpointName(PushEndpoint e);

// ---------------------------------------------------------------------------
// ORBITAL AXES
// ---------------------------------------------------------------------------
enum class OrbitalShape : uint8_t {
    S = 0,        // a circle, centered on the atom            [GUIDE p241]
    Sigma,        // a circle with a node marker, centered     [GUIDE p241]
    SingleLobe,   // ONE lobe; narrow end on the atom          [GUIDE p241]
    P,            // TWO lobes with a node; node on the atom   [GUIDE p240]
    HybridSp3,    // an asymmetric two-lobe (large+small)      [GUIDE p240]
    Dxy,          // FOUR lobes in a cloverleaf                [GUIDE p240]
    Dz2           // TWO large lobes + a torus/donut           [GUIDE p240]
};

const char* orbitalShapeName(OrbitalShape s);

// Fill / shading vocabulary, matching the toolbar's own suffix words:
//   _Open   - outline only, no fill
//   _Shaded - outline + a hatch/stipple overlay
//   _Solid  - outline + a flat fill
//
// _Shaded2 / _Solid2 are the toolbar's SECOND variant of the same word, drawn
// with the PHASE INVERTED. They are kept as distinct rows distinguished by
// `invertPhase`, so "opposite-phase fill" is a stored parameter, not a cosmetic
// difference invisible in the model.
enum class OrbitalFill : uint8_t {
    Open = 0,
    Shaded,
    Solid
};

const char* orbitalFillName(OrbitalFill f);

// ---------------------------------------------------------------------------
// SymbolParams: the resolved bundle for ONE chemical symbol object
// ---------------------------------------------------------------------------
struct SymbolParams {
    uint32_t      presetId;
    SymbolKind    kind;
    GlyphSign     sign;
    SymbolMeaning meaning;
    SymbolAnchor  anchor;
    core::Vec2    position;      // document units: the symbol ANCHOR point
    double        rotation;      // degrees, CCW math orientation
    double        scale;         // 1.0 == preset default; resizable [p242]
    double        size;          // document units; 0 == derive from font size
    int           chargeDelta;   // +1 / -1 / 0
    int           radicalValue;  // core::Radical as int; 0 == no change
    bool          rotateFromEnd; // rotatable about the charge end [GUIDE p243]
    bool          resizable;     // marquee-resizable [p242/p243]
    bool          drawCircle;    // ring forms draw an enclosing circle

    SymbolParams()
        : presetId(kNoPreset), kind(SymbolKind::None), sign(GlyphSign::None),
          meaning(SymbolMeaning::Decorative), anchor(SymbolAnchor::Free),
          position(0, 0), rotation(0.0), scale(1.0), size(0.0), chargeDelta(0),
          radicalValue(0), rotateFromEnd(false), resizable(true),
          drawCircle(false) {}
};

// ---------------------------------------------------------------------------
// PushParams: the resolved bundle for ONE electron-transfer trajectory
// ---------------------------------------------------------------------------
// The trajectory GEOMETRY is owned by T10 (arrows / curves): T12 stores only the
// two endpoints plus which of T10's arrow shapes to use, and hands the actual
// flattening to T10 at render time. Deliberate - the acceptance criterion says
// "electron push trajectories use T10's path/arrow component", so T12 must not
// grow a second curve flattener.
struct PushParams {
    uint32_t      presetId;
    PushHead      head;
    core::Vec2    begin;         // source object centre
    core::Vec2    end;           // target object centre
    double        sagitta;       // chord-normal bulge, document units
    core::ObjectRef sourceObject;
    core::ObjectRef targetObject;
    PushEndpoint  sourceKind;
    PushEndpoint  targetKind;
    double        scale;

    PushParams()
        : presetId(kNoPreset), head(PushHead::FullPair), begin(0, 0), end(0, 0),
          sagitta(0.0), sourceKind(PushEndpoint::None),
          targetKind(PushEndpoint::None), scale(1.0) {}
};

// ---------------------------------------------------------------------------
// OrbitalParams: the resolved bundle for ONE orbital diagram
// ---------------------------------------------------------------------------
struct OrbitalParams {
    uint32_t       presetId;
    OrbitalShape   shape;
    OrbitalFill    fill;
    bool           invertPhase;  // the *_Shaded2 / *_Solid2 rows
    SymbolAnchor   anchor;
    core::Vec2     position;     // document units: the ANCHOR point
    double         rotation;     // degrees, CCW math orientation
    double         length;       // document units; major axis [p241 Info window]
    double         width;        // document units; minor axis
    double         scale;
    int            lobeCount;    // derived from shape; stored so it is inspectable
    bool           lobeFilled[6];
    bool           lobePositive[6];

    OrbitalParams()
        : presetId(kNoPreset), shape(OrbitalShape::S), fill(OrbitalFill::Open),
          invertPhase(false), anchor(SymbolAnchor::Centered), position(0, 0),
          rotation(0.0), length(24.0), width(16.0), scale(1.0), lobeCount(1) {
        for (int i = 0; i < 6; ++i) { lobeFilled[i] = false; lobePositive[i] = true; }
    }
};

// ---------------------------------------------------------------------------
// Preset rows
// ---------------------------------------------------------------------------
struct SymbolPresetSpec {
    uint32_t       presetId;
    const char*    sourceCommand;  // tools.csv command, never NULL
    const char*    toolId;         // stable namespaced registry id
    const char*    displayName;
    SOFamily       family;
    SymbolKind     kind;
    GlyphSign      sign;
    SymbolMeaning  meaning;
    SymbolAnchor   anchor;
    int            chargeDelta;
    int            radicalValue;   // core::Radical value; 0 == no change
    bool           rotateFromEnd;
    bool           resizable;
    bool           drawCircle;
    bool           implemented;
    const char*    note;
};

struct PushPresetSpec {
    uint32_t    presetId;
    const char* sourceCommand;
    const char* toolId;
    const char* displayName;
    SOFamily    family;
    PushHead    head;
    bool        implemented;
    const char* note;
};

struct OrbitalPresetSpec {
    uint32_t       presetId;
    const char*    sourceCommand;
    const char*    toolId;
    const char*    displayName;
    SOFamily       family;
    OrbitalShape   shape;
    OrbitalFill    fill;
    bool           invertPhase;
    SymbolAnchor   anchor;
    int            lobeCount;
    bool           implemented;
    const char*    note;
};

// ---------------------------------------------------------------------------
// The tables
// ---------------------------------------------------------------------------
// 16 rows, in Chemical_Symbols_Toolbar.xml order (8x2 palette, column 1..8 of
// row 1 then row 2). The order is preserved because the guide's figure captions
// "A) CirclePlus ... P) Circleminus" refer to it, and because presetId is
// PERSISTED: never reorder.
const SymbolPresetSpec* symbolPresets();
size_t                  symbolPresetCount();       // == 16

const PushPresetSpec*   pushPresets();
size_t                  pushPresetCount();         // == 2

// 21 rows, in Orbital_Toolbar.xml order (column-major over the 7x3 palette,
// which is the order tools.csv preserved).
const OrbitalPresetSpec* orbitalPresets();
size_t                   orbitalPresetCount();     // == 21

const SymbolPresetSpec*  symbolPresetById(uint32_t id);
const SymbolPresetSpec*  symbolPresetByCommand(const std::string& cmd);
const SymbolPresetSpec*  symbolPresetByToolId(const std::string& toolId);
const PushPresetSpec*    pushPresetById(uint32_t id);
const PushPresetSpec*    pushPresetByCommand(const std::string& cmd);
const OrbitalPresetSpec* orbitalPresetById(uint32_t id);
const OrbitalPresetSpec* orbitalPresetByCommand(const std::string& cmd);
const OrbitalPresetSpec* orbitalPresetByToolId(const std::string& toolId);

// True when `command` is one of the 37 source commands this module owns.
bool isOwnedCommand(const std::string& command);
SOFamily familyOfCommand(const std::string& command, bool* known);

// ---------------------------------------------------------------------------
// Resolution: preset + gesture -> a fully specified parameter bundle
// ---------------------------------------------------------------------------
SymbolParams  resolveSymbolPreset(uint32_t presetId, const core::Vec2& anchorPoint,
                                  double documentTextSize);
OrbitalParams resolveOrbitalPreset(uint32_t presetId, const core::Vec2& anchorPoint,
                                   double documentBondLength, double documentTextSize);
PushParams    resolvePushPreset(uint32_t presetId, const core::Vec2& begin,
                                const core::Vec2& end);

OrbitalParams applyOrbitalStyle(const OrbitalParams& current, uint32_t presetId,
                                double documentBondLength);
SymbolParams  applySymbolStyle(const SymbolParams& current, uint32_t presetId);

// The lone-pair BAR form, produced by ALT+click with the lone-pair tool
// [GUIDE p242].
SymbolParams lonePairBarVariant(const SymbolParams& lonePair);

// ---------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------
core::Error validateSymbolParams(const SymbolParams& p);
core::Error validatePushParams(const PushParams& p);
core::Error validateOrbitalParams(const OrbitalParams& p);

int  orbitalLobeCount(OrbitalShape shape);
bool orbitalIsDirectional(OrbitalShape shape);
bool orbitalIsCentered(OrbitalShape shape);

} // namespace symbols
} // namespace r62

#endif // R62_NEXT_SYMBOLS_ORBITALS_PRESET_HPP
