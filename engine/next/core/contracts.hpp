#ifndef R62_NEXT_CORE_CONTRACTS_HPP
#define R62_NEXT_CORE_CONTRACTS_HPP
// =============================================================================
// re_round62_viewer/next/core/contracts.hpp
//
// T01 - PUBLIC CONTRACTS for the shared document / command / tool layer.
//
// This header is the single source of truth for cross-module vocabulary.
// Downstream modules (T02 persistence, T03 rendering, T04 platform input,
// T19 host assembly) MUST NOT redefine any type declared here. If a change is
// needed, request it from T01 and it will be versioned.
//
// -----------------------------------------------------------------------------
// UNITS AND COORDINATES  (normative - do not flip y anywhere else)
// -----------------------------------------------------------------------------
//  * Document space: x grows to the RIGHT, y grows DOWNWARD.
//  * Document space is the same handedness as Win32 client coordinates and as
//    CDXML page coordinates. No module may apply its own y flip.
//  * Lengths are in CDXML "points" (1/72 inch), type double. A bond drawn at
//    the document DefaultBondLength is exactly that many points long.
//  * Angles: degrees, measured counter-clockwise in MATH orientation
//    (screen-visible counter-clockwise, i.e. decreasing y for +sin). Helper
//    contracts::direction() is the only sanctioned way to turn an angle into a
//    document-space unit vector.
//  * DPI is a property of exactly one object: the ViewTransform. Document
//    space never contains DPI.
//  * ViewTransform maps document -> device pixels:
//        device = (doc - center) * scale * (dpi/72) + viewportCenter
//        doc    = (device - viewportCenter) / (scale * dpi/72) + center
//    "scale" is the user zoom (1.0 = 100%). The caller stores dpiScale =
//    dpi/72 once. Screen-space hit radii and snap radii are expressed in
//    device pixels and divided by (scale*dpiScale) before use in doc space -
//    that conversion lives in geometry.hpp, not in each module.
//
// -----------------------------------------------------------------------------
// IDENTITY  (four distinct concepts - never conflate them)
// -----------------------------------------------------------------------------
//  1. ObjectId   : internal stable slot index of an object inside ONE document.
//                  Monotonic within the document; deletions set alive=false and
//                  NEVER reuse or renumber the slot. This preserves the legacy
//                  CDXEDIT 1 invariant "ID == slot".
//  2. SourceId   : the id="..." attribute from the originating CDXML file, or 0
//                  when the object was not read from CDXML. Source ids are only
//                  unique inside one source document; two objects created by
//                  two separate imports can share a SourceId.
//  3. DocumentId : 64-bit identity of the document INSTANCE (new/load/clone all
//                  produce a fresh one). Used to reject stale view caches and
//                  stale host bindings.
//  4. ObjectRef  : (kind, ObjectId) pair. A bare integer id is ambiguous once
//                  more than one object slab exists; all public APIs that
//                  accept "an object" take an ObjectRef.
//
// -----------------------------------------------------------------------------
// ERROR HANDLING
// -----------------------------------------------------------------------------
//  Every command/validation entry point returns core::Error. Exceptions are
//  used only for programmer errors (out-of-range access, poisoned document) and
//  are always std::logic_error/std::out_of_range derived. No public function
//  throws for user-triggered failures.
//
// Threading: see THREADING.md. Short version: one document == one thread. The
// command log has a documented single-writer / multi-reader snapshot rule.
// =============================================================================

