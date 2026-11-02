// =============================================================================
// re_round62_viewer/next/render/batcher.cpp
//
// T03 - incremental mesh cache + device batching.
// =============================================================================

#include "next/render/batcher.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <unordered_map>

namespace r62 {
namespace render {

// Content signature used to decide whether a cached world mesh is still valid.
// Document serials bump every primitive's version on many edits (adding one
// atom bumps the whole scene); hashing the actual geometry/style lets the cache
// reuse the 3000+ unchanged meshes instead of rebuilding them.
static uint64_t primitiveSignature(const Primitive& p, double viewScale) {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](const void* data, size_t size) {
        const unsigned char* b = static_cast<const unsigned char*>(data);
        for (size_t i = 0; i < size; ++i) { h ^= b[i]; h *= 1099511628211ull; }
    };
    // O(1) signature: bounds + sampled points + text/style.  Hashing every
    // point of every primitive on a document-wide serial bump cost ~700 ms;
    // bounds catch moves, resizes and endpoint edits, which is what structural
    // editing changes.
    mix(&p.kind, sizeof(p.kind));
    mix(&p.layer.value, sizeof(p.layer.value));
    mix(&p.z, sizeof(p.z));
    size_t pointCount = p.points.size();
    mix(&pointCount, sizeof(pointCount));
    auto sample = [&](const std::vector<core::Vec2>& pts) {
        if (pts.empty()) return;
        const size_t n = pts.size();
        const size_t idx[5] = { 0, n / 4, n / 2, (n * 3) / 4, n - 1 };
        for (size_t k = 0; k < 5; ++k) {
            mix(&pts[idx[k]].x, sizeof(double));
            mix(&pts[idx[k]].y, sizeof(double));
        }
    };
    sample(p.points);
    size_t subCount = p.subpaths.size();
    mix(&subCount, sizeof(subCount));
    for (const auto& sub : p.subpaths) {
        size_t subPoints = sub.points.size();
        mix(&subPoints, sizeof(subPoints));
        sample(sub.points);
        mix(&sub.closed, sizeof(sub.closed));
    }
    mix(&p.bounds.min.x, sizeof(double));
    mix(&p.bounds.min.y, sizeof(double));
    mix(&p.bounds.max.x, sizeof(double));
    mix(&p.bounds.max.y, sizeof(double));
    if (!p.text.empty()) mix(p.text.data(), p.text.size());
    mix(&p.textSize, sizeof(p.textSize));
    if (!p.fontName.empty()) mix(p.fontName.data(), p.fontName.size());
    mix(&p.arrow, sizeof(p.arrow));
    mix(&p.marker, sizeof(p.marker));
    mix(&p.size, sizeof(p.size));
    mix(&p.style.stroke.rgba, sizeof(p.style.stroke.rgba));
    mix(&p.style.stroke.enabled, sizeof(p.style.stroke.enabled));
    mix(&p.style.fill.rgba, sizeof(p.style.fill.rgba));
    mix(&p.style.fill.enabled, sizeof(p.style.fill.enabled));
    mix(&p.style.strokeWidth, sizeof(p.style.strokeWidth));
    mix(&p.style.dash.on, sizeof(p.style.dash.on));
    mix(&p.style.dash.off, sizeof(p.style.dash.off));
    mix(&p.style.cap, sizeof(p.style.cap));
    mix(&p.style.join, sizeof(p.style.join));
    mix(&p.style.fillRule, sizeof(p.style.fillRule));
    mix(&p.style.wavyAmplitude, sizeof(p.style.wavyAmplitude));
    mix(&p.style.wavyWavelength, sizeof(p.style.wavyWavelength));
    mix(&p.style.miterLimit, sizeof(p.style.miterLimit));
    if (p.kind == PrimKind::Curve) mix(&viewScale, sizeof(viewScale));
    return h;
}

static inline bool overlaps(const core::Box& a, const core::Box& b) {
    return a.min.x <= b.max.x && a.max.x >= b.min.x &&
           a.min.y <= b.max.y && a.max.y >= b.min.y;
}

// ---------------------------------------------------------------------------
// SimpleCullIndex
// ---------------------------------------------------------------------------
// Rejects invalid boxes before mutating, and reacquires nodes by index because
// a recursive push_back can reallocate the vector - the same two hazards the
// legacy spatial index documents.
void SimpleCullIndex::rebuild(const std::vector<core::Box>& boxes) {
    boxes_.clear(); ids_.clear(); nodes_.clear();
    boxes_.reserve(boxes.size());
    for (size_t i = 0; i < boxes.size(); ++i) {
        core::Box b = boxes[i];
        if (!std::isfinite(b.min.x) || !std::isfinite(b.min.y) ||
            !std::isfinite(b.max.x) || !std::isfinite(b.max.y)) {
            b = core::Box(core::Vec2(0, 0), core::Vec2(0, 0));
        }
        if (b.max.x < b.min.x) std::swap(b.min.x, b.max.x);
        if (b.max.y < b.min.y) std::swap(b.min.y, b.max.y);
        boxes_.push_back(b);
    }
    ids_.resize(boxes_.size());
    for (size_t i = 0; i < boxes_.size(); ++i) ids_[i] = i;
    if (!boxes_.empty()) buildNode(0, boxes_.size());
}

size_t SimpleCullIndex::buildNode(size_t begin, size_t end) {
    core::Box bounds = boxes_[ids_[begin]];
    double minCx = bounds.min.x * 0.5 + bounds.max.x * 0.5;
    double maxCx = minCx;
    double minCy = bounds.min.y * 0.5 + bounds.max.y * 0.5;
    double maxCy = minCy;
    for (size_t i = begin + 1; i < end; ++i) {
        const core::Box& b = boxes_[ids_[i]];
        if (b.min.x < bounds.min.x) bounds.min.x = b.min.x;
        if (b.min.y < bounds.min.y) bounds.min.y = b.min.y;
        if (b.max.x > bounds.max.x) bounds.max.x = b.max.x;
        if (b.max.y > bounds.max.y) bounds.max.y = b.max.y;
        const double cx = b.min.x * 0.5 + b.max.x * 0.5;
        const double cy = b.min.y * 0.5 + b.max.y * 0.5;
        if (cx < minCx) minCx = cx;
        if (cx > maxCx) maxCx = cx;
        if (cy < minCy) minCy = cy;
        if (cy > maxCy) maxCy = cy;
    }
    Node node;
    node.box = bounds;
    node.begin = begin; node.end = end;
    node.left = node.right = 0;
    node.leaf = (end - begin) <= leafSize_;
    const size_t at = nodes_.size();
    nodes_.push_back(node);
    if (!node.leaf) {
        const bool xAxis = (maxCx * 0.5 - minCx * 0.5) >= (maxCy * 0.5 - minCy * 0.5);
        const size_t middle = begin + (end - begin) / 2;
        const std::vector<core::Box>& boxes = boxes_;
        std::vector<size_t>& ids = ids_;
        std::nth_element(ids.begin() + static_cast<std::ptrdiff_t>(begin),
                         ids.begin() + static_cast<std::ptrdiff_t>(middle),
                         ids.begin() + static_cast<std::ptrdiff_t>(end),
            [&boxes, xAxis](size_t a, size_t b) {
                const core::Box& ba = boxes[a];
                const core::Box& bb = boxes[b];
                const double ca = xAxis ? (ba.min.x + ba.max.x) * 0.5 : (ba.min.y + ba.max.y) * 0.5;
                const double cb = xAxis ? (bb.min.x + bb.max.x) * 0.5 : (bb.min.y + bb.max.y) * 0.5;
                return ca < cb || (ca == cb && a < b);
            });
        const size_t left = buildNode(begin, middle);
        const size_t right = buildNode(middle, end);
        nodes_[at].left = left;
        nodes_[at].right = right;
    }
    return at;
}

void SimpleCullIndex::query(const core::Box& viewport, std::vector<size_t>* out) const {
    out->clear();
    if (nodes_.empty()) return;
    queryNode(0, viewport, out);
    // Traversal order depends on the tree shape; sort so a caller's draw order
    // and a test's expectation do not depend on the split decisions.
    std::sort(out->begin(), out->end());
}

void SimpleCullIndex::queryNode(size_t at, const core::Box& v, std::vector<size_t>* out) const {
    const Node& node = nodes_[at];
    if (!overlaps(node.box, v)) return;
    if (node.leaf) {
        for (size_t i = node.begin; i < node.end; ++i) {
            const size_t id = ids_[i];
            if (overlaps(boxes_[id], v)) out->push_back(id);
        }
    } else {
        queryNode(node.left, v, out);
        queryNode(node.right, v, out);
    }
}

// ---------------------------------------------------------------------------
// Batcher
// ---------------------------------------------------------------------------
Batcher::Batcher() : indexSignature_(0), originX_(0), originY_(0),
                     lastViewHash_(0), haveView_(false) {}

void Batcher::clear() {
    scenes_.clear();
    sceneSlots_.clear();
    indexSignature_ = 0;
    haveView_ = false;
}

uint64_t Batcher::viewHash(const View& view, size_t primCount) {
    uint64_t h = 1469598103934665603ull;
    const core::ViewTransform& t = view.transform;
    double vals[8] = { t.center.x, t.center.y, t.scale, t.dpiScale,
                       t.viewportCenter.x, t.viewportCenter.y,
                       view.widthPx, view.heightPx };
    for (int i = 0; i < 8; ++i) {
        const double q = std::floor(vals[i] * 4096.0 + 0.5);
        h ^= static_cast<uint64_t>(static_cast<int64_t>(q));
        h *= 1099511628211ull;
    }
    h ^= view.clipEnabled ? 1ull : 0ull;
    h *= 1099511628211ull;
    double clip[4] = { view.clipMin.x, view.clipMin.y, view.clipMax.x, view.clipMax.y };
    for (int i = 0; i < 4; ++i) {
        h ^= static_cast<uint64_t>(static_cast<int64_t>(std::floor(clip[i] * 4096.0 + 0.5)));
        h *= 1099511628211ull;
    }
    h ^= static_cast<uint64_t>(primCount);
    h *= 1099511628211ull;
    h ^= static_cast<uint64_t>(std::floor(view.defaultStrokeWidth * 4096.0 + 0.5));
    h *= 1099511628211ull;
    return h;
}

// ---------------------------------------------------------------------------
// World mesh construction for one primitive
// ---------------------------------------------------------------------------
// This is the ONLY place world geometry is made. Everything it needs comes from
// the Primitive record, so the GL path and the rasterizer cannot diverge.
void Batcher::buildPrimitiveMeshes(const Primitive& prim, const View& view,
                                   const geom::TextMeasurer* measurer, CachedMesh* out,
                                   size_t* glyphSamples, size_t* glyphReuses) {
    out->fill.clear();
    out->stroke.clear();
    out->version = prim.version;
    out->valid = true;

    // Selection handles are axis-aligned filled rectangles. Avoid contour
    // allocation, flattening and general polygon triangulation for each one.
    if (prim.kind == PrimKind::Rect && prim.points.size() == 2 && prim.subpaths.empty() &&
        prim.style.fill.enabled && !prim.style.stroke.enabled) {
        const auto& a=prim.points[0];const auto& c=prim.points[1];
        core::Vec2 lo(std::min(a.x,c.x),std::min(a.y,c.y)),hi(std::max(a.x,c.x),std::max(a.y,c.y));
        out->fill.pushTriangle(lo,core::Vec2(hi.x,lo.y),hi);
        out->fill.pushTriangle(lo,hi,core::Vec2(lo.x,hi.y));
        out->worldBounds=prim.bounds;return;
    }
    // A plain segment needs no contour/dash/wave staging. Keep the exact same
    // stroke tessellator so line caps and rasterization remain unchanged.
    if (prim.kind == PrimKind::Segment && prim.points.size() == 2 && prim.subpaths.empty() &&
        !prim.style.fill.enabled && prim.style.stroke.enabled && prim.style.dash.solid() && prim.style.wavyAmplitude<=0) {
        geom::StrokeOptions so;so.width=prim.style.strokeWidth>0?prim.style.strokeWidth:view.defaultStrokeWidth;
        so.cap=prim.style.cap;so.join=prim.style.join;so.miterLimit=prim.style.miterLimit;
        geom::strokePolyline(prim.points,so,&out->stroke,0);out->worldBounds=prim.bounds;return;
    }

    // Flattening tolerance: one device pixel expressed in document units. This is
    // the ONLY way the view influences WORLD geometry, and it is a monotone
    // refinement (smaller tolerance -> more segments), not a re-interpretation.
    const double px = view.transform.docPerDevice();
    geom::FlattenOptions flatten;
    flatten.tolerance = px > 0.0 ? px * 0.35 : 0.05;
    if (!(flatten.tolerance > 0.0)) flatten.tolerance = 0.05;
    flatten.maxSegments = 512;

    const double width = prim.style.strokeWidth > 0.0 ? prim.style.strokeWidth
                                                      : view.defaultStrokeWidth;

    switch (prim.kind) {
        case PrimKind::Text: {
            if (prim.text.empty() || !measurer) break;
            const core::Vec2 baseline = prim.points.empty() ? core::Vec2(0, 0) : prim.points[0];
            geom::Mesh textMesh;
            core::Box ink;
            double advance = 0.0;
            const size_t before = measurer->sampleCount();
            const size_t beforeReuse = measurer->reuseCount();
            core::Error e = geom::layoutText(prim.text, baseline, prim.textSize,
                                             prim.fontName, const_cast<geom::TextMeasurer*>(measurer),
                                             &textMesh, &ink, &advance);
            if (glyphSamples) *glyphSamples += measurer->sampleCount() - before;
            if (glyphReuses) *glyphReuses += measurer->reuseCount() - beforeReuse;
            if (!e.ok()) break;
            out->fill.triangles = textMesh.triangles;
            out->fill.hasBounds = true;
            out->fill.bounds = ink;
            out->worldBounds = ink;
            return;
        }
        case PrimKind::Marker: {
            if (prim.points.empty()) break;
            const double size = prim.size > 0.0 ? prim.size : 2.0;
            if (prim.style.stroke.enabled && prim.marker != MarkerShape::Cross &&
                prim.marker != MarkerShape::Ring) {
                // Outline-only markers (a hover ring) are drawn as a thin ring.
                geom::Mesh ring;
                std::vector<core::Vec2> outline;
                bool has = false;
                geom::markerGeometry(prim.points[0], prim.marker, size, &ring, &has, &outline);
                if (has && outline.size() >= 2) {
                    geom::StrokeOptions so;
                    so.width = prim.style.strokeWidth > 0.0 ? prim.style.strokeWidth : 0.6;
                    so.closed = true;
                    geom::strokePolyline(outline, so, &out->stroke, 0);
                }
                if (prim.style.fill.enabled) out->fill = ring;
            } else {
                geom::Mesh m;
                bool has = false;
                std::vector<core::Vec2> outline;
                geom::markerGeometry(prim.points[0], prim.marker, size, &m, &has, &outline);
                out->fill = m;
                if (prim.style.stroke.enabled && has && outline.size() >= 2) {
                    geom::StrokeOptions so;
                    so.width = prim.style.strokeWidth > 0.0 ? prim.style.strokeWidth : 0.6;
                    so.closed = true;
                    geom::strokePolyline(outline, so, &out->stroke, 0);
                }
            }
            out->worldBounds = prim.bounds;
            return;
        }
        case PrimKind::Arrowhead: {
            if (prim.points.size() < 2) break;
            const double len = prim.size > 0.0 ? prim.size : 2.0;
            const geom::ArrowHeadGeometry g = geom::arrowHeadGeometry(
                prim.points[0], prim.points[1], prim.arrow, len, 0.5);
            if (g.filled && g.outline.size() >= 3) {
                std::vector<std::vector<core::Vec2> > cs;
                cs.push_back(g.outline);
                geom::triangulateContours(cs, FillRule::NonZero, &out->fill);
            }
            if (!g.strokes.empty()) {
                geom::StrokeOptions so;
                so.width = width > 0.0 ? width : 0.6;
                so.cap = CapStyle::Round;
                for (size_t i = 0; i + 1 < g.strokes.size(); i += 2) {
                    std::vector<core::Vec2> seg;
                    seg.push_back(g.strokes[i]);
                    seg.push_back(g.strokes[i + 1]);
                    geom::strokePolyline(seg, so, &out->stroke, 0);
                }
            }
            out->worldBounds = prim.bounds;
            return;
        }
        default:
            break;
    }

    // Path-like primitives: Polygon fills (with holes), Curve/Polyline/Segment
    // strokes. A bond with order 2/3 arrives as a Polyline with subpaths.
    std::vector<std::vector<core::Vec2> > contours;
    // Closure is tracked per contour. A Polygon outline arrives as an open point
    // list (no duplicated first point), so the stroke pass below has to close the
    // loop itself; forcing every stroke open dropped the last edge (the TLC
    // plate's left border) and any other closed polygon outline.
    std::vector<uint8_t> contourClosed;
    bool isFill = false;

    if (prim.kind == PrimKind::Polygon && !prim.subpaths.empty()) {
        for (size_t s = 0; s < prim.subpaths.size(); ++s) {
            std::vector<core::Vec2> pts;
            bool closed = false;
            Primitive sub = prim;
            sub.kind = PrimKind::Polyline;
            sub.points = prim.subpaths[s].points;
            sub.pointKind = prim.subpaths[s].kind;
            if (geom::flattenPath(sub, flatten, &pts, &closed) && pts.size() >= 3) {
                contours.push_back(pts);
                contourClosed.push_back(prim.subpaths[s].closed ? 1u : 0u);
            } else if (pts.size() >= 2) {
                contours.push_back(pts);
                contourClosed.push_back(prim.subpaths[s].closed ? 1u : 0u);
            }
        }
        isFill = prim.style.fill.enabled && contours.size() > 0;
    } else {
        std::vector<core::Vec2> pts;
        bool closed = false;
        if (prim.kind == PrimKind::Polyline && !prim.subpaths.empty()) {
            for (size_t s = 0; s < prim.subpaths.size(); ++s) {
                Primitive sub = prim;
                sub.kind = PrimKind::Polyline;
                sub.points = prim.subpaths[s].points;
                std::vector<core::Vec2> sp;
                bool sc = false;
                if (geom::flattenPath(sub, flatten, &sp, &sc)) {
                    geom::StrokeOptions so;
                    so.width = width;
                    so.cap = prim.style.cap;
                    so.join = prim.style.join;
                    so.miterLimit = prim.style.miterLimit;
                    so.closed = false;
                    std::vector<std::vector<core::Vec2> > paths;
                    if (!prim.style.dash.solid())
                        geom::applyDash(sp, false, prim.style.dash, width, &paths);
                    else
                        paths.push_back(sp);
                    for (size_t k = 0; k < paths.size(); ++k) {
                        std::vector<core::Vec2> use = paths[k];
                        if (prim.style.wavyAmplitude > 0.0) {
                            std::vector<core::Vec2> waved;
                            geom::applyWave(use, false, prim.style.wavyAmplitude,
                                            prim.style.wavyWavelength, flatten.tolerance, &waved);
                            use.swap(waved);
                        }
                        geom::strokePolyline(use, so, &out->stroke, 0);
                    }
                }
            }
            out->worldBounds = prim.bounds;
            return;
        }
        if (!geom::flattenPath(prim, flatten, &pts, &closed)) {
            out->worldBounds = prim.bounds;
            return;
        }
        if (pts.size() >= 2) contours.push_back(pts);
        contourClosed.push_back(closed ? 1u : 0u);
        // A closed path with a requested fill is filled, whatever primitive kind
        // produced it. Testing only for PrimKind::Polygon left a Rect (and a closed
        // Polyline) stroked but hollow, which looks exactly like 'the shape is
        // missing' when the caller asked for a fill and no stroke.
        isFill = prim.style.fill.enabled && closed && pts.size() >= 3;
    }

    if (contours.empty()) {
        out->worldBounds = prim.bounds;
        return;
    }

    if (isFill) {
        geom::triangulateContours(contours, prim.style.fillRule, &out->fill);
    }
    if (prim.style.stroke.enabled) {
        geom::StrokeOptions so;
        so.width = width;
        so.cap = prim.style.cap;
        so.join = prim.style.join;
        so.miterLimit = prim.style.miterLimit;
        for (size_t c = 0; c < contours.size(); ++c) {
            const bool contourIsClosed = c < contourClosed.size() && contourClosed[c] != 0;
            std::vector<std::vector<core::Vec2> > paths;
            if (!prim.style.dash.solid())
                geom::applyDash(contours[c], contourIsClosed && contours[c].size() >= 3,
                                prim.style.dash, width, &paths);
            else
                paths.push_back(contours[c]);
            for (size_t k = 0; k < paths.size(); ++k) {
                std::vector<core::Vec2> use = paths[k];
                if (prim.style.wavyAmplitude > 0.0) {
                    std::vector<core::Vec2> waved;
                    geom::applyWave(use, false, prim.style.wavyAmplitude,
                                    prim.style.wavyWavelength, flatten.tolerance, &waved);
                    use.swap(waved);
                }
                // A dashed closed loop is split into open runs by applyDash, so the
                // closure only applies when we are stroking the original contour.
                so.closed = contourIsClosed && prim.style.dash.solid();
                geom::strokePolyline(use, so, &out->stroke, 0);
            }
        }
    }
    out->worldBounds = prim.bounds;
}

Batcher::Key Batcher::keyFor(const Primitive& p, uint32_t sceneSlot, double viewScale) const {
    Key key;
    key.kind = static_cast<int>(p.kind);
    if (p.cacheKey) {
        key.sceneSlot = sceneSlot;
        key.objectKind = 0u;
        key.objectId = 0;
        key.ordinal = static_cast<int64_t>(p.cacheKey);
    } else if (p.source.valid()) {
        key.sceneSlot = 0u;
        key.objectKind = static_cast<uint32_t>(p.source.kind);
        key.objectId = p.source.id;
        key.ordinal = 0;
    } else {
        key.sceneSlot = sceneSlot;
        key.objectKind = 0u;
        key.objectId = 0;
        key.ordinal = static_cast<int64_t>(primitiveSignature(p, viewScale));
    }
    return key;
}

size_t Batcher::sync(const Scene& scene, const View& view, const geom::TextMeasurer* measurer,
                     BatchStats* stats, bool directContours, bool updateIndex) {
    const uint32_t sceneSlot = slotFor(scene);
    // Per-sync counters are RESET, not accumulated. A caller that reuses one
    // BatchStats across frames (the normal case) must see this frame's numbers;
    // accumulating them silently turns '0 rebuilds' into 'N rebuilds' and would
    // make the no-rebuild guarantee unobservable.
    if (stats) {
        stats->meshRebuilds = 0;
        stats->meshReuses = 0;
        stats->bvhRebuilds = 0;
        stats->glyphSamples = 0;
        stats->glyphReuses = 0;
    }
    const std::vector<Primitive>& prims = scene.primitives();

    // This scene's own cache entry. Every read and write below goes through it,
    // so syncing another scene can never disturb this one's meshes or BVH.
    SceneCache& sceneCache = cacheFor(sceneSlot);
    if(sceneCache.directContours!=directContours){sceneCache.meshes.clear();sceneCache.directContours=directContours;}

    // --- 1. Bring THIS scene's mesh set up to date, IN PLACE -----------------
    //
    // The cache is updated in place rather than rebuilt into a fresh map and
    // swapped. Rebuilding paid an insert and a node allocation for EVERY primitive
    // on EVERY frame, including frames where nothing changed at all - a steady-state
    // drag frame over 5000 atoms cost hundreds of milliseconds for that alone. The
    // in-place form does one `find` per primitive and touches a node only when the
    // primitive is new or actually changed.
    size_t rebuilds = 0;
    size_t reuses = 0;
    bool insertedKey = false;
    std::vector<core::Box> newBoxes;
    newBoxes.reserve(prims.size());
    std::vector<Key> seen;
    seen.reserve(prims.size());

    const double meshViewScale = view.transform.scale * view.transform.dpiScale;
    for (size_t i = 0; i < prims.size(); ++i) {
        const Primitive& p = prims[i];
        // Stable cacheKey for gpuScene primitives; source key for overlay;
        // content signature for anonymous primitives.
        uint64_t contentSignature = p.cacheKey ? 0 : (p.source.valid() ? 0 : primitiveSignature(p, meshViewScale));
        Key key = keyFor(p, sceneSlot, meshViewScale);
        seen.push_back(key);

        std::map<Key, CachedMesh>::iterator it = sceneCache.meshes.find(key);
        uint64_t signature = contentSignature;
        bool reuse = false;
        if (it != sceneCache.meshes.end() && it->second.valid) {
            if (it->second.version == p.version && p.kind != PrimKind::Curve) {
                reuse = true;
            } else {
                // A document-wide serial bump changes every primitive's version
                // even when its geometry is untouched. Compare actual content.
                if (!signature) signature = primitiveSignature(p, meshViewScale);
                if (it->second.signature == signature) {
                    it->second.version = p.version;
                    reuse = true;
                }
            }
        }
        if (reuse) {
            ++reuses;
            // `worldBounds` is view-INDEPENDENT for every primitive kind except a
            // curve, whose flattening tolerance comes from the view. It is therefore
            // safe to use as the BVH key across view changes, which is what makes
            // 'a pan rebuilds no BVH' true.
            newBoxes.push_back(it->second.worldBounds);
            continue;
        }
        // A new or dirty primitive: build it and store the one node.
        CachedMesh mesh;
        if (!signature) signature = primitiveSignature(p, meshViewScale);
        if(directContours&&directContourPrimitive(p)){mesh.version=p.version;mesh.signature=signature;mesh.valid=true;mesh.worldBounds=p.bounds;}
        else buildPrimitiveMeshes(p, view, measurer, &mesh,
                             stats ? &stats->glyphSamples : 0,
                             stats ? &stats->glyphReuses : 0);
        mesh.signature = signature;
        ++rebuilds;
        const core::Box b = mesh.worldBounds;
        if (it != sceneCache.meshes.end()) it->second = mesh;      // assign, no alloc
        else {sceneCache.meshes.insert(std::make_pair(key, mesh));insertedKey=true;}  // one alloc
        newBoxes.push_back(b);
    }

    // --- 1b. Drop entries whose object no longer exists (structure change) ----
    // Only worth walking when the key count can have shrunk, so a steady-state frame
    // never pays for it.
    // If every input key already existed and the counts match, no key can
    // have disappeared. Sparse geometry edits need no sort/prune pass.
    if (sceneCache.meshes.size() >= seen.size() &&
        (insertedKey || sceneCache.meshes.size() != seen.size())) {
        std::sort(seen.begin(), seen.end());
        for (std::map<Key, CachedMesh>::iterator it = sceneCache.meshes.begin();
             it != sceneCache.meshes.end(); ) {
            if (!std::binary_search(seen.begin(), seen.end(), it->first))
                sceneCache.meshes.erase(it++);
            else
                ++it;
        }
    }

    // --- 2. Rebuild the BVH only when the box set actually changed -----------
    bool boxesChanged = !sceneCache.built || (newBoxes.size() != sceneCache.boxes.size());
    if (!boxesChanged) {
        for (size_t i = 0; i < newBoxes.size(); ++i) {
            if (!(newBoxes[i].min == sceneCache.boxes[i].min) ||
                !(newBoxes[i].max == sceneCache.boxes[i].max)) {
                boxesChanged = true;
                break;
            }
        }
    }
    if (updateIndex && boxesChanged) {
        sceneCache.boxes = newBoxes;
        sceneCache.index.rebuild(sceneCache.boxes);
        sceneCache.built = true;
        ++indexSignature_;
        if (stats) ++stats->bvhRebuilds;
    }

    if (stats) {
        stats->meshRebuilds = rebuilds;
        stats->meshReuses = reuses;
        stats->cachedMeshes = sceneCache.meshes.size();
    }
    return rebuilds;
}

bool Batcher::syncSparse(const Scene& scene,const View& view,const std::vector<uint8_t>& dirty,BatchStats* stats) {
    const auto& prims=scene.primitives();if(dirty.size()!=prims.size())return false;
    const uint32_t slot=slotFor(scene);SceneCache& cache=cacheFor(slot);
    if(cache.directContours||cache.meshes.empty())return false;
    const double scale=view.transform.scale*view.transform.dpiScale;
    for(size_t i=0;i<prims.size();++i)if(dirty[i]&&cache.meshes.find(keyFor(prims[i],slot,scale))==cache.meshes.end())return false;
    if(stats)*stats=BatchStats();
    for(size_t i=0;i<prims.size();++i)if(dirty[i]){
        auto& mesh=cache.meshes.find(keyFor(prims[i],slot,scale))->second;
        buildPrimitiveMeshes(prims[i],view,nullptr,&mesh,stats?&stats->glyphSamples:nullptr,stats?&stats->glyphReuses:nullptr);
        mesh.signature=primitiveSignature(prims[i],scale);if(stats)++stats->meshRebuilds;
    }
    cache.built=false;
    if(stats)stats->cachedMeshes=cache.meshes.size();
    return true;
}

void Batcher::accumulate(BatchStats* into, const BatchStats& from) {
    if (!into) return;
    into->meshRebuilds += from.meshRebuilds;
    into->meshReuses += from.meshReuses;
    into->bvhRebuilds += from.bvhRebuilds;
    into->glyphSamples += from.glyphSamples;
    into->glyphReuses += from.glyphReuses;
    into->cachedMeshes = from.cachedMeshes;
}

void Batcher::prune(const Scene& scene) {
    // sync() already rebuilds the key set exactly, so prune is a no-op hook kept
    // for callers that want to drop everything without a full sync.
    (void)scene;
}

void Batcher::appendDevice(const geom::Mesh& world, const core::ViewTransform& t,
                           double ox, double oy, bool lines, Batch* batch, uint32_t rgba,
                           uint8_t primKind) {
    const std::vector<float>& src = lines ? world.lines : world.triangles;
    if (src.empty()) return;
    const size_t perVertex = 2;
    const size_t vertexCount = src.size() / perVertex;
    DrawRange range;
    range.rgba = rgba;
    range.primKind = primKind;
    range.triangles = !lines;

    // NOTE: no per-primitive reserve() here. Reserving EXACTLY size()+src.size()
    // defeats std::vector's geometric growth, so every primitive reallocates and
    // copies the whole accumulated buffer - appending N primitives becomes
    // quadratic. At 50k primitives that alone cost ~76 seconds for one batch build,
    // while the next build on the same scene cost 26ms. buildBatch() reserves once
    // for the whole frame before entering the loop instead.
    // The transform is a single uniform scale plus a translation.  Hoisting
    // scale/offset out of the per-vertex loop removes two multiplies and a
    // subtraction per coordinate; this loop is the dominant cost of a
    // pan/zoom frame on a large document.
    const double scale = t.scale * t.dpiScale;
    const double tx = -t.center.x * scale + t.viewportCenter.x + ox;
    const double ty = -t.center.y * scale + t.viewportCenter.y + oy;
    if (lines) {
        range.first = static_cast<int>(batch->lineVertices.size() / 2);
        range.count = static_cast<int>(vertexCount);
        for (size_t i = 0; i < vertexCount; ++i) {
            batch->lineVertices.push_back(static_cast<float>(src[i * 2] * scale + tx));
            batch->lineVertices.push_back(static_cast<float>(src[i * 2 + 1] * scale + ty));
        }
        batch->lineRanges.push_back(range);
    } else {
        range.first = static_cast<int>(batch->triangleVertices.size() / 2);
        range.count = static_cast<int>(vertexCount);
        for (size_t i = 0; i < vertexCount; ++i) {
            batch->triangleVertices.push_back(static_cast<float>(src[i * 2] * scale + tx));
            batch->triangleVertices.push_back(static_cast<float>(src[i * 2 + 1] * scale + ty));
        }
        batch->triangleRanges.push_back(range);
    }
}

bool Batcher::updateBatchInPlace(const Scene& scene, const View& view,
                                 const std::vector<uint8_t>& dirty, Batch* batch,
                                 BatchStats* stats) {
    if (!batch || dirty.size() != scene.primitives().size()) return false;
    const std::vector<Primitive>& prims = scene.primitives();
    const uint32_t sceneSlot = slotFor(scene);
    std::map<uint32_t, SceneCache>::const_iterator si = scenes_.find(sceneSlot);
    if (si == scenes_.end()) return false;
    std::map<int, std::vector<size_t>> oldRanges;
    for (size_t k = 0; k < batch->triangleRanges.size(); ++k)
        if (batch->triangleRanges[k].primitiveIndex >= 0 &&
            size_t(batch->triangleRanges[k].primitiveIndex) < dirty.size() &&
            dirty[size_t(batch->triangleRanges[k].primitiveIndex)])
            oldRanges[batch->triangleRanges[k].primitiveIndex].push_back(k);

    int rebuilt = 0;
    for (size_t i = 0; i < prims.size(); ++i) {
        if (!dirty[i]) continue;
        const Primitive& p = prims[i];
        Key key = keyFor(p, sceneSlot, view.transform.scale * view.transform.dpiScale);
        std::map<Key, CachedMesh>::const_iterator it = si->second.meshes.find(key);
        if (it == si->second.meshes.end()) return false;
        const CachedMesh& mesh = it->second;
        Batch fresh;
        if (p.style.fill.enabled && !mesh.fill.triangles.empty()) {
            appendDevice(mesh.fill, view.transform, originX_, originY_, false, &fresh,
                         p.style.fill.rgba, static_cast<uint8_t>(p.kind));
            fresh.triangleRanges.back().primitiveIndex = int(i);
        }
        if (p.style.stroke.enabled && !mesh.stroke.triangles.empty()) {
            appendDevice(mesh.stroke, view.transform, originX_, originY_, false, &fresh,
                         p.style.stroke.rgba, static_cast<uint8_t>(p.kind));
            fresh.triangleRanges.back().primitiveIndex = int(i);
        }
        std::map<int, std::vector<size_t>>::const_iterator old = oldRanges.find(int(i));
        if (fresh.triangleRanges.empty() && old == oldRanges.end()) continue;
        if (old == oldRanges.end() || old->second.size() != fresh.triangleRanges.size()) return false;
        for (size_t k = 0; k < fresh.triangleRanges.size(); ++k) {
            const DrawRange& src = fresh.triangleRanges[k];
            DrawRange& dst = batch->triangleRanges[old->second[k]];
            if (dst.count != src.count || dst.first < 0) return false;
            std::memcpy(&batch->triangleVertices[size_t(dst.first) * 2],
                        &fresh.triangleVertices[size_t(src.first) * 2],
                        size_t(src.count) * 2 * sizeof(float));
            dst.rgba = src.rgba;
            dst.primKind = src.primKind;
        }
        ++rebuilt;
    }
    if (stats) {
        stats->viewRebuilds = size_t(rebuilt);
        stats->triangles = batch->triangleCount();
        stats->drawCalls = batch->triangleRanges.size() + batch->lineRanges.size();
    }
    return true;
}

bool Batcher::syncDirty(const Scene& scene, const View& view,
                           const std::vector<uint8_t>& dirty, BatchStats* stats) {
    const std::vector<Primitive>& prims = scene.primitives();
    if (dirty.size() != prims.size()) return false;
    const uint32_t slot = slotFor(scene);
    SceneCache& cache = cacheFor(slot);
    const double scale = view.transform.scale * view.transform.dpiScale;
    size_t rebuilt = 0;
    for (size_t i = 0; i < prims.size(); ++i) {
        if (!dirty[i]) continue;
        const Primitive& p = prims[i];
        CachedMesh mesh;
        buildPrimitiveMeshes(p, view, nullptr, &mesh,
                             stats ? &stats->glyphSamples : nullptr,
                             stats ? &stats->glyphReuses : nullptr);
        mesh.signature = primitiveSignature(p, scale);
        mesh.valid = true;
        cache.meshes[keyFor(p, slot, scale)] = std::move(mesh);
        ++rebuilt;
    }
    // The BVH does not describe the new box set; a full sync rebuilds it.
    cache.built = false;
    if (stats) {
        stats->meshRebuilds = rebuilt;
        stats->cachedMeshes = cache.meshes.size();
    }
    return true;
}

bool Batcher::updateBatchStructural(const Scene& scene, const View& view,
                                    const std::vector<uint8_t>& dirty,
                                    const std::vector<uint64_t>& previousKeys,
                                    Batch* batch, size_t* appendedFrom, BatchStats* stats) {
    if (!batch) return false;
    const std::vector<Primitive>& prims = scene.primitives();
    if (dirty.size() != prims.size()) return false;
    if (previousKeys.size() != batch->primitiveCount) return false;
    const size_t oldCount = previousKeys.size();
    if (!oldCount) return false;

    std::unordered_map<uint64_t, size_t> oldIndex;
    oldIndex.reserve(oldCount * 2);
    for (size_t i = 0; i < oldCount; ++i) {
        if (!previousKeys[i]) return false;
        if (!oldIndex.emplace(previousKeys[i], i).second) return false;
    }
    std::vector<int> rangeBegin(oldCount, -1);
    for (size_t r = 0; r < batch->triangleRanges.size(); ++r) {
        const size_t p = batch->triangleRanges[r].primitiveIndex;
        if (p < oldCount && rangeBegin[p] < 0) rangeBegin[p] = int(r);
    }

    const uint32_t slot = slotFor(scene);
    std::map<uint32_t, SceneCache>::const_iterator si = scenes_.find(slot);
    const double scale = view.transform.scale * view.transform.dpiScale;
    // Verify the dirty meshes first: a partial append must never be left behind.
    if (si == scenes_.end()) return false;
    for (size_t i = 0; i < prims.size(); ++i)
        if (dirty[i] && si->second.meshes.find(keyFor(prims[i], slot, scale)) == si->second.meshes.end())
            return false;

    const auto tStart = std::chrono::steady_clock::now();
    const size_t appendFrom = batch->triangleVertices.size() / 2;
    std::vector<DrawRange> ranges;
    ranges.reserve(batch->triangleRanges.size() + 16);
    size_t rebuilt = 0, liveVertices = 0;
    for (size_t i = 0; i < prims.size(); ++i) {
        const Primitive& p = prims[i];
        std::unordered_map<uint64_t, size_t>::const_iterator old = oldIndex.end();
        if (p.cacheKey) old = oldIndex.find(p.cacheKey);
        if (!dirty[i] && old != oldIndex.end() && rangeBegin[old->second] >= 0) {
            // Keep the device vertices exactly where they are: only the scene
            // order of the range list changes, never the vertex array.
            for (int r = rangeBegin[old->second]; r < int(batch->triangleRanges.size()); ++r) {
                const DrawRange& src = batch->triangleRanges[size_t(r)];
                if (src.primitiveIndex != old->second) break;
                if (src.count <= 0 || src.first < 0) continue;
                DrawRange range = src;
                range.primitiveIndex = i;
                liveVertices += size_t(range.count);
                ranges.push_back(range);
            }
            continue;
        }
        ++rebuilt;
        if (si == scenes_.end()) continue;
        std::map<Key, CachedMesh>::const_iterator it = si->second.meshes.find(keyFor(p, slot, scale));
        if (it == si->second.meshes.end()) continue;
        const CachedMesh& mesh = it->second;
        Batch fresh;
        if (p.style.fill.enabled && !mesh.fill.triangles.empty()) {
            appendDevice(mesh.fill, view.transform, originX_, originY_, false, &fresh,
                         p.style.fill.rgba, static_cast<uint8_t>(p.kind));
            fresh.triangleRanges.back().primitiveIndex = i;
        }
        if (p.style.stroke.enabled && !mesh.stroke.triangles.empty()) {
            appendDevice(mesh.stroke, view.transform, originX_, originY_, false, &fresh,
                         p.style.stroke.rgba, static_cast<uint8_t>(p.kind));
            fresh.triangleRanges.back().primitiveIndex = i;
        }
        if (fresh.triangleVertices.empty()) continue;
        const size_t at = batch->triangleVertices.size();
        batch->triangleVertices.insert(batch->triangleVertices.end(),
                                       fresh.triangleVertices.begin(), fresh.triangleVertices.end());
        for (size_t k = 0; k < fresh.triangleRanges.size(); ++k) {
            DrawRange range = fresh.triangleRanges[k];
            range.first += int(at / 2);
            range.primitiveIndex = i;
            liveVertices += size_t(range.count);
            ranges.push_back(range);
        }
    }
    batch->triangleRanges.swap(ranges);
    batch->primitiveCount = prims.size();
    if (appendedFrom) *appendedFrom = appendFrom;
    if (stats) {
        stats->viewRebuilds = rebuilt;
        stats->drawn = prims.size();
        stats->triangles = liveVertices / 3;
        stats->drawCalls = batch->triangleRanges.size() + batch->lineRanges.size();
    }
    if (std::getenv("MOLECULE_STUDIO_PERF_TRACE")) {
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tStart).count();
        std::fprintf(stderr, "[struct] prims=%zu rebuilt=%zu appended=%zu total=%zu live=%zu ms=%.1f\n",
                     prims.size(), rebuilt, batch->triangleVertices.size() / 2 - appendFrom,
                     batch->triangleVertices.size() / 2, liveVertices, ms);
    }
    return true;
}

