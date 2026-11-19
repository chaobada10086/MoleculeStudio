#ifndef R62_NEXT_INTERACTION_KEYMAP_HPP
#define R62_NEXT_INTERACTION_KEYMAP_HPP
// =============================================================================
// re_round62_viewer/next/interaction/keymap.hpp
//
// T04 - THE HOTKEY TABLE, WITH A SOURCE COLUMN FOR EVERY ROW.
//
// RULE: NO BINDING EXISTS WITHOUT A CITED SOURCE. The task statement forbids
// mapping keys from memory. Every entry below carries a HotkeySource value that
// says exactly where the mapping came from, and the test suite asserts that no
// row in the table has HotkeySource::Uncited.
//
// Sources used:
//  [G70-77] re_round55/t1/runtime/appcopy/ChemDraw.pdf, PDF reader pages 70-77
//           (SHA-256 E039B216...BFAE0), transcribed in
//           tests/chemdraw_hotkeys_excerpt.txt.
//  [G81-85] same PDF, pages 81-85 (undo/redo/repeat, fixed length/angle, zoom,
//           marquee, Shift/Ctrl drag modifiers).
//  [G92-93] same PDF, pages 92-93 (15-degree fixed angles, 120-degree chains,
//           Alt temporarily releases both constraints).
//  [G124-126] same PDF, pages 124-126 (Shift+arrow nudge, Ctrl+J merge).
//  [EBOOK4]  revvitysignals.com ChemDraw tips-and-tricks eBook, page 4
//           (Spacebar selects the current molecule; Enter inserts a hotspot).
//  [PROJECT] A deliberate project decision, recorded as such. Never presented
//           as an official ChemDraw constant.
//
// -----------------------------------------------------------------------------
// WHAT THIS TABLE IS NOT
// -----------------------------------------------------------------------------
// It is NOT a claim that the surrounding engine implements the action. A row
// says "when the platform sees this key in this context, the router resolves it
// to THIS ACTION". Whether that action is implemented is a separate field
// (HotkeyBinding::implemented) and an unimplemented action MUST return
// HotkeyOutcome::Unsupported so the UI can say so honestly. Silently turning an
// unimplemented ChemDraw action into a DIFFERENT working tool is the exact
// failure mode the task calls out: "do not swap it for another tool".
// -----------------------------------------------------------------------------

#include "input_event.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace r62 {
namespace interaction {

// ---------------------------------------------------------------------------
// HotspotContext: the SAME physical key means different things depending on what
// is under the pointer. ChemDraw resolves the key against the hotspot FIRST and
// falls back to the generic tool table only when there is no hotspot.
// [G70-77] is explicit about this ordering.
// ---------------------------------------------------------------------------
enum class HotspotContext : uint8_t {
    None = 0,   // pointer not on an atom or a bond -> GENERIC table
    Atom,       // atom hotspot -> ATOM table
    Bond        // bond hotspot -> BOND table
};

const char* hotspotContextName(HotspotContext context);

// ---------------------------------------------------------------------------
// HotkeySource: provenance for a binding
// ---------------------------------------------------------------------------
enum class HotkeySource : uint8_t {
    Uncited = 0,     // MUST NOT appear in the table; asserted by the tests
    Guide70,         // ChemDraw.pdf pp. 70-77 hotkey tables
    Guide81,         // ChemDraw.pdf pp. 81-85 commands / modifiers
    Guide92,         // ChemDraw.pdf pp. 92-93 geometry constraints
    Guide124,        // ChemDraw.pdf pp. 124-126 merge + arrow nudge
    EBooK4,          // official Revvity eBook page 4 (hotspot / molecule)
    Project          // deliberate project policy, not an official constant
};

const char* hotkeySourceName(HotkeySource source);
// Citation text suitable for a coverage report: file/page/URL.
const char* hotkeySourceCitation(HotkeySource source);

// ---------------------------------------------------------------------------
// HotkeyAction: WHAT the key means (context-specific), not HOW it is done
// ---------------------------------------------------------------------------
// The action vocabulary is deliberately chemical/editor-level, not Win32-level.
// 'SelectToolBondSolid' is not the same action as 'SetBondOrderDouble': the
// first changes the active tool, the second edits the hotspot bond, and
// conflating them is precisely the bug the task warns about for keys 1/2/3.
enum class HotkeyAction : uint16_t {
    None = 0,

