// =============================================================================
// re_round62_viewer/next/biodraw/plasmid.cpp
//
// T14 - plasmid map data implementation.
// =============================================================================

#include "next/biodraw/plasmid.hpp"

#include <cmath>
#include <cstdio>

namespace r62 {
namespace biodraw {

namespace {

// Wrap a base index into [0, baseCount). A negative index wraps backward, which
// is what a user dragging a marker past base 0 expects.
int wrapBase(int value, int baseCount) {
    if (baseCount <= 0) return 0;
    int v = value % baseCount;
    if (v < 0) v += baseCount;
    return v;
}

// The number of bases a feature covers, taking a wrap into account.
int coveredSpan(const PlasmidFeature& f, int baseCount) {
    if (baseCount <= 0) return 0;
    if (f.end >= f.start) return f.end - f.start;
    return (baseCount - f.start) + f.end;
}

} // namespace

// ---------------------------------------------------------------------------
// Default data
// ---------------------------------------------------------------------------
PlasmidData defaultPlasmidData() {
    PlasmidData data;
    data.baseCount = 2686;
    data.startAngleDeg = 90.0;   // base 0 at the top of the map
    data.clockwise = true;
    data.title = "pUC19 (illustrative)";

    PlasmidFeature ampr;
    ampr.name = "AmpR";
    ampr.start = 1626;
    ampr.end = 2486;
    ampr.colorRGBA = 0xC83C3CFFu;
    ampr.kind = 0;
    ampr.track = 0.0;
    data.features.push_back(ampr);

    PlasmidFeature ori;
    ori.name = "ori";
    ori.start = 2535;
    ori.end = 2686;   // deliberately runs to the END of the circle
    ori.colorRGBA = 0x3C78C8FFu;
    ori.kind = 0;
    ori.track = 1.0;
    data.features.push_back(ori);

    PlasmidFeature site;
    site.name = "EcoRI";
    site.start = 396;
    site.end = 402;
    site.colorRGBA = 0x3CC83CFFu;
    site.kind = 1;   // a marker / restriction site
    site.track = 2.0;
    data.features.push_back(site);

    return data;
}

int plasmidCoveredBases(const PlasmidData& data) {
    int total = 0;
    for (size_t i = 0; i < data.features.size(); ++i)
        total += coveredSpan(data.features[i], data.baseCount);
    return total;
}

// ---------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------
bool normalizeFeatureRange(const PlasmidData& data, int* start, int* end) {
    if (!start || !end) return false;
    if (data.baseCount <= 0) return false;
    const int s = wrapBase(*start, data.baseCount);
    int e = wrapBase(*end, data.baseCount);
    // An empty or degenerate range becomes a minimum span. It is NEVER zero:
    // a zero-length feature would draw nothing and look like lost data.
    if (e == s) e = wrapBase(s + kMinFeatureSpan, data.baseCount);
    *start = s;
    *end = e;
    return true;
}

bool validatePlasmidData(const PlasmidData& data, std::string* reason) {
    if (reason) reason->clear();
    if (data.baseCount <= 0) {
        if (reason) *reason = "base count must be positive";
        return false;
    }
    if (data.baseCount > kMaxPlasmidBases) {
        if (reason) *reason = "base count exceeds the supported maximum";
        return false;
    }
    if (data.features.size() > static_cast<size_t>(kMaxPlasmidFeatures)) {
        if (reason) *reason = "too many features for one map";
        return false;
    }
    for (size_t i = 0; i < data.features.size(); ++i) {
        if (data.features[i].name.empty()) {
            if (reason) *reason = "a feature has an empty name";
            return false;
        }
        if (data.features[i].start < 0 || data.features[i].start >= data.baseCount) {
            if (reason) *reason = "a feature start is outside the circle";
            return false;
        }
        if (data.features[i].end < 0 || data.features[i].end > data.baseCount) {
            // end == baseCount is allowed: it means 'up to the origin'.
            if (reason) *reason = "a feature end is outside the circle";
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Feature editing
// ---------------------------------------------------------------------------
bool moveFeatureStart(PlasmidData* data, size_t index, int newStart) {
    if (!data || index >= data->features.size()) return false;
    if (data->baseCount <= 0) return false;
    PlasmidFeature& f = data->features[index];
    const int span = coveredSpan(f, data->baseCount);
    const int s = wrapBase(newStart, data->baseCount);
    int e = wrapBase(s + span, data->baseCount);
    // Preserve the wrap CHARACTER of the feature: a feature that wrapped before
    // still wraps after a move. Without this, dragging a wrapping feature one
    // base would silently un-wrap it and change which bases it covers.
    const bool wrapped = f.end < f.start;
    if (wrapped && e == s) e = wrapBase(s + 1, data->baseCount);
    if (wrapped && e > s && span > 0 && (s + span) > data->baseCount) {
        // still wraps; e already equals wrapBase(s+span) which is < s
    }
    f.start = s;
    f.end = e;
    return true;
}

bool setFeatureSpan(PlasmidData* data, size_t index, int span) {
    if (!data || index >= data->features.size()) return false;
    if (data->baseCount <= 0) return false;
    if (span < kMinFeatureSpan) span = kMinFeatureSpan;
    if (span > data->baseCount) span = data->baseCount;
    PlasmidFeature& f = data->features[index];
    const int s = wrapBase(f.start, data->baseCount);
    int e = wrapBase(s + span, data->baseCount);
    if (e == s) e = wrapBase(s + kMinFeatureSpan, data->baseCount);
    f.start = s;
    f.end = e;
    return true;
}

bool setFeatureRange(PlasmidData* data, size_t index, int start, int end) {
    if (!data || index >= data->features.size()) return false;
    int s = start, e = end;
    // A caller that passes start == end asks for an empty feature, which is
    // rejected rather than silently widened -- widening would MOVE the feature
    // the user just emptied, and losing that edit is exactly the failure mode the
    // acceptance criterion forbids.
    if (s == e) {
        if (!normalizeFeatureRange(*data, &s, &e)) return false;
    }
    if (data->baseCount <= 0) return false;
    s = wrapBase(s, data->baseCount);
    // `end` may legitimately equal baseCount, meaning 'up to the origin'.
    if (e == data->baseCount) e = 0;
    else e = wrapBase(e, data->baseCount);
    if (e == s) return false;
    data->features[index].start = s;
    data->features[index].end = e;
    return true;
}

bool addFeature(PlasmidData* data, const PlasmidFeature& feature) {
    if (!data) return false;
    if (data->baseCount <= 0) return false;
    if (feature.name.empty()) return false;
    if (data->features.size() >= static_cast<size_t>(kMaxPlasmidFeatures)) return false;
    size_t existing = 0;
    if (findFeature(*data, feature.name, &existing)) return false;   // duplicate name
    PlasmidFeature copy = feature;
    if (!normalizeFeatureRange(*data, &copy.start, &copy.end)) return false;
    if (copy.start < 0 || copy.start >= data->baseCount) return false;
    data->features.push_back(copy);
    return true;
}

bool removeFeature(PlasmidData* data, size_t index) {
    if (!data || index >= data->features.size()) return false;
    data->features.erase(data->features.begin() + static_cast<long>(index));
    return true;
}

bool findFeature(const PlasmidData& data, const std::string& name, size_t* index) {
    for (size_t i = 0; i < data.features.size(); ++i) {
        if (data.features[i].name == name) {
            if (index) *index = i;
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Geometry derived from the data
// ---------------------------------------------------------------------------
FeatureAngle featureAngleSpan(const PlasmidData& data, const PlasmidFeature& feature) {
    FeatureAngle out;
    if (data.baseCount <= 0) return out;
    const double perBase = 360.0 / static_cast<double>(data.baseCount);
    const double direction = data.clockwise ? -1.0 : 1.0;
    out.startDeg = data.startAngleDeg + direction * perBase * static_cast<double>(feature.start);
    const int span = coveredSpan(feature, data.baseCount);
    out.spanDeg = direction * perBase * static_cast<double>(span);
    out.wraps = feature.end < feature.start;
    return out;
}

double featureLabelAngleDeg(const PlasmidData& data, const PlasmidFeature& feature) {
    const FeatureAngle a = featureAngleSpan(data, feature);
    return a.startDeg + 0.5 * a.spanDeg;
}

} // namespace biodraw
} // namespace r62
