#ifndef R62_NEXT_SYMBOLS_ORBITALS_TOOLS_HPP
#define R62_NEXT_SYMBOLS_ORBITALS_TOOLS_HPP
// =============================================================================
// re_round62_viewer/next/symbols_orbitals/tools.hpp
//
// T12 - THE INTERACTIVE TOOLS.
//
// -----------------------------------------------------------------------------
// THREE BOUNDED BEHAVIORS COVER ALL 37 COMMANDS
// -----------------------------------------------------------------------------
//   ToolBehavior::PlaceSingle - every symbol preset AND every orbital preset.
//                               ONE instance serves both families; the preset id
//                               is a constructor argument, not a subclass.
//   ToolBehavior::DragBond    - the two pushing tools (press-drag-release from a
//                               source object to a target object, exactly the
//                               gesture [GUIDE p158] describes).
//
// -----------------------------------------------------------------------------
// THE PREVIEW/COMMIT RULE, MADE STRUCTURAL
// -----------------------------------------------------------------------------
// A T01 Tool publishes a PreviewScene and NEVER mutates the document during a
// gesture; the commit happens in endGesture through ToolHost::submit(). Preview
// and commit both read the SAME member (`state_.pending`), so they cannot differ
// (the defect class SOURCES_AND_LIMITS.md records for the legacy editor).
//
// -----------------------------------------------------------------------------
// BINDING ON PLACE: EXPLICIT, NEVER BY PROXIMITY
// -----------------------------------------------------------------------------
// When a symbol is placed on an atom hotspot, the tool calls
// bindSymbolToAtom() as part of the SAME transaction as the creation. If there is
// no atom under the pointer, no binding happens and no chemistry is written.
// There is no "guess by distance" branch anywhere in this file - which is the
// mechanical reason a symbol moved near an atom cannot change its charge.
//
// ALT+click with the lone-pair tool produces the BAR form [GUIDE p242].
//
// -----------------------------------------------------------------------------
// WHAT IS NOT IMPLEMENTED (reported honestly, not approximated)
// -----------------------------------------------------------------------------
//   * Bond-level stereochemistry for H-dot / H-dash. The guide says they mark an H
//     out of / into the plane [GUIDE p242], but AtomSemantics has no field that
//     can express "the hydrogen that is here", so the mark is stored as an
//     annotation and no bond stereo is written. See README "supported limits".
//   * Attachment rank editing (the diamond numbers are derived, not editable).
// =============================================================================

#include "next/symbols_orbitals/geometry.hpp"
#include "next/symbols_orbitals/model.hpp"
#include "next/symbols_orbitals/preset.hpp"
#include "next/symbols_orbitals/push.hpp"
#include "next/core/tool_registry.hpp"
#include "next/interaction/hotspot.hpp"

#include <string>
#include <vector>

namespace r62 {
namespace symbols {

// Alt releases geometric constraints [SOURCES_AND_LIMITS.md] and, for the
// lone-pair tool, requests the BAR form [GUIDE p242].
inline bool altRequestsBarLonePair(uint32_t modifiers) {
    return (modifiers & core::kModAlt) != 0u;
}

// ---------------------------------------------------------------------------
// Shared gesture state
// ---------------------------------------------------------------------------
struct SymbolGestureState {
    bool           active;
    uint32_t       presetId;
    core::ObjectId target;         // an existing symbol being edited, else invalid
    SymbolParams   pending;        // THE value preview and commit both read
    core::ObjectId bindAtom;       // the atom to bind to at commit, or invalid
    core::Vec2     pressPoint;
    bool           moved;
    SymbolGestureState()
        : active(false), presetId(kNoPreset), target(core::kInvalidObjectId),
          bindAtom(core::kInvalidObjectId), pressPoint(0, 0), moved(false) {}
};

struct OrbitalGestureState {
    bool           active;
    uint32_t       presetId;
    core::ObjectId target;
    OrbitalParams  pending;
    core::Vec2     pressPoint;
    bool           moved;
    OrbitalGestureState()
        : active(false), presetId(kNoPreset), target(core::kInvalidObjectId),
          pressPoint(0, 0), moved(false) {}
};

struct PushGestureState {
    bool           active;
    uint32_t       presetId;
    PushParams     pending;
    core::Vec2     pressPoint;
    bool           moved;
    PushGestureState() : active(false), presetId(kNoPreset), pressPoint(0, 0), moved(false) {}
};

// ---------------------------------------------------------------------------
// SymbolTool  (PlaceSingle)
// ---------------------------------------------------------------------------
class SymbolTool : public core::Tool {
public:
    explicit SymbolTool(uint32_t presetId);

    const core::ToolId& id() const { return id_; }
    core::ToolBehavior behavior() const { return core::ToolBehavior::PlaceSingle; }
    uint32_t presetId() const { return presetId_; }

    core::Invalidation activate(core::ToolHost& host);
    core::CommandResult beginGesture(core::ToolHost& host, const core::PointerState& pointer,
                                     core::HitContext& hits);
    core::CommandResult updateGesture(core::ToolHost& host, const core::PointerState& pointer,
                                      core::HitContext& hits);
    core::CommandResult endGesture(core::ToolHost& host, const core::PointerState& pointer,
                                   core::HitContext& hits);
    core::CommandResult cancelGesture(core::ToolHost& host);

    const SymbolParams& previewParams() const { return state_.pending; }
    const ShapeGeometry& previewGeometry() const { return previewGeometry_; }
    const SymbolGestureState& state() const { return state_; }

