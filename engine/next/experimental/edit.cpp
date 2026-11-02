#include "next/experimental/edit.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace r62 {
namespace experimental {

namespace {

// Every pure mutation builds a CANDIDATE and only commits it when the whole
// figure still validates. That is how "illegal parameters must not damage the
// document" is enforced at the value level rather than trusted at each call site.
core::Error commitCandidate(PlateFigure* target, const PlateFigure& candidate) {
    std::string why;
    core::Error e = validateFigure(candidate, &why);
    if (!e.ok()) return e;
    *target = candidate;
    return core::Error::success();
}

core::Error readBoth(const core::Document& document, core::ObjectId figureId,
                     PlateFigure* figure, PlateFrame* frame) {
    if (!figure || !frame) return core::Error(core::ErrorCode::InvalidArgument, "null output");
    return readFigureAndFrame(document, figureId, figure, frame, 0);
}

} // namespace

// ---------------------------------------------------------------------------
// Pure figure mutations
// ---------------------------------------------------------------------------
core::Error figureAddLane(PlateFigure* figure, double u, double width,
                          const std::string& label, uint32_t* outElementId) {
    if (!figure) return core::Error(core::ErrorCode::InvalidArgument, "null figure");
    PlateFigure candidate = *figure;
    Lane lane;
    lane.id = allocateElementId(&candidate);
    lane.u = u;
    lane.width = width;
    lane.label = label;
    candidate.lanes.push_back(lane);
    core::Error e = commitCandidate(figure, candidate);
    if (!e.ok()) return e;
    if (outElementId) *outElementId = lane.id;
    return core::Error::success();
}

core::Error figureRemoveLane(PlateFigure* figure, uint32_t laneId,
                             size_t* removedSpots, size_t* removedBands) {
    if (!figure) return core::Error(core::ErrorCode::InvalidArgument, "null figure");
    if (!findLane(*figure, laneId))
        return core::Error(core::ErrorCode::NotFound, "no lane with that id");
    PlateFigure candidate = *figure;
    size_t spots = 0, bands = 0;
    for (size_t i = candidate.spots.size(); i > 0; --i)
        if (candidate.spots[i - 1].laneId == laneId) { candidate.spots.erase(candidate.spots.begin() + static_cast<std::ptrdiff_t>(i - 1)); ++spots; }
    for (size_t i = candidate.bands.size(); i > 0; --i)
        if (candidate.bands[i - 1].laneId == laneId) { candidate.bands.erase(candidate.bands.begin() + static_cast<std::ptrdiff_t>(i - 1)); ++bands; }
    for (size_t i = 0; i < candidate.lanes.size(); ++i)
        if (candidate.lanes[i].id == laneId) { candidate.lanes.erase(candidate.lanes.begin() + static_cast<std::ptrdiff_t>(i)); break; }
    core::Error e = commitCandidate(figure, candidate);
    if (!e.ok()) return e;
    if (removedSpots) *removedSpots = spots;
    if (removedBands) *removedBands = bands;
    return core::Error::success();
}

core::Error figureAddSpot(PlateFigure* figure, uint32_t laneId, double u, double v,
                          uint32_t* outElementId) {
    if (!figure) return core::Error(core::ErrorCode::InvalidArgument, "null figure");
    PlateFigure candidate = *figure;
    Spot spot;
    spot.id = allocateElementId(&candidate);
    spot.laneId = laneId;
    spot.u = u;
    spot.v = v;
    candidate.spots.push_back(spot);
    core::Error e = commitCandidate(figure, candidate);
    if (!e.ok()) return e;
    if (outElementId) *outElementId = spot.id;
    return core::Error::success();
}

core::Error figureRemoveSpot(PlateFigure* figure, uint32_t spotId) {
    if (!figure) return core::Error(core::ErrorCode::InvalidArgument, "null figure");
    PlateFigure candidate = *figure;
    bool found = false;
    for (size_t i = 0; i < candidate.spots.size(); ++i)
        if (candidate.spots[i].id == spotId) {
            candidate.spots.erase(candidate.spots.begin() + static_cast<std::ptrdiff_t>(i));
            found = true;
            break;
        }
    if (!found) return core::Error(core::ErrorCode::NotFound, "no spot with that id");
    return commitCandidate(figure, candidate);
}

core::Error figureAddBand(PlateFigure* figure, uint32_t laneId, double v,
                          uint32_t* outElementId) {
    if (!figure) return core::Error(core::ErrorCode::InvalidArgument, "null figure");
    PlateFigure candidate = *figure;
    Band band;
    band.id = allocateElementId(&candidate);
    band.laneId = laneId;
    band.v = v;
    candidate.bands.push_back(band);
    core::Error e = commitCandidate(figure, candidate);
    if (!e.ok()) return e;
    if (outElementId) *outElementId = band.id;
    return core::Error::success();
}

core::Error figureRemoveBand(PlateFigure* figure, uint32_t bandId) {
    if (!figure) return core::Error(core::ErrorCode::InvalidArgument, "null figure");
    PlateFigure candidate = *figure;
    bool found = false;
    for (size_t i = 0; i < candidate.bands.size(); ++i)
        if (candidate.bands[i].id == bandId) {
            candidate.bands.erase(candidate.bands.begin() + static_cast<std::ptrdiff_t>(i));
            found = true;
            break;
        }
    if (!found) return core::Error(core::ErrorCode::NotFound, "no band with that id");
    return commitCandidate(figure, candidate);
}

core::Error figureSetSpotPosition(PlateFigure* figure, uint32_t spotId, double u, double v) {
    if (!figure) return core::Error(core::ErrorCode::InvalidArgument, "null figure");
    PlateFigure candidate = *figure;
    Spot* spot = mutableSpot(&candidate, spotId);
    if (!spot) return core::Error(core::ErrorCode::NotFound, "no spot with that id");
    spot->u = u;
    spot->v = v;
    return commitCandidate(figure, candidate);
}

core::Error figureSetSpotRadius(PlateFigure* figure, uint32_t spotId, double radiusU, double radiusV) {
    if (!figure) return core::Error(core::ErrorCode::InvalidArgument, "null figure");
    PlateFigure candidate = *figure;
    Spot* spot = mutableSpot(&candidate, spotId);
    if (!spot) return core::Error(core::ErrorCode::NotFound, "no spot with that id");
    spot->radiusU = radiusU;
    spot->radiusV = radiusV;
    return commitCandidate(figure, candidate);
}

core::Error figureSetBandPosition(PlateFigure* figure, uint32_t bandId, double v) {
    if (!figure) return core::Error(core::ErrorCode::InvalidArgument, "null figure");
    PlateFigure candidate = *figure;
    Band* band = mutableBand(&candidate, bandId);
    if (!band) return core::Error(core::ErrorCode::NotFound, "no band with that id");
    band->v = v;
    return commitCandidate(figure, candidate);
}

core::Error figureSetLanePosition(PlateFigure* figure, uint32_t laneId, double u) {
    if (!figure) return core::Error(core::ErrorCode::InvalidArgument, "null figure");
    PlateFigure candidate = *figure;
    Lane* lane = mutableLane(&candidate, laneId);
    if (!lane) return core::Error(core::ErrorCode::NotFound, "no lane with that id");
    const double du = u - lane->u;
    lane->u = u;
    // A lane carries its spots; bands follow their lane by construction.
    for (size_t i = 0; i < candidate.spots.size(); ++i)
        if (candidate.spots[i].laneId == laneId) candidate.spots[i].u += du;
    return commitCandidate(figure, candidate);
}

core::Error figureSetOriginFront(PlateFigure* figure, bool originDefined, double originV,
                                 bool frontDefined, double frontV) {
    if (!figure) return core::Error(core::ErrorCode::InvalidArgument, "null figure");
    PlateFigure candidate = *figure;
    candidate.originDefined = originDefined;
    candidate.originV = originV;
    candidate.frontDefined = frontDefined;
    candidate.frontV = frontV;
    return commitCandidate(figure, candidate);
}

core::Error figureSetTitle(PlateFigure* figure, const std::string& title) {
    if (!figure) return core::Error(core::ErrorCode::InvalidArgument, "null figure");
    PlateFigure candidate = *figure;
    candidate.title = title;
    return commitCandidate(figure, candidate);
}

core::Error figureSetSpotAppearance(PlateFigure* figure, uint32_t spotId,
                                    uint32_t fillRGBA, double opacity,
                                    const std::string& label) {
    if (!figure) return core::Error(core::ErrorCode::InvalidArgument, "null figure");
    PlateFigure candidate = *figure;
    Spot* spot = mutableSpot(&candidate, spotId);
    if (!spot) return core::Error(core::ErrorCode::NotFound, "no spot with that id");
    spot->fillRGBA = fillRGBA;
    spot->opacity = opacity;
    spot->label = label;
    return commitCandidate(figure, candidate);
}

core::Error figureSetBandDisplay(PlateFigure* figure, uint32_t bandId,
                                 double displayIntensity, double thicknessV,
                                 double widthFraction) {
    if (!figure) return core::Error(core::ErrorCode::InvalidArgument, "null figure");
    PlateFigure candidate = *figure;
    Band* band = mutableBand(&candidate, bandId);
    if (!band) return core::Error(core::ErrorCode::NotFound, "no band with that id");
    band->displayIntensity = displayIntensity;
    band->thicknessV = thicknessV;
    band->widthFraction = widthFraction;
    return commitCandidate(figure, candidate);
}

core::Error figureSetMeasurement(PlateFigure* figure, const std::string& name,
                                 const MeasurementValue& value) {
    if (!figure) return core::Error(core::ErrorCode::InvalidArgument, "null figure");
    if (name.empty()) return core::Error(core::ErrorCode::InvalidArgument, "empty field name");
    std::string why;
    core::Error m = value.validate(&why);
    if (!m.ok()) return m;
    PlateFigure candidate = *figure;
    bool replaced = false;
    for (size_t i = 0; i < candidate.fields.size(); ++i)
        if (candidate.fields[i].name == name) { candidate.fields[i].value = value; replaced = true; break; }
    if (!replaced) candidate.fields.push_back(NamedMeasurement(name, value));
    return commitCandidate(figure, candidate);
}

core::Error figureRemoveMeasurement(PlateFigure* figure, const std::string& name) {
    if (!figure) return core::Error(core::ErrorCode::InvalidArgument, "null figure");
    PlateFigure candidate = *figure;
    for (size_t i = 0; i < candidate.fields.size(); ++i)
        if (candidate.fields[i].name == name) {
            candidate.fields.erase(candidate.fields.begin() + static_cast<std::ptrdiff_t>(i));
            return commitCandidate(figure, candidate);
        }
    return core::Error(core::ErrorCode::NotFound, "no measurement with that name");
}

core::Error figureAttachAnnotation(PlateFigure* figure, const std::string& slot,
                                   double u, double v, core::ObjectId textId) {
    if (!figure) return core::Error(core::ErrorCode::InvalidArgument, "null figure");
    PlateFigure candidate = *figure;
    for (size_t i = 0; i < candidate.annotations.size(); ++i)
        if (candidate.annotations[i].slot == slot) {
            candidate.annotations[i].u = u;
            candidate.annotations[i].v = v;
            candidate.annotations[i].textId = textId;
            return commitCandidate(figure, candidate);
        }
    FigureAnnotation a;
    a.slot = slot;
    a.u = u;
    a.v = v;
    a.textId = textId;
    candidate.annotations.push_back(a);
    return commitCandidate(figure, candidate);
}

core::Error figureDetachAnnotation(PlateFigure* figure, const std::string& slot) {
    if (!figure) return core::Error(core::ErrorCode::InvalidArgument, "null figure");
    PlateFigure candidate = *figure;
    for (size_t i = 0; i < candidate.annotations.size(); ++i)
        if (candidate.annotations[i].slot == slot) {
            candidate.annotations.erase(candidate.annotations.begin() + static_cast<std::ptrdiff_t>(i));
            return commitCandidate(figure, candidate);
        }
    return core::Error(core::ErrorCode::NotFound, "no annotation with that slot");
}

// ---------------------------------------------------------------------------
// Document operations
// ---------------------------------------------------------------------------
core::Error applyAddFigure(core::Document& document, FigureKind kind, const PlateFrame& frame,
                           uint32_t laneCount, core::ObjectId* outId) {
    const PlatePresetSpec* spec = presetForKind(kind);
    PlateFigure figure = makeDefaultFigure(kind, laneCount ? laneCount : spec->defaultLaneCount);
    return addFigure(document, figure, frame, document.defaultLineWidth(), outId);
}

core::Error applyAddLane(core::Document& document, core::ObjectId figureId, double u,
                         double width, const std::string& label, uint32_t* outElementId) {
    PlateFigure figure;
    PlateFrame frame;
    core::Error e = readBoth(document, figureId, &figure, &frame);
    if (!e.ok()) return e;
    PlateFigure candidate = figure;
    uint32_t id = 0;
    e = figureAddLane(&candidate, u, width, label, &id);
    if (!e.ok()) return e;
    e = writeFigure(document, figureId, candidate);
    if (!e.ok()) return e;
    if (outElementId) *outElementId = id;
    return core::Error::success();
}

core::Error applyRemoveLane(core::Document& document, core::ObjectId figureId, uint32_t laneId) {
    PlateFigure figure;
    PlateFrame frame;
    core::Error e = readBoth(document, figureId, &figure, &frame);
    if (!e.ok()) return e;
    PlateFigure candidate = figure;
    e = figureRemoveLane(&candidate, laneId, 0, 0);
    if (!e.ok()) return e;
    return writeFigure(document, figureId, candidate);
}

core::Error applyAddSpot(core::Document& document, core::ObjectId figureId, uint32_t laneId,
                         double u, double v, uint32_t* outElementId) {
    PlateFigure figure;
    PlateFrame frame;
    core::Error e = readBoth(document, figureId, &figure, &frame);
    if (!e.ok()) return e;
    PlateFigure candidate = figure;
    uint32_t id = 0;
    e = figureAddSpot(&candidate, laneId, u, v, &id);
    if (!e.ok()) return e;
    e = writeFigure(document, figureId, candidate);
    if (!e.ok()) return e;
    if (outElementId) *outElementId = id;
    return core::Error::success();
}

core::Error applyRemoveSpot(core::Document& document, core::ObjectId figureId, uint32_t spotId) {
    PlateFigure figure;
    PlateFrame frame;
    core::Error e = readBoth(document, figureId, &figure, &frame);
    if (!e.ok()) return e;
    PlateFigure candidate = figure;
    e = figureRemoveSpot(&candidate, spotId);
    if (!e.ok()) return e;
    return writeFigure(document, figureId, candidate);
}

core::Error applyAddBand(core::Document& document, core::ObjectId figureId, uint32_t laneId,
                         double v, uint32_t* outElementId) {
    PlateFigure figure;
    PlateFrame frame;
    core::Error e = readBoth(document, figureId, &figure, &frame);
    if (!e.ok()) return e;
    PlateFigure candidate = figure;
    uint32_t id = 0;
    e = figureAddBand(&candidate, laneId, v, &id);
    if (!e.ok()) return e;
    e = writeFigure(document, figureId, candidate);
    if (!e.ok()) return e;
    if (outElementId) *outElementId = id;
    return core::Error::success();
}

core::Error applyRemoveBand(core::Document& document, core::ObjectId figureId, uint32_t bandId) {
    PlateFigure figure;
    PlateFrame frame;
    core::Error e = readBoth(document, figureId, &figure, &frame);
    if (!e.ok()) return e;
    PlateFigure candidate = figure;
    e = figureRemoveBand(&candidate, bandId);
    if (!e.ok()) return e;
    return writeFigure(document, figureId, candidate);
}

core::Error applyMoveElement(core::Document& document, core::ObjectId figureId,
                             const ElementRef& element, double u, double v) {
    PlateFigure figure;
    PlateFrame frame;
    core::Error e = readBoth(document, figureId, &figure, &frame);
    if (!e.ok()) return e;
    PlateFigure candidate = figure;
    switch (element.kind) {
    case ElementKind::Spot:
        e = figureSetSpotPosition(&candidate, element.id, u, v);
        break;
    case ElementKind::Band:
        e = figureSetBandPosition(&candidate, element.id, v);
        break;
    case ElementKind::Lane:
        e = figureSetLanePosition(&candidate, element.id, u);
        break;
    default:
        return core::Error(core::ErrorCode::InvalidArgument,
                           "only a spot, band or lane can be dragged inside the figure");
    }
    if (!e.ok()) return e;
    return writeFigure(document, figureId, candidate);
}

core::Error applyResizeBoard(core::Document& document, core::ObjectId figureId,
                             const PlateFrame& frame) {
    return writeFrame(document, figureId, frame);
}

core::Error applySetOriginFront(core::Document& document, core::ObjectId figureId,
                                bool originDefined, double originV, bool frontDefined,
                                double frontV) {
    PlateFigure figure;
    PlateFrame frame;
    core::Error e = readBoth(document, figureId, &figure, &frame);
    if (!e.ok()) return e;
    PlateFigure candidate = figure;
    e = figureSetOriginFront(&candidate, originDefined, originV, frontDefined, frontV);
    if (!e.ok()) return e;
    return writeFigure(document, figureId, candidate);
}

core::Error applySetTitle(core::Document& document, core::ObjectId figureId,
                          const std::string& title) {
    PlateFigure figure;
    PlateFrame frame;
    core::Error e = readBoth(document, figureId, &figure, &frame);
    if (!e.ok()) return e;
    PlateFigure candidate = figure;
    e = figureSetTitle(&candidate, title);
    if (!e.ok()) return e;
    return writeFigure(document, figureId, candidate);
}

core::Error applySetMeasurement(core::Document& document, core::ObjectId figureId,
                                const std::string& name, const MeasurementValue& value) {
    PlateFigure figure;
    PlateFrame frame;
    core::Error e = readBoth(document, figureId, &figure, &frame);
    if (!e.ok()) return e;
    PlateFigure candidate = figure;
    e = figureSetMeasurement(&candidate, name, value);
    if (!e.ok()) return e;
    return writeFigure(document, figureId, candidate);
}

core::Error applyDuplicateFigure(core::Document& document, core::ObjectId sourceId,
                                 const core::Vec2& delta, core::ObjectId* outId) {
    return duplicateFigure(document, sourceId, delta, false, outId);
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------
core::Command makeFigureCommand(const std::string& name, core::Invalidation invalidation,
                                const std::function<core::Error(core::Document&)>& fn) {
    core::Command::Fn body = [fn, invalidation](core::CommandContext& ctx) -> core::CommandResult {
        core::Error e = fn(ctx.document);
        if (!e.ok()) return core::CommandResult::fail(e.code, e.message);
        return core::CommandResult::success(invalidation);
    };
    return core::Command(name, invalidation, body);
}

namespace {
const core::Invalidation kElementInvalidation =
    core::kInvalidateGeometry | core::kInvalidateAppearance;
}

core::Command addFigureCommand(FigureKind kind, const PlateFrame& frame, uint32_t laneCount,
                               EditOutcome* out) {
    return makeFigureCommand("experimental.addFigure",
        core::kInvalidateStructure | core::kInvalidateGeometry | core::kInvalidateAppearance,
        [kind, frame, laneCount, out](core::Document& document) -> core::Error {
            core::ObjectId id = core::kInvalidObjectId;
            core::Error e = applyAddFigure(document, kind, frame, laneCount, &id);
            if (e.ok() && out) { out->objectId = id; out->changed = true; }
            return e;
        });
}

core::Command addLaneCommand(core::ObjectId figureId, double u, double width,
                             const std::string& label, EditOutcome* out) {
    return makeFigureCommand("experimental.addLane", kElementInvalidation,
        [figureId, u, width, label, out](core::Document& document) -> core::Error {
            uint32_t id = 0;
            core::Error e = applyAddLane(document, figureId, u, width, label, &id);
            if (e.ok() && out) { out->objectId = figureId; out->elementId = id; out->changed = true; }
            return e;
        });
}

core::Command removeLaneCommand(core::ObjectId figureId, uint32_t laneId, EditOutcome* out) {
    return makeFigureCommand("experimental.removeLane", kElementInvalidation,
        [figureId, laneId, out](core::Document& document) -> core::Error {
            core::Error e = applyRemoveLane(document, figureId, laneId);
            if (e.ok() && out) { out->objectId = figureId; out->elementId = laneId; out->changed = true; }
            return e;
        });
}

core::Command addSpotCommand(core::ObjectId figureId, uint32_t laneId, double u, double v,
                             EditOutcome* out) {
    return makeFigureCommand("experimental.addSpot", kElementInvalidation,
        [figureId, laneId, u, v, out](core::Document& document) -> core::Error {
            uint32_t id = 0;
            core::Error e = applyAddSpot(document, figureId, laneId, u, v, &id);
            if (e.ok() && out) { out->objectId = figureId; out->elementId = id; out->changed = true; }
            return e;
        });
}

core::Command removeSpotCommand(core::ObjectId figureId, uint32_t spotId, EditOutcome* out) {
    return makeFigureCommand("experimental.removeSpot", kElementInvalidation,
        [figureId, spotId, out](core::Document& document) -> core::Error {
            core::Error e = applyRemoveSpot(document, figureId, spotId);
            if (e.ok() && out) { out->objectId = figureId; out->elementId = spotId; out->changed = true; }
            return e;
        });
}

core::Command addBandCommand(core::ObjectId figureId, uint32_t laneId, double v,
                             EditOutcome* out) {
    return makeFigureCommand("experimental.addBand", kElementInvalidation,
        [figureId, laneId, v, out](core::Document& document) -> core::Error {
            uint32_t id = 0;
            core::Error e = applyAddBand(document, figureId, laneId, v, &id);
            if (e.ok() && out) { out->objectId = figureId; out->elementId = id; out->changed = true; }
            return e;
        });
}

core::Command removeBandCommand(core::ObjectId figureId, uint32_t bandId, EditOutcome* out) {
    return makeFigureCommand("experimental.removeBand", kElementInvalidation,
        [figureId, bandId, out](core::Document& document) -> core::Error {
            core::Error e = applyRemoveBand(document, figureId, bandId);
            if (e.ok() && out) { out->objectId = figureId; out->elementId = bandId; out->changed = true; }
            return e;
        });
}

core::Command moveElementCommand(core::ObjectId figureId, const ElementRef& element,
                                 double u, double v, EditOutcome* out) {
    return makeFigureCommand("experimental.moveElement", kElementInvalidation,
        [figureId, element, u, v, out](core::Document& document) -> core::Error {
            core::Error e = applyMoveElement(document, figureId, element, u, v);
            if (e.ok() && out) { out->objectId = figureId; out->elementId = element.id; out->changed = true; }
            return e;
        });
}

core::Command resizeBoardCommand(core::ObjectId figureId, const PlateFrame& frame,
                                 EditOutcome* out) {
    return makeFigureCommand("experimental.resizeBoard", kElementInvalidation,
        [figureId, frame, out](core::Document& document) -> core::Error {
            core::Error e = applyResizeBoard(document, figureId, frame);
            if (e.ok() && out) { out->objectId = figureId; out->changed = true; }
            return e;
        });
}

core::Command duplicateFigureCommand(core::ObjectId sourceId, const core::Vec2& delta,
                                     EditOutcome* out) {
    return makeFigureCommand("experimental.duplicateFigure",
        core::kInvalidateStructure | core::kInvalidateGeometry | core::kInvalidateAppearance,
        [sourceId, delta, out](core::Document& document) -> core::Error {
            core::ObjectId id = core::kInvalidObjectId;
            core::Error e = applyDuplicateFigure(document, sourceId, delta, &id);
            if (e.ok() && out) { out->objectId = id; out->changed = true; }
            return e;
        });
}

core::Command writeFigureCommand(core::ObjectId figureId, const PlateFigure& figure,
                                 EditOutcome* out) {
    return makeFigureCommand("experimental.writeFigure", kElementInvalidation,
        [figureId, figure, out](core::Document& document) -> core::Error {
            core::Error e = writeFigure(document, figureId, figure);
            if (e.ok() && out) { out->objectId = figureId; out->changed = true; }
            return e;
        });
}

core::Command setOriginFrontCommand(core::ObjectId figureId, bool originDefined, double originV,
                                    bool frontDefined, double frontV, EditOutcome* out) {
    return makeFigureCommand("experimental.setOriginFront", kElementInvalidation,
        [figureId, originDefined, originV, frontDefined, frontV, out](core::Document& document) -> core::Error {
            core::Error e = applySetOriginFront(document, figureId, originDefined, originV,
                                                frontDefined, frontV);
            if (e.ok() && out) { out->objectId = figureId; out->changed = true; }
            return e;
        });
}

core::Command setTitleCommand(core::ObjectId figureId, const std::string& title, EditOutcome* out) {
    return makeFigureCommand("experimental.setTitle", kElementInvalidation,
        [figureId, title, out](core::Document& document) -> core::Error {
            core::Error e = applySetTitle(document, figureId, title);
            if (e.ok() && out) { out->objectId = figureId; out->changed = true; }
            return e;
        });
}

core::Command setMeasurementCommand(core::ObjectId figureId, const std::string& name,
                                    const MeasurementValue& value, EditOutcome* out) {
    return makeFigureCommand("experimental.setMeasurement", kElementInvalidation,
        [figureId, name, value, out](core::Document& document) -> core::Error {
            core::Error e = applySetMeasurement(document, figureId, name, value);
            if (e.ok() && out) { out->objectId = figureId; out->changed = true; }
            return e;
        });
}

core::Command createAnnotationTextCommand(core::ObjectId figureId, const std::string& slot,
                                          const std::string& utf8, double textSize,
                                          bool bold, bool italic, EditOutcome* out) {
    return makeFigureCommand("experimental.createAnnotationText",
        core::kInvalidateStructure | core::kInvalidateAppearance,
        [figureId, slot, utf8, textSize, bold, italic, out](core::Document& document) -> core::Error {
            core::ObjectId textId = core::kInvalidObjectId;
            core::Error e = createAnnotationText(document, figureId, slot, utf8, textSize,
                                                 bold, italic, &textId);
            if (e.ok() && out) { out->objectId = textId; out->changed = true; }
            return e;
        });
}

core::Command setAnnotationTextCommand(core::ObjectId figureId, const std::string& slot,
                                       const std::string& utf8, EditOutcome* out) {
    return makeFigureCommand("experimental.setAnnotationText", core::kInvalidateAppearance,
        [figureId, slot, utf8, out](core::Document& document) -> core::Error {
            core::Error e = setAnnotationText(document, figureId, slot, utf8);
            if (e.ok() && out) { out->objectId = figureId; out->changed = true; }
            return e;
        });
}

core::Command syncAnnotationTextsCommand(core::ObjectId figureId, EditOutcome* out) {
    return makeFigureCommand("experimental.syncAnnotationTexts", core::kInvalidateGeometry,
        [figureId, out](core::Document& document) -> core::Error {
            core::Error e = syncAnnotationTextPositions(document, figureId);
            if (e.ok() && out) { out->objectId = figureId; out->changed = true; }
            return e;
        });
}

// ---------------------------------------------------------------------------
// T09 annotation text
// ---------------------------------------------------------------------------
core::Vec2 annotationSlotPosition(const PlateFrame& frame, const PlateFigure& figure,
                                  const std::string& slot, double u, double v) {
    if (slot == "title") return frame.pointAt(0.0, -1.18);
    if (slot == "origin") return frame.pointAt(1.10, figure.originV);
    if (slot == "front") return frame.pointAt(1.10, figure.frontV);
    if (slot.compare(0, 5, "lane:") == 0) {
        const uint32_t id = static_cast<uint32_t>(std::atoi(slot.c_str() + 5));
        const Lane* lane = findLane(figure, id);
        if (lane) return frame.pointAt(lane->u, 1.14);
        return frame.pointAt(u, 1.14);
    }
    if (slot.compare(0, 5, "spot:") == 0) {
        const uint32_t id = static_cast<uint32_t>(std::atoi(slot.c_str() + 5));
        const Spot* spot = findSpot(figure, id);
        if (spot) return frame.pointAt(spot->u + 0.06, spot->v);
        return frame.pointAt(u, v);
    }
    return frame.pointAt(u, v);
}

namespace {

// The relative position stored with an annotation slot. Known slots recompute
// from the current geometry when the text is (re)anchored, so the stored value is
// a hint for a free slot and a cache for a known one.
void storedSlotPosition(const PlateFigure& figure, const std::string& slot,
                        double* u, double* v) {
    if (slot == "title") { *u = 0.0; *v = -1.18; return; }
    if (slot == "origin") { *u = 1.10; *v = figure.originV; return; }
    if (slot == "front") { *u = 1.10; *v = figure.frontV; return; }
    if (slot.compare(0, 5, "lane:") == 0) {
        const uint32_t id = static_cast<uint32_t>(std::atoi(slot.c_str() + 5));
        const Lane* lane = findLane(figure, id);
        *u = lane ? lane->u : 0.0;
        *v = 1.14;
        return;
    }
    if (slot.compare(0, 5, "spot:") == 0) {
        const uint32_t id = static_cast<uint32_t>(std::atoi(slot.c_str() + 5));
        const Spot* spot = findSpot(figure, id);
        *u = spot ? spot->u + 0.06 : 0.0;
        *v = spot ? spot->v : 0.0;
        return;
    }
    *u = 0.0;
    *v = 0.0;
}

} // namespace

core::Error createAnnotationText(core::Document& document, core::ObjectId figureId,
                                 const std::string& slot, const std::string& utf8,
                                 double textSize, bool bold, bool italic,
                                 core::ObjectId* outTextId) {
    if (utf8.empty())
        return core::Error(core::ErrorCode::InvalidText, "annotation text must not be empty");
    PlateFigure figure;
    PlateFrame frame;
    core::Error e = readBoth(document, figureId, &figure, &frame);
    if (!e.ok()) return e;

    double u = 0.0, v = 0.0;
    storedSlotPosition(figure, slot, &u, &v);
    const core::Vec2 anchor = annotationSlotPosition(frame, figure, slot, u, v);

    // T09 authors the runs. Its edit session is the only sanctioned way to build
    // a run vector, which keeps annotation text editable by the same code that
    // edits every other caption in the document.
    text::TextEditSession session;
    session.begin(std::vector<core::TextRun>());
    text::EditStyle style;
    style.useCaretStyle = false;
    style.size = textSize;
    if (bold) style.flags |= core::kBold;
    if (italic) style.flags |= core::kItalic;
    if (!session.insertUtf8(utf8, style))
        return core::Error(core::ErrorCode::InvalidText, "T09 refused the annotation text");

    core::ObjectId textId = core::kInvalidObjectId;
    e = document.addText(anchor, textSize, utf8, &textId);
    if (!e.ok()) return e;
    e = document.setRuns(textId, session.commit());
    if (!e.ok()) return e;

    PlateFigure candidate = figure;
    e = figureAttachAnnotation(&candidate, slot, u, v, textId);
    if (!e.ok()) return e;
    e = writeFigure(document, figureId, candidate);
    if (!e.ok()) return e;
    if (outTextId) *outTextId = textId;
    return core::Error::success();
}

core::Error setAnnotationText(core::Document& document, core::ObjectId figureId,
                              const std::string& slot, const std::string& utf8) {
    PlateFigure figure;
    PlateFrame frame;
    core::Error e = readBoth(document, figureId, &figure, &frame);
    if (!e.ok()) return e;
    core::ObjectId textId = core::kInvalidObjectId;
    for (size_t i = 0; i < figure.annotations.size(); ++i)
        if (figure.annotations[i].slot == slot) textId = figure.annotations[i].textId;
    if (textId == core::kInvalidObjectId)
        return core::Error(core::ErrorCode::NotFound, "no annotation with that slot");
    if (!document.texts().alive(textId))
        return core::Error(core::ErrorCode::NotFound, "the annotation text is gone");
    if (utf8.empty()) return core::Error(core::ErrorCode::InvalidText, "text must not be empty");

    text::TextEditSession session;
    session.begin(document.runs(textId));
    session.selectAll();
    text::EditStyle style;
    style.useCaretStyle = true;
    if (!session.insertUtf8(utf8, style))
        return core::Error(core::ErrorCode::InvalidText, "T09 refused the annotation text");
    return document.setRuns(textId, session.commit());
}

std::string annotationText(const core::Document& document, core::ObjectId figureId,
                           const std::string& slot) {
    PlateFigure figure;
    if (!readFigure(document, figureId, &figure, 0).ok()) return std::string();
    for (size_t i = 0; i < figure.annotations.size(); ++i)
        if (figure.annotations[i].slot == slot)
            return document.plainText(figure.annotations[i].textId);
    return std::string();
}

core::Error syncAnnotationTextPositions(core::Document& document, core::ObjectId figureId) {
    PlateFigure figure;
    PlateFrame frame;
    core::Error e = readBoth(document, figureId, &figure, &frame);
    if (!e.ok()) return e;
    for (size_t i = 0; i < figure.annotations.size(); ++i) {
        const FigureAnnotation& a = figure.annotations[i];
        if (a.textId == core::kInvalidObjectId) continue;
        if (!document.texts().alive(a.textId)) continue;
        const core::Vec2 anchor = annotationSlotPosition(frame, figure, a.slot, a.u, a.v);
        core::Error p = document.setTextPosition(a.textId, anchor);
        if (!p.ok()) return p;
    }
    return core::Error::success();
}

} // namespace experimental
} // namespace r62


