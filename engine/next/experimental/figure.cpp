#include "next/experimental/figure.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <sstream>

namespace r62 {
namespace experimental {

// ---------------------------------------------------------------------------
// Local helpers
// ---------------------------------------------------------------------------
namespace {

const double kMinElementExtent = 1e-4;   // [PROJ] smallest legal radius/thickness
const double kMaxElementExtent = 1.0;    // [PROJ] relative half-extent bound

std::string num(double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.17g", v);
    return std::string(buf);
}

bool parseDouble(const std::string& text, double* out) {
    if (text.empty()) return false;
    char* end = 0;
    const double v = std::strtod(text.c_str(), &end);
    if (!end || *end != '\0') return false;
    if (!std::isfinite(v)) return false;
    *out = v;
    return true;
}

bool parseU32(const std::string& text, uint32_t* out) {
    if (text.empty()) return false;
    char* end = 0;
    const unsigned long v = std::strtoul(text.c_str(), &end, 10);
    if (!end || *end != '\0') return false;
    *out = static_cast<uint32_t>(v);
    return true;
}

void split(const std::string& text, char sep, std::vector<std::string>* out) {
    out->clear();
    std::string current;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == sep) { out->push_back(current); current.clear(); }
        else current.push_back(text[i]);
    }
    out->push_back(current);
}

std::string joinEncoded(const std::vector<std::string>& fields) {
    std::string out;
    for (size_t i = 0; i < fields.size(); ++i) {
        if (i) out.push_back('|');
        out += fields[i];
    }
    return out;
}

bool isSafeByte(unsigned char c) {
    if (c < 0x21u || c > 0x7Eu) return false;
    return c != (unsigned char)'%' && c != (unsigned char)'|' && c != (unsigned char)';';
}

// Relative-space tolerance: the caller's document-space tolerance converted by
// the smaller half-extent of the frame. Used by geometry.cpp through this file's
// public API only; kept here because it is a pure numeric policy.
} // namespace

// ---------------------------------------------------------------------------
// MeasurementValue
// ---------------------------------------------------------------------------
const char* measureOriginName(MeasureOrigin origin) {
    switch (origin) {
    case MeasureOrigin::Unspecified:     return "Unspecified";
    case MeasureOrigin::UserEntered:     return "UserEntered";
    case MeasureOrigin::InstrumentFile:  return "InstrumentFile";
    case MeasureOrigin::VendorReference: return "VendorReference";
    }
    return "Unspecified";
}

bool measureOriginFromName(const std::string& name, MeasureOrigin* out) {
    if (!out) return false;
    if (name == "Unspecified")     { *out = MeasureOrigin::Unspecified; return true; }
    if (name == "UserEntered")     { *out = MeasureOrigin::UserEntered; return true; }
    if (name == "InstrumentFile")  { *out = MeasureOrigin::InstrumentFile; return true; }
    if (name == "VendorReference") { *out = MeasureOrigin::VendorReference; return true; }
    return false;
}

core::Error MeasurementValue::validate(std::string* why) const {
    if (!present) return core::Error::success();
    if (!std::isfinite(value)) {
        if (why) *why = "measurement value is not finite";
        return core::Error(core::ErrorCode::InvalidArgument, "measurement value is not finite");
    }
    if (unit.empty()) {
        if (why) *why = "measurement has no unit";
        return core::Error(core::ErrorCode::InvalidArgument, "measurement has no unit");
    }
    if (origin == MeasureOrigin::Unspecified) {
        if (why) *why = "measurement has no provenance";
        return core::Error(core::ErrorCode::InvalidArgument, "measurement has no provenance");
    }
    return core::Error::success();
}

// ---------------------------------------------------------------------------
// Defaults
// ---------------------------------------------------------------------------
PlateFigure::PlateFigure()
    : kind(FigureKind::TlcPlate),
      originDefined(true), originV(0.6),
      frontDefined(true),  frontV(-0.75),
      plateFillRGBA(0xF2F2F2FFu), plateFillEnabled(true),
      showLaneBorders(true), laneMarginV(0.92),
      nextElementId(1), payloadVersion(1) {
    border.width = 0.0;          // 0 == document default line width
    // core::LineStyle colour is 0xRRGGBBAA, so opaque black is 0x000000FF. The
    // T01 default 0xFF000000 is RED with alpha 0 (which T03 resolvePaint then
    // makes opaque red), so a figure that copied it would draw a red board.
    border.colorRGBA = 0x000000FFu;
    border.dash = 0;
    border.lineCap = 0;
}

