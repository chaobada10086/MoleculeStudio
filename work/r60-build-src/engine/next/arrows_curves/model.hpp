#ifndef R62_NEXT_ARROWS_CURVES_MODEL_HPP
#define R62_NEXT_ARROWS_CURVES_MODEL_HPP
// =============================================================================
// re_round62_viewer/next/arrows_curves/model.hpp
//
// T10 - HOW AN ARROW OR A CURVE LIVES IN THE T01 DOCUMENT.
//
// -----------------------------------------------------------------------------
// THE PROBLEM
// -----------------------------------------------------------------------------
// T01 froze two different slabs that both fit an arrow:
//
//   * core::AnnotationRecord - ObjectKind::Arrow, geometry[4], presetId,
//     scale, rotation, LineStyle, fillRGBA. Explicitly documented as the home of
//     "arrows (all 56 presets)".
//   * core::PathRecord       - points[], pointKind[], closed, LineStyle, fillRGBA
//     + fillEnabled. Explicitly documented as "a polyline with optional quadratic
//     control points ... free curves all reduce to this".
//
// The mapping is therefore unambiguous and this module does NOT invent a third
// type:
//
//   arrow  -> ObjectKind::Arrow  in the arrows() slab  (AnnotationRecord)
//   curve  -> ObjectKind::Path   in the paths()  slab  (PathRecord)
//
// -----------------------------------------------------------------------------
// WHERE THE RICH PARAMETERS GO
// -----------------------------------------------------------------------------
// AnnotationRecord has room for two points (geometry[0], geometry[2]) and the
// box corners (geometry[1], geometry[3]). It does NOT have room for: arc
// sagitta, equilibrium separation, split ratio, two independent half lengths,
// per-end head flags, the sweep/direction enums. Those live in the OBJECT
// EXTENSION BAG under ONE key:
//
//     "arc.arrow.params"      value = a compact, versioned, human-readable record
//
// Why the extension bag rather than asking T01 for new fields:
//   * the bag is T01's documented extension point for "richer appearance ...
//     until it is promoted into a public field";
//   * isPersistentExtensionKey() only accepts "cdxml."-prefixed keys, so a
//     "arc." key is NOT replayed into CDXML as if it were source data. It is
//     stored in the native .r62ext document and in .cdxml through the CDXML
//     writer's own graphic attributes (see encodeArrowCdxml).
//   * the geometry (begin/end/presetId/line) stays in the FROZEN AnnotationRecord,
//     so a consumer that knows nothing about T10 still sees a usable arrow.
//
// -----------------------------------------------------------------------------
// WHY THIS MATTERS FOR THE ACCEPTANCE CRITERION
// -----------------------------------------------------------------------------
// "connecting/disconnecting, Undo and save-reload must not lose parameters".
// The extension bag IS part of the document digest and IS part of
// Document::RawState, so:
//   * Transaction rollback restores it (core captures extensions in RawState);
//   * serialization writes it;
//   * the digest changes when it changes, so "nothing was lost" is a digest
//     comparison and not a hope.
// =============================================================================

#include "next/arrows_curves/geometry.hpp"
#include "next/arrows_curves/preset.hpp"
#include "next/core/document.hpp"

#include <string>
#include <vector>

