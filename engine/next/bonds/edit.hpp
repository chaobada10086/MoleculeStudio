#ifndef R62_NEXT_BONDS_EDIT_HPP
#define R62_NEXT_BONDS_EDIT_HPP
// =============================================================================
// re_round62_viewer/next/bonds/edit.hpp
//
// T07 - EDIT COMMANDS for bonds. Every function returns a core::Command, which
// the caller submits through the ONE mutation entry point (core::CommandLog).
// Nothing here writes to a Document directly.
//
// WHY EVERYTHING RETURNS core::Command
//   * One user action == one atomic transaction == one undo step
//     (core/INTERFACES.md section 7.1). A "change order and display and side"
//     action must therefore be ONE Command, not three.
//   * CommandLog captures a RawState baseline at begin() and rolls back on
//     failure, so a rejected edit cannot leave half the axes changed.
//
// STYLE vs KEY LEVEL (the T07 acceptance criterion)
//   "键级、显示风格和化学类型分开" - so setting the display does NOT touch the
//   order, and setting the order does NOT touch the display. Two separate
//   commands exist for exactly this reason, plus a combined one used by the
//   tool presets that legitimately change both.
//
// INVALIDATION
//   A style edit requests kInvalidateAppearance only, never kInvalidateGeometry,
//   so changing a bond to bold or wavy does not rebuild the spatial index
//   (core transaction.style_not_geometry asserts the same rule for atoms).
// =============================================================================

#include "next/bonds/spec.hpp"
#include "next/interaction/hotspot.hpp"   // T04 SnapResult (declared dependency)

#include <vector>

namespace r62 {
namespace bonds {

// ---------------------------------------------------------------------------
// Creating bonds
// ---------------------------------------------------------------------------
// Create a bond between two EXISTING atoms with an explicit axis bundle.
// `begin`/`end` are the stable atom ids, and the begin->end order IS the stereo
// direction for a wedge/hash (guide 2498), so the caller must pass the resolved
// SnapResult endpoints in the order the user dragged them.
core::Command addBondCommand(core::ObjectId begin, core::ObjectId end,
                             const BondAxes& axes, core::ObjectId* outBondId);

// A bond from TWO resolved endpoints. T04's SnapResult carries only the END
// endpoint plus its target, so the origin is passed separately: either an
// existing atom id, or a free document point (kInvalidObjectId + originPoint).
//
// Every endpoint is already final, so the bond and any atom it needs are
// committed in ONE transaction. This is what a T04 tool handler calls on
// PointerUp, and it never re-resolves the snap - the "preview and commit use the
// same resolved value" rule. Duplicate edges and self bonds are refused.
struct BondEndpoint {
    core::Vec2      point;       // resolved document position, always meaningful
    core::ObjectRef target;      // existing atom joined, or invalid for a free end
    bool            joined;
    BondEndpoint() : joined(false) {}
};

core::Command addBondFromSnapCommand(const BondEndpoint& origin,
                                     const interaction::SnapResult& snap,
                                     const BondAxes& axes,
                                     int newAtomElement,
                                     core::ObjectId* outBondId);

// ---------------------------------------------------------------------------
// Changing existing bonds
// ---------------------------------------------------------------------------
// Change the ORDER axis only. Aromatic order is validated against the existing
// semantics; use setBondAxesCommand to move both together.
core::Command setBondOrderCommand(const std::vector<core::ObjectId>& bonds,
                                  core::BondOrder order);

// Change the DISPLAY axis only (Normal/Wedge/Hash/HollowWedge/Bold/Dashed/
// Dotted/Wavy/WedgeHash). Does not change the order. Requests appearance-only
// invalidation.
core::Command setBondDisplayCommand(const std::vector<core::ObjectId>& bonds,
                                    core::BondDisplay display);

// Change the SEMANTICS axis only (Normal/Aromatic/Dative/Hydrogen/Query/
// Unsaturated).
core::Command setBondSemanticsCommand(const std::vector<core::ObjectId>& bonds,
                                      core::BondSemantics semantics);

// Change the STEREO axis only (None/Up/Down/Either).
core::Command setBondStereoCommand(const std::vector<core::ObjectId>& bonds,
                                   core::BondStereo stereo);

// Change the double-bond SIDE only. Stored as a persistent extension on the
// bond, because the T01 model has no side field on the order axis and adding
// one would be a public model change (see INTERFACES.md section 6).
core::Command setBondSideCommand(const std::vector<core::ObjectId>& bonds,
                                 DoubleSide side);

// Apply a complete axis bundle in ONE transaction (what a tool preset does).
core::Command setBondAxesCommand(const std::vector<core::ObjectId>& bonds,
                                 const BondAxes& axes);

// Apply a tool preset by id ("r62.tool.bond.wedge") or command name
// ("chooseBondTool_Wedged"). Returns InvalidArgument for an unknown preset.
core::Command applyBondPresetCommand(const std::vector<core::ObjectId>& bonds,
                                     const std::string& presetIdOrCommand);

// Direction reversal: swap begin/end so a wedge/hash/dative flips. Geometry is
// unchanged; only the stored endpoint order moves. One undo step.
core::Command reverseBondDirectionCommand(const std::vector<core::ObjectId>& bonds);

// ---------------------------------------------------------------------------
// Tool behaviours taken from the official guide
// ---------------------------------------------------------------------------
// GUIDE 2448-2449: "Use the single bond tool and click once on any single bond
// to make it a double bond. Subsequent clicks will reposition the two lines".
// Returns, for one bond, the axis bundle the NEXT click should produce. This is
// the whole "single bond tool retypes an existing bond" behaviour, expressed as
// data so it can be unit tested without a window.
BondAxes nextClickAxes(const BondAxes& current);

// GUIDE 2459-2461: the wavy bond tool advances single -> double ->
// double-either on repeated clicks.
BondAxes nextWavyClickAxes(const BondAxes& current);

// ---------------------------------------------------------------------------
// Persistence of the axes the core model does not own
// ---------------------------------------------------------------------------
// The side placement lives in the bond extension bag under this key. It is NOT
// "cdxml.*", so core::isPersistentExtensionKey() is false and T02 will not write
// it - T07 therefore round-trips it through its own record (see persistence.hpp).
extern const char* const kSideExtensionKey;

// Read/write the side placement on a bond extension bag.
DoubleSide sideOf(const core::ExtensionBag& bag);
void       setSideOn(core::ExtensionBag& bag, DoubleSide side);

// ---------------------------------------------------------------------------
// Undo-step accounting helper used by the tests and the results report
// ---------------------------------------------------------------------------
// True when the command only changes appearance (no geometry invalidation).
bool isAppearanceOnly(const core::Command& command, const core::CommandContext& ctx);

} // namespace bonds
} // namespace r62

#endif // R62_NEXT_BONDS_EDIT_HPP
