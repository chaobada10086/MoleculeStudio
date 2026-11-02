// =============================================================================
// re_round62_viewer/next/render/batcher.cpp
//
// T03 - incremental mesh cache + device batching.
// =============================================================================

#include "next/render/batcher.hpp"

#include <algorithm>
#include <cmath>

namespace r62 {
namespace render {

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

size_t Batcher::sync(const Scene& scene, const View& view, const geom::TextMeasurer* measurer,
                     BatchStats* stats) {
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
    std::vector<core::Box> newBoxes;
    newBoxes.reserve(prims.size());
    std::vector<Key> seen;
    seen.reserve(prims.size());

    for (size_t i = 0; i < prims.size(); ++i) {
        const Primitive& p = prims[i];
        Key key;
        key.kind = static_cast<int>(p.kind);
        key.sceneSlot = p.source.valid() ? 0u : sceneSlot;
        key.objectKind = static_cast<uint32_t>(p.source.kind);
        key.objectId = p.source.id;
        key.ordinal = p.source.valid() ? 0 : static_cast<int64_t>(i);
        seen.push_back(key);

        std::map<Key, CachedMesh>::iterator it = sceneCache.meshes.find(key);
        if (it != sceneCache.meshes.end() && it->second.valid &&
            it->second.version == p.version) {
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
        buildPrimitiveMeshes(p, view, measurer, &mesh,
                             stats ? &stats->glyphSamples : 0,
                             stats ? &stats->glyphReuses : 0);
        ++rebuilds;
        const core::Box b = mesh.worldBounds;
        if (it != sceneCache.meshes.end()) it->second = mesh;      // assign, no alloc
        else sceneCache.meshes.insert(std::make_pair(key, mesh));  // one alloc
        newBoxes.push_back(b);
    }

    // --- 1b. Drop entries whose object no longer exists (structure change) ----
    // Only worth walking when the key count can have shrunk, so a steady-state frame
    // never pays for it.
    if (sceneCache.meshes.size() != seen.size()) {
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
    if (boxesChanged) {
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
    if (lines) {
        range.first = static_cast<int>(batch->lineVertices.size() / 2);
        range.count = static_cast<int>(vertexCount);
        for (size_t i = 0; i < vertexCount; ++i) {
            const core::Vec2 d = t.toDevice(core::Vec2(src[i * 2], src[i * 2 + 1]));
            batch->lineVertices.push_back(static_cast<float>(d.x + ox));
            batch->lineVertices.push_back(static_cast<float>(d.y + oy));
        }
        batch->lineRanges.push_back(range);
    } else {
        range.first = static_cast<int>(batch->triangleVertices.size() / 2);
        range.count = static_cast<int>(vertexCount);
        for (size_t i = 0; i < vertexCount; ++i) {
            const core::Vec2 d = t.toDevice(core::Vec2(src[i * 2], src[i * 2 + 1]));
            batch->triangleVertices.push_back(static_cast<float>(d.x + ox));
            batch->triangleVertices.push_back(static_cast<float>(d.y + oy));
        }
        batch->triangleRanges.push_back(range);
    }
}

void Batcher::buildBatch(const Scene& scene, const View& view, bool force, Batch* batch,
                         BatchStats* stats) {
    if (!batch) return;
    batch->clear();

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
    if (si != scenes_.end() && si->second.built) si->second.index.query(padded, &visible);

    // Reserve ONCE for the whole frame. Estimate the vertex count from the visible
    // primitives so the arrays grow at most logarithmically instead of once per
    // primitive. An over-estimate only costs memory; an under-estimate still works
    // because push_back may grow again.
    if (!visible.empty()) {
        size_t estimate = 0;
        for (size_t e = 0; e < visible.size(); ++e) {
            const size_t idx = visible[e];
            if (idx >= prims.size()) continue;
            Key k;
            k.kind = static_cast<int>(prims[idx].kind);
            k.sceneSlot = prims[idx].source.valid() ? 0u : sceneSlot;
            k.objectKind = static_cast<uint32_t>(prims[idx].source.kind);
            k.objectId = prims[idx].source.id;
            k.ordinal = prims[idx].source.valid() ? 0 : static_cast<int64_t>(idx);
            if (si == scenes_.end()) break;
            std::map<Key, CachedMesh>::const_iterator it = si->second.meshes.find(k);
            if (it == si->second.meshes.end()) continue;
            estimate += it->second.fill.triangles.size() + it->second.stroke.triangles.size();
        }
        batch->triangleVertices.reserve(estimate);
        batch->triangleRanges.reserve(visible.size() * 2);
    }

    size_t drawn = 0;
    for (size_t vi = 0; vi < visible.size(); ++vi) {
        const size_t i = visible[vi];
        if (i >= prims.size()) continue;
        const Primitive& p = prims[i];
        Key key;
        key.kind = static_cast<int>(p.kind);
        key.sceneSlot = p.source.valid() ? 0u : sceneSlot;
        key.objectKind = static_cast<uint32_t>(p.source.kind);
        key.objectId = p.source.id;
        key.ordinal = p.source.valid() ? 0 : static_cast<int64_t>(i);
        if (si == scenes_.end()) continue;
        std::map<Key, CachedMesh>::const_iterator it = si->second.meshes.find(key);
        if (it == si->second.meshes.end()) continue;
        const CachedMesh& mesh = it->second;
        ++drawn;
        if (p.style.fill.enabled && !mesh.fill.triangles.empty())
            appendDevice(mesh.fill, view.transform, originX_, originY_, false, batch,
                         p.style.fill.rgba, static_cast<uint8_t>(p.kind));
        if (p.style.stroke.enabled && !mesh.stroke.triangles.empty())
            appendDevice(mesh.stroke, view.transform, originX_, originY_, false, batch,
                         p.style.stroke.rgba, static_cast<uint8_t>(p.kind));
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