    // Commit the pending symbol as ONE transaction (including its bind). Public so
    // a test can drive the gesture headlessly.
    core::Error commit(core::ToolHost& host);

private:
    void publishPreview(core::ToolHost& host, const core::ViewTransform& view);

    uint32_t            presetId_;
    core::ToolId        id_;
    SymbolGestureState  state_;
    ShapeGeometry       previewGeometry_;
};

// ---------------------------------------------------------------------------
// OrbitalTool  (PlaceSingle)
// ---------------------------------------------------------------------------
class OrbitalTool : public core::Tool {
public:
    explicit OrbitalTool(uint32_t presetId);

    const core::ToolId& id() const { return id_; }
    core::ToolBehavior behavior() const { return core::ToolBehavior::PlaceSingle; }
    uint32_t presetId() const { return presetId_; }

    core::Invalidation activate(core::ToolHost& host);
    core::CommandResult beginGesture(core::ToolHost& host, const core::PointerState& pointer,
                                     core::HitContext& hits);
    core::CommandResult updateGesture(core::ToolHost& host, const core::PointerState& pointer,
                                      core::HitContext& hits);
    core::CommandResult endGesture(core::ToolHost& host, const core::PointerState& pointer,
                                   core::HitContext& hits);
    core::CommandResult cancelGesture(core::ToolHost& host);

    const OrbitalParams& previewParams() const { return state_.pending; }
    const ShapeGeometry& previewGeometry() const { return previewGeometry_; }
    const OrbitalGestureState& state() const { return state_; }

    core::Error commit(core::ToolHost& host);

private:
    void publishPreview(core::ToolHost& host, const core::ViewTransform& view);

    uint32_t             presetId_;
    core::ToolId         id_;
    OrbitalGestureState  state_;
    ShapeGeometry        previewGeometry_;
};

// ---------------------------------------------------------------------------
// PushTool  (DragBond behavior: press-drag-release)
// ---------------------------------------------------------------------------
// [GUIDE p158] "Click and drag the mouse from the source object to the target
// object and release the mouse."
class PushTool : public core::Tool {
public:
    explicit PushTool(uint32_t presetId);

    const core::ToolId& id() const { return id_; }
    core::ToolBehavior behavior() const { return core::ToolBehavior::DragBond; }
    uint32_t presetId() const { return presetId_; }
    PushHead head() const { return head_; }

    core::Invalidation activate(core::ToolHost& host);
    core::CommandResult beginGesture(core::ToolHost& host, const core::PointerState& pointer,
                                     core::HitContext& hits);
    core::CommandResult updateGesture(core::ToolHost& host, const core::PointerState& pointer,
                                      core::HitContext& hits);
    core::CommandResult endGesture(core::ToolHost& host, const core::PointerState& pointer,
                                   core::HitContext& hits);
    core::CommandResult cancelGesture(core::ToolHost& host);

    const PushParams& previewParams() const { return state_.pending; }
    const arrows::ArrowGeometry& previewGeometry() const { return previewGeometry_; }
    const PushGestureState& state() const { return state_; }

    core::Error commit(core::ToolHost& host);

private:
    void publishPreview(core::ToolHost& host, const core::ViewTransform& view);

    uint32_t          presetId_;
    PushHead          head_;
    core::ToolId      id_;
    PushGestureState  state_;
    arrows::ArrowGeometry previewGeometry_;
};

// ---------------------------------------------------------------------------
// Registration and platform ids
// ---------------------------------------------------------------------------
// Registers all 16 symbol presets, 2 push presets and 21 orbital presets in the
// T01 registry and binds each to a platform command in the ToolBand. Returns the
// number of presets registered, or a negative value on the first error.
//
// `outPresetIds`, when non-null, must have room for 39 entries in the order
// (symbols 16, pushes 2, orbitals 21).
core::Error registerSymbolsOrbitals(core::ToolRegistry* registry,
                                    uint32_t* outPresetIds,     // [39], may be null
                                    size_t* outCount);

// ---------------------------------------------------------------------------
// T04 candidates and handles
// ---------------------------------------------------------------------------
// A symbol/orbital contributes a ControlHandle candidate at its anchor and at
// each of its editable handles. Anchors are `eligible=false` for a bond join
// (nothing should draw a bond into a charge ring) but `tolerant=true`, so they use
// the smaller radius and do not swallow a whole neighbourhood.
core::Error appendSymbolCandidates(const core::Document& d, core::ObjectId symbolId,
                                   const core::ViewTransform& view,
                                   double documentLineWidth, const Tessellation& tess,
                                   std::vector<interaction::SnapCandidate>* out);
core::Error appendOrbitalCandidates(const core::Document& d, core::ObjectId orbitalId,
                                    const core::ViewTransform& view,
                                    double documentLineWidth, const Tessellation& tess,
                                    std::vector<interaction::SnapCandidate>* out);

// The handles to draw for the currently selected object, as overlay primitives.
core::Error appendSymbolHandlesOf(const core::Document& d, core::ObjectId symbolId,
                                  double documentLineWidth, const Tessellation& tess,
                                  std::vector<Handle>* out);
core::Error appendOrbitalHandlesOf(const core::Document& d, core::ObjectId orbitalId,
                                   double documentLineWidth, const Tessellation& tess,
                                   std::vector<Handle>* out);

} // namespace symbols
} // namespace r62

#endif // R62_NEXT_SYMBOLS_ORBITALS_TOOLS_HPP
