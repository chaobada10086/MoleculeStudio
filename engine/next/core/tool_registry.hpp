#ifndef R62_NEXT_CORE_TOOL_REGISTRY_HPP
#define R62_NEXT_CORE_TOOL_REGISTRY_HPP
// =============================================================================
// re_round62_viewer/next/core/tool_registry.hpp
//
// T01 - tools, tool events, preview scene, platform command mapping.
//
// WHY A REGISTRY AND NOT AN ENUM
// -----------------------------------------------------------------------------
// The inventory in host_integration/drawing_inventory counts 275 distinct tool
// and preset COMMANDS (269 rows in tools.csv plus the dynamic template entry).
// They are NOT 275 algorithms. This registry separates the two:
//
//   * ToolId       - a stable NAMESPACED STRING such as
//                    "r62.tool.bond.solid" or "r62.tool.ring.benzene".
//                    Downstream modules and the host adapter key off this
//                    string. New presets are additive data, not new code.
//   * ToolBehavior - which of a SMALL, BOUNDED set of interaction algorithms the
//                    preset uses (place / drag / freehand / multi-click /
//                    lasso / marquee / select / erase / text / template).
//   * ToolPreset   - a parameter bundle: which command factory to instantiate
//                    and which style option it applies.
//   * Platform command - an integer in a reserved band. Registrants request a
//                    platform id; the registry allocates from a per-namespace
//                    band so two modules can never collide on the same number.
//
// Tool events are routed through the SAME entry point as menu commands
// (CommandLog::execute). A tool does not get a private mutation path.
// =============================================================================

