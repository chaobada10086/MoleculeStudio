#ifndef R62_NEXT_GRAPHICS_TOOLS_HPP
#define R62_NEXT_GRAPHICS_TOOLS_HPP
// =============================================================================
// re_round62_viewer/next/graphics/tools.hpp
//
// T11 - INTERACTION TOOLS for the 32 shapes, the 8 brackets and the table.
//
// These implement the T01 core::Tool interface, so T04 (platform input routing)
// drives them through exactly the same event path as every other tool: it never
// sees an HWND and these never see a message id or a pixel coordinate. Every
// gesture obeys the T01 rules:
//
//   * during a gesture the document is NOT touched; the tool publishes a
//     PreviewScene (buildPreview()) and nothing else;
//   * on release the SAME resolved parameters that were previewed are committed
//     through ToolHost::submit() as ONE transaction == ONE undo step;
//   * cancelGesture() publishes nothing and commits nothing.
//
// The preview/commit agreement is structural here: both read the parameter bundle
// produced by the single call to resolve() at end-gesture time, and
// resolveShapePreset()/resolveBracketPreset() are pure functions of the two
// pointer positions.
// =============================================================================

#include "next/core/tool_registry.hpp"
#include "next/graphics/model.hpp"
#include "next/graphics/tables.hpp"

#include <string>
#include <vector>

namespace r62 {
namespace graphics {

// ---------------------------------------------------------------------------
// Namespaced tool ids
// ---------------------------------------------------------------------------
// Same convention as T01's tool_ids: a dotted, namespaced string that never
// collides with another module's presets.
namespace tool_ids {
extern const char* const kPlaceShape;    // one instance serves all 32 presets
extern const char* const kPlaceBracket;  // one instance serves all 8 presets
extern const char* const kPlaceGraphics; // the PlaceSingle dispatcher the host registers
extern const char* const kPlaceTable;
extern const char* const kEditTable;
extern const char* const kEditShape;     // handle dragging for shape/bracket
} // namespace tool_ids

// ---------------------------------------------------------------------------
// Placement tool for the 32 shape presets
// ---------------------------------------------------------------------------
// Press sets one corner, drag sets the opposite corner, release commits. The
// preset is chosen by activatePreset(); the tool itself has no per-preset code
// path, which is the mechanical meaning of "32 presets are data".
class PlaceShapeTool : public core::Tool {
public:
    PlaceShapeTool();
    const core::ToolId& id() const;
    core::ToolBehavior behavior() const { return core::ToolBehavior::PlaceSingle; }

    // Select which of the 32 presets this instance places. Returns false for an
    // unknown preset id, so a mis-wired toolbar button fails loudly.
    bool activatePreset(uint32_t presetId);
    uint32_t presetId() const { return presetId_; }

    core::CommandResult beginGesture(core::ToolHost& host, const core::PointerState& pointer,
                                     core::HitContext& hits);
    core::CommandResult updateGesture(core::ToolHost& host, const core::PointerState& pointer,
                                      core::HitContext& hits);
    core::CommandResult endGesture(core::ToolHost& host, const core::PointerState& pointer,
                                   core::HitContext& hits);
    core::CommandResult cancelGesture(core::ToolHost& host);
    bool handleKey(core::ToolHost& host, uint32_t virtualKey, uint32_t modifiers);

    // The resolved previewed parameters, exposed so a test can assert that the
    // preview and the committed object are the same geometry.
    const ShapeParams& previewParams() const { return preview_; }
    bool dragging() const { return dragging_; }
    void buildPreview(core::PreviewScene* scene, const core::ObjectRef& subject) const;

private:
    core::ToolId id_;
    uint32_t     presetId_;
    bool         dragging_;
    core::Vec2   origin_;
    core::Vec2   current_;
    ShapeParams  preview_;
    bool         constrain_;   // Shift: force a perfect square / circle box
};

// ---------------------------------------------------------------------------
// Placement tool for the 8 bracket presets
// ---------------------------------------------------------------------------
class PlaceBracketTool : public core::Tool {
public:
    PlaceBracketTool();
    const core::ToolId& id() const;
    core::ToolBehavior behavior() const { return core::ToolBehavior::PlaceSingle; }
    bool activatePreset(uint32_t presetId);
    uint32_t presetId() const { return presetId_; }