    // --- atom hotspot: label edits. [G70-75] ---------------------------------
    SetAtomElement,        // c/n/o/s/f/p/h/i/b/l/... -> the element in `element`
    SetAtomLabel,          // Shift+f -> CF3, Shift+p -> Ph, m -> Me, ...
    SetAtomChargeDelta,    // '-' / '+' [G70-75]
    SetAtomNumber,         // ' (apostrophe) -> atom numbering [G70-75]
    InsertAttachmentPoint, // '.' -> add a connection point [G70-75]
    ClearAtomLabel,        // Backspace / Delete -> clear the label [G70-75]
    EditAtomLabel,         // Enter -> edit the atom label [G70-75]
    SelectHotspotAtom,     // g -> select the hotspot atom [G70-75]
    SelectMolecule,        // Spacebar on a hotspot -> select the molecule [EBooK4]
    SelectFragment,        // Enter with a fragment selected -> restore hotspot [G80]
    OpenNicknameDialog,    // '=' -> nickname dialog [G70-75]
    OpenAtomProperties,    // / or ? -> atom properties [G70-75]
    SproutBond,            // 1 -> sprout a single bond, linear [G70-75]
    SproutCarbonyl,        // 2 -> environment-dependent carbonyl/acetyl [G70-75]
    SproutPhenyl,          // 3 or a -> environment-dependent phenyl [G70-75]
    SproutWedge,           // 4 -> wedge bond [G70-75]
    SproutHashWedge,       // 5 -> hashed wedge bond [G70-75]
    SproutSixRing,         // 6 -> six-membered ring sprout / spiro [G70-75]
    SproutFiveRing,        // 7 -> five-membered ring sprout / spiro [G70-75]
    SproutMethylene,       // 8 -> methylene operation [G70-75]
    SproutDimethyl,        // 9 -> dimethyl operation [G70-75]
    SproutAlkyne,          // z -> alkyne sprout [G70-75]
    SproutCyclopentadienyl,// j / Shift+j -> metallocene-type ligand [G70-75]
    SproutTBu,             // Shift+k -> 90-degree t-Bu [G70-75]
    SproutEnvironmentBond, // 0 -> environment-dependent sprout [G70-75]

    // NOTE on 'k': the guide lists BOTH a K label and a sulfonyl sprout; the
    // androidx excerpt refuses to pick one statically [G70-75]. k is therefore
    // AMBIGUOUS and the router reports Unsupported rather than guessing.
    AmbiguousGuideBinding,

    // --- bond hotspot: order + display + ring fusion. [G75-76] -------------
    SetBondOrderSingle,    // 1
    SetBondOrderDouble,    // 2
    SetBondOrderTriple,    // 3
    SetBondDisplayDashed,  // d
    SetBondDisplayDashDouble, // Shift+d
    SetBondDisplayBoldDouble, // Shift+b
    SetBondDisplayWavy,    // y
    SetBondDoubleLeft,     // l
    SetBondDoubleCenter,   // c
    SetBondDoubleRight,    // r
    SetBondDisplayBold,    // b
    SetBondDisplayWedge,   // w
    SetBondFront,          // f
    SetBondDisplayHash,    // Shift+h
    SetBondDisplayHashWedge, // h
    SelectHotspotBond,     // g
    FuseBenzene,           // a
    FuseCyclopentadiene,   // z
    FuseRing,              // 4/5/6/7/8
    ChairRingOrientation,  // 9 / 0
    OpenBondProperties,    // / or ?

    // --- generic table (no hotspot). [G77] ----------------------------------
    ActivateMarqueeTool,   // Spacebar -> marquee selection
    ActivateBondTool,      // x -> solid bond tool
    ActivateChainTool,     // Shift+x -> chain tool
    ActivateTextTool,      // t
    ActivateBracketTool,   // Shift+t
    ActivateArrowTool,     // e
    ActivateSymbolTool,    // Shift+e
    ActivateBenzeneTool,   // j
    ActivateCyclopentadieneTool, // Shift+j
    ActivateChromatographyTool,  // g
    ActivateOrbitalTool,   // Shift+g

    // --- global commands. [G81-85], [G124-126] ------------------------------
    Undo,
    Redo,
    RepeatLastAction,      // Ctrl+Y - repeat, NOT redo [G81]
    ToggleFixedLength,     // Ctrl+L [G82]
    ToggleFixedAngles,     // Ctrl+E [G82]
    ZoomActualSize,        // F5
    ZoomFit,               // F6
    ZoomIn,                // F7
    ZoomOut,               // F8
    EditCopy,              // Ctrl+C [G81]
    EditPaste,             // Ctrl+V [G81]
    EditCut,               // Ctrl+X [G81]
    EditSelectAll,         // Ctrl+A [G81]
    EditDelete,            // Delete [G81]
    NudgeSelection,        // Shift+arrow, 10 pt [G124]
    MoveSelection,         // arrow key (project granularity)
    MergeSelection,        // Ctrl+J [G125-126]
    FileNew, FileOpen, FileSave, FileSaveAs,   // accelerators [PROJECT]
    CancelGesture,         // Escape
    CommitTextEntry,       // Enter while editing text