#include "commands.hpp"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace r62 {
namespace core {

// ---------------------------------------------------------------------------
// Tool identity and behavior
// ---------------------------------------------------------------------------
typedef std::string ToolId;

// A bounded set. Adding a behavior IS a code change; adding a preset is not.
enum class ToolBehavior : uint8_t {
    Select = 0,       // pick, add-to-selection, connected selection
    Marquee,          // rectangular rubber-band selection
    Lasso,            // freehand region selection
    Erase,            // click-erase objects
    PlaceSingle,      // click places one object (arrow, shape, symbol, orbital)
    PlaceRings,       // click places a ring preset
    PlaceChain,       // drag lays down a carbon chain
    DragBond,         // press-drag-release creates/edits a bond
    DrawCurve,        // freehand polyline (pen)
    EditCurve,        // move control points of an existing path
    Text,             // click places a text object
    Template,         // opens a template library picker
    Table,            // drag a table frame
    Perspective       // structure perspective adjustment (no document edit)
};
const char* behaviorName(ToolBehavior behavior);

// Which document object a preset creates. Unknown == the preset only changes
// style options of an existing selection (the "275 as style options" case).
enum class PresetTarget : uint8_t {
    None = 0,
    Atom,
    Bond,
    Text,
    Path,
    Arrow,
    Bracket,
    Table,
    Orbital,
    Symbol,
    BioShape,
    Selection   // applies a style to whatever is selected
};

// Stable preset index. tool_registry.def declares kPresetNone = 0xFFFFFFFF.
static const uint32_t kPresetNone = 0xFFFFFFFFu;

// ---------------------------------------------------------------------------
// ToolPreset
// ---------------------------------------------------------------------------
// Everything a platform layer needs to build a menu item or a toolbar button,
// and everything a tool module needs to run the interaction, without either of
// them knowing the other's data structures.
struct ToolPreset {
    uint32_t      presetId;      // dense, stable, assigned at registration
    ToolId        id;            // namespaced string, e.g. "r62.tool.bond.solid"
    ToolBehavior  behavior;
    PresetTarget  target;
    std::string   displayName;   // localised, for menus
    std::string   category;      // matches drawing_inventory categories
    std::string   sourceCommand; // tools.csv command name, "" if project-defined
    std::string   optionKey;     // style option applied on commit, "" if none
    Invalidation  invalidation;  // what committing this preset dirties
    bool          implemented;   // false == registered but reports NotImplemented
    ToolPreset()
        : presetId(kPresetNone), behavior(ToolBehavior::Select),
          target(PresetTarget::None), invalidation(kInvalidateNone),
          implemented(false) {}
};

// ---------------------------------------------------------------------------
// Tool event
// ---------------------------------------------------------------------------
// ONE event == ONE eventual transaction. A running gesture therefore carries a
// Transaction with it, which is why the lifetime is explicit: beginGesture
// opens, updateGesture refines the preview, endGesture commits, cancelGesture
// rolls back. Nothing else may write to the document during a gesture.
enum class PointerPhase : uint8_t { Down = 0, Move = 1, Up = 2, Hover = 3, Cancel = 4 };

enum ModifierKey : uint32_t {
    kModNone    = 0u,
    kModShift   = 1u << 0,   // add to selection / constrain
    kModControl = 1u << 1,   // copy-drag / accelerator
    kModAlt     = 1u << 2,   // temporarily release fixed length+angle
    kModSpace   = 1u << 3
};

enum MouseButton : uint8_t { kButtonNone = 0, kButtonLeft = 1, kButtonRight = 2, kButtonMiddle = 3 };

struct PointerState {
    Vec2        position;      // DOCUMENT coordinates (see contracts.hpp units)
    Vec2        rawPosition;   // device pixels, for debugging only
    uint32_t    modifiers;
    MouseButton button;
    PointerPhase phase;
    double      wheelDelta;
    PointerState()
        : modifiers(kModNone), button(kButtonNone), phase(PointerPhase::Hover), wheelDelta(0) {}
};

// What the platform layer tells the tool about the pointer environment. The
// tool must not open its own BVH; it asks through this interface so the renderer
// and the tool agree on hit results.
class HitContext {
public:
    virtual ~HitContext() {}
    // Nearest alive atom within radiusDevice pixels of a DOCUMENT point.
    virtual ObjectId nearestAtom(const Vec2& documentPoint, double radiusDevice) const = 0;
    // Nearest alive bond within radiusDevice pixels.
    virtual ObjectId nearestBond(const Vec2& documentPoint, double radiusDevice) const = 0;
    // Objects whose bounds intersect a document-space box.
    virtual std::vector<ObjectRef> objectsInBox(const Box& box) const = 0;
    // Objects inside a freehand document-space polygon.
    virtual std::vector<ObjectRef> objectsInPolygon(const std::vector<Vec2>& points) const = 0;
    // The view transform the hit test used; tools need it to size previews.
    virtual const ViewTransform& view() const = 0;
    // Read-only document access for the tool's own decisions (snapping to the
    // exact coordinate of an existing atom, reading the default bond length).
    // Tools must not MUTATE through this; writes go through ToolHost::submit.
    virtual const Document& document() const = 0;
};

// ---------------------------------------------------------------------------
// Preview scene
// ---------------------------------------------------------------------------
// During a gesture the tool NEVER mutates the document. It publishes a preview
// description; the renderer draws the static scene plus this overlay. On commit
// the SAME resolved values are written through a single transaction, which is
// how "preview and commit agree" is guaranteed structurally rather than by
// discipline (SOURCES_AND_LIMITS.md records preview/commit divergence as a past
// defect).
enum class PreviewPrimitive : uint8_t {
    Segment = 0,      // p0 - p1
    Polyline,         // points[]
    Circle,           // center + radius (document units)
    Rect,             // points[0], points[1]
    Polygon,          // points[]
    Label,            // position + text
    GlyphTransform,   // transform an existing object's vertices (points[0..1])
    Ghost,            // draw an object that would be created, dimmed
    Highlight         // draw an existing object highlighted
};

struct PreviewItem {
    PreviewPrimitive primitive;
    std::vector<Vec2> points;
    ObjectRef        subject;       // for Ghost/Highlight/GlyphTransform
    std::string      text;          // for Label
    LineStyle        style;
    uint32_t         fillRGBA;
    uint8_t          fillEnabled;
    PreviewItem()
        : primitive(PreviewPrimitive::Segment), fillRGBA(0), fillEnabled(0) {}
};

class PreviewScene {
public:
    PreviewScene() : generation_(0) {}
    void clear() { items_.clear(); ++generation_; }
    void add(const PreviewItem& item) { items_.push_back(item); ++generation_; }
    const std::vector<PreviewItem>& items() const { return items_; }
    bool empty() const { return items_.empty(); }
    // Incremented on every change; the renderer compares it to skip rebuilding
    // its preview mesh when nothing was added.
    uint64_t generation() const { return generation_; }
    // Objects the renderer must NOT draw (they are being replaced by the
    // preview). Sorted and deduplicated by the tool before returning.
    const std::vector<ObjectRef>& hidden() const { return hidden_; }
    std::vector<ObjectRef>& mutableHidden() { return hidden_; }
private:
    std::vector<PreviewItem> items_;
    std::vector<ObjectRef>   hidden_;
    uint64_t                 generation_;
};

// ---------------------------------------------------------------------------
// Tool
// ---------------------------------------------------------------------------
// The platform layer (T04) owns WndProc and translates Win32 messages into
// PointerState/key events. A Tool never sees an HWND, a message id or a pixel
// coordinate. That boundary is what makes the same tool testable headlessly.
class ToolHost;   // defined below; tools only see the reference

class Tool {
public:
    virtual ~Tool() {}
    virtual const ToolId& id() const = 0;
    virtual ToolBehavior behavior() const = 0;

