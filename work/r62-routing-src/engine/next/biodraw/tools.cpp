// =============================================================================
// re_round62_viewer/next/biodraw/tools.cpp
//
// T14 - placement tool, handles and preset registration.
// =============================================================================

#include "next/biodraw/tools.hpp"
#include "next/biodraw/plasmid.hpp"

#include <cmath>

namespace r62 {
namespace biodraw {

namespace tool_ids {
const char* const kPlaceBioShape = "r62.tool.biodraw.place";
const char* const kEditBioShape  = "r62.tool.biodraw.edit";
}

const char* bioHandleName(BioHandleKind kind) {
    switch (kind) {
        case BioHandleKind::ExtentU:      return "ExtentU";
        case BioHandleKind::ExtentV:      return "ExtentV";
        case BioHandleKind::Centre:       return "Centre";
        case BioHandleKind::UnitSpacing:  return "UnitSpacing";
        case BioHandleKind::UnitSize:     return "UnitSize";
        case BioHandleKind::PlasmidStart: return "PlasmidStart";
        case BioHandleKind::PlasmidEnd:   return "PlasmidEnd";
    }
    return "Unknown";
}

// ---------------------------------------------------------------------------
// Placement tool
// ---------------------------------------------------------------------------
BioDrawPlaceTool::BioDrawPlaceTool()
    : id_(tool_ids::kPlaceBioShape), activePreset_(kNoPreset), dragging_(false),
      beginPoint_(0.0, 0.0) {}

bool BioDrawPlaceTool::activatePreset(uint32_t presetId) {
    if (!bioPresetById(presetId)) return false;
    activePreset_ = presetId;
    dragging_ = false;
    return true;
}

void BioDrawPlaceTool::publishPreview(core::ToolHost& host, const core::Vec2& begin,
                                      const core::Vec2& current) {
    // The preview is built by the SAME resolver the commit uses.
    preview_ = resolveBioPreset(activePreset_, begin, current, host.document().defaultLineWidth());
    core::PreviewScene& scene = host.preview();
    scene.clear();
    // The preview is the figure's OUTLINE plus its bounding frame, drawn from the
    // geometry that would actually be created.
    const BioGeometry geometry = buildGlyphGeometry(preview_, host.document().defaultLineWidth());
    for (size_t i = 0; i < geometry.elements.size(); ++i) {
        for (size_t k = 0; k < geometry.elements[i].contours.size(); ++k) {
            const BioContour& contour = geometry.elements[i].contours[k];
            if (contour.points.size() < 2) continue;
            core::PreviewItem item;
            item.primitive = contour.closed ? core::PreviewPrimitive::Polygon
                                            : core::PreviewPrimitive::Polyline;
            item.points = contour.points;
            item.style = preview_.style;
            item.fillRGBA = resolveSubElementColor(preview_, contour.key);
            item.fillEnabled = contour.filled ? 1 : 0;
            scene.add(item);
        }
    }
    // A bounding frame, so an empty-looking figure still shows its extent.
    core::PreviewItem frame;
    frame.primitive = core::PreviewPrimitive::Rect;
    core::Vec2 corners[2];
    frame.points.push_back(geometry.hitBounds.min);
    frame.points.push_back(geometry.hitBounds.max);
    (void)corners;
    scene.add(frame);
}

core::CommandResult BioDrawPlaceTool::beginGesture(core::ToolHost& host,
                                                   const core::PointerState& pointer,
                                                   core::HitContext& hits) {
    (void)hits;
    if (!bioPresetById(activePreset_)) {
        host.setStatus("BioDraw: no preset is active");
        return core::CommandResult::fail(core::ErrorCode::InvalidArgument,
                                         "no active BioDraw preset");
    }
    dragging_ = true;
    beginPoint_ = pointer.position;
    publishPreview(host, beginPoint_, pointer.position);
    return core::CommandResult::noChange();
}

core::CommandResult BioDrawPlaceTool::updateGesture(core::ToolHost& host,
                                                    const core::PointerState& pointer,
                                                    core::HitContext& hits) {
    (void)hits;
    if (!dragging_) return core::CommandResult::noChange();
    publishPreview(host, beginPoint_, pointer.position);
    // The document is NOT touched during a drag: only the overlay changed.
    return core::CommandResult::noChange();
}

core::CommandResult BioDrawPlaceTool::endGesture(core::ToolHost& host,
                                                 const core::PointerState& pointer,
                                                 core::HitContext& hits) {
    (void)hits;
    if (!dragging_) return core::CommandResult::fail(core::ErrorCode::InvalidArgument,
                                                     "no gesture is in progress");
    dragging_ = false;
    // Re-resolve from the SAME begin point and the release point, then COMMIT
    // those parameters. The preview shown on the last update used the same
    // resolver, so the committed figure is what the user saw.
    const BioShapeParams finalParams =
        resolveBioPreset(activePreset_, beginPoint_, pointer.position,
                         host.document().defaultLineWidth());
    host.preview().clear();

    core::ObjectId created = core::kInvalidObjectId;
    const core::Error error = host.submit(
        addBioShapeCommand(finalParams, host.document().defaultLineWidth(), &created),
        "biodraw.place");
    if (!error.ok()) return core::CommandResult::fail(error.code, error.message);
    if (created != core::kInvalidObjectId) {
        host.setSelection(std::vector<core::ObjectRef>(1, core::ObjectRef(core::ObjectKind::BioShape, created)));
        host.setStatus("BioDraw: placed a figure");
    }
    return core::CommandResult::success(core::kInvalidateStructure |
                                        core::kInvalidateGeometry |
                                        core::kInvalidateSelection);
}

core::CommandResult BioDrawPlaceTool::cancelGesture(core::ToolHost& host) {
    dragging_ = false;
    host.preview().clear();
    return core::CommandResult::noChange();
}

bool BioDrawPlaceTool::handleKey(core::ToolHost& host, uint32_t virtualKey,
                                 uint32_t modifiers) {
    (void)modifiers;
    const uint32_t kEscape = 0x1B;
    if (virtualKey == kEscape) {
        if (dragging_) { cancelGesture(host); return true; }
        return false;
    }
    // While a figure is selected, +/- step its FIRST component's count. This is
    // the keyboard form of the 'repeated object count is editable data'
    // requirement; the handle drag and the command are the other two forms.
    const uint32_t kPlus  = 0xBB;   // VK_OEM_PLUS
    const uint32_t kMinus = 0xBD;   // VK_OEM_MINUS
    if (virtualKey != kPlus && virtualKey != kMinus) return false;
    const std::vector<core::ObjectRef>& selection = host.selection();
    if (selection.size() != 1 || selection[0].kind != core::ObjectKind::BioShape) return false;
    BioShapeParams params;
    if (!readBioShape(host.document(), selection[0].id, &params, 0).ok()) return false;
    if (params.components.empty()) {
        host.setStatus("BioDraw: this figure has no repeated units");
        return true;
    }
    const int step = (virtualKey == kPlus) ? 1 : -1;
    const int next = params.components[0].count + step;
    const core::Error error = host.submit(
        setComponentCountCommand(selection[0].id, 0, next), "biodraw.count");
    if (!error.ok()) {
        host.setStatus(std::string("BioDraw: ") + error.message);
        return true;
    }
    host.setStatus("BioDraw: unit count changed");
    return true;
}


// ---------------------------------------------------------------------------
// Handles
// ---------------------------------------------------------------------------
// Every handle is placed ON the geometry it edits: the extent handles sit on the
// frame corners, the spacing handle sits at the first unit, and a plasmid range
// handle sits at the feature's start/end angle. A handle that floated somewhere
// else would move a parameter the user cannot associate with what they grabbed.
std::vector<BioHandle> bioShapeHandles(const BioShapeParams& params,
                                       double documentLineWidth) {
    std::vector<BioHandle> handles;
    core::Vec2 axisU, axisV;
    frameAxes(params, &axisU, &axisV);

    BioHandle centre;
    centre.kind = BioHandleKind::Centre;
    centre.position = params.centre;
    handles.push_back(centre);

    BioHandle extentU;
    extentU.kind = BioHandleKind::ExtentU;
    extentU.position = core::Vec2(params.centre.x + axisU.x, params.centre.y + axisU.y);
    handles.push_back(extentU);

    BioHandle extentV;
    extentV.kind = BioHandleKind::ExtentV;
    extentV.position = core::Vec2(params.centre.x + axisV.x, params.centre.y + axisV.y);
    handles.push_back(extentV);

    // Per-component handles, one spacing and one size handle each. Their
    // positions come from the component expansion, so they track the layout.
    const std::vector<HostPath> paths = glyphHostPaths(params);
    BioShapeParams normalised = normalizeComponents(params, 0);
    for (size_t i = 0; i < normalised.components.size() && i < paths.size(); ++i) {
        const std::vector<PlacedUnit> units = expandUnits(normalised, i, paths[i]);
        core::Vec2 anchor = params.centre;
        if (!units.empty()) anchor = units[0].centre;
        BioHandle spacing;
        spacing.kind = BioHandleKind::UnitSpacing;
        spacing.index = i;
        spacing.position = anchor;
        handles.push_back(spacing);

        BioHandle size;
        size.kind = BioHandleKind::UnitSize;
        size.index = i;
        if (!units.empty())
            size.position = core::Vec2(units[0].centre.x + units[0].axisU.x,
                                       units[0].centre.y + units[0].axisU.y);
        else
            size.position = anchor;
        handles.push_back(size);
    }

    // Plasmid range handles, at the feature start/end angles.
    if (params.isPlasmid() && params.plasmid.baseCount > 0) {
        const double r = core::length(axisU);
        for (size_t i = 0; i < params.plasmid.features.size(); ++i) {
            const FeatureAngle span = featureAngleSpan(params.plasmid, params.plasmid.features[i]);
            const double a0 = span.startDeg * 3.14159265358979323846 / 180.0;
            const double a1 = (span.startDeg + span.spanDeg) * 3.14159265358979323846 / 180.0;
            BioHandle start;
            start.kind = BioHandleKind::PlasmidStart;
            start.index = i;
            start.position = core::Vec2(params.centre.x + r * std::cos(a0),
                                        params.centre.y - r * std::sin(a0));
            handles.push_back(start);
            BioHandle end;
            end.kind = BioHandleKind::PlasmidEnd;
            end.index = i;
            end.position = core::Vec2(params.centre.x + r * std::cos(a1),
                                      params.centre.y - r * std::sin(a1));
            handles.push_back(end);
        }
    }
    (void)documentLineWidth;
    return handles;
}

bool nearestBioHandle(const std::vector<BioHandle>& handles, const core::Vec2& point,
                      double tolerance, size_t* index) {
    double best = tolerance;
    bool found = false;
    for (size_t i = 0; i < handles.size(); ++i) {
        const double dx = handles[i].position.x - point.x;
        const double dy = handles[i].position.y - point.y;
        const double d = std::sqrt(dx * dx + dy * dy);
        if (d <= best) { best = d; found = true; if (index) *index = i; }
    }
    return found;
}

// ---------------------------------------------------------------------------
// Handle gesture
// ---------------------------------------------------------------------------
BioHandleGesture::BioHandleGesture()
    : active_(false), handleIndex_(0), grabPoint_(0.0, 0.0), handle_(0) {}

bool BioHandleGesture::begin(const BioShapeParams& original, size_t handleIndex,
                             const core::Vec2& point) {
    handles_ = bioShapeHandles(original, original.style.width > 0.0 ? original.style.width : 0.6);
    if (handleIndex >= handles_.size()) return false;
    original_ = original;
    handleIndex_ = handleIndex;
    grabPoint_ = point;
    handle_ = &handles_[handleIndex_];
    active_ = true;
    return true;
}

BioShapeParams BioHandleGesture::solve(const core::Vec2& current,
                                       double documentLineWidth) const {
    // Every solve starts from the ORIGINAL parameters, never from the previous
    // solve. Accumulating deltas across a long drag would drift, and a drift is
    // indistinguishable from a real edit once it is committed.
    BioShapeParams out = original_;
    if (!active_ || !handle_) return out;
    const core::Vec2 delta(current.x - grabPoint_.x, current.y - grabPoint_.y);
    switch (handle_->kind) {
        case BioHandleKind::Centre:
            out.centre.x += delta.x;
            out.centre.y += delta.y;
            break;
        case BioHandleKind::ExtentU:
            out.axisU.x += delta.x;
            out.axisU.y += delta.y;
            break;
        case BioHandleKind::ExtentV:
            out.axisV.x += delta.x;
            out.axisV.y += delta.y;
            break;
        case BioHandleKind::UnitSpacing: {
            // Dragging a unit along its host path changes SPACING, and the count is
            // left alone: dragging must not silently delete units.
            if (handle_->index < out.components.size()) {
                const double magnitude = std::sqrt(delta.x * delta.x + delta.y * delta.y);
                const double sign = (delta.x + delta.y) >= 0.0 ? 1.0 : -1.0;
                double spacing = out.components[handle_->index].spacing + sign * magnitude;
                if (spacing < 0.1) spacing = 0.1;
                out.components[handle_->index].spacing = spacing;
            }
            break;
        }
        case BioHandleKind::UnitSize: {
            if (handle_->index < out.components.size()) {
                const double magnitude = std::sqrt(delta.x * delta.x + delta.y * delta.y);
                const double sign = (delta.x + delta.y) >= 0.0 ? 1.0 : -1.0;
                double size = out.components[handle_->index].unitSize + sign * magnitude * 0.25;
                if (size < 0.05) size = 0.05;
                out.components[handle_->index].unitSize = size;
            }
            break;
        }
        case BioHandleKind::PlasmidStart:
        case BioHandleKind::PlasmidEnd: {
            // Convert the pointer back to a BASE INDEX and edit the RANGE. The
            // angle is never stored, so a drag cannot desynchronise from the bases.
            if (!out.plasmid.valid() || handle_->index >= out.plasmid.features.size()) break;
            const double dx = current.x - out.centre.x;
            const double dy = current.y - out.centre.y;
            double angleDeg = std::atan2(-dy, dx) * 180.0 / 3.14159265358979323846;
            double relative = angleDeg - out.plasmid.startAngleDeg;
            if (!out.plasmid.clockwise) relative = -relative;
            while (relative < 0.0) relative += 360.0;
            while (relative >= 360.0) relative -= 360.0;
            int base = static_cast<int>(relative / 360.0 * out.plasmid.baseCount + 0.5);
            if (base < 0) base = 0;
            if (base >= out.plasmid.baseCount) base = out.plasmid.baseCount - 1;
            PlasmidFeature& f = out.plasmid.features[handle_->index];
            if (handle_->kind == BioHandleKind::PlasmidStart) {
                const int span = f.end >= f.start ? f.end - f.start : 1;
                f.start = base;
                int end = base + span;
                if (end > out.plasmid.baseCount) end -= out.plasmid.baseCount;
                f.end = end;
            } else {
                f.end = base;
            }
            break;
        }
    }
    (void)documentLineWidth;
    return out;
}

void BioHandleGesture::cancel() {
    active_ = false;
    handle_ = 0;
}


// ---------------------------------------------------------------------------
// Edit tool helpers (NOT registered: see tools.hpp)
// ---------------------------------------------------------------------------
core::Command BioEditTool::setCount(const core::Document& document, core::ObjectId id,
                                    size_t componentIndex, int count) const {
    (void)document;
    return setComponentCountCommand(id, componentIndex, count);
}

core::Command BioEditTool::setSpacing(const core::Document& document, core::ObjectId id,
                                      size_t componentIndex, double spacing) const {
    (void)document;
    return setComponentSpacingCommand(id, componentIndex, spacing, true);
}

core::Command BioEditTool::setUnitSize(const core::Document& document, core::ObjectId id,
                                       size_t componentIndex, double unitSize) const {
    (void)document;
    return setComponentUnitSizeCommand(id, componentIndex, unitSize);
}

core::Command BioEditTool::setPlasmidBaseCount(const core::Document& document,
                                               core::ObjectId id, int baseCount) const {
    BioShapeParams params;
    core::Command command;
    command = setPlasmidDataCommand(id, PlasmidData());
    if (!readBioShape(document, id, &params, 0).ok()) return command;
    if (!params.plasmid.valid()) return command;
    PlasmidData data = params.plasmid;
    data.baseCount = baseCount;
    // Every feature is RE-CLAMPED into the new circle rather than dropped: a map
    // whose base count is edited must not lose its markers.
    for (size_t i = 0; i < data.features.size(); ++i) {
        if (data.features[i].start >= baseCount) data.features[i].start = baseCount - 1;
        if (data.features[i].end > baseCount) data.features[i].end = baseCount;
    }
    return setPlasmidDataCommand(id, data);
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------
size_t registerBioPresets(core::ToolRegistry& registry, BioDrawPlaceTool* placeTool,
                          std::string* firstError) {
    if (firstError) firstError->clear();
    if (!placeTool) {
        if (firstError) *firstError = "null place tool";
        return 0;
    }
    // ONE instance for the PlaceSingle behavior. Registering a tool per preset
    // would leave only the last one reachable, because ToolRegistry keys tools by
    // behavior -- every button would then place the same figure.
    const core::Error toolError = registry.registerTool(core::ToolBehavior::PlaceSingle, placeTool);
    if (!toolError.ok() && firstError) *firstError = toolError.message;

    size_t bound = 0;
    for (size_t i = 0; i < bioPresetCount(); ++i) {
        const BioPresetSpec* spec = bioPresetById(static_cast<uint32_t>(i));
        if (!spec) continue;
        core::ToolPreset preset;
        preset.id = spec->toolId;
        preset.behavior = core::ToolBehavior::PlaceSingle;
        preset.target = core::PresetTarget::BioShape;
        preset.displayName = spec->displayName;
        preset.category = "BioDraw 生物图示";
        preset.sourceCommand = spec->sourceCommand;
        preset.invalidation = core::kInvalidateStructure | core::kInvalidateGeometry;
        preset.implemented = true;
        uint32_t presetId = core::kPresetNone;
        const core::Error error = registry.registerPreset(preset, &presetId);
        if (!error.ok()) {
            if (firstError && firstError->empty()) *firstError = error.message;
            continue;
        }
        const uint32_t command = core::platform_command::ToolBand + static_cast<uint32_t>(presetId);
        const core::Error bindError = registry.bindPlatformCommand(spec->toolId, command);
        if (!bindError.ok()) {
            if (firstError && firstError->empty()) *firstError = bindError.message;
            continue;
        }
        ++bound;
    }
    return bound;
}

} // namespace biodraw
} // namespace r62
