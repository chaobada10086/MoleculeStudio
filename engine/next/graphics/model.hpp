#ifndef R62_NEXT_GRAPHICS_MODEL_HPP
#define R62_NEXT_GRAPHICS_MODEL_HPP
// =============================================================================
// re_round62_viewer/next/graphics/model.hpp
//
// T11 - HOW A SHAPE AND A BRACKET LIVE IN THE T01 DOCUMENT.
//
// -----------------------------------------------------------------------------
// THE MAPPING IS UNCHANGED FROM WHAT T01 FROZE
// -----------------------------------------------------------------------------
// T01 already declared the home of both:
//
//   geometry shape -> ObjectKind::Arrow?  NO.
//
// AnnotationRecord is documented by T01 as "Generic annotation drawn with a
// shared shape vocabulary: arrows (all 56 presets), brackets and daggers (8),
// drawing shapes (32), experimental plates (2) and BioDraw presets (89)" and its
// slab is selected by the ObjectKind passed to Document::addAnnotation().
//
// ObjectKind has no dedicated "Shape" member, so a DRAWING SHAPE is stored as
// ObjectKind::Arrow in the arrows() slab - that slab is the "generic annotation"
// slab. This module does NOT invent a new object kind, a new slab or a new
// record type.
//
//   drawing shape -> ObjectKind::Arrow    in arrows()   (AnnotationRecord)
//   bracket       -> ObjectKind::Bracket  in brackets() (AnnotationRecord)
//   table         -> ObjectKind::Table    in tables()   (TableRecord)  [tables.hpp]
//
// -----------------------------------------------------------------------------
// THE AUTHORITATIVE COPY IS THE EXTENSION RECORD, AND THE RECORD IS A MIRROR
// -----------------------------------------------------------------------------
// AnnotationRecord has four geometry points, a preset id, a scale, a rotation,
// a LineStyle and a fill colour. It does NOT have: the shape form, the
// appearance, the corner fraction, the arc start/span, the two independent axis
// vectors, or (for a bracket) the label list. Those live in the OBJECT EXTENSION
// BAG under one versioned key:
//
//     "gfx.shape.params.1"    / "gfx.bracket.params.1"
//
// This module keeps the record a MIRROR of the bag, never a second opinion:
//
//     geometry[0] = centre
//     geometry[2] = centre + axisU     (half-extent along the shape +x)
//     geometry[3] = centre + axisV     (half-extent along the shape +y)
//     geometry[1] = (min.x, min.y) of the hit bounds   (conservative)
//     presetId, scale, rotation, style, fillRGBA copied from the bag
//
// The mirror exists so that a consumer which knows nothing about T11 - the T05
// bounds rule "the preset's ACTUALLY USED geometry[] points", T03 hit tests, a
// CDXML writer - still sees a usable, correctly placed object. readShape()
// prefers the bag and FALLS BACK to the mirror, so an object that entered the
// document from another module is never invisible.
//
// -----------------------------------------------------------------------------
// WHY EVERY WRITE GOES THROUGH writeShape()/writeBracket()/commitTable()
// -----------------------------------------------------------------------------
// DocumentSession::buildChangeSet() emits an ExtensionPatch ONLY for refs that
// were explicitly noted through Document::noteTouchedExtensions(). A caller that
// writes document.extensions(ref).set(...) by hand therefore changes the document
// but contributes nothing to the change set: the transaction's reverse patch
// would not mention the bag, and undo would silently keep the NEW parameters
// while restoring the old record. That is exactly the bug the first undo test
// run exposed, so the notice is mandatory, not decorative.
//
// Every write path here calls notifyBagDirty(), which is a thin wrapper over the
// public Document::noteTouchedExtensions(ref, false). Do not bypass it.
// Regression: tests/T11/graphics/t11_graphics_test.cpp "restyle_undone" and
// "undo_restores_the_exact_digest".
// =============================================================================

#include "next/core/document.hpp"
#include "next/graphics/geometry.hpp"
#include "next/graphics/preset.hpp"

#include <vector>