#include <cstddef>
#include <cstdint>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace r62 {
namespace core {

// ---------------------------------------------------------------------------
// Interface version
// ---------------------------------------------------------------------------
// Bumped whenever a caller-visible signature changes. Downstream modules record
// the value they compiled against and pass it to requestCompatibility().
enum {
    kInterfaceMajor      = 1,
    kInterfaceMinor      = 0,
    kInterfaceRevision   = 0,
    kInterfaceVersion    = kInterfaceMajor * 10000 + kInterfaceMinor * 100 + kInterfaceRevision
};

// Native document format written by serialization.hpp. Changing the on-disk
// layout of a released record requires a new value plus a migration reader.
enum {
    kNativeFormatVersion = 1
};

// ---------------------------------------------------------------------------
// Error
// ---------------------------------------------------------------------------
enum class ErrorCode {
    Ok = 0,
    NotImplemented,
    InvalidArgument,
    OutOfRange,
    NotFound,
    AlreadyExists,
    NotAlive,
    WrongKind,          // ObjectRef kind does not match the slab holding the id
    InvalidGeometry,
    InvalidSemantics,   // e.g. unknown aromatic representation for a given order
    InvalidText,
    DuplicateBond,
    SelfBond,
    DocumentMismatch,   // refs resolved against the wrong DocumentId
    RevisionConflict,
    Locked,             // read-only / protected source
    RolledBack,         // enclosing transaction was aborted
    Unsupported,        // valid per spec but deliberately out of scope here
    IoError,
    ParseError,
    Truncated,
    LimitExceeded
};

const char* errorName(ErrorCode code);

// Rich result. message is a human-readable detail; it is never parsed.
struct Error {
    ErrorCode code;
    std::string message;
    Error() : code(ErrorCode::Ok) {}
    explicit Error(ErrorCode c) : code(c) { if (c != ErrorCode::Ok) message = errorName(c); }
    Error(ErrorCode c, const std::string& m) : code(c), message(m) {}
    bool ok() const { return code == ErrorCode::Ok; }
    static Error success() { return Error(); }
    static Error fail(ErrorCode c, const std::string& m = std::string()) { return Error(c, m); }
};

// ---------------------------------------------------------------------------
// Value types
// ---------------------------------------------------------------------------
struct Vec2 {
    double x, y;
    Vec2() : x(0), y(0) {}
    Vec2(double x_, double y_) : x(x_), y(y_) {}
    bool finite() const { return std::isfinite(x) && std::isfinite(y); }
    bool operator==(const Vec2& o) const { return x == o.x && y == o.y; }
    bool operator!=(const Vec2& o) const { return !(*this == o); }
};
inline Vec2 operator+(const Vec2& a, const Vec2& b) { return Vec2(a.x + b.x, a.y + b.y); }
inline Vec2 operator-(const Vec2& a, const Vec2& b) { return Vec2(a.x - b.x, a.y - b.y); }
inline Vec2 operator*(const Vec2& a, double s) { return Vec2(a.x * s, a.y * s); }
inline double dot(const Vec2& a, const Vec2& b) { return a.x * b.x + a.y * b.y; }
inline double lengthSq(const Vec2& a) { return a.x * a.x + a.y * a.y; }
inline double length(const Vec2& a) { return std::sqrt(lengthSq(a)); }

struct Box {
    Vec2 min, max;
    Box() : min(0, 0), max(0, 0) {}
    Box(const Vec2& a, const Vec2& b) : min(a), max(b) {}
    bool empty() const { return max.x < min.x || max.y < min.y; }
    double width() const { return max.x - min.x; }
    double height() const { return max.y - min.y; }
    void add(const Vec2& p) {
        if (p.x < min.x) min.x = p.x;
        if (p.y < min.y) min.y = p.y;
        if (p.x > max.x) max.x = p.x;
        if (p.y > max.y) max.y = p.y;
    }
    bool contains(const Vec2& p) const { return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y; }
    bool operator==(const Box& o) const { return min == o.min && max == o.max; }
    bool operator!=(const Box& o) const { return !(*this == o); }
};

// Axis-aligned affine transform. Document -> device pixels.
struct ViewTransform {
    Vec2 center;        // document point shown at viewportCenter
    double scale;       // user zoom, 1.0 == 100%
    double dpiScale;    // deviceDpi / 72.0 ; supplied by the platform module
    Vec2 viewportCenter;// device pixels, usually (width/2, height/2)
    ViewTransform() : center(0, 0), scale(1.0), dpiScale(1.0), viewportCenter(0, 0) {}
    double docPerDevice() const { return (scale * dpiScale) == 0.0 ? 0.0 : 1.0 / (scale * dpiScale); }
    Vec2 toDevice(const Vec2& d) const {
        return Vec2((d.x - center.x) * scale * dpiScale + viewportCenter.x,
                    (d.y - center.y) * scale * dpiScale + viewportCenter.y);
    }
    Vec2 toDocument(const Vec2& s) const {
        const double k = docPerDevice();
        return Vec2((s.x - viewportCenter.x) * k + center.x,
                    (s.y - viewportCenter.y) * k + center.y);
    }
    // Convert a device-pixel radius (hit radius, snap radius) to doc units.
    double deviceRadiusToDocument(double r) const { return r * docPerDevice(); }
};

// Counter-clockwise-in-math-orientation unit vector, in document space (y down).
// angle 0 -> (1,0) right; angle 90 -> (0,-1) visually up.
inline Vec2 direction(double degrees) {
    const double r = degrees * 3.14159265358979323846 / 180.0;
    return Vec2(std::cos(r), -std::sin(r));
}
inline double angleOf(const Vec2& v) {
    return std::atan2(-v.y, v.x) * 180.0 / 3.14159265358979323846;
}

// ---------------------------------------------------------------------------
// Object classes and kinds
// ---------------------------------------------------------------------------
// ObjectClass groups kinds for rendering/persistence scheduling. ObjectKind is
// finer grained. Adding a kind is an additive interface change; adding an
// ObjectClass is a breaking one. Every kind here has a declared slab in
// Document, even when the slab is empty in every document we can currently
// produce (arrows/brackets/table/science/bio). This is deliberate: it makes the
// common extension container and the serialization format complete NOW rather
// than after a later refactor of the public model.
enum class ObjectClass : uint16_t {
    Unknown = 0,
    Node = 1,        // atoms and other labelled nodes
    Bond = 2,        // bonds and connectors
    RichText = 3,    // captions / annotations
    Path = 4,        // free curves, lines, shapes
    Arrow = 5,
    Bracket = 6,
    Table = 7,
    Science = 8,     // orbitals, symbols, TLC/GEP plates
    Bio = 9,         // BioDraw shapes
    Group = 10       // grouping / z-order records
};

enum class ObjectKind : uint16_t {
    Unknown = 0,
    Atom = 1,
    Bond = 2,
    Text = 3,
    Path = 4,
    Arrow = 5,
    Bracket = 6,
    Table = 7,
    Orbital = 8,
    Symbol = 9,
    BioShape = 10,
    Group = 11
};

ObjectClass classOf(ObjectKind kind);
const char* kindName(ObjectKind kind);

// Slab identifiers. Order matters only for stable indexing inside Document.
enum class Slab : uint8_t {
    Atom = 0,
    Bond = 1,
    Text = 2,
    Path = 3,
    Arrow = 4,
    Bracket = 5,
    Table = 6,
    Orbital = 7,
    Symbol = 8,
    BioShape = 9,
    Group = 10
};
enum { kSlabCount = 11 };

typedef uint32_t ObjectId;
typedef uint64_t DocumentId;
static const ObjectId kInvalidObjectId = 0xFFFFFFFFu;
static const DocumentId kInvalidDocumentId = 0;

struct ObjectRef {
    ObjectKind kind;
    ObjectId   id;
    ObjectRef() : kind(ObjectKind::Unknown), id(kInvalidObjectId) {}
    ObjectRef(ObjectKind k, ObjectId i) : kind(k), id(i) {}
    bool valid() const { return kind != ObjectKind::Unknown && id != kInvalidObjectId; }
    bool operator==(const ObjectRef& o) const { return kind == o.kind && id == o.id; }
    bool operator!=(const ObjectRef& o) const { return !(*this == o); }
    bool operator<(const ObjectRef& o) const {
        return kind != o.kind ? kind < o.kind : id < o.id;
    }
};

// ---------------------------------------------------------------------------
// Bond semantics: ORDER / DISPLAY / SEMANTIC are three separate axes.
// ---------------------------------------------------------------------------
// Never encode "aromatic" or "dative" or "query" as order==4,5,... . The legacy
// CDXEDIT 1 format only had order in [1,3]; migration maps it to
// (orderValue=1,2,3, display=Normal, semantics=Normal). Aromatic benzene needs
// (order=SINGLE_OR_DOUBLE, semantics=AROMATIC) or (order=AROMATIC, ...), and a
// coordinate/dative bond needs semantics=DATIVE - all without touching an
// integer that a renderer might interpret as "draw N parallel lines".
enum class BondOrder : uint8_t {
    Unspecified = 0,
    Single  = 1,
    Double  = 2,
    Triple  = 3,
    Quadruple = 4,
    Aromatic = 5,           // ChemDraw "aromatic" order
    SingleOrDouble = 6,     // query
    SingleOrAromatic = 7,   // query
    DoubleOrAromatic = 8,   // query
    Any = 9                 // query "any bond"
};

enum class BondDisplay : uint8_t {
    Normal = 0,
    Wedge = 1,              // solid wedge (up stereo)
    Hash = 2,               // hashed wedge (down stereo)
    WedgeHash = 3,          // "hashed wedge" variant
    HollowWedge = 4,
    Bold = 5,
    Dashed = 6,
    Dotted = 7,
    Wavy = 8
};

enum class BondSemantics : uint8_t {
    Normal = 0,
    Aromatic = 1,
    Dative = 2,             // coordinate / donor-acceptor
    Hydrogen = 3,           // dotted hydrogen bond
    Query = 4,              // any explicit query-style bond
    Unsaturated = 5         // deliberately unspecified multiplicity
};

// Stereo at bond level, independent of the drawn display.
enum class BondStereo : uint8_t {
    None = 0,
    Up = 1,
    Down = 2,
    Either = 3
};

// ---------------------------------------------------------------------------
// Atom semantics
// ---------------------------------------------------------------------------
// Charge / radical / isotope / implicit hydrogens are explicit fields. They are
// NOT packed into an element number and NOT stored in the element field.
enum class Radical : uint8_t {
    None = 0,
    Singlet = 1,    // one unpaired electron, paired presentation
    Doublet = 2,    // one unpaired electron
    Triplet = 3     // two unpaired electrons
};

enum class NodeType : uint8_t {
    Element = 0,
    Nickname = 1,       // abbreviation such as Ph, Bn
    Fragment = 2,
    RGroup = 3,
    Query = 4,
    Polymer = 5,
    Attachment = 6,
    ExternalConnection = 7
};

struct AtomSemantics {
    int8_t     charge;              // -15..+15
    uint8_t    radical;             // core::Radical as uint8_t for POD stability
    int16_t    isotope;             // 0 == natural
    int8_t     implicitHydrogens;   // -1 == compute, else explicit count
    uint8_t    nodeType;            // core::NodeType
    bool       showLabel;           // force-draw the element label
    bool       aromatic;            // participates in an aromatic system
    AtomSemantics()
        : charge(0), radical(0), isotope(0), implicitHydrogens(-1),
          nodeType(static_cast<uint8_t>(NodeType::Element)),
          showLabel(false), aromatic(false) {}
    bool operator==(const AtomSemantics& o) const {
        return charge == o.charge && radical == o.radical && isotope == o.isotope &&
               implicitHydrogens == o.implicitHydrogens && nodeType == o.nodeType &&
               showLabel == o.showLabel && aromatic == o.aromatic;
    }
    bool operator!=(const AtomSemantics& o) const { return !(*this == o); }
};

// ---------------------------------------------------------------------------
// Rich text
// ---------------------------------------------------------------------------
enum class TextRole : uint8_t {
    Caption = 0,        // free caption / annotation (legacy editor::Text)
    AtomLabel = 1,      // label owned by an atom
    Name = 2,
    Condition = 3,
    TableCell = 4
};

enum TextFlags : uint16_t {
    kTextNone      = 0,
    kBold          = 1u << 0,
    kItalic        = 1u << 1,
    kUnderline     = 1u << 2,
    kSubscript     = 1u << 3,
    kSuperscript   = 1u << 4,
    kFormula       = 1u << 5,
    kHighlight     = 1u << 6
};

enum class TextAlign : uint8_t { Left = 0, Center = 1, Right = 2 };

// A run is a maximal span sharing identical formatting. Plain text is one run.
struct TextRun {
    std::string utf8;
    uint16_t    flags;
    std::string fontName;   // empty == document default font
    double      size;       // 0 == inherit object size
    TextRun() : flags(0), size(0) {}
    bool operator==(const TextRun& o) const {
        return utf8 == o.utf8 && flags == o.flags && fontName == o.fontName && size == o.size;
    }
    bool operator!=(const TextRun& o) const { return !(*this == o); }
};

// ---------------------------------------------------------------------------
// Style shared by drawable objects. Kept small; richer appearance lives in the
// extension container until it is promoted into a public field.
// ---------------------------------------------------------------------------
struct LineStyle {
    double      width;          // document units; 0 == document default
    uint32_t    colorRGBA;      // 0xRRGGBBAA
    uint8_t     dash;           // core::DashStyle
    uint8_t     lineCap;
    LineStyle() : width(0.0), colorRGBA(0x000000FFu), dash(0), lineCap(0) {}
    bool operator==(const LineStyle& o) const {
        return width == o.width && colorRGBA == o.colorRGBA && dash == o.dash && lineCap == o.lineCap;
    }
    bool operator!=(const LineStyle& o) const { return !(*this == o); }
};

enum class DashStyle : uint8_t { Solid = 0, Dashed = 1, Dotted = 2, Wavy = 3 };

struct TextFormat {
    std::string fontName;
    double      size;           // 0 == document default text size
    uint16_t    flags;
    uint8_t     align;
    uint32_t    colorRGBA;
    TextFormat() : size(0.0), flags(0), align(0), colorRGBA(0xFF000000u) {}
    bool operator==(const TextFormat& o) const {
        return fontName == o.fontName && size == o.size && flags == o.flags &&
               align == o.align && colorRGBA == o.colorRGBA;
    }
    bool operator!=(const TextFormat& o) const { return !(*this == o); }
};

// ---------------------------------------------------------------------------
// Extension container
// ---------------------------------------------------------------------------
// Preserves source attributes/objects we do not model natively. THIS IS NOT A
// LOSSLESS CDXML ROUND TRIP BY ITSELF: it preserves recorded unknowns for a
// document loaded through an adapter that actually records them, and it is the
// prerequisite. It does not restore comments, ordering of unknown siblings,
// DTD-level details, or anything no adapter ever read.
struct ExtensionEntry {
    std::string key;    // e.g. "cdxml.attr.Element" or "cdxml.node.arrow"
    std::string value;  // opaque; adapters own the encoding
    int32_t     sourceId;
    ExtensionEntry() : sourceId(0) {}
    ExtensionEntry(const std::string& k, const std::string& v, int32_t sid = 0)
        : key(k), value(v), sourceId(sid) {}
    bool operator==(const ExtensionEntry& o) const {
        return key == o.key && value == o.value && sourceId == o.sourceId;
    }
    bool operator!=(const ExtensionEntry& o) const { return !(*this == o); }
};

class ExtensionBag {
public:
    ExtensionBag() {}
    bool empty() const { return !entries_ || entries_->empty(); }
    size_t size() const { return entries_ ? entries_->size() : 0; }
    const std::vector<ExtensionEntry>& entries() const {
        static const std::vector<ExtensionEntry> emptyEntries;
        return entries_ ? *entries_ : emptyEntries;
    }
    const ExtensionEntry* find(const std::string& key, int32_t sourceId = 0) const {
        const auto& values=entries();
        for (size_t i = 0; i < values.size(); ++i)
            if (values[i].key == key && values[i].sourceId == sourceId) return &values[i];
        return 0;
    }
    const std::string* findValue(const std::string& key, int32_t sourceId = 0) const {
        const ExtensionEntry* e = find(key, sourceId);
        return e ? &e->value : 0;
    }
    // Replace-or-insert. Keeps insertion order, so serialization is stable.
    void set(const std::string& key, const std::string& value, int32_t sourceId = 0) {
        const auto& values=entries();
        for (size_t i = 0; i < values.size(); ++i) {
            if (values[i].key == key && values[i].sourceId == sourceId) {
                if(values[i].value==value)return;
                writable()[i].value = value;
                return;
            }
        }
        writable().push_back(ExtensionEntry(key, value, sourceId));
    }
    bool erase(const std::string& key, int32_t sourceId = 0) {
        const auto& values=entries();
        for (size_t i = 0; i < values.size(); ++i) {
            if (values[i].key == key && values[i].sourceId == sourceId) {
                auto& dst=writable();
                dst.erase(dst.begin() + static_cast<std::ptrdiff_t>(i));
                return true;
            }
        }
        return false;
    }
    void clear() { entries_.reset(); }
    bool operator==(const ExtensionBag& o) const { return entries_==o.entries_ || entries()==o.entries(); }
    bool operator!=(const ExtensionBag& o) const { return !(*this == o); }
private:
    std::vector<ExtensionEntry>& writable() {
        if(!entries_)entries_=std::make_shared<std::vector<ExtensionEntry>>();
        else if(!entries_.unique())entries_=std::make_shared<std::vector<ExtensionEntry>>(*entries_);
        return *entries_;
    }
    // Snapshots share immutable entries until either copy is changed.
    std::shared_ptr<std::vector<ExtensionEntry>> entries_;
};

// ---------------------------------------------------------------------------
// Reserved extension key namespaces (documentation, not enforcement).
//   "cdxml.attr.<AttrName>"    : a source attribute we did not promote
//   "cdxml.node.<ElementName>" : a source element we did not promote
//   "cdxml.raw.<hash>"         : verbatim source fragment
//   "render.<module>.<name>"   : renderer-private cache hints (never persisted)
//   "host.<module>.<name>"     : host adapter private data
//   "user.<module>.<name>"     : other modules
// ---------------------------------------------------------------------------
inline bool isPersistentExtensionKey(const std::string& key) {
    return key.compare(0, 6, "cdxml.") == 0;
}

// ---------------------------------------------------------------------------
// Document-level scalars
// ---------------------------------------------------------------------------
// The subset of a CDXML root/page that applies to the whole document. Held in a
// POD struct so a transaction can invert it and so "nothing changed" is a single
// comparison.
struct Defaults {
    double      bondLength;   // document units, the length of a freshly drawn bond
    double      lineWidth;    // document units, stroke width of newly created lines
    double      textSize;     // document units, size of newly created captions
    std::string font;         // default font name for newly created text
    Defaults() : bondLength(14.4), lineWidth(0.6), textSize(10.0), font("Arial") {}
    bool operator==(const Defaults& o) const {
        return bondLength == o.bondLength && lineWidth == o.lineWidth &&
               textSize == o.textSize && font == o.font;
    }
    bool operator!=(const Defaults& o) const { return !(*this == o); }
};

// ---------------------------------------------------------------------------
// Validity helpers shared by all modules
// ---------------------------------------------------------------------------
inline bool validCoordinate(double v) { return std::isfinite(v); }
inline bool validElement(int e) { return e >= 1 && e <= 118; }
inline bool validCharge(int c) { return c >= -15 && c <= 15; }
inline bool validBondOrderValue(BondOrder o) {
    return o >= BondOrder::Single && o <= BondOrder::Any;
}

// Strict UTF-8 + XML 1.0 character check. Same rules the legacy editor used;
// kept here so every module validates identically.
Error validateUtf8(const std::string& text);
Error validateText(const std::string& utf8, bool allowEmpty = true);

// Element symbols (1..118) and lookup. Index 0 is "?" deliberately.
const char* elementSymbol(int element);
int elementNumber(const std::string& symbol);

// Compatibility handshake for downstream modules.
inline bool requestCompatibility(int major, int minor) {
    return major == kInterfaceMajor && minor <= kInterfaceMinor;
}

} // namespace core
} // namespace r62

#endif // R62_NEXT_CORE_CONTRACTS_HPP
