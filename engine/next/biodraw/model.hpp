#ifndef R62_NEXT_BIODRAW_MODEL_HPP
#define R62_NEXT_BIODRAW_MODEL_HPP
// =============================================================================
// re_round62_viewer/next/biodraw/model.hpp
//
// T14 - HOW A BioDraw FIGURE LIVES IN THE T01 DOCUMENT, AND HOW IT IS EDITED.
//
// -----------------------------------------------------------------------------
// THE OBJECT KIND: BioShape, IN THE bioShapes() SLAB
// -----------------------------------------------------------------------------
// next/core/document.hpp already declares FIVE AnnotationRecord slabs and says of
// them: 'arrows (all 56 presets), brackets and daggers (8), drawing shapes (32),
// experimental plates (2) and BioDraw presets (89)'. The BioDraw slab is
// annotations_[4] and is reached through Document::addAnnotation(ObjectKind::
// BioShape, ...).
//
// T14 therefore introduces NO new object kind, NO new slab and NO new record
// type. It is the module that finally gives that frozen slab an editor.
//
// -----------------------------------------------------------------------------
// RECORD IS A MIRROR; THE EXTENSION BAG IS AUTHORITATIVE
// -----------------------------------------------------------------------------
// See params.hpp for the full argument. Summary: the record holds
//
//     geometry[0] = centre
//     geometry[2] = centre + axisU
//     geometry[3] = centre + axisV
//     geometry[1] = (min.x, min.y) of the conservative hit box
//     presetId, scale, rotation, style, fillRGBA
//
// and the bag holds the glyph block, the component list, the plasmid data and the
// sub-element colours. readBioShape() prefers the bag and falls back to the
// mirror.
//
// -----------------------------------------------------------------------------
// EVERY WRITE GOES THROUGH addBioShape / writeBioShape
// -----------------------------------------------------------------------------
// DocumentSession::buildChangeSet() emits an ExtensionPatch ONLY for refs that
// were explicitly noted through Document::noteTouchedExtensions(). A caller that
// writes document.extensions(ref).set(...) directly changes the document but
// contributes nothing to the change set, so undo would restore the record and
// keep the NEW parameters -- a silent data corruption. Every write path here
// calls notifyBagDirty(), which is a thin wrapper over the public
// noteTouchedExtensions(). Do not bypass it.
// =============================================================================

#include "next/biodraw/geometry.hpp"
#include "next/biodraw/params.hpp"
#include "next/core/commands.hpp"
#include "next/core/document.hpp"

#include <vector>