namespace r62 {
namespace arrows {

// The single extension key. Bump the trailing number if the encoding changes.
extern const char* const kArrowParamsKey;   // "arc.arrow.params.1"
extern const char* const kCurveStyleKey;    // "arc.curve.style.1"

// ---------------------------------------------------------------------------
// Encoding
// ---------------------------------------------------------------------------
// A compact "k=v;k=v;..." record. Chosen over a binary blob because:
//   * it survives a text diff, which is how a lost-parameter bug is diagnosed;
//   * it is stable across compilers (no struct layout in the document);
//   * an unknown key is ignored on read, so an older reader degrades rather than
//     refusing, and a newer writer cannot corrupt an older document.
std::string encodeArrowParams(const ArrowParams& params);

// Parse. On success `*ok` is true and every field was present and well formed.
// On a malformed record `*ok` is false and the out value is left at its default
// (never half-updated), so a corrupt document cannot produce a half-configured
// arrow.
ArrowParams decodeArrowParams(const std::string& text, bool* ok);

std::string encodeCurveStyle(const CurveStyleSpec& style);
CurveStyleSpec decodeCurveStyle(const std::string& text, bool* ok);

// A revision counter so the renderer can tell "unchanged" from "changed" without
// diffing the whole parameter struct.
uint64_t arrowParamsSignature(const ArrowParams& params);

// ---------------------------------------------------------------------------
// Document adapters
// ---------------------------------------------------------------------------
// These are the ONLY functions in T10 that touch a core::Document. They are
// deliberately plain: no command, no transaction, no history. A caller wraps
// them in core::commands / core::CommandLog, which is where "one drag == one
// transaction" is enforced (T01 R1).

// Fill an AnnotationRecord from an ArrowParams. Does NOT write to the document.
core::Error toAnnotationRecord(const ArrowParams& params, double documentLineWidth,
                               core::AnnotationRecord* out);

// Read an ArrowParams back out of an AnnotationRecord + its extension bag.
// `ok` is false when the bag has no usable record; the geometry from the record
// is still returned so a legacy arrow (imported from CDXML before T10 existed)
// is never invisible.
ArrowParams fromAnnotationRecord(const core::AnnotationRecord& record,
                                 const core::ExtensionBag& bag, bool* ok);

// Add an arrow to the document. On success `*outId` is the new ObjectId.
core::Error addArrow(core::Document& document, const ArrowParams& params,
                     double documentLineWidth, core::ObjectId* outId);

// Replace an existing arrow's record + bag. Returns NotFound for a dead id.
core::Error writeArrow(core::Document& document, core::ObjectId id,
                       const ArrowParams& params, double documentLineWidth);

// Read it back. `ok` is false when the extension record was missing or corrupt.
core::Error readArrow(const core::Document& document, core::ObjectId id,
                      ArrowParams* out, bool* ok);

// Add a free curve. `style` selects the CurveStyleSpec; the resulting appearance
// is stored on the PathRecord (closed/style/fillEnabled/fillRGBA) AND in the bag
// so the exact style preset survives a round trip.
core::Error addCurve(core::Document& document, const std::vector<core::Vec2>& points,
                     const std::vector<uint8_t>& pointKind, bool closed,
                     const CurveStyleSpec& style, double documentLineWidth,
                     core::ObjectId* outId);

core::Error writeCurve(core::Document& document, core::ObjectId id,
                       const std::vector<core::Vec2>& points,
                       const std::vector<uint8_t>& pointKind, bool closed,
                       const CurveStyleSpec& style, double documentLineWidth);

core::Error readCurve(const core::Document& document, core::ObjectId id,
                      std::vector<core::Vec2>* points,
                      std::vector<uint8_t>* pointKind, bool* closed,
                      CurveStyleSpec* style, bool* ok);

// ---------------------------------------------------------------------------
// Attaching a curve to an arrow (connection points)
// ---------------------------------------------------------------------------
// [GUIDE p111] "You can connect a new arrow to an existing arrow at either end
// or at the midpoint." The connection is stored as an ANCHOR on the
// AnnotationRecord (anchors[0..3]) plus, in the bag, which of the four slots was
// used and whether it was an end or the midpoint. The anchor is an ObjectRef
// flattened to (kind, id) so it survives serialization without a new type.
enum class AnchorSlot : uint8_t {
    None = 0,
    Begin = 1,          // connected to another object's Begin handle
    End = 2,
    Midpoint = 3        // connected to another object's midpoint handle
};

struct ArrowAnchor {
    AnchorSlot   slot;
    core::ObjectRef target;
    ArrowAnchor() : slot(AnchorSlot::None) {}
    bool valid() const { return slot != AnchorSlot::None && target.valid(); }
};

std::string encodeAnchor(const ArrowAnchor& anchor);
ArrowAnchor decodeAnchor(const std::string& text, bool* ok);
core::Error setArrowAnchor(core::Document& document, core::ObjectId arrowId,
                           const ArrowAnchor& anchor);
core::Error clearArrowAnchor(core::Document& document, core::ObjectId arrowId);
ArrowAnchor arrowAnchor(const core::Document& document, core::ObjectId arrowId,
                        bool* ok);

const char* anchorSlotName(AnchorSlot slot);

} // namespace arrows
} // namespace r62

#endif // R62_NEXT_ARROWS_CURVES_MODEL_HPP
