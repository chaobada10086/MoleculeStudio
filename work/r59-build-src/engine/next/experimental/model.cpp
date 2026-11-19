#include "next/experimental/model.hpp"

#include "next/symbols_orbitals/model.hpp"

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace r62 {
namespace experimental {

namespace {

const double kMinHalfExtent = 1e-3;

void notifyBagDirty(core::Document& document, core::ObjectId id) {
    // NOT optional: DocumentSession::buildChangeSet() emits an ExtensionPatch only
    // for refs noted here. Without it a payload change is invisible to undo.
    document.noteTouchedExtensions(core::ObjectRef(core::ObjectKind::Symbol, id), false);
}

void fillMirror(const PlateFrame& frame, const PlateFigure& figure, double documentLineWidth,
                core::AnnotationRecord* rec) {
    core::Vec2 c[4];
    frame.corners(c);
    rec->geometry[0] = frame.centre;
    core::Box box(c[0], c[0]);
    for (int i = 1; i < 4; ++i) box.add(c[i]);
    rec->geometry[1] = box.min;
    rec->geometry[2] = core::Vec2(frame.centre.x + frame.axisU.x, frame.centre.y + frame.axisU.y);
    rec->geometry[3] = core::Vec2(frame.centre.x + frame.axisV.x, frame.centre.y + frame.axisV.y);
    rec->kind = core::ObjectKind::Symbol;
    rec->presetId = presetForKind(figure.kind)->presetId;
    rec->scale = 1.0;
    rec->rotation = 0.0;   // the frame axes carry any rotation, like next/graphics
    rec->style = figure.border;
    rec->style.width = figure.border.width > 0.0 ? figure.border.width : documentLineWidth;
    rec->fillRGBA = figure.plateFillRGBA;
}

PlateFrame frameFromMirror(const core::AnnotationRecord& rec) {
    PlateFrame frame;
    frame.centre = rec.geometry[0];
    frame.axisU = core::Vec2(rec.geometry[2].x - rec.geometry[0].x,
                             rec.geometry[2].y - rec.geometry[0].y);
    frame.axisV = core::Vec2(rec.geometry[3].x - rec.geometry[0].x,
                             rec.geometry[3].y - rec.geometry[0].y);
    if (core::length(frame.axisU) < kMinHalfExtent) frame.axisU = core::Vec2(40.0, 0.0);
    if (core::length(frame.axisV) < kMinHalfExtent) frame.axisV = core::Vec2(0.0, 30.0);
    return frame;
}

FigureKind kindFromPreset(uint32_t presetId, bool* found) {
    const PlatePresetSpec* spec = platePresetById(presetId);
    if (found) *found = (spec != 0);
    return spec ? spec->kind : FigureKind::TlcPlate;
}

} // namespace

// ---------------------------------------------------------------------------
// Identity
// ---------------------------------------------------------------------------
core::ObjectKind figureObjectKind() { return core::ObjectKind::Symbol; }

core::ObjectRef figureRef(core::ObjectId id) {
    return core::ObjectRef(core::ObjectKind::Symbol, id);
}

bool isFigureObject(const core::Document& document, const core::ObjectRef& ref) {
    if (ref.kind != core::ObjectKind::Symbol) return false;
    const core::AnnotationRecord* rec = document.symbols().alive(ref.id);
    if (!rec) return false;
    // A chemical symbol and a figure both live in symbols() and both keep their
    // preset row in rec->presetId, and the two preset tables overlap: CirclePlus is
    // row 0 in symbols_orbitals/preset.cpp and the TLC plate is row 0 here. Reading
    // rec->presetId alone therefore turns the plus-charge symbol into a TLC board.
    // The extension bag is the discriminator the rest of the pipeline already uses
    // (session.cpp picks push over symbol the same way): every figure write stores
    // kFigureParamsKey, every symbol/push write stores its own key.
    const core::ExtensionBag& bag = document.extensions(ref);
    if (bag.findValue(kFigureParamsKey)) return true;
    if (bag.findValue(symbols::kSymbolParamsKey) || bag.findValue(symbols::kPushParamsKey))
        return false;
    // Bag-less record: fall back to the preset row so an older document still
    // opens; only the two colliding symbol rows are affected, and those always
    // carry their own bag.
    return platePresetById(rec->presetId) != 0;
}

const PlatePresetSpec* presetForKind(FigureKind kind) {
    for (size_t i = 0; i < platePresetCount(); ++i)
        if (platePresets()[i].kind == kind) return &platePresets()[i];
    return &platePresets()[0];
}

// ---------------------------------------------------------------------------
// Reads
// ---------------------------------------------------------------------------
core::Error readFigure(const core::Document& document, core::ObjectId id,
                       PlateFigure* out, bool* usedExtension) {
    if (!out) return core::Error(core::ErrorCode::InvalidArgument, "null output");
    const core::AnnotationRecord* rec = document.symbols().alive(id);
    if (!rec) return core::Error(core::ErrorCode::NotFound, "no figure with that id");
    bool found = false;
    const FigureKind kind = kindFromPreset(rec->presetId, &found);
    if (usedExtension) *usedExtension = false;
    const std::string* payload =
        document.extensions(figureRef(id)).findValue(kFigureParamsKey);
    if (payload) {
        bool ok = false;
        PlateFigure decoded = decodeFigure(*payload, &ok);
        if (ok) {
            *out = decoded;
            if (usedExtension) *usedExtension = true;
            return core::Error::success();
        }
    }
    // Fall back to a minimal default of the preset kind. The record mirror still
    // gives the placement, so the object is never invisible.
    const PlatePresetSpec* spec = platePresetById(rec->presetId);
    const uint32_t laneCount = spec ? spec->defaultLaneCount : 2u;
    *out = makeDefaultFigure(found ? kind : FigureKind::TlcPlate, laneCount);
    return core::Error::success();
}

core::Error readFrame(const core::Document& document, core::ObjectId id, PlateFrame* out) {
    if (!out) return core::Error(core::ErrorCode::InvalidArgument, "null output");
    const core::AnnotationRecord* rec = document.symbols().alive(id);
    if (!rec) return core::Error(core::ErrorCode::NotFound, "no figure with that id");
    *out = frameFromMirror(*rec);
    return core::Error::success();
}

core::Error readFigureAndFrame(const core::Document& document, core::ObjectId id,
                               PlateFigure* figure, PlateFrame* frame, bool* usedExtension) {
    core::Error e = readFigure(document, id, figure, usedExtension);
    if (!e.ok()) return e;
    return readFrame(document, id, frame);
}

// ---------------------------------------------------------------------------
// Writes
// ---------------------------------------------------------------------------
core::Error addFigure(core::Document& document, const PlateFigure& figure,
                      const PlateFrame& frame, double documentLineWidth,
                      core::ObjectId* outId) {
    std::string why;
    core::Error valid = validateFigure(figure, &why);
    if (!valid.ok()) return valid;
    if (frame.degenerate())
        return core::Error(core::ErrorCode::InvalidGeometry, "the board frame is degenerate");
    if (!frame.centre.finite() || !frame.axisU.finite() || !frame.axisV.finite())
        return core::Error(core::ErrorCode::InvalidGeometry, "the board frame is not finite");

    core::AnnotationRecord rec;
    fillMirror(frame, figure, documentLineWidth, &rec);
    core::ObjectId id = core::kInvalidObjectId;
    core::Error e = document.addAnnotation(core::ObjectKind::Symbol, rec, &id);
    if (!e.ok()) return e;
    document.extensions(figureRef(id)).set(kFigureParamsKey, encodeFigure(figure));
    notifyBagDirty(document, id);
    if (outId) *outId = id;
    return core::Error::success();
}

core::Error writeFigure(core::Document& document, core::ObjectId id,
                        const PlateFigure& figure) {
    core::AnnotationRecord* rec = document.symbols().mutableGet(id);
    if (!rec) return core::Error(core::ErrorCode::NotFound, "no figure with that id");
    if (!rec->isAlive()) return core::Error(core::ErrorCode::NotAlive, "the figure is deleted");
    std::string why;
    core::Error valid = validateFigure(figure, &why);
    if (!valid.ok()) return valid;
    rec->kind = core::ObjectKind::Symbol;
    rec->presetId = presetForKind(figure.kind)->presetId;
    rec->fillRGBA = figure.plateFillRGBA;
    if (figure.border.width > 0.0) rec->style.width = figure.border.width;
    rec->style.colorRGBA = figure.border.colorRGBA;
    rec->style.dash = figure.border.dash;
    document.extensions(figureRef(id)).set(kFigureParamsKey, encodeFigure(figure));
    document.noteTouchedAnnotation(core::ObjectKind::Symbol, id);
    notifyBagDirty(document, id);
    document.bumpStructure();
    return core::Error::success();
}

core::Error writeFrame(core::Document& document, core::ObjectId id, const PlateFrame& frame) {
    core::AnnotationRecord* rec = document.symbols().mutableGet(id);
    if (!rec) return core::Error(core::ErrorCode::NotFound, "no figure with that id");
    if (!rec->isAlive()) return core::Error(core::ErrorCode::NotAlive, "the figure is deleted");
    if (!frame.centre.finite() || !frame.axisU.finite() || !frame.axisV.finite())
        return core::Error(core::ErrorCode::InvalidGeometry, "the board frame is not finite");
    if (frame.degenerate())
        return core::Error(core::ErrorCode::InvalidGeometry, "the board frame is degenerate");

    core::Vec2 c[4];
    frame.corners(c);
    core::Box box(c[0], c[0]);
    for (int i = 1; i < 4; ++i) box.add(c[i]);
    rec->geometry[0] = frame.centre;
    rec->geometry[1] = box.min;
    rec->geometry[2] = core::Vec2(frame.centre.x + frame.axisU.x, frame.centre.y + frame.axisU.y);
    rec->geometry[3] = core::Vec2(frame.centre.x + frame.axisV.x, frame.centre.y + frame.axisV.y);
    // NOTE: this deliberately does NOT call Document::setAnnotationGeometry(id, ...).
    // That function takes a bare ObjectId and searches the five annotation slabs in
    // order, so when a Symbol and an Arrow happen to share the same slot index it
    // writes the WRONG object. The slab-specific mutableGet above is unambiguous.
    // A suggested T01 patch (ObjectRef parameter) is in INTERFACES.md section 8.
    document.noteTouchedAnnotation(core::ObjectKind::Symbol, id);
    document.bumpStructure();
    return core::Error::success();
}

core::Error writeFigureAndFrame(core::Document& document, core::ObjectId id,
                                const PlateFigure& figure, const PlateFrame& frame) {
    core::Error e = writeFigure(document, id, figure);
    if (!e.ok()) return e;
    return writeFrame(document, id, frame);
}

// ---------------------------------------------------------------------------
// Signature / bounds
// ---------------------------------------------------------------------------
uint64_t figureObjectSignature(const core::Document& document, core::ObjectId id) {
    PlateFrame frame;
    if (!readFrame(document, id, &frame).ok()) return 0;
    PlateFigure figure;
    readFigure(document, id, &figure, 0);
    uint64_t h = figureContentSignature(figure);
    const double parts[10] = {
        frame.centre.x, frame.centre.y, frame.axisU.x, frame.axisU.y,
        frame.axisV.x, frame.axisV.y, static_cast<double>(figure.payloadVersion),
        figure.plateFillEnabled ? 1.0 : 0.0, 0.0, 0.0
    };
    for (int i = 0; i < 10; ++i) {
        uint64_t bits = 0;
        double v = parts[i];
        if (v == 0.0) v = 0.0;
        static_assert(sizeof(bits) == sizeof(v), "double and uint64_t must match");
        std::memcpy(&bits, &v, sizeof(bits));
        h ^= bits + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    }
    return h ? h : 1ULL;
}

core::Box figureObjectBounds(const core::Document& document, core::ObjectId id) {
    core::Box box;
    const core::AnnotationRecord* rec = document.symbols().alive(id);
    if (!rec) return box;
    const PlateFrame frame = frameFromMirror(*rec);
    box = frame.aabb();
    double half = rec->style.width > 0.0 ? rec->style.width * 0.5
                                          : document.defaultLineWidth() * 0.5;
    if (half < 0.3) half = 0.3;
    box.min.x -= half;
    box.min.y -= half;
    box.max.x += half;
    box.max.y += half;
    return box;
}

// ---------------------------------------------------------------------------
// Duplicate
// ---------------------------------------------------------------------------
core::Error duplicateFigure(core::Document& document, core::ObjectId sourceId,
                            const core::Vec2& delta, bool freshElementIds,
                            core::ObjectId* outId) {
    PlateFigure figure;
    PlateFrame frame;
    core::Error e = readFigureAndFrame(document, sourceId, &figure, &frame, 0);
    if (!e.ok()) return e;
    frame.centre = core::Vec2(frame.centre.x + delta.x, frame.centre.y + delta.y);

    // Copy the annotation text objects first, so the new payload can reference
    // the copies instead of the originals.
    std::vector<core::ObjectId> remapped(figure.annotations.size(), core::kInvalidObjectId);
    for (size_t i = 0; i < figure.annotations.size(); ++i) {
        const core::ObjectId oldId = figure.annotations[i].textId;
        if (oldId == core::kInvalidObjectId) continue;
        const core::TextRecord* source = document.texts().alive(oldId);
        if (!source) { remapped[i] = core::kInvalidObjectId; continue; }
        core::TextRecord copy = *source;
        copy.position = core::Vec2(copy.position.x + delta.x, copy.position.y + delta.y);
        copy.sourceId = 0;
        core::ObjectId newTextId = core::kInvalidObjectId;
        core::Error te = document.addTextRecord(copy, &newTextId);
        if (!te.ok()) return te;
        document.setRuns(newTextId, document.runs(oldId));
        remapped[i] = newTextId;
    }
    for (size_t i = 0; i < figure.annotations.size(); ++i)
        figure.annotations[i].textId = remapped[i];

    if (freshElementIds) {
        // Renumber from a fresh counter and remap every lane reference, so the
        // copy is a genuinely independent figure rather than one that shares
        // element identities with its source.
        PlateFigure original;
        readFigure(document, sourceId, &original, 0);
        std::vector<uint32_t> mapOld, mapNew;
        uint32_t next = figure.nextElementId;
        for (size_t j = 0; j < figure.lanes.size(); ++j) {
            mapOld.push_back(original.lanes[j].id);
            mapNew.push_back(next);
            figure.lanes[j].id = next++;
        }
        for (size_t i = 0; i < figure.spots.size(); ++i) figure.spots[i].id = next++;
        for (size_t i = 0; i < figure.bands.size(); ++i) figure.bands[i].id = next++;
        for (size_t i = 0; i < figure.spots.size(); ++i) {
            for (size_t j = 0; j < mapOld.size(); ++j)
                if (figure.spots[i].laneId == mapOld[j]) figure.spots[i].laneId = mapNew[j];
        }
        for (size_t i = 0; i < figure.bands.size(); ++i) {
            for (size_t j = 0; j < mapOld.size(); ++j)
                if (figure.bands[i].laneId == mapOld[j]) figure.bands[i].laneId = mapNew[j];
        }
        figure.nextElementId = next;
    }

    core::ObjectId newId = core::kInvalidObjectId;
    e = addFigure(document, figure, frame, document.defaultLineWidth(), &newId);
    if (!e.ok()) return e;
    if (outId) *outId = newId;
    return core::Error::success();
}

// ---------------------------------------------------------------------------
// Capability reporting
// ---------------------------------------------------------------------------
bool figureDeletionAvailable() { return false; }

const char* figureDeletionStatusText() {
    return "core::Document::removeObject() returns NotImplemented for "
           "Annotation/Path/Group, so core::commands::deleteObjects() rolls back a "
           "mixed selection that contains a T16 figure. T16 does NOT pretend the "
           "command works; see INTERFACES.md section 8 patch 1.";
}

} // namespace experimental
} // namespace r62