PlateFigure makeDefaultFigure(FigureKind kind, uint32_t laneCount) {
    PlateFigure figure;
    figure.kind = kind;
    if (kind == FigureKind::GelElectrophoresis) {
        // A gel has no origin/front lines: nothing here defines Rf.
        figure.originDefined = false;
        figure.frontDefined = false;
        figure.plateFillRGBA = 0x303030FFu;
        figure.plateFillEnabled = true;
        figure.border.colorRGBA = 0x101010FFu;
    }
    if (laneCount == 0) laneCount = 1;
    if (laneCount > 12) laneCount = 12;
    const double span = 1.4;                     // [PROJ] lanes live in u in [-0.7, 0.7]
    const double step = laneCount > 1 ? span / static_cast<double>(laneCount - 1) : 0.0;
    const double start = laneCount > 1 ? -span * 0.5 : 0.0;
    double laneWidth = laneCount > 1 ? step * 0.55 : 0.6;
    if (laneWidth > 0.6) laneWidth = 0.6;   // keep the outermost lane inside the board
    for (uint32_t i = 0; i < laneCount; ++i) {
        Lane lane;
        lane.id = figure.nextElementId++;
        lane.u = start + step * static_cast<double>(i);
        lane.width = laneWidth;
        lane.dashedBorder = false;
        std::ostringstream label;
        label << "L" << (i + 1);
        lane.label = label.str();
        figure.lanes.push_back(lane);
    }
    return figure;
}

// ---------------------------------------------------------------------------
// Element ids
// ---------------------------------------------------------------------------
uint32_t allocateElementId(PlateFigure* figure) {
    if (!figure) return 0;
    if (figure->nextElementId == 0) figure->nextElementId = 1;
    return figure->nextElementId++;
}

bool elementIdInUse(const PlateFigure& figure, uint32_t id) {
    if (id == 0) return true;
    for (size_t i = 0; i < figure.lanes.size(); ++i) if (figure.lanes[i].id == id) return true;
    for (size_t i = 0; i < figure.spots.size(); ++i) if (figure.spots[i].id == id) return true;
    for (size_t i = 0; i < figure.bands.size(); ++i) if (figure.bands[i].id == id) return true;
    return false;
}

const Lane* findLane(const PlateFigure& figure, uint32_t id) {
    for (size_t i = 0; i < figure.lanes.size(); ++i)
        if (figure.lanes[i].id == id) return &figure.lanes[i];
    return 0;
}
const Spot* findSpot(const PlateFigure& figure, uint32_t id) {
    for (size_t i = 0; i < figure.spots.size(); ++i)
        if (figure.spots[i].id == id) return &figure.spots[i];
    return 0;
}
const Band* findBand(const PlateFigure& figure, uint32_t id) {
    for (size_t i = 0; i < figure.bands.size(); ++i)
        if (figure.bands[i].id == id) return &figure.bands[i];
    return 0;
}
Lane* mutableLane(PlateFigure* figure, uint32_t id) {
    if (!figure) return 0;
    for (size_t i = 0; i < figure->lanes.size(); ++i)
        if (figure->lanes[i].id == id) return &figure->lanes[i];
    return 0;
}
Spot* mutableSpot(PlateFigure* figure, uint32_t id) {
    if (!figure) return 0;
    for (size_t i = 0; i < figure->spots.size(); ++i)
        if (figure->spots[i].id == id) return &figure->spots[i];
    return 0;
}
Band* mutableBand(PlateFigure* figure, uint32_t id) {
    if (!figure) return 0;
    for (size_t i = 0; i < figure->bands.size(); ++i)
        if (figure->bands[i].id == id) return &figure->bands[i];
    return 0;
}

