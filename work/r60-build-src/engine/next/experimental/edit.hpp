#ifndef R62_NEXT_EXPERIMENTAL_EDIT_HPP
#define R62_NEXT_EXPERIMENTAL_EDIT_HPP
// =============================================================================
// re_round62_viewer/next/experimental/edit.hpp
//
// T16 - EDITING OPERATIONS ON A TLC PLATE / GEL FIGURE.
//
// -----------------------------------------------------------------------------
// TWO LAYERS, DELIBERATELY
// -----------------------------------------------------------------------------
//   * figure*() functions are PURE mutations of a PlateFigure value. They do not
//     know a document exists, they validate before they change anything, and they
//     return an error that leaves the value untouched. This is what makes
//     "illegal parameters must not damage the document" testable without a
//     document at all.
//   * apply*() functions are document operations: read figure + frame, apply the
//     pure function, write both back through model.hpp. Each one is meant to run
//     inside ONE transaction (core::commands.hpp R1: one action == one undo step).
//     Nothing here opens a transaction itself.
//   * make*Command() factories wrap an apply* call in a copyable core::Command so
//     it can go through ToolHost::submit or CommandLog::execute, which is the only
//     sanctioned write path.
//
// -----------------------------------------------------------------------------
// INTERNAL ELEMENTS ARE NOT DOCUMENT OBJECTS
// -----------------------------------------------------------------------------
// Adding a lane/spot/band rewrites the figure payload. It does not create an
// atom, a path or a text object, and it does not grow the document object count.
// Deleting one therefore does NOT need Document::removeObject() - which is the
// T01 gap that blocks deleting whole annotations. Undo restores the payload
// byte for byte, so a deleted element comes back with the same element id.
//
// -----------------------------------------------------------------------------
// T09 OWNS ANNOTATION TEXT
// -----------------------------------------------------------------------------
// A visible annotation (title, origin/front caption, lane caption, "Rf 0.42") is
// a real core::Text object whose runs are authored through
// text::TextEditSession. The figure payload stores only WHERE the text belongs
// (a relative slot) and WHICH object it is. Text is created and edited through
// T09, never by writing a string into the payload and hoping a renderer finds it.
// =============================================================================

#include "next/core/commands.hpp"
#include "next/experimental/model.hpp"
#include "next/text/glyph_source.hpp"
#include "next/text/text_edit.hpp"

#include <functional>
#include <string>
#include <vector>

