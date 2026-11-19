#ifndef R62_NEXT_BONDS_ADAPTERS_HPP
#define R62_NEXT_BONDS_ADAPTERS_HPP
// =============================================================================
// re_round62_viewer/next/bonds/adapters.hpp
//
// T07 - adapters that connect the bond module to the FROZEN public interfaces
// of T03 (render) and T01 (core). Nothing here redefines a public type.
//
// THREE ADAPTERS
//   1. render adapter   : BondRenderData -> std::vector<render::Primitive>
//   2. tool adapter     : a core::Tool that drives bond creation through
//                         core::ToolHost (which is the ONE mutation entry point)
//   3. preset adapter   : BondPreset -> core::ToolPreset (registration data)
//
// WHY THE RENDER ADAPTER IS SEPARATE FROM geometry.cpp
//   geometry.cpp must be usable with NO render module linked, so a headless test
//   can assert exact polygons. Only this file includes next/render. That keeps the
//   two concerns separable and lets T07 pass its geometry tests even if T03 is
//   rebuilt.
// =============================================================================

#include "next/bonds/geometry.hpp"
#include "next/core/tool_registry.hpp"
#include "next/render/scene_api.hpp"

#include <vector>

namespace r62 {
namespace bonds {

// ---------------------------------------------------------------------------
// 1. Render adapter
// ---------------------------------------------------------------------------
// One BondRenderData becomes one or more render::Primitive records that share a
// single source ObjectRef, so the backend's identity cache still treats them as
// one object's geometry.
struct RenderAdapterOptions {
    core::LineStyle style;         // persisted bond style
    uint32_t        color;         // 0xRRGGBBAA
    double          viewDefaultWidth;
    render::LayerId layer;
    int32_t         z;
    RenderAdapterOptions()
        : color(0x000000FFu), viewDefaultWidth(0.6),
          layer(render::LayerId(render::layers::kContent)), z(5) {}
};

// Returns false when there is nothing to draw (a degenerate bond). An empty
// output is a legitimate answer, not an error.
bool toPrimitives(const BondRenderData& data, const core::ObjectRef& source,
                  const RenderAdapterOptions& options,
                  std::vector<render::Primitive>* out);

// A content signature. Two calls returning the same value MUST produce the same
// primitives; that is the T03 incremental-cache contract. It hashes every axis
// AND the endpoints, because moving an atom must invalidate the mesh.
uint64_t signatureOf(const BondRenderData& data, const core::BondRecord& record,
                     const RenderAdapterOptions& options);

// ---------------------------------------------------------------------------
// 2. Preset adapter
// ---------------------------------------------------------------------------
// Convert a BondPreset into the core::ToolPreset that T01's ToolRegistry stores.
// The namespaced id, the tools.csv command and the category are all preserved so
// the coverage report can join against tools.csv with no mapping table.
core::ToolPreset toToolPreset(const BondPreset& preset);

// ---------------------------------------------------------------------------
// 3. Tool adapter
// ---------------------------------------------------------------------------
// A bond tool driven entirely by core::ToolHost. (Named BondStyleTool because
// ToolHost::submit -> CommandLog, so a gesture is one transaction and one undo
// step.
//
// The tool holds the preset (axes) it was activated with. T04 selects the
// preset; the tool does not read the keyboard itself except for the documented
// side hotkeys, which are handled by T04's keymap.
class BondStyleTool : public core::Tool {
public:
    explicit BondStyleTool(const BondPreset& preset);

    // Switch the active preset without recreating the tool (toolbar change).
    void setPreset(const BondPreset& preset);
    const BondPreset& preset() const { return preset_; }

    const core::ToolId& id() const { return id_; }
    core::ToolBehavior behavior() const { return core::ToolBehavior::DragBond; }

    virtual core::CommandResult beginGesture(core::ToolHost& host,
                                             const core::PointerState& pointer,
                                             core::HitContext& hits);
    virtual core::CommandResult updateGesture(core::ToolHost& host,
                                              const core::PointerState& pointer,
                                              core::HitContext& hits);
    virtual core::CommandResult endGesture(core::ToolHost& host,
                                           const core::PointerState& pointer,
                                           core::HitContext& hits);
    virtual core::CommandResult cancelGesture(core::ToolHost& host);
    virtual bool handleChar(core::ToolHost& host, uint32_t unicodeChar,
                            uint32_t modifiers);

    // The origin the gesture resolved at PointerDown. Exposed so a test can
    // assert that the committed bond used EXACTLY the origin the preview showed.
    const core::Vec2& origin() const { return origin_; }
    bool originJoined() const { return originJoined_; }
    core::ObjectId originAtom() const { return originAtom_; }

private:
    core::ToolId     id_;
    BondPreset       preset_;
    BondGeometryOptions options_;
    core::Vec2       origin_;
    bool             originJoined_;
    core::ObjectId   originAtom_;
    bool             active_;
};

} // namespace bonds
} // namespace r62

#endif // R62_NEXT_BONDS_ADAPTERS_HPP
