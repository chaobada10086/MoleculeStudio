#include "next/arrows_curves/model.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <sstream>

namespace r62 {
namespace arrows {

const char* const kArrowParamsKey = "arc.arrow.params.1";
const char* const kCurveStyleKey  = "arc.curve.style.1";
// The anchor record lives in its OWN key rather than inside the params record so
// that connecting or disconnecting an arrow does not rewrite its style record.
// That is what makes "connect" and "disconnect" each a single small change that
// a transaction can carry without touching the appearance.
static const char* const kAnchorKey = "arc.arrow.anchor.1";

namespace {

std::string fmt(double v) {
    char buf[64];
    // %.17g is what makes a double survive a text encode/decode EXACTLY. %.9g
    // looks tidier but loses the low bits, and a "the parameter came back" check
    // then fails by about 1e-7 - which is exactly the kind of silent precision
    // loss that makes a save/reload look lossy.
    std::snprintf(buf, sizeof(buf), "%.17g", v);
    return std::string(buf);
}

std::string fmtInt(long long v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%lld", v);
    return std::string(buf);
}

void put(std::ostringstream& o, const char* key, const std::string& value) {
    o << key << '=' << value << ';';
}

// A tiny key/value reader. It does NOT use strtod directly on the raw pointer
// because a malformed value must be detectable, and "the field was absent" must
// be distinguishable from "the field was 0".
typedef std::map<std::string, std::string> Fields;

Fields parse(const std::string& text) {
    Fields out;
    size_t pos = 0;
    while (pos < text.size()) {
        const size_t eq = text.find('=', pos);
        if (eq == std::string::npos) break;
        const size_t end = text.find(';', eq);
        if (end == std::string::npos) break;
        out[text.substr(pos, eq - pos)] = text.substr(eq + 1, end - eq - 1);
        pos = end + 1;
    }
    return out;
}

bool fieldDouble(const Fields& f, const char* key, double* out) {
    Fields::const_iterator it = f.find(key);
    if (it == f.end() || it->second.empty()) return false;
    const char* s = it->second.c_str();
    char* stop = 0;
    const double v = std::strtod(s, &stop);
    if (stop == s || *stop != '\0') return false;
    if (!std::isfinite(v)) return false;
    *out = v;
    return true;
}

bool fieldLong(const Fields& f, const char* key, long long* out) {
    Fields::const_iterator it = f.find(key);
    if (it == f.end() || it->second.empty()) return false;
    const char* s = it->second.c_str();
    char* stop = 0;
    const long long v = std::strtoll(s, &stop, 10);
    if (stop == s || *stop != '\0') return false;
    *out = v;
    return true;
}

bool fieldBool(const Fields& f, const char* key, bool* out) {
    Fields::const_iterator it = f.find(key);
    if (it == f.end()) return false;
    if (it->second == "1") { *out = true;  return true; }
    if (it->second == "0") { *out = false; return true; }
    return false;
}

// Object refs are flattened to "kind:id". A bare id would be ambiguous once more
// than one slab can hold the same numeric id (core/contracts.hpp says exactly
// this about ObjectRef).
std::string refText(const core::ObjectRef& ref) {
    if (!ref.valid()) return "-";
    return fmtInt(static_cast<long long>(ref.kind)) + ":" +
           fmtInt(static_cast<long long>(ref.id));
}

core::ObjectRef parseRef(const std::string& text) {
    if (text == "-" || text.empty()) return core::ObjectRef();
    const size_t colon = text.find(':');
    if (colon == std::string::npos) return core::ObjectRef();
    const int kindN = std::atoi(text.substr(0, colon).c_str());
    const long long idN = std::strtoll(text.substr(colon + 1).c_str(), 0, 10);
    // ObjectId 0 is a LEGAL slab index, so "id > 0" is the wrong test. The only
    // invalid id is kInvalidObjectId, which is also what the "-" spelling (an
    // absent reference) produces.
    if (idN < 0 || static_cast<unsigned long long>(idN) ==
                       static_cast<unsigned long long>(core::kInvalidObjectId))
        return core::ObjectRef();
    return core::ObjectRef(static_cast<core::ObjectKind>(kindN),
                           static_cast<core::ObjectId>(idN));
}

// The half-sweep flag is not a stored field; it is recoverable from the preset,
// so the record does not duplicate it. Documenting that here means a reader does
// not have to guess why the record looks short.

} // namespace

// ---------------------------------------------------------------------------
// encodeArrowParams / decodeArrowParams
// ---------------------------------------------------------------------------
std::string encodeArrowParams(const ArrowParams& p) {
    std::ostringstream o;
    o << "v=1;";
    put(o, "preset",  fmtInt(static_cast<long long>(p.presetId)));
    put(o, "family",  fmtInt(static_cast<long long>(p.family)));
    put(o, "size",    fmtInt(static_cast<long long>(p.sizeClass)));
    put(o, "line",    fmtInt(static_cast<long long>(p.line)));
    put(o, "shafts",  fmtInt(static_cast<long long>(p.shafts)));
    put(o, "sweep",   fmtInt(static_cast<long long>(p.sweep)));
    put(o, "dir",     fmtInt(static_cast<long long>(p.direction)));
    put(o, "hshape",  fmtInt(static_cast<long long>(p.head.shape)));
    put(o, "hside",   fmtInt(static_cast<long long>(p.head.sides)));
    put(o, "hlen",    fmt(p.head.length));
    put(o, "hwidth",  fmt(p.head.width));
    put(o, "hend",    p.head.atEnd   ? "1" : "0");
    put(o, "hbegin",  p.head.atBegin ? "1" : "0");
    put(o, "bx",      fmt(p.begin.x));
    put(o, "by",      fmt(p.begin.y));
    put(o, "ex",      fmt(p.end.x));
    put(o, "ey",      fmt(p.end.y));
    put(o, "sag",     fmt(p.arcSagitta));
    put(o, "sep",     fmt(p.separation));
    put(o, "split",   fmt(p.splitRatio));
    put(o, "osplit",  fmt(p.originalSplitRatio));
    put(o, "scale",   fmt(p.scale));
    if(p.strokeWidth>0)put(o,"stroke",fmt(p.strokeWidth));
    return o.str();
}

ArrowParams decodeArrowParams(const std::string& text, bool* ok) {
    ArrowParams out;
    if (ok) *ok = false;
    const Fields f = parse(text);

    long long v = 0;
    // The version field is the one field we require: a record without it is not
    // ours and must not be half-read.
    if (!fieldLong(f, "v", &v) || v != 1) return out;

    // Geometry is required: without two finite endpoints there is no arrow.
    double bx, by, ex, ey;
    if (!fieldDouble(f, "bx", &bx) || !fieldDouble(f, "by", &by) ||
        !fieldDouble(f, "ex", &ex) || !fieldDouble(f, "ey", &ey)) return out;
    out.begin = core::Vec2(bx, by);
    out.end   = core::Vec2(ex, ey);

    // Everything else is OPTIONAL with a defined default, so an older writer
    // that omitted a field still yields a usable arrow instead of a refusal.
    long long n = 0;
    if (fieldLong(f, "preset", &n)) out.presetId = static_cast<uint32_t>(n);
    if (fieldLong(f, "family", &n)) out.family = static_cast<ArrowFamily>(n);
    if (fieldLong(f, "size",   &n)) out.sizeClass = static_cast<ArrowSize>(n);
    if (fieldLong(f, "line",   &n)) out.line = static_cast<ArrowLine>(n);
    if (fieldLong(f, "shafts", &n)) out.shafts = static_cast<ShaftCount>(n);
    if (fieldLong(f, "sweep",  &n)) out.sweep = static_cast<ArcSweep>(n);
    if (fieldLong(f, "dir",    &n)) out.direction = static_cast<ArcDirection>(n);
    if (fieldLong(f, "hshape", &n)) out.head.shape = static_cast<ArrowHeadShape>(n);
    if (fieldLong(f, "hside",  &n)) out.head.sides = static_cast<HeadSides>(n);
    double d = 0.0;
    if(f.find("stroke")!=f.end()){if(!fieldDouble(f,"stroke",&d))return out;out.strokeWidth=d;}
    if (fieldDouble(f, "hlen",   &d)) out.head.length = d;
    if (fieldDouble(f, "hwidth", &d)) out.head.width  = d;
    bool b = false;
    if (fieldBool(f, "hend",   &b)) out.head.atEnd   = b;
    if (fieldBool(f, "hbegin", &b)) out.head.atBegin = b;
    if (fieldDouble(f, "sag",    &d)) out.arcSagitta = d;
    if (fieldDouble(f, "sep",    &d)) out.separation = d;
    if (fieldDouble(f, "split",  &d)) out.splitRatio = d;
    if (fieldDouble(f, "osplit", &d)) out.originalSplitRatio = d;
    if (fieldDouble(f, "scale",  &d)) out.scale = d;

    // The record must describe a VALID arrow, not merely a parseable one. This is
    // the difference between "the bytes were there" and "the arrow is usable".
    const core::Error valid = validateArrowParams(out);
    if (!valid.ok()) return ArrowParams();
    if (ok) *ok = true;
    return out;
}

std::string encodeCurveStyle(const CurveStyleSpec& s) {
    std::ostringstream o;
    o << "v=1;";
    put(o, "preset", fmtInt(static_cast<long long>(s.presetId)));
    put(o, "closed", s.closed  ? "1" : "0");
    put(o, "dashed", s.dashed  ? "1" : "0");
    put(o, "bold",   s.bold    ? "1" : "0");
    put(o, "dbl",    s.doubled ? "1" : "0");
    put(o, "faded",  s.faded   ? "1" : "0");
    put(o, "shaded", s.shaded  ? "1" : "0");
    put(o, "filled", s.filled  ? "1" : "0");
    return o.str();
}

CurveStyleSpec decodeCurveStyle(const std::string& text, bool* ok) {
    CurveStyleSpec out;
    if (ok) *ok = false;
    const Fields f = parse(text);
    long long v = 0;
    if (!fieldLong(f, "v", &v) || v != 1) return out;
    long long n = 0;
    if (fieldLong(f, "preset", &n)) out.presetId = static_cast<uint32_t>(n);
    bool b = false;
    if (fieldBool(f, "closed", &b)) out.closed  = b;
    if (fieldBool(f, "dashed", &b)) out.dashed  = b;
    if (fieldBool(f, "bold",   &b)) out.bold    = b;
    if (fieldBool(f, "dbl",    &b)) out.doubled = b;
    if (fieldBool(f, "faded",  &b)) out.faded   = b;
    if (fieldBool(f, "shaded", &b)) out.shaded  = b;
    if (fieldBool(f, "filled", &b)) out.filled  = b;
    // Fill implies closure; a record that says otherwise is repaired rather than
    // rejected, because the repair is unambiguous and a refusal would lose the
    // user's curve.
    if (out.filled) out.closed = true;
    // Re-attach the human-readable identity when the preset is known.
    if (const CurveStyleSpec* spec = curveStyleById(out.presetId)) {
        out.sourceCommand = spec->sourceCommand;
        out.toolId        = spec->toolId;
        out.displayName   = spec->displayName;
        out.note          = spec->note;
        out.implemented   = spec->implemented;
    }
    if (out.sourceCommand == 0) out.sourceCommand = "arc.curve.style.custom";
    if (out.toolId == 0)        out.toolId        = "r62.tool.curve.style.custom";
    if (out.displayName == 0)   out.displayName   = "Custom";
    if (ok) *ok = true;
    return out;
}

// ---------------------------------------------------------------------------
// Signatures
// ---------------------------------------------------------------------------
// FNV-1a over the encoded record. Used by the renderer cache and by the test
// that asserts "a style change moves the signature and a no-op does not".
uint64_t arrowParamsSignature(const ArrowParams& params) {
    const std::string text = encodeArrowParams(params);
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < text.size(); ++i) {
        h ^= static_cast<uint64_t>(static_cast<unsigned char>(text[i]));
        h *= 1099511628211ull;
    }
    return h;
}

// ---------------------------------------------------------------------------
// Record adapters
// ---------------------------------------------------------------------------
core::Error toAnnotationRecord(const ArrowParams& params, double documentLineWidth,
                               core::AnnotationRecord* out) {
    if (!out) return core::Error(core::ErrorCode::InvalidArgument, "no output record");
    const core::Error v = validateArrowParams(params);
    if (!v.ok()) return v;

    core::AnnotationRecord r;
    r.kind = core::ObjectKind::Arrow;
    r.presetId = params.presetId;
    r.scale = params.scale;
    r.rotation = 0.0;
    // Geometry slots. The FROZEN convention the CDXML reader already writes is:
    //   geometry[0] = first endpoint, geometry[1] = second endpoint (a "pair"),
    //   geometry[2] = box corner A,   geometry[3] = box corner B.
    // This module keeps that convention: a consumer that only reads geometry[0]
    // and geometry[1] (which is what the T02 reader/writer already does) sees a
    // correct line.
    r.geometry[0] = params.begin;
    r.geometry[1] = params.end;
    r.geometry[2] = params.begin;
    r.geometry[3] = params.end;
    r.style.width = arrowStrokeWidth(params, documentLineWidth) /
                    (params.scale > 0.0 ? params.scale : 1.0);
    r.style.colorRGBA = 0xFF000000u;
    r.style.dash = (params.line == ArrowLine::Dashed)
                       ? static_cast<uint8_t>(core::DashStyle::Dashed)
                       : static_cast<uint8_t>(core::DashStyle::Solid);
    r.fillRGBA = (params.line == ArrowLine::Hollow) ? 0x00000000u : 0xFF000000u;
    r.alive = 1;
    *out = r;
    return core::Error::success();
}

ArrowParams fromAnnotationRecord(const core::AnnotationRecord& record,
                                 const core::ExtensionBag& bag, bool* ok) {
    if (ok) *ok = false;
    if (const std::string* encoded = bag.findValue(kArrowParamsKey)) {
        bool parsed = false;
        ArrowParams p = decodeArrowParams(*encoded, &parsed);
        if (parsed) {
            // The FROZEN record is authoritative for the two endpoints: a stale
            // bag must not be able to move an arrow that a non-T10 tool moved
            // (a plain commands::translateObjects, for instance). Only the
            // appearance and curve parameters come from the bag.
            p.begin = record.geometry[0];
            p.end   = record.geometry[1];
            if (record.presetId) p.presetId = record.presetId;
            if (ok) *ok = true;
            return p;
        }
    }
    // Fallback: build a defensible arrow from the frozen fields alone. This is
    // what makes an arrow imported from a CDXML file (before T10 ever wrote one)
    // drawable rather than invisible.
    ArrowParams p;
    p.presetId  = record.presetId;
    p.begin     = record.geometry[0];
    p.end       = record.geometry[1];
    p.scale     = record.scale > 0.0 ? record.scale : 1.0;
    p.head.shape = (record.fillRGBA == 0x00000000u) ? ArrowHeadShape::Hollow
                                                     : ArrowHeadShape::Solid;
    p.head.sides = HeadSides::Both;
    p.head.atEnd = true;
    p.family     = ArrowFamily::Straight;
    if (record.style.dash == static_cast<uint8_t>(core::DashStyle::Dashed))
        p.line = ArrowLine::Dashed;
    // An imported arrow may have a box instead of a pair; prefer the pair when
    // the two coincide, which is the direction the reader recorded.
    if (p.begin == p.end && !(record.geometry[2] == record.geometry[3])) {
        p.begin = record.geometry[2];
        p.end   = record.geometry[3];
    }
    if (ok) *ok = false;   // honest: the rich parameters were NOT recovered
    return p;
}

// ---------------------------------------------------------------------------
// Document mutation adapters
// ---------------------------------------------------------------------------
// NOTE ON TRANSACTIONS: none of these opens one. T01 R1 says one USER ACTION ==
// one transaction, and a user action may be "drag a control point", which calls
// writeCurve many times during the gesture. The transaction belongs to the
// gesture, not to the record write. A caller that forgets to wrap these gets no
// undo, which is exactly the documented T01 rule for a bare mutator.
core::Error addArrow(core::Document& document, const ArrowParams& params,
                     double documentLineWidth, core::ObjectId* outId) {
    core::AnnotationRecord record;
    core::Error e = toAnnotationRecord(params, documentLineWidth, &record);
    if (!e.ok()) return e;
    core::ObjectId id = core::kInvalidObjectId;
    e = document.addAnnotation(core::ObjectKind::Arrow, record, &id);
    if (!e.ok()) return e;
    document.extensions(core::ObjectRef(core::ObjectKind::Arrow, id))
            .set(kArrowParamsKey, encodeArrowParams(params));
    document.noteTouchedExtensions(core::ObjectRef(core::ObjectKind::Arrow, id), false);
    if (outId) *outId = id;
    return core::Error::success();
}

core::Error writeArrow(core::Document& document, core::ObjectId id,
                       const ArrowParams& params, double documentLineWidth) {
    if (!document.arrows().alive(id))
        return core::Error(core::ErrorCode::NotFound, "arrow id is not alive");
    core::AnnotationRecord record;
    core::Error e = toAnnotationRecord(params, documentLineWidth, &record);
    if (!e.ok()) return e;
    // PRESERVE the anchors: a geometry or style rewrite must not silently
    // disconnect an arrow the user connected. [GUIDE p111]
    const core::AnnotationRecord* old = document.arrows().get(id);
    if (old) {
        for (int i = 0; i < 4; ++i) record.anchors[i] = old->anchors[i];
        record.sourceId = old->sourceId;
    }
    record.id = id;
    record.alive = 1;
    // Write through the slab and then note the touch, so the transaction session
    // records ONE change to this annotation rather than two.
    *document.arrows().mutableGet(id) = record;
    document.noteTouchedAnnotation(core::ObjectKind::Arrow, id);
    core::ExtensionBag& bag =
        document.extensions(core::ObjectRef(core::ObjectKind::Arrow, id));
    bag.set(kArrowParamsKey, encodeArrowParams(params));
    document.noteTouchedExtensions(core::ObjectRef(core::ObjectKind::Arrow, id), false);
    return core::Error::success();
}

core::Error readArrow(const core::Document& document, core::ObjectId id,
                      ArrowParams* out, bool* ok) {
    if (!out) return core::Error(core::ErrorCode::InvalidArgument, "no output");
    const core::AnnotationRecord* record = document.arrows().alive(id);
    if (!record) return core::Error(core::ErrorCode::NotFound, "arrow id is not alive");
    const core::ObjectRef ref(core::ObjectKind::Arrow, id);
    const core::ExtensionBag& bag = document.extensions(ref);
    *out = fromAnnotationRecord(*record, bag, ok);
    return core::Error::success();
}

core::Error addCurve(core::Document& document, const std::vector<core::Vec2>& points,
                     const std::vector<uint8_t>& pointKind, bool closed,
                     const CurveStyleSpec& style, double documentLineWidth,
                     core::ObjectId* outId) {
    if (points.size() < 2)
        return core::Error(core::ErrorCode::InvalidGeometry, "a curve needs at least 2 points");
    core::Error vs = validateCurveStyle(style);
    if (!vs.ok()) return vs;

    core::PathRecord record;
    record.points = points;
    record.pointKind = pointKind;
    if (record.pointKind.size() != record.points.size())
        record.pointKind.assign(record.points.size(), 0);
    record.closed = closed || style.closed;
    const double base = documentLineWidth > 0.0 ? documentLineWidth : 0.6;
    record.style.width = base * (style.bold ? 2.6 : 1.0);
    record.style.colorRGBA = style.faded ? 0x80808080u : 0xFF000000u;
    record.style.dash = style.dashed
        ? static_cast<uint8_t>(core::DashStyle::Dashed)
        : static_cast<uint8_t>(core::DashStyle::Solid);
    record.fillEnabled = style.filled ? 1 : 0;
    record.fillRGBA = style.faded ? 0x80808080u : 0xFF000000u;

    core::ObjectId id = core::kInvalidObjectId;
    core::Error e = document.addPath(record, &id);
    if (!e.ok()) return e;
    document.extensions(core::ObjectRef(core::ObjectKind::Path, id))
            .set(kCurveStyleKey, encodeCurveStyle(style));
    document.noteTouchedExtensions(core::ObjectRef(core::ObjectKind::Path, id), false);
    if (outId) *outId = id;
    return core::Error::success();
}

core::Error writeCurve(core::Document& document, core::ObjectId id,
                       const std::vector<core::Vec2>& points,
                       const std::vector<uint8_t>& pointKind, bool closed,
                       const CurveStyleSpec& style, double documentLineWidth) {
    core::PathRecord* record = document.paths().mutableGet(id);
    if (!record || !record->isAlive())
        return core::Error(core::ErrorCode::NotFound, "curve id is not alive");
    if (points.size() < 2)
        return core::Error(core::ErrorCode::InvalidGeometry, "a curve needs at least 2 points");
    core::Error vs = validateCurveStyle(style);
    if (!vs.ok()) return vs;

    record->points = points;
    record->pointKind = pointKind;
    if (record->pointKind.size() != record->points.size())
        record->pointKind.assign(record->points.size(), 0);
    record->closed = closed || style.closed;
    const double base = documentLineWidth > 0.0 ? documentLineWidth : 0.6;
    record->style.width = base * (style.bold ? 2.6 : 1.0);
    record->style.colorRGBA = style.faded ? 0x80808080u : 0xFF000000u;
    record->style.dash = style.dashed
        ? static_cast<uint8_t>(core::DashStyle::Dashed)
        : static_cast<uint8_t>(core::DashStyle::Solid);
    record->fillEnabled = style.filled ? 1 : 0;
    record->fillRGBA = style.faded ? 0x80808080u : 0xFF000000u;
    document.noteTouchedPath(id);
    core::ExtensionBag& bag =
        document.extensions(core::ObjectRef(core::ObjectKind::Path, id));
    bag.set(kCurveStyleKey, encodeCurveStyle(style));
    document.noteTouchedExtensions(core::ObjectRef(core::ObjectKind::Path, id), false);
    return core::Error::success();
}

core::Error readCurve(const core::Document& document, core::ObjectId id,
                      std::vector<core::Vec2>* points,
                      std::vector<uint8_t>* pointKind, bool* closed,
                      CurveStyleSpec* style, bool* ok) {
    const core::PathRecord* record = document.paths().alive(id);
    if (!record) return core::Error(core::ErrorCode::NotFound, "curve id is not alive");
    if (points) *points = record->points;
    if (pointKind) *pointKind = record->pointKind;
    if (closed) *closed = record->closed;
    bool haveStyle = false;
    CurveStyleSpec s;
    const core::ObjectRef ref(core::ObjectKind::Path, id);
    if (const std::string* text = document.extensions(ref).findValue(kCurveStyleKey)) {
        bool parsed = false;
        s = decodeCurveStyle(*text, &parsed);
        haveStyle = parsed;
    }
    if (!haveStyle) {
        // Reconstruct a style from the FROZEN PathRecord so an imported curve is
        // usable: the record already holds stroke width, dash and fill.
        s.presetId = 0;
        s.sourceCommand = "arc.curve.style.custom";
        s.toolId = "r62.tool.curve.style.custom";
        s.displayName = "Custom";
        s.note = "reconstructed from the frozen PathRecord fields";
        s.implemented = true;
        s.closed  = record->closed;
        s.dashed  = (record->style.dash == static_cast<uint8_t>(core::DashStyle::Dashed));
        s.bold    = record->style.width > 0.9;
        s.doubled = false;
        const uint8_t alpha = static_cast<uint8_t>(record->style.colorRGBA & 0xFFu);
        s.faded   = (alpha != 0u && alpha < 0xFFu);
        s.shaded  = false;
        s.filled  = record->fillEnabled != 0;
    }
    if (style) *style = s;
    if (ok) *ok = haveStyle;
    return core::Error::success();
}

// ---------------------------------------------------------------------------
// Anchors
// ---------------------------------------------------------------------------
// [GUIDE p111] "You can connect a new arrow to an existing arrow at either end
// or at the midpoint." The connection is persisted as an ObjectRef in one of the
// AnnotationRecord anchor slots, and the SLOT SEMANTICS (which of begin/end/mid
// the connection used) go into the bag. Splitting it that way means a non-T10
// reader still sees a valid `anchors[]` entry rather than an opaque string.
std::string encodeAnchor(const ArrowAnchor& anchor) {
    std::ostringstream o;
    o << "v=1;";
    put(o, "slot", fmtInt(static_cast<long long>(anchor.slot)));
    put(o, "target", refText(anchor.target));
    return o.str();
}

ArrowAnchor decodeAnchor(const std::string& text, bool* ok) {
    ArrowAnchor out;
    if (ok) *ok = false;
    const Fields f = parse(text);
    long long v = 0;
    if (!fieldLong(f, "v", &v) || v != 1) return out;
    long long slot = 0;
    if (!fieldLong(f, "slot", &slot)) return out;
    if (slot < 1 || slot > 3) return out;
    Fields::const_iterator it = f.find("target");
    if (it == f.end()) return out;
    const core::ObjectRef target = parseRef(it->second);
    if (!target.valid()) return out;
    out.slot = static_cast<AnchorSlot>(slot);
    out.target = target;
    if (ok) *ok = true;
    return out;
}

core::Error setArrowAnchor(core::Document& document, core::ObjectId arrowId,
                           const ArrowAnchor& anchor) {
    core::AnnotationRecord* record = document.arrows().mutableGet(arrowId);
    if (!record || !record->isAlive())
        return core::Error(core::ErrorCode::NotFound, "arrow id is not alive");
    if (!anchor.valid())
        return core::Error(core::ErrorCode::InvalidArgument, "anchor has no target");
    // Self-connection is refused: an arrow cannot be its own parent, and allowing
    // it would make the dependency walk cyclic.
    if (anchor.target.kind == core::ObjectKind::Arrow && anchor.target.id == arrowId)
        return core::Error(core::ErrorCode::InvalidArgument, "an arrow cannot anchor to itself");
    for (int i = 0; i < 4; ++i) record->anchors[i] = core::kInvalidObjectId;
    record->anchors[0] = anchor.target.id;
    document.noteTouchedAnnotation(core::ObjectKind::Arrow, arrowId);
    core::ExtensionBag& bag =
        document.extensions(core::ObjectRef(core::ObjectKind::Arrow, arrowId));
    bag.set(kAnchorKey, encodeAnchor(anchor));
    document.noteTouchedExtensions(core::ObjectRef(core::ObjectKind::Arrow, arrowId), false);
    return core::Error::success();
}

core::Error clearArrowAnchor(core::Document& document, core::ObjectId arrowId) {
    core::AnnotationRecord* record = document.arrows().mutableGet(arrowId);
    if (!record || !record->isAlive())
        return core::Error(core::ErrorCode::NotFound, "arrow id is not alive");
    bool had = false;
    for (int i = 0; i < 4; ++i) {
        if (record->anchors[i] != core::kInvalidObjectId) had = true;
        record->anchors[i] = core::kInvalidObjectId;
    }
    const core::ObjectRef ref(core::ObjectKind::Arrow, arrowId);
    const bool hadRecord = document.extensions(ref).find(kAnchorKey) != 0;
    if (had || hadRecord) {
        document.noteTouchedAnnotation(core::ObjectKind::Arrow, arrowId);
        document.extensions(ref).erase(kAnchorKey);
        document.noteTouchedExtensions(ref, false);
    }
    return core::Error::success();
}

ArrowAnchor arrowAnchor(const core::Document& document, core::ObjectId arrowId,
                        bool* ok) {
    ArrowAnchor out;
    if (ok) *ok = false;
    const core::AnnotationRecord* record = document.arrows().alive(arrowId);
    if (!record) return out;
    const core::ObjectRef ref(core::ObjectKind::Arrow, arrowId);
    if (const std::string* text = document.extensions(ref).findValue(kAnchorKey)) {
        bool parsed = false;
        ArrowAnchor a = decodeAnchor(*text, &parsed);
        if (parsed) { if (ok) *ok = true; return a; }
    }
    // A record whose anchors[] were set by something other than this module (a
    // CDXML import, a future T05 connect command) still reports a usable anchor.
    for (int i = 0; i < 4; ++i) {
        if (record->anchors[i] == core::kInvalidObjectId) continue;
        out.target = core::ObjectRef(core::ObjectKind::Unknown, record->anchors[i]);
        out.slot = AnchorSlot::End;
        return out;
    }
    return out;
}

const char* anchorSlotName(AnchorSlot slot) {
    switch (slot) {
    case AnchorSlot::None:     return "None";
    case AnchorSlot::Begin:    return "Begin";
    case AnchorSlot::End:      return "End";
    case AnchorSlot::Midpoint: return "Midpoint";
    }
    return "?";
}

} // namespace arrows
} // namespace r62