    core::CommandResult beginGesture(core::ToolHost& host, const core::PointerState& pointer,
                                     core::HitContext& hits);
    core::CommandResult updateGesture(core::ToolHost& host, const core::PointerState& pointer,
                                      core::HitContext& hits);
    core::CommandResult endGesture(core::ToolHost& host, const core::PointerState& pointer,
                                   core::HitContext& hits);
    core::CommandResult cancelGesture(core::ToolHost& host);

    const BracketParams& previewParams() const { return preview_; }
    bool dragging() const { return dragging_; }
    void buildPreview(core::PreviewScene* scene, const core::ObjectRef& subject) const;

private:
    core::ToolId  id_;
    uint32_t      presetId_;
    bool          dragging_;
    core::Vec2    origin_;
    core::Vec2    current_;
    BracketParams preview_;
};

// ---------------------------------------------------------------------------
// PlaceSingle dispatcher: one behavior, one instance
// ---------------------------------------------------------------------------
// core::ToolRegistry keys tools by ToolBehavior and keeps ONE instance per
// behavior ("v1: one instance per behavior"). Shapes and brackets are both
// ToolBehavior::PlaceSingle, so registering the two placers separately silently
// drops the first one (std::map overwrite) and a bracket preset would then be
// served by the SHAPE placer - it would create an Arrow where a Bracket belongs.
//
// This adapter owns PlaceSingle and forwards every event to whichever placer the
// activated preset belongs to. The host still owns the two concrete placers and
// hands them over with adopt(); the adapter never owns or deletes them.
//
// Preset numbering: registry preset ids 0..31 are the 32 shapes, 32..39 the 8
// brackets (that is the order registerGraphicsPresets() registers them in), and
// the table brings its own behavior.
class PlaceGraphicsTool : public core::Tool {
public:
    PlaceGraphicsTool();
    void adopt(PlaceShapeTool* shapes, PlaceBracketTool* brackets);
    const core::ToolId& id() const;
    core::ToolBehavior behavior() const { return core::ToolBehavior::PlaceSingle; }

    // Accepts the REGISTRY preset id (0..39) and activates the right placer.
    // Returns false for an id that is neither a shape nor a bracket preset.
    bool activatePreset(uint32_t registryPresetId);
    uint32_t presetId() const { return presetId_; }
    bool placingBracket() const { return bracket_; }
    PlaceShapeTool*   shapes() const { return shapes_; }
    PlaceBracketTool* brackets() const { return brackets_; }

    core::CommandResult beginGesture(core::ToolHost& host, const core::PointerState& pointer,
                                     core::HitContext& hits);
    core::CommandResult updateGesture(core::ToolHost& host, const core::PointerState& pointer,
                                      core::HitContext& hits);
    core::CommandResult endGesture(core::ToolHost& host, const core::PointerState& pointer,
                                   core::HitContext& hits);
    core::CommandResult cancelGesture(core::ToolHost& host);
    bool handleKey(core::ToolHost& host, uint32_t virtualKey, uint32_t modifiers);
    void buildPreview(core::PreviewScene* scene, const core::ObjectRef& subject) const;

private:
    core::ToolId      id_;
    PlaceShapeTool*   shapes_;
    PlaceBracketTool* brackets_;
    uint32_t          presetId_;
    bool              bracket_;
};

// ---------------------------------------------------------------------------
// Table creation tool: drag the frame, then auto-fill rows/columns from the
// default cell size. Row/column counts can be overridden before the drag.
// ---------------------------------------------------------------------------
class PlaceTableTool : public core::Tool {
public:
    PlaceTableTool();
    const core::ToolId& id() const;
    core::ToolBehavior behavior() const { return core::ToolBehavior::Table; }
    // Which cell the drag must have reached before the table gets another column.
    void setDefaultCellSize(double width, double height);
    void setMinimumGrid(uint32_t rows, uint32_t columns);