bool Batcher::rebuildBatchReusing(const Scene& scene, const View& view,
                                  const std::vector<uint8_t>& dirty,
                                  const std::vector<uint64_t>& previousKeys,
                                  const Batch& previous, Batch* batch,
                                  size_t* firstChangedVertex, BatchStats* stats) {
    if (!batch) return false;
    const std::vector<Primitive>& prims = scene.primitives();
    if (dirty.size() != prims.size()) return false;
    if (previousKeys.size() != previous.primitiveCount) return false;
    const size_t oldCount = previousKeys.size();
    if (!oldCount) return false;

    // Stable cache keys make a primitive's device vertices reusable even when
    // inserting or deleting objects shifted every later primitive's index.
    std::unordered_map<uint64_t, size_t> oldIndex;
    oldIndex.reserve(oldCount * 2);
    for (size_t i = 0; i < oldCount; ++i) {
        if (!previousKeys[i]) return false;
        if (!oldIndex.emplace(previousKeys[i], i).second) return false;
    }
    // Ranges of one primitive are contiguous and primitives are in scene order.
    std::vector<int> rangeBegin(oldCount, -1);
    for (size_t r = 0; r < previous.triangleRanges.size(); ++r) {
        const size_t p = previous.triangleRanges[r].primitiveIndex;
        if (p < oldCount && rangeBegin[p] < 0) rangeBegin[p] = int(r);
    }

    const uint32_t sceneSlot = slotFor(scene);
    std::map<uint32_t, SceneCache>::const_iterator si = scenes_.find(sceneSlot);
    batch->clear();
    batch->triangleVertices.reserve(previous.triangleVertices.size() + 4096);
    batch->triangleRanges.reserve(previous.triangleRanges.size() + 16);

    const float* base = previous.triangleVertices.data();
    const size_t oldVertices = previous.triangleVertices.size() / 2;
    // Contiguous unchanged spans are copied with one memcpy instead of one call
    // per range. On a large page that turns ~90000 small copies into a handful.
    size_t runOld = 0, runCount = 0, runDest = 0;
    auto flushRun = [&]() {
        if (!runCount) return;
        batch->triangleVertices.insert(batch->triangleVertices.end(),
                                       base + runOld * 2, base + (runOld + runCount) * 2);
        runCount = 0;
    };

    auto tStart = std::chrono::steady_clock::now();
    size_t copyCalls = 0;
    size_t changed = 0, dirtyCount = 0, firstChanged = size_t(-1);
    for (size_t i = 0; i < prims.size(); ++i) {
        const Primitive& p = prims[i];
        std::unordered_map<uint64_t, size_t>::const_iterator old = oldIndex.end();
        if (p.cacheKey) old = oldIndex.find(p.cacheKey);
        const bool reuse = !dirty[i] && old != oldIndex.end() && rangeBegin[old->second] >= 0;
        size_t newFirst = size_t(-1);
        if (reuse) {
            const int begin = rangeBegin[old->second];
            for (int r = begin; r < int(previous.triangleRanges.size()); ++r) {
                const DrawRange& src = previous.triangleRanges[size_t(r)];
                if (src.primitiveIndex != old->second) break;
                if (src.count <= 0 || src.first < 0) continue;
                if (size_t(src.first) + size_t(src.count) > oldVertices) return false;
                if (!runCount || runOld + runCount != size_t(src.first)) {
                    flushRun();
                    runOld = size_t(src.first);
                    runDest = batch->triangleVertices.size() / 2;
                    ++copyCalls;
                }
                if (newFirst == size_t(-1)) newFirst = runDest + size_t(src.first) - runOld;
                DrawRange range = src;
                range.first = int(runDest + size_t(src.first) - runOld);
                range.primitiveIndex = i;
                batch->triangleRanges.push_back(range);
                runCount += size_t(src.count);
            }
            if (newFirst == size_t(-1)) newFirst = runDest + runCount;
        } else {
            ++changed;
            if (dirty[i]) ++dirtyCount;
            flushRun();
            if (si == scenes_.end()) continue;
            Key key = keyFor(p, sceneSlot, view.transform.scale * view.transform.dpiScale);
            std::map<Key, CachedMesh>::const_iterator it = si->second.meshes.find(key);
            if (it == si->second.meshes.end()) continue;
            const CachedMesh& mesh = it->second;
            newFirst = batch->triangleVertices.size() / 2;
            if (p.style.fill.enabled && !mesh.fill.triangles.empty()) {
                appendDevice(mesh.fill, view.transform, originX_, originY_, false, batch,
                             p.style.fill.rgba, static_cast<uint8_t>(p.kind));
                batch->triangleRanges.back().primitiveIndex = i;
            }
            if (p.style.stroke.enabled && !mesh.stroke.triangles.empty()) {
                appendDevice(mesh.stroke, view.transform, originX_, originY_, false, batch,
                             p.style.stroke.rgba, static_cast<uint8_t>(p.kind));
                batch->triangleRanges.back().primitiveIndex = i;
            }
            if (newFirst == size_t(-1)) newFirst = batch->triangleVertices.size() / 2;
        }
        // A primitive whose device position moved forces everything after it in
        // the vertex buffer to be refreshed on the device.
        size_t oldFirst = size_t(-1);
        if (old != oldIndex.end() && rangeBegin[old->second] >= 0) {
            for (int r = rangeBegin[old->second]; r < int(previous.triangleRanges.size()); ++r) {
                const DrawRange& src = previous.triangleRanges[size_t(r)];
                if (src.primitiveIndex != old->second) break;
                if (src.count > 0 && src.first >= 0) { oldFirst = size_t(src.first); break; }
            }
        }
        if (newFirst != size_t(-1) && (dirty[i] || oldFirst != newFirst) && newFirst < firstChanged)
            firstChanged = newFirst;
    }
    flushRun();
    batch->primitiveCount = prims.size();
    const size_t total = batch->triangleVertices.size() / 2;
    if (std::getenv("MOLECULE_STUDIO_PERF_TRACE")) {
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tStart).count();
        std::fprintf(stderr, "[batch] prims=%zu changed=%zu dirty=%zu calls=%zu copy=%.1fMB first=%zu ms=%.1f\n",
                     prims.size(), changed, dirtyCount, copyCalls,
                     double(batch->triangleVertices.size() * sizeof(float)) / (1024.0 * 1024.0),
                     firstChanged == size_t(-1) ? 0 : firstChanged, ms);
    }
    if (firstChanged == size_t(-1)) firstChanged = std::min(total, previous.triangleVertices.size() / 2);
    if (firstChangedVertex) *firstChangedVertex = firstChanged;

    if (stats) {
        stats->viewRebuilds = changed;
        stats->drawn = prims.size();
        stats->triangles = batch->triangleCount();
        stats->drawCalls = batch->triangleRanges.size() + batch->lineRanges.size();
    }
    return true;
}