// ---------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------
core::Error validateFigure(const PlateFigure& figure, std::string* why) {
    const double kU = 1.0 + 1e-9;
    const double kV = 1.0 + 1e-9;

    if (figure.payloadVersion != 1)
        return core::Error(core::ErrorCode::InvalidArgument, "unknown payload version");
    if (figure.nextElementId == 0)
        return core::Error(core::ErrorCode::InvalidArgument, "nextElementId must be >= 1");

    // Origin / front: each may be independently undefined; when both are defined
    // the front must be strictly ABOVE the origin (frontV < originV) or Rf has no
    // meaning and the figure is refused rather than silently drawn wrong.
    if (figure.originDefined) {
        if (!std::isfinite(figure.originV) || figure.originV < -kU || figure.originV > kV) {
            if (why) *why = "originV is not inside the relative board";
            return core::Error(core::ErrorCode::InvalidGeometry, "originV out of range");
        }
    }
    if (figure.frontDefined) {
        if (!std::isfinite(figure.frontV) || figure.frontV < -kU || figure.frontV > kV) {
            if (why) *why = "frontV is not inside the relative board";
            return core::Error(core::ErrorCode::InvalidGeometry, "frontV out of range");
        }
    }
    if (figure.originDefined && figure.frontDefined && !(figure.frontV < figure.originV)) {
        if (why) *why = "solvent front must be above the origin (frontV < originV)";
        return core::Error(core::ErrorCode::InvalidGeometry,
                           "solvent front must be above the origin");
    }

    if (!std::isfinite(figure.laneMarginV) || figure.laneMarginV <= 0.0 || figure.laneMarginV > 1.0) {
        if (why) *why = "laneMarginV must be in (0, 1]";
        return core::Error(core::ErrorCode::InvalidGeometry, "laneMarginV out of range");
    }
    if (!std::isfinite(figure.border.width) || figure.border.width < 0.0) {
        if (why) *why = "border width must be finite and >= 0";
        return core::Error(core::ErrorCode::InvalidArgument, "border width invalid");
    }

    // Lanes
    for (size_t i = 0; i < figure.lanes.size(); ++i) {
        const Lane& lane = figure.lanes[i];
        if (lane.id == 0) {
            if (why) *why = "lane id 0 is not a valid element id";
            return core::Error(core::ErrorCode::InvalidArgument, "lane id 0");
        }
        for (size_t j = i + 1; j < figure.lanes.size(); ++j)
            if (figure.lanes[j].id == lane.id) {
                if (why) *why = "duplicate lane id";
                return core::Error(core::ErrorCode::AlreadyExists, "duplicate lane id");
            }
        if (!std::isfinite(lane.u) || lane.u < -kU || lane.u > kU) {
            if (why) *why = "lane u is outside [-1, 1]";
            return core::Error(core::ErrorCode::InvalidGeometry, "lane u out of range");
        }
        if (!std::isfinite(lane.width) || lane.width <= kMinElementExtent || lane.width > 2.0) {
            if (why) *why = "lane width must be in (0, 2]";
            return core::Error(core::ErrorCode::InvalidGeometry, "lane width out of range");
        }
        if (lane.u - lane.width * 0.5 < -kU || lane.u + lane.width * 0.5 > kU) {
            if (why) *why = "lane extends past the board edge";
            return core::Error(core::ErrorCode::InvalidGeometry, "lane extends past the board");
        }
    }

    // Spots
    for (size_t i = 0; i < figure.spots.size(); ++i) {
        const Spot& spot = figure.spots[i];
        if (spot.id == 0) {
            if (why) *why = "spot id 0 is not a valid element id";
            return core::Error(core::ErrorCode::InvalidArgument, "spot id 0");
        }
        for (size_t j = i + 1; j < figure.spots.size(); ++j)
            if (figure.spots[j].id == spot.id) {
                if (why) *why = "duplicate spot id";
                return core::Error(core::ErrorCode::AlreadyExists, "duplicate spot id");
            }
        if (figure.kind != FigureKind::TlcPlate) {
            if (why) *why = "spots are a TLC element; this figure is a gel";
            return core::Error(core::ErrorCode::InvalidSemantics, "spot on a gel figure");
        }
        if (spot.laneId != 0 && !findLane(figure, spot.laneId)) {
            if (why) *why = "spot references a lane that does not exist";
            return core::Error(core::ErrorCode::NotFound, "spot laneId not found");
        }
        if (!std::isfinite(spot.u) || spot.u < -kU || spot.u > kU ||
            !std::isfinite(spot.v) || spot.v < -kV || spot.v > kV) {
            if (why) *why = "spot is outside the relative board";
            return core::Error(core::ErrorCode::InvalidGeometry, "spot out of range");
        }
        if (!std::isfinite(spot.radiusU) || spot.radiusU <= kMinElementExtent ||
            spot.radiusU > kMaxElementExtent ||
            !std::isfinite(spot.radiusV) || spot.radiusV <= kMinElementExtent ||
            spot.radiusV > kMaxElementExtent) {
            if (why) *why = "spot radius must be in (1e-4, 1]";
            return core::Error(core::ErrorCode::InvalidGeometry, "spot radius out of range");
        }
        if (!std::isfinite(spot.opacity) || spot.opacity < 0.0 || spot.opacity > 1.0) {
            if (why) *why = "spot opacity must be in [0, 1]";
            return core::Error(core::ErrorCode::InvalidArgument, "spot opacity out of range");
        }
        core::Error m = spot.rfMeasured.validate(why);
        if (!m.ok()) return m;
    }

    // Bands
    for (size_t i = 0; i < figure.bands.size(); ++i) {
        const Band& band = figure.bands[i];
        if (band.id == 0) {
            if (why) *why = "band id 0 is not a valid element id";
            return core::Error(core::ErrorCode::InvalidArgument, "band id 0");
        }
        for (size_t j = i + 1; j < figure.bands.size(); ++j)
            if (figure.bands[j].id == band.id) {
                if (why) *why = "duplicate band id";
                return core::Error(core::ErrorCode::AlreadyExists, "duplicate band id");
            }
        if (figure.kind != FigureKind::GelElectrophoresis) {
            if (why) *why = "bands are a gel element; this figure is a TLC plate";
            return core::Error(core::ErrorCode::InvalidSemantics, "band on a TLC figure");
        }
        if (band.laneId == 0 || !findLane(figure, band.laneId)) {
            if (why) *why = "band references a lane that does not exist";
            return core::Error(core::ErrorCode::NotFound, "band laneId not found");
        }
        if (!std::isfinite(band.v) || band.v < -kV || band.v > kV) {
            if (why) *why = "band v is outside the relative board";
            return core::Error(core::ErrorCode::InvalidGeometry, "band v out of range");
        }
        if (!std::isfinite(band.thicknessV) || band.thicknessV <= kMinElementExtent ||
            band.thicknessV > kMaxElementExtent) {
            if (why) *why = "band thickness must be in (1e-4, 1]";
            return core::Error(core::ErrorCode::InvalidGeometry, "band thickness out of range");
        }
        if (!std::isfinite(band.widthFraction) || band.widthFraction <= 0.0 ||
            band.widthFraction > 1.0) {
            if (why) *why = "band widthFraction must be in (0, 1]";
            return core::Error(core::ErrorCode::InvalidArgument, "band widthFraction out of range");
        }
        if (!std::isfinite(band.displayIntensity) || band.displayIntensity < 0.0 ||
            band.displayIntensity > 1.0) {
            if (why) *why = "band displayIntensity must be in [0, 1]";
            return core::Error(core::ErrorCode::InvalidArgument, "band displayIntensity out of range");
        }
        core::Error m = band.markerMass.validate(why);
        if (!m.ok()) return m;
    }

    for (size_t i = 0; i < figure.fields.size(); ++i) {
        if (figure.fields[i].name.empty()) {
            if (why) *why = "a measurement field needs a name";
            return core::Error(core::ErrorCode::InvalidArgument, "measurement field without a name");
        }
        core::Error m = figure.fields[i].value.validate(why);
        if (!m.ok()) return m;
    }

    // Annotation slots
    for (size_t i = 0; i < figure.annotations.size(); ++i) {
        const FigureAnnotation& a = figure.annotations[i];
        if (a.slot.empty()) {
            if (why) *why = "annotation slot name is empty";
            return core::Error(core::ErrorCode::InvalidArgument, "empty annotation slot");
        }
        // An annotation is a LABEL near the board, so it may sit just outside the
        // unit square (the title above it, a lane caption below it, an Rf caption
        // to the side). The bound is generous but finite, so a NaN or a wild value
        // is still refused.
        const double kAnnotationMargin = 4.0;
        if (!std::isfinite(a.u) || a.u < -kAnnotationMargin || a.u > kAnnotationMargin ||
            !std::isfinite(a.v) || a.v < -kAnnotationMargin || a.v > kAnnotationMargin) {
            if (why) *why = "annotation position is not a finite position near the board";
            return core::Error(core::ErrorCode::InvalidGeometry, "annotation out of range");
        }
    }

    return core::Error::success();
}