    // Menu/toolbar activation. Returns the invalidation the platform should
    // apply (usually kInvalidateSelection for cursor/toolbar state).
    virtual Invalidation activate(ToolHost& host) { (void)host; return kInvalidateNone; }
    virtual void deactivate(ToolHost& host) { (void)host; }

    // Pointer lifecycle. beginGesture may open a Transaction on the host.
    virtual CommandResult beginGesture(ToolHost& host, const PointerState& pointer,
                                       HitContext& hits) = 0;
    virtual CommandResult updateGesture(ToolHost& host, const PointerState& pointer,
                                        HitContext& hits) {
        (void)host; (void)pointer; (void)hits;
        return CommandResult::noChange();
    }
    virtual CommandResult endGesture(ToolHost& host, const PointerState& pointer,
                                     HitContext& hits) = 0;
    virtual CommandResult cancelGesture(ToolHost& host) = 0;

    // Keyboard routed to the active tool. Returns true when consumed. Character
    // keys keep their case: SOURCES_AND_LIMITS.md records that "l"/"Shift+c"
    // mean Cl while "c" means C, so lowercasing early is a defect.
    virtual bool handleChar(ToolHost& host, uint32_t unicodeChar, uint32_t modifiers) {
        (void)host; (void)unicodeChar; (void)modifiers;
        return false;
    }
    virtual bool handleKey(ToolHost& host, uint32_t virtualKey, uint32_t modifiers) {
        (void)host; (void)virtualKey; (void)modifiers;
        return false;
    }
};

// ---------------------------------------------------------------------------
// ToolHost
// ---------------------------------------------------------------------------
// The ONLY surface a tool may use to read or change state. It gives access to
// the document and to the single mutation entry point, and nothing else.
class ToolHost {
public:
    virtual ~ToolHost() {}
    virtual Document& document() = 0;
    virtual CommandLog& log() = 0;
    virtual PreviewScene& preview() = 0;
    // The selection lives outside the document because it is view state, not
    // document state: saving must not persist it and undo must not restore it.
    virtual const std::vector<ObjectRef>& selection() const = 0;
    virtual void setSelection(const std::vector<ObjectRef>& selection) = 0;
    virtual void addToSelection(const std::vector<ObjectRef>& selection) = 0;
    virtual void clearSelection() = 0;
    virtual const ViewTransform& view() const = 0;
    virtual void setView(const ViewTransform& view) = 0;
    // Interaction parameters (fixed length/angle, snap radius) proven in
    // SOURCES_AND_LIMITS.md. Owned by the platform profile, read by tools.
    virtual bool fixedLength() const = 0;
    virtual bool fixedAngles() const = 0;
    virtual double angleStepDegrees() const = 0;
    virtual double snapRadiusDevice() const = 0;
    virtual double releaseRadiusDevice() const = 0;
    // Route any user action through the shared transaction entry point.
    virtual Error submit(const Command& command, const std::string& tag) = 0;
    // Publish a status line (platform shows it); tools never call MessageBox.
    virtual void setStatus(const std::string& text) = 0;
    // Report an unimplemented but requested ChemDraw action honestly.
    virtual void reportUnsupported(const std::string& action) = 0;
};

// ---------------------------------------------------------------------------
// Platform command ids
// ---------------------------------------------------------------------------
// Per-namespace bands. A registrant asks for an id inside its own namespace and
// can never collide with another module. The bands start far above the legacy
// editor's 1001..1299 ids so both can coexist during migration.
namespace platform_command {
enum NamespaceId : uint32_t {
    ToolBand        = 10000,   // one id per registered preset
    ToolBandEnd     = 19999,
    EditBand        = 20000,   // undo/redo/copy/paste/delete
    EditBandEnd     = 20999,
    ViewBand        = 21000,   // zoom/fit/actual size
    ViewBandEnd     = 21999,
    FileBand        = 22000,
    FileBandEnd     = 22999,
    ProjectBand     = 23000,   // project-specific extras
    ProjectBandEnd  = 23999,
    HostBand        = 24000,   // host adapter only
    HostBandEnd     = 24999
};
} // namespace platform_command

// ---------------------------------------------------------------------------
// ToolRegistry
// ---------------------------------------------------------------------------
// Not a singleton: the platform owns one instance and passes it to tools. That
// keeps headless tests free of global state (the legacy editor's globals are
// exactly what made unit testing hard).
class ToolRegistry {
public:
    ToolRegistry();