namespace r62 {
namespace experimental {

// ---------------------------------------------------------------------------
// Outcome of one edit
// ---------------------------------------------------------------------------
struct EditOutcome {
    core::ObjectId objectId;    // the figure or text object involved
    uint32_t       elementId;   // the internal element id, when one was created
    bool           changed;
    EditOutcome() : objectId(core::kInvalidObjectId), elementId(0), changed(false) {}
};

// ---------------------------------------------------------------------------
// Pure figure mutations
// ---------------------------------------------------------------------------
core::Error figureAddLane(PlateFigure* figure, double u, double width,
                          const std::string& label, uint32_t* outElementId);
core::Error figureRemoveLane(PlateFigure* figure, uint32_t laneId,
                             size_t* removedSpots, size_t* removedBands);
core::Error figureAddSpot(PlateFigure* figure, uint32_t laneId, double u, double v,
                          uint32_t* outElementId);
core::Error figureRemoveSpot(PlateFigure* figure, uint32_t spotId);
core::Error figureAddBand(PlateFigure* figure, uint32_t laneId, double v,
                          uint32_t* outElementId);
core::Error figureRemoveBand(PlateFigure* figure, uint32_t bandId);
// Absolute relative placement (the value a drag already resolved). A lane keeps
// its spots: moving a lane moves its spots by the same du.
core::Error figureSetSpotPosition(PlateFigure* figure, uint32_t spotId, double u, double v);
core::Error figureSetSpotRadius(PlateFigure* figure, uint32_t spotId, double radiusU, double radiusV);
core::Error figureSetBandPosition(PlateFigure* figure, uint32_t bandId, double v);
core::Error figureSetLanePosition(PlateFigure* figure, uint32_t laneId, double u);
core::Error figureSetOriginFront(PlateFigure* figure, bool originDefined, double originV,
                                 bool frontDefined, double frontV);
core::Error figureSetTitle(PlateFigure* figure, const std::string& title);
core::Error figureSetSpotAppearance(PlateFigure* figure, uint32_t spotId,
                                    uint32_t fillRGBA, double opacity,
                                    const std::string& label);
core::Error figureSetBandDisplay(PlateFigure* figure, uint32_t bandId,
                                 double displayIntensity, double thicknessV,
                                 double widthFraction);
core::Error figureSetMeasurement(PlateFigure* figure, const std::string& name,
                                 const MeasurementValue& value);
core::Error figureRemoveMeasurement(PlateFigure* figure, const std::string& name);
core::Error figureAttachAnnotation(PlateFigure* figure, const std::string& slot,
                                   double u, double v, core::ObjectId textId);
core::Error figureDetachAnnotation(PlateFigure* figure, const std::string& slot);

// ---------------------------------------------------------------------------
// Document operations (caller owns the transaction)
// ---------------------------------------------------------------------------
core::Error applyAddFigure(core::Document& document, FigureKind kind, const PlateFrame& frame,
                           uint32_t laneCount, core::ObjectId* outId);
core::Error applyAddLane(core::Document& document, core::ObjectId figureId, double u,
                         double width, const std::string& label, uint32_t* outElementId);
core::Error applyRemoveLane(core::Document& document, core::ObjectId figureId, uint32_t laneId);
core::Error applyAddSpot(core::Document& document, core::ObjectId figureId, uint32_t laneId,
                         double u, double v, uint32_t* outElementId);
core::Error applyRemoveSpot(core::Document& document, core::ObjectId figureId, uint32_t spotId);
core::Error applyAddBand(core::Document& document, core::ObjectId figureId, uint32_t laneId,
                         double v, uint32_t* outElementId);
core::Error applyRemoveBand(core::Document& document, core::ObjectId figureId, uint32_t bandId);
core::Error applyMoveElement(core::Document& document, core::ObjectId figureId,
                             const ElementRef& element, double u, double v);
core::Error applyResizeBoard(core::Document& document, core::ObjectId figureId,
                             const PlateFrame& frame);
core::Error applySetOriginFront(core::Document& document, core::ObjectId figureId,
                                bool originDefined, double originV, bool frontDefined,
                                double frontV);
core::Error applySetTitle(core::Document& document, core::ObjectId figureId,
                          const std::string& title);
core::Error applySetMeasurement(core::Document& document, core::ObjectId figureId,
                                const std::string& name, const MeasurementValue& value);
core::Error applyDuplicateFigure(core::Document& document, core::ObjectId sourceId,
                                 const core::Vec2& delta, core::ObjectId* outId);

// ---------------------------------------------------------------------------
// Commands (copyable; safe for Repeat except for the optional out pointer)
// ---------------------------------------------------------------------------
core::Command makeFigureCommand(const std::string& name, core::Invalidation invalidation,
                                const std::function<core::Error(core::Document&)>& fn);
core::Command addFigureCommand(FigureKind kind, const PlateFrame& frame, uint32_t laneCount,
                               EditOutcome* out);
core::Command addLaneCommand(core::ObjectId figureId, double u, double width,
                             const std::string& label, EditOutcome* out);
core::Command removeLaneCommand(core::ObjectId figureId, uint32_t laneId, EditOutcome* out);
core::Command addSpotCommand(core::ObjectId figureId, uint32_t laneId, double u, double v,
                             EditOutcome* out);
core::Command removeSpotCommand(core::ObjectId figureId, uint32_t spotId, EditOutcome* out);
core::Command addBandCommand(core::ObjectId figureId, uint32_t laneId, double v,
                             EditOutcome* out);
core::Command removeBandCommand(core::ObjectId figureId, uint32_t bandId, EditOutcome* out);
core::Command moveElementCommand(core::ObjectId figureId, const ElementRef& element,
                                 double u, double v, EditOutcome* out);
core::Command resizeBoardCommand(core::ObjectId figureId, const PlateFrame& frame,
                                 EditOutcome* out);
core::Command duplicateFigureCommand(core::ObjectId sourceId, const core::Vec2& delta,
                                     EditOutcome* out);
// Whole-payload replacement in one transaction. Useful for a bulk edit (seed a
// plate, import a lane table) where every intermediate value has already been
// validated by the pure figure*() functions.
core::Command writeFigureCommand(core::ObjectId figureId, const PlateFigure& figure,
                                 EditOutcome* out);
core::Command setOriginFrontCommand(core::ObjectId figureId, bool originDefined, double originV,
                                    bool frontDefined, double frontV, EditOutcome* out);
core::Command setTitleCommand(core::ObjectId figureId, const std::string& title, EditOutcome* out);
core::Command setMeasurementCommand(core::ObjectId figureId, const std::string& name,
                                    const MeasurementValue& value, EditOutcome* out);
// T09 text as a command, so a caption edit is ONE transaction like every other
// edit (a bare Transaction::commit() outside CommandLog does not build a patch).
core::Command createAnnotationTextCommand(core::ObjectId figureId, const std::string& slot,
                                          const std::string& utf8, double textSize,
                                          bool bold, bool italic, EditOutcome* out);
core::Command setAnnotationTextCommand(core::ObjectId figureId, const std::string& slot,
                                       const std::string& utf8, EditOutcome* out);
core::Command syncAnnotationTextsCommand(core::ObjectId figureId, EditOutcome* out);

// ---------------------------------------------------------------------------
// T09 annotation text
// ---------------------------------------------------------------------------
// Where a slot is anchored, in document space. Recognised slots:
//   "title"      above the board centre
//   "origin"     right of the origin line
//   "front"      right of the solvent front
//   "lane:<id>"  below the lane centre
//   "spot:<id>"  right of the spot
//   anything else: the stored (u, v) of the annotation record
core::Vec2 annotationSlotPosition(const PlateFrame& frame, const PlateFigure& figure,
                                  const std::string& slot, double u, double v);
core::Error createAnnotationText(core::Document& document, core::ObjectId figureId,
                                 const std::string& slot, const std::string& utf8,
                                 double textSize, bool bold, bool italic,
                                 core::ObjectId* outTextId);
core::Error setAnnotationText(core::Document& document, core::ObjectId figureId,
                              const std::string& slot, const std::string& utf8);
std::string  annotationText(const core::Document& document, core::ObjectId figureId,
                            const std::string& slot);
// Re-anchor every attached text object after a frame change (T05 move/rotate/scale
// or a board resize). Without this a rotated board would drag its captions wrong.
core::Error syncAnnotationTextPositions(core::Document& document, core::ObjectId figureId);

} // namespace experimental
} // namespace r62

#endif // R62_NEXT_EXPERIMENTAL_EDIT_HPP
