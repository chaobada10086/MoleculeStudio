// =============================================================================
// re_round62_viewer/next/biodraw/params.cpp
//
// T14 - parameter codecs, frame helpers and sub-element identity.
//
// The text codec is deliberately total: encode() always produces a parseable
// record for any in-range value, and decode() never throws. A malformed record
// sets *ok = false and leaves the output at its default, so a corrupt document
// cannot produce a half-configured figure that looks fine but draws nothing.
// =============================================================================

#include "next/biodraw/params.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace r62 {
namespace biodraw {

const char* const kBioParamsKey     = "bio.params.1";
const char* const kBioComponentsKey = "bio.components.1";
const char* const kBioPlasmidKey    = "bio.plasmid.1";
const char* const kBioColorsKey     = "bio.colors.1";

namespace {

// ---------------------------------------------------------------------------
// Small, strict field helpers.
// ---------------------------------------------------------------------------
// 'garbage in a field' must be DETECTABLE, not silently zero. These return false
// on any trailing character, on an empty field, and on a non-finite value, and
// the caller turns false into a decode failure.
bool parseDouble(const std::string& text, double* out) {
    if (text.empty()) return false;
    char* end = 0;
    const double v = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || (end && *end != '\0')) return false;
    if (!std::isfinite(v)) return false;
    *out = v;
    return true;
}

bool parseInt(const std::string& text, int* out) {
    if (text.empty()) return false;
    char* end = 0;
    const long v = std::strtol(text.c_str(), &end, 10);
    if (end == text.c_str() || (end && *end != '\0')) return false;
    *out = static_cast<int>(v);
    return true;
}

bool parseU32(const std::string& text, uint32_t* out) {
    if (text.empty()) return false;
    char* end = 0;
    const unsigned long v = std::strtoul(text.c_str(), &end, 10);
    if (end == text.c_str() || (end && *end != '\0')) return false;
    *out = static_cast<uint32_t>(v);
    return true;
}

std::string fmtDouble(double v) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.6g", v);
    return std::string(buffer);
}

// Split 'k=v;k=v'. An entry without '=' is skipped AND reported, because a
// truncated record is exactly how a half-written document looks.
bool splitPairs(const std::string& text, std::vector<std::pair<std::string, std::string> >* out) {
    out->clear();
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find(';', start);
        if (end == std::string::npos) end = text.size();
        const std::string entry = text.substr(start, end - start);
        if (!entry.empty()) {
            const size_t eq = entry.find('=');
            if (eq == std::string::npos) return false;
            out->push_back(std::make_pair(entry.substr(0, eq), entry.substr(eq + 1)));
        }
        if (end == text.size()) break;
        start = end + 1;
    }
    return true;
}

const std::string* findPair(const std::vector<std::pair<std::string, std::string> >& pairs,
                           const char* key) {
    for (size_t i = 0; i < pairs.size(); ++i)
        if (pairs[i].first == key) return &pairs[i].second;
    return 0;
}

// The component / plasmid / colour records use 'v=1|entry|entry' -- ';' separates
// the fields of the header, '|' separates the repeated entries. Those are two
// different jobs and need two different splitters: routing the pipe form through
// splitPairs() silently produced zero components, which is exactly the kind of
// 'it saved but came back empty' defect the round-trip test is meant to catch.
//
// Returns false only for a malformed 'key=value' entry (no '='), never for a
// missing entry: an absent entry is simply not in the list.
bool splitPipes(const std::string& text, std::vector<std::pair<std::string, std::string> >* out) {
    out->clear();
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find('|', start);
        if (end == std::string::npos) end = text.size();
        const std::string entry = text.substr(start, end - start);
        if (!entry.empty()) {
            const size_t eq = entry.find('=');
            if (eq == std::string::npos) return false;
            out->push_back(std::make_pair(entry.substr(0, eq), entry.substr(eq + 1)));
        }
        if (end == text.size()) break;
        start = end + 1;
    }
    return true;
}

