#ifndef R62_NEXT_EXPERIMENTAL_FIGURE_HPP
#define R62_NEXT_EXPERIMENTAL_FIGURE_HPP
// =============================================================================
// re_round62_viewer/next/experimental/figure.hpp
//
// T16 - THE STRUCTURED CONTENT OF A TLC PLATE / ELECTROPHORESIS GEL.
//
// -----------------------------------------------------------------------------
// LAYERS OF MEANING (they are deliberately separate, and the API names say which
// is which)
// -----------------------------------------------------------------------------
//   * RELATIVE BOARD COORDINATES. Every lane/spot/band position is stored in the
//     plate's own unit square:
//         u in [-1, 1]   left  -> right
//         v in [-1, 1]   TOP   -> BOTTOM   (document space has y downward)
//     The absolute placement is NOT stored here. It is the AnnotationRecord
//     geometry mirror, so a translate/rotate/scale done by T05 moves the whole
//     figure without touching a single relative coordinate. That is what "relative
//     coordinates and display stay consistent" means mechanically.
//   * DISPLAY PROPERTIES. Colour, opacity, band "intensity" (drawn darkness),
//     dashed borders, fill. These describe pixels, not chemistry.
//   * MEASUREMENTS. A MeasurementValue carries a number AND a unit AND a
//     provenance. It is NEVER derived from the drawn geometry: the gel module
//     does not turn a band position into a molecular weight, and it does not
//     claim a concentration from a spot darkness. The only derived quantity is
//     Rf, whose exact formula is stated below and which is reported as
//     `geometric` (not as a measurement).
//   * INTERNAL ELEMENT IDS. Lanes, spots, bands and annotation slots have their
//     own stable ids inside the figure. They are NOT document objects and NOT
//     atoms; `nextElementId` is monotonic, and deleting an element retires its id
//     (the id is never reused). Undo restores the payload verbatim, so a deleted
//     spot comes back with the SAME element id - that is the "undo restores
//     element identity" acceptance item.
//
// -----------------------------------------------------------------------------
// Rf - THE ONLY DERIVED VALUE, AND ITS EXACT BASIS
// -----------------------------------------------------------------------------
//   Rf = (originV - v) / (originV - frontV)
// where originV is the v of the origin line, frontV is the v of the solvent
// front, and v is the spot centre. With y downward and the solvent front above
// the origin, frontV < originV is REQUIRED; otherwise the mapping is undefined
// and this module says so instead of returning a number.
// geometricRf() reports valid == false and a reason when either line is
// undefined or the two are inverted. A caller-supplied measured Rf (for a
// standard, say) lives in Spot::rfMeasured and is a different thing entirely.
//
// -----------------------------------------------------------------------------
// PROVENANCE OF EVERY CONSTANT
// -----------------------------------------------------------------------------
//  [SRC] tools.csv / drawing inventory.
//  [PROJ] a project-chosen default (lane width, spot radius, ...). Tunable
//         without touching readers, because the codec is versioned.
// =============================================================================

#include "next/core/contracts.hpp"
#include "next/experimental/preset.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace r62 {
namespace experimental {

// ---------------------------------------------------------------------------
// Measurement semantics
// ---------------------------------------------------------------------------
enum class MeasureOrigin : uint8_t {
    Unspecified      = 0,   // no value; `present` must be false
    UserEntered      = 1,   // typed by the user
    InstrumentFile   = 2,   // read from an instrument export
    VendorReference  = 3    // a vendor/literature reference value
};

const char* measureOriginName(MeasureOrigin origin);
bool measureOriginFromName(const std::string& name, MeasureOrigin* out);

// A value WITH a unit and a provenance. The point of the type is that "42" is
// not an answer: value 42 without a unit and a source is not accepted as a
// measurement, and validateMeasurement() says why.
struct MeasurementValue {
    double        value;
    std::string   unit;      // e.g. "kDa", "ug", "Rf" (Rf is dimensionless: "-")
    MeasureOrigin origin;
    std::string   source;    // e.g. "ladder lane 1", "user note"
    bool          present;   // false == the field is simply not set