namespace r62 {
namespace graphics {

// ---------------------------------------------------------------------------
// Reads
// ---------------------------------------------------------------------------
// `usedExtension` is false when the object had no usable gfx.* record and the
// values were reconstructed from the AnnotationRecord mirror alone. That flag is
// the honest answer to "did this object come from T11 or from somewhere else".
core::Error readShape(const core::Document& document, core::ObjectId id,
                      ShapeParams* out, bool* usedExtension);
core::Error readBracket(const core::Document& document, core::ObjectId id,
                        BracketParams* out, bool* usedExtension);

// ---------------------------------------------------------------------------
// Writes (primitives; a caller wraps them in one core::Command)
// ---------------------------------------------------------------------------
// Add a shape. The object kind is recorded as Arrow and the preset id is stored
// in the record, so a registry/demo can map back to the 32 tools.
core::Error addShape(core::Document& document, const ShapeParams& params,
                     double documentLineWidth, core::ObjectId* outId);
// Replace an existing shape. NotFound for a dead id, WrongKind for a bracket.
core::Error writeShape(core::Document& document, core::ObjectId id,
                       const ShapeParams& params, double documentLineWidth);

core::Error addBracket(core::Document& document, const BracketParams& params,
                       double documentLineWidth, core::ObjectId* outId);
core::Error writeBracket(core::Document& document, core::ObjectId id,
                         const BracketParams& params, double documentLineWidth);

// Signatures used as Primitive::version and as the "did anything change?" key.
uint64_t shapeObjectSignature(const core::Document& document, core::ObjectId id);
uint64_t bracketObjectSignature(const core::Document& document, core::ObjectId id);

// ---------------------------------------------------------------------------
// Preset application
// ---------------------------------------------------------------------------
// Build a parameter bundle from a preset and a drag rectangle. `begin` is where
// the user pressed, `current` is where the pointer is now; EITHER may be to the
// right/below/above the other, and a reversed drag must produce the same shape as
// the equivalent forward drag. It does: the frame is normalised here and the
// handedness is preserved by the axis signs, so the contour is identical.
//
// A Circle is kept circular: the larger of |dx|/2 and |dy|/2 becomes both axes.
// The preset id is NOT changed by this function; call chooseShapePreset() when
// the drag shape must change the preset (Oval <- Circle).
ShapeParams resolveShapePreset(uint32_t presetId, const core::Vec2& begin,
                               const core::Vec2& current, double documentLineWidth);

// Style-only change: keeps geometry, applies the new preset's appearance / form
// family. Used by "select a shape, click another shape tool".
ShapeParams applyShapePresetStyle(const ShapeParams& current, uint32_t presetId);

// The conversion the task asks about explicitly: what happens when a CIRCLE is
// non-uniformly scaled? It cannot stay a Circle, so the caller must convert it to
// the Oval preset. Returns true and rewrites params when a conversion happened.
// Returns false (and changes nothing) when no conversion was needed - it never
// silently changes an Oval into something else.
bool swapCircleForOval(ShapeParams* params);

BracketParams resolveBracketPreset(uint32_t presetId, const core::Vec2& begin,
                                   const core::Vec2& current, double documentLineWidth);
BracketParams applyBracketPresetStyle(const BracketParams& current, uint32_t presetId);

// ---------------------------------------------------------------------------
// Geometry edits
// ---------------------------------------------------------------------------
// Move by a document-space delta. Works for a shape or a bracket and does not
// care which, because both store a ShapeFrame.
core::Error translateShape(core::Document& document, core::ObjectId id,
                           const core::Vec2& delta);
core::Error translateBracket(core::Document& document, core::ObjectId id,
                             const core::Vec2& delta);

// Scale about a pivot. `factorU` and `factorV` are separate so a NON-UNIFORM
// stretch is expressible; a CIRCLE that would stop being circular is converted to
// the Oval preset and *convertedToOval reports it.
core::Error scaleShape(core::Document& document, core::ObjectId id,
                       const core::Vec2& pivot, double factorU, double factorV,
                       bool* convertedToOval);
core::Error scaleBracket(core::Document& document, core::ObjectId id,
                         const core::Vec2& pivot, double factorU, double factorV);

// ---------------------------------------------------------------------------
// Bracket labels
// ---------------------------------------------------------------------------
// Attach a label positioned in FRAME coordinates. The absolute position is
// derived from the CURRENT frame on every read, so a drag or a resize keeps the
// label where the user put it relative to the bracket. That is the acceptance
// criterion "label position is preserved after dragging the bracket", and it is
// structural rather than a correction applied afterwards.
core::Error attachBracketLabel(core::Document& document, core::ObjectId bracketId,
                               const std::string& text, const BracketLabelAnchor& anchor,
                               double textSize);
core::Error setBracketLabelAnchor(core::Document& document, core::ObjectId bracketId,
                                  size_t index, const BracketLabelAnchor& anchor);
core::Error removeBracketLabel(core::Document& document, core::ObjectId bracketId,
                               size_t index);
// Absolute document position of a label of the CURRENT bracket. Fails with
// OutOfRange for an index that does not exist.
core::Error bracketLabelDocumentPosition(const core::Document& document,
                                         core::ObjectId bracketId, size_t index,
                                         core::Vec2* out);

// ---------------------------------------------------------------------------
// Deletion, honestly
// ---------------------------------------------------------------------------
// core::Document::removeObject() returns NotImplemented for Arrow/Bracket/Table
// (next/core/document.cpp, the switch in removeObject). core::commands::
// deleteObjects() therefore FAILS AND ROLLS BACK the whole transaction when a
// selection contains any of them, which is an upstream defect with a suggested
// patch in INTERFACES.md.
//
// This module does not pretend otherwise: eraseShape() reports whether the
// operation is available. It is currently NOT, and the caller must not report a
// deletion that did not happen.
bool graphicObjectDeletionAvailable();

} // namespace graphics
} // namespace r62

#endif // R62_NEXT_GRAPHICS_MODEL_HPP
