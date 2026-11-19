#ifndef R62_NEXT_EXPERIMENTAL_MODEL_HPP
#define R62_NEXT_EXPERIMENTAL_MODEL_HPP
// =============================================================================
// re_round62_viewer/next/experimental/model.hpp
//
// T16 - HOW A TLC PLATE / GEL FIGURE LIVES IN THE T01 DOCUMENT.
//
// -----------------------------------------------------------------------------
// THE MAPPING USES EXISTING T01 SLABS - NO NEW OBJECT KIND IS INVENTED
// -----------------------------------------------------------------------------
// T01 documents AnnotationRecord as the generic annotation record and names
// "experimental plates (2)" among the things it must cover. ObjectClass has a
// Science class whose comment is literally "orbitals, symbols, TLC/GEP plates",
// and ObjectKind::Symbol maps to it. So:
//
//     experimental figure  -> ObjectKind::Symbol  in  Document::symbols()
//
// The AnnotationRecord is the MIRROR of the frame (the same convention
// next/graphics/model.hpp chose, for the same reason: every consumer that knows
// nothing about T16 still sees a correctly placed object), and the structured
// content (lanes, spots, bands, measurements, annotation slots) lives in the
// OBJECT EXTENSION BAG under one versioned key:
//
//     "exp.figure.params.1"
//
//   geometry[0] = centre
//   geometry[2] = centre + axisU          (half-extent to the right edge)
//   geometry[3] = centre + axisV          (half-extent to the bottom edge)
//   geometry[1] = (min.x, min.y) of the board box   (conservative mirror)
//
// WHY THE ABSOLUTE PLACEMENT IS *ONLY* IN THE MIRROR: T05 transforms an
// annotation by applying an affine to its geometry points and leaving scale and
// rotation alone. If the payload also stored an absolute frame, a T05 rotate
// would move the mirror without moving the payload and the figure would render
// at the old place. Keeping the payload purely relative removes that failure
// mode instead of documenting it.
//
// -----------------------------------------------------------------------------
// EVERY WRITE GOES THROUGH writeFigure()/writeFrame(): the bag notice is MANDATORY
// -----------------------------------------------------------------------------
// DocumentSession::buildChangeSet() emits an ExtensionPatch only for refs that
// were explicitly noted through Document::noteTouchedExtensions(). Writing
// document.extensions(ref).set(...) by hand therefore changes the document but
// contributes nothing to undo - the record restores, the bag does not. This was
// a real defect in T11 (regression test restyle_undone) and the same mistake is
// avoided here by routing every write through model.cpp.
//
// -----------------------------------------------------------------------------
// KNOWN T01 GAP (reported, not worked around silently)
// -----------------------------------------------------------------------------
// Document::removeObject() returns NotImplemented for Annotation/Path/Group, so a
// T16 figure cannot be deleted through core::commands::deleteObjects() yet.
// figureDeletionAvailable() returns false and figureDeletionStatusText() says
// why, so a UI can disable Delete instead of showing a command that rolls back.
// Deleting a LANE or a SPOT is NOT affected: those are payload edits, not object
// removals, which is exactly why internal elements are not document objects.
// =============================================================================

#include "next/core/document.hpp"
#include "next/experimental/geometry.hpp"

namespace r62 {
namespace experimental {

// ---------------------------------------------------------------------------
// Object identity
// ---------------------------------------------------------------------------
core::ObjectKind figureObjectKind();          // ObjectKind::Symbol
core::ObjectRef  figureRef(core::ObjectId id);
bool             isFigureObject(const core::Document& document, const core::ObjectRef& ref);
// The preset row that creates a given kind (never returns 0).
const PlatePresetSpec* presetForKind(FigureKind kind);

// ---------------------------------------------------------------------------
// Reads
// ---------------------------------------------------------------------------
// usedExtension is false when the object carried no usable exp.* record and the
// figure was reconstructed as a minimal default. readFigure() prefers the bag.
core::Error readFigure(const core::Document& document, core::ObjectId id,
                       PlateFigure* out, bool* usedExtension);
core::Error readFrame(const core::Document& document, core::ObjectId id, PlateFrame* out);
core::Error readFigureAndFrame(const core::Document& document, core::ObjectId id,
                               PlateFigure* figure, PlateFrame* frame, bool* usedExtension);

// ---------------------------------------------------------------------------
// Writes (primitives; a caller wraps them in ONE core::Command)
// ---------------------------------------------------------------------------
core::Error addFigure(core::Document& document, const PlateFigure& figure,
                      const PlateFrame& frame, double documentLineWidth,
                      core::ObjectId* outId);
// Payload only; the frame is left alone.
core::Error writeFigure(core::Document& document, core::ObjectId id,
                        const PlateFigure& figure);
// Frame mirror only; the payload is left alone.
core::Error writeFrame(core::Document& document, core::ObjectId id, const PlateFrame& frame);
core::Error writeFigureAndFrame(core::Document& document, core::ObjectId id,
                                const PlateFigure& figure, const PlateFrame& frame);

// Stable content signature (frame + payload). Used as Primitive::version.
uint64_t figureObjectSignature(const core::Document& document, core::ObjectId id);

// The board box of one figure object (the mirror is authoritative and always
// present, so this works even if the payload is unreadable).
core::Box figureObjectBounds(const core::Document& document, core::ObjectId id);

// ---------------------------------------------------------------------------
// Duplicate
// ---------------------------------------------------------------------------
// A deep copy: new annotation record, copied payload, copied annotation text
// objects. `freshElementIds` renumbers the internal element ids from a fresh
// counter; the default keeps them identical, because element ids are
// figure-local (identity is restored by undo, not by global uniqueness).
core::Error duplicateFigure(core::Document& document, core::ObjectId sourceId,
                            const core::Vec2& delta, bool freshElementIds,
                            core::ObjectId* outId);

// ---------------------------------------------------------------------------
// Honest capability reporting (see the header block)
// ---------------------------------------------------------------------------
bool        figureDeletionAvailable();
const char* figureDeletionStatusText();

} // namespace experimental
} // namespace r62

#endif // R62_NEXT_EXPERIMENTAL_MODEL_HPP