// Split a comma-separated payload into its fields. Used for one entry's body.
std::vector<std::string> splitCommas(const std::string& payload) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (start <= payload.size()) {
        size_t end = payload.find(',', start);
        if (end == std::string::npos) end = payload.size();
        parts.push_back(payload.substr(start, end - start));
        if (end == payload.size()) break;
        start = end + 1;
    }
    return parts;
}

} // namespace
// ---------------------------------------------------------------------------
// Scalar record: frame, style, glyph block
// ---------------------------------------------------------------------------
std::string encodeBioParams(const BioShapeParams& p) {
    std::ostringstream out;
    out << "v=1"
        << ";preset=" << p.presetId
        << ";family=" << static_cast<int>(p.family)
        << ";glyph=" << static_cast<int>(p.glyph)
        << ";variant=" << p.variant
        << ";cx=" << fmtDouble(p.centre.x) << ";cy=" << fmtDouble(p.centre.y)
        << ";ux=" << fmtDouble(p.axisU.x) << ";uy=" << fmtDouble(p.axisU.y)
        << ";vx=" << fmtDouble(p.axisV.x) << ";vy=" << fmtDouble(p.axisV.y)
        << ";rot=" << fmtDouble(p.rotationDeg)
        << ";scale=" << fmtDouble(p.scale)
        << ";fill=" << p.fillRGBA
        << ";stroke=" << p.style.colorRGBA
        << ";width=" << fmtDouble(p.style.width)
        << ";dash=" << static_cast<int>(p.style.dash);
    static const char* const kNames[] = {
        "lobes", "lobeDepth", "cornerFraction", "nodes", "innerRatio",
        "sweepDeg", "turns", "amplitude", "barrelAngleDeg", "leafletGap",
        "headRadius", "tailLength", "subunitRadius", "tmSegments", "loops",
        "ligandPresent", "disulfides", "chainLength", "basePairs",
        "helixRadius", "risePerTurn", "spikes", "capsidFaces", "beadCount",
        "nucleusOffset"
    };
    const double values[] = {
        p.glyphParams.lobes, p.glyphParams.lobeDepth, p.glyphParams.cornerFraction,
        p.glyphParams.nodes, p.glyphParams.innerRatio, p.glyphParams.sweepDeg,
        p.glyphParams.turns, p.glyphParams.amplitude, p.glyphParams.barrelAngleDeg,
        p.glyphParams.leafletGap, p.glyphParams.headRadius, p.glyphParams.tailLength,
        p.glyphParams.subunitRadius, p.glyphParams.tmSegments, p.glyphParams.loops,
        p.glyphParams.ligandPresent, p.glyphParams.disulfides, p.glyphParams.chainLength,
        p.glyphParams.basePairs, p.glyphParams.helixRadius, p.glyphParams.risePerTurn,
        p.glyphParams.spikes, p.glyphParams.capsidFaces, p.glyphParams.beadCount,
        p.glyphParams.nucleusOffset
    };
    const size_t count = sizeof(values) / sizeof(values[0]);
    for (size_t i = 0; i < count; ++i) out << ";g." << kNames[i] << "=" << fmtDouble(values[i]);
    return out.str();
}