void Batcher::buildBatch(const Scene& scene, const View& view, bool force, Batch* batch,
                         BatchStats* stats, bool directContours, bool cull,
                         const Batch* previous, const std::vector<uint8_t>* dirty) {
    if (!batch) return;
    batch->clear();
    batch->primitiveCount = scene.primitives().size();

    const std::vector<Primitive>& prims = scene.primitives();
    const uint32_t sceneSlot = slotFor(scene);
    // The device batch is cached per (view, scene, primitive count): a different
    // scene must never reuse another scene's batch.
    const uint64_t vh = viewHash(view, prims.size()) ^ (static_cast<uint64_t>(sceneSlot) << 56);
    const bool viewChanged = force || !haveView_ || vh != lastViewHash_;
    lastViewHash_ = vh;
    haveView_ = true;

    // Device-space viewport used for culling, in document units.
    const core::Box worldView = core::Box(
        view.transform.toDocument(core::Vec2(0.0, 0.0)),
        view.transform.toDocument(core::Vec2(view.widthPx, view.heightPx)));
    core::Box padded = worldView;
    // Pad by the largest half-stroke we may encounter plus the head length, so a
    // primitive whose bounds straddle the edge still draws its stroke.
    padded.min.x -= 8.0; padded.min.y -= 8.0;
    padded.max.x += 8.0; padded.max.y += 8.0;

    std::vector<size_t> visible;
    // Query THIS scene's index and read THIS scene's meshes. A shared cache or a
    // shared index would let one scene's sync evict the other's geometry.
    std::map<uint32_t, SceneCache>::const_iterator si = scenes_.find(sceneSlot);

    // Incremental path for interactive edits: unchanged primitives keep their
    // previous device-space ranges; only dirty primitives are re-projected.
    // The view must be identical to the previous batch (the caller guarantees
    // that), so old vertices are still valid.
    if (previous && dirty && dirty->size() == prims.size() && !cull) {
        std::map<int, std::vector<const DrawRange*>> oldRanges;
        for (const auto& r : previous->triangleRanges) oldRanges[r.primitiveIndex].push_back(&r);
        batch->triangleVertices.reserve(previous->triangleVertices.size());
        size_t drawn = 0, rebuilt = 0;
        for (size_t i = 0; i < prims.size(); ++i) {
            const Primitive& p = prims[i];
            std::map<int, std::vector<const DrawRange*>>::const_iterator old = oldRanges.find(int(i));
            if (!(*dirty)[i] && old != oldRanges.end()) {
                for (const DrawRange* r : old->second) {
                    if (r->count <= 0 || r->first < 0) continue;
                    size_t begin = size_t(r->first) * 2;
                    size_t end = begin + size_t(r->count) * 2;
                    if (end > previous->triangleVertices.size()) continue;
                    DrawRange copy = *r;
                    copy.first = int(batch->triangleVertices.size() / 2);
                    batch->triangleVertices.insert(batch->triangleVertices.end(),
                        previous->triangleVertices.begin() + begin,
                        previous->triangleVertices.begin() + end);
                    batch->triangleRanges.push_back(copy);
                }
                ++drawn;
                continue;
            }
            if (si == scenes_.end()) continue;
            Key key = keyFor(p, sceneSlot, view.transform.scale * view.transform.dpiScale);
            std::map<Key, CachedMesh>::const_iterator it = si->second.meshes.find(key);
            if (it == si->second.meshes.end()) continue;
            const CachedMesh& mesh = it->second;
            ++drawn; ++rebuilt;
            if (p.style.fill.enabled && !mesh.fill.triangles.empty()) {
                appendDevice(mesh.fill, view.transform, originX_, originY_, false, batch,
                             p.style.fill.rgba, static_cast<uint8_t>(p.kind));
                batch->triangleRanges.back().primitiveIndex = int(i);
            }
            if (p.style.stroke.enabled && !mesh.stroke.triangles.empty()) {
                appendDevice(mesh.stroke, view.transform, originX_, originY_, false, batch,
                             p.style.stroke.rgba, static_cast<uint8_t>(p.kind));
                batch->triangleRanges.back().primitiveIndex = int(i);
            }
        }
        if (stats) {
            stats->viewRebuilds = rebuilt;
            stats->culled = prims.size() > drawn ? prims.size() - drawn : 0;
            stats->drawn = drawn;
            stats->triangles = batch->triangleCount();
            stats->drawCalls = batch->triangleRanges.size() + batch->lineRanges.size();
        }
        return;
    }

    if (cull) {
        if (si != scenes_.end() && si->second.built) si->second.index.query(padded, &visible);
    } else {
        visible.resize(prims.size());
        for (size_t i = 0; i < prims.size(); ++i) visible[i] = i;
    }

    // Reserve ONCE for the whole frame. Estimate the vertex count from the visible
    // primitives so the arrays grow at most logarithmically instead of once per
    // primitive. An over-estimate only costs memory; an under-estimate still works
    // because push_back may grow again.
    if (!visible.empty()) {
        size_t estimate = 0;
        for (size_t e = 0; e < visible.size(); ++e) {
            const size_t idx = visible[e];
            if (idx >= prims.size()) continue;
            Key k=keyFor(prims[idx],sceneSlot,view.transform.scale*view.transform.dpiScale);
            if (si == scenes_.end()) break;
            std::map<Key, CachedMesh>::const_iterator it = si->second.meshes.find(k);
            if (it == si->second.meshes.end()) continue;
            estimate += it->second.fill.triangles.size() + it->second.stroke.triangles.size();
        }
        // Headroom for the append-only structural path: unchanged vertices are
        // never shifted, changed ones are appended, so a little spare capacity
        // keeps interactive edits from reallocating the whole array.
        batch->triangleVertices.reserve(estimate + estimate / 4 + 4096);
        batch->triangleRanges.reserve(visible.size() * 2);
    }

    size_t drawn = 0;
    for (size_t vi = 0; vi < visible.size(); ++vi) {
        const size_t i = visible[vi];
        if (i >= prims.size()) continue;
        const Primitive& p = prims[i];
        Key key = keyFor(p, sceneSlot, view.transform.scale * view.transform.dpiScale);
        if (si == scenes_.end()) continue;
        std::map<Key, CachedMesh>::const_iterator it = si->second.meshes.find(key);
        if (it == si->second.meshes.end()) continue;
        const CachedMesh& mesh = it->second;
        ++drawn;
        if (directContours && directContourPrimitive(p)) {
            DrawRange range;range.rgba=p.style.fill.rgba;range.primKind=uint8_t(p.kind);range.primitiveIndex=i;batch->triangleRanges.push_back(range);
        } else if (p.style.fill.enabled && !mesh.fill.triangles.empty()) {
            appendDevice(mesh.fill, view.transform, originX_, originY_, false, batch,
                         p.style.fill.rgba, static_cast<uint8_t>(p.kind));
            batch->triangleRanges.back().primitiveIndex=i;
        }
        if (p.style.stroke.enabled && !mesh.stroke.triangles.empty()) {
            appendDevice(mesh.stroke, view.transform, originX_, originY_, false, batch,
                         p.style.stroke.rgba, static_cast<uint8_t>(p.kind));
            batch->triangleRanges.back().primitiveIndex = int(i);
        }
    }

    if (stats) {
        stats->viewRebuilds = viewChanged ? drawn : 0;
        // `culled` is the exact complement of `drawn`, so drawn + culled always
        // accounts for every primitive in the scene. Reporting
        // prims.size() - visible.size() instead would overcount whenever a
        // visible primitive had no cache entry, and that silent mismatch is
        // exactly the kind of accounting error a coverage claim must not have.
        stats->culled = prims.size() > drawn ? prims.size() - drawn : 0;
        stats->drawn = drawn;
        stats->triangles = batch->triangleCount();
        stats->drawCalls = batch->triangleRanges.size() + batch->lineRanges.size();
    }
}

} // namespace render
} // namespace r62
