#include "next/graphics/tools.hpp"

#include <algorithm>
#include <cmath>

namespace r62 {
namespace graphics {

namespace tool_ids {
const char* const kPlaceShape   = "r62.tool.graphics.shape";
const char* const kPlaceBracket = "r62.tool.graphics.bracket";
const char* const kPlaceTable   = "r62.tool.graphics.table";
const char* const kEditTable    = "r62.tool.graphics.table.edit";
const char* const kEditShape    = "r62.tool.graphics.shape.edit";
// The PlaceSingle dispatcher. Shapes and brackets share one behavior value, and
// T01's registry holds one instance per behavior, so this is the id the host sees
// for BOTH of them; the concrete placers keep their own ids for direct use.
const char* const kPlaceGraphics = "r62.tool.graphics.place";
} // namespace tool_ids

namespace {

double clampd(double v, double lo, double hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// ---------------------------------------------------------------------------
// Preview helpers
// ---------------------------------------------------------------------------
core::PreviewItem lineItem(const core::Vec2& a, const core::Vec2& b, const core::LineStyle& s) {
    core::PreviewItem it;
    it.primitive = core::PreviewPrimitive::Segment;
    it.points.push_back(a);
    it.points.push_back(b);
    it.style = s;
    return it;
}

core::PreviewItem polyItem(const std::vector<core::Vec2>& pts, bool closed,
                           const core::LineStyle& s, uint32_t fill, bool fillEnabled) {
    core::PreviewItem it;
    it.primitive = closed ? core::PreviewPrimitive::Polygon : core::PreviewPrimitive::Polyline;
    it.points = pts;
    it.style = s;
    it.fillRGBA = fill;
    it.fillEnabled = fillEnabled ? 1 : 0;
    return it;
}

core::PreviewItem rectItem(const core::Box& b, const core::LineStyle& s, uint32_t fill,
                           bool fillEnabled) {
    core::PreviewItem it;
    it.primitive = core::PreviewPrimitive::Rect;
    it.points.push_back(b.min);
    it.points.push_back(b.max);
    it.style = s;
    it.fillRGBA = fill;
    it.fillEnabled = fillEnabled ? 1 : 0;
    return it;
}

core::LineStyle previewStyleFor(const ShapeParams& p, double defaultWidth) {
    core::LineStyle s = p.style;
    if (!(s.width > 0.0)) s.width = defaultWidth;
    return s;
}

core::LineStyle previewStyleFor(const BracketParams& p, double defaultWidth) {
    core::LineStyle s = p.style;
    if (!(s.width > 0.0)) s.width = defaultWidth;
    return s;
}

// Convenience: the default cell size used when a table drag does not say.
const double kDefaultCellWidth  = 48.0;   // [PROJ] document units
const double kDefaultCellHeight = 18.0;   // [PROJ]

// Same construction as model.cpp's resolveShapePreset uses for a Line: the shape
// is the DIAGONAL of the drag, and shapeOutline() returns exactly a and b for
// either drag direction. Kept here because the handle gesture needs it too.
core::Vec2 lineFrameFromEndpoints(const core::Vec2& a, const core::Vec2& b, double width,
                                  ShapeFrame* out) {
    out->centre = core::Vec2((a.x + b.x) * 0.5, (a.y + b.y) * 0.5);
    out->axisU = core::Vec2((b.x - a.x) * 0.5, (b.y - a.y) * 0.5);
    const double len = core::length(out->axisU);
    const double halfW = std::max(width, 0.05) * 0.5;
    if (len > 1e-12)
        out->axisV = core::Vec2(-out->axisU.y / len * halfW, out->axisU.x / len * halfW);
    else
        out->axisV = core::Vec2(0.0, halfW);
    return out->axisV;
}

} // namespace

// ===========================================================================
// PlaceShapeTool
// ===========================================================================
PlaceShapeTool::PlaceShapeTool()
    : id_(tool_ids::kPlaceShape), presetId_(0u), dragging_(false),
      origin_(0, 0), current_(0, 0), constrain_(false) {}

const core::ToolId& PlaceShapeTool::id() const { return id_; }

bool PlaceShapeTool::activatePreset(uint32_t presetId) {
    if (!shapePresetById(presetId)) return false;
    presetId_ = presetId;
    dragging_ = false;
    return true;
}

core::CommandResult PlaceShapeTool::beginGesture(core::ToolHost& host,
                                                 const core::PointerState& pointer,
                                                 core::HitContext& hits) {
    (void)hits;
    if (!shapePresetById(presetId_))
        return core::CommandResult::fail(core::ErrorCode::InvalidArgument, "No shape preset selected");
    origin_ = pointer.position;
    current_ = pointer.position;
    dragging_ = true;
    constrain_ = (pointer.modifiers & core::kModShift) != 0u;
    preview_ = resolveShapePreset(presetId_, origin_, current_, host.document().defaultLineWidth());
    host.setStatus(std::string("place ") + shapePresetById(presetId_)->displayName);
    return core::CommandResult::noChange();
}

core::CommandResult PlaceShapeTool::updateGesture(core::ToolHost& host,
                                                  const core::PointerState& pointer,
                                                  core::HitContext& hits) {
    (void)hits;
    if (!dragging_) return core::CommandResult::noChange();
    current_ = pointer.position;
    constrain_ = (pointer.modifiers & core::kModShift) != 0u;
    core::Vec2 end = current_;
    if (constrain_) {
        const double dx = end.x - origin_.x;
        const double dy = end.y - origin_.y;
        const double s = std::max(std::fabs(dx), std::fabs(dy));
        end = core::Vec2(origin_.x + (dx < 0 ? -s : s), origin_.y + (dy < 0 ? -s : s));
    }
    preview_ = resolveShapePreset(presetId_, origin_, end, host.document().defaultLineWidth());
    // The preview scene is rebuilt from the SAME resolved parameters the commit
    // will use. No document write happens here.
    host.preview().clear();
    buildPreview(&host.preview(), core::ObjectRef());
    return core::CommandResult::noChange();
}

core::CommandResult PlaceShapeTool::endGesture(core::ToolHost& host,
                                               const core::PointerState& pointer,
                                               core::HitContext& hits) {
    (void)hits;
    if (!dragging_) return core::CommandResult::noChange();
    dragging_ = false;
    current_ = pointer.position;
    core::Vec2 end = current_;
    if (constrain_) {
        const double dx = end.x - origin_.x;
        const double dy = end.y - origin_.y;
        const double s = std::max(std::fabs(dx), std::fabs(dy));
        end = core::Vec2(origin_.x + (dx < 0 ? -s : s), origin_.y + (dy < 0 ? -s : s));
    }
    const ShapeParams resolved =
        resolveShapePreset(presetId_, origin_, end, host.document().defaultLineWidth());
    preview_ = resolved;
    host.preview().clear();

    core::Command cmd;
    const ShapeParams params = resolved;
    const double defaultWidth = host.document().defaultLineWidth();
    cmd = core::Command("graphics.placeShape", core::kInvalidateStructure | core::kInvalidateGeometry,
                        [params, defaultWidth](core::CommandContext& ctx) -> core::CommandResult {
        core::ObjectId id = core::kInvalidObjectId;
        core::Error e = addShape(ctx.document, params, defaultWidth, &id);
        if (!e.ok()) return core::CommandResult::fail(e.code, e.message);
        return core::CommandResult::success(core::kInvalidateStructure | core::kInvalidateGeometry);
    });
    core::Error e = host.submit(cmd, "graphics.placeShape");
    if (!e.ok()) return core::CommandResult::fail(e.code, e.message);
    return core::CommandResult::success(core::kInvalidateStructure | core::kInvalidateGeometry);
}

core::CommandResult PlaceShapeTool::cancelGesture(core::ToolHost& host) {
    dragging_ = false;
    host.preview().clear();
    host.setStatus("cancelled");
    return core::CommandResult::noChange();
}

bool PlaceShapeTool::handleKey(core::ToolHost& host, uint32_t virtualKey, uint32_t modifiers) {
    (void)host; (void)modifiers;
    if (virtualKey == 27u /* VK_ESCAPE */) { cancelGesture(host); return true; }
    return false;
}

void PlaceShapeTool::buildPreview(core::PreviewScene* scene, const core::ObjectRef& subject) const {
    buildShapePreview(scene, preview_, subject);
}

// ===========================================================================
// PlaceBracketTool
// ===========================================================================
PlaceBracketTool::PlaceBracketTool()
    : id_(tool_ids::kPlaceBracket), presetId_(0u), dragging_(false),
      origin_(0, 0), current_(0, 0) {}

const core::ToolId& PlaceBracketTool::id() const { return id_; }

bool PlaceBracketTool::activatePreset(uint32_t presetId) {
    if (!bracketPresetById(presetId)) return false;
    presetId_ = presetId;
    dragging_ = false;
    return true;
}

core::CommandResult PlaceBracketTool::beginGesture(core::ToolHost& host,
                                                   const core::PointerState& pointer,
                                                   core::HitContext& hits) {
    (void)hits;
    if (!bracketPresetById(presetId_))
        return core::CommandResult::fail(core::ErrorCode::InvalidArgument, "No bracket preset selected");
    origin_ = pointer.position;
    current_ = pointer.position;
    dragging_ = true;
    preview_ = resolveBracketPreset(presetId_, origin_, current_, host.document().defaultLineWidth());
    host.setStatus(std::string("place ") + bracketPresetById(presetId_)->displayName);
    return core::CommandResult::noChange();
}

core::CommandResult PlaceBracketTool::updateGesture(core::ToolHost& host,
                                                    const core::PointerState& pointer,
                                                    core::HitContext& hits) {
    (void)hits;
    if (!dragging_) return core::CommandResult::noChange();
    current_ = pointer.position;
    preview_ = resolveBracketPreset(presetId_, origin_, current_, host.document().defaultLineWidth());
    host.preview().clear();
    buildPreview(&host.preview(), core::ObjectRef());
    return core::CommandResult::noChange();
}

core::CommandResult PlaceBracketTool::endGesture(core::ToolHost& host,
                                                 const core::PointerState& pointer,
                                                 core::HitContext& hits) {
    (void)hits;
    if (!dragging_) return core::CommandResult::noChange();
    dragging_ = false;
    current_ = pointer.position;
    const BracketParams resolved =
        resolveBracketPreset(presetId_, origin_, current_, host.document().defaultLineWidth());
    preview_ = resolved;
    host.preview().clear();
    const BracketParams params = resolved;
    const double defaultWidth = host.document().defaultLineWidth();
    core::Command cmd("graphics.placeBracket",
                      core::kInvalidateStructure | core::kInvalidateGeometry,
                      [params, defaultWidth](core::CommandContext& ctx) -> core::CommandResult {
        core::ObjectId id = core::kInvalidObjectId;
        core::Error e = addBracket(ctx.document, params, defaultWidth, &id);
        if (!e.ok()) return core::CommandResult::fail(e.code, e.message);
        return core::CommandResult::success(core::kInvalidateStructure | core::kInvalidateGeometry);
    });
    core::Error e = host.submit(cmd, "graphics.placeBracket");
    if (!e.ok()) return core::CommandResult::fail(e.code, e.message);
    return core::CommandResult::success(core::kInvalidateStructure | core::kInvalidateGeometry);
}

core::CommandResult PlaceBracketTool::cancelGesture(core::ToolHost& host) {
    dragging_ = false;
    host.preview().clear();
    host.setStatus("cancelled");
    return core::CommandResult::noChange();
}

void PlaceBracketTool::buildPreview(core::PreviewScene* scene, const core::ObjectRef& subject) const {
    buildBracketPreview(scene, preview_, 10.0, subject);
}

// ===========================================================================
// PlaceGraphicsTool (the PlaceSingle dispatcher)
// ===========================================================================
PlaceGraphicsTool::PlaceGraphicsTool()
    : id_(tool_ids::kPlaceGraphics), shapes_(0), brackets_(0), presetId_(0u),
      bracket_(false) {}

void PlaceGraphicsTool::adopt(PlaceShapeTool* shapes, PlaceBracketTool* brackets) {
    shapes_ = shapes;
    brackets_ = brackets;
}

const core::ToolId& PlaceGraphicsTool::id() const { return id_; }

bool PlaceGraphicsTool::activatePreset(uint32_t registryPresetId) {
    // 0..31 are the shapes, 32..39 the brackets - the registration order in
    // registerGraphicsPresets(), which is also the order the preset ids are
    // assigned in. Anything else is a mis-wired toolbar button.
    if (registryPresetId < shapePresetCount()) {
        if (!shapes_ || !shapes_->activatePreset(registryPresetId)) return false;
        presetId_ = registryPresetId;
        bracket_ = false;
        return true;
    }
    const uint32_t bracketId = registryPresetId - static_cast<uint32_t>(shapePresetCount());
    if (bracketId < bracketPresetCount()) {
        if (!brackets_ || !brackets_->activatePreset(bracketId)) return false;
        presetId_ = registryPresetId;
        bracket_ = true;
        return true;
    }
    return false;
}

core::CommandResult PlaceGraphicsTool::beginGesture(core::ToolHost& host,
                                                    const core::PointerState& pointer,
                                                    core::HitContext& hits) {
    if (bracket_) return brackets_ ? brackets_->beginGesture(host, pointer, hits)
                                   : core::CommandResult::fail(core::ErrorCode::NotImplemented,
                                                               "No bracket placer installed");
    return shapes_ ? shapes_->beginGesture(host, pointer, hits)
                   : core::CommandResult::fail(core::ErrorCode::NotImplemented,
                                               "No shape placer installed");
}

core::CommandResult PlaceGraphicsTool::updateGesture(core::ToolHost& host,
                                                     const core::PointerState& pointer,
                                                     core::HitContext& hits) {
    if (bracket_) return brackets_ ? brackets_->updateGesture(host, pointer, hits)
                                   : core::CommandResult::noChange();
    return shapes_ ? shapes_->updateGesture(host, pointer, hits) : core::CommandResult::noChange();
}

core::CommandResult PlaceGraphicsTool::endGesture(core::ToolHost& host,
                                                  const core::PointerState& pointer,
                                                  core::HitContext& hits) {
    if (bracket_) return brackets_ ? brackets_->endGesture(host, pointer, hits)
                                   : core::CommandResult::noChange();
    return shapes_ ? shapes_->endGesture(host, pointer, hits) : core::CommandResult::noChange();
}

core::CommandResult PlaceGraphicsTool::cancelGesture(core::ToolHost& host) {
    if (bracket_) return brackets_ ? brackets_->cancelGesture(host) : core::CommandResult::noChange();
    return shapes_ ? shapes_->cancelGesture(host) : core::CommandResult::noChange();
}

bool PlaceGraphicsTool::handleKey(core::ToolHost& host, uint32_t virtualKey, uint32_t modifiers) {
    if (bracket_) return false;   // brackets have no Shift-constrain yet
    return shapes_ ? shapes_->handleKey(host, virtualKey, modifiers) : false;
}

void PlaceGraphicsTool::buildPreview(core::PreviewScene* scene,
                                     const core::ObjectRef& subject) const {
    if (bracket_) {
        if (brackets_) brackets_->buildPreview(scene, subject);
    } else if (shapes_) {
        shapes_->buildPreview(scene, subject);
    }
}

// ===========================================================================
// PlaceTableTool
// ===========================================================================
PlaceTableTool::PlaceTableTool()
    : id_(tool_ids::kPlaceTable), dragging_(false), origin_(0, 0), current_(0, 0),
      cellWidth_(kDefaultCellWidth), cellHeight_(kDefaultCellHeight),
      minRows_(1u), minColumns_(1u) {}

const core::ToolId& PlaceTableTool::id() const { return id_; }

void PlaceTableTool::setDefaultCellSize(double width, double height) {
    if (width > 0.0) cellWidth_ = width;
    if (height > 0.0) cellHeight_ = height;
}

void PlaceTableTool::setMinimumGrid(uint32_t rows, uint32_t columns) {
    minRows_ = rows > 0u ? rows : 1u;
    minColumns_ = columns > 0u ? columns : 1u;
}

namespace {

TableGrid gridForDrag(const core::Vec2& a, const core::Vec2& b, double cellW, double cellH,
                      uint32_t minRows, uint32_t minColumns) {
    TableGrid g;
    const double w = std::fabs(b.x - a.x);
    const double h = std::fabs(b.y - a.y);
    uint32_t cols = static_cast<uint32_t>(w / cellW) + 1u;
    uint32_t rows = static_cast<uint32_t>(h / cellH) + 1u;
    if (cols < minColumns) cols = minColumns;
    if (rows < minRows) rows = minRows;
    if (cols > 200u) cols = 200u;
    if (rows > 200u) rows = 200u;
    const core::Vec2 origin(std::min(a.x, b.x), std::min(a.y, b.y));
    // The last column / row absorbs the remainder so the frame matches the drag.
    const double totalW = w > cellW * (cols - 1u) ? w : cellW * cols;
    const double totalH = h > cellH * (rows - 1u) ? h : cellH * rows;
    g.columnWidths.assign(cols, totalW / static_cast<double>(cols));
    g.rowHeights.assign(rows, totalH / static_cast<double>(rows));
    g.frame = core::Box(origin, core::Vec2(origin.x + totalW, origin.y + totalH));
    return g;
}

} // namespace

core::CommandResult PlaceTableTool::beginGesture(core::ToolHost& host,
                                                 const core::PointerState& pointer,
                                                 core::HitContext& hits) {
    (void)hits;
    origin_ = pointer.position;
    current_ = pointer.position;
    dragging_ = true;
    preview_ = gridForDrag(origin_, current_, cellWidth_, cellHeight_, minRows_, minColumns_);
    host.setStatus("drag a table frame");
    return core::CommandResult::noChange();
}

core::CommandResult PlaceTableTool::updateGesture(core::ToolHost& host,
                                                  const core::PointerState& pointer,
                                                  core::HitContext& hits) {
    (void)hits;
    if (!dragging_) return core::CommandResult::noChange();
    current_ = pointer.position;
    preview_ = gridForDrag(origin_, current_, cellWidth_, cellHeight_, minRows_, minColumns_);
    host.preview().clear();
    buildPreview(&host.preview(), core::ObjectRef());
    return core::CommandResult::noChange();
}

core::CommandResult PlaceTableTool::endGesture(core::ToolHost& host,
                                               const core::PointerState& pointer,
                                               core::HitContext& hits) {
    (void)hits;
    if (!dragging_) return core::CommandResult::noChange();
    dragging_ = false;
    current_ = pointer.position;
    const TableGrid grid =
        gridForDrag(origin_, current_, cellWidth_, cellHeight_, minRows_, minColumns_);
    preview_ = grid;
    host.preview().clear();
    core::Command cmd("graphics.placeTable",
                      core::kInvalidateStructure | core::kInvalidateGeometry,
                      [grid](core::CommandContext& ctx) -> core::CommandResult {
        core::ObjectId id = core::kInvalidObjectId;
        core::Error e = createTable(ctx.document, grid, &id);
        if (!e.ok()) return core::CommandResult::fail(e.code, e.message);
        return core::CommandResult::success(core::kInvalidateStructure | core::kInvalidateGeometry);
    });
    core::Error e = host.submit(cmd, "graphics.placeTable");
    if (!e.ok()) return core::CommandResult::fail(e.code, e.message);
    return core::CommandResult::success(core::kInvalidateStructure | core::kInvalidateGeometry);
}

core::CommandResult PlaceTableTool::cancelGesture(core::ToolHost& host) {
    dragging_ = false;
    host.preview().clear();
    host.setStatus("cancelled");
    return core::CommandResult::noChange();
}

void PlaceTableTool::buildPreview(core::PreviewScene* scene, const core::ObjectRef& subject) const {
    buildTablePreview(scene, preview_, subject);
}

// ===========================================================================
// EditTableTool
// ===========================================================================
EditTableTool::EditTableTool()
    : id_(tool_ids::kEditTable), table_(core::kInvalidObjectId),
      dragSelect_(false), anchorRow_(0u), anchorColumn_(0u) {}

const core::ToolId& EditTableTool::id() const { return id_; }

void EditTableTool::beginEdit(core::ObjectId tableId) {
    table_ = tableId;
    range_ = TableCellRange();
    typed_.clear();
}

void EditTableTool::endEdit() {
    table_ = core::kInvalidObjectId;
    range_ = TableCellRange();
    typed_.clear();
}

core::CommandResult EditTableTool::beginGesture(core::ToolHost& host,
                                                const core::PointerState& pointer,
                                                core::HitContext& hits) {
    (void)hits;
    if (table_ == core::kInvalidObjectId)
        return core::CommandResult::fail(core::ErrorCode::InvalidArgument, "No table is being edited");
    TableData d;
    bool used = false;
    core::Error e = readTable(host.document(), table_, &d, &used);
    if (!e.ok()) return core::CommandResult::fail(e.code, e.message);
    uint32_t r = 0, c = 0;
    if (!d.grid.cellAt(pointer.position, &r, &c, 0.0, 0, 0)) {
        range_ = TableCellRange();
        return core::CommandResult::noChange();
    }
    anchorRow_ = r;
    anchorColumn_ = c;
    range_ = TableCellRange(table_, r, c, r, c);
    dragSelect_ = true;
    typed_.clear();
    host.setStatus("table cell selected");
    return core::CommandResult::noChange();
}

core::CommandResult EditTableTool::updateGesture(core::ToolHost& host,
                                                 const core::PointerState& pointer,
                                                 core::HitContext& hits) {
    (void)hits;
    if (!dragSelect_ || table_ == core::kInvalidObjectId)
        return core::CommandResult::noChange();
    TableData d;
    bool used = false;
    if (!readTable(host.document(), table_, &d, &used).ok())
        return core::CommandResult::noChange();
    uint32_t r = 0, c = 0;
    if (!d.grid.cellAt(pointer.position, &r, &c, 0.0, 0, 0)) return core::CommandResult::noChange();
    range_ = TableCellRange(table_, anchorRow_, anchorColumn_, r, c);
    return core::CommandResult::noChange();
}

core::CommandResult EditTableTool::endGesture(core::ToolHost& host,
                                              const core::PointerState& pointer,
                                              core::HitContext& hits) {
    (void)pointer; (void)hits;
    dragSelect_ = false;
    host.setStatus(range_.valid() ? "cell range selected" : "no cell");
    return core::CommandResult::noChange();
}

core::CommandResult EditTableTool::cancelGesture(core::ToolHost& host) {
    dragSelect_ = false;
    typed_.clear();
    host.setStatus("cancelled");
    return core::CommandResult::noChange();
}

bool EditTableTool::handleChar(core::ToolHost& host, uint32_t unicodeChar, uint32_t modifiers) {
    (void)modifiers;
    if (!range_.valid() || table_ == core::kInvalidObjectId) return false;
    if (unicodeChar == 8u || unicodeChar == 127u) {
        if (!typed_.empty()) typed_.erase(typed_.size() - 1);
    } else if (unicodeChar == 13u) {
        // Enter commits the typed text into the single selected cell.
        if (range_.rowCount() != 1u || range_.columnCount() != 1u) {
            host.reportUnsupported("table: multi-cell text entry");
            return false;
        }
        const std::string text = typed_;
        const core::ObjectId table = table_;
        const uint32_t row = range_.row0, column = range_.column0;
        core::Command cmd("graphics.setCellText", core::kInvalidateStructure,
                          [text, table, row, column](core::CommandContext& ctx) -> core::CommandResult {
            core::Error e = setCellText(ctx.document, table, row, column, text, 0.0);
            if (!e.ok()) return core::CommandResult::fail(e.code, e.message);
            return core::CommandResult::success(core::kInvalidateStructure);
        });
        core::Error e = host.submit(cmd, "graphics.setCellText");
        typed_.clear();
        if (!e.ok()) return false;
        return true;
    } else if (unicodeChar == 27u) {
        typed_.clear();
        return true;
    } else if (unicodeChar >= 32u) {
        // Encode the code point as UTF-8. Case is preserved: no tolower here.
        if (unicodeChar < 0x80u) {
            typed_.push_back(static_cast<char>(unicodeChar));
        } else if (unicodeChar < 0x800u) {
            typed_.push_back(static_cast<char>(0xC0u | (unicodeChar >> 6)));
            typed_.push_back(static_cast<char>(0x80u | (unicodeChar & 0x3Fu)));
        } else if (unicodeChar < 0x10000u) {
            typed_.push_back(static_cast<char>(0xE0u | (unicodeChar >> 12)));
            typed_.push_back(static_cast<char>(0x80u | ((unicodeChar >> 6) & 0x3Fu)));
            typed_.push_back(static_cast<char>(0x80u | (unicodeChar & 0x3Fu)));
        } else {
            typed_.push_back(static_cast<char>(0xF0u | (unicodeChar >> 18)));
            typed_.push_back(static_cast<char>(0x80u | ((unicodeChar >> 12) & 0x3Fu)));
            typed_.push_back(static_cast<char>(0x80u | ((unicodeChar >> 6) & 0x3Fu)));
            typed_.push_back(static_cast<char>(0x80u | (unicodeChar & 0x3Fu)));
        }
    } else {
        return false;
    }
    host.setStatus(std::string("cell text: ") + typed_);
    return true;
}

bool EditTableTool::handleKey(core::ToolHost& host, uint32_t virtualKey, uint32_t modifiers) {
    (void)modifiers;
    if (virtualKey == 27u) { typed_.clear(); return true; }
    if (!range_.valid() || table_ == core::kInvalidObjectId) return false;
    uint32_t r = range_.row0, c = range_.column0;
    // Arrow keys move the single-cell selection. The virtual key codes are the
    // Win32 ones; T04 maps its own keyboard state to them.
    if (virtualKey == 37u && c > 0u) --c;
    else if (virtualKey == 39u) ++c;
    else if (virtualKey == 38u && r > 0u) --r;
    else if (virtualKey == 40u) ++r;
    else return false;
    TableData d;
    bool used = false;
    if (!readTable(host.document(), table_, &d, &used).ok()) return false;
    if (r >= d.grid.rows() || c >= d.grid.columns()) return false;
    range_ = TableCellRange(table_, r, c, r, c);
    return true;
}

// ===========================================================================
// Handle gestures
// ===========================================================================
// ===========================================================================
// Shared preview builders
// ===========================================================================
void buildShapePreview(core::PreviewScene* scene, const ShapeParams& params,
                       const core::ObjectRef&) {
    if (!scene) return;
    const StrokeSpec stroke = strokeSpecFor(params, 0.6);
    ShapeGeometry g;
    if (!buildShapeGeometry(params, stroke, &g).ok()) return;
    const core::LineStyle s = previewStyleFor(params, stroke.width);
    if (g.hasFill && !g.fills.empty())
        scene->add(polyItem(g.fills[0].points, true, s, params.fillRGBA, true));
    for (size_t i = 0; i < g.strokes.size(); ++i)
        scene->add(polyItem(g.strokes[i].points, g.strokes[i].closed, s, 0u, false));
    for (size_t i = 0; i < g.shadows.size(); ++i)
        scene->add(polyItem(g.shadows[i].points, g.shadows[i].closed, s, 0x00000040u, true));
}

void buildBracketPreview(core::PreviewScene* scene, const BracketParams& params,
                         double labelTextSize, const core::ObjectRef&) {
    if (!scene) return;
    const StrokeSpec stroke = strokeSpecFor(params, 0.6);
    BracketGeometry g;
    if (!buildBracketGeometry(params, stroke, labelTextSize, &g).ok()) return;
    const core::LineStyle s = previewStyleFor(params, stroke.width);
    for (size_t i = 0; i < g.runs.size(); ++i) {
        const Contour& c = g.runs[i];
        if (params.family == BracketFamily::Curly ||
            params.family == BracketFamily::Parenthesis) {
            scene->add(polyItem(flattenQuadChain(c, 8), false, s, 0u, false));
        } else if (params.family == BracketFamily::Dagger) {
            std::vector<core::Vec2> spine;
            for (size_t k = 0; k < c.points.size() && c.kind.size() == c.points.size(); ++k)
                if (c.kind[k] != 2) spine.push_back(c.points[k]);
            scene->add(polyItem(spine, false, s, 0u, false));
            for (size_t k = 0; k + 1 < c.points.size(); ++k) {
                if (c.kind.size() == c.points.size() && c.kind[k] == 2 && c.kind[k + 1] == 2) {
                    scene->add(lineItem(c.points[k], c.points[k + 1], s));
                    ++k;
                }
            }
        } else {
            scene->add(polyItem(c.points, false, s, 0u, false));
        }
    }
}

void buildTablePreview(core::PreviewScene* scene, const TableGrid& grid,
                       const core::ObjectRef&) {
    if (!scene) return;
    core::LineStyle s;
    s.width = grid.borderWidth > 0.0 ? grid.borderWidth : 0.6;
    for (uint32_t r = 0; r < grid.rows(); ++r)
        for (uint32_t c = 0; c < grid.columns(); ++c)
            scene->add(rectItem(grid.cellBox(r, c), s, 0u, false));
}

void buildTableCellPreview(core::PreviewScene* scene, const core::Document& document,
                           core::ObjectId tableId, const TableCellRange& range) {
    if (!scene || !range.valid()) return;
    TableData d;
    bool used = false;
    if (!readTable(document, tableId, &d, &used).ok()) return;
    core::LineStyle s;
    s.width = 0.6;
    for (uint32_t r = range.row0; r <= range.row1 && r < d.grid.rows(); ++r)
        for (uint32_t c = range.column0; c <= range.column1 && c < d.grid.columns(); ++c)
            scene->add(rectItem(d.grid.cellBox(r, c), s, 0x3366FF33u, true));
}

bool ShapeHandleGesture::begin(const core::Document& document, core::ObjectId id, Handle h,
                               const core::Vec2& pointer, double tolerance) {
    ShapeParams p;
    bool used = false;
    if (!readShape(document, id, &p, &used).ok()) return false;
    start = p;
    current = p;
    object = id;
    handle = h;
    active = true;
    // The pointer must actually be near the handle it claims to have grabbed.
    core::Vec2 corners[4];
    p.frame.corners(corners);
    const core::Vec2* target = 0;
    switch (h) {
    case CornerTL: target = &corners[0]; break;
    case CornerTR: target = &corners[1]; break;
    case CornerBR: target = &corners[2]; break;
    case CornerBL: target = &corners[3]; break;
    default: return true;   // edge handles are checked by the caller
    }
    if (core::length(*target - pointer) > tolerance) {
        active = false;
        return false;
    }
    return true;
}

const ShapeParams& ShapeHandleGesture::update(const core::Vec2& pointer, bool keepAspectRatio,
                                             bool freeBothAxes) {
    keepAspect = keepAspectRatio;
    freeAxes = freeBothAxes;
    current = resolve(pointer, keepAspectRatio, freeBothAxes);
    return current;
}

ShapeParams ShapeHandleGesture::resolve(const core::Vec2& pointer, bool keepAspect,
                                        bool freeAxes) const {
    ShapeParams p = start;
    core::Vec2 corners[4];
    start.frame.corners(corners);
    // The FIXED corner is the one diagonally opposite the grabbed one.
    core::Vec2 fixed = corners[2];
    switch (handle) {
    case CornerTL: fixed = corners[2]; break;
    case CornerTR: fixed = corners[3]; break;
    case CornerBR: fixed = corners[0]; break;
    case CornerBL: fixed = corners[1]; break;
    case EdgeTop:    fixed = corners[2]; break;
    case EdgeRight:  fixed = corners[3]; break;
    case EdgeBottom: fixed = corners[0]; break;
    case EdgeLeft:   fixed = corners[1]; break;
    default: return p;
    }
    double halfW = std::fabs(pointer.x - fixed.x) * 0.5;
    double halfH = std::fabs(pointer.y - fixed.y) * 0.5;
    if (keepAspect) {
        const double s = std::max(halfW, halfH);
        halfW = s;
        halfH = s;
    }
    if (!freeAxes) {
        if (handle == EdgeTop || handle == EdgeBottom) halfW = start.frame.halfWidth();
        if (handle == EdgeLeft || handle == EdgeRight) halfH = start.frame.halfHeight();
    }
    p.frame.axisU = core::Vec2(halfW, 0.0);
    p.frame.axisV = core::Vec2(0.0, halfH);
    p.frame.centre = core::Vec2((fixed.x + pointer.x) * 0.5, (fixed.y + pointer.y) * 0.5);
    if (start.form == ShapeForm::Circle) {
        // A circle handle keeps it circular; the caller converts to Oval only if
        // it deliberately asks for a stretch.
        const double s = std::max(std::fabs(halfW), std::fabs(halfH));
        p.frame.axisU = core::Vec2(s, 0.0);
        p.frame.axisV = core::Vec2(0.0, s);
    }
    if (start.form == ShapeForm::Line) {
        // Dragging a line handle moves an END point: the line is the diagonal of
        // the rectangle whose opposite corner is the FIXED end.
        lineFrameFromEndpoints(fixed, pointer, start.style.width, &p.frame);
    }
    return p;
}

void ShapeHandleGesture::cancel() {
    active = false;
    object = core::kInvalidObjectId;
    handle = None;
}

void ShapeHandleGesture::buildPreview(core::PreviewScene* scene,
                                      const core::ObjectRef& subject) const {
    if (!scene || !active) return;
    buildShapePreview(scene, current, subject);
}

bool BracketHandleGesture::begin(const core::Document& document, core::ObjectId id, Handle h,
                                 const core::Vec2& pointer, double tolerance) {
    BracketParams p;
    bool used = false;
    if (!readBracket(document, id, &p, &used).ok()) return false;
    start = p;
    current = p;
    object = id;
    handle = h;
    active = true;
    if (h == Start) {
        if (core::length(p.frame.pointAt(-1.0, 1.0) - pointer) > tolerance) { active = false; return false; }
    } else if (h == End) {
        if (core::length(p.frame.pointAt(-1.0, -1.0) - pointer) > tolerance) { active = false; return false; }
    }
    return true;
}

const BracketParams& BracketHandleGesture::update(const core::Vec2& pointer) {
    current = resolve(pointer);
    return current;
}

BracketParams BracketHandleGesture::resolve(const core::Vec2& pointer) const {
    BracketParams p = start;
    if (handle == Start) {
        // Keep the opposite end fixed; the span follows the pointer.
        const core::Vec2 fixedPt = start.frame.pointAt(-1.0, -1.0);
        const double half = std::fabs(pointer.y - fixedPt.y) * 0.5;
        p.frame.axisV = core::Vec2(0.0, half > 1.0 ? half : 1.0);
        p.frame.centre = core::Vec2(start.frame.centre.x, (fixedPt.y + pointer.y) * 0.5);
    } else if (handle == End) {
        const core::Vec2 fixedPt = start.frame.pointAt(-1.0, 1.0);
        const double half = std::fabs(pointer.y - fixedPt.y) * 0.5;
        p.frame.axisV = core::Vec2(0.0, half > 1.0 ? half : 1.0);
        p.frame.centre = core::Vec2(start.frame.centre.x, (fixedPt.y + pointer.y) * 0.5);
    } else if (handle == Left || handle == Right) {
        const double half = std::fabs(pointer.x - start.frame.centre.x);
        p.frame.axisU = core::Vec2(half > 1.0 ? half : 1.0, 0.0);
    } else if (handle == Label0) {
        // Converting an absolute drag into frame coordinates is what keeps the
        // label attached: only (u,v) is stored.
        const double w = start.frame.halfWidth();
        const double h = start.frame.halfHeight();
        if (w > 1e-9 && h > 1e-9 && !start.labels.empty()) {
            const double u = (pointer.x - start.frame.centre.x) / w;
            const double v = (pointer.y - start.frame.centre.y) / h;
            p.labels[0].anchor = BracketLabelAnchor(clampd(u, -3.0, 3.0), clampd(v, -3.0, 3.0));
        }
    }
    return p;
}

void BracketHandleGesture::cancel() {
    active = false;
    object = core::kInvalidObjectId;
    handle = None;
}

void BracketHandleGesture::buildPreview(core::PreviewScene* scene,
                                        const core::ObjectRef& subject) const {
    if (!scene || !active) return;
    buildBracketPreview(scene, current, 10.0, subject);
}

bool TableBorderGesture::begin(const core::Document& document, core::ObjectId id,
                               const core::Vec2& pointer, double tolerance) {
    TableData d;
    bool used = false;
    if (!readTable(document, id, &d, &used).ok()) return false;
    uint32_t r = 0, c = 0;
    int vBorder = -1, hBorder = -1;
    if (!d.grid.cellAt(pointer, &r, &c, tolerance, &vBorder, &hBorder)) return false;
    if (vBorder < 0 && hBorder < 0) return false;
    table = id;
    columnIndex = vBorder;
    rowIndex = hBorder;
    startSize = vBorder >= 0 ? d.grid.columnWidths[static_cast<size_t>(vBorder)]
                             : d.grid.rowHeights[static_cast<size_t>(hBorder)];
    startPointer = pointer;
    active = true;
    return true;
}

double TableBorderGesture::resolveDelta(const core::Vec2& pointer) const {
    if (!active) return 0.0;
    if (columnIndex >= 0) return pointer.x - startPointer.x;
    if (rowIndex >= 0)    return pointer.y - startPointer.y;
    return 0.0;
}

void TableBorderGesture::cancel() {
    active = false;
    table = core::kInvalidObjectId;
    columnIndex = -1;
    rowIndex = -1;
}

void TableBorderGesture::buildPreview(core::PreviewScene* scene, double pointerX,
                                      double pointerY) const {
    if (!scene || !active) return;
    core::PreviewItem it;
    it.primitive = core::PreviewPrimitive::Segment;
    it.points.push_back(core::Vec2(pointerX, -1e6));
    it.points.push_back(core::Vec2(pointerX,  1e6));
    if (rowIndex >= 0) {
        it.points.clear();
        it.points.push_back(core::Vec2(-1e6, pointerY));
        it.points.push_back(core::Vec2( 1e6, pointerY));
    }
    it.style.width = 0.6;
    scene->add(it);
}

// ===========================================================================
// Registry wiring
// ===========================================================================
size_t registerGraphicsPresets(core::ToolRegistry& registry, PlaceGraphicsTool* graphics,
                               PlaceTableTool* table, std::string* firstError) {
    if (firstError) firstError->clear();
    // ONE instance per behavior: PlaceSingle is owned by the dispatcher, so the
    // same tool serves a shape preset and a bracket preset and still creates the
    // right object class.
    if (graphics) registry.registerTool(core::ToolBehavior::PlaceSingle, graphics);
    if (table)    registry.registerTool(core::ToolBehavior::Table, table);

    size_t bound = 0;
    for (size_t i = 0; i < shapePresetCount(); ++i) {
        const ShapePresetSpec& s = shapePresets()[i];
        core::ToolPreset preset;
        preset.id = s.toolId;
        preset.behavior = core::ToolBehavior::PlaceSingle;
        preset.target = core::PresetTarget::Arrow;   // the generic annotation slab
        preset.displayName = s.displayName;
        preset.category = "几何图形";
        preset.sourceCommand = s.sourceCommand;
        preset.invalidation = core::kInvalidateStructure | core::kInvalidateGeometry;
        preset.implemented = s.implemented;
        uint32_t id = core::kPresetNone;
        core::Error e = registry.registerPreset(preset, &id);
        if (!e.ok()) {
            if (firstError && firstError->empty()) *firstError = std::string(s.toolId) + ": " + e.message;
            continue;
        }
        core::Error b = registry.bindPlatformCommand(s.toolId, core::platform_command::ToolBand + id);
        if (!b.ok()) {
            if (firstError && firstError->empty()) *firstError = std::string(s.toolId) + ": " + b.message;
            continue;
        }
        ++bound;
    }
    for (size_t i = 0; i < bracketPresetCount(); ++i) {
        const BracketPresetSpec& s = bracketPresets()[i];
        core::ToolPreset preset;
        preset.id = s.toolId;
        preset.behavior = core::ToolBehavior::PlaceSingle;
        preset.target = core::PresetTarget::Bracket;
        preset.displayName = s.displayName;
        preset.category = "括号与匕首标记";
        preset.sourceCommand = s.sourceCommand;
        preset.invalidation = core::kInvalidateStructure | core::kInvalidateGeometry;
        preset.implemented = s.implemented;
        uint32_t id = core::kPresetNone;
        core::Error e = registry.registerPreset(preset, &id);
        if (!e.ok()) {
            if (firstError && firstError->empty()) *firstError = std::string(s.toolId) + ": " + e.message;
            continue;
        }
        core::Error b = registry.bindPlatformCommand(s.toolId, core::platform_command::ToolBand + id);
        if (!b.ok()) {
            if (firstError && firstError->empty()) *firstError = std::string(s.toolId) + ": " + b.message;
            continue;
        }
        ++bound;
    }
    // The single table command from tools.csv row 25.
    {
        core::ToolPreset preset;
        preset.id = "r62.tool.graphics.table.place";
        preset.behavior = core::ToolBehavior::Table;
        preset.target = core::PresetTarget::Table;
        preset.displayName = "Table";
        preset.category = "表格";
        preset.sourceCommand = "chooseTableTool";
        preset.invalidation = core::kInvalidateStructure | core::kInvalidateGeometry;
        preset.implemented = true;
        uint32_t id = core::kPresetNone;
        core::Error e = registry.registerPreset(preset, &id);
        if (e.ok()) {
            core::Error b = registry.bindPlatformCommand(preset.id,
                                                         core::platform_command::ToolBand + id);
            if (b.ok()) ++bound;
            else if (firstError && firstError->empty()) *firstError = preset.id + ": " + b.message;
        } else if (firstError && firstError->empty()) {
            *firstError = preset.id + ": " + e.message;
        }
    }
    return bound;
}

} // namespace graphics
} // namespace r62