// ---------------------------------------------------------------------------
// Rf
// ---------------------------------------------------------------------------
bool rfMappingDefined(const PlateFigure& figure, std::string* why) {
    if (!figure.originDefined) { if (why) *why = "no origin line is defined"; return false; }
    if (!figure.frontDefined)  { if (why) *why = "no solvent front is defined"; return false; }
    if (!(figure.frontV < figure.originV)) {
        if (why) *why = "solvent front is not above the origin (frontV >= originV)";
        return false;
    }
    return true;
}

RfResult geometricRf(const PlateFigure& figure, double v) {
    RfResult result;
    std::string why;
    if (!rfMappingDefined(figure, &why)) {
        result.why = why;
        return result;
    }
    if (!std::isfinite(v)) {
        result.why = "spot v is not finite";
        return result;
    }
    const double span = figure.originV - figure.frontV;
    result.valid = true;
    result.rf = (figure.originV - v) / span;
    std::ostringstream basis;
    basis << "Rf = (originV - v) / (originV - frontV) = ("
          << num(figure.originV) << " - " << num(v) << ") / ("
          << num(figure.originV) << " - " << num(figure.frontV) << ") = "
          << num(result.rf)
          << "; v is the relative board coordinate (v increases downward), "
          << "relative units, Rf dimensionless";
    result.basis = basis.str();
    return result;
}

