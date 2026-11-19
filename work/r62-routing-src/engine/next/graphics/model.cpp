#include "next/graphics/model.hpp"

#include <algorithm>
#include <cmath>

namespace r62 {
namespace graphics {
namespace {

// Mark an object's extension bag dirty so the change set carries it.
//
// This is NOT optional: DocumentSession::buildChangeSet() emits an ExtensionPatch
// only for refs that were explicitly noted, so without this call a parameter
// written into the bag is invisible to undo/redo and to the transaction
// rollback digest - the record restores, the bag does not, and readShape() then
// returns the pre-edit parameters. `Document::noteTouchedExtensions` is a public
// mutator notice (core/document.hpp), and outside a transaction it is a single
// predictable branch.
//
// The previous implementation routed this through `Document::setLineStyle`
// because the notice was believed to be private. setLineStyle notes the
// *annotation* record, not the bag, so it did not work for Arrow/Bracket/Table;
// the undo tests caught it.
void notifyBagDirty(core::Document& document, core::ObjectKind kind, core::ObjectId id) {
    document.noteTouchedExtensions(core::ObjectRef(kind, id), false);
}

core::Error fillMirror(const ShapeParams& p, double documentLineWidth,
                       core::AnnotationRecord* rec) {
    const ShapeFrame& f = p.frame;
    core::Vec2 c[4];
    f.corners(c);
    rec->geometry[0] = f.centre;
    rec->geometry[1] = core::Vec2(std::min(std::min(c[0].x, c[1].x), std::min(c[2].x, c[3].x)),
                                  std::min(std::min(c[0].y, c[1].y), std::min(c[2].y, c[3].y)));
    rec->geometry[2] = core::Vec2(f.centre.x + f.axisU.x, f.centre.y + f.axisU.y);
    rec->geometry[3] = core::Vec2(f.centre.x + f.axisV.x, f.centre.y + f.axisV.y);
    rec->presetId = p.presetId;
    rec->scale = p.scale;
    rec->rotation = 0.0;   // rotation is carried by the axis vectors
    rec->style = p.style;
    rec->style.width = p.style.width > 0.0 ? p.style.width : documentLineWidth;
    rec->fillRGBA = p.fillRGBA;
    rec->kind = core::ObjectKind::Arrow;
    return core::Error::success();
}

core::Error fillMirror(const BracketParams& p, double documentLineWidth,
                       core::AnnotationRecord* rec) {
    const ShapeFrame& f = p.frame;
    core::Vec2 c[4];
    f.corners(c);
    rec->geometry[0] = f.centre;
    rec->geometry[1] = core::Vec2(std::min(std::min(c[0].x, c[1].x), std::min(c[2].x, c[3].x)),
                                  std::min(std::min(c[0].y, c[1].y), std::min(c[2].y, c[3].y)));
    rec->geometry[2] = core::Vec2(f.centre.x + f.axisU.x, f.centre.y + f.axisU.y);
    rec->geometry[3] = core::Vec2(f.centre.x + f.axisV.x, f.centre.y + f.axisV.y);
    rec->presetId = p.presetId;
    rec->scale = p.scale;
    rec->rotation = 0.0;
    rec->style = p.style;
    rec->style.width = p.style.width > 0.0 ? p.style.width : documentLineWidth;
    rec->kind = core::ObjectKind::Bracket;
    return core::Error::success();
}

// Rebuild ShapeParams from the mirror alone. Used when the bag is missing, so a
// shape imported by a module that never heard of T11 is still drawable.
ShapeParams fromMirror(const core::AnnotationRecord& rec) {
    ShapeParams p;
    p.presetId = rec.presetId;
    const ShapePresetSpec* spec = shapePresetById(rec.presetId);
    if (spec) {
        p.form = spec->form;
        p.appearance = spec->appearance;
        p.line = spec->line;
        p.arcSweepDeg = spec->arcSweepDeg;
    } else {
        p.form = ShapeForm::Rectangle;
        p.appearance = rec.fillRGBA != 0u ? ShapeAppearance::Solid : ShapeAppearance::Hollow;
    }
    p.frame.centre = rec.geometry[0];
    p.frame.axisU = core::Vec2(rec.geometry[2].x - rec.geometry[0].x,
                               rec.geometry[2].y - rec.geometry[0].y);
    p.frame.axisV = core::Vec2(rec.geometry[3].x - rec.geometry[0].x,
                               rec.geometry[3].y - rec.geometry[0].y);
    p.scale = rec.scale > 0.0 ? rec.scale : 1.0;
    p.style = rec.style;
    p.fillRGBA = rec.fillRGBA;
    if (p.form == ShapeForm::Arc) {
        p.arcStartDeg = 180.0;
        p.arcSpanDeg = static_cast<double>(p.arcSweepDeg > 0 ? p.arcSweepDeg : 90);
    }
    return p;
}

BracketParams bracketFromMirror(const core::AnnotationRecord& rec) {
    BracketParams p;
    p.presetId = rec.presetId;
    const BracketPresetSpec* spec = bracketPresetById(rec.presetId);
    if (spec) {
        p.family = spec->family;
        p.side = spec->paired ? BracketSide::Pair : BracketSide::Left;
    }
    p.frame.centre = rec.geometry[0];
    p.frame.axisU = core::Vec2(rec.geometry[2].x - rec.geometry[0].x,
                               rec.geometry[2].y - rec.geometry[0].y);
    p.frame.axisV = core::Vec2(rec.geometry[3].x - rec.geometry[0].x,
                               rec.geometry[3].y - rec.geometry[0].y);
    p.scale = rec.scale > 0.0 ? rec.scale : 1.0;
    p.style = rec.style;
    return p;
}

// The canonical frame for a closed form dragged from `begin` to `current`.
// Both axes are non-negative, so a reversed drag produces exactly the same frame
// as the equivalent forward drag, and therefore exactly the same contour. This is
// the "reversed drag is correct" requirement, satisfied by construction.
ShapeFrame boxFrame(const core::Vec2& begin, const core::Vec2& current) {
    ShapeFrame f;
    f.centre = core::Vec2((begin.x + current.x) * 0.5, (begin.y + current.y) * 0.5);
    f.axisU = core::Vec2(std::fabs(current.x - begin.x) * 0.5, 0.0);
    f.axisV = core::Vec2(0.0, std::fabs(current.y - begin.y) * 0.5);
    return f;
}

// A Line is the DIAGONAL begin->current of the drag rectangle, not the rectangle
// that contains it. axisU is half the displacement, and axisV is a UNIT
// perpendicular of the axis, so shapeOutline() - which uses corners[0] and
// corners[2] - returns exactly begin and current for either drag direction, with
// NO dependence on the stroke width.
//
// The unit length matters. The plausible-looking alternative (axisV = perpendicular
// * strokeWidth/2) double-counts the width: the hit test already adds
// strokeWidth/2 as its band and the rasteriser already strokes the centreline, so
// encoding the width in axisV shifted every line's endpoints by the half width
// perpendicular to the line. The rest of the bridge could not see that, which is
// exactly why it is pinned down here.
ShapeFrame lineFrame(const core::Vec2& begin, const core::Vec2& current, double width) {
    (void)width;
    ShapeFrame f;
    f.centre = core::Vec2((begin.x + current.x) * 0.5, (begin.y + current.y) * 0.5);
    f.axisU = core::Vec2((current.x - begin.x) * 0.5, (current.y - begin.y) * 0.5);
    const double len = core::length(f.axisU);
    if (len > 1e-12) {
        f.axisV = core::Vec2(-f.axisU.y / len, f.axisU.x / len);
    } else {
        f.axisV = core::Vec2(0.0, 1.0);
    }
    return f;
}

} // namespace

// ===========================================================================
// Reads
// ===========================================================================
core::Error readShape(const core::Document& document, core::ObjectId id,
                      ShapeParams* out, bool* usedExtension) {
    if (usedExtension) *usedExtension = false;
    if (!out) return core::Error(core::ErrorCode::InvalidArgument, "null output");
    const core::AnnotationRecord* rec = document.arrows().alive(id);
    if (!rec) return core::Error(core::ErrorCode::NotFound, "No live shape with that id");
    const core::ExtensionBag& bag = document.extensions(core::ObjectRef(core::ObjectKind::Arrow, id));
    const std::string* text = bag.findValue(kShapeParamsKey);
    if (text) {
        bool ok = false;
        const ShapeParams decoded = decodeShapeParams(*text, &ok);
        if (ok) {
            *out = decoded;
            if (usedExtension) *usedExtension = true;
            return core::Error::success();
        }
    }
    *out = fromMirror(*rec);
    return core::Error::success();
}

core::Error readBracket(const core::Document& document, core::ObjectId id,
                        BracketParams* out, bool* usedExtension) {
    if (usedExtension) *usedExtension = false;
    if (!out) return core::Error(core::ErrorCode::InvalidArgument, "null output");
    const core::AnnotationRecord* rec = document.brackets().alive(id);
    if (!rec) return core::Error(core::ErrorCode::NotFound, "No live bracket with that id");
    const core::ExtensionBag& bag = document.extensions(core::ObjectRef(core::ObjectKind::Bracket, id));
    const std::string* text = bag.findValue(kBracketParamsKey);
    if (text) {
        bool ok = false;
        const BracketParams decoded = decodeBracketParams(*text, &ok);
        if (ok) {
            *out = decoded;
            if (usedExtension) *usedExtension = true;
            return core::Error::success();
        }
    }
    *out = bracketFromMirror(*rec);
    return core::Error::success();
}

// ===========================================================================
// Writes
// ===========================================================================
core::Error addShape(core::Document& document, const ShapeParams& params,
                     double documentLineWidth, core::ObjectId* outId) {
    if (!params.frame.centre.finite() || !params.frame.axisU.finite() ||
        !params.frame.axisV.finite())
        return core::Error(core::ErrorCode::InvalidGeometry, "Shape frame must be finite");
    if (!(params.scale > 0.0))
        return core::Error(core::ErrorCode::InvalidArgument, "Shape scale must be positive");
    core::AnnotationRecord rec;
    fillMirror(params, documentLineWidth, &rec);
    core::ObjectId id = core::kInvalidObjectId;
    core::Error e = document.addAnnotation(core::ObjectKind::Arrow, rec, &id);
    if (!e.ok()) return e;
    document.extensions(core::ObjectRef(core::ObjectKind::Arrow, id))
            .set(kShapeParamsKey, encodeShapeParams(params));
    notifyBagDirty(document, core::ObjectKind::Arrow, id);
    if (outId) *outId = id;
    return core::Error::success();
}

core::Error writeShape(core::Document& document, core::ObjectId id,
                       const ShapeParams& params, double documentLineWidth) {
    core::AnnotationRecord* rec = document.arrows().mutableGet(id);
    if (!rec) return core::Error(core::ErrorCode::NotFound, "No shape with that id");
    if (!rec->isAlive()) return core::Error(core::ErrorCode::NotAlive, "Shape is deleted");
    fillMirror(params, documentLineWidth, rec);
    rec->kind = core::ObjectKind::Arrow;
    document.extensions(core::ObjectRef(core::ObjectKind::Arrow, id))
            .set(kShapeParamsKey, encodeShapeParams(params));
    document.noteTouchedAnnotation(core::ObjectKind::Arrow, id);
    notifyBagDirty(document, core::ObjectKind::Arrow, id);
    document.bumpStructure();
    return core::Error::success();
}

core::Error addBracket(core::Document& document, const BracketParams& params,
                       double documentLineWidth, core::ObjectId* outId) {
    if (!params.frame.centre.finite() || !params.frame.axisU.finite() ||
        !params.frame.axisV.finite())
        return core::Error(core::ErrorCode::InvalidGeometry, "Bracket frame must be finite");
    if (!(params.scale > 0.0))
        return core::Error(core::ErrorCode::InvalidArgument, "Bracket scale must be positive");
    for (size_t i = 0; i < params.labels.size(); ++i) {
        if (params.labels[i].text.find(';') != std::string::npos)
            return core::Error(core::ErrorCode::InvalidText,
                               "A bracket label may not contain ';' (record separator)");
    }
    core::AnnotationRecord rec;
    fillMirror(params, documentLineWidth, &rec);
    core::ObjectId id = core::kInvalidObjectId;
    core::Error e = document.addAnnotation(core::ObjectKind::Bracket, rec, &id);
    if (!e.ok()) return e;
    document.extensions(core::ObjectRef(core::ObjectKind::Bracket, id))
            .set(kBracketParamsKey, encodeBracketParams(params));
    notifyBagDirty(document, core::ObjectKind::Bracket, id);
    if (outId) *outId = id;
    return core::Error::success();
}

core::Error writeBracket(core::Document& document, core::ObjectId id,
                         const BracketParams& params, double documentLineWidth) {
    core::AnnotationRecord* rec = document.brackets().mutableGet(id);
    if (!rec) return core::Error(core::ErrorCode::NotFound, "No bracket with that id");
    if (!rec->isAlive()) return core::Error(core::ErrorCode::NotAlive, "Bracket is deleted");
    for (size_t i = 0; i < params.labels.size(); ++i) {
        if (params.labels[i].text.find(';') != std::string::npos)
            return core::Error(core::ErrorCode::InvalidText,
                               "A bracket label may not contain ';' (record separator)");
    }
    fillMirror(params, documentLineWidth, rec);
    rec->kind = core::ObjectKind::Bracket;
    document.extensions(core::ObjectRef(core::ObjectKind::Bracket, id))
            .set(kBracketParamsKey, encodeBracketParams(params));
    document.noteTouchedAnnotation(core::ObjectKind::Bracket, id);
    notifyBagDirty(document, core::ObjectKind::Bracket, id);
    document.bumpStructure();
    return core::Error::success();
}

uint64_t shapeObjectSignature(const core::Document& document, core::ObjectId id) {
    ShapeParams p;
    bool used = false;
    if (!readShape(document, id, &p, &used).ok()) return 0;
    return shapeSignature(p, document.defaultLineWidth());
}

uint64_t bracketObjectSignature(const core::Document& document, core::ObjectId id) {
    BracketParams p;
    bool used = false;
    if (!readBracket(document, id, &p, &used).ok()) return 0;
    return bracketSignature(p, document.defaultLineWidth());
}

// ===========================================================================
// Preset application
// ===========================================================================
ShapeParams resolveShapePreset(uint32_t presetId, const core::Vec2& begin,
                               const core::Vec2& current, double documentLineWidth) {
    ShapeParams p;
    const ShapePresetSpec* spec = shapePresetById(presetId);
    if (!spec) return p;
    p.presetId = presetId;
    p.form = spec->form;
    p.appearance = spec->appearance;
    p.line = spec->line;
    p.arcSweepDeg = spec->arcSweepDeg;
    p.style.width = documentLineWidth > 0.0 ? documentLineWidth : 0.6;
    // Bold stretches the stroke; the flag lives in the LineVariant so it survives
    // a round trip even though the width is what the renderer reads.
    if (spec->line == LineVariant::Bold) p.style.width *= 2.5;
    p.style.dash = (spec->appearance == ShapeAppearance::Dashed ||
                    spec->line == LineVariant::Dashed) ? 1 : 0;
    p.fillRGBA = appearanceFillColor(spec->appearance, 0x80808000u);

    if (spec->form == ShapeForm::Line) {
        p.frame = lineFrame(begin, current, p.style.width);
        // Degenerate click (no drag at all): a default-length horizontal line,
        // which is what a single click with a line tool must produce.
        if (core::length(p.frame.axisU) < 0.01) {
            const double len = documentLineWidth > 0.0 ? documentLineWidth : 14.4;
            p.frame.centre = begin;
            p.frame.axisU = core::Vec2(len * 0.5, 0.0);
            p.frame.axisV = core::Vec2(0.0, 1.0);
        }
        return p;
    }

    p.frame = boxFrame(begin, current);
    if (spec->form == ShapeForm::Circle) {
        const double r = std::max(p.frame.halfWidth(), p.frame.halfHeight());
        p.frame.axisU = core::Vec2(r, 0.0);
        p.frame.axisV = core::Vec2(0.0, r);
    }
    if (spec->form == ShapeForm::Arc) {
        // A drag defines the bounding ellipse (the axes above). The arc is the
        // TOP half opening downward, which is the orientation the palette shows.
        p.arcStartDeg = 180.0;
        p.arcSpanDeg = -static_cast<double>(spec->arcSweepDeg > 0 ? spec->arcSweepDeg : 90);
        if (p.arcSweepDeg == 270) {
            p.arcStartDeg = 135.0;
            p.arcSpanDeg = -270.0;
        }
    }
    return p;
}

ShapeParams applyShapePresetStyle(const ShapeParams& current, uint32_t presetId) {
    const ShapePresetSpec* spec = shapePresetById(presetId);
    if (!spec) return current;
    ShapeParams p = current;
    const ShapeForm oldForm = current.form;
    p.presetId = presetId;
    p.form = spec->form;
    p.appearance = spec->appearance;
    p.line = spec->line;
    p.arcSweepDeg = spec->arcSweepDeg;
    p.style.dash = (spec->appearance == ShapeAppearance::Dashed ||
                    spec->line == LineVariant::Dashed) ? 1 : 0;
    p.style.width = current.style.width;
    p.fillRGBA = appearanceFillColor(spec->appearance, current.fillRGBA);
    if (spec->form == ShapeForm::Circle) {
        const double r = std::max(p.frame.halfWidth(), p.frame.halfHeight());
        p.frame.axisU = core::Vec2(r, 0.0);
        p.frame.axisV = core::Vec2(0.0, r);
    } else if (spec->form == ShapeForm::Arc && oldForm != ShapeForm::Arc) {
        p.arcStartDeg = 180.0;
        p.arcSpanDeg = -static_cast<double>(spec->arcSweepDeg > 0 ? spec->arcSweepDeg : 90);
        if (p.arcSweepDeg == 270) { p.arcStartDeg = 135.0; p.arcSpanDeg = -270.0; }
    }
    return p;
}

bool swapCircleForOval(ShapeParams* params) {
    if (!params) return false;
    if (params->form != ShapeForm::Circle) return false;
    const double w = params->frame.halfWidth();
    const double h = params->frame.halfHeight();
    if (std::fabs(w - h) <= 1e-9) return false;
    const ShapePresetSpec* oval = shapePresetFor(ShapeForm::Oval, params->appearance);
    if (!oval) return false;
    params->form = ShapeForm::Oval;
    params->presetId = oval->presetId;
    return true;
}

BracketParams resolveBracketPreset(uint32_t presetId, const core::Vec2& begin,
                                   const core::Vec2& current, double documentLineWidth) {
    BracketParams p;
    const BracketPresetSpec* spec = bracketPresetById(presetId);
    if (!spec) return p;
    p.presetId = presetId;
    p.family = spec->family;
    p.side = spec->paired ? BracketSide::Pair : BracketSide::Left;
    p.direction = BracketDirection::Vertical;
    p.style.width = documentLineWidth > 0.0 ? documentLineWidth : 0.6;
    if (spec->family == BracketFamily::Dagger) {
        // A dagger is a square glyph, not a span.
        const double s = std::max(std::fabs(current.x - begin.x),
                                  std::fabs(current.y - begin.y)) * 0.5;
        const double r = s > 0.5 ? s : 8.0;
        p.frame.centre = core::Vec2((begin.x + current.x) * 0.5, (begin.y + current.y) * 0.5);
        p.frame.axisU = core::Vec2(r, 0.0);
        p.frame.axisV = core::Vec2(0.0, r);
        return p;
    }
    ShapeFrame f = boxFrame(begin, current);
    // A bracket drawn with a short drag still needs a usable span.
    if (f.halfHeight() < 1.0) f.axisV = core::Vec2(0.0, 1.0);
    if (f.halfWidth() < 1.0)  f.axisU = core::Vec2(1.0, 0.0);
    p.frame = f;
    return p;
}

BracketParams applyBracketPresetStyle(const BracketParams& current, uint32_t presetId) {
    const BracketPresetSpec* spec = bracketPresetById(presetId);
    if (!spec) return current;
    BracketParams p = current;
    p.presetId = presetId;
    p.family = spec->family;
    if (spec->paired) p.side = BracketSide::Pair;
    else if (p.side == BracketSide::Pair) p.side = BracketSide::Left;
    return p;
}

// ===========================================================================
// Geometry edits
// ===========================================================================
core::Error translateShape(core::Document& document, core::ObjectId id,
                           const core::Vec2& delta) {
    if (!delta.finite()) return core::Error(core::ErrorCode::InvalidGeometry, "Delta must be finite");
    ShapeParams p;
    bool used = false;
    core::Error e = readShape(document, id, &p, &used);
    if (!e.ok()) return e;
    p.frame.centre = core::Vec2(p.frame.centre.x + delta.x, p.frame.centre.y + delta.y);
    return writeShape(document, id, p, document.defaultLineWidth());
}

core::Error translateBracket(core::Document& document, core::ObjectId id,
                             const core::Vec2& delta) {
    if (!delta.finite()) return core::Error(core::ErrorCode::InvalidGeometry, "Delta must be finite");
    BracketParams p;
    bool used = false;
    core::Error e = readBracket(document, id, &p, &used);
    if (!e.ok()) return e;
    p.frame.centre = core::Vec2(p.frame.centre.x + delta.x, p.frame.centre.y + delta.y);
    return writeBracket(document, id, p, document.defaultLineWidth());
}

core::Error scaleShape(core::Document& document, core::ObjectId id,
                       const core::Vec2& pivot, double factorU, double factorV,
                       bool* convertedToOval) {
    if (convertedToOval) *convertedToOval = false;
    if (!pivot.finite()) return core::Error(core::ErrorCode::InvalidGeometry, "Pivot must be finite");
    if (!std::isfinite(factorU) || !std::isfinite(factorV))
        return core::Error(core::ErrorCode::InvalidArgument, "Scale factors must be finite");
    ShapeParams p;
    bool used = false;
    core::Error e = readShape(document, id, &p, &used);
    if (!e.ok()) return e;
    // Scale the axes; the centre moves exactly as a point would.
    p.frame.axisU = core::Vec2(p.frame.axisU.x * factorU, p.frame.axisU.y * factorU);
    p.frame.axisV = core::Vec2(p.frame.axisV.x * factorV, p.frame.axisV.y * factorV);
    p.frame.centre = core::Vec2(pivot.x + (p.frame.centre.x - pivot.x) * factorU,
                                pivot.y + (p.frame.centre.y - pivot.y) * factorV);
    p.scale *= (std::fabs(factorU) + std::fabs(factorV)) * 0.5;
    bool converted = swapCircleForOval(&p);
    if (convertedToOval) *convertedToOval = converted;
    return writeShape(document, id, p, document.defaultLineWidth());
}

core::Error scaleBracket(core::Document& document, core::ObjectId id,
                         const core::Vec2& pivot, double factorU, double factorV) {
    if (!pivot.finite()) return core::Error(core::ErrorCode::InvalidGeometry, "Pivot must be finite");
    if (!std::isfinite(factorU) || !std::isfinite(factorV))
        return core::Error(core::ErrorCode::InvalidArgument, "Scale factors must be finite");
    BracketParams p;
    bool used = false;
    core::Error e = readBracket(document, id, &p, &used);
    if (!e.ok()) return e;
    p.frame.axisU = core::Vec2(p.frame.axisU.x * factorU, p.frame.axisU.y * factorU);
    p.frame.axisV = core::Vec2(p.frame.axisV.x * factorV, p.frame.axisV.y * factorV);
    p.frame.centre = core::Vec2(pivot.x + (p.frame.centre.x - pivot.x) * factorU,
                                pivot.y + (p.frame.centre.y - pivot.y) * factorV);
    p.scale *= (std::fabs(factorU) + std::fabs(factorV)) * 0.5;
    // The labels are NOT touched: their (u,v) anchors are frame-parametric, so
    // they follow the frame automatically. That is the whole point of the anchor.
    return writeBracket(document, id, p, document.defaultLineWidth());
}

// ===========================================================================
// Bracket labels
// ===========================================================================
core::Error attachBracketLabel(core::Document& document, core::ObjectId bracketId,
                               const std::string& text, const BracketLabelAnchor& anchor,
                               double textSize) {
    if (text.empty()) return core::Error(core::ErrorCode::InvalidText, "Label text must not be empty");
    if (text.find(';') != std::string::npos)
        return core::Error(core::ErrorCode::InvalidText, "Label text must not contain ';'");
    BracketParams p;
    bool used = false;
    core::Error e = readBracket(document, bracketId, &p, &used);
    if (!e.ok()) return e;
    if (p.labels.size() >= 64u)
        return core::Error(core::ErrorCode::LimitExceeded, "At most 64 labels per bracket");
    BracketLabel l;
    l.anchor = anchor;
    l.text = text;
    l.size = textSize;
    p.labels.push_back(l);
    return writeBracket(document, bracketId, p, document.defaultLineWidth());
}

core::Error setBracketLabelAnchor(core::Document& document, core::ObjectId bracketId,
                                  size_t index, const BracketLabelAnchor& anchor) {
    BracketParams p;
    bool used = false;
    core::Error e = readBracket(document, bracketId, &p, &used);
    if (!e.ok()) return e;
    if (index >= p.labels.size())
        return core::Error(core::ErrorCode::OutOfRange, "No label at that index");
    p.labels[index].anchor = anchor;
    return writeBracket(document, bracketId, p, document.defaultLineWidth());
}

core::Error removeBracketLabel(core::Document& document, core::ObjectId bracketId,
                               size_t index) {
    BracketParams p;
    bool used = false;
    core::Error e = readBracket(document, bracketId, &p, &used);
    if (!e.ok()) return e;
    if (index >= p.labels.size())
        return core::Error(core::ErrorCode::OutOfRange, "No label at that index");
    p.labels.erase(p.labels.begin() + static_cast<std::ptrdiff_t>(index));
    return writeBracket(document, bracketId, p, document.defaultLineWidth());
}

core::Error bracketLabelDocumentPosition(const core::Document& document,
                                         core::ObjectId bracketId, size_t index,
                                         core::Vec2* out) {
    if (!out) return core::Error(core::ErrorCode::InvalidArgument, "null output");
    BracketParams p;
    bool used = false;
    core::Error e = readBracket(document, bracketId, &p, &used);
    if (!e.ok()) return e;
    if (index >= p.labels.size())
        return core::Error(core::ErrorCode::OutOfRange, "No label at that index");
    *out = bracketLabelPosition(p, p.labels[index].anchor);
    return core::Error::success();
}

bool graphicObjectDeletionAvailable() { return false; }

} // namespace graphics
} // namespace r62