BioShapeParams decodeBioParams(const std::string& text, bool* ok) {
    BioShapeParams out;
    if (ok) *ok = false;
    std::vector<std::pair<std::string, std::string> > pairs;
    if (!splitPairs(text, &pairs)) return out;
    // A record with no recognisable version is NOT optimistically accepted.
    const std::string* version = findPair(pairs, "v");
    int v = 0;
    if (!version || !parseInt(*version, &v) || v != 1) return out;

    int value = 0;
    if (const std::string* s = findPair(pairs, "preset")) { if (parseU32(*s, &out.presetId)) {} }
    if (const std::string* s = findPair(pairs, "family")) { if (parseInt(*s, &value)) out.family = static_cast<BioFamily>(value); }
    if (const std::string* s = findPair(pairs, "glyph"))  { if (parseInt(*s, &value)) out.glyph  = static_cast<GlyphKind>(value); }
    if (const std::string* s = findPair(pairs, "variant")){ if (parseInt(*s, &value)) out.variant = value; }
    if (const std::string* s = findPair(pairs, "cx")) { parseDouble(*s, &out.centre.x); }
    if (const std::string* s = findPair(pairs, "cy")) { parseDouble(*s, &out.centre.y); }
    if (const std::string* s = findPair(pairs, "ux")) { parseDouble(*s, &out.axisU.x); }
    if (const std::string* s = findPair(pairs, "uy")) { parseDouble(*s, &out.axisU.y); }
    if (const std::string* s = findPair(pairs, "vx")) { parseDouble(*s, &out.axisV.x); }
    if (const std::string* s = findPair(pairs, "vy")) { parseDouble(*s, &out.axisV.y); }
    if (const std::string* s = findPair(pairs, "rot"))   { parseDouble(*s, &out.rotationDeg); }
    if (const std::string* s = findPair(pairs, "scale")) { parseDouble(*s, &out.scale); }
    if (const std::string* s = findPair(pairs, "fill"))  { parseU32(*s, &out.fillRGBA); }
    if (const std::string* s = findPair(pairs, "stroke")){ parseU32(*s, &out.style.colorRGBA); }
    if (const std::string* s = findPair(pairs, "width")) { parseDouble(*s, &out.style.width); }
    if (const std::string* s = findPair(pairs, "dash"))  { if (parseInt(*s, &value)) out.style.dash = static_cast<uint8_t>(value); }

    static const char* const kBare[] = {
        "lobes", "lobeDepth", "cornerFraction", "nodes", "innerRatio",
        "sweepDeg", "turns", "amplitude", "barrelAngleDeg", "leafletGap",
        "headRadius", "tailLength", "subunitRadius", "tmSegments", "loops",
        "ligandPresent", "disulfides", "chainLength", "basePairs",
        "helixRadius", "risePerTurn", "spikes", "capsidFaces", "beadCount",
        "nucleusOffset"
    };
    double* slots[] = {
        &out.glyphParams.lobes, &out.glyphParams.lobeDepth, &out.glyphParams.cornerFraction,
        &out.glyphParams.nodes, &out.glyphParams.innerRatio, &out.glyphParams.sweepDeg,
        &out.glyphParams.turns, &out.glyphParams.amplitude, &out.glyphParams.barrelAngleDeg,
        &out.glyphParams.leafletGap, &out.glyphParams.headRadius, &out.glyphParams.tailLength,
        &out.glyphParams.subunitRadius, &out.glyphParams.tmSegments, &out.glyphParams.loops,
        &out.glyphParams.ligandPresent, &out.glyphParams.disulfides, &out.glyphParams.chainLength,
        &out.glyphParams.basePairs, &out.glyphParams.helixRadius, &out.glyphParams.risePerTurn,
        &out.glyphParams.spikes, &out.glyphParams.capsidFaces, &out.glyphParams.beadCount,
        &out.glyphParams.nucleusOffset
    };
    const size_t count = sizeof(kBare) / sizeof(kBare[0]);
    int parsed = 0;
    for (size_t i = 0; i < count; ++i) {
        std::string key = "g.";
        key += kBare[i];
        const std::string* s = findPair(pairs, key.c_str());
        if (!s) continue;
        double parsedValue = 0.0;
        if (!parseDouble(*s, &parsedValue)) return out;   // a bad number is a failure
        *slots[i] = parsedValue;
        ++parsed;
    }
    // Too few fields means this is not (or not yet) a BioDraw record.
    if (parsed < static_cast<int>(count)) return out;
    // A persisted frame must be usable geometry, not a degenerate point.
    if (!(std::fabs(out.scale) > 1e-9)) return out;
    if (ok) *ok = true;
    return out;
}
// ---------------------------------------------------------------------------
// Component list
// ---------------------------------------------------------------------------
// One entry per line-ish group separated by '|', fields by ','. A component
// list is USER DATA ("this membrane has 17 lipids"), so a count that is out of
// range is clamped by the MODEL, not silently dropped here -- but a non-numeric
// field is still a decode failure.
std::string encodeComponents(const std::vector<ComponentSpec>& c) {
    std::ostringstream out;
    out << "v=1";
    for (size_t i = 0; i < c.size(); ++i) {
        out << "|c=" << static_cast<int>(c[i].kind)
            << "," << c[i].count
            << "," << fmtDouble(c[i].spacing)
            << "," << fmtDouble(c[i].unitSize)
            << "," << fmtDouble(c[i].phase)
            << "," << (c[i].evenlySpaced ? 1 : 0);
    }
    return out.str();
}