RfResult spotRf(const PlateFigure& figure, const Spot& spot) {
    return geometricRf(figure, spot.v);
}

double relativeVFromRf(const PlateFigure& figure, double rf) {
    std::string why;
    if (!rfMappingDefined(figure, &why)) return std::numeric_limits<double>::quiet_NaN();
    if (!std::isfinite(rf)) return std::numeric_limits<double>::quiet_NaN();
    return figure.originV - rf * (figure.originV - figure.frontV);
}

// ---------------------------------------------------------------------------
// Percent codec
// ---------------------------------------------------------------------------
std::string percentEncode(const std::string& value) {
    static const char* kHex = "0123456789ABCDEF";
    std::string out;
    out.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(value[i]);
        if (isSafeByte(c)) {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(kHex[(c >> 4) & 0x0Fu]);
            out.push_back(kHex[c & 0x0Fu]);
        }
    }
    return out;
}

bool percentDecode(const std::string& text, std::string* out) {
    if (!out) return false;
    out->clear();
    out->reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c != (unsigned char)'%') { out->push_back(static_cast<char>(c)); continue; }
        if (i + 2 >= text.size()) return false;
        const int hi = std::isdigit((unsigned char)text[i + 1]) ? text[i + 1] - '0'
                      : (std::toupper((unsigned char)text[i + 1]) >= 'A' &&
                         std::toupper((unsigned char)text[i + 1]) <= 'F'
                             ? std::toupper((unsigned char)text[i + 1]) - 'A' + 10 : -1);
        const int lo = std::isdigit((unsigned char)text[i + 2]) ? text[i + 2] - '0'
                      : (std::toupper((unsigned char)text[i + 2]) >= 'A' &&
                         std::toupper((unsigned char)text[i + 2]) <= 'F'
                             ? std::toupper((unsigned char)text[i + 2]) - 'A' + 10 : -1);
        if (hi < 0 || lo < 0) return false;
        out->push_back(static_cast<char>((hi << 4) | lo));
        i += 2;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Encoding
// ---------------------------------------------------------------------------
const char* const kFigureParamsKey = "exp.figure.params.1";

