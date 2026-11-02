#include "next/selection/bounds.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace r62 {
namespace selection {

namespace {

// A box that unions NOTHING. core::Box's default is (0,0)-(0,0), which is a
// valid point box and would silently drag every union's min down to the origin.
} // namespace

int compiledCoreInterfaceVersion() { return core::kInterfaceVersion; }

// ---------------------------------------------------------------------------
// BoundsReport
// ---------------------------------------------------------------------------
BoundsReport::BoundsReport()
    : objectsMeasured(0), textWithoutMetrics(0), emptyObjects(0),
      unruledPresets(0), deadRefsSkipped(0), groupsExpanded(0) {}

void BoundsReport::clear() {
    objectsMeasured = 0;
    textWithoutMetrics = 0;
    emptyObjects = 0;
    unruledPresets = 0;
    deadRefsSkipped = 0;
    groupsExpanded = 0;
}

std::string BoundsReport::summary() const {
    std::ostringstream out;
    out << "measured=" << objectsMeasured
        << " textWithoutMetrics=" << textWithoutMetrics
        << " empty=" << emptyObjects
        << " unruledPresets=" << unruledPresets
        << " deadRefs=" << deadRefsSkipped
        << " groupsExpanded=" << groupsExpanded;
    return out.str();
}

// ---------------------------------------------------------------------------
// BoundsOptions
// ---------------------------------------------------------------------------
BoundsOptions::BoundsOptions()
    : textMetrics(0), defaultTextSize(10.0), defaultFont("Arial"),
      expandGroups(true), maxGroupDepth(64), annotationRules(0) {}

// ---------------------------------------------------------------------------
// MonospaceMetricSink
// ---------------------------------------------------------------------------
// Every code point is one em wide and the ink box spans from the baseline up
// one em (in document coordinates y grows DOWN, so "up" is anchor.y - size).
// Newlines restart the pen on the next line, which is what makes a multi-line
// caption's box taller - and is the property the tests assert.
bool MonospaceMetricSink::textInkBox(const std::string& utf8, const core::Vec2& anchor,
                                     double size, const std::string& fontName,
                                     core::Box* out) {
    (void)fontName;
    if (!out) return false;
    if (!anchor.finite() || !(size > 0.0)) return false;
    // Count code POINTS, not bytes: a UTF-8 continuation byte is 0b10xxxxxx.
    double lineWidth = 0.0, maxWidth = 0.0;
    double line = 0.0, maxLine = 0.0;
    for (size_t i = 0; i < utf8.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(utf8[i]);
        if (c == '\n') {
            if (lineWidth > maxWidth) maxWidth = lineWidth;
            lineWidth = 0.0;
            line += 1.0;
            continue;
        }
        if ((c & 0xC0u) == 0x80u) continue;   // continuation byte
        lineWidth += 1.0;
    }
    if (lineWidth > maxWidth) maxWidth = lineWidth;
    maxLine = line + 1.0;
    // A NEGATIVE height is not produced: an empty string still has zero width
    // and one line of height, which is what "the anchor with no ink" means.
    const double w = maxWidth * size;
    const double h = maxLine * size;
    out->min = core::Vec2(anchor.x, anchor.y - h);
    out->max = core::Vec2(anchor.x + w, anchor.y);
    return true;
}

// ---------------------------------------------------------------------------
// AnnotationGeomRules
// ---------------------------------------------------------------------------
AnnotationGeomRules::AnnotationGeomRules() {}

AnnotationGeomRule AnnotationGeomRules::ruleFor(core::ObjectKind kind, uint32_t presetId) const {
    const std::map<std::pair<uint16_t, uint32_t>, AnnotationGeomRule>::const_iterator it =
        rules_.find(std::make_pair(static_cast<uint16_t>(kind), presetId));
    if (it != rules_.end()) return it->second;
    AnnotationGeomRule fallback;
    // Arrows and brackets are two-point by nature; the science and bio slabs are
    // placed shapes with a full control box. These are reported as UNRULED when
    // they hit the fallback so a missing rule is visible rather than invisible.
    switch (kind) {
    case core::ObjectKind::Arrow:
    case core::ObjectKind::Bracket:   fallback.usedPoints = 2; break;
    default:                          fallback.usedPoints = 4; break;
    }
    return fallback;
}

void AnnotationGeomRules::setRule(core::ObjectKind kind, uint32_t presetId,
                                  const AnnotationGeomRule& rule) {
    rules_[std::make_pair(static_cast<uint16_t>(kind), presetId)] = rule;
}

// ---------------------------------------------------------------------------
// Generic box helpers
// ---------------------------------------------------------------------------
void unionBox(core::Box* accumulator, const core::Box& box) {
    if (!accumulator) return;
    if (box.empty() || !boxFinite(box)) return;
    if (accumulator->empty()) { *accumulator = box; return; }
    accumulator->add(box.min);
    accumulator->add(box.max);
}

bool intersectBox(const core::Box& a, const core::Box& b, core::Box* out) {
    if (a.empty() || b.empty()) return false;
    const core::Box r(core::Vec2(std::max(a.min.x, b.min.x), std::max(a.min.y, b.min.y)),
                      core::Vec2(std::min(a.max.x, b.max.x), std::min(a.max.y, b.max.y)));
    if (r.empty()) return false;
    if (out) *out = r;
    return true;
}

core::Box inflateBox(const core::Box& box, double margin) {
    if (box.empty()) return box;
    return core::Box(core::Vec2(box.min.x - margin, box.min.y - margin),
                     core::Vec2(box.max.x + margin, box.max.y + margin));
}

core::Box scaleBox(const core::Box& box, const core::Vec2& pivot, double sx, double sy) {
    if (box.empty()) return box;
    const core::Vec2 a(pivot.x + (box.min.x - pivot.x) * sx, pivot.y + (box.min.y - pivot.y) * sy);
    const core::Vec2 b(pivot.x + (box.max.x - pivot.x) * sx, pivot.y + (box.max.y - pivot.y) * sy);
    // A negative scale (a flip) swaps the corners; normalise so min <= max holds
    // and every consumer can stop worrying about it.
    return core::Box(core::Vec2(std::min(a.x, b.x), std::min(a.y, b.y)),
                     core::Vec2(std::max(a.x, b.x), std::max(a.y, b.y)));
}

} // namespace selection
} // namespace r62