    // Registration. The preset id is assigned densely in registration order and
    // never reused after clear(). Duplicate string ids are rejected.
    Error registerPreset(const ToolPreset& preset, uint32_t* outPresetId);
    // Register the tool INSTANCE that implements a behavior. One instance may
    // serve many presets (v1: one instance per behavior).
    Error registerTool(ToolBehavior behavior, Tool* tool);
    // Bind a preset to the platform command id it should answer to.
    Error bindPlatformCommand(const ToolId& id, uint32_t platformCommand);

    const ToolPreset* preset(const ToolId& id) const;
    const ToolPreset* presetById(uint32_t presetId) const;
    const ToolPreset* presetForCommand(uint32_t platformCommand) const;
    Tool* tool(ToolBehavior behavior) const;
    Tool* toolFor(const ToolId& id) const;

    // Resolve a platform command id to a preset, or 0.
    const ToolPreset* resolvePlatformCommand(uint32_t command) const;

    size_t presetCount() const { return presets_.size(); }
    size_t toolCount() const { return tools_.size(); }
    const std::vector<ToolPreset>& presets() const { return presets_; }

    // Convenience accessors used by the coverage report.
    size_t implementedPresetCount() const;
    size_t categoryCount() const;
    std::vector<std::string> categories() const;

    void clear();

private:
    std::vector<ToolPreset>              presets_;
    std::map<ToolId, uint32_t>           byId_;
    std::map<uint32_t, ToolId>           byCommand_;
    std::map<ToolBehavior, Tool*>        tools_;
    uint32_t                             nextPresetId_;
};

// ---------------------------------------------------------------------------
// Built-in tool ids (the ones T01 itself implements as examples)
// ---------------------------------------------------------------------------
// These exist so the acceptance criterion "two example tools can register and
// submit through the same transaction entry point" is satisfied by real code,
// not by a test-only stub.
namespace tool_ids {
extern const char* kSelect;
extern const char* kBondSolid;
extern const char* kRingBenzene;
extern const char* kTextCaption;
extern const char* kMarquee;
extern const char* kEraser;
} // namespace tool_ids

// ---------------------------------------------------------------------------
// Small concrete tools
// ---------------------------------------------------------------------------
// SelectionTool: click selects an atom/bond/text; Shift adds; drag on an object
//   translates the selection in ONE transaction; drag on empty space is a
//   marquee.
// BondTool: press on an atom/nothing, drag, release -> create or retarget a bond
//   through a single transaction. Hop count and direction come from the
//   platform profile in a later task; v1 uses the pointer delta directly.
// PlaceRingTool: click places a 6-ring anchored on a hovered atom.
class SelectionTool : public Tool {
public:
    const ToolId& id() const;
    ToolBehavior behavior() const { return ToolBehavior::Select; }
    CommandResult beginGesture(ToolHost& host, const PointerState& pointer, HitContext& hits);
    CommandResult updateGesture(ToolHost& host, const PointerState& pointer, HitContext& hits);
    CommandResult endGesture(ToolHost& host, const PointerState& pointer, HitContext& hits);
    CommandResult cancelGesture(ToolHost& host);
    // Geometry-only preview: the dragged objects are shown transformed by the
    // SAME delta that commit will apply.
    const Vec2& previewDelta() const { return delta_; }
private:
    Vec2 delta_;
    Vec2 origin_;
    bool dragging_;
    std::vector<ObjectRef> dragSet_;
    bool takeDragSet_;
public:
    SelectionTool() : origin_(0, 0), dragging_(false), takeDragSet_(true) {}
};

class BondTool : public Tool {
public:
    explicit BondTool(BondOrder order) : order_(order), pressed_(false), startAtom_(kInvalidObjectId) {}
    const ToolId& id() const;
    ToolBehavior behavior() const { return ToolBehavior::DragBond; }
    CommandResult beginGesture(ToolHost& host, const PointerState& pointer, HitContext& hits);
    CommandResult updateGesture(ToolHost& host, const PointerState& pointer, HitContext& hits);
    CommandResult endGesture(ToolHost& host, const PointerState& pointer, HitContext& hits);
    CommandResult cancelGesture(ToolHost& host);
    BondOrder order() const { return order_; }
private:
    BondOrder   order_;
    bool        pressed_;
    ObjectId    startAtom_;
    Vec2        startPoint_;
    Vec2        endPoint_;
    ObjectId    targetAtom_;
};

class PlaceRingTool : public Tool {
public:
    const ToolId& id() const;
    ToolBehavior behavior() const { return ToolBehavior::PlaceRings; }
    CommandResult beginGesture(ToolHost& host, const PointerState& pointer, HitContext& hits);
    CommandResult updateGesture(ToolHost& host, const PointerState& pointer, HitContext& hits);
    CommandResult endGesture(ToolHost& host, const PointerState& pointer, HitContext& hits);
    CommandResult cancelGesture(ToolHost& host);
    PlaceRingTool() : anchor_(kInvalidObjectId), center_(0, 0), radius_(14.4) {}
    // Exposed so a test can assert preview and commit used one geometry.
    const Vec2& previewCenter() const { return center_; }
    double previewRadius() const { return radius_; }
    ObjectId anchor() const { return anchor_; }
private:
    ObjectId anchor_;
    Vec2     center_;
    double   radius_;
};

} // namespace core
} // namespace r62

#endif // R62_NEXT_CORE_TOOL_REGISTRY_HPP