namespace r62 {
namespace biodraw {

// ---------------------------------------------------------------------------
// Reads
// ---------------------------------------------------------------------------
// `usedExtension` is false when the object had no usable bio.* record and the
// values were reconstructed from the AnnotationRecord mirror alone. That flag is
// the honest answer to 'did this figure come from T14 or from somewhere else'.
core::Error readBioShape(const core::Document& document, core::ObjectId id,
                         BioShapeParams* out, bool* usedExtension);

// True when the id is an alive annotation of kind BioShape.
bool isBioShape(const core::Document& document, core::ObjectId id);

// Every alive BioDraw figure in the document, in slab order. Used by the
// renderer's scene builder and by the gallery.
std::vector<core::ObjectId> allBioShapes(const core::Document& document);

// ---------------------------------------------------------------------------
// Primitive writes (a caller wraps them in ONE core::Command)
// ---------------------------------------------------------------------------
// Insert a figure. Writes the record mirror AND the bag, and notes BOTH, so undo
// restores everything. Returns the new id through *outId.
core::Error addBioShape(core::Document& document, const BioShapeParams& params,
                        double documentLineWidth, core::ObjectId* outId);
// Replace an existing figure in place. NotFound for a dead id, WrongKind for a
// non-BioShape object.
core::Error writeBioShape(core::Document& document, core::ObjectId id,
                          const BioShapeParams& params, double documentLineWidth);

// Content signature, used as Primitive::version and as the 'did anything
// change?' key. Two figures with equal signatures are byte-identical in the
// document.
uint64_t bioShapeSignature(const core::Document& document, core::ObjectId id);

// ---------------------------------------------------------------------------
// Preset resolution
// ---------------------------------------------------------------------------
// Build a complete parameter bundle for a preset and a drag rectangle. `begin` is
// where the user pressed, `current` is where the pointer is; EITHER may be to the
// right/below/above the other, and a reversed drag must produce the SAME figure as
// the equivalent forward drag. It does: the frame is normalised and the handedness
// is preserved by the axis signs.
BioShapeParams resolveBioPreset(uint32_t presetId, const core::Vec2& begin,
                                const core::Vec2& current, double documentLineWidth);

// Apply a preset's defaults to an EXISTING figure, keeping its placement. This is
// 'select a figure, click another BioDraw tool': the shape changes, the position
// does not.
BioShapeParams applyBioPresetToShape(const BioShapeParams& current, uint32_t presetId);

// ---------------------------------------------------------------------------
// Commands (one user action == one transaction == one undo step)
// ---------------------------------------------------------------------------
// Insert as a command. `outId` is filled when the command BODY runs, so a caller
// reads it only after CommandLog::execute succeeded.
core::Command addBioShapeCommand(const BioShapeParams& params,
                                 double documentLineWidth, core::ObjectId* outId);
core::Command writeBioShapeCommand(core::ObjectId id, const BioShapeParams& params,
                                   double documentLineWidth);
// Move by a document-space delta.
core::Command translateBioShapeCommand(core::ObjectId id, const core::Vec2& delta);
// Scale about a pivot; a non-uniform stretch is expressible. The repeated unit
// COUNT is preserved.
core::Command scaleBioShapeCommand(core::ObjectId id, const core::Vec2& pivot,
                                   double factorU, double factorV);
core::Command rotateBioShapeCommand(core::ObjectId id, const core::Vec2& pivot,
                                    double degrees);
core::Command distortBioShapeCommand(core::ObjectId id, double shearU, double shearV);
// Change one component's count / spacing / unit size. This is the operation the
// acceptance criterion drives: 'the repeated object count follows the parameter'.
core::Command setComponentCountCommand(core::ObjectId id, size_t componentIndex,
                                       int count);
core::Command setComponentSpacingCommand(core::ObjectId id, size_t componentIndex,
                                         double spacing, bool evenlySpaced);
core::Command setComponentUnitSizeCommand(core::ObjectId id, size_t componentIndex,
                                          double unitSize);
// Add or remove a whole component by kind.
core::Command addComponentCommand(core::ObjectId id, const ComponentSpec& spec);
core::Command removeComponentCommand(core::ObjectId id, size_t componentIndex);
// Per-sub-element colour. `key` comes from subElementKeyForUnit/ForBand.
core::Command setSubElementColorCommand(core::ObjectId id, uint32_t key, uint32_t rgba);
core::Command clearSubElementColorCommand(core::ObjectId id, uint32_t key);
core::Command setBioShapeStyleCommand(core::ObjectId id, const core::LineStyle& style,
                                      uint32_t fillRGBA);
// Plasmid data. Each is one undo step; none of them touches another feature.
core::Command setPlasmidDataCommand(core::ObjectId id, const PlasmidData& data);
core::Command movePlasmidFeatureCommand(core::ObjectId id, size_t featureIndex,
                                        int newStart);
core::Command setPlasmidFeatureRangeCommand(core::ObjectId id, size_t featureIndex,
                                            int start, int end);
core::Command addPlasmidFeatureCommand(core::ObjectId id, const PlasmidFeature& feature);
core::Command removePlasmidFeatureCommand(core::ObjectId id, size_t featureIndex);

// ---------------------------------------------------------------------------
// Group / Ungroup of a figure's own sub-elements
// ---------------------------------------------------------------------------
// A figure is ONE document object. 'Ungroup' materialises its sub-elements as
// independent figures, and an UNDO puts the original back COMPLETELY -- not
// 'close enough', because the original is a single object and the exploded parts
// are several. This is implemented as ONE command that removes the figure and
// inserts the parts, so the transaction's own patch restores everything; it does
// NOT rely on a compensating command being written correctly.
//
// `outParts`/`outPartCount` are filled when the command body runs.
core::Command explodeBioShapeCommand(const BioShapeParams& geometrySource,
                                     core::ObjectId id,
                                     const std::vector<ExplodedPart>& parts,
                                     double documentLineWidth);
// Build the explode command for a figure already in the document: reads it,
// builds its geometry, extracts the parts, and returns the command plus the keys.
core::Command explodeBioShapeCommand(const core::Document& document,
                                     core::ObjectId id,
                                     double documentLineWidth);

// The number of sub-elements a figure currently draws -- what ungroup would
// produce. Cheap enough for a UI enable/disable check.
size_t bioShapeSubElementCount(const core::Document& document, core::ObjectId id);

// ---------------------------------------------------------------------------
// Deletion, honestly
// ---------------------------------------------------------------------------
// core::Document::removeObject() returns NotImplemented for the annotation slabs
// (next/core/document.cpp, the switch in removeObject). core::commands::
// deleteObjects() therefore FAILS AND ROLLS BACK the whole transaction when a
// selection contains a BioShape, which is an upstream gap with a suggested patch
// recorded in INTERFACES.md, section 8.
//
// This module does not pretend otherwise: bioShapeDeletionAvailable() reports
// whether deletion currently works, and explode/collapse do not depend on it
// (they use a command that manipulates the slab directly).
bool bioShapeDeletionAvailable();

// ---------------------------------------------------------------------------
// Chemical-graph honesty (enforced, not documented)
// ---------------------------------------------------------------------------
// A BioDraw figure NEVER contributes an atom or a bond. `chemicallyInert()` is a
// runtime-readable statement of that, and the test
// 'graph.biodraw_never_creates_atoms_or_bonds' asserts the atom and bond counts of
// a document are unchanged by inserting every one of the 89 presets.
bool chemicallyInert();

} // namespace biodraw
} // namespace r62

#endif // R62_NEXT_BIODRAW_MODEL_HPP