    MeasurementValue() : value(0.0), origin(MeasureOrigin::Unspecified), present(false) {}
    // Ok when absent; when present it must be finite, carry a unit and a
    // provenance. Empty-collection-typed values are refused rather than stored.
    core::Error validate(std::string* why = 0) const;
};

struct NamedMeasurement {
    std::string      name;    // e.g. "mobile phase", "marker mass"
    MeasurementValue value;
    NamedMeasurement() {}
    NamedMeasurement(const std::string& n, const MeasurementValue& v) : name(n), value(v) {}
};

// ---------------------------------------------------------------------------
// Elements. Every id is a figure-local stable id (>= 1; 0 is never a valid id).
// ---------------------------------------------------------------------------
struct Lane {
    uint32_t    id;
    double      u;             // centre, relative
    double      width;         // full width in u units (half-width = width / 2)
    std::string label;         // sample name / lane caption
    std::string note;
    bool        dashedBorder;  // display
    Lane() : id(0), u(0.0), width(0.25), dashedBorder(false) {}
};

struct Spot {
    uint32_t         id;
    uint32_t         laneId;    // owning lane (0 == not assigned to a lane)
    double           u, v;      // centre, relative board coordinates
    double           radiusU;   // relative half-extent along u
    double           radiusV;   // relative half-extent along v
    uint32_t         fillRGBA;  // display colour
    double           opacity;   // display alpha multiplier, 0..1
    std::string      shape;     // "ellipse" (default); reserved vocabulary
    std::string      label;     // optional annotation for this spot
    MeasurementValue rfMeasured;// OPTIONAL measured Rf (not derived)
    Spot() : id(0), laneId(0), u(0.0), v(0.0), radiusU(0.03), radiusV(0.03),
             fillRGBA(0x303030FFu), opacity(1.0), shape("ellipse") {}
};

struct Band {
    uint32_t id;
    uint32_t laneId;
    double   v;                // centre, relative
    double   thicknessV;       // full thickness in v units
    double   widthFraction;    // 0..1 of its lane's width
    double   displayIntensity; // DISPLAY darkness 0..1; NOT a measured amount
    uint32_t fillRGBA;
    std::string label;
    MeasurementValue markerMass;// OPTIONAL measured value with a unit
    Band() : id(0), laneId(0), v(0.0), thicknessV(0.045), widthFraction(0.85),
             displayIntensity(0.7), fillRGBA(0x202020FFu) {}
};

// An annotation slot: a relative position at which a T09 text object is drawn.
// The text itself lives in a real core::Text object (T09 owns its runs and its
// editing), so this record only says WHERE it belongs and WHICH object it is.
struct FigureAnnotation {
    std::string    slot;    // "title", "origin", "front", "lane:<id>", "spot:<id>", "free"
    double         u, v;    // relative position of the text anchor
    core::ObjectId textId;  // core::Text object, or kInvalidObjectId when detached
    FigureAnnotation() : u(0.0), v(0.0), textId(core::kInvalidObjectId) {}
};

// ---------------------------------------------------------------------------
// The figure
// ---------------------------------------------------------------------------
struct PlateFigure {
    FigureKind kind;

    // Origin line / solvent front. Each is independently "defined"; Rf exists
    // only when BOTH are defined and frontV < originV.
    bool   originDefined;
    double originV;
    bool   frontDefined;
    double frontV;

    std::vector<Lane>   lanes;
    std::vector<Spot>   spots;   // TLC
    std::vector<Band>   bands;   // gel

    std::string title;

    // display
    uint32_t         plateFillRGBA;
    bool             plateFillEnabled;
    core::LineStyle  border;
    bool             showLaneBorders;
    double           laneMarginV;   // vertical half-extent of lane guide lines (PROJ)

    // figure-level measurements (mobile phase, voltage, run time, ...)
    std::vector<NamedMeasurement> fields;

    // T09 text anchors
    std::vector<FigureAnnotation> annotations;

    uint32_t nextElementId;      // monotonic; ids are never reused
    uint32_t payloadVersion;

    PlateFigure();
};

// ---------------------------------------------------------------------------
// Defaults / factories  [PROJ unless marked]
// ---------------------------------------------------------------------------
PlateFigure makeDefaultFigure(FigureKind kind, uint32_t laneCount);

// ---------------------------------------------------------------------------
// Element ids
// ---------------------------------------------------------------------------
uint32_t allocateElementId(PlateFigure* figure);   // advances nextElementId, never returns 0
bool     elementIdInUse(const PlateFigure& figure, uint32_t id);
const Lane* findLane(const PlateFigure& figure, uint32_t id);
const Spot* findSpot(const PlateFigure& figure, uint32_t id);
const Band* findBand(const PlateFigure& figure, uint32_t id);
Lane*       mutableLane(PlateFigure* figure, uint32_t id);
Spot*       mutableSpot(PlateFigure* figure, uint32_t id);
Band*       mutableBand(PlateFigure* figure, uint32_t id);

// ---------------------------------------------------------------------------
// Validation. "Illegal parameters must not damage the document" is implemented
// as: every editor mutation validates BEFORE it writes, and the transaction
// aborts when validation fails, so the document is byte-identical afterwards.
// ---------------------------------------------------------------------------
core::Error validateFigure(const PlateFigure& figure, std::string* why = 0);

// ---------------------------------------------------------------------------
// Rf
// ---------------------------------------------------------------------------
struct RfResult {
    bool        valid;
    double      rf;
    std::string basis;   // the formula and the numbers it used
    std::string why;     // non-empty when valid == false
    RfResult() : valid(false), rf(0.0) {}
};

bool     rfMappingDefined(const PlateFigure& figure, std::string* why);
RfResult geometricRf(const PlateFigure& figure, double v);
RfResult spotRf(const PlateFigure& figure, const Spot& spot);
// Inverse mapping; returns a NaN when the mapping is undefined.
double   relativeVFromRf(const PlateFigure& figure, double rf);

// ---------------------------------------------------------------------------
// Payload codec (object extension bag value)
// ---------------------------------------------------------------------------
// Format: records separated by ';', fields by '|', first record "v|1".
// String fields are percent-escaped for '%', '|', ';' and any byte outside
// 0x21..0x7E, so a label may contain any UTF-8 text. Unknown record tags are
// ignored on read (forward compatibility); a malformed record makes decode fail
// with *ok == false and a default-constructed figure.
extern const char* const kFigureParamsKey;   // "exp.figure.params.1"

std::string  encodeFigure(const PlateFigure& figure);
PlateFigure  decodeFigure(const std::string& text, bool* ok);

// Stable content signature: changes exactly when something that affects the
// drawn result changes. Used as render::Primitive::version.
uint64_t figureContentSignature(const PlateFigure& figure);

std::string percentEncode(const std::string& value);
bool        percentDecode(const std::string& text, std::string* out);

} // namespace experimental
} // namespace r62

#endif // R62_NEXT_EXPERIMENTAL_FIGURE_HPP