std::vector<ComponentSpec> decodeComponents(const std::string& text, bool* ok) {
    std::vector<ComponentSpec> out;
    if (ok) *ok = false;
    std::vector<std::pair<std::string, std::string> > pairs;
    if (!splitPipes(text, &pairs)) return out;
    const std::string* version = findPair(pairs, "v");
    int v = 0;
    if (!version || !parseInt(*version, &v) || v != 1) return out;
    for (size_t i = 0; i < pairs.size(); ++i) {
        if (pairs[i].first != "c") continue;
        const std::string& payload = pairs[i].second;
        const std::vector<std::string> parts = splitCommas(payload);
        if (parts.size() != 6) return std::vector<ComponentSpec>();
        ComponentSpec spec;
        int kind = 0, count = 0, even = 1;
        if (!parseInt(parts[0], &kind)) return std::vector<ComponentSpec>();
        if (!parseInt(parts[1], &count)) return std::vector<ComponentSpec>();
        if (!parseDouble(parts[2], &spec.spacing)) return std::vector<ComponentSpec>();
        if (!parseDouble(parts[3], &spec.unitSize)) return std::vector<ComponentSpec>();
        if (!parseDouble(parts[4], &spec.phase)) return std::vector<ComponentSpec>();
        if (!parseInt(parts[5], &even)) return std::vector<ComponentSpec>();
        spec.kind = static_cast<ComponentKind>(kind);
        spec.count = count;
        spec.evenlySpaced = even != 0;
        out.push_back(spec);
    }
    if (ok) *ok = true;
    return out;
}

// ---------------------------------------------------------------------------
// Plasmid record
// ---------------------------------------------------------------------------
// The plasmid record is the one place the 'do not lose data' acceptance
// criterion is decided, so every marker and region is written EXPLICITLY and
// read back field by field. Nothing about a feature is derived from another
// feature, and a feature that fails to parse fails the WHOLE record rather than
// disappearing quietly.
std::string encodePlasmid(const PlasmidData& d) {
    std::ostringstream out;
    out << "v=1"
        << ";bases=" << d.baseCount
        << ";start=" << fmtDouble(d.startAngleDeg)
        << ";cw=" << (d.clockwise ? 1 : 0)
        << ";title=" << d.title;
    for (size_t i = 0; i < d.features.size(); ++i) {
        const PlasmidFeature& f = d.features[i];
        out << "|f=" << f.name
            << "," << f.start
            << "," << f.end
            << "," << f.colorRGBA
            << "," << f.kind
            << "," << fmtDouble(f.track);
    }
    return out.str();
}

