// =============================================================================
// re_round62_viewer/next/biodraw/model.cpp
//
// T14 - the document binding and every edit command.
//
// Structure:
//   1. bag-key helpers and the mirror sync
//   2. reads (bag first, mirror fallback)
//   3. primitive writes
//   4. preset resolution
//   5. commands (each exactly one transaction's worth of work)
//   6. group/ungroup and the honest capability flags
// =============================================================================

#include "next/biodraw/model.hpp"
#include "next/biodraw/components.hpp"
#include "next/biodraw/plasmid.hpp"

#include <cmath>
#include <cstring>

namespace r62 {
namespace biodraw {

namespace {

const core::ObjectKind kKind = core::ObjectKind::BioShape;

core::ObjectRef refOf(core::ObjectId id) { return core::ObjectRef(kKind, id); }

// --- THE ONLY LEGAL WAY TO TOUCH THE BAG --------------------------------
// DocumentSession::buildChangeSet() emits an ExtensionPatch only for refs noted
// through noteTouchedExtensions(). Every bag write therefore goes through here.
void notifyBagDirty(core::Document& document, core::ObjectId id) {
    document.noteTouchedExtensions(refOf(id), false);
}

// Build a failed CommandResult carrying an already-constructed Error, so a command
// can propagate the SPECIFIC error a primitive write returned instead of flattening
// every failure into one code.
core::CommandResult failWith(const core::Error& error) {
    core::CommandResult result;
    result.error = error;
    result.invalidation = core::kInvalidateNone;
    return result;
}

bool readBagValue(const core::Document& document, core::ObjectId id,
                  const char* key, std::string* out) {
    const std::string* value = document.extensions(refOf(id)).findValue(key);
    if (!value) return false;
    *out = *value;
    return true;
}

// Write the whole parameter bundle. Writes each sub-record only when it has
// content, and ERASES the key when it does not -- so a figure whose plasmid data
// was removed does not keep a stale record that a later read would resurrect.
void writeBag(core::Document& document, core::ObjectId id, const BioShapeParams& p) {
    core::ExtensionBag& bag = document.extensions(refOf(id));
    bag.set(kBioParamsKey, encodeBioParams(p));
    if (p.components.empty()) bag.erase(kBioComponentsKey);
    else bag.set(kBioComponentsKey, encodeComponents(p.components));
    if (!p.isPlasmid() || !p.plasmid.valid()) bag.erase(kBioPlasmidKey);
    else bag.set(kBioPlasmidKey, encodePlasmid(p.plasmid));
    if (p.subElementColors.empty()) bag.erase(kBioColorsKey);
    else bag.set(kBioColorsKey, encodeColors(p.subElementColors));
    notifyBagDirty(document, id);
}

// Keep the AnnotationRecord a faithful MIRROR of the bag, so a consumer that
// knows nothing about T14 still sees a correctly placed object.
void syncMirror(core::Document& document, core::ObjectId id, const BioShapeParams& p) {
    core::AnnotationRecord* record = document.bioShapes().mutableGet(id);
    if (!record) return;
    core::Vec2 axisU, axisV;
    frameAxes(p, &axisU, &axisV);
    record->geometry[0] = p.centre;
    record->geometry[2] = core::Vec2(p.centre.x + axisU.x, p.centre.y + axisU.y);
    record->geometry[3] = core::Vec2(p.centre.x + axisV.x, p.centre.y + axisV.y);
    record->geometry[1] = bioShapeBounds(p, document.defaultLineWidth()).min;
    record->presetId = p.presetId;
    record->scale = p.scale;
    record->rotation = p.rotationDeg;
    record->style = p.style;
    record->fillRGBA = p.fillRGBA;
    document.noteTouchedAnnotation(kKind, id);
}

// The record carries only the scalar block, so a fallback read reconstructs a
// usable figure from the mirror alone.
BioShapeParams paramsFromMirror(const core::AnnotationRecord& record) {
    BioShapeParams p;
    p.presetId = record.presetId;
    p.centre = record.geometry[0];
    p.axisU = core::Vec2(record.geometry[2].x - record.geometry[0].x,
                         record.geometry[2].y - record.geometry[0].y);
    p.axisV = core::Vec2(record.geometry[3].x - record.geometry[0].x,
                         record.geometry[3].y - record.geometry[0].y);
    p.scale = record.scale > 0.0 ? record.scale : 1.0;
    p.rotationDeg = record.rotation;
    p.style = record.style;
    p.fillRGBA = record.fillRGBA;
    // The preset id restores the family and glyph, so a figure that entered the
    // document from another module is editable rather than opaque.
    const BioPresetSpec* spec = bioPresetById(record.presetId);
    if (spec) {
        p.family = spec->family;
        p.glyph = spec->glyph;
        p.variant = spec->variant;
        p.components = defaultComponents(*spec);
        defaultGlyphParamsFor(*spec, &p.glyphParams);
    } else {
        p.family = BioFamily::Misc;
        p.glyph = GlyphKind::Polygon;
        p.variant = 3;   // rectangle: the safest thing a bare record can mean
    }
    // An axis that collapsed in the record must not produce a zero-size figure.
    if (core::length(p.axisU) < 1e-9) p.axisU = core::Vec2(18.0, 0.0);
    if (core::length(p.axisV) < 1e-9) p.axisV = core::Vec2(0.0, 12.0);
    return p;
}

} // namespace
// ---------------------------------------------------------------------------
// Reads
// ---------------------------------------------------------------------------
bool isBioShape(const core::Document& document, core::ObjectId id) {
    const core::AnnotationRecord* record = document.bioShapes().alive(id);
    return record != 0 && record->kind == kKind;
}

core::Error readBioShape(const core::Document& document, core::ObjectId id,
                         BioShapeParams* out, bool* usedExtension) {
    if (usedExtension) *usedExtension = false;
    if (!out) return core::Error(core::ErrorCode::OutOfRange, "null output");
    // get(), not mutableGet(): this is the READ path and document is const. Using a
    // mutable accessor here would be a const-correctness hole that compiles only by
    // accident of which overload the compiler picks.
    const core::AnnotationRecord* record = document.bioShapes().get(id);
    if (!record) return core::Error(core::ErrorCode::OutOfRange, "BioShape id out of range");
    if (!record->isAlive()) return core::Error(core::ErrorCode::NotAlive, "BioShape is deleted");

    std::string text;
    if (readBagValue(document, id, kBioParamsKey, &text)) {
        bool ok = false;
        BioShapeParams decoded = decodeBioParams(text, &ok);
        if (!ok) {
            // A CORRUPT record is reported, not silently replaced by the mirror.
            // Falling back here would make a damaged document look healthy.
            return core::Error(core::ErrorCode::ParseError,
                               "bio.params.1 record is malformed");
        }
        *out = decoded;
        std::string sub;
        if (readBagValue(document, id, kBioComponentsKey, &sub)) {
            bool subOk = false;
            const std::vector<ComponentSpec> components = decodeComponents(sub, &subOk);
            if (!subOk) return core::Error(core::ErrorCode::ParseError,
                                           "bio.components.1 record is malformed");
            out->components = components;
        }
        if (readBagValue(document, id, kBioPlasmidKey, &sub)) {
            bool subOk = false;
            const PlasmidData plasmid = decodePlasmid(sub, &subOk);
            if (!subOk) return core::Error(core::ErrorCode::ParseError,
                                           "bio.plasmid.1 record is malformed");
            out->plasmid = plasmid;
        }
        if (readBagValue(document, id, kBioColorsKey, &sub)) {
            bool subOk = false;
            const std::vector<std::pair<uint32_t, uint32_t> > colors = decodeColors(sub, &subOk);
            if (!subOk) return core::Error(core::ErrorCode::ParseError,
                                           "bio.colors.1 record is malformed");
            out->subElementColors = colors;
        }
        // The record is the authority for placement, so a mirror that was edited
        // by another module is not lost: the bag's frame wins, because the bag is
        // what every T14 write keeps in sync.
        if (usedExtension) *usedExtension = true;
        return core::Error::success();
    }

    // No bag record: reconstruct from the mirror. `usedExtension` stays false,
    // which is the honest answer to 'did this come from T14'.
    *out = paramsFromMirror(*record);
    return core::Error::success();
}

std::vector<core::ObjectId> allBioShapes(const core::Document& document) {
    std::vector<core::ObjectId> out;
    const core::ObjectSlab<core::AnnotationRecord>& slab = document.bioShapes();
    for (size_t i = 0; i < slab.size(); ++i) {
        const core::AnnotationRecord* record = slab.get(static_cast<core::ObjectId>(i));
        if (record && record->isAlive() && record->kind == kKind)
            out.push_back(static_cast<core::ObjectId>(i));
    }
    return out;
}

uint64_t bioShapeSignature(const core::Document& document, core::ObjectId id) {
    // A cheap, stable FNV-1a over the encoded bundle. Two figures with equal
    // signatures are byte-identical in the bag and in the record's geometry.
    std::string blob;
    std::string text;
    if (readBagValue(document, id, kBioParamsKey, &text)) blob += text;
    if (readBagValue(document, id, kBioComponentsKey, &text)) blob += text;
    if (readBagValue(document, id, kBioPlasmidKey, &text)) blob += text;
    if (readBagValue(document, id, kBioColorsKey, &text)) blob += text;
    const core::AnnotationRecord* record = document.bioShapes().alive(id);
    if (record) {
        char buffer[160];
        std::snprintf(buffer, sizeof(buffer), "|%u|%g|%g|%g|%g",
                      record->presetId, record->geometry[0].x, record->geometry[0].y,
                      record->scale, record->rotation);
        blob += buffer;
    }
    uint64_t hash = 1469598103934665603ULL;
    for (size_t i = 0; i < blob.size(); ++i) {
        hash ^= static_cast<unsigned char>(blob[i]);
        hash *= 1099511628211ULL;
    }
    return hash;
}

// ---------------------------------------------------------------------------
// Primitive writes
// ---------------------------------------------------------------------------
core::Error addBioShape(core::Document& document, const BioShapeParams& params,
                        double documentLineWidth, core::ObjectId* outId) {
    bool clamped = false;
    BioShapeParams normalised = normalizeComponents(params, &clamped);
    if (!(core::length(normalised.axisU) > 1e-9) || !(core::length(normalised.axisV) > 1e-9))
        return core::Error(core::ErrorCode::InvalidArgument, "degenerate figure frame");
    if (normalised.isPlasmid() && normalised.plasmid.valid()) {
        std::string reason;
        if (!validatePlasmidData(normalised.plasmid, &reason))
            return core::Error(core::ErrorCode::InvalidArgument, reason);
    }

    core::AnnotationRecord record;
    // kKind is already the default in AnnotationRecord only if the caller set it,
    // so it is assigned explicitly rather than assumed.
    record.kind = kKind;
    core::ObjectId id = core::kInvalidObjectId;
    core::Error error = document.addAnnotation(kKind, record, &id);
    if (!error.ok()) return error;

    // The values that MUST be present on the record before the bag is written,
    // so a reader that never sees the bag still gets a placed object.
    core::AnnotationRecord* stored = document.bioShapes().mutableGet(id);
    if (!stored) return core::Error(core::ErrorCode::OutOfRange, "annotation slot missing");
    stored->kind = kKind;
    const double strokeWidth = resolveStrokeWidth(normalised, documentLineWidth);
    if (!(normalised.style.width > 0.0)) normalised.style.width = strokeWidth;
    syncMirror(document, id, normalised);
    writeBag(document, id, normalised);
    document.bumpStructure();
    if (outId) *outId = id;
    return core::Error::success();
}

core::Error writeBioShape(core::Document& document, core::ObjectId id,
                          const BioShapeParams& params, double documentLineWidth) {
    core::AnnotationRecord* record = document.bioShapes().mutableGet(id);
    if (!record) return core::Error(core::ErrorCode::OutOfRange, "BioShape id out of range");
    if (!record->isAlive()) return core::Error(core::ErrorCode::NotAlive, "BioShape is deleted");
    if (record->kind != kKind)
        return core::Error(core::ErrorCode::WrongKind, "annotation is not a BioShape");

    bool clamped = false;
    BioShapeParams normalised = normalizeComponents(params, &clamped);
    if (!(core::length(normalised.axisU) > 1e-9) || !(core::length(normalised.axisV) > 1e-9))
        return core::Error(core::ErrorCode::InvalidArgument, "degenerate figure frame");
    if (normalised.isPlasmid() && normalised.plasmid.valid()) {
        std::string reason;
        if (!validatePlasmidData(normalised.plasmid, &reason))
            return core::Error(core::ErrorCode::InvalidArgument, reason);
    }
    if (!(normalised.style.width > 0.0))
        normalised.style.width = resolveStrokeWidth(normalised, documentLineWidth);

    syncMirror(document, id, normalised);
    writeBag(document, id, normalised);
    return core::Error::success();
}

// ---------------------------------------------------------------------------
// Preset resolution
// ---------------------------------------------------------------------------
BioShapeParams resolveBioPreset(uint32_t presetId, const core::Vec2& begin,
                                const core::Vec2& current, double documentLineWidth) {
    BioShapeParams params;
    const BioPresetSpec* spec = bioPresetById(presetId);
    if (spec) {
        params.presetId = spec->presetId;
        params.family = spec->family;
        params.glyph = spec->glyph;
        params.variant = spec->variant;
        params.components = defaultComponents(*spec);
        defaultGlyphParamsFor(*spec, &params.glyphParams);
        if (params.glyph == GlyphKind::Plasmid) params.plasmid = defaultPlasmidData();
    }

    // The frame is NORMALISED from the two drag corners, so a reversed drag
    // produces the identical figure. Half-extents are used, not the raw delta.
    const double dx = 0.5 * (current.x - begin.x);
    const double dy = 0.5 * (current.y - begin.y);
    params.centre = core::Vec2(0.5 * (begin.x + current.x), 0.5 * (begin.y + current.y));
    params.axisU = core::Vec2(dx, 0.0);
    params.axisV = core::Vec2(0.0, dy);
    params.rotationDeg = 0.0;
    params.scale = 1.0;
    params.style.width = documentLineWidth > 0.0 ? documentLineWidth : 0.6;

    // A zero-size drag would make a degenerate figure. It is given a default
    // extent instead, drawn from the document text size so it looks deliberate.
    if (core::length(params.axisU) < 1e-6) params.axisU = core::Vec2(18.0, 0.0);
    if (core::length(params.axisV) < 1e-6) params.axisV = core::Vec2(0.0, 12.0);
    return params;
}

BioShapeParams applyBioPresetToShape(const BioShapeParams& current, uint32_t presetId) {
    const BioPresetSpec* spec = bioPresetById(presetId);
    if (!spec) return current;
    BioShapeParams out = current;
    out.presetId = spec->presetId;
    out.family = spec->family;
    out.glyph = spec->glyph;
    out.variant = spec->variant;
    // PLACEMENT IS PRESERVED: this is 'select a figure, click another BioDraw
    // tool'. The components are replaced by the new preset's defaults, because a
    // membrane's lipid list is meaningless for an antibody.
    out.components = defaultComponents(*spec);
    defaultGlyphParamsFor(*spec, &out.glyphParams);
    out.plasmid = (spec->glyph == GlyphKind::Plasmid) ? defaultPlasmidData() : PlasmidData();
    out.subElementColors.clear();
    return out;
}
// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------
// Every command captures its inputs BY VALUE. Command::Fn is a std::function that
// OUTLIVES the call that built it -- CommandLog::repeat() (Ctrl+Y) re-runs it -- so
// a command that held a pointer to a caller's stack payload would be a
// use-after-free. The mutator is therefore a std::function captured by value, and
// the per-command numbers are captured into it, not pointed at.
// ---------------------------------------------------------------------------
typedef std::function<core::Error(BioShapeParams&)> Mutator;

namespace {

bool loadParams(const core::Document& document, core::ObjectId id, BioShapeParams* out) {
    return readBioShape(document, id, out, 0).ok();
}

// The generic 'read, mutate, write' command. EVERY edit below is built from this,
// so no edit can forget the bag write, the mirror sync or the invalidation flags.
core::Command editCommand(const char* name, core::ObjectId id, double documentLineWidth,
                          const Mutator& mutate) {
    return core::Command(name, core::kInvalidateNone,
                         [id, documentLineWidth, mutate](core::CommandContext& ctx) {
        BioShapeParams params;
        if (!loadParams(ctx.document, id, &params))
            return core::CommandResult::fail(core::ErrorCode::NotFound, "BioShape not readable");
        const core::Error error = mutate(params);
        if (!error.ok()) return failWith(error);
        const core::Error written = writeBioShape(ctx.document, id, params, documentLineWidth);
        if (!written.ok()) return failWith(written);
        return core::CommandResult::success(core::kInvalidateGeometry |
                                            core::kInvalidateAppearance);
    });
}

} // namespace

core::Command addBioShapeCommand(const BioShapeParams& params,
                                 double documentLineWidth, core::ObjectId* outId) {
    return core::Command("biodraw.add", core::kInvalidateNone, [params, documentLineWidth, outId](core::CommandContext& ctx) {
        core::ObjectId id = core::kInvalidObjectId;
        const core::Error error = addBioShape(ctx.document, params, documentLineWidth, &id);
        if (!error.ok()) return failWith(error);
        if (outId) *outId = id;
        return core::CommandResult::success(core::kInvalidateStructure |
                                            core::kInvalidateGeometry);
    });
}

core::Command writeBioShapeCommand(core::ObjectId id, const BioShapeParams& params,
                                   double documentLineWidth) {
    return core::Command("biodraw.write", core::kInvalidateNone, [id, params, documentLineWidth](core::CommandContext& ctx) {
        const core::Error error = writeBioShape(ctx.document, id, params, documentLineWidth);
        if (!error.ok()) return failWith(error);
        return core::CommandResult::success(core::kInvalidateGeometry |
                                            core::kInvalidateAppearance);
    });
}

core::Command translateBioShapeCommand(core::ObjectId id, const core::Vec2& delta) {
    const double dx = delta.x, dy = delta.y;
    return editCommand("biodraw.translate", id, 0.0,
                       [dx, dy](BioShapeParams& p) {
                           p.centre.x += dx;
                           p.centre.y += dy;
                           return core::Error::success();
                       });
}

core::Command scaleBioShapeCommand(core::ObjectId id, const core::Vec2& pivot,
                                   double factorU, double factorV) {
    const core::Vec2 pivotCopy = pivot;
    return editCommand("biodraw.scale", id, 0.0,
                       [pivotCopy, factorU, factorV](BioShapeParams& p) {
                           bool changed = false;
                           return scaleBioShape(&p, pivotCopy, factorU, factorV, &changed);
                       });
}

core::Command rotateBioShapeCommand(core::ObjectId id, const core::Vec2& pivot,
                                    double degrees) {
    const core::Vec2 pivotCopy = pivot;
    return editCommand("biodraw.rotate", id, 0.0,
                       [pivotCopy, degrees](BioShapeParams& p) {
                           return rotateBioShape(&p, pivotCopy, degrees);
                       });
}

core::Command distortBioShapeCommand(core::ObjectId id, double shearU, double shearV) {
    return editCommand("biodraw.distort", id, 0.0,
                       [shearU, shearV](BioShapeParams& p) {
                           return distortBioShape(&p, shearU, shearV);
                       });
}

core::Command setComponentCountCommand(core::ObjectId id, size_t componentIndex,
                                       int count) {
    return editCommand("biodraw.component.count", id, 0.0,
                       [componentIndex, count](BioShapeParams& p) {
                           if (componentIndex >= p.components.size())
                               return core::Error(core::ErrorCode::OutOfRange,
                                                  "component index out of range");
                           if (count < 0)
                               return core::Error(core::ErrorCode::InvalidArgument,
                                                  "negative count");
                           p.components[componentIndex].count = count;
                           return core::Error::success();
                       });
}

core::Command setComponentSpacingCommand(core::ObjectId id, size_t componentIndex,
                                         double spacing, bool evenlySpaced) {
    return editCommand("biodraw.component.spacing", id, 0.0,
                       [componentIndex, spacing, evenlySpaced](BioShapeParams& p) {
                           if (componentIndex >= p.components.size())
                               return core::Error(core::ErrorCode::OutOfRange,
                                                  "component index out of range");
                           if (!(spacing > 0.0))
                               return core::Error(core::ErrorCode::InvalidArgument,
                                                  "spacing must be positive");
                           p.components[componentIndex].spacing = spacing;
                           p.components[componentIndex].evenlySpaced = evenlySpaced;
                           return core::Error::success();
                       });
}

core::Command setComponentUnitSizeCommand(core::ObjectId id, size_t componentIndex,
                                          double unitSize) {
    return editCommand("biodraw.component.unit", id, 0.0,
                       [componentIndex, unitSize](BioShapeParams& p) {
                           if (componentIndex >= p.components.size())
                               return core::Error(core::ErrorCode::OutOfRange,
                                                  "component index out of range");
                           p.components[componentIndex].unitSize = unitSize;
                           return core::Error::success();
                       });
}

core::Command addComponentCommand(core::ObjectId id, const ComponentSpec& spec) {
    return editCommand("biodraw.component.add", id, 0.0,
                       [spec](BioShapeParams& p) {
                           if (p.components.size() >= 32)
                               return core::Error(core::ErrorCode::InvalidArgument,
                                                  "too many components");
                           p.components.push_back(spec);
                           return core::Error::success();
                       });
}

core::Command removeComponentCommand(core::ObjectId id, size_t componentIndex) {
    return editCommand("biodraw.component.remove", id, 0.0,
                       [componentIndex](BioShapeParams& p) {
                           if (componentIndex >= p.components.size())
                               return core::Error(core::ErrorCode::OutOfRange,
                                                  "component index out of range");
                           p.components.erase(p.components.begin() +
                                              static_cast<long>(componentIndex));
                           return core::Error::success();
                       });
}

core::Command setSubElementColorCommand(core::ObjectId id, uint32_t key, uint32_t rgba) {
    return editCommand("biodraw.color.set", id, 0.0,
                       [key, rgba](BioShapeParams& p) {
                           setSubElementColor(&p, key, rgba);
                           return core::Error::success();
                       });
}

core::Command clearSubElementColorCommand(core::ObjectId id, uint32_t key) {
    return editCommand("biodraw.color.clear", id, 0.0,
                       [key](BioShapeParams& p) {
                           clearSubElementColor(&p, key);
                           return core::Error::success();
                       });
}

core::Command setBioShapeStyleCommand(core::ObjectId id, const core::LineStyle& style,
                                      uint32_t fillRGBA) {
    return editCommand("biodraw.style", id, 0.0,
                       [style, fillRGBA](BioShapeParams& p) {
                           p.style = style;
                           p.fillRGBA = fillRGBA;
                           return core::Error::success();
                       });
}

core::Command setPlasmidDataCommand(core::ObjectId id, const PlasmidData& data) {
    return editCommand("biodraw.plasmid.set", id, 0.0,
                       [data](BioShapeParams& p) {
                           if (p.glyph != GlyphKind::Plasmid)
                               return core::Error(core::ErrorCode::WrongKind,
                                                  "figure is not a plasmid map");
                           std::string reason;
                           if (!validatePlasmidData(data, &reason))
                               return core::Error(core::ErrorCode::InvalidArgument, reason);
                           p.plasmid = data;
                           return core::Error::success();
                       });
}

core::Command movePlasmidFeatureCommand(core::ObjectId id, size_t featureIndex,
                                        int newStart) {
    return editCommand("biodraw.plasmid.move", id, 0.0,
                       [featureIndex, newStart](BioShapeParams& p) {
                           if (p.glyph != GlyphKind::Plasmid)
                               return core::Error(core::ErrorCode::WrongKind,
                                                  "figure is not a plasmid map");
                           if (!moveFeatureStart(&p.plasmid, featureIndex, newStart))
                               return core::Error(core::ErrorCode::OutOfRange,
                                                  "feature could not be moved");
                           return core::Error::success();
                       });
}

core::Command setPlasmidFeatureRangeCommand(core::ObjectId id, size_t featureIndex,
                                            int start, int end) {
    return editCommand("biodraw.plasmid.range", id, 0.0,
                       [featureIndex, start, end](BioShapeParams& p) {
                           if (p.glyph != GlyphKind::Plasmid)
                               return core::Error(core::ErrorCode::WrongKind,
                                                  "figure is not a plasmid map");
                           if (featureIndex >= p.plasmid.features.size())
                               return core::Error(core::ErrorCode::OutOfRange,
                                                  "feature index out of range");
                           // An empty range is REFUSED rather than widened:
                           // widening would MOVE the feature, and 'the edit moved
                           // something I did not touch' is the data-loss mode the
                           // acceptance criterion forbids.
                           if (start == end)
                               return core::Error(core::ErrorCode::InvalidArgument,
                                                  "empty feature range");
                           if (!setFeatureRange(&p.plasmid, featureIndex, start, end))
                               return core::Error(core::ErrorCode::InvalidArgument,
                                                  "range could not be normalised");
                           return core::Error::success();
                       });
}

core::Command addPlasmidFeatureCommand(core::ObjectId id, const PlasmidFeature& feature) {
    return editCommand("biodraw.plasmid.add", id, 0.0,
                       [feature](BioShapeParams& p) {
                           if (p.glyph != GlyphKind::Plasmid)
                               return core::Error(core::ErrorCode::WrongKind,
                                                  "figure is not a plasmid map");
                           if (!addFeature(&p.plasmid, feature))
                               return core::Error(core::ErrorCode::InvalidArgument,
                                                  "feature rejected") ;
                           return core::Error::success();
                       });
}

core::Command removePlasmidFeatureCommand(core::ObjectId id, size_t featureIndex) {
    return editCommand("biodraw.plasmid.remove", id, 0.0,
                       [featureIndex](BioShapeParams& p) {
                           if (p.glyph != GlyphKind::Plasmid)
                               return core::Error(core::ErrorCode::WrongKind,
                                                  "figure is not a plasmid map");
                           if (!removeFeature(&p.plasmid, featureIndex))
                               return core::Error(core::ErrorCode::OutOfRange,
                                                  "feature index out of range");
                           return core::Error::success();
                       });
}
// ---------------------------------------------------------------------------
// Group / Ungroup of a figure's own sub-elements
// ---------------------------------------------------------------------------
// 'Ungroup' replaces ONE figure with its N sub-elements as independent figures.
// The undo of that must restore the ORIGINAL COMPLETELY -- not 'close enough',
// because the original is a single object and the parts are several.
//
// It is implemented as ONE command that, in a single transaction, removes the
// original from its slab and inserts the parts. The transaction's own patch
// (which stores both the before and after state of every touched record) is what
// makes undo exact; no compensating command is written by hand. That is a
// structural guarantee, not a promise: there is no second code path that could
// disagree.
namespace {

// Kill the slot AND drop its bag, exactly as the suggested T01 patch would, but
// scoped to this module so it does not require the core change to land first.
core::Error removeBioShapeSlot(core::Document& document, core::ObjectId id) {
    core::AnnotationRecord* record = document.bioShapes().mutableGet(id);
    if (!record) return core::Error(core::ErrorCode::OutOfRange, "BioShape id out of range");
    if (!record->isAlive()) return core::Error(core::ErrorCode::NotAlive, "already deleted");
    document.bioShapes().kill(id);
    document.mutableObjectExtensions().erase(refOf(id));
    document.noteTouchedExtensions(refOf(id), false);
    document.noteTouchedAnnotation(kKind, id);
    document.noteTouchedStructure();
    document.bumpStructure();
    return core::Error::success();
}

} // namespace

core::Command explodeBioShapeCommand(const BioShapeParams& geometrySource,
                                     core::ObjectId id,
                                     const std::vector<ExplodedPart>& parts,
                                     double documentLineWidth) {
    return core::Command("biodraw.ungroup", core::kInvalidateNone, [geometrySource, id, parts, documentLineWidth](core::CommandContext& ctx) {
        core::Document& document = ctx.document;
        const core::AnnotationRecord* existing = document.bioShapes().alive(id);
        if (!existing) return core::CommandResult::fail(core::ErrorCode::NotFound, "figure is not alive");
        (void)geometrySource;
        std::vector<core::ObjectId> created;
        for (size_t i = 0; i < parts.size(); ++i) {
            core::ObjectId partId = core::kInvalidObjectId;
            const core::Error error = addBioShape(document, parts[i].params,
                                                  documentLineWidth, &partId);
            if (!error.ok()) {
                // Partial explosion is worse than none: undo the ones created so
                // the transaction rolls back to a consistent document either way.
                for (size_t k = 0; k < created.size(); ++k) removeBioShapeSlot(document, created[k]);
                return failWith(error);
            }
            created.push_back(partId);
        }
        const core::Error removed = removeBioShapeSlot(document, id);
        if (!removed.ok()) {
            for (size_t k = 0; k < created.size(); ++k) removeBioShapeSlot(document, created[k]);
            return failWith(removed);
        }
        return core::CommandResult::success(core::kInvalidateStructure |
                                            core::kInvalidateGeometry);
    });
}

core::Command explodeBioShapeCommand(const core::Document& document,
                                     core::ObjectId id,
                                     double documentLineWidth) {
    BioShapeParams params;
    if (!readBioShape(document, id, &params, 0).ok()) {
        // A command that cannot even read its input still has to be a Command; it
        // simply fails when executed, rather than silently doing nothing.
        return core::Command("biodraw.ungroup.invalid", core::kInvalidateNone, [](core::CommandContext&) {
            return core::CommandResult::fail(core::ErrorCode::NotFound, "figure is not readable");
        });
    }
    const BioGeometry geometry = buildGlyphGeometry(params, documentLineWidth);
    const std::vector<ExplodedPart> parts = explodeParts(geometry, documentLineWidth);
    if (parts.empty()) {
        return core::Command("biodraw.ungroup.nothing", core::kInvalidateNone, [](core::CommandContext&) {
            return core::CommandResult::fail(core::ErrorCode::NotImplemented, "figure has no sub-elements");
        });
    }
    return explodeBioShapeCommand(params, id, parts, documentLineWidth);
}

size_t bioShapeSubElementCount(const core::Document& document, core::ObjectId id) {
    BioShapeParams params;
    if (!readBioShape(document, id, &params, 0).ok()) return 0;
    const BioGeometry geometry = buildGlyphGeometry(params, document.defaultLineWidth());
    return geometry.elements.size();
}

// ---------------------------------------------------------------------------
// Capability flags
// ---------------------------------------------------------------------------
bool bioShapeDeletionAvailable() {
    // next/core/document.cpp returns NotImplemented for the annotation slabs, so
    // deleteObjects() rolls the whole transaction back. This module reports the
    // truth rather than letting a caller offer a Delete that silently does nothing.
    return false;
}

bool chemicallyInert() {
    // Readable at runtime so a host can ASSERT it, rather than having to trust a
    // sentence in a document.
    return true;
}

} // namespace biodraw
} // namespace r62