    core::CommandResult beginGesture(core::ToolHost& host, const core::PointerState& pointer,
                                     core::HitContext& hits);
    core::CommandResult updateGesture(core::ToolHost& host, const core::PointerState& pointer,
                                      core::HitContext& hits);
    core::CommandResult endGesture(core::ToolHost& host, const core::PointerState& pointer,
                                   core::HitContext& hits);
    core::CommandResult cancelGesture(core::ToolHost& host);

    const TableGrid& previewGrid() const { return preview_; }
    bool dragging() const { return dragging_; }
    void buildPreview(core::PreviewScene* scene, const core::ObjectRef& subject) const;

private:
    core::ToolId id_;
    bool         dragging_;
    core::Vec2   origin_;
    core::Vec2   current_;
    double       cellWidth_;
    double       cellHeight_;
    uint32_t     minRows_, minColumns_;
    TableGrid    preview_;
};

// ---------------------------------------------------------------------------
// Table edit tool: cell selection and per-cell text entry
// ---------------------------------------------------------------------------
// Deliberately does NOT do row/column resizing: those are handle drags on the
// table overlay, and they are exposed as plain functions
// (TableEditGestures below) so T19 can wire whichever handle scheme it uses.
class EditTableTool : public core::Tool {
public:
    EditTableTool();
    const core::ToolId& id() const;
    core::ToolBehavior behavior() const { return core::ToolBehavior::Select; }

    // Which table is being edited (set by T19 when the host selection changes).
    void beginEdit(core::ObjectId tableId);
    void endEdit();
    core::ObjectId table() const { return table_; }
    const TableCellRange& range() const { return range_; }
    // Typing target: the single cell inside the current range.
    const std::string& typedText() const { return typed_; }