PlasmidData decodePlasmid(const std::string& text, bool* ok) {
    PlasmidData out;
    if (ok) *ok = false;
    // The record is 'header|entry|entry'. The HEADER IS SPLIT OFF FIRST, before
    // anything is read from it: splitting the whole string on ';' would make the
    // last header field swallow the entire entry list as its value, and re-encoding
    // would then write those entries inside the title AND again as entries --
    // duplicating every feature. That defect was observed on a write-back round
    // trip (3 features became 6) and is why the split order here is load-bearing.
    const size_t firstPipe = text.find('|');
    const std::string header = (firstPipe == std::string::npos) ? text : text.substr(0, firstPipe);
    const std::string body = (firstPipe == std::string::npos) ? std::string() : text.substr(firstPipe);

    std::vector<std::pair<std::string, std::string> > pairs;
    if (!splitPairs(header, &pairs)) return out;
    const std::string* version = findPair(pairs, "v");
    int v = 0;
    if (!version || !parseInt(*version, &v) || v != 1) return out;
    if (const std::string* s = findPair(pairs, "bases")) { if (!parseInt(*s, &out.baseCount)) return out; }
    if (const std::string* s = findPair(pairs, "start")) { if (!parseDouble(*s, &out.startAngleDeg)) return out; }
    if (const std::string* s = findPair(pairs, "cw"))    { int c = 1; if (!parseInt(*s, &c)) return out; out.clockwise = c != 0; }
    if (const std::string* s = findPair(pairs, "title")) { out.title = *s; }

    // Now the entries, from the body only.
    std::vector<std::pair<std::string, std::string> > entries;
    if (!splitPipes(body, &entries)) return out;
    for (size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].first != "f") continue;
        const std::string& payload = entries[i].second;
        const std::vector<std::string> parts = splitCommas(payload);
        if (parts.size() != 6) return PlasmidData();
        PlasmidFeature f;
        f.name = parts[0];
        if (!parseInt(parts[1], &f.start)) return PlasmidData();
        if (!parseInt(parts[2], &f.end)) return PlasmidData();
        if (!parseU32(parts[3], &f.colorRGBA)) return PlasmidData();
        if (!parseInt(parts[4], &f.kind)) return PlasmidData();
        if (!parseDouble(parts[5], &f.track)) return PlasmidData();
        out.features.push_back(f);
    }
    if (ok) *ok = true;
    return out;
}

// ---------------------------------------------------------------------------
// Colour overrides
// ---------------------------------------------------------------------------
std::string encodeColors(const std::vector<std::pair<uint32_t, uint32_t> >& c) {
    std::ostringstream out;
    out << "v=1";
    for (size_t i = 0; i < c.size(); ++i)
        out << "|k=" << c[i].first << "," << c[i].second;
    return out.str();
}

std::vector<std::pair<uint32_t, uint32_t> > decodeColors(const std::string& text, bool* ok) {
    std::vector<std::pair<uint32_t, uint32_t> > out;
    if (ok) *ok = false;
    std::vector<std::pair<std::string, std::string> > pairs;
    if (!splitPipes(text, &pairs)) return out;
    const std::string* version = findPair(pairs, "v");
    int v = 0;
    if (!version || !parseInt(*version, &v) || v != 1) return out;
    for (size_t i = 0; i < pairs.size(); ++i) {
        if (pairs[i].first != "k") continue;
        const size_t comma = pairs[i].second.find(',');
        if (comma == std::string::npos) return std::vector<std::pair<uint32_t, uint32_t> >();
        uint32_t key = 0, colour = 0;
        if (!parseU32(pairs[i].second.substr(0, comma), &key)) return std::vector<std::pair<uint32_t, uint32_t> >();
        if (!parseU32(pairs[i].second.substr(comma + 1), &colour)) return std::vector<std::pair<uint32_t, uint32_t> >();
        out.push_back(std::make_pair(key, colour));
    }
    if (ok) *ok = true;
    return out;
}
// ---------------------------------------------------------------------------
// Frame helpers
// ---------------------------------------------------------------------------
void frameAxes(const BioShapeParams& p, core::Vec2* outU, core::Vec2* outV) {
    // Rotation by p.rotationDeg, following core::direction() (degrees, positive
    // is counter-clockwise in the math sense, which on a y-down screen reads as
    // counter-clockwise visually too because core::direction(90) IS up).
    const double radians = p.rotationDeg * 3.14159265358979323846 / 180.0;
    const double c = std::cos(radians);
    const double s = std::sin(radians);
    const double ux = p.axisU.x * p.scale;
    const double uy = p.axisU.y * p.scale;
    const double vx = p.axisV.x * p.scale;
    const double vy = p.axisV.y * p.scale;
    if (outU) *outU = core::Vec2(ux * c - uy * s, ux * s + uy * c);
    if (outV) *outV = core::Vec2(vx * c - vy * s, vx * s + vy * c);
}