namespace {

void appendMeasurement(std::vector<std::string>* fields, const MeasurementValue& m) {
    fields->push_back(m.present ? "1" : "0");
    fields->push_back(num(m.value));
    fields->push_back(percentEncode(m.unit));
    fields->push_back(percentEncode(measureOriginName(m.origin)));
    fields->push_back(percentEncode(m.source));
}

std::string encodeMeasurementRecord(const char* tag, const MeasurementValue& m) {
    std::vector<std::string> f;
    f.push_back(tag);
    appendMeasurement(&f, m);
    return joinEncoded(f);
}

bool decodeMeasurement(const std::vector<std::string>& f, size_t base, MeasurementValue* out) {
    if (f.size() < base + 5) return false;
    if (f[base] != "0" && f[base] != "1") return false;
    MeasurementValue m;
    m.present = (f[base] == "1");
    if (!parseDouble(f[base + 1], &m.value)) return false;
    if (!percentDecode(f[base + 2], &m.unit)) return false;
    std::string origin;
    if (!percentDecode(f[base + 3], &origin)) return false;
    if (!measureOriginFromName(origin, &m.origin)) return false;
    if (!percentDecode(f[base + 4], &m.source)) return false;
    *out = m;
    return true;
}

} // namespace

std::string encodeFigure(const PlateFigure& figure) {
    std::vector<std::string> records;

    { std::vector<std::string> f; f.push_back("v"); f.push_back("1"); records.push_back(joinEncoded(f)); }
    { std::vector<std::string> f; f.push_back("kind");
      f.push_back(percentEncode(figureKindName(figure.kind))); records.push_back(joinEncoded(f)); }
    { std::vector<std::string> f; f.push_back("origin");
      f.push_back(figure.originDefined ? "1" : "0"); f.push_back(num(figure.originV));
      records.push_back(joinEncoded(f)); }
    { std::vector<std::string> f; f.push_back("front");
      f.push_back(figure.frontDefined ? "1" : "0"); f.push_back(num(figure.frontV));
      records.push_back(joinEncoded(f)); }
    { std::vector<std::string> f; f.push_back("title"); f.push_back(percentEncode(figure.title));
      records.push_back(joinEncoded(f)); }
    { std::vector<std::string> f; f.push_back("board");
      f.push_back(num(static_cast<double>(figure.plateFillRGBA)));
      f.push_back(figure.plateFillEnabled ? "1" : "0");
      f.push_back(figure.showLaneBorders ? "1" : "0");
      f.push_back(num(figure.laneMarginV));
      records.push_back(joinEncoded(f)); }
    { std::vector<std::string> f; f.push_back("stroke"); f.push_back(num(figure.border.width));
      f.push_back(num(static_cast<double>(figure.border.colorRGBA)));
      f.push_back(num(static_cast<double>(figure.border.dash)));
      records.push_back(joinEncoded(f)); }
    { std::vector<std::string> f; f.push_back("next");
      f.push_back(num(static_cast<double>(figure.nextElementId)));
      records.push_back(joinEncoded(f)); }
    { std::vector<std::string> f; f.push_back("pver"); f.push_back(num(figure.payloadVersion));
      records.push_back(joinEncoded(f)); }

    for (size_t i = 0; i < figure.lanes.size(); ++i) {
        const Lane& lane = figure.lanes[i];
        std::vector<std::string> f;
        f.push_back("lane");
        f.push_back(num(lane.id));
        f.push_back(num(lane.u));
        f.push_back(num(lane.width));
        f.push_back(lane.dashedBorder ? "1" : "0");
        f.push_back(percentEncode(lane.label));
        f.push_back(percentEncode(lane.note));
        records.push_back(joinEncoded(f));
    }
    for (size_t i = 0; i < figure.spots.size(); ++i) {
        const Spot& spot = figure.spots[i];
        std::vector<std::string> f;
        f.push_back("spot");
        f.push_back(num(spot.id));
        f.push_back(num(spot.laneId));
        f.push_back(num(spot.u));
        f.push_back(num(spot.v));
        f.push_back(num(spot.radiusU));
        f.push_back(num(spot.radiusV));
        f.push_back(num(static_cast<double>(spot.fillRGBA)));
        f.push_back(num(spot.opacity));
        f.push_back(percentEncode(spot.shape));
        f.push_back(percentEncode(spot.label));
        records.push_back(joinEncoded(f));
        if (spot.rfMeasured.present)
            records.push_back(encodeMeasurementRecord("spotrf", spot.rfMeasured));
    }
    for (size_t i = 0; i < figure.bands.size(); ++i) {
        const Band& band = figure.bands[i];
        std::vector<std::string> f;
        f.push_back("band");
        f.push_back(num(band.id));
        f.push_back(num(band.laneId));
        f.push_back(num(band.v));
        f.push_back(num(band.thicknessV));
        f.push_back(num(band.widthFraction));
        f.push_back(num(band.displayIntensity));
        f.push_back(num(static_cast<double>(band.fillRGBA)));
        f.push_back(percentEncode(band.label));
        records.push_back(joinEncoded(f));
        if (band.markerMass.present)
            records.push_back(encodeMeasurementRecord("bandmass", band.markerMass));
    }
    for (size_t i = 0; i < figure.fields.size(); ++i) {
        std::vector<std::string> f;
        f.push_back("field");
        f.push_back(percentEncode(figure.fields[i].name));
        appendMeasurement(&f, figure.fields[i].value);
        records.push_back(joinEncoded(f));
    }
    for (size_t i = 0; i < figure.annotations.size(); ++i) {
        const FigureAnnotation& a = figure.annotations[i];
        std::vector<std::string> f;
        f.push_back("annot");
        f.push_back(percentEncode(a.slot));
        f.push_back(num(a.u));
        f.push_back(num(a.v));
        f.push_back(num(a.textId));
        records.push_back(joinEncoded(f));
    }

    std::string out;
    for (size_t i = 0; i < records.size(); ++i) {
        if (i) out.push_back(';');
        out += records[i];
    }
    return out;
}