    core::CommandResult beginGesture(core::ToolHost& host, const core::PointerState& pointer,
                                     core::HitContext& hits);
    core::CommandResult updateGesture(core::ToolHost& host, const core::PointerState& pointer,
                                      core::HitContext& hits);
    core::CommandResult endGesture(core::ToolHost& host, const core::PointerState& pointer,
                                   core::HitContext& hits);
    core::CommandResult cancelGesture(core::ToolHost& host);
    bool handleChar(core::ToolHost& host, uint32_t unicodeChar, uint32_t modifiers);
    bool handleKey(core::ToolHost& host, uint32_t virtualKey, uint32_t modifiers);

private:
    core::ToolId      id_;
    core::ObjectId    table_;
    TableCellRange    range_;
    std::string       typed_;
    bool              dragSelect_;
    uint32_t          anchorRow_, anchorColumn_;
};

// ---------------------------------------------------------------------------
// Handle-drag gestures for shapes, brackets and table borders
// ---------------------------------------------------------------------------
// These are NOT Tools; they are small value objects a tool (or T19 directly)
// drives. Each one resolves an ABSOLUTE parameter bundle during the drag and
// hands that same bundle to the commit, which is the T05 rule "preview and commit
// must use one resolved value".
struct ShapeHandleGesture {
    enum Handle { None = 0, CornerTL, CornerTR, CornerBR, CornerBL, EdgeTop,
                  EdgeRight, EdgeBottom, EdgeLeft };
    core::ObjectId object;
    Handle         handle;
    ShapeParams    start;
    bool           active;
    // The gesture carries the LATEST resolved value so the preview and the commit
    // are the same object, and the caller does not have to re-resolve at commit
    // time (which is where preview/commit divergence historically came from).
    ShapeParams    current;
    bool           keepAspect;
    bool           freeAxes;
    ShapeHandleGesture()
        : object(core::kInvalidObjectId), handle(None), active(false),
          keepAspect(false), freeAxes(false) {}
    bool begin(const core::Document& document, core::ObjectId id, Handle h,
               const core::Vec2& pointer, double tolerance);
    // Resolve AND remember the new parameters for a pointer position. Shift keeps
    // the aspect ratio; Alt frees the two axes independently. Pure: touches
    // nothing in the document.
    const ShapeParams& update(const core::Vec2& pointer, bool keepAspectRatio, bool freeBothAxes);
    ShapeParams resolve(const core::Vec2& pointer, bool keepAspectRatio, bool freeBothAxes) const;
    void cancel();
    // Fill the preview with the CURRENT resolved outline (no document write).
    void buildPreview(core::PreviewScene* scene, const core::ObjectRef& subject) const;
};

struct BracketHandleGesture {
    enum Handle { None = 0, Start, End, Left, Right, Label0 };
    core::ObjectId object;
    Handle         handle;
    BracketParams  start;
    bool           active;
    BracketParams  current;
    BracketHandleGesture() : object(core::kInvalidObjectId), handle(None), active(false) {}
    bool begin(const core::Document& document, core::ObjectId id, Handle h,
               const core::Vec2& pointer, double tolerance);
    const BracketParams& update(const core::Vec2& pointer);
    BracketParams resolve(const core::Vec2& pointer) const;
    void cancel();
    void buildPreview(core::PreviewScene* scene, const core::ObjectRef& subject) const;
};

struct TableBorderGesture {
    core::ObjectId table;
    int            columnIndex;   // -1 == none
    int            rowIndex;      // -1 == none
    double         startSize;
    core::Vec2     startPointer;
    bool           active;
    TableBorderGesture() : table(core::kInvalidObjectId), columnIndex(-1), rowIndex(-1),
                          startSize(0.0), active(false) {}
    // `tolerance` is in document units. Returns false when the pointer is not on
    // an internal border of that table.
    bool begin(const core::Document& document, core::ObjectId id, const core::Vec2& pointer,
               double tolerance);
    // The absolute delta to feed resizeColumn()/resizeRow() (which take deltas).
    double resolveDelta(const core::Vec2& pointer) const;
    void cancel();
    void buildPreview(core::PreviewScene* scene, double pointerX, double pointerY) const;
};

// ---------------------------------------------------------------------------
// Preview builders shared by the tools and by the handle gestures
// ---------------------------------------------------------------------------
// "Preview and commit use one resolved value" only holds if there is ONE place
// that turns a parameter bundle into preview primitives. These are that place.
void buildShapePreview(core::PreviewScene* scene, const ShapeParams& params,
                       const core::ObjectRef& subject);
void buildBracketPreview(core::PreviewScene* scene, const BracketParams& params,
                         double labelTextSize, const core::ObjectRef& subject);
void buildTablePreview(core::PreviewScene* scene, const TableGrid& grid,
                       const core::ObjectRef& subject);
void buildTableCellPreview(core::PreviewScene* scene, const core::Document& document,
                           core::ObjectId tableId, const TableCellRange& range);

// ---------------------------------------------------------------------------
// Registry wiring
// ---------------------------------------------------------------------------
// Register the placement tools and bind platform command ids for every preset.
// Returns the number of presets that were successfully bound. A caller (T19)
// calls this once at start-up; the function is idempotent per registry because
// registerPreset() rejects duplicates with AlreadyExists, which is counted as a
// failure rather than silently tolerated.
// The dispatcher must already have adopt()ed the shape and bracket placers.
// It is registered under PlaceSingle; the table tool under Table. Two tools is
// the maximum T01's one-instance-per-behavior registry allows for this module.
size_t registerGraphicsPresets(core::ToolRegistry& registry, PlaceGraphicsTool* graphics,
                               PlaceTableTool* table, std::string* firstError);

} // namespace graphics
} // namespace r62

#endif // R62_NEXT_GRAPHICS_TOOLS_HPP
