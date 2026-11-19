#include "keymap.hpp"

#include <algorithm>
#include <cstring>

namespace r62 {
namespace interaction {

// ---------------------------------------------------------------------------
// Names
// ---------------------------------------------------------------------------
const char* inputFocusName(InputFocus focus) {
    switch (focus) {
    case InputFocus::Canvas: return "Canvas";
    case InputFocus::TextEntry: return "TextEntry";
    case InputFocus::ImeComposition: return "ImeComposition";
    case InputFocus::MenuOpen: return "MenuOpen";
    case InputFocus::ModalDialog: return "ModalDialog";
    case InputFocus::DragCapture: return "DragCapture";
    case InputFocus::HostOverlay: return "HostOverlay";
    }
    return "Unknown";
}

const char* inputKindName(InputKind kind) {
    switch (kind) {
    case InputKind::PointerDown: return "PointerDown";
    case InputKind::PointerMove: return "PointerMove";
    case InputKind::PointerUp: return "PointerUp";
    case InputKind::PointerWheel: return "PointerWheel";
    case InputKind::KeyDown: return "KeyDown";
    case InputKind::KeyUp: return "KeyUp";
    case InputKind::Character: return "Character";
    case InputKind::HotkeyCommand: return "HotkeyCommand";
    case InputKind::FocusLost: return "FocusLost";
    case InputKind::CaptureLost: return "CaptureLost";
    case InputKind::Cancel: return "Cancel";
    case InputKind::DocumentChanged: return "DocumentChanged";
    case InputKind::ViewChanged: return "ViewChanged";
    }
    return "Unknown";
}

const char* hotspotContextName(HotspotContext context) {
    switch (context) {
    case HotspotContext::None: return "None";
    case HotspotContext::Atom: return "Atom";
    case HotspotContext::Bond: return "Bond";
    }
    return "Unknown";
}

const char* hotkeySourceName(HotkeySource source) {
    switch (source) {
    case HotkeySource::Uncited: return "Uncited";
    case HotkeySource::Guide70: return "Guide70-77";
    case HotkeySource::Guide81: return "Guide81-85";
    case HotkeySource::Guide92: return "Guide92-93";
    case HotkeySource::Guide124: return "Guide124-126";
    case HotkeySource::EBooK4: return "EBooK4";
    case HotkeySource::Project: return "Project";
    }
    return "Unknown";
}

const char* hotkeySourceCitation(HotkeySource source) {
    switch (source) {
    case HotkeySource::Uncited:
        return "UNSOURCED - must not be used";
    case HotkeySource::Guide70:
        return "re_round55/t1/runtime/appcopy/ChemDraw.pdf pp.70-77 "
               "(SHA256 E039B2160C027996A4BE901630FC274FBC75F2F7E872385BA6BDDB78375BFAE0)";
    case HotkeySource::Guide81:
        return "re_round55/t1/runtime/appcopy/ChemDraw.pdf pp.81-85";
    case HotkeySource::Guide92:
        return "re_round55/t1/runtime/appcopy/ChemDraw.pdf pp.92-93";
    case HotkeySource::Guide124:
        return "re_round55/t1/runtime/appcopy/ChemDraw.pdf pp.124-126";
    case HotkeySource::EBooK4:
        return "revvitysignals.com rs-e-book-chemdraw-tips-and-tricks-eBook-02132024.pdf p.4";
    case HotkeySource::Project:
        return "project decision - NOT an official ChemDraw constant";
    }
    return "Unknown";
}

const char* hotkeyActionName(HotkeyAction action) {
    switch (action) {
    case HotkeyAction::None: return "None";
    case HotkeyAction::SetAtomElement: return "SetAtomElement";
    case HotkeyAction::SetAtomLabel: return "SetAtomLabel";
    case HotkeyAction::SetAtomChargeDelta: return "SetAtomChargeDelta";
    case HotkeyAction::SetAtomNumber: return "SetAtomNumber";
    case HotkeyAction::InsertAttachmentPoint: return "InsertAttachmentPoint";
    case HotkeyAction::ClearAtomLabel: return "ClearAtomLabel";
    case HotkeyAction::EditAtomLabel: return "EditAtomLabel";
    case HotkeyAction::SelectHotspotAtom: return "SelectHotspotAtom";
    case HotkeyAction::SelectMolecule: return "SelectMolecule";
    case HotkeyAction::SelectFragment: return "SelectFragment";
    case HotkeyAction::OpenNicknameDialog: return "OpenNicknameDialog";
    case HotkeyAction::OpenAtomProperties: return "OpenAtomProperties";
    case HotkeyAction::SproutBond: return "SproutBond";
    case HotkeyAction::SproutCarbonyl: return "SproutCarbonyl";
    case HotkeyAction::SproutPhenyl: return "SproutPhenyl";
    case HotkeyAction::SproutWedge: return "SproutWedge";
    case HotkeyAction::SproutHashWedge: return "SproutHashWedge";
    case HotkeyAction::SproutSixRing: return "SproutSixRing";
    case HotkeyAction::SproutFiveRing: return "SproutFiveRing";
    case HotkeyAction::SproutMethylene: return "SproutMethylene";
    case HotkeyAction::SproutDimethyl: return "SproutDimethyl";
    case HotkeyAction::SproutAlkyne: return "SproutAlkyne";
    case HotkeyAction::SproutCyclopentadienyl: return "SproutCyclopentadienyl";
    case HotkeyAction::SproutTBu: return "SproutTBu";
    case HotkeyAction::SproutEnvironmentBond: return "SproutEnvironmentBond";
    case HotkeyAction::AmbiguousGuideBinding: return "AmbiguousGuideBinding";
    case HotkeyAction::SetBondOrderSingle: return "SetBondOrderSingle";
    case HotkeyAction::SetBondOrderDouble: return "SetBondOrderDouble";
    case HotkeyAction::SetBondOrderTriple: return "SetBondOrderTriple";
    case HotkeyAction::SetBondDisplayDashed: return "SetBondDisplayDashed";
    case HotkeyAction::SetBondDisplayDashDouble: return "SetBondDisplayDashDouble";
    case HotkeyAction::SetBondDisplayBoldDouble: return "SetBondDisplayBoldDouble";
    case HotkeyAction::SetBondDisplayWavy: return "SetBondDisplayWavy";
    case HotkeyAction::SetBondDoubleLeft: return "SetBondDoubleLeft";
    case HotkeyAction::SetBondDoubleCenter: return "SetBondDoubleCenter";
    case HotkeyAction::SetBondDoubleRight: return "SetBondDoubleRight";
    case HotkeyAction::SetBondDisplayBold: return "SetBondDisplayBold";
    case HotkeyAction::SetBondDisplayWedge: return "SetBondDisplayWedge";
    case HotkeyAction::SetBondFront: return "SetBondFront";
    case HotkeyAction::SetBondDisplayHash: return "SetBondDisplayHash";
    case HotkeyAction::SetBondDisplayHashWedge: return "SetBondDisplayHashWedge";
    case HotkeyAction::SelectHotspotBond: return "SelectHotspotBond";
    case HotkeyAction::FuseBenzene: return "FuseBenzene";
    case HotkeyAction::FuseCyclopentadiene: return "FuseCyclopentadiene";
    case HotkeyAction::FuseRing: return "FuseRing";
    case HotkeyAction::ChairRingOrientation: return "ChairRingOrientation";
    case HotkeyAction::OpenBondProperties: return "OpenBondProperties";
    case HotkeyAction::ActivateMarqueeTool: return "ActivateMarqueeTool";
    case HotkeyAction::ActivateBondTool: return "ActivateBondTool";
    case HotkeyAction::ActivateChainTool: return "ActivateChainTool";
    case HotkeyAction::ActivateTextTool: return "ActivateTextTool";
    case HotkeyAction::ActivateBracketTool: return "ActivateBracketTool";
    case HotkeyAction::ActivateArrowTool: return "ActivateArrowTool";
    case HotkeyAction::ActivateSymbolTool: return "ActivateSymbolTool";
    case HotkeyAction::ActivateBenzeneTool: return "ActivateBenzeneTool";
    case HotkeyAction::ActivateCyclopentadieneTool: return "ActivateCyclopentadieneTool";
    case HotkeyAction::ActivateChromatographyTool: return "ActivateChromatographyTool";
    case HotkeyAction::ActivateOrbitalTool: return "ActivateOrbitalTool";
    case HotkeyAction::Undo: return "Undo";
    case HotkeyAction::Redo: return "Redo";
    case HotkeyAction::RepeatLastAction: return "RepeatLastAction";
    case HotkeyAction::ToggleFixedLength: return "ToggleFixedLength";
    case HotkeyAction::ToggleFixedAngles: return "ToggleFixedAngles";
    case HotkeyAction::ZoomActualSize: return "ZoomActualSize";
    case HotkeyAction::ZoomFit: return "ZoomFit";
    case HotkeyAction::ZoomIn: return "ZoomIn";
    case HotkeyAction::ZoomOut: return "ZoomOut";
    case HotkeyAction::EditCopy: return "EditCopy";
    case HotkeyAction::EditPaste: return "EditPaste";
    case HotkeyAction::EditCut: return "EditCut";
    case HotkeyAction::EditSelectAll: return "EditSelectAll";
    case HotkeyAction::EditDelete: return "EditDelete";
    case HotkeyAction::NudgeSelection: return "NudgeSelection";
    case HotkeyAction::MoveSelection: return "MoveSelection";
    case HotkeyAction::MergeSelection: return "MergeSelection";
    case HotkeyAction::FileNew: return "FileNew";
    case HotkeyAction::FileOpen: return "FileOpen";
    case HotkeyAction::FileSave: return "FileSave";
    case HotkeyAction::FileSaveAs: return "FileSaveAs";
    case HotkeyAction::CancelGesture: return "CancelGesture";
    case HotkeyAction::CommitTextEntry: return "CommitTextEntry";
    case HotkeyAction::Ambiguous: return "Ambiguous";
    case HotkeyAction::Unsupported: return "Unsupported";
    }
    return "Unknown";
}

// ---------------------------------------------------------------------------
// Table construction helpers
// ---------------------------------------------------------------------------
namespace {

HotkeyBinding charBinding(HotspotContext context, char key, uint32_t mods,
                          HotkeyAction action, HotkeySource source, int element,
                          const char* label, int parameter, bool implemented) {
    HotkeyBinding b;
    b.context = context;
    b.character = static_cast<uint32_t>(static_cast<unsigned char>(key));
    b.modifiers = mods;
    b.action = action;
    b.source = source;
    b.element = element;
    if (label) b.label = label;
    b.parameter = parameter;
    b.implemented = implemented;
    return b;
}

HotkeyBinding vkBinding(HotspotContext context, uint32_t vk, uint32_t mods,
                        HotkeyAction action, HotkeySource source, int parameter,
                        bool implemented) {
    HotkeyBinding b;
    b.context = context;
    b.virtualKey = vk;
    b.modifiers = mods;
    b.action = action;
    b.source = source;
    b.parameter = parameter;
    b.implemented = implemented;
    return b;
}

} // namespace

// Modifier helper: a character binding implies 'Shift is part of the character'
// for an uppercase key. We therefore forbid Shift only where the guide gives a
// DIFFERENT meaning to the shifted form, which is the whole point of the atom
// table above.

// ---------------------------------------------------------------------------
// The default table
// ---------------------------------------------------------------------------
// ORGANISED BY CONTEXT, exactly as the guide's pages 70-77 are: the ATOM table,
// the BOND table and the GENERIC table. A reader who wants to audit a row can
// put the guide page beside this function and read down the columns.
const Keymap& Keymap::defaults() {
    static Keymap map;
    static bool built = false;
    if (built) return map;
    built = true;

    const uint32_t kShift = kModShift;

    // =======================================================================
    // ATOM HOTSPOT  [G70-75]
    // Characters are matched on the EXACT code unit. Note the pairs:
    //   'c' -> C(6)      vs  'C' (Shift+c) -> Cl(17)
    //   'b' -> Br(35)    vs  'B' (Shift+b) -> B(5)
    //   'n' -> N(7)      vs  'N' (Shift+n) -> NO2 label
    //   'o' -> O(8)      vs  'O' (Shift+o) -> OMe label
    //   'e' -> Et label  vs  'E' (Shift+e) -> CO2Me label
    //   'a' is only defined for the BOND table as benzene fusion; in the atom
    //   table the guide uses Shift+a for the Ac label.
    // =======================================================================
    map.add(charBinding(HotspotContext::Atom, 'f', 0, HotkeyAction::SetAtomElement, HotkeySource::Guide70, 9, 0, 0, true));
    map.add(charBinding(HotspotContext::Atom, 'F', kShift, HotkeyAction::SetAtomLabel, HotkeySource::Guide70, 0, "CF3", 0, true));
    map.add(charBinding(HotspotContext::Atom, 'p', 0, HotkeyAction::SetAtomElement, HotkeySource::Guide70, 15, 0, 0, true));
    map.add(charBinding(HotspotContext::Atom, 'P', kShift, HotkeyAction::SetAtomLabel, HotkeySource::Guide70, 0, "Ph", 0, true));
    map.add(charBinding(HotspotContext::Atom, 'A', kShift, HotkeyAction::SetAtomLabel, HotkeySource::Guide70, 0, "Ac", 0, true));
    map.add(charBinding(HotspotContext::Atom, 'h', 0, HotkeyAction::SetAtomElement, HotkeySource::Guide70, 1, 0, 0, true));
    map.add(charBinding(HotspotContext::Atom, 'H', kShift, HotkeyAction::SetAtomLabel, HotkeySource::Guide70, 0, "Cbz", 0, true));
    map.add(charBinding(HotspotContext::Atom, 'b', 0, HotkeyAction::SetAtomElement, HotkeySource::Guide70, 35, 0, 0, true));
    map.add(charBinding(HotspotContext::Atom, 'B', kShift, HotkeyAction::SetAtomElement, HotkeySource::Guide70, 5, 0, 0, true));
    map.add(charBinding(HotspotContext::Atom, 'i', 0, HotkeyAction::SetAtomElement, HotkeySource::Guide70, 53, 0, 0, true));
    map.add(charBinding(HotspotContext::Atom, 'r', 0, HotkeyAction::SetAtomLabel, HotkeySource::Guide70, 0, "R", 0, true));
    map.add(charBinding(HotspotContext::Atom, 's', 0, HotkeyAction::SetAtomElement, HotkeySource::Guide70, 16, 0, 0, true));
    map.add(charBinding(HotspotContext::Atom, 'S', kShift, HotkeyAction::SetAtomElement, HotkeySource::Guide70, 14, 0, 0, true));
    map.add(charBinding(HotspotContext::Atom, 'm', 0, HotkeyAction::SetAtomLabel, HotkeySource::Guide70, 0, "Me", 0, true));
    map.add(charBinding(HotspotContext::Atom, 'M', kShift, HotkeyAction::SetAtomLabel, HotkeySource::Guide70, 0, "MgBr", 0, true));
    map.add(charBinding(HotspotContext::Atom, 'n', 0, HotkeyAction::SetAtomElement, HotkeySource::Guide70, 7, 0, 0, true));
    map.add(charBinding(HotspotContext::Atom, 'N', kShift, HotkeyAction::SetAtomLabel, HotkeySource::Guide70, 0, "NO2", 0, true));
    map.add(charBinding(HotspotContext::Atom, 'w', 0, HotkeyAction::SetAtomElement, HotkeySource::Guide70, 7, 0, 0, true));
    map.add(charBinding(HotspotContext::Atom, 'c', 0, HotkeyAction::SetAtomElement, HotkeySource::Guide70, 6, 0, 0, true));
    map.add(charBinding(HotspotContext::Atom, 'l', 0, HotkeyAction::SetAtomElement, HotkeySource::Guide70, 17, 0, 0, true));
    map.add(charBinding(HotspotContext::Atom, 'C', kShift, HotkeyAction::SetAtomElement, HotkeySource::Guide70, 17, 0, 0, true));
    map.add(charBinding(HotspotContext::Atom, 'x', 0, HotkeyAction::SetAtomLabel, HotkeySource::Guide70, 0, "X", 0, true));
    map.add(charBinding(HotspotContext::Atom, 'o', 0, HotkeyAction::SetAtomElement, HotkeySource::Guide70, 8, 0, 0, true));
    map.add(charBinding(HotspotContext::Atom, 'q', 0, HotkeyAction::SetAtomElement, HotkeySource::Guide70, 8, 0, 0, true));
    map.add(charBinding(HotspotContext::Atom, 'O', kShift, HotkeyAction::SetAtomLabel, HotkeySource::Guide70, 0, "OMe", 0, true));
    map.add(charBinding(HotspotContext::Atom, 'Q', kShift, HotkeyAction::SetAtomLabel, HotkeySource::Guide70, 0, "Fmoc", 0, true));
    map.add(charBinding(HotspotContext::Atom, 'd', 0, HotkeyAction::SetAtomLabel, HotkeySource::Guide70, 0, "D", 0, true));
    map.add(charBinding(HotspotContext::Atom, 'e', 0, HotkeyAction::SetAtomLabel, HotkeySource::Guide70, 0, "Et", 0, true));
    map.add(charBinding(HotspotContext::Atom, 'E', kShift, HotkeyAction::SetAtomLabel, HotkeySource::Guide70, 0, "CO2Me", 0, true));
    map.add(charBinding(HotspotContext::Atom, 'Z', kShift, HotkeyAction::SetAtomLabel, HotkeySource::Guide70, 0, "N3", 0, true));
    map.add(charBinding(HotspotContext::Atom, 'L', kShift, HotkeyAction::SetAtomElement, HotkeySource::Guide70, 3, 0, 0, true));
    map.add(charBinding(HotspotContext::Atom, 'Y', kShift, HotkeyAction::SetAtomLabel, HotkeySource::Guide70, 0, "Boc", 0, true));
    // Charges, numbering, attachment point, label clearing and editing.
    map.add(charBinding(HotspotContext::Atom, '-', 0, HotkeyAction::SetAtomChargeDelta, HotkeySource::Guide70, 0, 0, -1, false));
    map.add(charBinding(HotspotContext::Atom, '+', kShift, HotkeyAction::SetAtomChargeDelta, HotkeySource::Guide70, 0, 0, +1, false));
    map.add(charBinding(HotspotContext::Atom, '\'', 0, HotkeyAction::SetAtomNumber, HotkeySource::Guide70, 0, 0, 0, false));
    map.add(charBinding(HotspotContext::Atom, '.', 0, HotkeyAction::InsertAttachmentPoint, HotkeySource::Guide70, 0, 0, 0, false));
    map.add(vkBinding(HotspotContext::Atom, vk::Return, 0, HotkeyAction::EditAtomLabel, HotkeySource::Guide70, 0, false));
    map.add(vkBinding(HotspotContext::Atom, vk::Back, 0, HotkeyAction::ClearAtomLabel, HotkeySource::Guide70, 0, false));
    map.add(vkBinding(HotspotContext::Atom, vk::Delete, 0, HotkeyAction::ClearAtomLabel, HotkeySource::Guide70, 0, false));
    // Selection / molecule context.
    map.add(charBinding(HotspotContext::Atom, 'g', 0, HotkeyAction::SelectHotspotAtom, HotkeySource::Guide70, 0, 0, 0, true));
    map.add(vkBinding(HotspotContext::Atom, vk::Space, 0, HotkeyAction::SelectMolecule, HotkeySource::EBooK4, 0, true));
    map.add(charBinding(HotspotContext::Atom, '=', 0, HotkeyAction::OpenNicknameDialog, HotkeySource::Guide70, 0, 0, 0, false));
    map.add(charBinding(HotspotContext::Atom, '/', 0, HotkeyAction::OpenAtomProperties, HotkeySource::Guide70, 0, 0, 0, false));
    map.add(charBinding(HotspotContext::Atom, '?', kShift, HotkeyAction::OpenAtomProperties, HotkeySource::Guide70, 0, 0, 0, false));
    // Environment-dependent sprouts. Orders/parameters come from the guide text.
    map.add(charBinding(HotspotContext::Atom, '0', 0, HotkeyAction::SproutEnvironmentBond, HotkeySource::Guide70, 0, 0, 0, false));
    map.add(charBinding(HotspotContext::Atom, '1', 0, HotkeyAction::SproutBond, HotkeySource::Guide70, 0, 0, 1, true));
    map.add(charBinding(HotspotContext::Atom, '2', 0, HotkeyAction::SproutCarbonyl, HotkeySource::Guide70, 0, 0, 2, true));
    map.add(charBinding(HotspotContext::Atom, '3', 0, HotkeyAction::SproutPhenyl, HotkeySource::Guide70, 0, 0, 3, false));
    map.add(charBinding(HotspotContext::Atom, 'a', 0, HotkeyAction::SproutPhenyl, HotkeySource::Guide70, 0, 0, 3, false));
    map.add(charBinding(HotspotContext::Atom, '4', 0, HotkeyAction::SproutWedge, HotkeySource::Guide70, 0, 0, 4, false));
    map.add(charBinding(HotspotContext::Atom, '5', 0, HotkeyAction::SproutHashWedge, HotkeySource::Guide70, 0, 0, 5, false));
    map.add(charBinding(HotspotContext::Atom, '6', 0, HotkeyAction::SproutSixRing, HotkeySource::Guide70, 0, 0, 6, false));
    map.add(charBinding(HotspotContext::Atom, '7', 0, HotkeyAction::SproutFiveRing, HotkeySource::Guide70, 0, 0, 5, false));
    map.add(charBinding(HotspotContext::Atom, '8', 0, HotkeyAction::SproutMethylene, HotkeySource::Guide70, 0, 0, 8, false));
    map.add(charBinding(HotspotContext::Atom, '9', 0, HotkeyAction::SproutDimethyl, HotkeySource::Guide70, 0, 0, 9, false));
    map.add(charBinding(HotspotContext::Atom, 'z', 0, HotkeyAction::SproutAlkyne, HotkeySource::Guide70, 0, 0, 0, false));
    map.add(charBinding(HotspotContext::Atom, 'j', 0, HotkeyAction::SproutCyclopentadienyl, HotkeySource::Guide70, 0, 0, 0, false));
    map.add(charBinding(HotspotContext::Atom, 'J', kShift, HotkeyAction::SproutCyclopentadienyl, HotkeySource::Guide70, 0, 0, 0, false));
    map.add(charBinding(HotspotContext::Atom, 'K', kShift, HotkeyAction::SproutTBu, HotkeySource::Guide70, 0, 0, 0, false));
    map.add(charBinding(HotspotContext::Atom, 'v', 0, HotkeyAction::Unsupported, HotkeySource::Guide70, 0, 0, 3, false));
    map.add(charBinding(HotspotContext::Atom, 'u', 0, HotkeyAction::Unsupported, HotkeySource::Guide70, 0, 0, 4, false));
    // 'k' is AMBIGUOUS in the guide (K label vs sulfonyl sprout). The excerpt
    // explicitly refuses to resolve it statically, so the router must not either.
    map.add(charBinding(HotspotContext::Atom, 'k', 0, HotkeyAction::AmbiguousGuideBinding, HotkeySource::Guide70, 0, 0, 0, false));

    // =======================================================================
    // BOND HOTSPOT  [G75-76]
    // Here 1/2/3 mean BOND ORDER, which is why the atom and bond tables cannot
    // share a row for '1'. '2' is the case the task statement calls out: on an
    // ATOM it is carbonyl sprouting, on a BOND it is double. If one table served
    // both contexts, atom-2 would silently become 'set bond order double'.
    // =======================================================================
    map.add(charBinding(HotspotContext::Bond, '1', 0, HotkeyAction::SetBondOrderSingle, HotkeySource::Guide70, 0, 0, 1, true));
    map.add(charBinding(HotspotContext::Bond, '2', 0, HotkeyAction::SetBondOrderDouble, HotkeySource::Guide70, 0, 0, 2, true));
    map.add(charBinding(HotspotContext::Bond, '3', 0, HotkeyAction::SetBondOrderTriple, HotkeySource::Guide70, 0, 0, 3, true));
    map.add(charBinding(HotspotContext::Bond, 'd', 0, HotkeyAction::SetBondDisplayDashed, HotkeySource::Guide70, 0, 0, 0, true));
    map.add(charBinding(HotspotContext::Bond, 'D', kShift, HotkeyAction::SetBondDisplayDashDouble, HotkeySource::Guide70, 0, 0, 0, true));
    map.add(charBinding(HotspotContext::Bond, 'B', kShift, HotkeyAction::SetBondDisplayBoldDouble, HotkeySource::Guide70, 0, 0, 0, true));
    map.add(charBinding(HotspotContext::Bond, 'y', 0, HotkeyAction::SetBondDisplayWavy, HotkeySource::Guide70, 0, 0, 0, true));
    map.add(charBinding(HotspotContext::Bond, 'l', 0, HotkeyAction::SetBondDoubleLeft, HotkeySource::Guide70, 0, 0, -1, true));
    map.add(charBinding(HotspotContext::Bond, 'c', 0, HotkeyAction::SetBondDoubleCenter, HotkeySource::Guide70, 0, 0, 0, true));
    map.add(charBinding(HotspotContext::Bond, 'r', 0, HotkeyAction::SetBondDoubleRight, HotkeySource::Guide70, 0, 0, +1, true));
    map.add(charBinding(HotspotContext::Bond, 'b', 0, HotkeyAction::SetBondDisplayBold, HotkeySource::Guide70, 0, 0, 0, true));
    map.add(charBinding(HotspotContext::Bond, 'w', 0, HotkeyAction::SetBondDisplayWedge, HotkeySource::Guide70, 0, 0, 0, true));
    map.add(charBinding(HotspotContext::Bond, 'f', 0, HotkeyAction::SetBondFront, HotkeySource::Guide70, 0, 0, 0, true));
    map.add(charBinding(HotspotContext::Bond, 'H', kShift, HotkeyAction::SetBondDisplayHash, HotkeySource::Guide70, 0, 0, 0, true));
    map.add(charBinding(HotspotContext::Bond, 'h', 0, HotkeyAction::SetBondDisplayHashWedge, HotkeySource::Guide70, 0, 0, 0, true));
    map.add(charBinding(HotspotContext::Bond, 'g', 0, HotkeyAction::SelectHotspotBond, HotkeySource::Guide70, 0, 0, 0, true));
    map.add(charBinding(HotspotContext::Bond, 'a', 0, HotkeyAction::FuseBenzene, HotkeySource::Guide70, 0, 0, 6, false));
    map.add(charBinding(HotspotContext::Bond, 'z', 0, HotkeyAction::FuseCyclopentadiene, HotkeySource::Guide70, 0, 0, 5, false));
    map.add(charBinding(HotspotContext::Bond, '4', 0, HotkeyAction::FuseRing, HotkeySource::Guide70, 0, 0, 4, false));
    map.add(charBinding(HotspotContext::Bond, '5', 0, HotkeyAction::FuseRing, HotkeySource::Guide70, 0, 0, 5, false));
    map.add(charBinding(HotspotContext::Bond, '6', 0, HotkeyAction::FuseRing, HotkeySource::Guide70, 0, 0, 6, false));
    map.add(charBinding(HotspotContext::Bond, '7', 0, HotkeyAction::FuseRing, HotkeySource::Guide70, 0, 0, 7, false));
    map.add(charBinding(HotspotContext::Bond, '8', 0, HotkeyAction::FuseRing, HotkeySource::Guide70, 0, 0, 8, false));
    map.add(charBinding(HotspotContext::Bond, '9', 0, HotkeyAction::ChairRingOrientation, HotkeySource::Guide70, 0, 0, 1, false));
    map.add(charBinding(HotspotContext::Bond, '0', 0, HotkeyAction::ChairRingOrientation, HotkeySource::Guide70, 0, 0, 2, false));
    map.add(charBinding(HotspotContext::Bond, '/', 0, HotkeyAction::OpenBondProperties, HotkeySource::Guide70, 0, 0, 0, false));
    map.add(charBinding(HotspotContext::Bond, '?', kShift, HotkeyAction::OpenBondProperties, HotkeySource::Guide70, 0, 0, 0, false));
    // The guide's bond table does not define Enter, so NO row is created for it.
    // Inventing one would be exactly the 'do not fabricate a rule' failure.

    // =======================================================================
    // GENERIC (no hotspot)  [G77]
    // =======================================================================
    map.add(vkBinding(HotspotContext::None, vk::Space, 0, HotkeyAction::ActivateMarqueeTool, HotkeySource::Guide70, 0, true));
    map.add(charBinding(HotspotContext::None, 'x', 0, HotkeyAction::ActivateBondTool, HotkeySource::Guide70, 0, 0, 0, true));
    map.add(charBinding(HotspotContext::None, 'X', kShift, HotkeyAction::ActivateChainTool, HotkeySource::Guide70, 0, 0, 0, false));
    map.add(charBinding(HotspotContext::None, 't', 0, HotkeyAction::ActivateTextTool, HotkeySource::Guide70, 0, 0, 0, true));
    map.add(charBinding(HotspotContext::None, 'T', kShift, HotkeyAction::ActivateBracketTool, HotkeySource::Guide70, 0, 0, 0, false));
    map.add(charBinding(HotspotContext::None, 'e', 0, HotkeyAction::ActivateArrowTool, HotkeySource::Guide70, 0, 0, 0, false));
    map.add(charBinding(HotspotContext::None, 'E', kShift, HotkeyAction::ActivateSymbolTool, HotkeySource::Guide70, 0, 0, 0, false));
    map.add(charBinding(HotspotContext::None, 'j', 0, HotkeyAction::ActivateBenzeneTool, HotkeySource::Guide70, 0, 0, 0, false));
    map.add(charBinding(HotspotContext::None, 'J', kShift, HotkeyAction::ActivateCyclopentadieneTool, HotkeySource::Guide70, 0, 0, 0, false));
    map.add(charBinding(HotspotContext::None, 'g', 0, HotkeyAction::ActivateChromatographyTool, HotkeySource::Guide70, 0, 0, 0, false));
    map.add(charBinding(HotspotContext::None, 'G', kShift, HotkeyAction::ActivateOrbitalTool, HotkeySource::Guide70, 0, 0, 0, false));
    // The guide's GENERIC table does NOT list '6'. SOURCES_AND_LIMITS.md is
    // explicit: no 'plain 6 cuts a six-ring' rule may be confirmed or enabled.
    // No binding is created here, and a test asserts that resolve() returns null
    // for '6' with no hotspot.

    return map;
}

// ---------------------------------------------------------------------------
// Keymap lookup
// ---------------------------------------------------------------------------
bool Keymap::matches(const HotkeyBinding& b, uint32_t virtualKey, uint32_t character,
                     uint32_t modifiers) const {
    // Required modifiers must all be present.
    if ((modifiers & b.modifiers) != b.modifiers) return false;
    // Forbidden modifiers must all be absent.
    if ((modifiers & b.forbiddenModifiers) != 0u) return false;
    // Any modifier the binding did not ask for must not be present, EXCEPT Shift
    // on a character binding: Shift produces the uppercase code unit, so it has
    // already been accounted for by the character value itself. Treating the
    // leftover Shift bit as disqualifying would make every uppercase label key
    // unreachable, which is the classic way case-sensitivity silently breaks.
    const uint32_t allowed = b.modifiers | (b.character ? kModShift : 0u);
    if ((modifiers & ~allowed) != 0u) return false;

    if (b.character != 0u) {
        // Character binding: the code unit must match EXACTLY. No case folding.
        return character == b.character;
    }
    if (b.virtualKey != 0u) return virtualKey == b.virtualKey;
    return false;
}

const HotkeyBinding* Keymap::resolve(HotspotContext context, uint32_t virtualKey,
                                     uint32_t character, uint32_t modifiers) const {
    // Try the requested context FIRST, then fall back to the GENERIC table. Two
    // separate passes rather than one mixed loop, because the ordering IS the
    // documented rule: "hotspot context takes priority over the generic tool
    // keys" [G70-77]. A single loop over the table would make the answer depend
    // on the order rows happened to be added in, which is not a rule at all.
    //
    // The fallback is what keeps 'x' working: on empty space it activates the
    // solid bond tool, while on an atom it is the X atom label. It is NOT what
    // makes digits work - '1' on an atom means "sprout a bond" [G70-75] and must
    // never reach the generic table's (nonexistent) digit rows.
    if (context != HotspotContext::None) {
        for (size_t j = 0; j < bindings_.size(); ++j) {
            if (bindings_[j].context != context) continue;
            if (matches(bindings_[j], virtualKey, character, modifiers))
                return &bindings_[j];
        }
    }
    for (size_t j = 0; j < bindings_.size(); ++j) {
        if (bindings_[j].context != HotspotContext::None) continue;
        if (matches(bindings_[j], virtualKey, character, modifiers))
            return &bindings_[j];
    }
    return 0;
}

std::vector<const HotkeyBinding*> Keymap::bySource(HotkeySource source) const {
    std::vector<const HotkeyBinding*> out;
    for (size_t i = 0; i < bindings_.size(); ++i)
        if (bindings_[i].source == source) out.push_back(&bindings_[i]);
    return out;
}

std::vector<const HotkeyBinding*> Keymap::byAction(HotkeyAction action) const {
    std::vector<const HotkeyBinding*> out;
    for (size_t i = 0; i < bindings_.size(); ++i)
        if (bindings_[i].action == action) out.push_back(&bindings_[i]);
    return out;
}

size_t Keymap::implementedCount() const {
    size_t n = 0;
    for (size_t i = 0; i < bindings_.size(); ++i) if (bindings_[i].implemented) ++n;
    return n;
}

void Keymap::add(const HotkeyBinding& binding) { bindings_.push_back(binding); }

// ---------------------------------------------------------------------------
// Accelerators: Ctrl combinations resolved BEFORE the hotspot
// ---------------------------------------------------------------------------
// Not hotspot-dependent, and not routed through the Keymap. An accelerator is a
// command; putting it in the same table as the chemistry keys is how Ctrl+L
// eventually gets interpreted as the 'l' chlorine key.
const std::vector<Accelerator>& defaultAccelerators() {
    static std::vector<Accelerator> table;
    static bool built = false;
    if (built) return table;
    built = true;

    struct Row { uint32_t key; uint32_t mods; HotkeyAction action; HotkeySource src; bool impl; };
    // [G81] Undo Ctrl+Z; Redo Ctrl+Shift+Z; Repeat Ctrl+Y (repeat, NOT redo).
    // [G82] Ctrl+L fixed length; Ctrl+E fixed angles.
    // [G81] Clipboard and Select All are standard, cited to the commands page.
    // [G125-126] Ctrl+J merge.
    // [PROJECT] File accelerators: the guide documents the menu commands, the
    //           association to the conventional Ctrl keys is a project choice.
    const Row rows[] = {
        { vk::Z, kModControl,                HotkeyAction::Undo,            HotkeySource::Guide81, true  },
        { vk::Z, kModControl | kModShift,    HotkeyAction::Redo,            HotkeySource::Guide81, true  },
        { vk::Y, kModControl,                HotkeyAction::RepeatLastAction,HotkeySource::Guide81, false },
        { vk::L, kModControl,                HotkeyAction::ToggleFixedLength,HotkeySource::Guide81, true },
        { vk::E, kModControl,                HotkeyAction::ToggleFixedAngles,HotkeySource::Guide81, true },
        { vk::C, kModControl,                HotkeyAction::EditCopy,        HotkeySource::Guide81, true  },
        { vk::X, kModControl,                HotkeyAction::EditCut,         HotkeySource::Guide81, true  },
        { vk::V, kModControl,                HotkeyAction::EditPaste,       HotkeySource::Guide81, true  },
        { vk::A, kModControl,                HotkeyAction::EditSelectAll,   HotkeySource::Guide81, true  },
        { vk::J, kModControl,                HotkeyAction::MergeSelection,  HotkeySource::Guide124,false },
        { vk::N, kModControl,                HotkeyAction::FileNew,         HotkeySource::Project, true  },
        { vk::O, kModControl,                HotkeyAction::FileOpen,        HotkeySource::Project, true  },
        { vk::S, kModControl,                HotkeyAction::FileSave,        HotkeySource::Project, true  },
        { vk::S, kModControl | kModShift,    HotkeyAction::FileSaveAs,      HotkeySource::Project, false }
    };
    const size_t count = sizeof(rows) / sizeof(rows[0]);
    for (size_t i = 0; i < count; ++i) {
        Accelerator a;
        a.virtualKey = rows[i].key;
        a.modifiers = rows[i].mods;
        a.action = rows[i].action;
        a.source = rows[i].src;
        a.implemented = rows[i].impl;
        table.push_back(a);
    }
    // F-keys are accelerators too: no Ctrl, no hotspot dependence. [G82]
    //   F5 actual size, F6 fit, F7 zoom in, F8 zoom out.
    const Row fkeys[] = {
        { vk::F5, 0, HotkeyAction::ZoomActualSize, HotkeySource::Guide81, true  },
        { vk::F6, 0, HotkeyAction::ZoomFit,        HotkeySource::Guide81, true  },
        { vk::F7, 0, HotkeyAction::ZoomIn,         HotkeySource::Guide81, true  },
        { vk::F8, 0, HotkeyAction::ZoomOut,        HotkeySource::Guide81, true  }
    };
    const size_t fcount = sizeof(fkeys) / sizeof(fkeys[0]);
    for (size_t i = 0; i < fcount; ++i) {
        Accelerator a;
        a.virtualKey = fkeys[i].key;
        a.modifiers = fkeys[i].mods;
        a.action = fkeys[i].action;
        a.source = fkeys[i].src;
        a.implemented = fkeys[i].impl;
        table.push_back(a);
    }
    return table;
}

const Accelerator* resolveAccelerator(uint32_t virtualKey, uint32_t modifiers) {
    const std::vector<Accelerator>& table = defaultAccelerators();
    // The MOST SPECIFIC row wins: Ctrl+Shift+Z must beat Ctrl+Z, and the check is
    // 'every required modifier present AND no unexpected modifier present', so a
    // plain Ctrl+Z cannot accidentally match the Redo row either.
    const Accelerator* best = 0;
    size_t bestBits = 0;
    for (size_t i = 0; i < table.size(); ++i) {
        const Accelerator& a = table[i];
        if (a.virtualKey != virtualKey) continue;
        // Required modifiers present, and NO UNEXPECTED modifier present. The
        // second half matters: without it Ctrl+F5 would match the bare-F5 row,
        // silently turning an unbound combination into "fit to window". A row
        // may opt into extra tolerated bits through forbiddenModifiers, which is
        // how Ctrl+Shift+Z is allowed to be tested against the Ctrl+Z row and
        // still lose to the more specific one.
        if ((modifiers & a.modifiers) != a.modifiers) continue;
        if ((modifiers & ~a.modifiers) != 0u) continue;
        size_t bits = 0;
        for (uint32_t bit = 1u; bit; bit <<= 1) if (a.modifiers & bit) ++bits;
        if (best && bits <= bestBits) continue;
        best = &a;
        bestBits = bits;
    }
    return best;
}

} // namespace interaction
} // namespace r62
