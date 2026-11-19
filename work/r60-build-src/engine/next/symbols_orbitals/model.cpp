#include "next/symbols_orbitals/model.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <sstream>

namespace r62 {
namespace symbols {

const char* const kSymbolParamsKey  = "so.symbol.params.1";
const char* const kOrbitalParamsKey = "so.orbital.params.1";
const char* const kPushParamsKey    = "so.push.params.1";
const char* const kBindKey          = "so.bind.1";

namespace {

typedef std::map<std::string, std::string> Fields;

std::string fmtDouble(double v) {
    if (!std::isfinite(v)) return "0";
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.17g", v);
    return std::string(buf);
}

std::string fmtInt(long long v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%lld", v);
    return std::string(buf);
}

void put(Fields* f, const char* k, const std::string& v) { (*f)[k] = v; }
void putD(Fields* f, const char* k, double v) { (*f)[k] = fmtDouble(v); }
void putI(Fields* f, const char* k, long long v) { (*f)[k] = fmtInt(v); }
void putB(Fields* f, const char* k, bool v) { (*f)[k] = (v ? "1" : "0"); }

std::string join(const Fields& f) {
    std::ostringstream os;
    for (Fields::const_iterator it = f.begin(); it != f.end(); ++it)
        os << it->first << "=" << it->second << ";";
    return os.str();
}

bool parse(const std::string& text, Fields* out) {
    out->clear();
    size_t i = 0;
    while (i < text.size()) {
        const size_t eq = text.find('=', i);
        if (eq == std::string::npos) break;
        const size_t semi = text.find(';', eq + 1);
        if (semi == std::string::npos) break;
        const std::string key = text.substr(i, eq - i);
        const std::string val = text.substr(eq + 1, semi - eq - 1);
        if (key.empty()) return false;
        (*out)[key] = val;
        i = semi + 1;
    }
    return !out->empty();
}

bool getD(const Fields& f, const char* k, double* out) {
    Fields::const_iterator it = f.find(k);
    if (it == f.end()) return false;
    const char* s = it->second.c_str();
    char* end = 0;
    const double v = std::strtod(s, &end);
    if (end == s || *end != 0) return false;
    if (!std::isfinite(v)) return false;
    *out = v;
    return true;
}

bool getI(const Fields& f, const char* k, long long* out) {
    Fields::const_iterator it = f.find(k);
    if (it == f.end()) return false;
    const char* s = it->second.c_str();
    char* end = 0;
    const long long v = std::strtoll(s, &end, 10);
    if (end == s || *end != 0) return false;
    *out = v;
    return true;
}

bool getB(const Fields& f, const char* k, bool* out) {
    long long v = 0;
    if (!getI(f, k, &v)) return false;
    *out = (v != 0);
    return true;
}

std::string getS(const Fields& f, const char* k) {
    Fields::const_iterator it = f.find(k);
    return it == f.end() ? std::string() : it->second;
}

uint64_t fnv1a(const std::string& s) {
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < s.size(); ++i) {
        h ^= static_cast<unsigned char>(s[i]);
        h *= 1099511628211ull;
    }
    return h;
}

bool kindFromToken(const std::string& t, SymbolKind* out) {
    for (int i = 0; i <= 9; ++i) {
        const SymbolKind k = static_cast<SymbolKind>(i);
        if (t == symbolKindName(k)) { *out = k; return true; }
    }
    return false;
}
bool meaningFromToken(const std::string& t, SymbolMeaning* out) {
    for (int i = 0; i <= 8; ++i) {
        const SymbolMeaning m = static_cast<SymbolMeaning>(i);
        if (t == symbolMeaningName(m)) { *out = m; return true; }
    }
    return false;
}
bool anchorFromToken(const std::string& t, SymbolAnchor* out) {
    for (int i = 0; i <= 4; ++i) {
        const SymbolAnchor a = static_cast<SymbolAnchor>(i);
        if (t == symbolAnchorName(a)) { *out = a; return true; }
    }
    return false;
}
bool shapeFromToken(const std::string& t, OrbitalShape* out) {
    for (int i = 0; i <= 6; ++i) {
        const OrbitalShape s = static_cast<OrbitalShape>(i);
        if (t == orbitalShapeName(s)) { *out = s; return true; }
    }
    return false;
}
bool fillFromToken(const std::string& t, OrbitalFill* out) {
    for (int i = 0; i <= 2; ++i) {
        const OrbitalFill f = static_cast<OrbitalFill>(i);
        if (t == orbitalFillName(f)) { *out = f; return true; }
    }
    return false;
}
bool headFromToken(const std::string& t, PushHead* out) {
    for (int i = 0; i <= 1; ++i) {
        const PushHead h = static_cast<PushHead>(i);
        if (t == pushHeadName(h)) { *out = h; return true; }
    }
    return false;
}
bool endpointFromToken(const std::string& t, PushEndpoint* out) {
    for (int i = 0; i <= 3; ++i) {
        const PushEndpoint e = static_cast<PushEndpoint>(i);
        if (t == pushEndpointName(e)) { *out = e; return true; }
    }
    return false;
}

} // namespace
// ===========================================================================
// Encoding
// ===========================================================================
std::string encodeSymbolParams(const SymbolParams& p) {
    Fields f;
    put(&f, "preset", fmtInt(static_cast<long long>(p.presetId)));
    put(&f, "kind", symbolKindName(p.kind));
    put(&f, "sign", fmtInt(static_cast<long long>(p.sign)));
    put(&f, "meaning", symbolMeaningName(p.meaning));
    put(&f, "anchor", symbolAnchorName(p.anchor));
    putD(&f, "px", p.position.x);
    putD(&f, "py", p.position.y);
    putD(&f, "rot", p.rotation);
    putD(&f, "scale", p.scale);
    putD(&f, "size", p.size);
    putI(&f, "cd", p.chargeDelta);
    putI(&f, "rv", p.radicalValue);
    putB(&f, "rfe", p.rotateFromEnd);
    putB(&f, "rsz", p.resizable);
    putB(&f, "ring", p.drawCircle);
    return join(f);
}

SymbolParams decodeSymbolParams(const std::string& text, bool* ok) {
    if (ok) *ok = false;
    SymbolParams p;
    Fields f;
    if (!parse(text, &f)) return p;
    long long preset = 0, sign = 0, cd = 0, rv = 0;
    double px = 0, py = 0, rot = 0, scale = 1, size = 0;
    bool rfe = false, rsz = true, ring = false;
    SymbolKind kind = SymbolKind::None;
    SymbolMeaning meaning = SymbolMeaning::Decorative;
    SymbolAnchor anchor = SymbolAnchor::Free;
    // EVERY field must be present and well formed; a half-present record is
    // refused whole rather than producing a half-configured object.
    if (!getI(f, "preset", &preset)) return p;
    if (!kindFromToken(getS(f, "kind"), &kind)) return p;
    if (!getI(f, "sign", &sign)) return p;
    if (!meaningFromToken(getS(f, "meaning"), &meaning)) return p;
    if (!anchorFromToken(getS(f, "anchor"), &anchor)) return p;
    if (!getD(f, "px", &px) || !getD(f, "py", &py)) return p;
    if (!getD(f, "rot", &rot)) return p;
    if (!getD(f, "scale", &scale)) return p;
    if (!getD(f, "size", &size)) return p;
    if (!getI(f, "cd", &cd)) return p;
    if (!getI(f, "rv", &rv)) return p;
    if (!getB(f, "rfe", &rfe)) return p;
    if (!getB(f, "rsz", &rsz)) return p;
    if (!getB(f, "ring", &ring)) return p;
    if (sign < -1 || sign > 1) return p;
    if (preset < 0 || preset > 0x7FFFFFFFll) return p;
    p.presetId      = static_cast<uint32_t>(preset);
    p.kind          = kind;
    p.sign          = static_cast<GlyphSign>(sign);
    p.meaning       = meaning;
    p.anchor        = anchor;
    p.position      = core::Vec2(px, py);
    p.rotation      = rot;
    p.scale         = scale;
    p.size          = size;
    p.chargeDelta   = static_cast<int>(cd);
    p.radicalValue  = static_cast<int>(rv);
    p.rotateFromEnd = rfe;
    p.resizable     = rsz;
    p.drawCircle    = ring;
    if (ok) *ok = true;
    return p;
}

std::string encodeOrbitalParams(const OrbitalParams& p) {
    Fields f;
    put(&f, "preset", fmtInt(static_cast<long long>(p.presetId)));
    put(&f, "shape", orbitalShapeName(p.shape));
    put(&f, "fill", orbitalFillName(p.fill));
    putB(&f, "inv", p.invertPhase);
    put(&f, "anchor", symbolAnchorName(p.anchor));
    putD(&f, "px", p.position.x);
    putD(&f, "py", p.position.y);
    putD(&f, "rot", p.rotation);
    putD(&f, "len", p.length);
    putD(&f, "wid", p.width);
    putD(&f, "scale", p.scale);
    putI(&f, "lobes", p.lobeCount);
    // Per-lobe phase bits, packed as a small integer each so the record stays
    // readable: bit0 = filled, bit1 = positive phase.
    std::ostringstream ph;
    for (int i = 0; i < 6; ++i) {
        int v = 0;
        if (p.lobeFilled[i]) v |= 1;
        if (p.lobePositive[i]) v |= 2;
        ph << v;
    }
    put(&f, "phase", ph.str());
    return join(f);
}

OrbitalParams decodeOrbitalParams(const std::string& text, bool* ok) {
    if (ok) *ok = false;
    OrbitalParams p;
    Fields f;
    if (!parse(text, &f)) return p;
    long long preset = 0, lobes = 0;
    double px = 0, py = 0, rot = 0, len = 0, wid = 0, scale = 1;
    bool inv = false;
    OrbitalShape shape = OrbitalShape::S;
    OrbitalFill fill = OrbitalFill::Open;
    SymbolAnchor anchor = SymbolAnchor::Centered;
    if (!getI(f, "preset", &preset)) return p;
    if (!shapeFromToken(getS(f, "shape"), &shape)) return p;
    if (!fillFromToken(getS(f, "fill"), &fill)) return p;
    if (!getB(f, "inv", &inv)) return p;
    if (!anchorFromToken(getS(f, "anchor"), &anchor)) return p;
    if (!getD(f, "px", &px) || !getD(f, "py", &py)) return p;
    if (!getD(f, "rot", &rot)) return p;
    if (!getD(f, "len", &len)) return p;
    if (!getD(f, "wid", &wid)) return p;
    if (!getD(f, "scale", &scale)) return p;
    if (!getI(f, "lobes", &lobes)) return p;
    const std::string ph = getS(f, "phase");
    if (ph.size() != 6) return p;
    if (preset < 0 || preset > 0x7FFFFFFFll) return p;
    if (lobes < 0 || lobes > 6) return p;
    p.presetId    = static_cast<uint32_t>(preset);
    p.shape       = shape;
    p.fill        = fill;
    p.invertPhase = inv;
    p.anchor      = anchor;
    p.position    = core::Vec2(px, py);
    p.rotation    = rot;
    p.length      = len;
    p.width       = wid;
    p.scale       = scale;
    p.lobeCount   = static_cast<int>(lobes);
    for (int i = 0; i < 6; ++i) {
        const char c = ph[static_cast<size_t>(i)];
        if (c < '0' || c > '3') return p;
        const int v = c - '0';
        p.lobeFilled[i]   = (v & 1) != 0;
        p.lobePositive[i] = (v & 2) != 0;
    }
    if (ok) *ok = true;
    return p;
}

std::string encodePushParams(const PushParams& p) {
    Fields f;
    put(&f, "preset", fmtInt(static_cast<long long>(p.presetId)));
    put(&f, "head", pushHeadName(p.head));
    putD(&f, "bx", p.begin.x);
    putD(&f, "by", p.begin.y);
    putD(&f, "ex", p.end.x);
    putD(&f, "ey", p.end.y);
    putD(&f, "sag", p.sagitta);
    putD(&f, "scale", p.scale);
    put(&f, "sk", pushEndpointName(p.sourceKind));
    put(&f, "tk", pushEndpointName(p.targetKind));
    putI(&f, "sid", static_cast<long long>(p.sourceObject.id));
    putI(&f, "st", static_cast<long long>(p.sourceObject.kind));
    putI(&f, "tid", static_cast<long long>(p.targetObject.id));
    putI(&f, "tt", static_cast<long long>(p.targetObject.kind));
    return join(f);
}

PushParams decodePushParams(const std::string& text, bool* ok) {
    if (ok) *ok = false;
    PushParams p;
    Fields f;
    if (!parse(text, &f)) return p;
    long long preset = 0, sid = 0, st = 0, tid = 0, tt = 0;
    double bx = 0, by = 0, ex = 0, ey = 0, sag = 0, scale = 1;
    PushHead head = PushHead::FullPair;
    PushEndpoint sk = PushEndpoint::None, tk = PushEndpoint::None;
    if (!getI(f, "preset", &preset)) return p;
    if (!headFromToken(getS(f, "head"), &head)) return p;
    if (!getD(f, "bx", &bx) || !getD(f, "by", &by)) return p;
    if (!getD(f, "ex", &ex) || !getD(f, "ey", &ey)) return p;
    if (!getD(f, "sag", &sag)) return p;
    if (!getD(f, "scale", &scale)) return p;
    if (!endpointFromToken(getS(f, "sk"), &sk)) return p;
    if (!endpointFromToken(getS(f, "tk"), &tk)) return p;
    if (!getI(f, "sid", &sid)) return p;
    if (!getI(f, "st", &st)) return p;
    if (!getI(f, "tid", &tid)) return p;
    if (!getI(f, "tt", &tt)) return p;
    if (preset < 0 || preset > 0x7FFFFFFFll) return p;
    if (sid < 0 || sid > 0xFFFFFFFFll) return p;
    if (tid < 0 || tid > 0xFFFFFFFFll) return p;
    p.presetId     = static_cast<uint32_t>(preset);
    p.head         = head;
    p.begin        = core::Vec2(bx, by);
    p.end          = core::Vec2(ex, ey);
    p.sagitta      = sag;
    p.scale        = scale;
    p.sourceKind   = sk;
    p.targetKind   = tk;
    p.sourceObject = core::ObjectRef(static_cast<core::ObjectKind>(st),
                                     static_cast<core::ObjectId>(sid));
    p.targetObject = core::ObjectRef(static_cast<core::ObjectKind>(tt),
                                     static_cast<core::ObjectId>(tid));
    if (ok) *ok = true;
    return p;
}

std::string encodeBinding(const SymbolBinding& b) {
    Fields f;
    putI(&f, "atom", static_cast<long long>(b.atom));
    putI(&f, "ac", b.appliedCharge);
    putI(&f, "ar", b.appliedRadical);
    putI(&f, "pc", b.priorCharge);
    putI(&f, "pr", b.priorRadical);
    putI(&f, "pn", b.priorNodeType);
    return join(f);
}

SymbolBinding decodeBinding(const std::string& text, bool* ok) {
    if (ok) *ok = false;
    SymbolBinding b;
    Fields f;
    if (!parse(text, &f)) return b;
    long long atom = 0, ac = 0, ar = 0, pc = 0, pr = 0, pn = 0;
    if (!getI(f, "atom", &atom)) return b;
    if (!getI(f, "ac", &ac)) return b;
    if (!getI(f, "ar", &ar)) return b;
    if (!getI(f, "pc", &pc)) return b;
    if (!getI(f, "pr", &pr)) return b;
    if (!getI(f, "pn", &pn)) return b;
    if (atom < 0 || atom > 0xFFFFFFFFll) return b;
    if (ac < -128 || ac > 127) return b;
    if (ar < -1 || ar > 3) return b;
    if (pc < -128 || pc > 127) return b;
    if (pr < 0 || pr > 255) return b;
    if (pn < -1 || pn > 255) return b;
    b.atom          = static_cast<core::ObjectId>(atom);
    b.appliedCharge = static_cast<int8_t>(ac);
    b.appliedRadical= static_cast<int8_t>(ar);
    b.priorCharge   = static_cast<int8_t>(pc);
    b.priorRadical  = static_cast<int8_t>(pr);
    b.priorNodeType = static_cast<int8_t>(pn);
    if (ok) *ok = true;
    return b;
}

uint64_t symbolParamsSignature(const SymbolParams& p) {
    return fnv1a(encodeSymbolParams(p));
}
uint64_t orbitalParamsSignature(const OrbitalParams& p) {
    return fnv1a(encodeOrbitalParams(p));
}
// ===========================================================================
// AnnotationRecord adapters
// ===========================================================================
// The record holds what any T01 consumer understands (position, preset, scale,
// rotation, style). The bag holds the rest. Geometry slot conventions:
//   geometry[0] = the object anchor / begin
//   geometry[2] = the second endpoint for a push (unused for symbol/orbital)
core::Error toAnnotationRecord(const SymbolParams& p, double documentLineWidth,
                               core::AnnotationRecord* out) {
    if (!out) return core::Error(core::ErrorCode::InvalidArgument, "null out");
    const core::Error v = validateSymbolParams(p);
    if (!v.ok()) return v;
    core::AnnotationRecord r;
    r.kind      = core::ObjectKind::Symbol;
    r.presetId  = p.presetId;
    r.geometry[0] = p.position;
    r.geometry[1] = p.position;
    r.geometry[2] = p.position;
    r.geometry[3] = p.position;
    r.scale     = p.scale;
    r.rotation  = p.rotation;
    r.style.width = documentLineWidth;
    *out = r;
    return core::Error::success();
}

core::Error toAnnotationRecord(const OrbitalParams& p, double documentLineWidth,
                               core::AnnotationRecord* out) {
    if (!out) return core::Error(core::ErrorCode::InvalidArgument, "null out");
    const core::Error v = validateOrbitalParams(p);
    if (!v.ok()) return v;
    core::AnnotationRecord r;
    r.kind      = core::ObjectKind::Orbital;
    r.presetId  = p.presetId;
    // The orbital occupies its length along +x in the local frame; the record
    // keeps the anchor and the far extent so a consumer that knows nothing about
    // T12 can still draw a plausible box (the guide says the Info window shows
    // length and angle [GUIDE p241], so those two ARE the canonical extents).
    const core::Vec2 far = localToDocument(core::Vec2(p.length * 0.5, 0), p.position,
                                          p.rotation, p.scale);
    const core::Vec2 wide = localToDocument(core::Vec2(0, p.width * 0.5), p.position,
                                            p.rotation, p.scale);
    r.geometry[0] = p.position;
    r.geometry[1] = wide;
    r.geometry[2] = far;
    r.geometry[3] = p.position;
    r.scale     = p.scale;
    r.rotation  = p.rotation;
    r.style.width = documentLineWidth;
    r.fillRGBA  = (p.fill == OrbitalFill::Open) ? 0u : 0x809EC5E8u;
    *out = r;
    return core::Error::success();
}

core::Error toAnnotationRecord(const PushParams& p, double documentLineWidth,
                               core::AnnotationRecord* out) {
    if (!out) return core::Error(core::ErrorCode::InvalidArgument, "null out");
    const core::Error v = validatePushParams(p);
    if (!v.ok()) return v;
    core::AnnotationRecord r;
    r.kind      = core::ObjectKind::Arrow;
    r.presetId  = p.presetId;
    r.geometry[0] = p.begin;
    r.geometry[2] = p.end;
    r.geometry[1] = p.begin;
    r.geometry[3] = p.end;
    r.scale     = p.scale;
    r.rotation  = 0.0;
    r.style.width = documentLineWidth;
    *out = r;
    return core::Error::success();
}

SymbolParams symbolFromAnnotationRecord(const core::AnnotationRecord& r,
                                        const core::ExtensionBag& bag, bool* ok) {
    if (ok) *ok = false;
    SymbolParams p;
    const std::string* text = bag.findValue(kSymbolParamsKey);
    if (text) {
        bool decoded = false;
        SymbolParams fromBag = decodeSymbolParams(*text, &decoded);
        if (decoded) {
            if (ok) *ok = true;
            return fromBag;
        }
    }
    // Degraded fallback: geometry from the frozen record, defaults elsewhere. The
    // object stays VISIBLE rather than vanishing because its bag entry is missing.
    p.position = r.geometry[0];
    p.scale    = (r.scale > 0.0) ? r.scale : 1.0;
    p.rotation = r.rotation;
    p.presetId = r.presetId;
    return p;
}

OrbitalParams orbitalFromAnnotationRecord(const core::AnnotationRecord& r,
                                          const core::ExtensionBag& bag, bool* ok) {
    if (ok) *ok = false;
    OrbitalParams p;
    const std::string* text = bag.findValue(kOrbitalParamsKey);
    if (text) {
        bool decoded = false;
        OrbitalParams fromBag = decodeOrbitalParams(*text, &decoded);
        if (decoded) {
            if (ok) *ok = true;
            return fromBag;
        }
    }
    p.position = r.geometry[0];
    p.scale    = (r.scale > 0.0) ? r.scale : 1.0;
    p.rotation = r.rotation;
    p.presetId = r.presetId;
    return p;
}

PushParams pushFromAnnotationRecord(const core::AnnotationRecord& r,
                                    const core::ExtensionBag& bag, bool* ok) {
    if (ok) *ok = false;
    PushParams p;
    const std::string* text = bag.findValue(kPushParamsKey);
    if (text) {
        bool decoded = false;
        PushParams fromBag = decodePushParams(*text, &decoded);
        if (decoded) {
            if (ok) *ok = true;
            return fromBag;
        }
    }
    p.begin   = r.geometry[0];
    p.end     = r.geometry[2];
    p.presetId = r.presetId;
    return p;
}

// ===========================================================================
// Document adapters
// ===========================================================================
core::Error addSymbol(core::Document& d, const SymbolParams& p,
                      double documentLineWidth, core::ObjectId* outId) {
    core::AnnotationRecord r;
    const core::Error e = toAnnotationRecord(p, documentLineWidth, &r);
    if (!e.ok()) return e;
    core::ObjectId id = core::kInvalidObjectId;
    const core::Error a = d.addAnnotation(core::ObjectKind::Symbol, r, &id);
    if (!a.ok()) return a;
    d.extensions(core::ObjectRef(core::ObjectKind::Symbol, id))
     .set(kSymbolParamsKey, encodeSymbolParams(p));
    // The extension bag is document state and part of RawState/digest, so it must
    // be recorded as touched. ObjectSlabBookkeeping keeps ObjectId stable.
    d.noteTouchedExtensions(core::ObjectRef(core::ObjectKind::Symbol, id), false);
    if (outId) *outId = id;
    return core::Error::success();
}

core::Error writeSymbol(core::Document& d, core::ObjectId id, const SymbolParams& p,
                        double documentLineWidth) {
    core::AnnotationRecord* rec = d.symbols().mutableGet(id);
    if (!rec || !rec->isAlive()) return core::Error(core::ErrorCode::NotFound, "no such symbol");
    core::AnnotationRecord r;
    const core::Error e = toAnnotationRecord(p, documentLineWidth, &r);
    if (!e.ok()) return e;
    // Preserve anchors: they are owned by the arrow/curve connection feature, not
    // by a style or position edit.
    for (int i = 0; i < 4; ++i) r.anchors[i] = rec->anchors[i];
    const core::ObjectId keepBind = id;
    (void)keepBind;
    *rec = r;
    rec->id = id;
    d.extensions(core::ObjectRef(core::ObjectKind::Symbol, id))
     .set(kSymbolParamsKey, encodeSymbolParams(p));
    d.noteTouchedExtensions(core::ObjectRef(core::ObjectKind::Symbol, id), false);
    d.noteTouchedAnnotation(core::ObjectKind::Symbol, id);
    return core::Error::success();
}

core::Error readSymbol(const core::Document& d, core::ObjectId id,
                       SymbolParams* out, bool* ok) {
    if (!out) return core::Error(core::ErrorCode::InvalidArgument, "null out");
    const core::AnnotationRecord* rec = d.symbols().alive(id);
    if (!rec) return core::Error(core::ErrorCode::NotFound, "no such symbol");
    *out = symbolFromAnnotationRecord(*rec,
               d.extensions(core::ObjectRef(core::ObjectKind::Symbol, id)), ok);
    return core::Error::success();
}

core::Error addOrbital(core::Document& d, const OrbitalParams& p,
                       double documentLineWidth, core::ObjectId* outId) {
    core::AnnotationRecord r;
    const core::Error e = toAnnotationRecord(p, documentLineWidth, &r);
    if (!e.ok()) return e;
    core::ObjectId id = core::kInvalidObjectId;
    const core::Error a = d.addAnnotation(core::ObjectKind::Orbital, r, &id);
    if (!a.ok()) return a;
    d.extensions(core::ObjectRef(core::ObjectKind::Orbital, id))
     .set(kOrbitalParamsKey, encodeOrbitalParams(p));
    d.noteTouchedExtensions(core::ObjectRef(core::ObjectKind::Orbital, id), false);
    if (outId) *outId = id;
    return core::Error::success();
}

core::Error writeOrbital(core::Document& d, core::ObjectId id, const OrbitalParams& p,
                         double documentLineWidth) {
    core::AnnotationRecord* rec = d.orbitals().mutableGet(id);
    if (!rec || !rec->isAlive()) return core::Error(core::ErrorCode::NotFound, "no such orbital");
    core::AnnotationRecord r;
    const core::Error e = toAnnotationRecord(p, documentLineWidth, &r);
    if (!e.ok()) return e;
    for (int i = 0; i < 4; ++i) r.anchors[i] = rec->anchors[i];
    *rec = r;
    rec->id = id;
    d.extensions(core::ObjectRef(core::ObjectKind::Orbital, id))
     .set(kOrbitalParamsKey, encodeOrbitalParams(p));
    d.noteTouchedExtensions(core::ObjectRef(core::ObjectKind::Orbital, id), false);
    d.noteTouchedAnnotation(core::ObjectKind::Orbital, id);
    return core::Error::success();
}

core::Error readOrbital(const core::Document& d, core::ObjectId id,
                        OrbitalParams* out, bool* ok) {
    if (!out) return core::Error(core::ErrorCode::InvalidArgument, "null out");
    const core::AnnotationRecord* rec = d.orbitals().alive(id);
    if (!rec) return core::Error(core::ErrorCode::NotFound, "no such orbital");
    *out = orbitalFromAnnotationRecord(*rec,
               d.extensions(core::ObjectRef(core::ObjectKind::Orbital, id)), ok);
    return core::Error::success();
}

core::Error addPush(core::Document& d, const PushParams& p,
                    double documentLineWidth, core::ObjectId* outId) {
    core::AnnotationRecord r;
    const core::Error e = toAnnotationRecord(p, documentLineWidth, &r);
    if (!e.ok()) return e;
    core::ObjectId id = core::kInvalidObjectId;
    const core::Error a = d.addAnnotation(core::ObjectKind::Arrow, r, &id);
    if (!a.ok()) return a;
    d.extensions(core::ObjectRef(core::ObjectKind::Arrow, id))
     .set(kPushParamsKey, encodePushParams(p));
    d.noteTouchedExtensions(core::ObjectRef(core::ObjectKind::Arrow, id), false);
    if (outId) *outId = id;
    return core::Error::success();
}

core::Error writePush(core::Document& d, core::ObjectId id, const PushParams& p,
                      double documentLineWidth) {
    core::AnnotationRecord* rec = d.arrows().mutableGet(id);
    if (!rec || !rec->isAlive()) return core::Error(core::ErrorCode::NotFound, "no such push");
    core::AnnotationRecord r;
    const core::Error e = toAnnotationRecord(p, documentLineWidth, &r);
    if (!e.ok()) return e;
    for (int i = 0; i < 4; ++i) r.anchors[i] = rec->anchors[i];
    *rec = r;
    rec->id = id;
    d.extensions(core::ObjectRef(core::ObjectKind::Arrow, id))
     .set(kPushParamsKey, encodePushParams(p));
    d.noteTouchedExtensions(core::ObjectRef(core::ObjectKind::Arrow, id), false);
    d.noteTouchedAnnotation(core::ObjectKind::Arrow, id);
    return core::Error::success();
}

core::Error readPush(const core::Document& d, core::ObjectId id,
                     PushParams* out, bool* ok) {
    if (!out) return core::Error(core::ErrorCode::InvalidArgument, "null out");
    const core::AnnotationRecord* rec = d.arrows().alive(id);
    if (!rec) return core::Error(core::ErrorCode::NotFound, "no such push");
    *out = pushFromAnnotationRecord(*rec,
               d.extensions(core::ObjectRef(core::ObjectKind::Arrow, id)), ok);
    return core::Error::success();
}
// ===========================================================================
// Binding: the graphic/chemistry seam
// ===========================================================================
bool symbolCarriesChemistry(const SymbolParams& p) {
    return p.chargeDelta != 0 || p.radicalValue != 0 ||
           p.meaning == SymbolMeaning::AttachmentPoint ||
           p.meaning == SymbolMeaning::PolymerBead;
}

namespace {

// An "unlabeled carbon" for the [GUIDE p242] diradical reading: element 6 with no
// label override and no nickname/query node type.
bool isUnlabeledCarbon(const core::Document& d, const core::AtomRecord& a) {
    if (a.element != 6) return false;
    if (a.semantics.nodeType != static_cast<uint8_t>(core::NodeType::Element)) return false;
    const std::string& label = d.atomLabel(a.id);
    return label.empty();
}

} // namespace

ChemistryDelta chemistryForBinding(const SymbolParams& symbol,
                                   const core::AtomRecord& atom,
                                   const BindOptions& options) {
    ChemistryDelta out;
    if (!options.applyChemistry) return out;

    switch (symbol.meaning) {
    case SymbolMeaning::Charge:
    case SymbolMeaning::ChargeMinus:
    case SymbolMeaning::ChargePlusMinus: {
        if (symbol.chargeDelta != 0) {
            out.changeCharge = true;
            out.charge = atom.semantics.charge + symbol.chargeDelta;
            if (out.charge < -15) out.charge = -15;
            if (out.charge > 15) out.charge = 15;
        }
        if (symbol.radicalValue != 0) {
            out.changeRadical = true;
            out.radical = symbol.radicalValue;
        }
        break;
    }
    case SymbolMeaning::RadicalMono:
    case SymbolMeaning::RadicalDouble: {
        if (symbol.radicalValue != 0) {
            out.changeRadical = true;
            out.radical = symbol.radicalValue;
        }
        break;
    }
    case SymbolMeaning::LonePair: {
        // [GUIDE p242] the diradical reading is OPT-IN and requires an unlabeled
        // carbon. Without the flag, binding a lone pair is a pure graphic link:
        // this is the rule that satisfies "moving a symbol near an atom must not
        // change the charge without evidence".
        if (options.lonePairOnUnlabeledCarbonIsDiradical) {
            out.changeRadical = true;
            out.radical = 3;   // core::Radical::Triplet == two unpaired electrons
        }
        break;
    }
    case SymbolMeaning::AttachmentPoint: {
        out.changeNodeType = true;
        out.nodeType = static_cast<int>(core::NodeType::Attachment);
        break;
    }
    case SymbolMeaning::PolymerBead: {
        out.changeNodeType = true;
        out.nodeType = static_cast<int>(core::NodeType::ExternalConnection);
        break;
    }
    case SymbolMeaning::Decorative:
        break;
    }

    // NOTE the precondition for the lone-pair diradical reading is NOT evaluated
    // here: it needs the whole Document (to see whether the atom has a label
    // override), so it is applied once inside bindSymbolToAtom(), at the single
    // place where chemistry is actually written. Keeping the rule in ONE place is
    // why a proximity move can never sneak a radical onto an atom.
    return out;
}

core::Error bindSymbolToAtom(core::Document& d, core::ObjectId symbolId,
                             core::ObjectId atomId, const BindOptions& options,
                             SymbolBinding* outBinding) {
    const core::AnnotationRecord* srec = d.symbols().alive(symbolId);
    if (!srec) return core::Error(core::ErrorCode::NotFound, "no such symbol");
    const core::AtomRecord* arec = d.atoms().alive(atomId);
    if (!arec) return core::Error(core::ErrorCode::NotFound, "no such atom");

    bool decoded = false;
    SymbolParams sp = symbolFromAnnotationRecord(*srec,
                          d.extensions(core::ObjectRef(core::ObjectKind::Symbol, symbolId)),
                          &decoded);
    if (!decoded) return core::Error(core::ErrorCode::InvalidSemantics,
                                     "symbol has no decodable parameter record");

    ChemistryDelta delta = chemistryForBinding(sp, *arec, options);

    // The diradical precondition, applied once, here.
    if (options.lonePairOnUnlabeledCarbonIsDiradical &&
        sp.meaning == SymbolMeaning::LonePair) {
        if (!isUnlabeledCarbon(d, *arec)) {
            delta.changeRadical = false;
        }
    }

    SymbolBinding b;
    b.atom        = atomId;
    b.priorCharge = arec->semantics.charge;
    b.priorRadical= arec->semantics.radical;
    b.priorNodeType = static_cast<int8_t>(arec->semantics.nodeType);

    if (delta.changeCharge) {
        core::AtomRecord copy = *arec;
        copy.semantics.charge = static_cast<int8_t>(delta.charge);
        core::Error e = d.writeAtom(copy);
        if (!e.ok()) return e;
        d.noteTouchedAtom(atomId);
        b.appliedCharge = static_cast<int8_t>(delta.charge);
    }
    if (delta.changeRadical) {
        const core::AtomRecord* cur = d.atoms().alive(atomId);
        core::AtomRecord copy = cur ? *cur : *arec;
        copy.semantics.radical = static_cast<uint8_t>(delta.radical);
        core::Error e = d.writeAtom(copy);
        if (!e.ok()) return e;
        d.noteTouchedAtom(atomId);
        b.appliedRadical = static_cast<int8_t>(delta.radical);
    }
    if (delta.changeNodeType) {
        const core::AtomRecord* cur = d.atoms().alive(atomId);
        core::AtomRecord copy = cur ? *cur : *arec;
        copy.semantics.nodeType = static_cast<uint8_t>(delta.nodeType);
        core::Error e = d.writeAtom(copy);
        if (!e.ok()) return e;
        d.noteTouchedAtom(atomId);
    }

    d.extensions(core::ObjectRef(core::ObjectKind::Symbol, symbolId))
     .set(kBindKey, encodeBinding(b));
    d.noteTouchedExtensions(core::ObjectRef(core::ObjectKind::Symbol, symbolId), false);
    if (outBinding) *outBinding = b;
    return core::Error::success();
}

core::Error unbindSymbol(core::Document& d, core::ObjectId symbolId) {
    const core::AnnotationRecord* srec = d.symbols().alive(symbolId);
    if (!srec) return core::Error(core::ErrorCode::NotFound, "no such symbol");
    const core::ExtensionBag& bag =
        d.extensions(core::ObjectRef(core::ObjectKind::Symbol, symbolId));
    const std::string* text = bag.findValue(kBindKey);
    if (!text) return core::Error(core::ErrorCode::NotFound, "symbol is not bound");
    bool ok = false;
    SymbolBinding b = decodeBinding(*text, &ok);
    if (!ok) return core::Error(core::ErrorCode::ParseError, "malformed binding record");

    // Restore EXACTLY the state captured at bind time, but only for the fields
    // this symbol actually wrote. A field the symbol never touched is left alone,
    // so unbinding cannot undo somebody else edit.
    const core::AtomRecord* arec = d.atoms().alive(b.atom);
    if (arec) {
        core::AtomRecord copy = *arec;
        bool touched = false;
        if (b.appliedCharge != -128) {
            copy.semantics.charge = b.priorCharge;
            touched = true;
        }
        if (b.appliedRadical != -1) {
            copy.semantics.radical = static_cast<uint8_t>(b.priorRadical);
            touched = true;
        }
        if (b.priorNodeType >= 0) {
            copy.semantics.nodeType = static_cast<uint8_t>(b.priorNodeType);
            touched = true;
        }
        if (touched) {
            core::Error e = d.writeAtom(copy);
            if (!e.ok()) return e;
            d.noteTouchedAtom(b.atom);
        }
    }
    // A missing atom is not an error: the bond partner may have been deleted
    // while the symbol stayed. The binding entry is still removed so the symbol
    // becomes a pure graphic again and no stale atom reference remains.
    d.extensions(core::ObjectRef(core::ObjectKind::Symbol, symbolId)).erase(kBindKey);
    d.noteTouchedExtensions(core::ObjectRef(core::ObjectKind::Symbol, symbolId), false);
    return core::Error::success();
}

SymbolBinding symbolBinding(const core::Document& d, core::ObjectId symbolId, bool* ok) {
    if (ok) *ok = false;
    SymbolBinding b;
    const core::AnnotationRecord* srec = d.symbols().alive(symbolId);
    if (!srec) return b;
    const std::string* text =
        d.extensions(core::ObjectRef(core::ObjectKind::Symbol, symbolId)).findValue(kBindKey);
    if (!text) {
        // Absent entry == legitimately unbound, which is not an error.
        if (ok) *ok = true;
        return b;
    }
    SymbolBinding parsed = decodeBinding(*text, ok);
    return parsed;
}

int symbolAttachmentRank(const core::Document& d, core::ObjectId symbolId, bool* ok) {
    if (ok) *ok = false;
    const core::AnnotationRecord* target = d.symbols().alive(symbolId);
    if (!target) return 0;
    // Rank counts the ATTACHMENT symbols in slot order. Because ObjectId is a
    // stable creation-ordered slot, "sequential numbers as you add diamonds"
    // [GUIDE p243] falls out of slot order with no stored counter.
    int rank = 0;
    const core::ObjectSlab<core::AnnotationRecord>& slab = d.symbols();
    for (core::ObjectId id = 0; id < slab.size(); ++id) {
        const core::AnnotationRecord* r = slab.alive(id);
        if (!r) continue;
        bool dec = false;
        SymbolParams sp = symbolFromAnnotationRecord(*r,
                              d.extensions(core::ObjectRef(core::ObjectKind::Symbol, id)),
                              &dec);
        if (!dec) continue;
        if (sp.meaning != SymbolMeaning::AttachmentPoint) continue;
        ++rank;
        if (id == symbolId) {
            if (ok) *ok = true;
            return rank;
        }
    }
    return 0;
}

} // namespace symbols
} // namespace r62