PlateFigure decodeFigure(const std::string& text, bool* ok) {
    PlateFigure figure;
    bool good = true;
    bool sawVersion = false;
    std::vector<std::string> records;
    split(text, ';', &records);
    for (size_t r = 0; r < records.size() && good; ++r) {
        if (records[r].empty()) continue;
        std::vector<std::string> f;
        split(records[r], '|', &f);
        const std::string& tag = f[0];
        if (tag == "v") {
            if (f.size() != 2 || f[1] != "1") { good = false; break; }
            sawVersion = true;
            continue;
        }
        if (!sawVersion) { good = false; break; }
        if (tag == "kind") {
            if (f.size() != 2) { good = false; break; }
            std::string name;
            if (!percentDecode(f[1], &name) || !figureKindFromName(name, &figure.kind)) { good = false; break; }
        } else if (tag == "origin") {
            if (f.size() != 3) { good = false; break; }
            figure.originDefined = (f[1] == "1");
            if (!parseDouble(f[2], &figure.originV)) { good = false; break; }
        } else if (tag == "front") {
            if (f.size() != 3) { good = false; break; }
            figure.frontDefined = (f[1] == "1");
            if (!parseDouble(f[2], &figure.frontV)) { good = false; break; }
        } else if (tag == "title") {
            if (f.size() != 2) { good = false; break; }
            if (!percentDecode(f[1], &figure.title)) { good = false; break; }
        } else if (tag == "board") {
            if (f.size() != 5) { good = false; break; }
            double rgba = 0;
            if (!parseDouble(f[1], &rgba)) { good = false; break; }
            figure.plateFillRGBA = static_cast<uint32_t>(rgba);
            figure.plateFillEnabled = (f[2] == "1");
            figure.showLaneBorders = (f[3] == "1");
            if (!parseDouble(f[4], &figure.laneMarginV)) { good = false; break; }
        } else if (tag == "stroke") {
            if (f.size() != 4) { good = false; break; }
            double rgba = 0, dash = 0;
            if (!parseDouble(f[1], &figure.border.width) ||
                !parseDouble(f[2], &rgba) || !parseDouble(f[3], &dash)) { good = false; break; }
            figure.border.colorRGBA = static_cast<uint32_t>(rgba);
            figure.border.dash = static_cast<uint8_t>(dash);
        } else if (tag == "next") {
            if (f.size() != 2) { good = false; break; }
            uint32_t v = 0;
            if (!parseU32(f[1], &v)) { good = false; break; }
            figure.nextElementId = v;
        } else if (tag == "pver") {
            if (f.size() != 2) { good = false; break; }
            uint32_t v = 0;
            if (!parseU32(f[1], &v)) { good = false; break; }
            figure.payloadVersion = v;
        } else if (tag == "lane") {
            if (f.size() != 7) { good = false; break; }
            Lane lane;
            if (!parseU32(f[1], &lane.id) || !parseDouble(f[2], &lane.u) ||
                !parseDouble(f[3], &lane.width)) { good = false; break; }
            lane.dashedBorder = (f[4] == "1");
            if (!percentDecode(f[5], &lane.label) || !percentDecode(f[6], &lane.note)) { good = false; break; }
            figure.lanes.push_back(lane);
        } else if (tag == "spot") {
            if (f.size() != 11) { good = false; break; }
            Spot spot;
            double rgba = 0;
            if (!parseU32(f[1], &spot.id) || !parseU32(f[2], &spot.laneId) ||
                !parseDouble(f[3], &spot.u) || !parseDouble(f[4], &spot.v) ||
                !parseDouble(f[5], &spot.radiusU) || !parseDouble(f[6], &spot.radiusV) ||
                !parseDouble(f[7], &rgba) || !parseDouble(f[8], &spot.opacity) ||
                !percentDecode(f[9], &spot.shape) || !percentDecode(f[10], &spot.label)) {
                good = false; break;
            }
            spot.fillRGBA = static_cast<uint32_t>(rgba);
            figure.spots.push_back(spot);
        } else if (tag == "spotrf") {
            if (!figure.spots.empty()) {
                MeasurementValue m;
                if (!decodeMeasurement(f, 1, &m)) { good = false; break; }
                figure.spots.back().rfMeasured = m;
            }
        } else if (tag == "band") {
            if (f.size() != 9) { good = false; break; }
            Band band;
            double rgba = 0;
            if (!parseU32(f[1], &band.id) || !parseU32(f[2], &band.laneId) ||
                !parseDouble(f[3], &band.v) || !parseDouble(f[4], &band.thicknessV) ||
                !parseDouble(f[5], &band.widthFraction) || !parseDouble(f[6], &band.displayIntensity) ||
                !parseDouble(f[7], &rgba) || !percentDecode(f[8], &band.label)) {
                good = false; break;
            }
            band.fillRGBA = static_cast<uint32_t>(rgba);
            figure.bands.push_back(band);
        } else if (tag == "bandmass") {
            if (!figure.bands.empty()) {
                MeasurementValue m;
                if (!decodeMeasurement(f, 1, &m)) { good = false; break; }
                figure.bands.back().markerMass = m;
            }
        } else if (tag == "field") {
            if (f.size() != 7) { good = false; break; }
            NamedMeasurement field;
            if (!percentDecode(f[1], &field.name)) { good = false; break; }
            if (!decodeMeasurement(f, 2, &field.value)) { good = false; break; }
            figure.fields.push_back(field);
        } else if (tag == "annot") {
            if (f.size() != 5) { good = false; break; }
            FigureAnnotation a;
            if (!percentDecode(f[1], &a.slot) || !parseDouble(f[2], &a.u) ||
                !parseDouble(f[3], &a.v)) { good = false; break; }
            uint32_t textId = 0;
            if (!parseU32(f[4], &textId)) { good = false; break; }
            a.textId = textId;
            figure.annotations.push_back(a);
        }
        // Unknown tags are ignored on purpose (forward compatibility).
    }

    if (ok) *ok = good && sawVersion;
    if (!good || !sawVersion) return PlateFigure();
    return figure;
}

// ---------------------------------------------------------------------------
// Signature (FNV-1a over the encoded payload)
// ---------------------------------------------------------------------------
uint64_t figureContentSignature(const PlateFigure& figure) {
    const std::string text = encodeFigure(figure);
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < text.size(); ++i) {
        h ^= static_cast<uint64_t>(static_cast<unsigned char>(text[i]));
        h *= 1099511628211ULL;
    }
    return h ? h : 1ULL;
}

} // namespace experimental
} // namespace r62
