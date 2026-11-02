#include "next/experimental/tools.hpp"

#include "next/experimental/render_bridge.hpp"

#include <cmath>

namespace r62 {
namespace experimental {

namespace tool_ids {
const char* const kPlacePlate = "r62.tool.experimental.plate";
} // namespace tool_ids

namespace {

void addSegmentPreview(core::PreviewScene* scene, const core::Vec2& a, const core::Vec2& b,
                       uint32_t color, double width) {
    core::PreviewItem item;
    item.primitive = core::PreviewPrimitive::Segment;
    item.points.push_back(a);
    item.points.push_back(b);
    item.style.colorRGBA = color;
    item.style.width = width;
    scene->add(item);
}

void addPolygonPreview(core::PreviewScene* scene, const std::vector<core::Vec2>& points,
                       uint32_t stroke, uint32_t fill, bool fillEnabled, double width) {
    core::PreviewItem item;
    item.primitive = core::PreviewPrimitive::Polygon;
    item.points = points;
    item.style.colorRGBA = stroke;
    item.style.width = width;
    item.fillRGBA = fill;
    item.fillEnabled = fillEnabled ? 1 : 0;
    scene->add(item);
}

void buildBoardPreview(core::PreviewScene* scene, const PlateFigure& figure,
                       const PlateFrame& frame, const core::ObjectRef& subject) {
    core::Vec2 corners[4];
    boardCornerPolygon(frame, corners);
    std::vector<core::Vec2> board(corners, corners + 4);
    addPolygonPreview(scene, board, 0x303030FFu, figure.plateFillRGBA, true, 1.0);
    if (figure.kind == FigureKind::TlcPlate) {
        if (figure.originDefined)
            addSegmentPreview(scene, frame.pointAt(-1.0, figure.originV),
                              frame.pointAt(1.0, figure.originV), 0x404040FFu, 0.8);
        if (figure.frontDefined)
            addSegmentPreview(scene, frame.pointAt(-1.0, figure.frontV),
                              frame.pointAt(1.0, figure.frontV), 0x404040FFu, 0.8);
    }
    for (size_t i = 0; i < figure.lanes.size(); ++i) {
        const Lane& lane = figure.lanes[i];
        const double half = lane.width * 0.5;
        addSegmentPreview(scene, frame.pointAt(lane.u - half, -figure.laneMarginV),
                          frame.pointAt(lane.u - half, figure.laneMarginV), 0x808080C0u, 0.5);
        addSegmentPreview(scene, frame.pointAt(lane.u + half, -figure.laneMarginV),
                          frame.pointAt(lane.u + half, figure.laneMarginV), 0x808080C0u, 0.5);
    }
    for (size_t i = 0; i < figure.spots.size(); ++i) {
        const Spot& spot = figure.spots[i];
        addPolygonPreview(scene, ellipsePolygon(frame, spot.u, spot.v, spot.radiusU, spot.radiusV, 20),
                          0x00000000u, spot.fillRGBA, true, 0.5);
    }
    for (size_t i = 0; i < figure.bands.size(); ++i) {
        const Band& band = figure.bands[i];
        const Lane* lane = findLane(figure, band.laneId);
        if (!lane) continue;
        addPolygonPreview(scene, bandPolygon(frame, *lane, band), 0x00000000u, band.fillRGBA, true, 0.5);
    }
    (void)subject;
}

} // namespace

// ---------------------------------------------------------------------------
// PlacePlateTool
// ---------------------------------------------------------------------------
PlacePlateTool::PlacePlateTool()
    : id_(tool_ids::kPlacePlate), presetId_(0), laneCount_(4), dragging_(false),
      origin_(0, 0), current_(0, 0), preview_(), previewFrame_() {}

const core::ToolId& PlacePlateTool::id() const { return id_; }

bool PlacePlateTool::activatePreset(uint32_t presetId) {
    const PlatePresetSpec* spec = platePresetById(presetId);
    if (!spec) return false;
    presetId_ = presetId;
    laneCount_ = spec->defaultLaneCount;
    preview_ = makeDefaultFigure(spec->kind, laneCount_);
    previewFrame_ = PlateFrame();
    return true;
}

FigureKind PlacePlateTool::figureKind() const {
    const PlatePresetSpec* spec = platePresetById(presetId_);
    return spec ? spec->kind : FigureKind::TlcPlate;
}

core::CommandResult PlacePlateTool::beginGesture(core::ToolHost& host,
                                                 const core::PointerState& pointer,
                                                 core::HitContext& hits) {
    (void)hits;
    origin_ = pointer.position;
    current_ = pointer.position;
    dragging_ = true;
    previewFrame_ = PlateFrame(origin_, core::Vec2(0.0, 0.0), core::Vec2(0.0, 0.0));
    preview_ = makeDefaultFigure(figureKind(), laneCount_);
    host.preview().clear();
    buildPreview(&host.preview(), core::ObjectRef());
    return core::CommandResult::noChange();
}

core::CommandResult PlacePlateTool::updateGesture(core::ToolHost& host,
                                                  const core::PointerState& pointer,
                                                  core::HitContext& hits) {
    (void)hits;
    if (!dragging_) return core::CommandResult::noChange();
    current_ = pointer.position;
    core::Vec2 adjusted = current_;
    if ((pointer.modifiers & core::kModShift) != 0u) {
        const double dx = current_.x - origin_.x;
        const double dy = current_.y - origin_.y;
        const double side = std::fabs(dx) > std::fabs(dy) ? dx : dy;
        adjusted = core::Vec2(origin_.x + (dx < 0.0 ? -std::fabs(side) : std::fabs(side)),
                              origin_.y + (dy < 0.0 ? -std::fabs(side) : std::fabs(side)));
    }
    previewFrame_ = frameFromDrag(origin_, adjusted);
    host.preview().clear();
    buildPreview(&host.preview(), core::ObjectRef());
    return core::CommandResult::noChange();
}

core::CommandResult PlacePlateTool::endGesture(core::ToolHost& host,
                                               const core::PointerState& pointer,
                                               core::HitContext& hits) {
    (void)hits;
    if (!dragging_) return core::CommandResult::noChange();
    dragging_ = false;
    current_ = pointer.position;
    PlateFrame frame = frameFromDrag(origin_, current_);
    if (frame.degenerate()) {
        // A plain click places a default board centred on the click, so the tool
        // never produces a zero-size object that no later gesture can grab.
        frame = PlateFrame(origin_, core::Vec2(30.0, 0.0), core::Vec2(0.0, 20.0));
    }
    const uint32_t laneCount = laneCount_;
    const FigureKind kind = figureKind();
    EditOutcome outcome;
    core::Error e = host.submit(addFigureCommand(kind, frame, laneCount, &outcome),
                                "experimental.placeFigure");
    host.preview().clear();
    if (!e.ok()) {
        host.setStatus(std::string("figure refused: ") + e.message);
        return core::CommandResult::fail(e.code, e.message);
    }
    host.setStatus(std::string("placed ") + figureKindName(kind));
    return core::CommandResult::success(core::kInvalidateStructure | core::kInvalidateGeometry);
}

core::CommandResult PlacePlateTool::cancelGesture(core::ToolHost& host) {
    dragging_ = false;
    host.preview().clear();
    return core::CommandResult::noChange();
}

bool PlacePlateTool::handleKey(core::ToolHost& host, uint32_t virtualKey, uint32_t modifiers) {
    (void)host; (void)virtualKey; (void)modifiers;
    return false;
}

void PlacePlateTool::buildPreview(core::PreviewScene* scene, const core::ObjectRef& subject) const {
    if (!scene) return;
    buildBoardPreview(scene, preview_, previewFrame_, subject);
}

// ---------------------------------------------------------------------------
// PlateEditGesture
// ---------------------------------------------------------------------------
PlateEditGesture::PlateEditGesture()
    : object(core::kInvalidObjectId), mode(Mode::None), active(false),
      handle(BoardHandle::None), startFrame(), startFigure(), currentFrame(), currentFigure(),
      startPointer(), grabU(0.0), grabV(0.0), changed(false) {}

bool PlateEditGesture::begin(const core::Document& document, core::ObjectId figureId,
                             const core::Vec2& pointer, double tolerance) {
    cancel();
    PlateFigure figure;
    PlateFrame frame;
    if (!readFigureAndFrame(document, figureId, &figure, &frame, 0).ok()) return false;
    if (!boardHitTest(figure, frame, pointer, tolerance)) return false;
    object = figureId;
    startFigure = figure;
    startFrame = frame;
    currentFigure = figure;
    currentFrame = frame;
    startPointer = pointer;
    changed = false;

    const BoardHandle grip = pickBoardHandle(frame, pointer, tolerance);
    if (grip != BoardHandle::None) {
        mode = Mode::ResizeBoard;
        handle = grip;
        active = true;
        return true;
    }
    element = pickElement(figure, frame, pointer, tolerance);
    double pu = 0.0, pv = 0.0;
    frame.relativeOf(pointer, &pu, &pv);
    switch (element.kind) {
    case ElementKind::Spot: {
        const Spot* spot = findSpot(figure, element.id);
        if (!spot) break;
        mode = Mode::MoveElement;
        grabU = spot->u - pu;
        grabV = spot->v - pv;
        active = true;
        return true;
    }
    case ElementKind::Band: {
        const Band* band = findBand(figure, element.id);
        if (!band) break;
        mode = Mode::MoveElement;
        grabU = 0.0;
        grabV = band->v - pv;
        active = true;
        return true;
    }
    case ElementKind::Lane: {
        const Lane* lane = findLane(figure, element.id);
        if (!lane) break;
        mode = Mode::MoveElement;
        grabU = lane->u - pu;
        grabV = 0.0;
        active = true;
        return true;
    }
    case ElementKind::Body:
    case ElementKind::OriginLine:
    case ElementKind::FrontLine:
    default:
        // Inside the board but not on any element: the WHOLE figure moves. The
        // frame changes; no relative coordinate does.
        mode = Mode::MoveBody;
        active = true;
        return true;
    }
    return false;
}

bool PlateEditGesture::update(const core::Vec2& pointer, bool constrainAxis) {
    if (!active) return false;
    switch (mode) {
    case Mode::ResizeBoard: {
        const PlateFrame resolved = resizeFrame(startFrame, handle, pointer);
        if (resolved.degenerate()) return false;
        const bool did = !(resolved.centre == currentFrame.centre) ||
                          !(resolved.axisU == currentFrame.axisU) ||
                          !(resolved.axisV == currentFrame.axisV);
        currentFrame = resolved;
        currentFigure = startFigure;
        changed = changed || did;
        return did;
    }
    case Mode::MoveBody: {
        core::Vec2 delta(pointer.x - startPointer.x, pointer.y - startPointer.y);
        if (constrainAxis) {
            if (std::fabs(delta.x) >= std::fabs(delta.y)) delta.y = 0.0;
            else delta.x = 0.0;
        }
        currentFrame = PlateFrame(core::Vec2(startFrame.centre.x + delta.x,
                                             startFrame.centre.y + delta.y),
                                  startFrame.axisU, startFrame.axisV);
        currentFigure = startFigure;
        changed = changed || delta.x != 0.0 || delta.y != 0.0;
        return true;
    }
    case Mode::MoveElement: {
        double pu = 0.0, pv = 0.0;
        if (!startFrame.relativeOf(pointer, &pu, &pv)) return false;
        PlateFigure candidate = startFigure;
        core::Error e;
        switch (element.kind) {
        case ElementKind::Spot: {
            const double u = clampDouble(pu + grabU, -1.0, 1.0);
            const double v = clampDouble(pv + grabV, -1.0, 1.0);
            e = figureSetSpotPosition(&candidate, element.id, u, v);
            break;
        }
        case ElementKind::Band: {
            const double v = clampDouble(pv + grabV, -1.0, 1.0);
            e = figureSetBandPosition(&candidate, element.id, v);
            break;
        }
        case ElementKind::Lane: {
            const double u = clampDouble(pu + grabU, -1.0, 1.0);
            e = figureSetLanePosition(&candidate, element.id, u);
            break;
        }
        default:
            return false;
        }
        if (!e.ok()) return false;
        currentFigure = candidate;
        currentFrame = startFrame;
        changed = true;
        return true;
    }
    default:
        return false;
    }
}

void PlateEditGesture::cancel() {
    object = core::kInvalidObjectId;
    mode = Mode::None;
    active = false;
    handle = BoardHandle::None;
    element = ElementRef();
    changed = false;
}

void PlateEditGesture::buildPreview(core::PreviewScene* scene, const core::ObjectRef& subject) const {
    if (!scene) return;
    if (mode == Mode::MoveElement) {
        switch (element.kind) {
        case ElementKind::Spot: {
            const Spot* spot = findSpot(currentFigure, element.id);
            if (spot)
                addPolygonPreview(scene, ellipsePolygon(currentFrame, spot->u, spot->v,
                                                        spot->radiusU, spot->radiusV, 24),
                                  0x00000000u, spot->fillRGBA, true, 0.0);
            break;
        }
        case ElementKind::Band: {
            const Band* band = findBand(currentFigure, element.id);
            const Lane* lane = band ? findLane(currentFigure, band->laneId) : 0;
            if (band && lane)
                addPolygonPreview(scene, bandPolygon(currentFrame, *lane, *band),
                                  0x00000000u, band->fillRGBA, true, 0.0);
            break;
        }
        case ElementKind::Lane: {
            const Lane* lane = findLane(currentFigure, element.id);
            if (lane) {
                const double half = lane->width * 0.5;
                addSegmentPreview(scene, currentFrame.pointAt(lane->u - half, -currentFigure.laneMarginV),
                                  currentFrame.pointAt(lane->u - half, currentFigure.laneMarginV),
                                  0x00A000FFu, 1.0);
                addSegmentPreview(scene, currentFrame.pointAt(lane->u + half, -currentFigure.laneMarginV),
                                  currentFrame.pointAt(lane->u + half, currentFigure.laneMarginV),
                                  0x00A000FFu, 1.0);
            }
            break;
        }
        default:
            break;
        }
        return;
    }
    if (mode == Mode::MoveBody || mode == Mode::ResizeBoard) {
        core::Vec2 corners[4];
        boardCornerPolygon(currentFrame, corners);
        std::vector<core::Vec2> board(corners, corners + 4);
        addPolygonPreview(scene, board, 0x0060C0FFu, 0x0060C020u, true, 1.2);
        (void)subject;
    }
}

core::Command commitGestureCommand(const PlateEditGesture& gesture, EditOutcome* out) {
    const core::ObjectId id = gesture.object;
    const PlateFrame frame = gesture.currentFrame;
    const PlateFigure figure = gesture.currentFigure;
    return makeFigureCommand("experimental.plateGesture",
        core::kInvalidateGeometry | core::kInvalidateAppearance,
        [id, frame, figure, out](core::Document& document) -> core::Error {
            core::Error e = writeFigureAndFrame(document, id, figure, frame);
            if (!e.ok()) return e;
            e = syncAnnotationTextPositions(document, id);
            if (!e.ok()) return e;
            if (out) { out->objectId = id; out->changed = true; }
            return core::Error::success();
        });
}

// ---------------------------------------------------------------------------
// PlateToolDispatcher
// ---------------------------------------------------------------------------
PlateToolDispatcher::PlateToolDispatcher()
    : id_(tool_ids::kPlacePlate), plate_(0), foreign_(0), foreignActivate_(),
      ownIds_(), plateActive_(true) {}

void PlateToolDispatcher::adoptPlate(PlacePlateTool* plate) { plate_ = plate; }
void PlateToolDispatcher::adoptForeign(core::Tool* foreign) { foreign_ = foreign; }

void PlateToolDispatcher::ownPreset(uint32_t registryPresetId, uint32_t localPresetId) {
    for (size_t i = 0; i < ownIds_.size(); ++i)
        if (ownIds_[i].registryId == registryPresetId) {
            ownIds_[i].localId = localPresetId;
            return;
        }
    OwnedPreset owned;
    owned.registryId = registryPresetId;
    owned.localId = localPresetId;
    ownIds_.push_back(owned);
}

bool PlateToolDispatcher::ownsPreset(uint32_t registryPresetId) const {
    return localPresetFor(registryPresetId) != core::kPresetNone;
}

uint32_t PlateToolDispatcher::localPresetFor(uint32_t registryPresetId) const {
    for (size_t i = 0; i < ownIds_.size(); ++i)
        if (ownIds_[i].registryId == registryPresetId) return ownIds_[i].localId;
    return core::kPresetNone;
}

bool PlateToolDispatcher::activatePreset(uint32_t registryPresetId) {
    const uint32_t local = localPresetFor(registryPresetId);
    if (local != core::kPresetNone) {
        plateActive_ = true;
        if (!plate_) return false;
        return plate_->activatePreset(local);
    }
    plateActive_ = false;
    if (foreignActivate_) return foreignActivate_(registryPresetId);
    return false;
}

const core::ToolId& PlateToolDispatcher::id() const { return id_; }

core::CommandResult PlateToolDispatcher::beginGesture(core::ToolHost& host,
                                                      const core::PointerState& pointer,
                                                      core::HitContext& hits) {
    if (plateActive_ && plate_) return plate_->beginGesture(host, pointer, hits);
    if (foreign_) return foreign_->beginGesture(host, pointer, hits);
    return core::CommandResult::noChange();
}

core::CommandResult PlateToolDispatcher::updateGesture(core::ToolHost& host,
                                                       const core::PointerState& pointer,
                                                       core::HitContext& hits) {
    if (plateActive_ && plate_) return plate_->updateGesture(host, pointer, hits);
    if (foreign_) return foreign_->updateGesture(host, pointer, hits);
    return core::CommandResult::noChange();
}

core::CommandResult PlateToolDispatcher::endGesture(core::ToolHost& host,
                                                    const core::PointerState& pointer,
                                                    core::HitContext& hits) {
    if (plateActive_ && plate_) return plate_->endGesture(host, pointer, hits);
    if (foreign_) return foreign_->endGesture(host, pointer, hits);
    return core::CommandResult::noChange();
}

core::CommandResult PlateToolDispatcher::cancelGesture(core::ToolHost& host) {
    if (plate_ && plateActive_) return plate_->cancelGesture(host);
    if (foreign_) return foreign_->cancelGesture(host);
    return core::CommandResult::noChange();
}

bool PlateToolDispatcher::handleKey(core::ToolHost& host, uint32_t virtualKey, uint32_t modifiers) {
    if (plateActive_ && plate_) return plate_->handleKey(host, virtualKey, modifiers);
    if (foreign_) return foreign_->handleKey(host, virtualKey, modifiers);
    return false;
}

core::Invalidation PlateToolDispatcher::activate(core::ToolHost& host) {
    if (plate_ && plateActive_) return plate_->activate(host);
    if (foreign_) return foreign_->activate(host);
    return core::kInvalidateNone;
}

void PlateToolDispatcher::deactivate(core::ToolHost& host) {
    if (plate_) plate_->deactivate(host);
    if (foreign_) foreign_->deactivate(host);
}

// ---------------------------------------------------------------------------
// Registry wiring
// ---------------------------------------------------------------------------
size_t registerExperimentalPresets(core::ToolRegistry& registry,
                                   PlateToolDispatcher* dispatcher,
                                   std::string* firstError) {
    size_t bound = 0;
    for (size_t i = 0; i < platePresetCount(); ++i) {
        const PlatePresetSpec& spec = platePresets()[i];
        core::ToolPreset preset;
        preset.id = spec.toolId;
        preset.behavior = core::ToolBehavior::PlaceSingle;
        preset.target = core::PresetTarget::Symbol;
        preset.displayName = spec.displayName;
        preset.category = "\xe5\xae\x9e\xe9\xaa\x8c\xe5\x9b\xbe\xe7\xa4\xba";
        preset.sourceCommand = spec.sourceCommand;
        preset.optionKey = "";
        preset.invalidation = core::kInvalidateStructure | core::kInvalidateGeometry |
                               core::kInvalidateAppearance;
        preset.implemented = spec.implemented;
        uint32_t presetId = core::kPresetNone;
        core::Error e = registry.registerPreset(preset, &presetId);
        if (!e.ok()) {
            if (firstError) *firstError = e.message;
            continue;
        }
        e = registry.bindPlatformCommand(spec.toolId,
                                         core::platform_command::ToolBand + presetId);
        if (!e.ok()) {
            if (firstError) *firstError = e.message;
            continue;
        }
        if (dispatcher) dispatcher->ownPreset(presetId, spec.presetId);
        ++bound;
    }
    if (dispatcher) {
        core::Error e = registry.registerTool(core::ToolBehavior::PlaceSingle, dispatcher);
        if (!e.ok() && firstError) *firstError = e.message;
    }
    return bound;
}

} // namespace experimental
} // namespace r62