    Ambiguous,             // guide documents more than one meaning
    Unsupported            // recognised as a ChemDraw key, but out of scope here
};

const char* hotkeyActionName(HotkeyAction action);

// ---------------------------------------------------------------------------
// HotkeyBinding
// ---------------------------------------------------------------------------
struct HotkeyBinding {
    HotspotContext context;
    uint32_t       virtualKey;    // VK_* value, or 0 when keyed on a character
    uint32_t       character;     // exact UTF-16 unit, or 0 when keyed on a VK
    uint32_t       modifiers;     // REQUIRED modifier bits
    uint32_t       forbiddenModifiers; // modifier bits that must be ABSENT
    HotkeyAction  action;
    HotkeySource  source;
    int            element;       // for SetAtomElement: 1..118, else 0
    std::string    label;         // for SetAtomLabel: "CF3", "Ph", "Me", ...
    int            parameter;     // ring size / double-bond side / order value
    bool           implemented;   // false -> router reports Unsupported
    HotkeyBinding()
        : context(HotspotContext::None), virtualKey(0), character(0),
          modifiers(kModNone), forbiddenModifiers(kModNone),
          action(HotkeyAction::None), source(HotkeySource::Uncited),
          element(0), parameter(0), implemented(false) {}
};

// Case sensitivity is a documented property, not an accident. A character binding
// is matched against the EXACT code unit the platform delivered.
//   lowercase 'c'  -> carbon   (element 6)
//   'C' (Shift+c)  -> chlorine (element 17)
//   lowercase 'l'  -> chlorine (element 17)
//   lowercase 'b'  -> bromine  (element 35)
//   'B' (Shift+b)  -> boron    (element 5)
// The test suite asserts each of these five pairs explicitly.

// ---------------------------------------------------------------------------
// Keymap: lookup + introspection
// ---------------------------------------------------------------------------
class Keymap {
public:
    // The one and only default table. Populated once, immutable afterwards.
    static const Keymap& defaults();

    // Resolve a key press. `character` is the exact code unit (may be 0 for a
    // key that produces no character, e.g. an arrow or F-key); `virtualKey` is
    // the VK_* value. Both are supplied so an accelerator can be VK-based while
    // a label edit stays character- and case-based.
    //
    // The hotspot context is tried explicitly: Atom, then Bond, then None. This
    // mirrors "hotspot context takes priority over the generic tool keys".
    const HotkeyBinding* resolve(HotspotContext context, uint32_t virtualKey,
                                 uint32_t character, uint32_t modifiers) const;

    // Every binding, in table order. Used by the coverage report and the test
    // that asserts every row is cited.
    const std::vector<HotkeyBinding>& bindings() const { return bindings_; }

    // Bindings matching a filter, for coverage.csv generation.
    std::vector<const HotkeyBinding*> bySource(HotkeySource source) const;
    std::vector<const HotkeyBinding*> byAction(HotkeyAction action) const;
    size_t implementedCount() const;

    // Add/override a binding. Used by tests and by a future hotkey.xml reader;
    // the default table is never mutated by production code.
    void add(const HotkeyBinding& binding);

private:
    std::vector<HotkeyBinding> bindings_;
    bool matches(const HotkeyBinding& b, uint32_t virtualKey, uint32_t character,
                 uint32_t modifiers) const;
};

// ---------------------------------------------------------------------------
// Accelerator table: Ctrl-combinations that are NOT hotspot dependent
// ---------------------------------------------------------------------------
// Separated from Keymap because these are resolved BEFORE the hotspot is even
// computed in ChemDraw (an accelerator is a command, not a chemistry key). The
// separation is also what makes "Ctrl+Z must not be re-interpreted as an atom
// label" a structural property rather than a comment.
struct Accelerator {
    uint32_t     virtualKey;
    uint32_t     modifiers;
    HotkeyAction action;
    HotkeySource source;
    bool         implemented;
    Accelerator()
        : virtualKey(0), modifiers(kModNone), action(HotkeyAction::None),
          source(HotkeySource::Uncited), implemented(false) {}
};

const std::vector<Accelerator>& defaultAccelerators();
const Accelerator* resolveAccelerator(uint32_t virtualKey, uint32_t modifiers);

} // namespace interaction
} // namespace r62

#endif // R62_NEXT_INTERACTION_KEYMAP_HPP