core::Vec2 framePoint(const BioShapeParams& p, double u, double v) {
    core::Vec2 axisU, axisV;
    frameAxes(p, &axisU, &axisV);
    return core::Vec2(p.centre.x + u * axisU.x + v * axisV.x,
                      p.centre.y + u * axisU.y + v * axisV.y);
}

core::Box frameAabb(const BioShapeParams& p) {
    const core::Vec2 c[4] = { framePoint(p, -1.0, -1.0), framePoint(p, 1.0, -1.0),
                              framePoint(p, 1.0, 1.0),   framePoint(p, -1.0, 1.0) };
    core::Box box(c[0], c[0]);
    for (int i = 1; i < 4; ++i) box.add(c[i]);
    return box;
}

// ---------------------------------------------------------------------------
// Sub-element identity
// ---------------------------------------------------------------------------
uint32_t subElementKeyForUnit(uint32_t componentIndex, uint32_t unitIndex) {
    // Component index and unit index are each bounded to 12 bits. That is not an
    // arbitrary limit: 4096 components x 4096 units is far beyond any figure a
    // user can place, and the bound is what makes the key unique. A caller that
    // overflows gets a SATURATED key rather than a key that aliases another
    // element -- aliasing would silently recolour the wrong thing.
    if (componentIndex > 0xFFFu) componentIndex = 0xFFFu;
    if (unitIndex > 0xFFFu) unitIndex = 0xFFFu;
    return 0xCC000000u | (componentIndex << 12) | unitIndex;
}

uint32_t subElementKeyForBand(uint32_t bandId) {
    if (bandId > 0xFFu) bandId = 0xFFu;
    return 0xBB000000u | bandId;
}

bool parseSubElementKey(uint32_t key, bool* isUnit, uint32_t* a, uint32_t* b) {
    if (isUnit) *isUnit = false;
    if (a) *a = 0;
    if (b) *b = 0;
    if ((key & 0xFF000000u) == 0xCC000000u) {
        if (isUnit) *isUnit = true;
        if (a) *a = (key >> 12) & 0xFFFu;
        if (b) *b = key & 0xFFFu;
        return true;
    }
    if ((key & 0xFFFFFF00u) == 0xBB000000u) {
        if (isUnit) *isUnit = false;
        if (a) *a = key & 0xFFu;
        return true;
    }
    return false;
}

uint32_t resolveSubElementColor(const BioShapeParams& p, uint32_t key) {
    for (size_t i = 0; i < p.subElementColors.size(); ++i)
        if (p.subElementColors[i].first == key) return p.subElementColors[i].second;
    return p.fillRGBA;
}

bool setSubElementColor(BioShapeParams* p, uint32_t key, uint32_t rgba) {
    if (!p) return false;
    for (size_t i = 0; i < p->subElementColors.size(); ++i) {
        if (p->subElementColors[i].first != key) continue;
        if (p->subElementColors[i].second == rgba) return false;
        p->subElementColors[i].second = rgba;
        return true;
    }
    p->subElementColors.push_back(std::make_pair(key, rgba));
    return true;
}

bool clearSubElementColor(BioShapeParams* p, uint32_t key) {
    if (!p) return false;
    for (size_t i = 0; i < p->subElementColors.size(); ++i) {
        if (p->subElementColors[i].first != key) continue;
        p->subElementColors.erase(p->subElementColors.begin() + static_cast<long>(i));
        return true;
    }
    return false;
}

} // namespace biodraw
} // namespace r62